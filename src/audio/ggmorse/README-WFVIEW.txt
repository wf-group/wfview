ggmorse - Morse code decoding library, vendored for the wfview CW decoder
=========================================================================

Upstream : https://github.com/ggerganov/ggmorse
Commit   : 7b4822a (2026-08-24)
License  : MIT (see LICENSE in this folder)

All files are copied unchanged from upstream except src/ggmorse.cpp:
upstream printf()s every decoded character to stdout. Those calls are
wrapped in GGMORSE_ECHO(...), which compiles to nothing unless
GGMORSE_STDOUT_ECHO is defined. No decoding logic was changed.

Used by include/cwdecodercore.h (via CwAudioDecoder).
