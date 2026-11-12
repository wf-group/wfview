#include "mcpprobe.h"

#include "audiohandlerbase.h"
#include "cachingqueue.h"
#include "rigcommander.h"
#include "rigidentities.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QMutex>
#include <QThread>
#include <QVector>

#include <array>
#include <deque>
#include <limits>

Q_LOGGING_CATEGORY(logMcp, "mcp")

std::atomic_bool McpProbe::s_enabled{false};
std::atomic_bool McpProbe::s_capturing{false};

namespace
{

constexpr int maxLogLines = 5000;
constexpr int maxAudioSlots = 16;
constexpr int maxPortErrors = 20;
constexpr int maxRadioTraffic = 5000;
constexpr int maxNetworkTraffic = 2000;
constexpr int maxTrafficBytes = 1024;
constexpr int maxCaptureSeconds = 900;

// Icom LAN keepalives, left out so they do not flood the capture.
constexpr int icomPingSize = 0x15;
constexpr int icomControlSize = 0x10;

struct TrafficEntry
{
    qint64 msecsSinceEpoch;
    bool toRadio;
    QString source;
    QByteArray data; // first maxTrafficBytes only
    int size;
};

struct LogEntry
{
    qint64 msecsSinceEpoch;
    QtMsgType type;
    const char *category; // QLoggingCategory names are static strings.
    QString msg;
};

// Level values are written by the audio thread and read by the MCP thread
// without a lock; a read may mix two updates, which is fine for diagnostics.
struct AudioSlot
{
    std::atomic<quint32> peak{0};
    std::atomic<quint32> rms{0};
    std::atomic<quint32> configuredLatency{0};
    std::atomic<quint32> measuredLatency{0};
    std::atomic_bool underrun{false};
    std::atomic_bool overrun{false};
    std::atomic<quint64> updates{0};
    std::atomic<quint64> underrunUpdates{0};
    std::atomic<quint64> overrunUpdates{0};
    std::atomic<qint64> lastUpdateMs{-1};

    // Guarded by Probe::audioMutex.
    const audioHandlerBase *handler = nullptr;
    bool active = false;
    qint64 attachedMs = 0;
    qint64 detachedMs = -1;
    QJsonObject description;
};

struct Probe
{
    // Started on first use: hooks attach whether or not MCP is running, and
    // may read the clock from any thread.
    Probe()
    {
        clock.start();
    }

    QElapsedTimer clock;

    QMutex logMutex;
    std::deque<LogEntry> log;

    QMutex audioMutex;
    std::array<AudioSlot, maxAudioSlots> audio;

    QMutex statusMutex;
    bool haveStatus = false;
    networkStatus status{};
    qint64 statusMs = -1;
    bool haveLevels = false;
    networkAudioLevels levels{};
    qint64 levelsMs = -1;
    std::deque<QJsonObject> portErrors;

    QMutex trafficMutex;
    std::deque<TrafficEntry> radioTraffic;
    std::deque<TrafficEntry> networkTraffic;
    qint64 captureStartMs = -1;
    qint64 captureUntilMs = -1;
    bool captureScope = false;
    quint64 radioRecorded = 0;
    quint64 networkRecorded = 0;
    quint64 scopeSkipped = 0;
    // Spectrum command bytes from the rig file, used to skip scope data.
    QVector<QByteArray> scopeSignatures;
    bool civFraming = false;

    QMutex capsMutex;
    bool haveCaps = false;
    bool capsCurrent = false;
    rigCapabilities caps;
    qint64 capsMs = -1;
    std::atomic_bool capsConnected{false};
};

Probe &probe()
{
    static Probe p;
    return p;
}

qint64 nowMs()
{
    return probe().clock.isValid() ? probe().clock.elapsed() : 0;
}

QString levelName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("critical");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

// QtMsgType is not ordered by severity (QtInfoMsg is 4).
int severity(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
        return 3;
    case QtFatalMsg:
        return 4;
    }
    return 0;
}

QJsonValue ageOf(qint64 thenMs)
{
    if (thenMs < 0)
        return QJsonValue();
    return double(nowMs() - thenMs);
}

QString threadLabel()
{
    QThread *thread = QThread::currentThread();
    return thread ? thread->objectName() : QString();
}

// Stores the latest status without ever blocking the radio thread; if the MCP
// thread is reading at that moment, this update is skipped.
void storeStatus(const networkStatus &status)
{
    if (!McpProbe::enabled())
        return;
    Probe &p = probe();
    if (!p.statusMutex.tryLock())
        return;
    p.status = status;
    p.haveStatus = true;
    p.statusMs = nowMs();
    p.statusMutex.unlock();
}

void storeLevels(const networkAudioLevels &levels)
{
    if (!McpProbe::enabled())
        return;
    Probe &p = probe();
    if (!p.statusMutex.tryLock())
        return;
    p.levels = levels;
    p.haveLevels = true;
    p.levelsMs = nowMs();
    p.statusMutex.unlock();
}

void storePortError(const errorType &err)
{
    Probe &p = probe();
    QMutexLocker locker(&p.statusMutex);
    QJsonObject entry;
    entry["time"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    entry["alert"] = err.alert;
    entry["device"] = err.device;
    entry["message"] = err.message;
    p.portErrors.push_back(entry);
    while (int(p.portErrors.size()) > maxPortErrors)
        p.portErrors.pop_front();
}

void storeCaps(const rigCapabilities *caps)
{
    Probe &p = probe();
    QMutexLocker locker(&p.capsMutex);
    // A null pointer means the radio went away; keep its last capabilities.
    p.capsCurrent = caps != nullptr;
    if (!caps)
        return;
    p.caps = *caps;
    p.haveCaps = true;
    p.capsMs = nowMs();
    locker.unlock();

    QVector<QByteArray> signatures;
    for (funcs f : {funcScopeWaveData, funcUSBScope, funcScopeInfo})
    {
        const QByteArray sig = caps->commands.value(f).data;
        if (!sig.isEmpty())
            signatures.append(sig);
    }
    QMutexLocker trafficLocker(&p.trafficMutex);
    p.scopeSignatures = signatures;
    p.civFraming = caps->manufacturer == manufIcom;
}

// Icom frames are FE FE <to> <from> <command...>; Kenwood and Yaesu commands
// are text, so a substring match is enough. Caller holds trafficMutex.
bool isScopeData(const Probe &p, const QByteArray &data)
{
    for (const QByteArray &sig : p.scopeSignatures)
    {
        if (p.civFraming)
        {
            if (data.size() >= 4 + sig.size() && quint8(data.at(0)) == 0xFE && quint8(data.at(1)) == 0xFE
                && data.mid(4, sig.size()) == sig)
                return true;
        }
        else if (data.contains(sig))
        {
            return true;
        }
    }
    return false;
}

void pushTraffic(std::deque<TrafficEntry> &log, int capacity, const QString &source, bool toRadio, const QByteArray &data)
{
    // Deep copy: callers may pass QByteArray::fromRawData() over a stack buffer.
    log.push_back({QDateTime::currentMSecsSinceEpoch(),
                   toRadio,
                   source,
                   QByteArray(data.constData(), qMin(int(data.size()), maxTrafficBytes)),
                   int(data.size())});
    while (int(log.size()) > capacity)
        log.pop_front();
}

QJsonObject trafficJson(const TrafficEntry &e)
{
    QJsonObject o;
    o["time"] = QDateTime::fromMSecsSinceEpoch(e.msecsSinceEpoch).toString("hh:mm:ss.zzz");
    o["source"] = e.source;
    o["direction"] = e.toRadio ? QStringLiteral("toRadio") : QStringLiteral("fromRadio");
    o["bytes"] = e.size;
    o["hex"] = QString::fromLatin1(e.data.toHex(' '));
    if (e.data.size() < e.size)
        o["truncated"] = true;

    // Kenwood and Yaesu CAT is text; show it when the bytes are mostly printable.
    int printable = 0;
    QString text;
    text.reserve(e.data.size());
    for (char ch : e.data)
    {
        const quint8 b = quint8(ch);
        if (b >= 0x20 && b < 0x7F)
        {
            ++printable;
            text += QLatin1Char(ch);
        }
        else if (b == '\r')
            text += QStringLiteral("\\r");
        else if (b == '\n')
            text += QStringLiteral("\\n");
        else
            text += QLatin1Char('.');
    }
    if (!e.data.isEmpty() && printable * 10 >= e.data.size() * 8)
        o["text"] = text;
    return o;
}

} // namespace

void McpProbe::setEnabled(bool on)
{
    s_enabled.store(on, std::memory_order_relaxed);
    if (!on)
        stopCapture();
}

qint64 McpProbe::uptimeMs()
{
    return nowMs();
}

void McpProbe::logMessage(QtMsgType type, const char *category, const QString &msg)
{
    if (!enabled())
        return;

    Probe &p = probe();
    QMutexLocker locker(&p.logMutex);
    p.log.push_back({QDateTime::currentMSecsSinceEpoch(), type, category ? category : "default", msg});
    if (int(p.log.size()) > maxLogLines)
        p.log.pop_front();
}

// Rigs and audio handlers are attached even while MCP is off, so that MCP can
// be turned on at run time and still see the radio and audio already open.
// Until then each hook costs an atomic load.
void McpProbe::attachRig(rigCommander *rig)
{
    if (!rig)
        return;

    // Lambdas run in the emitting thread; the rig is the context so the
    // connections end with it.
    QObject::connect(rig, &rigCommander::haveStatusUpdate, rig, &storeStatus, Qt::DirectConnection);
    QObject::connect(rig, &rigCommander::haveNetworkAudioLevels, rig, &storeLevels, Qt::DirectConnection);
    QObject::connect(rig, &rigCommander::havePortError, rig, &storePortError, Qt::DirectConnection);

    // Most command traffic passes through these two signals; the other send
    // paths call MCP_RADIO_TX or MCP_RADIO_RX themselves.
    QObject::connect(
        rig,
        &rigCommander::dataForComm,
        rig,
        [rig](const QByteArray &data) { radioTraffic(rig, true, data); },
        Qt::DirectConnection);
    QObject::connect(
        rig,
        &rigCommander::haveDataFromRig,
        rig,
        [rig](const QByteArray &data) { radioTraffic(rig, false, data); },
        Qt::DirectConnection);

    // The rig capabilities belong to the rig thread and are deleted with the
    // rig, so keep a copy rather than the pointer.
    if (!probe().capsConnected.exchange(true))
    {
        cachingQueue *queue = cachingQueue::getInstance();
        QObject::connect(queue, &cachingQueue::rigCapsUpdated, queue, &storeCaps, Qt::DirectConnection);
    }
}

void McpProbe::attachAudioHandler(audioHandlerBase *handler,
                                  const audioSetup &setup,
                                  const QAudioFormat &nativeFormat,
                                  const QAudioFormat &radioFormat)
{
    if (!handler)
        return;

    Probe &p = probe();
    int index = -1;
    {
        QMutexLocker locker(&p.audioMutex);
        // Prefer a free slot, then the slot whose handler went away first.
        qint64 oldestDetach = std::numeric_limits<qint64>::max();
        for (int i = 0; i < maxAudioSlots; ++i)
        {
            const AudioSlot &slot = p.audio[i];
            if (!slot.handler && !slot.active)
            {
                index = i;
                break;
            }
            if (!slot.active && slot.detachedMs < oldestDetach)
            {
                oldestDetach = slot.detachedMs;
                index = i;
            }
        }
        if (index < 0)
        {
            qCWarning(logMcp) << "No free audio level slot for" << handler;
            return;
        }

        AudioSlot &slot = p.audio[index];
        slot.peak = 0;
        slot.rms = 0;
        slot.configuredLatency = setup.latency;
        slot.measuredLatency = 0;
        slot.underrun = false;
        slot.overrun = false;
        slot.updates = 0;
        slot.underrunUpdates = 0;
        slot.overrunUpdates = 0;
        slot.lastUpdateMs = -1;
        slot.handler = handler;
        slot.active = true;
        slot.attachedMs = nowMs();
        slot.detachedMs = -1;

        const QString handlerClass = QString::fromLatin1(handler->metaObject()->className());
        QJsonObject d;
        d["handlerClass"] = handlerClass;
        d["direction"] = setup.isinput ? QStringLiteral("input") : QStringLiteral("output");
        d["thread"] = threadLabel();
        // TCI handlers exchange audio with a TCI client over the network; the
        // device name they inherit from the local audio settings is not used.
        if (handlerClass.contains(QLatin1String("Tci")))
            d["deviceName"] = QStringLiteral("TCI client (network)");
        else
            d["deviceName"] = setup.name;
        d["audioSystem"] = int(setup.type);
        d["codec"] = int(setup.codec);
        d["sampleRate"] = double(setup.sampleRate);
        d["latencyMs"] = int(setup.latency);
        d["blockSizeMs"] = int(setup.blockSize);
        d["resampleQuality"] = int(setup.resampleQuality);
        d["deviceSampleRate"] = nativeFormat.sampleRate();
        d["deviceChannels"] = nativeFormat.channelCount();
        d["radioSampleRate"] = radioFormat.sampleRate();
        d["radioChannels"] = radioFormat.channelCount();
        slot.description = d;
    }

    AudioSlot *slot = &p.audio[index];
    // Runs on the audio thread for every block: atomics only, no locks or
    // allocations.
    QObject::connect(
        handler,
        &audioHandlerBase::haveLevels,
        handler,
        [slot](quint16 peak, quint16 rms, quint16 configured, quint16 measured, bool underrun, bool overrun) {
            if (!enabled())
                return;
            slot->peak.store(peak, std::memory_order_relaxed);
            slot->rms.store(rms, std::memory_order_relaxed);
            slot->configuredLatency.store(configured, std::memory_order_relaxed);
            slot->measuredLatency.store(measured, std::memory_order_relaxed);
            slot->underrun.store(underrun, std::memory_order_relaxed);
            slot->overrun.store(overrun, std::memory_order_relaxed);
            slot->updates.fetch_add(1, std::memory_order_relaxed);
            if (underrun)
                slot->underrunUpdates.fetch_add(1, std::memory_order_relaxed);
            if (overrun)
                slot->overrunUpdates.fetch_add(1, std::memory_order_relaxed);
            slot->lastUpdateMs.store(nowMs(), std::memory_order_relaxed);
        },
        Qt::DirectConnection);

    QObject::connect(handler, &QObject::destroyed, [slot, handler]() {
        QMutexLocker locker(&probe().audioMutex);
        if (slot->handler != handler)
            return;
        slot->active = false;
        slot->detachedMs = nowMs();
    });
}

QJsonObject McpProbe::logLines(int maxLines, QtMsgType minType, const QString &category, const QString &contains)
{
    std::deque<LogEntry> copy;
    {
        Probe &p = probe();
        QMutexLocker locker(&p.logMutex);
        copy = p.log;
    }

    const int minSeverity = severity(minType);
    QJsonArray lines;
    int matched = 0;
    // Walk newest first so the limit keeps the most recent lines.
    for (auto it = copy.crbegin(); it != copy.crend() && matched < maxLines; ++it)
    {
        if (severity(it->type) < minSeverity)
            continue;
        const QString cat = QString::fromUtf8(it->category);
        if (!category.isEmpty() && !cat.startsWith(category, Qt::CaseInsensitive))
            continue;
        if (!contains.isEmpty() && !it->msg.contains(contains, Qt::CaseInsensitive))
            continue;

        lines.prepend(QStringLiteral("%1 %2 %3: %4")
                          .arg(QDateTime::fromMSecsSinceEpoch(it->msecsSinceEpoch).toString("yyyy-MM-dd hh:mm:ss.zzz"),
                               levelName(it->type),
                               cat,
                               it->msg));
        ++matched;
    }

    QJsonObject result;
    result["bufferedLines"] = int(copy.size());
    result["bufferCapacity"] = maxLogLines;
    result["returnedLines"] = matched;
    result["lines"] = lines;
    return result;
}

QJsonObject McpProbe::audioLevels()
{
    Probe &p = probe();
    QJsonArray handlers;
    {
        QMutexLocker locker(&p.audioMutex);
        for (const AudioSlot &slot : p.audio)
        {
            if (!slot.handler)
                continue;
            QJsonObject h = slot.description;
            h["active"] = slot.active;
            h["attachedAgeMs"] = ageOf(slot.attachedMs);
            h["detachedAgeMs"] = ageOf(slot.detachedMs);
            h["peak"] = int(slot.peak.load(std::memory_order_relaxed));
            h["rms"] = int(slot.rms.load(std::memory_order_relaxed));
            h["configuredLatencyMs"] = int(slot.configuredLatency.load(std::memory_order_relaxed));
            h["measuredLatencyMs"] = int(slot.measuredLatency.load(std::memory_order_relaxed));
            h["underrun"] = slot.underrun.load(std::memory_order_relaxed);
            h["overrun"] = slot.overrun.load(std::memory_order_relaxed);
            h["levelUpdates"] = double(slot.updates.load(std::memory_order_relaxed));
            h["updatesWithUnderrun"] = double(slot.underrunUpdates.load(std::memory_order_relaxed));
            h["updatesWithOverrun"] = double(slot.overrunUpdates.load(std::memory_order_relaxed));
            h["lastUpdateAgeMs"] = ageOf(slot.lastUpdateMs.load(std::memory_order_relaxed));
            handlers.append(h);
        }
    }

    QJsonObject result;
    result["note"] = QStringLiteral("peak and rms are 0-255. Handlers are local audio devices, including those a "
                                    "network connection opens. lastUpdateAgeMs growing on an active handler means "
                                    "that audio stream has stalled.");
    result["handlers"] = handlers;
    result["network"] = networkStatusJson();
    return result;
}

QJsonObject McpProbe::networkStatusJson()
{
    Probe &p = probe();
    QMutexLocker locker(&p.statusMutex);

    QJsonObject result;
    if (p.haveStatus)
    {
        const networkStatus &s = p.status;
        QJsonObject st;
        st["ageMs"] = ageOf(p.statusMs);
        st["rxAudioLevel"] = int(s.rxAudioLevel);
        st["txAudioLevel"] = int(s.txAudioLevel);
        st["rxAudioBufferPercent"] = int(s.rxAudioBufferPercent);
        st["txAudioBufferPercent"] = int(s.txAudioBufferPercent);
        st["rxLatencyMs"] = int(s.rxLatency);
        st["txLatencyMs"] = int(s.txLatency);
        st["rxCurrentLatencyMs"] = int(s.rxCurrentLatency);
        st["txCurrentLatencyMs"] = int(s.txCurrentLatency);
        st["rxUnderrun"] = s.rxUnderrun;
        st["txUnderrun"] = s.txUnderrun;
        st["rxOverrun"] = s.rxOverrun;
        st["txOverrun"] = s.txOverrun;
        st["packetsSent"] = double(s.packetsSent);
        st["packetsLost"] = double(s.packetsLost);
        st["rttMs"] = int(s.rtt);
        st["networkLatencyMs"] = double(s.networkLatency);
        st["timeDifferenceMs"] = double(s.timeDifference);
        st["message"] = s.message;
        result["status"] = st;
    }
    else
    {
        result["status"] = QJsonValue();
    }

    if (p.haveLevels)
    {
        const networkAudioLevels &l = p.levels;
        QJsonObject lv;
        lv["ageMs"] = ageOf(p.levelsMs);
        lv["haveRxLevels"] = l.haveRxLevels;
        lv["haveTxLevels"] = l.haveTxLevels;
        lv["rxPeak"] = int(l.rxAudioPeak);
        lv["rxRms"] = int(l.rxAudioRMS);
        lv["txPeak"] = int(l.txAudioPeak);
        lv["txRms"] = int(l.txAudioRMS);
        result["levels"] = lv;
    }
    else
    {
        result["levels"] = QJsonValue();
    }

    QJsonArray errors;
    for (const QJsonObject &e : p.portErrors)
        errors.append(e);
    result["recentPortErrors"] = errors;
    return result;
}

bool McpProbe::rigCapsSnapshot(rigCapabilities *out, qint64 *ageMs, bool *current)
{
    Probe &p = probe();
    QMutexLocker locker(&p.capsMutex);
    if (!p.haveCaps || !out)
        return false;
    *out = p.caps;
    if (ageMs)
        *ageMs = nowMs() - p.capsMs;
    if (current)
        *current = p.capsCurrent;
    return true;
}

void McpProbe::recordRadio(const QObject *source, bool toRadio, const QByteArray &data)
{
    if (data.isEmpty())
        return;
    const QString label = source ? QString::fromLatin1(source->metaObject()->className()) : QStringLiteral("radio");

    Probe &p = probe();
    QMutexLocker locker(&p.trafficMutex);
    if (nowMs() >= p.captureUntilMs)
    {
        s_capturing.store(false, std::memory_order_relaxed);
        return;
    }
    if (!toRadio && !p.captureScope && isScopeData(p, data))
    {
        ++p.scopeSkipped;
        return;
    }
    pushTraffic(p.radioTraffic, maxRadioTraffic, label, toRadio, data);
    ++p.radioRecorded;
}

void McpProbe::recordNetwork(const QObject *source, bool toRadio, const QByteArray &data)
{
    if (!source || data.isEmpty())
        return;

    // Only control channels; the same send paths also carry audio and scope.
    const char *className = source->metaObject()->className();
    QString channel;
    if (qstrcmp(className, "icomUdpHandler") == 0)
    {
        if (data.size() == icomPingSize)
            return;
        // Idle control packets have type 0 in bytes 4-5.
        if (data.size() == icomControlSize && data.at(4) == 0 && data.at(5) == 0)
            return;
        channel = QStringLiteral("icom-control");
    }
    else if (qstrcmp(className, "yaesuUdpControl") == 0)
    {
        channel = QStringLiteral("yaesu-control");
    }
    else
    {
        return;
    }

    Probe &p = probe();
    QMutexLocker locker(&p.trafficMutex);
    if (nowMs() >= p.captureUntilMs)
    {
        s_capturing.store(false, std::memory_order_relaxed);
        return;
    }
    pushTraffic(p.networkTraffic, maxNetworkTraffic, channel, toRadio, data);
    ++p.networkRecorded;
}

void McpProbe::startCapture(int seconds, bool includeScope, bool clear)
{
    Probe &p = probe();
    QMutexLocker locker(&p.trafficMutex);
    if (clear)
    {
        p.radioTraffic.clear();
        p.networkTraffic.clear();
        p.radioRecorded = 0;
        p.networkRecorded = 0;
        p.scopeSkipped = 0;
    }
    p.captureStartMs = nowMs();
    p.captureUntilMs = p.captureStartMs + qint64(qBound(1, seconds, maxCaptureSeconds)) * 1000;
    p.captureScope = includeScope;
    s_capturing.store(true, std::memory_order_relaxed);
}

void McpProbe::ensureCapture(int seconds)
{
    Probe &p = probe();
    QMutexLocker locker(&p.trafficMutex);
    const qint64 now = nowMs();
    const qint64 until = now + qint64(qBound(1, seconds, maxCaptureSeconds)) * 1000;
    if (!s_capturing.load(std::memory_order_relaxed) || p.captureUntilMs <= now)
    {
        p.captureStartMs = now;
        p.captureScope = false;
    }
    p.captureUntilMs = qMax(p.captureUntilMs, until);
    s_capturing.store(true, std::memory_order_relaxed);
}

void McpProbe::stopCapture()
{
    Probe &p = probe();
    QMutexLocker locker(&p.trafficMutex);
    if (p.captureUntilMs > nowMs())
        p.captureUntilMs = nowMs();
    s_capturing.store(false, std::memory_order_relaxed);
}

QJsonObject McpProbe::captureStatus()
{
    Probe &p = probe();
    QMutexLocker locker(&p.trafficMutex);
    const qint64 now = nowMs();
    const bool active = s_capturing.load(std::memory_order_relaxed) && now < p.captureUntilMs;

    QJsonObject o;
    o["active"] = active;
    o["remainingSeconds"] = active ? double(p.captureUntilMs - now) / 1000.0 : 0.0;
    o["startedAgoSeconds"] = p.captureStartMs >= 0 ? QJsonValue(double(now - p.captureStartMs) / 1000.0) : QJsonValue();
    o["includeScope"] = p.captureScope;
    o["radioEntries"] = int(p.radioTraffic.size());
    o["radioEntriesRecorded"] = double(p.radioRecorded);
    o["networkEntries"] = int(p.networkTraffic.size());
    o["networkEntriesRecorded"] = double(p.networkRecorded);
    o["scopeChunksSkipped"] = double(p.scopeSkipped);
    o["capacity"] = QJsonObject{{"radio", maxRadioTraffic}, {"network", maxNetworkTraffic}, {"bytesPerEntry", maxTrafficBytes}};
    return o;
}

QJsonObject
McpProbe::traffic(const QString &kind, const QString &direction, int limit, const QString &contains, qint64 sinceMs)
{
    const bool wantRadio = kind != QLatin1String("network");
    const bool wantNetwork = kind != QLatin1String("radio");

    std::deque<TrafficEntry> radio;
    std::deque<TrafficEntry> network;
    {
        Probe &p = probe();
        QMutexLocker locker(&p.trafficMutex);
        if (wantRadio)
            radio = p.radioTraffic;
        if (wantNetwork)
            network = p.networkTraffic;
    }

    // Each log is in time order; merge them, then keep the newest matches.
    std::vector<const TrafficEntry *> merged;
    merged.reserve(radio.size() + network.size());
    auto r = radio.cbegin();
    auto n = network.cbegin();
    while (r != radio.cend() || n != network.cend())
    {
        if (n == network.cend() || (r != radio.cend() && r->msecsSinceEpoch <= n->msecsSinceEpoch))
            merged.push_back(&*r++);
        else
            merged.push_back(&*n++);
    }

    const QString needle = contains.toLower();
    QJsonArray entries;
    int matched = 0;
    for (auto it = merged.crbegin(); it != merged.crend() && matched < limit; ++it)
    {
        const TrafficEntry &e = **it;
        if (e.msecsSinceEpoch < sinceMs)
            break;
        if (direction == QLatin1String("toRadio") && !e.toRadio)
            continue;
        if (direction == QLatin1String("fromRadio") && e.toRadio)
            continue;
        const QJsonObject json = trafficJson(e);
        if (!needle.isEmpty() && !json.value("hex").toString().contains(needle)
            && !json.value("text").toString().toLower().contains(needle))
            continue;
        entries.prepend(json);
        ++matched;
    }

    QJsonObject result;
    result["capture"] = captureStatus();
    result["returnedEntries"] = matched;
    result["entries"] = entries;
    return result;
}
