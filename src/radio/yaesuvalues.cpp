#include "yaesuvalues.h"

#include <cstdlib>

namespace
{
constexpr int delayShortCodes[] = {30, 50, 100, 150, 200, 250};
constexpr int delayShortCount = int(sizeof(delayShortCodes) / sizeof(delayShortCodes[0]));
constexpr int delayMaxCode = 33;
constexpr int delayLongStartMs = 300;
constexpr int delayLongStepMs = 100;
}

int yaesuDelayCodeToMs(int code)
{
    if (code < 0)
        code = 0;
    if (code > delayMaxCode)
        code = delayMaxCode;
    if (code < delayShortCount)
        return delayShortCodes[code];
    return delayLongStartMs + (code - delayShortCount) * delayLongStepMs;
}

int yaesuDelayMsToCode(int ms)
{
    int best = 0;
    for (int code = 1; code <= delayMaxCode; ++code)
    {
        if (std::abs(yaesuDelayCodeToMs(code) - ms) < std::abs(yaesuDelayCodeToMs(best) - ms))
            best = code;
    }
    return best;
}
