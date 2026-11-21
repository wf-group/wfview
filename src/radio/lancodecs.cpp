#include "lancodecs.h"

namespace {

const lanCodecInfo pcm16Bit1ch = {lanCodecPcm16Bit1ch, QT_TRANSLATE_NOOP("SettingsController", "LPCM 1ch 16bit"), true};
const lanCodecInfo pcm8Bit1ch = {lanCodecPcm8Bit1ch, QT_TRANSLATE_NOOP("SettingsController", "PCM 1ch 8bit"), true};
const lanCodecInfo uLaw1ch = {lanCodecULaw1ch, QT_TRANSLATE_NOOP("SettingsController", "uLaw 1ch 8bit"), true};
const lanCodecInfo pcm16Bit2ch = {lanCodecPcm16Bit2ch, QT_TRANSLATE_NOOP("SettingsController", "LPCM 2ch 16bit"), true};
const lanCodecInfo uLaw2ch = {lanCodecULaw2ch, QT_TRANSLATE_NOOP("SettingsController", "uLaw 2ch 8bit"), true};
const lanCodecInfo pcm8Bit2ch = {lanCodecPcm8Bit2ch, QT_TRANSLATE_NOOP("SettingsController", "PCM 2ch 8bit"), true};
const lanCodecInfo adpcm1chOem = {lanCodecAdpcm1ch, QT_TRANSLATE_NOOP("SettingsController", "ADPCM 1ch"), true};

const lanCodecInfo opus1ch = {lanCodecOpus1ch, QT_TRANSLATE_NOOP("SettingsController", "Opus 1ch"), false};
const lanCodecInfo opus2ch = {lanCodecOpus2ch, QT_TRANSLATE_NOOP("SettingsController", "Opus 2ch"), false};
const lanCodecInfo adpcm1ch = {lanCodecAdpcm1ch, QT_TRANSLATE_NOOP("SettingsController", "ADPCM 1ch"), false};

} // namespace

QList<lanCodecInfo> lanCodecs(manufacturersType_t manufacturer, bool tx)
{
    switch (manufacturer) {
    case manufKenwood:
        // ##VP1, ##VP2, and ##VP3 for a wfview server. One command sets both
        // directions, so RX and TX offer the same list.
        return {pcm16Bit1ch, adpcm1chOem, opus1ch};
    case manufYaesu:
        // Yaesu's audio formats have no 8 bit PCM.
        if (tx)
            return {pcm16Bit1ch, uLaw1ch, opus1ch, adpcm1ch};
        return {pcm16Bit1ch, uLaw1ch, pcm16Bit2ch, uLaw2ch, opus1ch, opus2ch, adpcm1ch};
    case manufIcom:
    default:
        if (tx)
            return {pcm16Bit1ch, pcm8Bit1ch, uLaw1ch, opus1ch, adpcm1ch};
        return {pcm16Bit1ch, pcm8Bit1ch, uLaw1ch, pcm16Bit2ch, uLaw2ch, pcm8Bit2ch, opus1ch, opus2ch, adpcm1ch};
    }
}

bool lanCodecSupported(manufacturersType_t manufacturer, quint8 codec, bool tx)
{
    const QList<lanCodecInfo> codecs = lanCodecs(manufacturer, tx);
    for (const lanCodecInfo &info : codecs) {
        if (info.codec == codec)
            return true;
    }
    return false;
}

bool lanSanitizeCodecs(manufacturersType_t manufacturer, quint8 *rxCodec, quint8 *txCodec)
{
    if (rxCodec == nullptr || txCodec == nullptr)
        return false;

    const quint8 oldRx = *rxCodec;
    const quint8 oldTx = *txCodec;

    if (!lanCodecSupported(manufacturer, *rxCodec, false))
        *rxCodec = lanCodecDefault;

    if (manufacturer == manufKenwood)
        *txCodec = *rxCodec;
    else if (!lanCodecSupported(manufacturer, *txCodec, true))
        *txCodec = lanCodecDefault;

    return *rxCodec != oldRx || *txCodec != oldTx;
}

quint8 kenwoodVoipModeForCodec(quint8 codec)
{
    switch (codec) {
    case lanCodecAdpcm1ch:
        return 2;
    case lanCodecOpus1ch:
        return 3;
    default:
        return 1;
    }
}
