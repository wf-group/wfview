#pragma once

#include <QByteArray>
#include <QtGlobal>

// Value codings used by Yaesu CAT commands, from the FT-710 MAIN firmware.

// Break-in delay (SD) and VOX delay (VD) codes 00-33: 30, 50, 100, 150, 200
// and 250 ms, then 300-3000 ms in 100 ms steps.
int yaesuDelayCodeToMs(int code);
// The code with the delay nearest to ms.
int yaesuDelayMsToCode(int ms);

// IF shift (IS0): sign and four digits, -1200..+1200 Hz in 20 Hz steps.
// Formatting rounds to the nearest step and clamps to the range.
QByteArray yaesuFormatIfShift(int hz);
// Parses the sign and four digits; false if they are malformed.
bool yaesuParseIfShift(const QByteArray &text, int *hz);

// Scope reference level (SS04): sign, two digits, '.', then '0' or '5',
// -30.0..+30.0 dB. wfview carries the level in tenths of a dB.
// Formatting rounds to the nearest 0.5 dB and clamps to the range.
QByteArray yaesuFormatScopeRef(int tenthsDb);
// Parses the sign, digits and point; false if they are malformed.
bool yaesuParseScopeRef(const QByteArray &text, int *tenthsDb);

// CW pitch (KP): step numbers 00-75 mean 300-1050 Hz in 10 Hz steps.
int yaesuCwPitchHzFromSteps(int steps);
// The step nearest to hz (not clamped).
int yaesuCwPitchStepsFromHz(int hz);

// Tone mode: swaps 1 and 2 (tone and tone squelch), for rigs whose CT uses
// 1 for tone squelch (ToneSquelchIsOne). Other values are unchanged.
quint8 yaesuSwapToneMode(quint8 mode);

// CW text sent through a keyer memory: how many characters of pending go into
// the next memory, at most maxChars. When the text has to be split, the memory
// ends after the last space that fits, so the pause between memories falls
// between words.
int yaesuCwMemoryChunkLength(const QByteArray &pending, int maxChars);
// How long the radio takes to key text at wpm with the given dash ratio, in ms.
int yaesuCwKeyingMs(const QByteArray &text, int wpm, double dashRatio);

// Scope window in Cursor and Fixed modes: true when the VFO is outside the
// window that starts at startMHz and is spanMHz wide.
bool yaesuVfoOutsideScopeWindow(double vfoMHz, double startMHz, double spanMHz);
// Whether to read the window start (FD00n) now. It is read when it isn't known,
// when the VFO has left the window, and every second, but at most every 200 ms.
// sinceLastReadMs is -1 when it has never been read.
bool yaesuScopeWindowReadDue(bool startKnown, bool vfoOutside, qint64 sinceLastReadMs);

// A VFO-A the radio reported itself (Auto Information or a reply) is newer than
// a scope frame's VFO-A for limitMs after the report, when the two differ.
// reportAgeMs is -1 when there has been no report.
bool yaesuVfoReportNewer(qint64 reportAgeMs, quint64 reportHz, quint64 frameHz, qint64 limitMs);
