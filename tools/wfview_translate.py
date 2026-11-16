#!/usr/bin/env python3
"""
Machine-assisted translation helper for wfview, using the Google Cloud
Translation API (v2 "Basic", API-key authentication). Standard library only.

Pipeline for one language:
  1. lupdate  - refresh translations/wfview_<LANG>.ts from src/, include/, qml/
  2. Cloud Translation - propose translations for the selected messages
  3. write the .ts file (then lupdate again to normalise formatting)
  4. lrelease - build translations/wfview_<LANG>.qm

Modes:
  interactive (default)  Review each string and its proposed translation.
  --bulk                 Translate everything selected without prompting.
  --all                  Update every translations/wfview_*.ts file (each
                         language and region) in bulk: one lupdate pass picks
                         up new source strings, only the strings that are
                         still empty are machine-translated, then all .qm
                         files are rebuilt. English files are only lupdated
                         and lreleased.

Existing translations are never re-sent: a new string whose exact English
text is already translated elsewhere in the same file reuses that
translation, and within one run each distinct string is sent to the API
only once per target language (so es, es_ES and es_MX share one request).

Machine translations written in bulk mode stay marked "unfinished" so a
human can review them later in Qt Linguist (lrelease still includes them
in the .qm). Strings you accept or edit in interactive mode are marked
finished. Use --mark-finished to mark bulk results finished too.

Placeholders (%1, %n, %L1), HTML tags, entities, all-caps technical terms
(PTT, VFO, CI-V, ...) and a few names/units are wrapped in
<span translate="no"> so the API leaves them alone; they are verified
afterwards. Keyboard accelerators (&File) are removed before translation
and put back on the first letter of the result.

API key lookup order:
  --key-file FILE, $GOOGLE_TRANSLATE_API_KEY, ~/Documents/projects/google/API_KEY

Examples:
  tools/wfview_translate.py de                     # review untranslated German strings
  tools/wfview_translate.py fr_FR --bulk           # new French file, translate everything
  tools/wfview_translate.py ja --bulk --scope unfinished
  tools/wfview_translate.py de --scope all         # re-review every German string
  tools/wfview_translate.py de --no-translate      # only lupdate + lrelease
  tools/wfview_translate.py --all                  # add new strings to every language
  tools/wfview_translate.py --all --dry-run        # show what --all would translate
  tools/wfview_translate.py --all --no-translate   # lupdate + lrelease every language
"""

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRO_FILE = REPO / "wfview.pro"
TS_DIR = REPO / "translations"
QRC_FILE = TS_DIR / "translation.qrc"

LUPDATE_DIRS = ["src", "include", "qml"]   # every tr()/qsTr()/.ui source in wfview.pro

DEFAULT_KEY_FILE =Path.home() / "Documents" / "projects" / "google" / "API_KEY"
KEY_ENV = "GOOGLE_TRANSLATE_API_KEY"
API_URL = "https://translation.googleapis.com/language/translate/v2"

# Per-request limits: the API takes up to 128 strings; Google recommends
# keeping a request under about 5000 characters.
BATCH_STRINGS = 100
BATCH_CHARS = 5000

# Terms that should never be translated (in addition to all-caps acronyms).
DEFAULT_KEEP_TERMS = [
    "wfview", "wfserver", "Icom", "Kenwood", "Yaesu", "Elecraft", "FlexRadio",
    "Xiegu", "rigctld", "Hamlib", "PortAudio", "RtAudio", "Qt", "Opus",
    "LAN", "USB", "CI-V", "Hz", "kHz", "MHz", "GHz", "dB", "dBm", "dBFS", "ms",
]

# --------------------------------------------------------------------------
# Text protection
# --------------------------------------------------------------------------

PLACEHOLDER_RE = r"%L?\d+|%n|%%|%[sdifc]"
TAG_RE = r"</?[A-Za-z][^<>]*>"
ENTITY_RE = r"&(?:[A-Za-z]+|#\d+|#x[0-9A-Fa-f]+);"
ACRONYM_RE = r"(?<![\w-])(?=[A-Z0-9-]{2,}(?![\w-]))[A-Z0-9]*[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)*(?![\w-])"
ACCEL_RE = re.compile(r"&(?=[^\s&;])")
SPAN_OPEN = '<span translate="no">'
SPAN_RE = re.compile(r'<span translate="no">(.*?)</span>', re.S)
BR_RE = re.compile(r"<br\s*/?>", re.I)


@dataclass
class Protected:
    source: str               # original text, stripped
    html: str                 # HTML sent to the API
    plain: str                # protected text without markup (for "has words" check)
    kept: list                # strings wrapped in no-translate spans
    accel: str                # accelerator letter (from "&File") or ""
    lead: str                 # leading whitespace to restore
    trail: str                # trailing whitespace to restore


def protect(source: str, keep_terms, protect_acronyms: bool) -> Protected:
    lead = source[: len(source) - len(source.lstrip())]
    trail = source[len(source.rstrip()):]
    body = source.strip()

    patterns = [PLACEHOLDER_RE, TAG_RE, ENTITY_RE, r"&&"]
    if keep_terms:
        terms = sorted(set(keep_terms), key=len, reverse=True)
        patterns.append(r"(?<![\w-])(?:%s)(?![\w-])" % "|".join(re.escape(t) for t in terms))
    if protect_acronyms:
        patterns.append(ACRONYM_RE)
    combined = re.compile("|".join("(?:%s)" % p for p in patterns))

    accel = ""
    kept, out_html, out_plain = [], [], []

    def plain_segment(seg):
        nonlocal accel
        m = ACCEL_RE.search(seg)
        if m:
            accel = accel or seg[m.end()]
            seg = ACCEL_RE.sub("", seg)
        out_html.append(html.escape(seg, quote=False).replace("\n", "<br>"))
        out_plain.append(seg)

    pos = 0
    for m in combined.finditer(body):
        plain_segment(body[pos: m.start()])
        kept.append(m.group(0))
        out_html.append(SPAN_OPEN + html.escape(m.group(0), quote=False) + "</span>")
        pos = m.end()
    plain_segment(body[pos:])

    return Protected(body, "".join(out_html), "".join(out_plain), kept, accel, lead, trail)


def restore(p: Protected, translated_html: str):
    """Turn the API's HTML back into plain text. Returns (text, problems)."""
    problems = []
    parts, found = [], []      # parts: (text, is_kept)
    pos = 0
    for m in SPAN_RE.finditer(translated_html):
        parts.append((html.unescape(BR_RE.sub("\n", translated_html[pos: m.start()])), False))
        inner = html.unescape(m.group(1))
        found.append(inner)
        parts.append((inner, True))
        pos = m.end()
    parts.append((html.unescape(BR_RE.sub("\n", translated_html[pos:])), False))

    # Put the accelerator back on the first letter of translated (not kept) text.
    # If the translation has no Latin letters (CJK etc.), use the Qt convention "(&X)".
    accel_suffix = ""
    if p.accel:
        for idx, (text, kept) in enumerate(parts):
            m = None if kept else re.search(r"[A-Za-z\u00c0-\u024f\u0370-\u03ff\u0400-\u04ff]", text)
            if m:
                parts[idx] = (text[: m.start()] + "&" + text[m.start():], False)
                break
        else:
            accel_suffix = "(&%s)" % p.accel.upper()
    out = "".join(t for t, _ in parts).strip()
    out = tidy(p.source, out)
    if accel_suffix:
        m = re.search(r"(\.\.\.|…|[:：])$", out)
        out = out[: m.start()] + accel_suffix + m.group(0) if m else out + accel_suffix

    want, got = Counter(p.kept), Counter(found)
    for t in sorted((want - got).elements()):
        problems.append("lost %r" % t)
    for t in sorted((got - want).elements()):
        problems.append("unexpected %r" % t)
    if "<span" in out:
        problems.append("leftover markup in result")
    return p.lead + out + p.trail, problems


def tidy(source: str, out: str) -> str:
    """Undo spacing and punctuation the translator adds around markup."""
    if not re.search(r"\n[ \t]", source):
        out = re.sub(r"\n[ \t]+", "\n", out)
    if not re.search(r"[ \t]\n", source):
        out = re.sub(r"[ \t]+\n", "\n", out)
    if not re.search(r"<[A-Za-z][^<>]*>\s", source):
        out = re.sub(r"(<[A-Za-z][^<>]*>)\s+", r"\1", out)
    if not re.search(r"\s</", source):
        out = re.sub(r"\s+(</[A-Za-z]+>)", r"\1", out)
    if not re.search(r"\s[)\]]", source):
        out = re.sub(r"\s+([)\]])", r"\1", out)
    if not re.search(r"[(\[]\s", source):
        out = re.sub(r"([(\[])\s+", r"\1", out)
    # German-style compounds: "CI-V -Adresse" -> "CI-V-Adresse"
    out = re.sub(r"(?<=[A-Za-z0-9]) -(?=\w)", lambda m: m.group(0) if " -" in source else "-", out)
    # Sentence punctuation the English did not have.
    if not re.search(r"[.!?。！？]$", source) and re.search(r"(?<!\.)[.。]$", out):
        out = out[:-1]
    return out


def has_words(p: Protected) -> bool:
    return bool(re.search(r"[^\W\d_]", p.plain))


def is_rich_text(s: str) -> bool:
    head = s.lstrip()[:200].lower()
    return head.startswith("<!doctype") or head.startswith("<html") or "<qt>" in head


# --------------------------------------------------------------------------
# Cloud Translation API
# --------------------------------------------------------------------------

class ApiError(Exception):
    pass


def load_api_key(key_file):
    if key_file:
        path = Path(key_file).expanduser()
    elif os.environ.get(KEY_ENV):
        return os.environ[KEY_ENV].strip()
    else:
        path = DEFAULT_KEY_FILE
    try:
        key = path.read_text().strip()
    except OSError as e:
        sys.exit("error: cannot read API key from %s (%s). Use --key-file or $%s."
                 % (path, e.strerror, KEY_ENV))
    if not key:
        sys.exit("error: API key file %s is empty" % path)
    return key


class CloudTranslate:
    def __init__(self, key, timeout=30):
        self.key = key
        self.timeout = timeout
        self.chars_sent = 0

    def _request(self, url, params=None):
        data = urllib.parse.urlencode(params, doseq=True).encode() if params else None
        # Key goes in a header, not the URL, so it never shows up in error messages.
        req = urllib.request.Request(url, data=data, headers={"X-Goog-Api-Key": self.key})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            try:
                msg = json.loads(e.read()).get("error", {}).get("message", "")
            except ValueError:
                msg = ""
            raise ApiError("HTTP %d %s" % (e.code, msg or e.reason)) from None
        except urllib.error.URLError as e:
            raise ApiError("network error: %s" % e.reason) from None

    def languages(self):
        data = self._request(API_URL + "/languages")
        return {l["language"] for l in data["data"]["languages"]}

    def translate(self, texts, target):
        """Translate a list of HTML strings; returns a list of HTML strings."""
        self.chars_sent += sum(len(t) for t in texts)
        data = self._request(API_URL, {"q": texts, "source": "en", "target": target,
                                       "format": "html"})
        out = [t["translatedText"] for t in data["data"]["translations"]]
        if len(out) != len(texts):
            raise ApiError("API returned %d results for %d strings" % (len(out), len(texts)))
        return out


# Locales whose nearest API language is not simply the base language.
LANG_FALLBACK = {
    "zh": "zh-CN",
    "zh_HK": "zh-TW",       # Hong Kong and Macau use Traditional characters
    "zh_MO": "zh-TW",
    "zh_SG": "zh-CN",
}


def api_lang(lang: str, override, supported):
    """Map a Qt locale suffix to an API language code; raises ValueError if unsupported."""
    if override:
        if override not in supported:
            raise ValueError("the API does not support language %r" % override)
        return override
    parts = lang.split("_")
    if len(parts) == 2 and "%s-%s" % (parts[0], parts[1]) in supported:
        return "%s-%s" % (parts[0], parts[1])     # zh-CN, zh-TW, pt-PT, fr-CA
    if LANG_FALLBACK.get(lang) in supported:
        return LANG_FALLBACK[lang]
    if parts[0] in supported:
        return parts[0]
    raise ValueError("the API does not support language %r (use --api-lang)" % lang)


def is_english(lang: str) -> bool:
    return lang.split("_")[0] == "en"


# --------------------------------------------------------------------------
# .ts handling
# --------------------------------------------------------------------------

@dataclass
class Item:
    context: str
    source: str
    comment: str
    extracomment: str
    location: str
    msg: ET.Element
    numerus: bool
    current: str
    status: str               # "" or "unfinished"
    proposed: str = ""
    problems: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    trivial: bool = False
    done: bool = False        # translation request completed (successfully or not)
    failed: bool = False


def write_ts(tree, path: Path):
    root = tree.getroot()
    ET.indent(root, space="    ")
    body = ET.tostring(root, encoding="unicode")
    tmp = path.with_suffix(".ts.tmp")
    tmp.write_text('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + body + "\n",
                   encoding="utf-8")
    tmp.replace(path)


def translation_text(tr: ET.Element, numerus: bool) -> str:
    if numerus:
        forms = tr.findall("numerusform")
        return (forms[0].text or "") if forms else ""
    return tr.text or ""


def collect_items(tree, scope: str, include_html: bool, context_re):
    """Returns (items to process, translation memory, number of rich-text strings skipped).

    The memory maps English source text to an existing translation of it in
    this file, taken from messages that are not being processed. Finished
    translations win over unfinished ones."""
    items, skipped_html = [], 0
    finished_mem, unfinished_mem = {}, {}
    for ctx in tree.getroot().iter("context"):
        cname = ctx.findtext("name", "")
        ctx_selected = not context_re or context_re.search(cname)
        for msg in ctx.findall("message"):
            src_el = msg.find("source")
            tr = msg.find("translation")
            if src_el is None or tr is None or len(src_el):
                continue  # missing parts, or <byte> children we cannot round-trip
            status = tr.get("type", "")
            if status in ("obsolete", "vanished"):
                continue
            numerus = msg.get("numerus") == "yes"
            current = translation_text(tr, numerus)
            source = src_el.text or ""
            if not source.strip():
                continue
            selected = ctx_selected and not (
                (scope == "untranslated" and current.strip()) or
                (scope == "unfinished" and status != "unfinished"))
            if not selected:
                if current.strip() and not numerus:
                    mem = unfinished_mem if status == "unfinished" else finished_mem
                    mem.setdefault(source, current)
                continue
            if is_rich_text(source) and not include_html:
                skipped_html += 1
                continue
            loc = msg.find("location")
            location = ""
            if loc is not None:
                location = "%s:%s" % (loc.get("filename", ""), loc.get("line", ""))
            items.append(Item(cname, source, msg.findtext("comment", ""),
                              msg.findtext("extracomment", ""), location, msg,
                              numerus, current, status))
    return items, {**unfinished_mem, **finished_mem}, skipped_html


def apply_translation(item: Item, text: str, finished: bool):
    tr = item.msg.find("translation")
    if item.numerus:
        forms = tr.findall("numerusform") or [ET.SubElement(tr, "numerusform")]
        for f in forms:
            f.text = text
    else:
        tr.text = text
    if finished:
        tr.attrib.pop("type", None)
    else:
        tr.set("type", "unfinished")


# --------------------------------------------------------------------------
# Qt tools and project registration
# --------------------------------------------------------------------------

def find_tool(name: str, qt_bin):
    candidates = []
    if qt_bin:
        candidates.append(str(Path(qt_bin) / name))
    for c in (name + "6", name + "-qt6"):
        w = shutil.which(c)
        if w:
            candidates.append(w)
    candidates += ["/usr/lib/qt6/bin/" + name, "/usr/lib64/qt6/bin/" + name,
                   "/usr/lib/x86_64-linux-gnu/qt6/bin/" + name]
    w = shutil.which(name)
    if w:
        candidates.append(w)
    for c in candidates:
        if not os.path.exists(c):
            continue
        try:
            r = subprocess.run([c, "-version"], capture_output=True, text=True, timeout=10)
        except OSError:
            continue
        if r.returncode == 0:
            return c
    sys.exit("error: could not find a working %s (use --qt-bin)" % name)


def run(cmd):
    print("+ " + " ".join(str(c) for c in cmd))
    r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    # lupdate warns about #if/#else brace mismatches in the audio code; keep the summary.
    lines = [l for l in out.splitlines() if "brace mismatch" not in l]
    if lines:
        print("  " + "\n  ".join(lines))
    if r.returncode != 0:
        sys.exit("error: command failed with exit code %d" % r.returncode)


def register_language(lang: str):
    ts_rel = "translations/wfview_%s.ts" % lang
    qm_name = "wfview_%s.qm" % lang

    pro = PRO_FILE.read_text(encoding="utf-8")
    if ts_rel not in pro:
        m = re.search(r"TRANSLATIONS\s*\+?=(?:[^\n]*\\\n)*[^\n]*", pro)
        if m:
            indent = re.search(r"\n(\s*)\S", m.group(0))
            indent = indent.group(1) if indent else "                "
            pro = pro[: m.end()] + " \\\n" + indent + ts_rel + pro[m.end():]
            PRO_FILE.write_text(pro, encoding="utf-8")
            print("Added %s to wfview.pro TRANSLATIONS" % ts_rel)
        else:
            print("warning: TRANSLATIONS block not found in wfview.pro; add %s by hand" % ts_rel)

    qrc = QRC_FILE.read_text(encoding="utf-8")
    if qm_name not in qrc:
        m = re.search(r"(\n(\s*)<file>[^<]*</file>)(?![\s\S]*<file>)", qrc)
        if m:
            qrc = qrc[: m.end()] + "\n%s<file>%s</file>" % (m.group(2), qm_name) + qrc[m.end():]
            QRC_FILE.write_text(qrc, encoding="utf-8")
            print("Added %s to translations/translation.qrc" % qm_name)
        else:
            print("warning: could not update translation.qrc; add %s by hand" % qm_name)


# --------------------------------------------------------------------------
# Translation driver
# --------------------------------------------------------------------------

class Engine:
    def __init__(self, api, target, args, memory=None, cache=None):
        self.api = api
        self.target = target
        self.keep = DEFAULT_KEEP_TERMS + (args.keep or [])
        self.protect_acronyms = not args.translate_acronyms
        self.memory = memory or {}      # source -> existing translation in this file
        # (target, html) -> API result. Shared between files in --all so that
        # languages and regions with the same API target pay for a string once.
        self.cache = {} if cache is None else cache
        self.error = None               # last API error, if any
        self.reused = 0                 # strings filled from self.memory
        self.cached = 0                 # strings answered from self.cache

    def propose(self, items):
        """Translate a list of items in as few requests as possible."""
        self.error = None
        pending = []
        for item in items:
            item.problems, item.notes, item.failed = [], [], False
            if not item.numerus and item.source in self.memory:
                item.proposed, item.done = self.memory[item.source], True
                item.notes.append("reused the existing translation of the same text")
                self.reused += 1
                continue
            p = protect(item.source, self.keep, self.protect_acronyms)
            if not has_words(p):
                item.proposed, item.trivial, item.done = item.source, True, True
            else:
                pending.append((item, p))

        # Send each distinct string once, skipping anything already answered.
        needed = []
        for _, p in pending:
            if (self.target, p.html) in self.cache:
                self.cached += 1
            elif p.html not in needed:
                needed.append(p.html)

        batch, size = [], 0
        for text in needed + [None]:
            flush = text is None or len(batch) >= BATCH_STRINGS or \
                (batch and size + len(text) > BATCH_CHARS)
            if flush and batch:
                if not self._send(batch):
                    break       # quota/network errors would just repeat
                batch, size = [], 0
            if text is not None:
                batch.append(text)
                size += len(text)

        for item, p in pending:
            out = self.cache.get((self.target, p.html))
            if out is None:
                item.proposed, item.problems = "", ["translation failed: %s" % self.error]
                item.failed = item.done = True
                continue
            item.proposed, item.problems = restore(p, out)
            if item.proposed.strip() == item.source.strip():
                item.notes.append("same as the English source")
            item.done = True

    def _send(self, texts):
        try:
            results = self.api.translate(texts, self.target)
        except ApiError as e:
            self.error = str(e)
            return False
        for text, out in zip(texts, results):
            self.cache[(self.target, text)] = out
        return True


def short(s, n=60):
    s = s.replace("\n", "\\n")
    return repr(s if len(s) <= n else s[: n - 3] + "...")


def run_bulk(engine, items, args, tree, ts_path):
    done = flagged = failed = 0
    chunk_size = BATCH_STRINGS * 2
    for start in range(0, len(items), chunk_size):
        chunk = items[start: start + chunk_size]
        engine.propose(chunk)
        for item in chunk:
            if item.failed:
                failed += 1
                continue
            if item.problems:
                flagged += 1
                print("  SKIP  %s -> %s  [%s]" % (short(item.source), short(item.proposed),
                                                  "; ".join(item.problems)))
                continue
            apply_translation(item, item.proposed, finished=args.mark_finished or item.trivial)
            done += 1
        print("  %d/%d processed" % (min(start + chunk_size, len(items)), len(items)))
        if not args.dry_run:
            write_ts(tree, ts_path)
        if engine.error:
            print("Stopping: %s" % engine.error)
            break
    print("Bulk: %d translated, %d flagged and left alone, %d failed." % (done, flagged, failed))
    return done, flagged, failed


def input_prefilled(prompt, text):
    try:
        import readline
    except ImportError:
        print("  (current: %s)" % text)
        r = input(prompt)
        return r if r else text
    readline.set_startup_hook(lambda: readline.insert_text(text))
    try:
        return input(prompt)
    finally:
        readline.set_startup_hook()


def show_multiline(label, text):
    lines = text.split("\n")
    print("  %-9s %s" % (label, lines[0]))
    for l in lines[1:]:
        print("  %-9s %s" % ("", l))


def run_interactive(engine, items, args, tree, ts_path):
    """Returns the number of strings changed, or None to discard unsaved work."""
    total = len(items)
    lookahead = 25          # translate this many at a time as the review advances
    changed = 0
    print("\nKeys: Enter/a=accept  e=edit  c=copy source  s=skip  r=retry  "
          "b=bulk the rest  q=save and quit  x=quit without saving\n")
    i = 0
    try:
        while i < total:
            item = items[i]
            if not item.done:
                engine.propose([it for it in items[i: i + lookahead] if not it.done])
            print("-" * 72)
            print("[%d/%d] %s   %s" % (i + 1, total, item.context, item.location))
            if item.comment:
                print("  comment:  %s" % item.comment)
            if item.extracomment:
                print("  note:     %s" % item.extracomment)
            show_multiline("source:", item.source)
            if item.current:
                show_multiline("current:", item.current + ("  (unfinished)" if item.status else ""))
            if item.proposed:
                show_multiline("proposed:", item.proposed)
            for p in item.problems:
                print("  WARNING:  %s" % p)
            for n in item.notes:
                print("  (%s)" % n)
            try:
                choice = input("> ").strip().lower()
            except EOFError:
                choice = "q"

            if choice in ("", "a"):
                if not item.proposed:
                    print("  nothing to accept; use e to type a translation, r to retry, or s to skip")
                    continue
                apply_translation(item, item.proposed, finished=True)
                changed += 1
            elif choice == "e":
                text = input_prefilled("  edit> ", item.proposed or item.current or item.source)
                if not text.strip():
                    print("  empty; not changed")
                    continue
                apply_translation(item, text, finished=True)
                changed += 1
            elif choice == "c":
                apply_translation(item, item.source, finished=True)
                changed += 1
            elif choice == "s":
                pass
            elif choice == "r":
                item.done = False
                continue
            elif choice == "b":
                rest = items[i:]
                print("Bulk-translating the remaining %d strings..." % len(rest))
                changed += run_bulk(engine, rest, args, tree, ts_path)[0]
                break
            elif choice == "q":
                break
            elif choice == "x":
                return None
            else:
                print("  unknown key")
                continue
            i += 1
            if changed and changed % 10 == 0 and not args.dry_run:
                write_ts(tree, ts_path)
    except KeyboardInterrupt:
        print("\nInterrupted; saving progress.")
    return changed


def setup_api(args):
    """Check the key, connectivity and target language before any file is touched."""
    if args.lang.split("_")[0] == "en" and not args.api_lang:
        sys.exit("error: the source text is already English, so there is nothing to machine-translate.\n"
                 "       English .ts files only need plural forms (and en_GB spellings), which are\n"
                 "       edited in Qt Linguist. Use --no-translate to just run lupdate + lrelease.")
    api = CloudTranslate(load_api_key(args.key_file))
    try:
        target = api_lang(args.lang, args.api_lang, api.languages())
    except ApiError as e:
        sys.exit("error: Cloud Translation API: %s" % e)
    except ValueError as e:
        sys.exit("error: %s" % e)
    return api, target


@dataclass
class FileResult:
    selected: int = 0
    translated: int = 0
    reused: int = 0
    flagged: int = 0
    failed: int = 0
    wrote: bool = False
    error: str = ""


def translate_file(args, ts_path: Path, api, target, cache=None) -> FileResult:
    res = FileResult()
    tree = ET.parse(ts_path)
    context_re = re.compile(args.context) if args.context else None
    items, memory, skipped_html = collect_items(tree, args.scope, args.include_html, context_re)
    if args.limit:
        items = items[: args.limit]
    res.selected = len(items)
    print("%d strings selected (scope: %s)%s." % (
        len(items), args.scope,
        ", %d rich-text strings skipped (--include-html)" % skipped_html if skipped_html else ""))
    if not items:
        return res

    print("Translating en -> %s with Google Cloud Translation" % target)
    engine = Engine(api, target, args, memory, cache)
    chars_before = api.chars_sent

    if args.bulk:
        changed, res.flagged, res.failed = run_bulk(engine, items, args, tree, ts_path)
    else:
        changed = run_interactive(engine, items, args, tree, ts_path)
    res.reused, res.error = engine.reused, engine.error or ""
    print("Sent %d characters to the API%s." % (
        api.chars_sent - chars_before,
        "; %d strings reused from this file, %d from earlier requests" % (engine.reused, engine.cached)
        if engine.reused or engine.cached else ""))

    if changed is None:
        print("Quit without saving (strings saved earlier remain in the file).")
        return res
    res.translated = changed
    if args.dry_run:
        print("Dry run: %s not written." % ts_path.name)
        return res
    write_ts(tree, ts_path)
    res.wrote = True
    print("Wrote %s (%d strings changed)." % (ts_path.relative_to(REPO), changed))
    return res


def lupdate_command(args, ts_paths):
    # Scan the source folders directly rather than wfview.pro: the result is
    # identical, but parsing the .pro makes lupdate warn about Qt5-only modules
    # (gamepad) and trip over Qt's private headers via INCLUDEPATH.
    cmd = [find_tool("lupdate", args.qt_bin), *LUPDATE_DIRS, "-ts",
           *[os.path.relpath(p, REPO) for p in ts_paths]]
    if args.no_obsolete:
        cmd.insert(1, "-no-obsolete")
    return cmd


def lrelease_command(lrelease, args, ts_paths):
    # Without -qm, lrelease writes each .qm next to its .ts with the same name.
    cmd = [lrelease, *[str(p.relative_to(REPO)) for p in ts_paths]]
    if args.finished_only:
        cmd.insert(1, "-nounfinished")
    return cmd


def discover_languages():
    """Locale suffixes of every translations/wfview_*.ts file, base languages before regions."""
    langs = []
    for p in sorted(TS_DIR.glob("wfview_*.ts")):
        m = re.fullmatch(r"wfview_([a-z]{2,3}(?:_[A-Za-z]{2,4})?)\.ts", p.name)
        if m:
            langs.append(m.group(1))
    return langs


def run_all(args):
    """Bulk-update every language and region that has a .ts file."""
    langs = discover_languages()
    if not langs:
        sys.exit("error: no translations/wfview_*.ts files found")
    ts_paths = {l: TS_DIR / ("wfview_%s.ts" % l) for l in langs}

    # Everything that can fail cheaply is checked before any file is touched.
    lupdate_cmd = None if args.no_lupdate else lupdate_command(args, ts_paths.values())
    lrelease = None if args.no_lrelease else find_tool("lrelease", args.qt_bin)
    api, targets, skipped = None, {}, {}
    for l in langs:
        if is_english(l):
            skipped[l] = "English source; lupdate/lrelease only"
    if not args.no_translate:
        api = CloudTranslate(load_api_key(args.key_file))
        try:
            supported = api.languages()
        except ApiError as e:
            sys.exit("error: Cloud Translation API: %s" % e)
        for l in langs:
            if l in skipped:
                continue
            try:
                targets[l] = api_lang(l, None, supported)
            except ValueError as e:
                skipped[l] = str(e)

    print("Languages: %s" % ", ".join(
        "%s->%s" % (l, targets[l]) if l in targets else l for l in langs))
    for l, why in skipped.items():
        if not (args.no_translate and is_english(l)):
            print("  not translating %s: %s" % (l, why))

    tmpdir = None
    if lupdate_cmd and args.dry_run:
        # Run lupdate on copies so the dry run sees the new strings without
        # touching the real files.
        tmpdir = tempfile.TemporaryDirectory(prefix="wfview_translate_")
        for l in langs:
            ts_paths[l] = Path(shutil.copy(ts_paths[l], tmpdir.name))
        lupdate_cmd = lupdate_command(args, ts_paths.values())
    if lupdate_cmd:
        run(lupdate_cmd)

    results, cache, stopped = {}, {}, ""
    for l in langs:
        if l not in targets:
            continue
        print("\n=== %s (%s) ===" % (l, targets[l]))
        res = results[l] = translate_file(args, ts_paths[l], api, targets[l], cache)
        if res.error:
            stopped = res.error
            print("Stopping before the remaining languages: %s" % res.error)
            break

    written = [ts_paths[l] for l, r in results.items() if r.wrote]
    if written and lupdate_cmd:
        run(lupdate_command(args, written))  # normalise to lupdate's own formatting
    if lrelease:
        run(lrelease_command(lrelease, args, ts_paths.values()))

    if results:
        print("\n%-8s %-7s %8s %10s %7s %8s %7s" % (
            "lang", "api", "selected", "translated", "reused", "flagged", "failed"))
        for l, r in results.items():
            print("%-8s %-7s %8d %10d %7d %8d %7d" % (
                l, targets[l], r.selected, r.translated, r.reused, r.flagged, r.failed))
        print("Sent %d characters to the API in total." % api.chars_sent)
    not_reached = [l for l in targets if l not in results]
    if stopped and not_reached:
        print("Not processed because of the API error: %s" % ", ".join(not_reached))
    if lrelease:
        print("Rebuild wfview to pick up the new .qm files (they are compiled in via translation.qrc).")
    if stopped:
        sys.exit(1)


def main():
    ap = argparse.ArgumentParser(
        description="Generate, machine-translate (Google Cloud Translation) and compile wfview translations.",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("Examples:")[1])
    ap.add_argument("lang", nargs="?", help="Qt locale suffix of the .ts file, e.g. de, es_ES, zh_CN, fr_FR")
    ap.add_argument("-a", "--all", action="store_true",
                    help="bulk-update every language and region that has a translations/wfview_*.ts file")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("-b", "--bulk", action="store_true", help="translate everything without prompting")
    mode.add_argument("-i", "--interactive", action="store_true", help="review each string (default)")
    ap.add_argument("--scope", choices=("untranslated", "unfinished", "all"), default="untranslated",
                    help="which strings to process (default: untranslated = empty translation)")
    ap.add_argument("--context", help="only process contexts (classes/QML files) matching this regex")
    ap.add_argument("--limit", type=int, help="process at most N strings")
    ap.add_argument("--mark-finished", action="store_true",
                    help="mark bulk machine translations finished instead of unfinished")
    ap.add_argument("--include-html", action="store_true", help="also translate rich-text (HTML) strings")
    ap.add_argument("--translate-acronyms", action="store_true",
                    help="let the translator touch all-caps terms such as PTT/VFO")
    ap.add_argument("--keep", action="append", metavar="TERM", help="extra term to keep untranslated (repeatable)")
    ap.add_argument("--api-lang", help="API language code if it differs from the file's, e.g. pt-PT")
    ap.add_argument("--key-file", help="file containing the API key (default: $%s or %s)"
                    % (KEY_ENV, DEFAULT_KEY_FILE))
    ap.add_argument("--no-translate", action="store_true", help="skip translation; just lupdate + lrelease")
    ap.add_argument("--no-lupdate", action="store_true", help="do not run lupdate")
    ap.add_argument("--no-lrelease", action="store_true", help="do not run lrelease")
    ap.add_argument("--no-obsolete", action="store_true", help="pass -no-obsolete to lupdate")
    ap.add_argument("--finished-only", action="store_true",
                    help="pass -nounfinished to lrelease (exclude unreviewed machine translations)")
    ap.add_argument("--no-register", action="store_true",
                    help="do not add a new language to wfview.pro and translation.qrc")
    ap.add_argument("--dry-run", action="store_true", help="propose translations but do not write anything")
    ap.add_argument("--qt-bin", help="directory containing lupdate/lrelease")
    args = ap.parse_args()

    if args.all:
        if args.lang:
            ap.error("give either a language or --all, not both")
        if args.interactive:
            ap.error("--all always runs in bulk mode")
        if args.api_lang:
            ap.error("--api-lang applies to a single language")
        args.bulk = True
        if args.dry_run:
            args.no_lrelease = True
        run_all(args)
        return
    if not args.lang:
        ap.error("a language (e.g. de, es_ES) or --all is required")

    if not re.fullmatch(r"[a-z]{2,3}(_[A-Za-z]{2,4})?", args.lang):
        sys.exit("error: language should look like de, es_ES or zh_CN")

    ts_path = TS_DIR / ("wfview_%s.ts" % args.lang)
    qm_path = ts_path.with_suffix(".qm")
    is_new = not ts_path.exists()

    if args.dry_run:
        args.no_lrelease = True
        args.no_register = True
        if is_new:
            sys.exit("error: %s does not exist; run without --dry-run first" % ts_path.name)

    if is_new and args.no_lupdate:
        sys.exit("error: %s does not exist and --no-lupdate was given" % ts_path.name)

    # Everything that can fail cheaply is checked before any file is touched.
    lupdate_cmd = None if args.no_lupdate else lupdate_command(args, [ts_path])
    lrelease = None if args.no_lrelease else find_tool("lrelease", args.qt_bin)
    api = target = None
    if not args.no_translate:
        api, target = setup_api(args)

    try:
        if lupdate_cmd and not args.dry_run:
            run(lupdate_cmd)

        wrote = False
        if not args.no_translate:
            wrote = translate_file(args, ts_path, api, target).wrote

        if wrote and lupdate_cmd:
            run(lupdate_cmd)  # re-scan and normalise the file to lupdate's own formatting

        if lrelease:
            cmd = [lrelease, str(ts_path.relative_to(REPO)), "-qm", str(qm_path.relative_to(REPO))]
            if args.finished_only:
                cmd.insert(1, "-nounfinished")
            run(cmd)
    except BaseException as e:
        # A new language that did not complete must leave no trace, or the
        # build breaks on a .qrc entry with no .qm (or a half-made .ts).
        if is_new and not (isinstance(e, SystemExit) and e.code in (0, None)):
            for p in (ts_path, ts_path.with_suffix(".ts.tmp"), qm_path):
                if p.exists():
                    p.unlink()
            print("Removed the partially created %s files." % ts_path.stem, file=sys.stderr)
        raise

    # Only register a new language once its .qm exists, so the build never
    # references a missing file.
    if is_new and not args.no_register:
        if qm_path.exists():
            register_language(args.lang)
        else:
            print("Not adding %s to wfview.pro/translation.qrc because %s was not built."
                  % (ts_path.name, qm_path.name))
    if lrelease:
        print("Rebuild wfview to pick up %s (it is compiled in via translation.qrc)." % qm_path.name)


if __name__ == "__main__":
    main()
