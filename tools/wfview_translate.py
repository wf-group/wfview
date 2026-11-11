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
"""

import argparse
import html
import json
import os
import re
import shutil
import subprocess
import sys
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


def api_lang(lang: str, override, supported):
    if override:
        if override not in supported:
            sys.exit("error: the API does not support language %r" % override)
        return override
    parts = lang.split("_")
    if len(parts) == 2 and "%s-%s" % (parts[0], parts[1]) in supported:
        return "%s-%s" % (parts[0], parts[1])     # zh-CN, zh-TW, pt-PT, fr-CA
    if parts[0] in supported:
        return parts[0]
    sys.exit("error: the API does not support language %r (use --api-lang)" % lang)


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
    items, skipped_html = [], 0
    for ctx in tree.getroot().iter("context"):
        cname = ctx.findtext("name", "")
        if context_re and not context_re.search(cname):
            continue
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
            if scope == "untranslated" and current.strip():
                continue
            if scope == "unfinished" and status != "unfinished":
                continue
            source = src_el.text or ""
            if not source.strip():
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
    return items, skipped_html


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
    def __init__(self, api, target, args):
        self.api = api
        self.target = target
        self.keep = DEFAULT_KEEP_TERMS + (args.keep or [])
        self.protect_acronyms = not args.translate_acronyms

    def propose(self, items):
        """Translate a list of items in as few requests as possible."""
        pending = []
        for item in items:
            item.problems, item.notes, item.failed = [], [], False
            p = protect(item.source, self.keep, self.protect_acronyms)
            if not has_words(p):
                item.proposed, item.trivial, item.done = item.source, True, True
            else:
                pending.append((item, p))

        batch, size = [], 0
        for entry in pending + [None]:
            flush = entry is None or len(batch) >= BATCH_STRINGS or \
                (batch and size + len(entry[1].html) > BATCH_CHARS)
            if flush and batch:
                self._send(batch)
                batch, size = [], 0
            if entry is not None:
                batch.append(entry)
                size += len(entry[1].html)

    def _send(self, batch):
        try:
            results = self.api.translate([p.html for _, p in batch], self.target)
        except ApiError as e:
            for item, _ in batch:
                item.proposed, item.problems = "", ["translation failed: %s" % e]
                item.failed = item.done = True
            return
        for (item, p), out in zip(batch, results):
            item.proposed, item.problems = restore(p, out)
            if item.proposed.strip() == item.source.strip():
                item.notes.append("same as the English source")
            item.done = True


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
        errors = {p for i in chunk if i.failed for p in i.problems}
        if errors:
            print("Stopping: %s" % "; ".join(sorted(errors)))
            break
    print("Bulk: %d translated, %d flagged and left alone, %d failed." % (done, flagged, failed))
    return done


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
                changed += run_bulk(engine, rest, args, tree, ts_path)
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
    return api, target


def translate_file(args, ts_path: Path, api, target):
    """Returns True if the .ts file was written."""
    tree = ET.parse(ts_path)
    context_re = re.compile(args.context) if args.context else None
    items, skipped_html = collect_items(tree, args.scope, args.include_html, context_re)
    if args.limit:
        items = items[: args.limit]
    print("%d strings selected (scope: %s)%s." % (
        len(items), args.scope,
        ", %d rich-text strings skipped (--include-html)" % skipped_html if skipped_html else ""))
    if not items:
        return False

    print("Translating en -> %s with Google Cloud Translation" % target)
    engine = Engine(api, target, args)

    if args.bulk:
        changed = run_bulk(engine, items, args, tree, ts_path)
    else:
        changed = run_interactive(engine, items, args, tree, ts_path)
    print("Sent %d characters to the API." % api.chars_sent)

    if changed is None:
        print("Quit without saving (strings saved earlier remain in the file).")
        return False
    if args.dry_run:
        print("Dry run: %s not written." % ts_path.name)
        return False
    write_ts(tree, ts_path)
    print("Wrote %s (%d strings changed)." % (ts_path.relative_to(REPO), changed))
    return True


def main():
    ap = argparse.ArgumentParser(
        description="Generate, machine-translate (Google Cloud Translation) and compile wfview translations.",
        formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("Examples:")[1])
    ap.add_argument("lang", help="Qt locale suffix of the .ts file, e.g. de, es_ES, zh_CN, fr_FR")
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
    lupdate_cmd = None
    if not args.no_lupdate:
        # Scan the source folders directly rather than wfview.pro: the result is
        # identical, but parsing the .pro makes lupdate warn about Qt5-only modules
        # (gamepad) and trip over Qt's private headers via INCLUDEPATH.
        lupdate_cmd = [find_tool("lupdate", args.qt_bin), *LUPDATE_DIRS,
                       "-ts", str(ts_path.relative_to(REPO))]
        if args.no_obsolete:
            lupdate_cmd.insert(2, "-no-obsolete")
    lrelease = None if args.no_lrelease else find_tool("lrelease", args.qt_bin)
    api = target = None
    if not args.no_translate:
        api, target = setup_api(args)

    try:
        if lupdate_cmd and not args.dry_run:
            run(lupdate_cmd)

        wrote = False
        if not args.no_translate:
            wrote = translate_file(args, ts_path, api, target)

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
