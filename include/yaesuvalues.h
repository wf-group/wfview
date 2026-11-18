#pragma once

// Value codings used by Yaesu CAT commands, from the FT-710 MAIN firmware.

// Break-in delay (SD) and VOX delay (VD) codes 00-33: 30, 50, 100, 150, 200
// and 250 ms, then 300-3000 ms in 100 ms steps.
int yaesuDelayCodeToMs(int code);
// The code with the delay nearest to ms.
int yaesuDelayMsToCode(int ms);
