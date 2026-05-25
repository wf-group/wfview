#include "rigctlcompat.h"

namespace rigctlcompat
{

int hamlibAntennaIndex(int rigNumber, const QList<int> &rigNumbers)
{
    bool zeroBased = false;
    for (const int number : rigNumbers)
        zeroBased = zeroBased || number == 0;

    return zeroBased ? rigNumber : rigNumber - 1;
}

quint8 rigAntennaNumber(int hamlibIndex, const QList<int> &rigNumbers)
{
    for (const int number : rigNumbers) {
        if (hamlibAntennaIndex(number, rigNumbers) == hamlibIndex)
            return static_cast<quint8>(number);
    }
    return static_cast<quint8>(hamlibIndex);
}

quint32 antennaMask(const QList<int> &rigNumbers)
{
    quint32 mask = 0;
    for (const int number : rigNumbers) {
        const int index = hamlibAntennaIndex(number, rigNumbers);
        if (index >= 0 && index < 32)
            mask |= 1U << index;
    }
    return mask;
}

QString antennaName(int hamlibIndex)
{
    switch (hamlibIndex) {
    case 0: return QStringLiteral("ANT1");
    case 1: return QStringLiteral("ANT2");
    case 2: return QStringLiteral("ANT3");
    case 3: return QStringLiteral("ANT4");
    case 4: return QStringLiteral("ANT5");
    case 30: return QStringLiteral("ANT_UNKNOWN");
    case 31: return QStringLiteral("ANT_CURR");
    default: return QStringLiteral("ANT_UNK");
    }
}

quint8 antennaIndexFromName(const QString &name)
{
    const QString upperName = name.toUpper();
    if (upperName == QLatin1String("ANT1"))
        return 0;
    if (upperName == QLatin1String("ANT2"))
        return 1;
    if (upperName == QLatin1String("ANT3"))
        return 2;
    if (upperName == QLatin1String("ANT4"))
        return 3;
    if (upperName == QLatin1String("ANT5"))
        return 4;
    if (upperName == QLatin1String("ANT_UNKNOWN"))
        return 30;
    if (upperName == QLatin1String("ANT_CURR"))
        return 31;
    return 99;
}

bool isVfoName(const QString &name)
{
    const QString upperName = name.toUpper();
    return upperName == QLatin1String("VFOA")
           || upperName == QLatin1String("VFOB")
           || upperName == QLatin1String("MAIN")
           || upperName == QLatin1String("SUB")
           || upperName == QLatin1String("MEM")
           || upperName == QLatin1String("CURR");
}

ModeBandwidths modeBandwidths(const QString &modeName)
{
    const QString mode = modeName.toUpper();
    ModeBandwidths bandwidths{2400, 1800, 3000};

    if (mode == QLatin1String("CW") || mode == QLatin1String("CWR") || mode == QLatin1String("CW-R")
        || mode == QLatin1String("RTTY") || mode == QLatin1String("RTTYR") || mode == QLatin1String("RTTY-R")
        || mode == QLatin1String("PSK") || mode == QLatin1String("PSKR")) {
        bandwidths.normal = 500;
        bandwidths.narrow = 250;
        bandwidths.wide = 1200;
    } else if (mode == QLatin1String("AM") || mode == QLatin1String("AMS") || mode == QLatin1String("AMN")
               || mode == QLatin1String("AM-D") || mode == QLatin1String("PKTAM")) {
        bandwidths.normal = 6000;
        bandwidths.narrow = 3000;
        bandwidths.wide = 9000;
    } else if (mode == QLatin1String("FM") || mode == QLatin1String("FMN") || mode == QLatin1String("WFM")
               || mode == QLatin1String("PKTFM") || mode == QLatin1String("PKTFMN") || mode == QLatin1String("FM-D")) {
        bandwidths.normal = 10000;
        bandwidths.narrow = 7000;
        bandwidths.wide = 15000;
    }

    return bandwidths;
}

QStringList modeBandwidthResponse(const QString &modeName)
{
    const ModeBandwidths bandwidths = modeBandwidths(modeName);
    return {
        QStringLiteral("Mode=%1").arg(modeName),
        QStringLiteral("Normal=%1Hz").arg(bandwidths.normal),
        QStringLiteral("Narrow=%1Hz").arg(bandwidths.narrow),
        QStringLiteral("Wide=%1Hz").arg(bandwidths.wide)
    };
}

}
