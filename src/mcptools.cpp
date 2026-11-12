#include "mcptools.h"

#include "audioconverter.h"
#include "cachingqueue.h"
#include "mcphost.h"
#include "mcpprobe.h"
#include "rigidentities.h"
#include "wfviewtypes.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMutex>
#include <QSysInfo>
#include <QTcpSocket>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <memory>

namespace
{

constexpr int defaultSettingsLimit = 1000;
constexpr int maxSettingsLimit = 5000;
constexpr int defaultSettingsFileBytes = 256 * 1024;
constexpr int maxSettingsFileBytes = 2 * 1024 * 1024;
constexpr int defaultCacheLimit = 300;
constexpr int maxRowLimit = 5000;
constexpr int defaultLogLines = 200;
constexpr int maxLogLines = 5000;
constexpr int maxHexBytes = 256;
constexpr int overviewHostTimeoutMs = 1000;
constexpr int defaultCaptureSeconds = 60;
constexpr int maxCaptureSeconds = 900;
constexpr int defaultTrafficLimit = 200;
constexpr int defaultSpectrumPoints = 128;
constexpr int defaultSpectrumPeaks = 10;
constexpr int maxSpectrumPeaks = 50;
constexpr int defaultRawWaitMs = 500;
constexpr int maxRawWaitMs = 5000;
constexpr int maxRawBytes = 256;
constexpr int rawCaptureSeconds = 10;
constexpr int defaultTransmitSeconds = 10;
constexpr int maxTransmitSeconds = 120;
constexpr int defaultRigctldTimeoutMs = 2000;
constexpr int maxRigctldTimeoutMs = 10000;
// rigctld sends no end marker after a plain get, so a reply is taken as
// complete once the socket has been quiet this long.
constexpr int rigctldQuietMs = 300;

const QStringList rigCapSections = {QStringLiteral("modes"),
                                    QStringLiteral("commands"),
                                    QStringLiteral("inputs"),
                                    QStringLiteral("bands"),
                                    QStringLiteral("filters"),
                                    QStringLiteral("widths"),
                                    QStringLiteral("attenuators"),
                                    QStringLiteral("preamps"),
                                    QStringLiteral("agc"),
                                    QStringLiteral("antennas"),
                                    QStringLiteral("roofing"),
                                    QStringLiteral("scope"),
                                    QStringLiteral("steps"),
                                    QStringLiteral("tones"),
                                    QStringLiteral("meters"),
                                    QStringLiteral("periodic"),
                                    QStringLiteral("memories")};

// getCacheItems() and getQueueItems() return with the queue locked.
struct QueueUnlocker
{
    cachingQueue *queue;
    ~QueueUnlocker()
    {
        queue->unlockMutex();
    }
};

QJsonObject schemaInt(const QString &description, int minimum, int maximum)
{
    return QJsonObject{{"type", "integer"}, {"description", description}, {"minimum", minimum}, {"maximum", maximum}};
}

QJsonObject schemaString(const QString &description)
{
    return QJsonObject{{"type", "string"}, {"description", description}};
}

QJsonObject schemaBool(const QString &description)
{
    return QJsonObject{{"type", "boolean"}, {"description", description}};
}

QJsonObject schemaEnum(const QString &description, const QStringList &values)
{
    return QJsonObject{{"type", "string"}, {"description", description}, {"enum", QJsonArray::fromStringList(values)}};
}

int argInt(const QJsonObject &args, const QString &key, int fallback, int minimum, int maximum)
{
    const QJsonValue v = args.value(key);
    if (!v.isDouble())
        return fallback;
    return qBound(minimum, v.toInt(fallback), maximum);
}

QString funcName(int func)
{
    if (func >= 0 && func < funcLastFunc)
        return funcString[func];
    return QString::number(func);
}

// A radio function by its name as get_cache shows it, or by number.
funcs findFunc(const QString &name)
{
    bool isNumber = false;
    const int number = name.toInt(&isNumber);
    if (isNumber)
        return (number > funcNone && number < funcLastFunc) ? funcs(number) : funcNone;
    for (int i = funcNone + 1; i < funcLastFunc; ++i)
    {
        if (funcString[i].compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return funcs(i);
    }
    return funcNone;
}

// Converts a JSON value to the type wfview uses for a function, which is
// learned from the value already in the cache.
bool jsonToFuncValue(const QJsonValue &json, const QVariant &current, QVariant *out, QString *error)
{
    const int type = current.userType();
    const auto number = [&](double minimum, double maximum, double *value) {
        if (json.isBool())
        {
            *value = json.toBool() ? 1 : 0;
            return true;
        }
        if (!json.isDouble() || json.toDouble() < minimum || json.toDouble() > maximum)
        {
            *error = QStringLiteral("The value must be a number from %1 to %2.").arg(minimum).arg(maximum);
            return false;
        }
        *value = json.toDouble();
        return true;
    };

    double v = 0;
    switch (type)
    {
    case QMetaType::Bool:
        if (!json.isBool() && !json.isDouble())
        {
            *error = QStringLiteral("The value must be true or false.");
            return false;
        }
        *out = QVariant::fromValue<bool>(json.isBool() ? json.toBool() : json.toDouble() != 0);
        return true;
    case QMetaType::UChar:
        if (!number(0, 255, &v))
            return false;
        *out = QVariant::fromValue<uchar>(uchar(v));
        return true;
    case QMetaType::UShort:
        if (!number(0, 65535, &v))
            return false;
        *out = QVariant::fromValue<ushort>(ushort(v));
        return true;
    case QMetaType::Short:
        if (!number(-32768, 32767, &v))
            return false;
        *out = QVariant::fromValue<short>(short(v));
        return true;
    case QMetaType::Int:
        if (!number(-2147483648.0, 2147483647.0, &v))
            return false;
        *out = QVariant::fromValue<int>(int(v));
        return true;
    case QMetaType::UInt:
        if (!number(0, 4294967295.0, &v))
            return false;
        *out = QVariant::fromValue<uint>(uint(v));
        return true;
    case QMetaType::Double:
        if (!number(-1e15, 1e15, &v))
            return false;
        *out = QVariant::fromValue<double>(v);
        return true;
    case QMetaType::QString:
        if (!json.isString())
        {
            *error = QStringLiteral("The value must be a string.");
            return false;
        }
        *out = QVariant::fromValue<QString>(json.toString());
        return true;
    default:
        *error = QStringLiteral("send_command only handles simple values (boolean, number or text); this function "
                                "uses %1. Use set_frequency, set_mode or send_raw instead.")
                     .arg(QString::fromLatin1(current.typeName()));
        return false;
    }
}

// Parses "FE FE 94 E0 03 FD", "fefe94e003fd" or "fe:fe:94".
bool parseHex(const QString &text, QByteArray *out)
{
    QByteArray digits;
    for (const QChar ch : text)
    {
        if (ch.isSpace() || ch == QLatin1Char(':') || ch == QLatin1Char(','))
            continue;
        if (!QByteArrayLiteral("0123456789abcdefABCDEF").contains(ch.toLatin1()))
            return false;
        digits.append(ch.toLatin1());
    }
    if (digits.isEmpty() || digits.size() % 2 != 0)
        return false;
    *out = QByteArray::fromHex(digits);
    return true;
}

QJsonValue timeJson(const QDateTime &t)
{
    return t.isValid() ? QJsonValue(t.toString(Qt::ISODateWithMs)) : QJsonValue();
}

QJsonValue ageJson(const QDateTime &t, const QDateTime &now)
{
    return t.isValid() ? QJsonValue(double(t.msecsTo(now))) : QJsonValue();
}

QJsonValue hexJson(const QByteArray &data)
{
    QString hex = QString::fromLatin1(data.left(maxHexBytes).toHex(' '));
    if (data.size() > maxHexBytes)
        hex += QStringLiteral(" ... (%1 bytes)").arg(data.size());
    return hex;
}

QString priorityName(int priority)
{
    switch (priority)
    {
    case priorityNone:
        return QStringLiteral("none");
    case priorityImmediate:
        return QStringLiteral("immediate");
    case priorityHighest:
        return QStringLiteral("highest");
    case priorityHigh:
        return QStringLiteral("high");
    case priorityMediumHigh:
        return QStringLiteral("mediumHigh");
    case priorityMedium:
        return QStringLiteral("medium");
    case priorityMediumLow:
        return QStringLiteral("mediumLow");
    case priorityLow:
        return QStringLiteral("low");
    case priorityLowest:
        return QStringLiteral("lowest");
    }
    return QString::number(priority);
}

QString meterName(int type)
{
    constexpr int meterNames = int(sizeof(meterString) / sizeof(meterString[0]));
    if (type >= 0 && type < meterNames)
        return meterString[type];
    return QString::number(type);
}

QJsonObject freqJson(const freqt &f)
{
    return QJsonObject{{"hz", double(f.Hz)}, {"mhz", f.MHzDouble}, {"vfo", int(f.VFO)}};
}

QJsonObject toneJson(const toneInfo &t)
{
    return QJsonObject{{"tone", int(t.tone)},
                       {"name", t.name},
                       {"txInvert", t.tinv},
                       {"rxInvert", t.rinv},
                       {"useSecondaryVfo", t.useSecondaryVFO}};
}

QJsonObject modeJson(const modeInfo &m)
{
    return QJsonObject{{"name", m.name},
                       {"mode", int(m.mk)},
                       {"reg", int(m.reg)},
                       {"filter", int(m.filter)},
                       {"data", int(m.data)},
                       {"vfo", int(m.VFO)},
                       {"bwMin", m.bwMin},
                       {"bwMax", m.bwMax},
                       {"pass", m.pass}};
}

QJsonArray genericListJson(const std::vector<genericType> &list)
{
    QJsonArray out;
    for (const genericType &g : list)
        out.append(QJsonObject{{"num", int(g.num)}, {"name", g.name}});
    return out;
}

} // namespace

QString Mcp::maskSecret(const QString &value)
{
    if (value.isEmpty())
        return value;
    if (value.size() <= 2)
        return QStringLiteral("XXX");
    return value.left(1) + QStringLiteral("XXX") + value.right(1);
}

bool Mcp::isSecretKey(const QString &key)
{
    const QString name = key.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1).toLower();
    return name.contains(QLatin1String("password")) || name.contains(QLatin1String("passwd"))
           || name.contains(QLatin1String("secret")) || name.contains(QLatin1String("token"))
           || name == QLatin1String("pass") || name.endsWith(QLatin1String("pwd"));
}

QString Mcp::maskSettingsText(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('[')) || trimmed.startsWith(QLatin1Char(';'))
            || trimmed.startsWith(QLatin1Char('#')))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0 || !isSecretKey(line.left(eq).trimmed()))
            continue;

        const bool cr = line.endsWith(QLatin1Char('\r'));
        QString value = line.mid(eq + 1).trimmed();
        const bool quoted = value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'));
        if (quoted)
            value = value.mid(1, value.size() - 2);

        QString masked = maskSecret(value);
        if (quoted)
            masked = QLatin1Char('"') + masked + QLatin1Char('"');
        line = line.left(eq + 1) + masked + (cr ? QStringLiteral("\r") : QString());
    }
    return lines.join(QLatin1Char('\n'));
}

bool Mcp::isWritableOption(const QString &key, QString *reason)
{
    // Groups are listed rather than assumed, so a new group stays read-only
    // until someone decides it is safe to change from MCP.
    static const QStringList writableGroups = {QStringLiteral("AudioProc"),
                                               QStringLiteral("Cluster"),
                                               QStringLiteral("Color"),
                                               QStringLiteral("Controls"),
                                               QStringLiteral("Experimental"),
                                               QStringLiteral("Interface"),
                                               QStringLiteral("LAN"),
                                               QStringLiteral("Radio"),
                                               QStringLiteral("Server"),
                                               QStringLiteral("UDP")};

    if (isSecretKey(key))
    {
        if (reason)
            *reason = QStringLiteral("%1 holds a password, which MCP never changes.").arg(key);
        return false;
    }
    if (!writableGroups.contains(key.section(QLatin1Char('.'), 0, 0)))
    {
        if (reason)
            *reason = QStringLiteral("Settings in the %1 group cannot be changed through MCP.")
                          .arg(key.section(QLatin1Char('.'), 0, 0));
        return false;
    }
    return true;
}

QJsonValue Mcp::variantToJson(const QVariant &v)
{
    if (!v.isValid())
        return QJsonValue();

    const int t = v.userType();
    switch (t)
    {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong:
        return double(v.toLongLong());
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        return double(v.toULongLong());
    case QMetaType::Float:
    case QMetaType::Double:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString();
    case QMetaType::QStringList:
        return QJsonArray::fromStringList(v.toStringList());
    case QMetaType::QByteArray:
        return hexJson(v.toByteArray());
    case QMetaType::QDateTime:
        return timeJson(v.toDateTime());
    default:
        break;
    }

    if (t == qMetaTypeId<freqt>())
        return freqJson(v.value<freqt>());
    if (t == qMetaTypeId<modeInfo>())
        return modeJson(v.value<modeInfo>());
    if (t == qMetaTypeId<toneInfo>())
        return toneJson(v.value<toneInfo>());
    if (t == qMetaTypeId<scopeData>())
    {
        const scopeData s = v.value<scopeData>();
        return QJsonObject{{"valid", s.valid},
                           {"receiver", int(s.receiver)},
                           {"mode", int(s.mode)},
                           {"fixedEdge", int(s.fixedEdge)},
                           {"outOfRange", s.oor},
                           {"startFreqMHz", s.startFreq},
                           {"endFreqMHz", s.endFreq},
                           {"points", int(s.data.size())}};
    }
    if (t == qMetaTypeId<meterkind>())
    {
        const meterkind m = v.value<meterkind>();
        return QJsonObject{{"meter", meterName(int(m.type))}, {"value", m.value}};
    }
    if (t == qMetaTypeId<timekind>())
    {
        const timekind k = v.value<timekind>();
        return QJsonObject{{"hours", int(k.hours)}, {"minutes", int(k.minutes)}, {"isMinus", k.isMinus}};
    }
    if (t == qMetaTypeId<datekind>())
    {
        const datekind k = v.value<datekind>();
        return QJsonObject{{"year", int(k.year)}, {"month", int(k.month)}, {"day", int(k.day)}};
    }
    if (t == qMetaTypeId<antennaInfo>())
    {
        const antennaInfo a = v.value<antennaInfo>();
        return QJsonObject{{"antenna", int(a.antenna)}, {"rx", a.rx}};
    }
    if (t == qMetaTypeId<rptrAccessData>())
    {
        const rptrAccessData r = v.value<rptrAccessData>();
        return QJsonObject{{"accessMode", int(r.accessMode)},
                           {"useSecondaryVfo", r.useSecondaryVFO},
                           {"turnOffTone", r.turnOffTone},
                           {"turnOffTsql", r.turnOffTSQL},
                           {"usingSequence", r.usingSequence},
                           {"sequence", r.sequence}};
    }
    if (t == qMetaTypeId<spectrumBounds>())
    {
        const spectrumBounds b = v.value<spectrumBounds>();
        return QJsonObject{{"start", b.start}, {"end", b.end}, {"edge", int(b.edge)}};
    }
    if (t == qMetaTypeId<lpfhpf>())
    {
        const lpfhpf f = v.value<lpfhpf>();
        return QJsonObject{{"lpf", int(f.lpf)}, {"hpf", int(f.hpf)}};
    }
    if (t == qMetaTypeId<centerSpanData>())
    {
        const centerSpanData c = v.value<centerSpanData>();
        return QJsonObject{{"reg", int(c.reg)}, {"name", c.name}, {"freq", double(c.freq)}};
    }
    if (t == qMetaTypeId<bandStackType>())
    {
        const bandStackType b = v.value<bandStackType>();
        return QJsonObject{{"band", int(b.band)},
                           {"reg", int(b.reg)},
                           {"freq", freqJson(b.freq)},
                           {"data", int(b.data)},
                           {"mode", int(b.mode)},
                           {"filter", int(b.filter)}};
    }
    if (t == qMetaTypeId<widthsType>())
    {
        const widthsType w = v.value<widthsType>();
        return QJsonObject{{"bands", int(w.bands)}, {"num", int(w.num)}, {"hz", int(w.hz)}};
    }
    if (t == qMetaTypeId<rigInput>())
    {
        const rigInput r = v.value<rigInput>();
        return QJsonObject{{"type", int(r.type)}, {"reg", int(r.reg)}, {"name", r.name}, {"level", int(r.level)}};
    }
    if (t == qMetaTypeId<duplexMode_t>())
        return int(v.value<duplexMode_t>());
    if (t == qMetaTypeId<rptAccessTxRx_t>())
        return int(v.value<rptAccessTxRx_t>());
    if (t == qMetaTypeId<vfo_t>())
        return int(v.value<vfo_t>());
    if (t == qMetaTypeId<rigMode_t>())
        return int(v.value<rigMode_t>());
    if (t == qMetaTypeId<meter_t>())
        return meterName(int(v.value<meter_t>()));
    if (t == qMetaTypeId<funcs>())
        return funcName(int(v.value<funcs>()));

    QJsonObject other;
    other["type"] = QString::fromLatin1(v.typeName());
    if (v.canConvert<QString>())
        other["text"] = v.toString();
    return other;
}

QString Mcp::manufacturerName(int manufacturer)
{
    switch (manufacturer)
    {
    case manufIcom:
        return QStringLiteral("Icom");
    case manufKenwood:
        return QStringLiteral("Kenwood");
    case manufYaesu:
        return QStringLiteral("Yaesu");
    case manufFlexRadio:
        return QStringLiteral("FlexRadio");
    }
    return QString::number(manufacturer);
}

QJsonObject Mcp::radioIdJson(int manufacturer, quint16 id)
{
    return QJsonObject{{"decimal", int(id)},
                       {"hex", QStringLiteral("0x%1").arg(id, 2, 16, QLatin1Char('0'))},
                       {"kind",
                        manufacturer == manufIcom ? QStringLiteral("CI-V address") : QStringLiteral("model ID")}};
}

QString Mcp::audioTypeName(int type)
{
    switch (type)
    {
    case qtAudio:
        return QStringLiteral("Qt");
    case portAudio:
        return QStringLiteral("PortAudio");
    case rtAudio:
        return QStringLiteral("RtAudio");
    }
    return QString::number(type);
}

QString Mcp::codecName(quint8 codec)
{
    switch (codec)
    {
    case 0x01:
        return QStringLiteral("uLaw 1ch 8bit");
    case 0x02:
        return QStringLiteral("PCM 1ch 8bit");
    case 0x04:
        return QStringLiteral("PCM 1ch 16bit");
    case 0x08:
        return QStringLiteral("PCM 2ch 8bit");
    case 0x10:
        return QStringLiteral("PCM 2ch 16bit");
    case 0x20:
        return QStringLiteral("uLaw 2ch 8bit");
    case 0x40:
        return QStringLiteral("Opus 1ch");
    case 0x41:
        return QStringLiteral("Opus 2ch");
    case 0x80:
        return QStringLiteral("ADPCM 1ch");
    }
    return QStringLiteral("unknown");
}

QJsonObject Mcp::audioSetupToJson(const audioSetup &setup)
{
    QJsonObject o;
    o["audioSystem"] = audioTypeName(int(setup.type));
    o["direction"] = setup.isinput ? QStringLiteral("input") : QStringLiteral("output");
    o["deviceName"] = setup.name;
    o["sampleRate"] = double(setup.sampleRate);
    o["codec"] = QStringLiteral("0x%1").arg(setup.codec, 2, 16, QLatin1Char('0'));
    o["codecName"] = codecName(setup.codec);
    o["latencyMs"] = int(setup.latency);
    o["blockSizeMs"] = int(setup.blockSize);
    o["resampleQuality"] = int(setup.resampleQuality);
    o["localAfGain"] = int(setup.localAFgain);
    o["portIndex"] = setup.portInt;
    o["deviceIsNull"] = setup.port.isNull();
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    o["deviceDescription"] = setup.port.deviceName();
#else
    o["deviceDescription"] = setup.port.description();
    o["deviceId"] = QString::fromUtf8(setup.port.id());
#endif
    return o;
}

QJsonObject Mcp::rigCapsToJson(const rigCapabilities &caps, const QStringList &sections)
{
    QJsonObject o;
    o["modelName"] = caps.modelName;
    o["model"] = int(caps.model);
    o["radioId"] = radioIdJson(caps.manufacturer, caps.modelID);
    o["manufacturer"] = manufacturerName(caps.manufacturer);
    o["rigFile"] = caps.filename;
    o["rigctlModel"] = caps.rigctlModel;
    o["baudRate"] = double(caps.baudRate);
    o["numReceivers"] = int(caps.numReceiver);
    o["numVfos"] = int(caps.numVFO);
    o["spectrum"] = QJsonObject{{"hasSpectrum", caps.hasSpectrum},
                                {"seqMax", int(caps.spectSeqMax)},
                                {"ampMax", int(caps.spectAmpMax)},
                                {"lenMax", int(caps.spectLenMax)},
                                {"dbPerDN", caps.spectDbPerDN}};
    o["features"] = QJsonObject{{"hasLan", caps.hasLan},
                                {"hasEthernet", caps.hasEthernet},
                                {"hasWiFi", caps.hasWiFi},
                                {"hasFDcomms", caps.hasFDcomms},
                                {"hasNB", caps.hasNB},
                                {"hasDD", caps.hasDD},
                                {"hasDV", caps.hasDV},
                                {"hasATU", caps.hasATU},
                                {"hasCTCSS", caps.hasCTCSS},
                                {"hasDTCS", caps.hasDTCS},
                                {"hasRepeaterModes", caps.hasRepeaterModes},
                                {"hasTransmit", caps.hasTransmit},
                                {"hasPTTCommand", caps.hasPTTCommand},
                                {"hasAttenuator", caps.hasAttenuator},
                                {"hasPreamp", caps.hasPreamp},
                                {"hasAntennaSel", caps.hasAntennaSel},
                                {"hasIFShift", caps.hasIFShift},
                                {"hasTBPF", caps.hasTBPF},
                                {"hasRXAntenna", caps.hasRXAntenna},
                                {"hasSpecifyMainSubCmd", caps.hasSpecifyMainSubCmd},
                                {"hasVFOMS", caps.hasVFOMS},
                                {"hasVFOAB", caps.hasVFOAB},
                                {"hasAdvancedRptrToneCmds", caps.hasAdvancedRptrToneCmds},
                                {"hasQuickSplitCommand", caps.hasQuickSplitCommand},
                                {"hasCommand29", caps.hasCommand29},
                                {"absoluteVFOFreq", caps.absoluteVFOFreq}};
    o["counts"] = QJsonObject{{"modes", int(caps.modes.size())},
                              {"commands", int(caps.commands.size())},
                              {"inputs", int(caps.inputs.size())},
                              {"bands", int(caps.bands.size())},
                              {"filters", int(caps.filters.size())},
                              {"attenuators", int(caps.attenuators.size())},
                              {"preamps", int(caps.preamps.size())},
                              {"agc", int(caps.agcModes.size())},
                              {"antennas", int(caps.antennas.size())},
                              {"periodic", int(caps.periodic.size())}};

    const bool all = sections.contains(QStringLiteral("all"), Qt::CaseInsensitive);
    auto want = [&](const char *name) { return all || sections.contains(QLatin1String(name), Qt::CaseInsensitive); };

    if (want("modes"))
    {
        QJsonArray modes;
        for (const modeInfo &m : caps.modes)
            modes.append(modeJson(m));
        o["modes"] = modes;
    }
    if (want("commands"))
    {
        QJsonArray commands;
        for (auto it = caps.commands.cbegin(); it != caps.commands.cend(); ++it)
        {
            const funcType &f = it.value();
            commands.append(QJsonObject{{"func", funcName(int(it.key()))},
                                        {"id", int(it.key())},
                                        {"data", hexJson(f.data)},
                                        {"min", f.minVal},
                                        {"max", f.maxVal},
                                        {"get", f.getCmd},
                                        {"set", f.setCmd},
                                        {"padr", f.padr},
                                        {"cmd29", f.cmd29},
                                        {"bytes", int(f.bytes)},
                                        {"admin", f.admin}});
        }
        o["commands"] = commands;
    }
    if (want("inputs"))
    {
        QJsonArray inputs;
        for (const rigInput &r : caps.inputs)
            inputs.append(QJsonObject{{"type", int(r.type)}, {"reg", int(r.reg)}, {"name", r.name}});
        o["inputs"] = inputs;
    }
    if (want("bands"))
    {
        QJsonArray bands;
        for (const bandType &b : caps.bands)
            bands.append(QJsonObject{{"name", b.name},
                                     {"region", b.region},
                                     {"band", int(b.band)},
                                     {"bsr", int(b.bsr)},
                                     {"lowHz", double(b.lowFreq)},
                                     {"highHz", double(b.highFreq)},
                                     {"memGroup", b.memGroup},
                                     {"power", double(b.power)}});
        o["bands"] = bands;
    }
    if (want("filters"))
    {
        QJsonArray filters;
        for (const filterType &f : caps.filters)
            filters.append(QJsonObject{{"num", int(f.num)}, {"name", f.name}, {"modes", double(f.modes)}});
        o["filters"] = filters;
    }
    if (want("widths"))
    {
        QJsonArray widths;
        for (const widthsType &w : caps.widths)
            widths.append(QJsonObject{{"bands", int(w.bands)}, {"num", int(w.num)}, {"hz", int(w.hz)}});
        o["widths"] = widths;
    }
    if (want("attenuators"))
        o["attenuators"] = genericListJson(caps.attenuators);
    if (want("preamps"))
        o["preamps"] = genericListJson(caps.preamps);
    if (want("agc")) {
        QJsonArray agc;
        for (const agcType &a : caps.agcModes)
            agc.append(QJsonObject{{"num", int(a.num)}, {"name", a.name}, {"setOnly", a.setOnly}, {"readOnly", a.readOnly}});
        o["agc"] = agc;
    }
    if (want("antennas"))
        o["antennas"] = genericListJson(caps.antennas);
    if (want("roofing"))
        o["roofing"] = genericListJson(caps.roofing);
    if (want("scope"))
    {
        QJsonArray spans;
        for (const centerSpanData &c : caps.scopeCenterSpans)
            spans.append(QJsonObject{{"reg", int(c.reg)}, {"name", c.name}, {"freq", double(c.freq)}});
        o["scopeCenterSpans"] = spans;
        o["scopeModes"] = genericListJson(caps.scopeModes);
    }
    if (want("steps"))
    {
        QJsonArray steps;
        for (const stepType &s : caps.steps)
            steps.append(QJsonObject{{"num", int(s.num)}, {"name", s.name}, {"hz", double(s.hz)}});
        o["steps"] = steps;
    }
    if (want("tones"))
    {
        QJsonArray ctcss;
        for (const toneInfo &t : caps.ctcss)
            ctcss.append(toneJson(t));
        QJsonArray dtcs;
        for (const toneInfo &t : caps.dtcs)
            dtcs.append(toneJson(t));
        o["ctcss"] = ctcss;
        o["dtcs"] = dtcs;
    }
    if (want("meters"))
    {
        QJsonObject meters;
        for (int i = 0; i <= meterUnknown; ++i)
        {
            if (caps.meters[i].isEmpty())
                continue;
            QJsonObject points;
            for (auto it = caps.meters[i].cbegin(); it != caps.meters[i].cend(); ++it)
                points[QString::number(it.key())] = it.value();
            meters[meterName(i)] = QJsonObject{{"rawToValue", points}, {"redline", caps.meterLines[i]}};
        }
        o["meters"] = meters;
    }
    if (want("periodic"))
    {
        QJsonArray periodic;
        for (const periodicType &p : caps.periodic)
            periodic.append(QJsonObject{{"func", funcName(int(p.func))},
                                        {"priority", p.priority},
                                        {"prioVal", p.prioVal},
                                        {"receiver", int(p.receiver)},
                                        {"modes", QJsonArray::fromStringList(p.modes)}});
        o["periodic"] = periodic;
    }
    if (want("memories"))
    {
        o["memories"] = QJsonObject{{"groups", int(caps.memGroups)},
                                    {"count", int(caps.memories)},
                                    {"start", int(caps.memStart)},
                                    {"format", caps.memFormat},
                                    {"satMemories", int(caps.satMemories)},
                                    {"satFormat", caps.satFormat}};
    }
    return o;
}

McpTools::McpTools(McpHost *host, const McpAppInfo &info, const std::atomic_bool *stopping)
    : m_host(host)
    , m_info(info)
    , m_stopping(stopping)
{
    addTool("get_overview",
            "Overview",
            "Start here. Returns the application and build, MCP mode, settings file, the connected radio, main-thread "
            "health, audio stream summary, cache and queue sizes, and a summary of the application state.",
            {},
            [this](const QJsonObject &a) { return toolOverview(a); });

    addTool("get_main_thread_health",
            "Main thread health",
            "Checks whether the main (GUI) thread is responsive. Reports how long a no-op takes to run on it, how long "
            "since its heartbeat timer last ran, and the longest stall seen. Use this when the interface seems frozen "
            "or slow.",
            {{"timeoutMs", schemaInt("How long to wait for the main thread (default 2000).", 100, 10000)},
             {"resetWorst", schemaBool("Reset the longest-stall record after reading it.")}},
            [this](const QJsonObject &a) { return toolMainThreadHealth(a); });

    addTool("get_windows",
            "Windows",
            "Lists the application's top-level windows with their titles, type, visibility, size, position, size "
            "limits and screen, plus the screens and their usable areas. Use this to check how windows are sized "
            "and placed.",
            {{"visibleOnly", schemaBool("Leave out windows that exist but are not shown.")}},
            [this](const QJsonObject &a) { return toolWindows(a); });

    addTool("get_settings",
            "Saved settings",
            "Returns saved settings as key/value pairs read from the settings store, with passwords masked. Changes "
            "not yet saved by the application are not shown.",
            {{"group", schemaString("Only keys in this group, for example \"LAN\" or \"Radios\".")},
             {"contains", schemaString("Only keys containing this text (case-insensitive).")},
             {"limit", schemaInt("Maximum number of keys (default 1000).", 1, maxSettingsLimit)}},
            [this](const QJsonObject &a) { return toolSettings(a); });

    addTool("get_settings_file",
            "Settings file",
            "Returns the path, size, modification time and raw text of the settings file, with password values "
            "masked.",
            {{"maxBytes", schemaInt("Maximum bytes to return (default 262144).", 1024, maxSettingsFileBytes)}},
            [this](const QJsonObject &a) { return toolSettingsFile(a); });

    addTool("get_connection",
            "Radio connection",
            "Returns how the application connects to the radio: connection type and status, serial port and baud "
            "rate, network addresses and ports, wfshare settings, and the rigctld, TCI and server ports. Passwords are "
            "masked.",
            {},
            [this](const QJsonObject &a) { return toolConnection(a); });

    addTool("get_rig_capabilities",
            "Rig capabilities",
            "Returns the capabilities of the connected radio from its rig file: model, CI-V address, rig file path, "
            "feature flags and list sizes. Ask for more detail with sections.",
            {{"sections",
              QJsonObject{{"type", "array"},
                          {"description", "Extra detail to include; \"all\" includes everything."},
                          {"items",
                           QJsonObject{{"type", "string"},
                                       {"enum", QJsonArray::fromStringList(rigCapSections + QStringList{"all"})}}}}},
             {"radio", schemaInt("wfserver radio index (default 0). wfview has one radio.", 0, 255)}},
            [this](const QJsonObject &a) { return toolRigCaps(a); });

    addTool("get_rig_files",
            "Rig files",
            "Lists the rig definition folders searched and every rig file found for the selected manufacturer, with "
            "CI-V address, model, version and path.",
            {},
            [this](const QJsonObject &a) { return toolRigFiles(a); });

    addTool("get_audio_config",
            "Audio configuration",
            "Returns the audio setup: audio system, the configured input and output devices, sample rates, codecs, "
            "latency and gains, which audio paths are active, and audio processing settings.",
            {},
            [this](const QJsonObject &a) { return toolAudioConfig(a); });

    addTool("get_audio_levels",
            "Audio levels",
            "Returns the most recent level measurements from every open audio device (peak, RMS, latency, underruns "
            "and how long since the last update) and the network audio status. Use this to find where audio stops.",
            {},
            [this](const QJsonObject &a) { return toolAudioLevels(a); });

    addTool("get_cache",
            "Radio state cache",
            "Returns the radio state cache: the latest value of each radio function, per receiver, with when it was "
            "last requested and last answered.",
            {{"func", schemaString("Only functions whose name contains this text, for example \"Freq\".")},
             {"receiver", schemaInt("Only this receiver (0 is main, 1 is sub).", 0, 255)},
             {"limit", schemaInt("Maximum number of entries (default 300).", 1, maxRowLimit)}},
            [this](const QJsonObject &a) { return toolCache(a); });

    addTool("get_queue",
            "Command queue",
            "Returns the commands waiting to be sent to the radio, with priority, receiver, whether each is a get or "
            "a set, its value, and whether it repeats.",
            {{"func", schemaString("Only functions whose name contains this text.")},
             {"limit", schemaInt("Maximum number of entries (default 300).", 1, maxRowLimit)}},
            [this](const QJsonObject &a) { return toolQueue(a); });

    addTool("get_meters",
            "Meters",
            "Returns the latest meter readings (S-meter, power, SWR, ALC, compression, voltage, current and so on) "
            "from the radio state cache.",
            {{"receiver", schemaInt("Only this receiver (0 is main, 1 is sub).", 0, 255)}},
            [this](const QJsonObject &a) { return toolMeters(a); });

    addTool("get_log",
            "Log",
            "Returns recent log lines, newest last. The buffer holds the last 5000 lines since MCP started.",
            {{"lines", schemaInt("Maximum lines to return (default 200).", 1, maxLogLines)},
             {"level", schemaEnum("Lowest level to include (default debug).", {"debug", "info", "warning", "critical"})},
             {"category", schemaString("Only this log category or its children, for example \"rig\" or \"audio\".")},
             {"contains", schemaString("Only lines containing this text (case-insensitive).")}},
            [this](const QJsonObject &a) { return toolLog(a); });

    addTool("set_traffic_capture",
            "Traffic capture",
            "Starts or stops recording the bytes exchanged with the radio: CI-V or CAT commands on any transport, and "
            "Icom or Yaesu network control-channel packets (keepalives left out). Capture only affects MCP, stops by "
            "itself after the given time, and is off until started. Read the result with get_traffic.",
            {{"seconds", schemaInt("How long to record (default 60).", 1, maxCaptureSeconds)},
             {"stop", schemaBool("Stop now instead of starting.")},
             {"includeScope", schemaBool("Also record spectrum data from the radio, which is very busy (default false).")},
             {"clear", schemaBool("Discard earlier captured traffic first (default true).")}},
            [this](const QJsonObject &a) { return toolSetTrafficCapture(a); });

    addTool("get_traffic",
            "Captured traffic",
            "Returns captured radio and network traffic, oldest first, as hex and, for text protocols such as Kenwood "
            "CAT, as text. Start a capture with set_traffic_capture first.",
            {{"kind", schemaEnum("Which traffic (default all).", {"radio", "network", "all"})},
             {"direction", schemaEnum("Only one direction.", {"toRadio", "fromRadio"})},
             {"limit", schemaInt("Maximum entries, newest kept (default 200).", 1, maxRowLimit)},
             {"contains", schemaString("Only entries whose hex or text contains this, for example \"FA;\" or \"fe fe\".")}},
            [this](const QJsonObject &a) { return toolTraffic(a); });

    addTool("get_spectrum",
            "Spectrum",
            "Returns the latest spectrum scope sweep for a receiver: frequency range, levels reduced to the requested "
            "number of points (keeping the highest level in each), the noise floor, and the strongest signals.",
            {{"receiver", schemaInt("Receiver (default 0, main).", 0, 255)},
             {"points", schemaInt("Number of points to return (default 128; the radio's full width if larger).", 8, 4096)},
             {"peaks", schemaInt("How many of the strongest signals to list (default 10).", 0, maxSpectrumPeaks)}},
            [this](const QJsonObject &a) { return toolSpectrum(a); });

    addTool("get_radio_server",
            "Radio server",
            "Returns the built-in radio server (remote access to a radio on a serial port): whether it is "
            "enabled and running and why not, its protocol, address and ports, the server audio devices, who is "
            "logged in from where, the number of logins since it started, and every user account with its "
            "access level and status (logged in, logged out or never seen since the server started). Passwords "
            "are masked.",
            {},
            [this](const QJsonObject &a) { return toolRadioServer(a); });

    addTool("get_setting_options",
            "Setting options",
            "Lists the settings that can be read and changed by name (the names set_setting takes) with their current "
            "values, including changes not saved yet, and which of them MCP may not change. Passwords are masked.",
            {{"contains", schemaString("Only names containing this text (case-insensitive).")}},
            [this](const QJsonObject &a) { return toolSettingOptions(a); });

    // Write tools, only available with --mcp-allow-write.

    addWriteTool("set_setting",
                 "Change a setting",
                 "Changes one setting by name the way the settings dialog does, so it takes effect at once. Names "
                 "come from get_setting_options; passwords cannot be changed. The change is not written to the "
                 "settings file unless save is true.",
                 {{"key", schemaString("Setting name, for example \"LAN.RigCtlPort\".")},
                  {"value",
                   QJsonObject{{"type", QJsonArray{"boolean", "number", "string"}}, {"description", "The new value."}}},
                  {"save", schemaBool("Then save all settings to the settings file (default false).")}},
                 [this](const QJsonObject &a) { return toolSetSetting(a); });

    addWriteTool("set_frequency",
                 "Set frequency",
                 "Tunes a receiver of the radio to a frequency in Hz, through the command queue like any other "
                 "frequency change.",
                 {{"hz", QJsonObject{{"type", "number"}, {"description", "Frequency in Hz."}, {"minimum", 1}}},
                  {"receiver", schemaInt("Receiver (default 0, main).", 0, 255)}},
                 [this](const QJsonObject &a) { return toolSetFrequency(a); });

    addWriteTool("set_mode",
                 "Set mode",
                 "Sets the operating mode of a receiver by name, as listed by get_rig_capabilities with sections "
                 "[\"modes\"]. The current data mode and filter are kept unless given.",
                 {{"mode", schemaString("Mode name, for example \"USB\" or \"CW\".")},
                  {"receiver", schemaInt("Receiver (default 0, main).", 0, 255)},
                  {"data", schemaInt("Data mode: 0 off, 1-3 DATA1-DATA3.", 0, 3)},
                  {"filter", schemaInt("Filter 1-3.", 1, 3)}},
                 [this](const QJsonObject &a) { return toolSetMode(a); });

    addWriteTool("set_ptt",
                 "Transmit",
                 "Keys or unkeys the transmitter. This makes the radio transmit. Keying is limited to maxSeconds, "
                 "after which MCP unkeys the radio itself.",
                 {{"transmit", schemaBool("true to transmit, false to return to receive.")},
                  {"maxSeconds",
                   schemaInt("Unkey after this many seconds (default 10).", 1, maxTransmitSeconds)}},
                 [this](const QJsonObject &a) { return toolSetPtt(a); });

    addWriteTool("send_command",
                 "Send radio command",
                 "Queues one radio function by the name get_cache shows (for example \"AF Gain\"). Without value it "
                 "asks the radio for the current value; with value it sets it. The value type is taken from the "
                 "cache, so the function must have been read at least once. Transmit is only available through "
                 "set_ptt.",
                 {{"func", schemaString("Function name as get_cache shows it, or its id.")},
                  {"value",
                   QJsonObject{{"type", QJsonArray{"boolean", "number", "string"}},
                               {"description", "Value to set; leave out to read."}}},
                  {"receiver", schemaInt("Receiver (default 0, main).", 0, 255)}},
                 [this](const QJsonObject &a) { return toolSendCommand(a); });

    addWriteTool("send_raw",
                 "Send raw bytes",
                 "Sends bytes to the radio exactly as given (a whole CI-V frame for Icom, a CAT command such as "
                 "\"FA;\" for Kenwood or Yaesu) and returns the traffic seen afterwards. Give hex or text. This "
                 "bypasses wfview's own command handling, so prefer set_frequency, set_mode and send_command, which "
                 "also exercise wfview; use this only when they cannot do the job.",
                 {{"hex", schemaString("Bytes as hex, for example \"FE FE 94 E0 03 FD\".")},
                  {"text", schemaString("Bytes as text, for example \"FA;\".")},
                  {"receiver", schemaInt("Receiver (default 0).", 0, 255)},
                  {"waitMs", schemaInt("How long to collect the reply (default 500).", 0, maxRawWaitMs)}},
                 [this](const QJsonObject &a) { return toolSendRaw(a); });

    addWriteTool("invalidate_cache",
                 "Invalidate cache",
                 "Removes entries from the radio state cache so wfview asks the radio again. Give a function name, "
                 "or all to empty the cache; polled values then refill as they are polled.",
                 {{"func", schemaString("Function name as get_cache shows it, or its id.")},
                  {"receiver", schemaInt("Only this receiver.", 0, 255)},
                  {"all", schemaBool("Empty the whole cache.")}},
                 [this](const QJsonObject &a) { return toolInvalidateCache(a); });

    addWriteTool("set_rigctld",
                 "Configure rigctld",
                 "Turns wfview's rigctld (Hamlib network control) server on or off and sets its port, through the "
                 "same settings as the settings dialog.",
                 {{"enabled", schemaBool("Run the rigctld server.")},
                  {"port", schemaInt("TCP port (wfview's default is 4533).", 1, 65535)},
                  {"save", schemaBool("Then save all settings to the settings file (default false).")}},
                 [this](const QJsonObject &a) { return toolSetRigctld(a); });

    addWriteTool("set_radio_server",
                 "Configure radio server",
                 "Turns the built-in radio server on or off, and changes an existing user's access level, through "
                 "the same settings as the settings dialog. A user made disabled, or removed, is disconnected at "
                 "once and cannot log in. The server only runs while a radio on a serial port is connected. "
                 "Returns the server state as get_radio_server does.",
                 {{"enabled", schemaBool("Run the radio server.")},
                  {"user", schemaString("Username of the account to change.")},
                  {"access", schemaEnum("New access level for user.", {"admin", "normal", "noTx", "disabled"})},
                  {"save", schemaBool("Then save all settings to the settings file (default false).")}},
                 [this](const QJsonObject &a) { return toolSetRadioServer(a); });

    addWriteTool("rigctld_command",
                 "rigctld command",
                 "Sends one command to wfview's own rigctld server over a local TCP connection and returns the "
                 "reply, as a Hamlib client would. For example \"f\" reads the frequency, \"F 14074000\" sets it, "
                 "\"\\dump_state\" lists capabilities. Prefix with + for Hamlib's extended replies.",
                 {{"command", schemaString("rigctld command line.")},
                  {"timeoutMs", schemaInt("Longest wait for the reply (default 2000).", 100, maxRigctldTimeoutMs)}},
                 [this](const QJsonObject &a) { return toolRigctldCommand(a); });
}

void McpTools::addTool(const QString &name,
                       const QString &title,
                       const QString &description,
                       const QJsonObject &properties,
                       Handler run)
{
    Tool tool;
    tool.name = name;
    tool.title = title;
    tool.description = description;
    tool.inputSchema =
        QJsonObject{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    tool.run = std::move(run);
    m_tools.append(tool);
}

void McpTools::addWriteTool(const QString &name,
                            const QString &title,
                            const QString &description,
                            const QJsonObject &properties,
                            Handler run)
{
    addTool(name, title, description, properties, std::move(run));
    m_tools.last().writes = true;
}

const McpTools::Tool *McpTools::findTool(const QString &name) const
{
    for (const Tool &tool : m_tools)
    {
        if (tool.name == name)
            return (tool.writes && !m_info.allowWrite) ? nullptr : &tool;
    }
    return nullptr;
}

QJsonArray McpTools::list() const
{
    QJsonArray tools;
    for (const Tool &tool : m_tools)
    {
        if (tool.writes && !m_info.allowWrite)
            continue;
        tools.append(QJsonObject{
            {"name", tool.name},
            {"title", tool.title},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema},
            {"annotations",
             QJsonObject{{"title", tool.title}, {"readOnlyHint", !tool.writes}, {"openWorldHint", false}}}});
    }
    return tools;
}

bool McpTools::call(const QString &name, const QJsonObject &args, McpToolResult *result)
{
    const Tool *tool = findTool(name);
    if (!tool || !result)
        return false;
    if (tool->writes)
        qCInfo(logMcp).noquote() << "MCP write:" << name
                                 << QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
    *result = tool->run(args);
    return true;
}

template <typename T> bool McpTools::onHost(std::function<T()> fn, T *out, int timeoutMs)
{
    struct Call
    {
        QMutex mutex;
        QWaitCondition finished;
        bool done = false;
        T value{};
    };

    // The call state is shared so a late answer after a timeout is harmless.
    auto call = std::make_shared<Call>();
    QMetaObject::invokeMethod(
        m_host,
        [call, fn]() {
            T value = fn();
            QMutexLocker locker(&call->mutex);
            call->value = std::move(value);
            call->done = true;
            call->finished.wakeAll();
        },
        Qt::QueuedConnection);

    QElapsedTimer timer;
    timer.start();
    QMutexLocker locker(&call->mutex);
    while (!call->done)
    {
        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0 || m_stopping->load(std::memory_order_relaxed))
            return false;
        // Wake regularly so shutdown never waits for the full timeout.
        call->finished.wait(&call->mutex, ulong(qMin<qint64>(remaining, 50)));
    }
    *out = std::move(call->value);
    return true;
}

QString McpTools::hostTimeoutText(int timeoutMs) const
{
    return QStringLiteral("The main thread did not answer within %1 ms; it may be busy or frozen (see "
                          "get_main_thread_health). The cache, queue, log, audio level and rig capability snapshot "
                          "tools do not need it.")
        .arg(timeoutMs);
}

McpToolResult McpTools::toolOverview(const QJsonObject &)
{
    McpToolResult r;
    QJsonObject &d = r.data;

    d["application"] = QJsonObject{{"name", m_info.appName},
                                   {"version", QString::fromUtf8(WFVIEW_VERSION)},
                                   {"git", QString::fromUtf8(GITSHORT)},
                                   {"built", QString::fromUtf8(__DATE__ " " __TIME__)},
                                   {"builtBy", QString::fromUtf8(UNAME "@" HOST)},
                                   {"os", QSysInfo::prettyProductName()},
                                   {"cpu", QSysInfo::buildCpuArchitecture()},
                                   {"qtBuild", QString::fromUtf8(QT_VERSION_STR)},
                                   {"qtRuntime", QString::fromLatin1(qVersion())},
                                   {"pid", double(QCoreApplication::applicationPid())},
                                   {"executable", QCoreApplication::applicationFilePath()},
                                   {"mcpUptimeMs", double(McpProbe::uptimeMs())}};

    d["mcp"] = QJsonObject{{"mode", m_info.allowWrite ? QStringLiteral("read-write") : QStringLiteral("read-only")},
                           {"bindAddress", m_info.bindAddress},
                           {"port", int(m_info.port)},
                           {"tokenRequired", m_info.tokenRequired},
                           {"tools", list().size()}};

    d["settingsFile"] = m_info.settingsFileName;

    rigCapabilities caps;
    qint64 capsAge = 0;
    bool capsCurrent = false;
    if (McpProbe::rigCapsSnapshot(&caps, &capsAge, &capsCurrent))
    {
        d["rig"] = QJsonObject{{"modelName", caps.modelName},
                               {"manufacturer", Mcp::manufacturerName(caps.manufacturer)},
                               {"radioId", Mcp::radioIdJson(caps.manufacturer, caps.modelID)},
                               {"rigFile", caps.filename},
                               {"connected", capsCurrent},
                               {"reportedAgoMs", double(capsAge)}};
    }
    else
    {
        d["rig"] = QStringLiteral("No radio has reported its capabilities since MCP started.");
    }

    if (m_info.queue)
    {
        int cacheSize = 0;
        {
            auto *items = m_info.queue->getCacheItems();
            QueueUnlocker unlock{m_info.queue};
            cacheSize = int(items->size());
        }
        int queueSize = 0;
        {
            auto *items = m_info.queue->getQueueItems();
            QueueUnlocker unlock{m_info.queue};
            queueSize = int(items->size());
        }
        d["cacheEntries"] = cacheSize;
        d["queuedCommands"] = queueSize;
    }

    const QJsonObject audio = McpProbe::audioLevels();
    int active = 0;
    int stalled = 0;
    for (const QJsonValue &h : audio.value("handlers").toArray())
    {
        const QJsonObject o = h.toObject();
        if (!o.value("active").toBool())
            continue;
        ++active;
        const QJsonValue age = o.value("lastUpdateAgeMs");
        if (age.isNull() || age.toDouble() > 1000)
            ++stalled;
    }
    d["audioStreams"] = QJsonObject{{"active", active}, {"activeWithoutRecentLevels", stalled}};

    d["mainThread"] = QJsonObject{{"msSinceHeartbeat", double(m_host->msSinceHeartbeat())},
                                  {"worstHeartbeatDelayMs", double(m_host->worstHeartbeatDelayMs())}};

    QJsonObject state;
    McpHost *host = m_host;
    if (onHost<QJsonObject>([host]() { return host->appState(); }, &state, overviewHostTimeoutMs))
        d["state"] = state;
    else
        d["state"] = hostTimeoutText(overviewHostTimeoutMs);

    return r;
}

McpToolResult McpTools::toolMainThreadHealth(const QJsonObject &args)
{
    McpToolResult r;
    const int timeoutMs = argInt(args, "timeoutMs", hostTimeoutMs, 100, 10000);

    QElapsedTimer timer;
    timer.start();
    qint64 unused = 0;
    const bool responsive = onHost<qint64>([]() { return qint64(0); }, &unused, timeoutMs);
    const qint64 roundTrip = timer.elapsed();

    r.data["responsive"] = responsive;
    r.data["roundTripMs"] = responsive ? QJsonValue(double(roundTrip)) : QJsonValue();
    r.data["msSinceHeartbeat"] = double(m_host->msSinceHeartbeat());
    r.data["worstHeartbeatDelayMs"] = double(m_host->worstHeartbeatDelayMs());
    r.data["heartbeatIntervalMs"] = McpHost::heartbeatIntervalMs;
    r.data["note"] = QStringLiteral("A healthy main thread answers in a few ms and msSinceHeartbeat stays near or "
                                    "below the heartbeat interval. worstHeartbeatDelayMs is the longest stall since "
                                    "MCP started or the last reset.");
    if (!responsive)
        r.data["timeout"] = hostTimeoutText(timeoutMs);

    if (args.value("resetWorst").toBool())
        m_host->resetWorstHeartbeatDelay();
    return r;
}

McpToolResult McpTools::toolWindows(const QJsonObject &args)
{
    McpToolResult r;
    McpHost *host = m_host;
    if (!onHost<QJsonObject>([host]() { return host->windows(); }, &r.data))
    {
        r.error = hostTimeoutText(hostTimeoutMs);
        return r;
    }

    if (args.value("visibleOnly").toBool())
    {
        QJsonArray visible;
        for (const QJsonValue &w : r.data.value("windows").toArray())
        {
            if (w.toObject().value("visible").toBool())
                visible.append(w);
        }
        r.data["windows"] = visible;
    }
    return r;
}

McpToolResult McpTools::toolSettings(const QJsonObject &args)
{
    McpToolResult r;
    if (m_info.settingsFileName.isEmpty())
    {
        r.error = QStringLiteral("The settings location is unknown.");
        return r;
    }

    const QString group = args.value("group").toString();
    const QString contains = args.value("contains").toString();
    const int limit = argInt(args, "limit", defaultSettingsLimit, 1, maxSettingsLimit);

    // A separate QSettings instance reads the same store safely from this thread.
    QSettings settings(m_info.settingsFileName, m_info.settingsFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (m_info.settingsFormat == QSettings::IniFormat)
        settings.setIniCodec("UTF-8");
#endif

    const QStringList keys = settings.allKeys();
    QJsonObject values;
    int returned = 0;
    bool truncated = false;
    for (const QString &key : keys)
    {
        if (!group.isEmpty() && !key.startsWith(group + QLatin1Char('/'), Qt::CaseInsensitive)
            && key.compare(group, Qt::CaseInsensitive) != 0)
            continue;
        if (!contains.isEmpty() && !key.contains(contains, Qt::CaseInsensitive))
            continue;
        if (returned >= limit)
        {
            truncated = true;
            break;
        }

        const QVariant value = settings.value(key);
        values[key] = Mcp::isSecretKey(key) ? QJsonValue(Mcp::maskSecret(value.toString())) : Mcp::variantToJson(value);
        ++returned;
    }

    r.data["settingsFile"] = m_info.settingsFileName;
    r.data["format"] = m_info.settingsFormat == QSettings::IniFormat ? QStringLiteral("ini") : QStringLiteral("native");
    r.data["status"] = settings.status() == QSettings::NoError ? QStringLiteral("ok") : QStringLiteral("error");
    r.data["totalKeys"] = int(keys.size());
    r.data["returnedKeys"] = returned;
    r.data["truncated"] = truncated;
    r.data["values"] = values;
    r.data["note"] = QStringLiteral("Values as saved; changes the application has not saved yet are not shown. "
                                    "Passwords are masked.");
    return r;
}

McpToolResult McpTools::toolSettingsFile(const QJsonObject &args)
{
    McpToolResult r;
    const QFileInfo info(m_info.settingsFileName);
    if (m_info.settingsFileName.isEmpty() || !info.isFile())
    {
        r.error = QStringLiteral("Settings are not stored in a file (%1). Use get_settings instead.")
                      .arg(m_info.settingsFileName);
        return r;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
    {
        r.error = QStringLiteral("Could not open %1: %2").arg(file.fileName(), file.errorString());
        return r;
    }

    const int maxBytes = argInt(args, "maxBytes", defaultSettingsFileBytes, 1024, maxSettingsFileBytes);
    const QByteArray data = file.read(maxBytes);

    r.data["path"] = info.absoluteFilePath();
    r.data["sizeBytes"] = double(info.size());
    r.data["modified"] = timeJson(info.lastModified());
    r.data["truncated"] = info.size() > data.size();
    r.data["contents"] = Mcp::maskSettingsText(QString::fromUtf8(data));
    return r;
}

McpToolResult McpTools::toolConnection(const QJsonObject &)
{
    McpToolResult r;
    McpHost *host = m_host;
    if (!onHost<QJsonObject>([host]() { return host->connectionInfo(); }, &r.data))
        r.error = hostTimeoutText(hostTimeoutMs);
    return r;
}

McpToolResult McpTools::toolRigCaps(const QJsonObject &args)
{
    struct CapsResult
    {
        bool ok = false;
        rigCapabilities caps;
        QString error;
    };

    McpToolResult r;
    QStringList sections;
    for (const QJsonValue &v : args.value("sections").toArray())
        sections.append(v.toString());
    const int radio = argInt(args, "radio", 0, 0, 255);

    CapsResult result;
    McpHost *host = m_host;
    const bool answered = onHost<CapsResult>(
        [host, radio]() {
            CapsResult c;
            c.ok = host->rigCaps(radio, &c.caps, &c.error);
            return c;
        },
        &result);

    if (answered && result.ok)
    {
        r.data = Mcp::rigCapsToJson(result.caps, sections);
        r.data["source"] = QStringLiteral("live");
        return r;
    }

    // Fall back to the copy taken when the radio last reported its capabilities.
    rigCapabilities snapshot;
    qint64 ageMs = 0;
    bool current = false;
    if (radio == 0 && McpProbe::rigCapsSnapshot(&snapshot, &ageMs, &current))
    {
        r.data = Mcp::rigCapsToJson(snapshot, sections);
        r.data["source"] = QStringLiteral("snapshot");
        r.data["snapshotAgeMs"] = double(ageMs);
        r.data["radioConnected"] = current;
        r.data["note"] = answered ? result.error : hostTimeoutText(hostTimeoutMs);
        return r;
    }

    r.error = answered ? result.error : hostTimeoutText(hostTimeoutMs);
    return r;
}

McpToolResult McpTools::toolRigFiles(const QJsonObject &)
{
    McpToolResult r;
    McpHost *host = m_host;
    if (!onHost<QJsonObject>([host]() { return host->rigFiles(); }, &r.data))
        r.error = hostTimeoutText(hostTimeoutMs);
    return r;
}

McpToolResult McpTools::toolAudioConfig(const QJsonObject &)
{
    McpToolResult r;
    McpHost *host = m_host;
    if (!onHost<QJsonObject>([host]() { return host->audioConfig(); }, &r.data))
        r.error = hostTimeoutText(hostTimeoutMs);
    return r;
}

McpToolResult McpTools::toolAudioLevels(const QJsonObject &)
{
    McpToolResult r;
    r.data = McpProbe::audioLevels();
    return r;
}

McpToolResult McpTools::toolCache(const QJsonObject &args)
{
    McpToolResult r;
    if (!m_info.queue)
    {
        r.error = QStringLiteral("The command queue is not running.");
        return r;
    }

    const QString func = args.value("func").toString();
    const int receiver = argInt(args, "receiver", -1, 0, 255);
    const int limit = argInt(args, "limit", defaultCacheLimit, 1, maxRowLimit);

    // Copy under the queue lock and format afterwards, so the radio thread
    // waits as little as possible.
    QVector<cacheItem> items;
    {
        auto *cache = m_info.queue->getCacheItems();
        QueueUnlocker unlock{m_info.queue};
        items.reserve(int(cache->size()));
        for (auto it = cache->cbegin(); it != cache->cend(); ++it)
            items.append(it.value());
    }

    const QDateTime now = QDateTime::currentDateTime();
    QJsonArray rows;
    bool truncated = false;
    for (const cacheItem &c : items)
    {
        const QString name = funcName(int(c.command));
        if (!func.isEmpty() && !name.contains(func, Qt::CaseInsensitive))
            continue;
        if (receiver >= 0 && c.receiver != receiver)
            continue;
        if (rows.size() >= limit)
        {
            truncated = true;
            break;
        }
        rows.append(QJsonObject{{"func", name},
                                {"id", int(c.command)},
                                {"receiver", int(c.receiver)},
                                {"value", Mcp::variantToJson(c.value)},
                                {"valueType", c.value.isValid() ? QString::fromLatin1(c.value.typeName()) : QString()},
                                {"requested", timeJson(c.req)},
                                {"replied", timeJson(c.reply)},
                                {"replyAgeMs", ageJson(c.reply, now)},
                                {"retries", int(c.retries)}});
    }

    r.data["totalEntries"] = int(items.size());
    r.data["returnedEntries"] = rows.size();
    r.data["truncated"] = truncated;
    r.data["entries"] = rows;
    return r;
}

McpToolResult McpTools::toolQueue(const QJsonObject &args)
{
    McpToolResult r;
    if (!m_info.queue)
    {
        r.error = QStringLiteral("The command queue is not running.");
        return r;
    }

    const QString func = args.value("func").toString();
    const int limit = argInt(args, "limit", defaultCacheLimit, 1, maxRowLimit);

    QVector<QPair<int, queueItem>> items;
    {
        auto *queue = m_info.queue->getQueueItems();
        QueueUnlocker unlock{m_info.queue};
        items.reserve(int(queue->size()));
        for (auto it = queue->cbegin(); it != queue->cend(); ++it)
            items.append(qMakePair(int(it.key()), it.value()));
    }

    QJsonArray rows;
    bool truncated = false;
    for (const auto &item : items)
    {
        const queueItem &q = item.second;
        const QString name = funcName(int(q.command));
        if (!func.isEmpty() && !name.contains(func, Qt::CaseInsensitive))
            continue;
        if (rows.size() >= limit)
        {
            truncated = true;
            break;
        }
        QJsonObject row{{"priority", priorityName(item.first)},
                        {"func", name},
                        {"id", int(q.command)},
                        {"receiver", int(q.receiver)},
                        {"kind", q.param.isValid() ? QStringLiteral("set") : QStringLiteral("get")},
                        {"value", Mcp::variantToJson(q.param)},
                        {"recurring", q.recurring}};
        if (!q.rawData.isEmpty())
            row["rawData"] = hexJson(q.rawData);
        rows.append(row);
    }

    r.data["totalQueued"] = int(items.size());
    r.data["returnedEntries"] = rows.size();
    r.data["truncated"] = truncated;
    r.data["entries"] = rows;
    return r;
}

McpToolResult McpTools::toolMeters(const QJsonObject &args)
{
    QJsonObject cacheArgs{{"func", "Meter"}, {"limit", maxRowLimit}};
    if (args.contains("receiver"))
        cacheArgs["receiver"] = args.value("receiver");

    McpToolResult r = toolCache(cacheArgs);
    if (!r.error.isEmpty())
        return r;

    // Meter Type is a setting, not a reading.
    QJsonArray meters;
    for (const QJsonValue &v : r.data.value("entries").toArray())
    {
        if (v.toObject().value("func").toString() != QLatin1String("Meter Type"))
            meters.append(v);
    }

    McpToolResult out;
    out.data["meters"] = meters;
    out.data["note"] = QStringLiteral("Values are as the radio reported them; replyAgeMs shows how fresh each is. "
                                      "Meters are only polled while their display is active.");
    return out;
}

McpToolResult McpTools::toolLog(const QJsonObject &args)
{
    const QString level = args.value("level").toString();
    QtMsgType minType = QtDebugMsg;
    if (level == QLatin1String("info"))
        minType = QtInfoMsg;
    else if (level == QLatin1String("warning"))
        minType = QtWarningMsg;
    else if (level == QLatin1String("critical"))
        minType = QtCriticalMsg;

    McpToolResult r;
    r.data = McpProbe::logLines(argInt(args, "lines", defaultLogLines, 1, maxLogLines),
                                minType,
                                args.value("category").toString(),
                                args.value("contains").toString());
    return r;
}

McpToolResult McpTools::toolSetTrafficCapture(const QJsonObject &args)
{
    McpToolResult r;
    if (args.value("stop").toBool())
    {
        McpProbe::stopCapture();
    }
    else
    {
        McpProbe::startCapture(argInt(args, "seconds", defaultCaptureSeconds, 1, maxCaptureSeconds),
                               args.value("includeScope").toBool(),
                               args.value("clear").toBool(true));
    }
    r.data = McpProbe::captureStatus();
    return r;
}

McpToolResult McpTools::toolTraffic(const QJsonObject &args)
{
    McpToolResult r;
    r.data = McpProbe::traffic(args.value("kind").toString(),
                               args.value("direction").toString(),
                               argInt(args, "limit", defaultTrafficLimit, 1, maxRowLimit),
                               args.value("contains").toString());
    return r;
}

McpToolResult McpTools::toolSpectrum(const QJsonObject &args)
{
    McpToolResult r;
    if (!m_info.queue)
    {
        r.error = QStringLiteral("The command queue is not running.");
        return r;
    }

    const int receiver = argInt(args, "receiver", 0, 0, 255);
    const cacheItem item = m_info.queue->getCache(funcScopeWaveData, uchar(receiver));
    if (item.value.userType() != qMetaTypeId<scopeData>())
    {
        r.error = QStringLiteral("No spectrum data for receiver %1 yet. The scope may be off, or the radio may not "
                                 "send spectrum data.")
                      .arg(receiver);
        return r;
    }

    const scopeData scope = item.value.value<scopeData>();
    const QByteArray &raw = scope.data;
    const int n = int(raw.size());
    if (n == 0)
    {
        r.error = QStringLiteral("The latest spectrum sweep for receiver %1 is empty.").arg(receiver);
        return r;
    }

    const int points = qMin(argInt(args, "points", defaultSpectrumPoints, 8, 4096), n);
    const double spanHz = (scope.endFreq - scope.startFreq) * 1e6;

    QJsonArray levels;
    for (int b = 0; b < points; ++b)
    {
        const int from = int(qint64(b) * n / points);
        const int to = qMax(from + 1, int(qint64(b + 1) * n / points));
        int level = 0;
        for (int i = from; i < to; ++i)
            level = qMax(level, int(quint8(raw.at(i))));
        levels.append(level);
    }

    QVector<int> sorted(n);
    for (int i = 0; i < n; ++i)
        sorted[i] = quint8(raw.at(i));
    std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
    const int noiseFloor = sorted.at(n / 2);

    // Local maxima clearly above the noise floor, strongest first.
    rigCapabilities caps;
    const bool haveCaps = McpProbe::rigCapsSnapshot(&caps, nullptr, nullptr);
    const int ampMax = haveCaps && caps.spectAmpMax > 0 ? int(caps.spectAmpMax) : 255;
    const int threshold = noiseFloor + qMax(3, ampMax / 20);
    QVector<QPair<int, int>> peaks; // level, index
    for (int i = 0; i < n; ++i)
    {
        const int level = quint8(raw.at(i));
        if (level < threshold)
            continue;
        bool isPeak = true;
        for (int j = qMax(0, i - 2); j <= qMin(n - 1, i + 2) && isPeak; ++j)
        {
            const int other = quint8(raw.at(j));
            if (other > level || (other == level && j < i))
                isPeak = false;
        }
        if (isPeak)
            peaks.append(qMakePair(level, i));
    }
    std::sort(peaks.begin(), peaks.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        return a.first > b.first;
    });

    const int maxPeaks = argInt(args, "peaks", defaultSpectrumPeaks, 0, maxSpectrumPeaks);
    QJsonArray peakList;
    for (int i = 0; i < peaks.size() && i < maxPeaks; ++i)
    {
        const double freqMHz = scope.startFreq + (peaks.at(i).second + 0.5) / n * (scope.endFreq - scope.startFreq);
        peakList.append(QJsonObject{{"freqMHz", freqMHz}, {"level", peaks.at(i).first}});
    }

    r.data["receiver"] = receiver;
    r.data["startFreqMHz"] = scope.startFreq;
    r.data["endFreqMHz"] = scope.endFreq;
    r.data["scopeMode"] = int(scope.mode);
    r.data["outOfRange"] = scope.oor;
    r.data["valid"] = scope.valid;
    r.data["ageMs"] = item.reply.isValid() ? QJsonValue(double(item.reply.msecsTo(QDateTime::currentDateTime())))
                                           : QJsonValue();
    r.data["rawPoints"] = n;
    r.data["points"] = points;
    r.data["binWidthHz"] = spanHz / points;
    r.data["levelMax"] = ampMax;
    r.data["noiseFloor"] = noiseFloor;
    r.data["levels"] = levels;
    r.data["peaks"] = peakList;
    r.data["note"] = QStringLiteral("Levels are the radio's raw scale from 0 to levelMax, higher is stronger; they are "
                                    "not calibrated in dBm. Each point is the highest level within its bin.");
    return r;
}

bool McpTools::radioReady(rigCapabilities *caps, QString *error) const
{
    bool current = false;
    if (!m_info.queue || m_info.queue->interval() == -1 || !McpProbe::rigCapsSnapshot(caps, nullptr, &current)
        || !current)
    {
        *error = QStringLiteral("No radio is connected.");
        return false;
    }
    return true;
}

bool McpTools::cachedValue(int func, int receiver, cacheItem *out) const
{
    auto *cache = m_info.queue->getCacheItems();
    QueueUnlocker unlock{m_info.queue};
    for (auto it = cache->constFind(funcs(func)); it != cache->cend() && it.key() == funcs(func); ++it)
    {
        if (it->receiver == receiver)
        {
            *out = it.value();
            return true;
        }
    }
    return false;
}

void McpTools::pause(int ms) const
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms && !m_stopping->load(std::memory_order_relaxed))
        QThread::msleep(ulong(qMin<qint64>(50, ms - timer.elapsed())));
}

McpToolResult McpTools::toolSettingOptions(const QJsonObject &args)
{
    McpToolResult r;
    McpHostResult result;
    McpHost *host = m_host;
    if (!onHost<McpHostResult>([host]() { return host->settingOptions(); }, &result))
    {
        r.error = hostTimeoutText(hostTimeoutMs);
        return r;
    }
    if (!result.ok)
    {
        r.error = result.error;
        return r;
    }

    const QString contains = args.value("contains").toString();
    if (!contains.isEmpty())
    {
        QJsonObject options;
        const QJsonObject all = result.data.value("options").toObject();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        {
            if (it.key().contains(contains, Qt::CaseInsensitive))
                options[it.key()] = it.value();
        }
        QJsonArray notWritable;
        for (const QJsonValue &key : result.data.value("notWritable").toArray())
        {
            if (key.toString().contains(contains, Qt::CaseInsensitive))
                notWritable.append(key);
        }
        result.data["options"] = options;
        result.data["notWritable"] = notWritable;
    }

    r.data = result.data;
    r.data["note"] = m_info.allowWrite
                         ? QStringLiteral("Change these with set_setting, except those in notWritable.")
                         : QStringLiteral("The server is read-only; start with --mcp-allow-write to change these.");
    return r;
}

McpToolResult McpTools::toolSetSetting(const QJsonObject &args)
{
    McpToolResult r;
    const QString key = args.value("key").toString();
    if (key.isEmpty() || !args.contains("value"))
    {
        r.error = QStringLiteral("Give the setting's key and its new value.");
        return r;
    }

    const QJsonValue value = args.value("value");
    const bool save = args.value("save").toBool();
    McpHostResult result;
    McpHost *host = m_host;
    if (!onHost<McpHostResult>([host, key, value, save]() { return host->setOption(key, value, save); }, &result))
    {
        r.error = hostTimeoutText(hostTimeoutMs);
        return r;
    }
    if (!result.ok)
        r.error = result.error;
    r.data = result.data;
    return r;
}

McpToolResult McpTools::toolSetFrequency(const QJsonObject &args)
{
    McpToolResult r;
    rigCapabilities caps;
    if (!radioReady(&caps, &r.error))
        return r;

    const double hz = args.value("hz").toDouble(-1);
    if (hz < 1 || hz > 1e11)
    {
        r.error = QStringLiteral("Give the frequency in Hz.");
        return r;
    }
    const int receiver = argInt(args, "receiver", 0, 0, 255);
    if (receiver >= qMax(1, int(caps.numReceiver)))
    {
        r.error = QStringLiteral("The radio has %1 receiver(s); receiver %2 does not exist.")
                      .arg(caps.numReceiver)
                      .arg(receiver);
        return r;
    }

    const vfoCommandType command = m_info.queue->getVfoCommand(vfoA, uchar(receiver), true);
    if (command.freqFunc == funcNone)
    {
        r.error = QStringLiteral("Receiver %1 cannot be tuned while it is not the selected receiver.").arg(receiver);
        return r;
    }

    freqt f;
    f.Hz = quint64(hz + 0.5);
    f.MHzDouble = f.Hz / 1e6;
    f.VFO = activeVFO;
    m_info.queue->addUnique(priorityImmediate,
                            queueItem(command.freqFunc, QVariant::fromValue<freqt>(f), false, command.receiver));

    r.data["queued"] = funcName(int(command.freqFunc));
    r.data["hz"] = double(f.Hz);
    r.data["receiver"] = int(command.receiver);
    r.data["note"] = QStringLiteral("Queued for the radio. The cache shows the requested value at once, so to "
                                    "confirm the radio took it, read it back with send_command (no value) and check "
                                    "that get_cache func=\"Freq\" has a newer reply time.");
    return r;
}

McpToolResult McpTools::toolSetMode(const QJsonObject &args)
{
    McpToolResult r;
    rigCapabilities caps;
    if (!radioReady(&caps, &r.error))
        return r;

    const QString name = args.value("mode").toString().trimmed();
    const auto it = std::find_if(caps.modes.cbegin(), caps.modes.cend(), [&name](const modeInfo &m) {
        return m.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (it == caps.modes.cend())
    {
        QStringList names;
        for (const modeInfo &m : caps.modes)
            names.append(m.name);
        r.error = QStringLiteral("The radio has no mode \"%1\". Its modes are: %2.").arg(name, names.join(", "));
        return r;
    }

    const int receiver = argInt(args, "receiver", 0, 0, 255);
    const vfoCommandType get = m_info.queue->getVfoCommand(vfoA, uchar(receiver), false);
    const vfoCommandType set = m_info.queue->getVfoCommand(vfoA, uchar(receiver), true);
    if (set.modeFunc == funcNone)
    {
        r.error = QStringLiteral("Receiver %1 cannot be changed while it is not the selected receiver.").arg(receiver);
        return r;
    }

    // Keep the current data mode and filter, as the receiver controls do.
    modeInfo requested = *it;
    cacheItem current;
    if (cachedValue(get.modeFunc, get.receiver, &current) && current.value.userType() == qMetaTypeId<modeInfo>())
    {
        const modeInfo now = current.value.value<modeInfo>();
        requested.data = now.data;
        requested.filter = now.filter;
    }
    if (args.contains("data"))
        requested.data = quint8(argInt(args, "data", 0, 0, 3));
    if (args.contains("filter"))
        requested.filter = quint8(argInt(args, "filter", 1, 1, 3));

    m_info.queue->addUnique(priorityImmediate,
                            queueItem(set.modeFunc, QVariant::fromValue<modeInfo>(requested), false, set.receiver));
    if (set.modeFunc == funcModeSet)
        m_info.queue->addUnique(
            priorityImmediate,
            queueItem(funcDataModeWithFilter, QVariant::fromValue<modeInfo>(requested), false, set.receiver));
    if (caps.manufacturer == manufIcom)
        m_info.queue->addUnique(priorityHighest, funcRoofingFilter, false, set.receiver);

    r.data["queued"] = funcName(int(set.modeFunc));
    r.data["mode"] = requested.name;
    r.data["data"] = int(requested.data);
    r.data["filter"] = int(requested.filter);
    r.data["receiver"] = int(set.receiver);
    r.data["note"] = QStringLiteral("Queued for the radio. The cache shows the requested value at once, so to "
                                    "confirm the radio took it, read it back with send_command (no value) and check "
                                    "that get_cache func=\"Mode\" has a newer reply time.");
    return r;
}

McpToolResult McpTools::toolSetPtt(const QJsonObject &args)
{
    McpToolResult r;
    if (!args.value("transmit").isBool())
    {
        r.error = QStringLiteral("Give transmit: true or false.");
        return r;
    }
    const bool on = args.value("transmit").toBool();
    const int maxSeconds = argInt(args, "maxSeconds", defaultTransmitSeconds, 1, maxTransmitSeconds);

    McpHostResult result;
    McpHost *host = m_host;
    if (!onHost<McpHostResult>([host, on, maxSeconds]() { return host->transmitFor(on, maxSeconds); }, &result))
    {
        r.error = hostTimeoutText(hostTimeoutMs);
        if (on)
            r.error += QStringLiteral(" The transmit request may still be carried out when the main thread recovers.");
        return r;
    }
    if (!result.ok)
        r.error = result.error;
    r.data = result.data;
    return r;
}

McpToolResult McpTools::toolSendCommand(const QJsonObject &args)
{
    McpToolResult r;
    rigCapabilities caps;
    if (!radioReady(&caps, &r.error))
        return r;

    const funcs func = findFunc(args.value("func").toString());
    if (func == funcNone)
    {
        r.error = QStringLiteral("Unknown function \"%1\"; use a name as get_cache shows it.")
                      .arg(args.value("func").toString());
        return r;
    }
    if (func == funcTransceiverStatus)
    {
        r.error = QStringLiteral("Use set_ptt to transmit; it limits how long the radio stays keyed.");
        return r;
    }
    if (!caps.commands.contains(func))
    {
        r.error = QStringLiteral("The radio's rig file has no %1 command.").arg(funcName(int(func)));
        return r;
    }

    const int receiver = argInt(args, "receiver", 0, 0, 255);
    r.data["func"] = funcName(int(func));
    r.data["receiver"] = receiver;

    if (!args.contains("value"))
    {
        m_info.queue->add(priorityImmediate, func, false, uchar(receiver));
        r.data["queued"] = QStringLiteral("get");
        r.data["note"] = QStringLiteral("Queued a read. The answer appears in get_cache in a moment.");
        return r;
    }

    cacheItem current;
    if (!cachedValue(int(func), receiver, &current) || !current.value.isValid())
    {
        r.error = QStringLiteral("wfview has no value for %1 yet, so its type is unknown. Send it without a value "
                                 "first to read it.")
                      .arg(funcName(int(func)));
        return r;
    }

    QVariant value;
    if (!jsonToFuncValue(args.value("value"), current.value, &value, &r.error))
        return r;

    m_info.queue->add(priorityImmediate, queueItem(func, value, false, uchar(receiver)));
    r.data["queued"] = QStringLiteral("set");
    r.data["value"] = Mcp::variantToJson(value);
    r.data["previous"] = Mcp::variantToJson(current.value);
    return r;
}

McpToolResult McpTools::toolSendRaw(const QJsonObject &args)
{
    McpToolResult r;
    rigCapabilities caps;
    if (!radioReady(&caps, &r.error))
        return r;

    QByteArray data;
    if (args.contains("hex") == args.contains("text"))
    {
        r.error = QStringLiteral("Give either hex or text.");
        return r;
    }
    if (args.contains("hex") && !parseHex(args.value("hex").toString(), &data))
    {
        r.error = QStringLiteral("hex must be pairs of hex digits, for example \"FE FE 94 E0 03 FD\".");
        return r;
    }
    if (args.contains("text"))
        data = args.value("text").toString().toLatin1();
    if (data.isEmpty() || data.size() > maxRawBytes)
    {
        r.error = QStringLiteral("Send between 1 and %1 bytes.").arg(maxRawBytes);
        return r;
    }

    const int receiver = argInt(args, "receiver", 0, 0, 255);
    const int waitMs = argInt(args, "waitMs", defaultRawWaitMs, 0, maxRawWaitMs);

    // Record the exchange, keeping anything a client is already capturing.
    McpProbe::ensureCapture(qMax(rawCaptureSeconds, waitMs / 1000 + 2));
    const qint64 sent = QDateTime::currentMSecsSinceEpoch();
    m_info.queue->add(priorityImmediate, queueItem(data, uchar(receiver)));
    pause(waitMs);

    const QJsonObject traffic = McpProbe::traffic(QStringLiteral("radio"), QString(), defaultTrafficLimit, QString(), sent);
    r.data["sent"] = hexJson(data);
    r.data["bytes"] = int(data.size());
    r.data["waitedMs"] = waitMs;
    r.data["traffic"] = traffic.value("entries");
    r.data["note"] = QStringLiteral("traffic is everything exchanged with the radio since the bytes were queued, "
                                    "so it includes wfview's own polling. Spectrum data is left out.");
    return r;
}

McpToolResult McpTools::toolInvalidateCache(const QJsonObject &args)
{
    McpToolResult r;
    if (!m_info.queue)
    {
        r.error = QStringLiteral("The command queue is not running.");
        return r;
    }

    if (args.value("all").toBool())
    {
        int removed = 0;
        {
            auto *cache = m_info.queue->getCacheItems();
            QueueUnlocker unlock{m_info.queue};
            removed = int(cache->size());
        }
        m_info.queue->clearCache();
        r.data["removed"] = removed;
        r.data["note"] = QStringLiteral("The cache is empty. Polled values come back as they are polled; others "
                                        "only when something asks for them.");
        return r;
    }

    const funcs func = findFunc(args.value("func").toString());
    if (func == funcNone)
    {
        r.error = QStringLiteral("Give a function name as get_cache shows it, or all: true.");
        return r;
    }
    const int receiver = argInt(args, "receiver", -1, 0, 255);

    QVector<uchar> receivers;
    {
        auto *cache = m_info.queue->getCacheItems();
        QueueUnlocker unlock{m_info.queue};
        auto it = cache->find(func);
        while (it != cache->end() && it.key() == func)
        {
            if (receiver < 0 || it->receiver == receiver)
            {
                receivers.append(it->receiver);
                it = cache->erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Ask the radio again, outside the lock (add() takes it too).
    for (uchar rx : receivers)
        m_info.queue->add(priorityImmediate, func, false, rx);

    r.data["func"] = funcName(int(func));
    r.data["removed"] = receivers.size();
    r.data["note"] = receivers.isEmpty() ? QStringLiteral("There was no such entry in the cache.")
                                         : QStringLiteral("Removed, and asked the radio again.");
    return r;
}

McpToolResult McpTools::toolSetRigctld(const QJsonObject &args)
{
    McpToolResult r;
    if (!args.contains("enabled") && !args.contains("port"))
    {
        r.error = QStringLiteral("Give enabled, port or both.");
        return r;
    }

    const QJsonValue enabled = args.value("enabled");
    const int port = argInt(args, "port", 0, 0, 65535);
    const bool save = args.value("save").toBool();

    McpHostResult result;
    McpHost *host = m_host;
    const bool answered = onHost<McpHostResult>(
        [host, enabled, port, save]() {
            McpHostResult out;
            // Port first, so turning the server on uses the new port. Only
            // the last change saves, since saving writes every setting.
            if (port > 0)
            {
                out = host->setOption(QStringLiteral("LAN.RigCtlPort"), port, save && !enabled.isBool());
                if (!out.ok)
                    return out;
            }
            if (enabled.isBool())
            {
                out = host->setOption(QStringLiteral("LAN.EnableRigCtlD"), enabled, save);
                if (!out.ok)
                    return out;
            }
            out.data = host->connectionInfo().value("rigctld").toObject();
            out.data["saved"] = save;
            return out;
        },
        &result);

    if (!answered)
        r.error = hostTimeoutText(hostTimeoutMs);
    else if (!result.ok)
        r.error = result.error;
    r.data = result.data;
    return r;
}

McpToolResult McpTools::toolRadioServer(const QJsonObject &)
{
    McpToolResult r;
    McpHost *host = m_host;
    if (!onHost<QJsonObject>([host]() { return host->radioServer(); }, &r.data))
        r.error = hostTimeoutText(hostTimeoutMs);
    return r;
}

McpToolResult McpTools::toolSetRadioServer(const QJsonObject &args)
{
    McpToolResult r;
    const QJsonValue enabled = args.value("enabled");
    const QString user = args.value("user").toString();
    const QString access = args.value("access").toString();
    if (!enabled.isBool() && user.isEmpty())
    {
        r.error = QStringLiteral("Give enabled, or user with access, or both.");
        return r;
    }
    if (user.isEmpty() != access.isEmpty())
    {
        r.error = QStringLiteral("Give user and access together.");
        return r;
    }
    const bool save = args.value("save").toBool();

    McpHostResult result;
    McpHost *host = m_host;
    const bool answered = onHost<McpHostResult>(
        [host, enabled, user, access, save]() {
            McpHostResult out;
            // Only the last change saves, since saving writes every setting.
            if (!user.isEmpty())
            {
                out = host->setServerUserAccess(user, access, save && !enabled.isBool());
                if (!out.ok)
                    return out;
            }
            if (enabled.isBool())
            {
                out = host->setOption(QStringLiteral("Server.Enabled"), enabled, save);
                if (!out.ok)
                    return out;
            }
            out.data = host->radioServer();
            out.data["saved"] = save;
            return out;
        },
        &result);

    if (!answered)
        r.error = hostTimeoutText(hostTimeoutMs);
    else if (!result.ok)
        r.error = result.error;
    r.data = result.data;
    return r;
}

McpToolResult McpTools::toolRigctldCommand(const QJsonObject &args)
{
    McpToolResult r;
    QString command = args.value("command").toString();
    if (command.trimmed().isEmpty())
    {
        r.error = QStringLiteral("Give a rigctld command, for example \"f\".");
        return r;
    }
    const int timeoutMs = argInt(args, "timeoutMs", defaultRigctldTimeoutMs, 100, maxRigctldTimeoutMs);

    QJsonObject rigctld;
    McpHost *host = m_host;
    if (!onHost<QJsonObject>([host]() { return host->connectionInfo().value("rigctld").toObject(); }, &rigctld))
    {
        r.error = hostTimeoutText(hostTimeoutMs);
        return r;
    }
    if (rigctld.isEmpty())
    {
        r.error = QStringLiteral("%1 has no rigctld server.").arg(m_info.appName);
        return r;
    }
    if (!rigctld.value("listening").toBool())
    {
        r.error = QStringLiteral("rigctld is not running; turn it on with set_rigctld.");
        return r;
    }

    // A socket of our own on this thread, like any Hamlib client.
    QTcpSocket socket;
    socket.connectToHost(QHostAddress(QHostAddress::LocalHost), quint16(rigctld.value("port").toInt()));
    if (!socket.waitForConnected(qMin(timeoutMs, 1000)))
    {
        r.error = QStringLiteral("Could not connect to rigctld on port %1: %2")
                      .arg(rigctld.value("port").toInt())
                      .arg(socket.errorString());
        return r;
    }

    if (!command.endsWith(QLatin1Char('\n')))
        command += QLatin1Char('\n');
    socket.write(command.toUtf8());

    QElapsedTimer timer;
    timer.start();
    QByteArray reply;
    while (timer.elapsed() < timeoutMs && !m_stopping->load(std::memory_order_relaxed))
    {
        const int wait = reply.isEmpty() ? int(qMin<qint64>(100, timeoutMs - timer.elapsed())) : rigctldQuietMs;
        if (socket.waitForReadyRead(qMax(1, wait)))
        {
            reply += socket.readAll();
            // Set commands and extended replies end with a return code.
            if (reply.contains("RPRT ") && reply.endsWith('\n'))
                break;
        }
        else if (!reply.isEmpty() || socket.state() != QAbstractSocket::ConnectedState)
        {
            break;
        }
    }
    socket.disconnectFromHost();

    const QString text = QString::fromUtf8(reply);
    r.data["command"] = command.trimmed();
    r.data["port"] = rigctld.value("port");
    r.data["reply"] = text;
    r.data["lines"] = QJsonArray::fromStringList(text.split(QLatin1Char('\n'), Qt::SkipEmptyParts));
    r.data["elapsedMs"] = double(timer.elapsed());
    if (reply.isEmpty())
        r.data["note"] = QStringLiteral("rigctld sent nothing back within the timeout.");
    return r;
}
