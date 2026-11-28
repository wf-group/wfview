#include "yaesustatusblock.h"

namespace
{
// Positions in wfview's data[] (status byte + 15).
constexpr int splitByte = 18; // bit 5 split
constexpr int txByte = 22; // bit 7 transmitting
constexpr int attPreampByte = 27; // bits 0-1 ATT, 2-3 IPO/AMP1/AMP2, 4-5 auto notch (2 in FM modes)
constexpr int dspFlagsByte = 28; // bit 6 APF on, bit 4 manual notch on
constexpr int notchByte = 42; // u16 BE, notch frequency / 10 Hz
constexpr int ifShiftByte = 46; // s16 BE, IF shift in Hz
constexpr int apfLevelByte = 48; // CO03 value 0-50
constexpr int cwPitchByte = 49; // CW pitch / 10 Hz
constexpr int clarifierByte = 82; // s16 BE, clarifier offset in Hz (active VFO)
constexpr int tuningFlagsByte = 111; // bit 2 dial lock, bit 4 NR on
constexpr int keySpeedByte = 121; // WPM
constexpr int minimumSize = keySpeedByte + 1;

// The scope window, see yaesuScopeWindow.
constexpr int scopeModeByte = 17;
constexpr int scopeSpanByte = 32;
constexpr quint8 scopeSpanMask = 0x0f;
constexpr int scopeWindowModeByte = 52;
constexpr int scopeWindowStartByte = 144;
constexpr int scopeWindowMinimumSize = scopeWindowStartByte + 4;
// The highest ScopeModes Num in FT-710.rig (Fixed (Normal)).
constexpr quint8 lastScopeMode = 0x0a;

// The CAT decoder scales APF level from the radio's 0-50 to wfview's 0-255.
constexpr int apfMax = 50;

quint8 byteAt(const QByteArray &data, int i)
{
    return quint8(data.at(i));
}

quint16 u16At(const QByteArray &data, int i)
{
    return quint16((byteAt(data, i) << 8) | byteAt(data, i + 1));
}

quint32 u32At(const QByteArray &data, int i)
{
    return (quint32(u16At(data, i)) << 16) | u16At(data, i + 2);
}
}

QHash<funcs, QVariant> yaesuStatusBlockValues(const QByteArray &data)
{
    QHash<funcs, QVariant> v;
    if (data.size() < minimumSize)
    {
        return v;
    }

    v.insert(funcSplitStatus, QVariant::fromValue<bool>(byteAt(data, splitByte) & 0x20));
    v.insert(funcTransceiverStatus, QVariant::fromValue<bool>(byteAt(data, txByte) & 0x80));

    const quint8 attPreamp = byteAt(data, attPreampByte);
    v.insert(funcAttenuator, QVariant::fromValue<uchar>(attPreamp & 0x03));
    v.insert(funcPreamp, QVariant::fromValue<uchar>((attPreamp >> 2) & 0x03));
    // 2 means an FM mode, where the radio has no auto notch; leave it to the CAT poll.
    const quint8 autoNotch = (attPreamp >> 4) & 0x03;
    if (autoNotch < 2)
    {
        v.insert(funcAutoNotch, QVariant::fromValue<bool>(autoNotch == 1));
    }

    const quint8 dspFlags = byteAt(data, dspFlagsByte);
    v.insert(funcAPFType, QVariant::fromValue<bool>(dspFlags & 0x40));
    v.insert(funcManualNotch, QVariant::fromValue<bool>(dspFlags & 0x10));

    v.insert(funcNotchFilter, QVariant::fromValue<ushort>(u16At(data, notchByte)));
    // Negative shifts travel in a ushort, as from the CAT decoder.
    v.insert(funcIFShift, QVariant::fromValue<ushort>(u16At(data, ifShiftByte)));

    const int apfLevel = qBound(0, int(byteAt(data, apfLevelByte)), apfMax);
    v.insert(funcAPFLevel, QVariant::fromValue<ushort>(ushort((apfLevel * 255 + apfMax / 2) / apfMax)));

    v.insert(funcCwPitch, QVariant::fromValue<ushort>(ushort(byteAt(data, cwPitchByte) * 10)));
    v.insert(funcRitFreq, QVariant::fromValue<short>(short(u16At(data, clarifierByte))));

    const quint8 tuningFlags = byteAt(data, tuningFlagsByte);
    v.insert(funcLockFunction, QVariant::fromValue<bool>(tuningFlags & 0x04));
    v.insert(funcNoiseReduction, QVariant::fromValue<uchar>((tuningFlags & 0x10) ? 1 : 0));

    v.insert(funcKeySpeed, QVariant::fromValue<uchar>(byteAt(data, keySpeedByte)));
    return v;
}

QHash<funcs, QVariant> YaesuStatusConfirm::update(const QHash<funcs, QVariant> &values,
                                                  QHash<funcs, QVariant> *confirmed)
{
    QHash<funcs, QVariant> changed;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
    {
        const auto prev = m_previous.constFind(it.key());
        if (prev == m_previous.constEnd() || prev.value() != it.value())
        {
            continue;
        }
        if (confirmed)
        {
            confirmed->insert(it.key(), it.value());
        }
        const auto reported = m_reported.constFind(it.key());
        if (reported == m_reported.constEnd() || reported.value() != it.value())
        {
            changed.insert(it.key(), it.value());
            m_reported.insert(it.key(), it.value());
        }
    }
    m_previous = values;
    return changed;
}

void YaesuStatusConfirm::reset()
{
    m_previous.clear();
    m_reported.clear();
}

quint8 yaesuScopeModeWindowMode(quint8 scopeMode)
{
    // FT-710.rig ScopeModes: 00-02 3DSS Center, Cursor, Fixed; 03-05 Center; 06-08 Cursor; 09-0A Fixed.
    if (scopeMode <= yaesuScopeWindow::fixed)
    {
        return scopeMode;
    }
    if (scopeMode <= lastScopeMode)
    {
        return quint8((scopeMode - 3) / 3);
    }
    return yaesuScopeWindow::unknown;
}

yaesuScopeWindow yaesuScopeWindowValues(const QByteArray &data)
{
    yaesuScopeWindow w;
    if (data.size() < scopeWindowMinimumSize)
    {
        return w;
    }
    const quint8 mode = byteAt(data, scopeWindowModeByte);
    if (mode == yaesuScopeWindow::unknown || mode != yaesuScopeModeWindowMode(byteAt(data, scopeModeByte)))
    {
        return w;
    }
    w.mode = mode;
    w.spanReg = byteAt(data, scopeSpanByte) & scopeSpanMask;
    w.startHz = u32At(data, scopeWindowStartByte);
    return w;
}

bool yaesuScopeWindowEdges(const yaesuScopeWindow &window, quint32 spanHz, double *startMHz, double *endMHz)
{
    if ((window.mode != yaesuScopeWindow::cursor && window.mode != yaesuScopeWindow::fixed)
        || window.startHz == 0 || spanHz == 0)
    {
        return false;
    }
    *startMHz = window.startHz / 1000000.0;
    *endMHz = (quint64(window.startHz) + spanHz) / 1000000.0;
    return true;
}

bool YaesuScopeWindowConfirm::update(const yaesuScopeWindow &window)
{
    const bool agrees = m_havePrevious && window == m_previous;
    m_previous = window;
    m_havePrevious = true;
    if (!agrees || window == m_confirmed)
    {
        return false;
    }
    m_confirmed = window;
    return true;
}

void YaesuScopeWindowConfirm::reset()
{
    m_previous = yaesuScopeWindow();
    m_confirmed = yaesuScopeWindow();
    m_havePrevious = false;
}
