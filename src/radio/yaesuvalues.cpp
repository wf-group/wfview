#include "yaesuvalues.h"

#include <QString>
#include <algorithm>
#include <cstdlib>

namespace
{
constexpr int delayShortCodes[] = {30, 50, 100, 150, 200, 250};
constexpr int delayShortCount = int(sizeof(delayShortCodes) / sizeof(delayShortCodes[0]));
constexpr int delayMaxCode = 33;
constexpr int delayLongStartMs = 300;
constexpr int delayLongStepMs = 100;
constexpr int ifShiftMaxHz = 1200;
constexpr int ifShiftStepHz = 20;
constexpr int signedDigits = 4;
constexpr int scopeRefMaxTenths = 300;
constexpr int scopeRefStepTenths = 5;

// Rounds half away from zero to a multiple of step.
int roundToStep(int value, int step)
{
    const int half = step / 2;
    return (value >= 0 ? (value + half) / step : -((-value + half) / step)) * step;
}

QByteArray formatSigned(int value, int digits)
{
    return (value < 0 ? QByteArray("-") : QByteArray("+"))
        + QString::number(std::abs(value)).rightJustified(digits, QChar('0')).toLatin1();
}

bool parseSigned(const QByteArray &text, int digits, int *value)
{
    if (text.size() < digits + 1 || (text.at(0) != '+' && text.at(0) != '-'))
        return false;
    bool ok = false;
    const int magnitude = text.mid(1, digits).toInt(&ok);
    if (!ok)
        return false;
    *value = text.at(0) == '-' ? -magnitude : magnitude;
    return true;
}
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

QByteArray yaesuFormatIfShift(int hz)
{
    const int shift = std::clamp(roundToStep(hz, ifShiftStepHz), -ifShiftMaxHz, ifShiftMaxHz);
    return formatSigned(shift, signedDigits);
}

bool yaesuParseIfShift(const QByteArray &text, int *hz)
{
    return parseSigned(text, signedDigits, hz);
}

QByteArray yaesuFormatScopeRef(int tenthsDb)
{
    const int level = std::clamp(roundToStep(tenthsDb, scopeRefStepTenths), -scopeRefMaxTenths, scopeRefMaxTenths);
    const int magnitude = std::abs(level);
    return (level < 0 ? QByteArray("-") : QByteArray("+"))
        + QString::number(magnitude / 10).rightJustified(2, QChar('0')).toLatin1() + "."
        + QByteArray::number(magnitude % 10);
}

bool yaesuParseScopeRef(const QByteArray &text, int *tenthsDb)
{
    if (text.size() < 5 || text.at(3) != '.')
        return false;
    int whole = 0;
    if (!parseSigned(text.left(3), 2, &whole))
        return false;
    bool ok = false;
    const int tenths = text.mid(4, 1).toInt(&ok);
    if (!ok)
        return false;
    *tenthsDb = whole * 10 + (text.at(0) == '-' ? -tenths : tenths);
    return true;
}
