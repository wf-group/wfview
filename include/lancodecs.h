#pragma once

#include "wfviewtypes.h"

#include <QList>
#include <QtGlobal>

// Audio codecs for LAN connections, using Icom's codec byte values, which
// wfview also uses for Kenwood and Yaesu.
//
// Each manufacturer's list starts with the codecs their own radios support
// (oem == true), then the codecs only a wfview or wfserver server adds.

constexpr quint8 lanCodecULaw1ch = 0x01;
constexpr quint8 lanCodecPcm8Bit1ch = 0x02;
constexpr quint8 lanCodecPcm16Bit1ch = 0x04;
constexpr quint8 lanCodecPcm8Bit2ch = 0x08;
constexpr quint8 lanCodecPcm16Bit2ch = 0x10;
constexpr quint8 lanCodecULaw2ch = 0x20;
constexpr quint8 lanCodecOpus1ch = 0x40;
constexpr quint8 lanCodecOpus2ch = 0x41;
constexpr quint8 lanCodecAdpcm1ch = 0x80;

// Used when the saved codec is not one the manufacturer supports.
constexpr quint8 lanCodecDefault = lanCodecPcm16Bit1ch;

// Kenwood sends RX and TX audio at a fixed 16 kHz.
constexpr quint32 kenwoodLanSampleRate = 16000;

struct lanCodecInfo
{
    quint8 codec;
    const char *label; // Untranslated, translate in the "SettingsController" context.
    bool oem;
};

QList<lanCodecInfo> lanCodecs(manufacturersType_t manufacturer, bool tx);
bool lanCodecSupported(manufacturersType_t manufacturer, quint8 codec, bool tx);

// Replaces unsupported codecs with lanCodecDefault. Kenwood sets both
// directions with one command, so its TX codec follows the RX codec.
// Returns true if either codec changed.
bool lanSanitizeCodecs(manufacturersType_t manufacturer, quint8 *rxCodec, quint8 *txCodec);

// The ##VP mode that asks a Kenwood server for this codec:
// 1 = LPCM 16 bit, 2 = ADPCM, 3 = Opus (wfview servers only).
quint8 kenwoodVoipModeForCodec(quint8 codec);
