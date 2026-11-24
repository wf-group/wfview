#pragma once

#include <QByteArray>

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

// wfview's single tone command uses 1 = tone and 2 = tone squelch (as on the
// TS-890). The FT-710's CT uses 1 = CTCSS encode and decode (tone squelch) and
// 2 = CTCSS encode only (tone); rigs like it set ToneSquelchIsOne in the rig
// file. This swaps 1 and 2; other values are the same both ways.
quint8 yaesuSwapToneMode(quint8 mode);
