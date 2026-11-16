#include "mcpaudiodiag.h"

#include <QDateTime>
#include <QJsonArray>
#include <QtEndian>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace McpAudioDiag
{
std::atomic_bool s_active{false};
} // namespace McpAudioDiag

namespace
{
using namespace McpAudioDiag;

constexpr int histBuckets = 13;
// Upper edges (ms) of all but the last interval bucket.
constexpr int histEdgesMs[histBuckets - 1] = {5, 10, 15, 20, 25, 30, 40, 50, 75, 100, 200, 500};
constexpr int anomalyRingSize = 1000;
// An interval is "late" once a stage has this many, and it is both this many
// times the mean and this much longer than it.
constexpr quint64 lateMinIntervals = 20;
constexpr double lateFactor = 2.5;
constexpr qint64 lateMarginUs = 15000;
// A block-boundary jump is a discontinuity when it is this many times the
// steepest step inside the blocks on either side, and at least this big
// (fraction of full scale).
constexpr float jumpFactor = 2.0f;
constexpr float jumpMinimum = 0.01f;

struct StageInfo
{
    const char *name;
    const char *description;
};

const StageInfo stageInfo[StageCount] = {
    {"tciChronoSent", "TCI server sent a TX chrono frame (value: chrono number)."},
    {"tciTxFromClient", "TX audio frame arrived from a TCI client (value: frames; continuity on the first channel)."},
    {"tciTxPaced", "TCI TX pacer released a block (value: frames still buffered after it)."},
    {"tciTxConverted", "TCI TX block after conversion to the radio's format (value: bytes)."},
    {"micTxForwarded", "Local microphone block routed towards the radio (value: bytes)."},
    {"radioTxPacket", "TX audio datagram sent to the radio, any source (value: bytes)."},
    {"radioTxAgeMs", "Age of each TX block when sent to the radio: ms since the pacer or audio input made it."},
    {"radioRxPacket", "RX audio datagram from the radio (value: bytes; sequence numbers checked)."},
    {"tciRxToClient", "RX audio frame sent to TCI clients (value: floats)."},
};

qint64 nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

template <typename T> void atomicMin(std::atomic<T> &a, T v)
{
    T cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
    {
    }
}

template <typename T> void atomicMax(std::atomic<T> &a, T v)
{
    T cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed))
    {
    }
}

struct StageState
{
    std::atomic<qint64> lastNs{-1};
    std::atomic<quint64> count{0};
    std::atomic<quint64> intervals{0};
    std::atomic<quint64> intervalSumUs{0};
    std::atomic<quint64> intervalMinUs{std::numeric_limits<quint64>::max()};
    std::atomic<quint64> intervalMaxUs{0};
    std::atomic<quint64> hist[histBuckets];
    std::atomic<quint64> late{0};

    std::atomic<quint64> values{0};
    std::atomic<qint64> valueLast{0};
    std::atomic<qint64> valueMin{std::numeric_limits<qint64>::max()};
    std::atomic<qint64> valueMax{std::numeric_limits<qint64>::min()};
    std::atomic<qint64> valueSum{0};

    std::atomic<quint64> contBlocks{0};
    std::atomic<quint64> contJumps{0};
    std::atomic<quint64> contSilent{0};
    std::atomic<float> contLast{0.0f};
    std::atomic<float> contPrevMaxStep{0.0f};
    std::atomic<bool> contHaveLast{false};

    std::atomic<qint32> seqLast{-1};
    std::atomic<quint64> seqPackets{0};
    std::atomic<quint64> seqMissing{0};
    std::atomic<quint64> seqRepeated{0};
    std::atomic<quint64> seqOutOfOrder{0};

    std::atomic<quint64> anomalies{0};

    void reset()
    {
        lastNs.store(-1);
        count.store(0);
        intervals.store(0);
        intervalSumUs.store(0);
        intervalMinUs.store(std::numeric_limits<quint64>::max());
        intervalMaxUs.store(0);
        for (auto &h : hist)
            h.store(0);
        late.store(0);
        values.store(0);
        valueLast.store(0);
        valueMin.store(std::numeric_limits<qint64>::max());
        valueMax.store(std::numeric_limits<qint64>::min());
        valueSum.store(0);
        contBlocks.store(0);
        contJumps.store(0);
        contSilent.store(0);
        contLast.store(0.0f);
        contPrevMaxStep.store(0.0f);
        contHaveLast.store(false);
        seqLast.store(-1);
        seqPackets.store(0);
        seqMissing.store(0);
        seqRepeated.store(0);
        seqOutOfOrder.store(0);
        anomalies.store(0);
    }
};

struct AnomalySlot
{
    std::atomic<quint64> seq{0}; // index + 1 once written
    std::atomic<qint64> tNs{0};
    std::atomic<int> stage{0};
    std::atomic<const char *> what{nullptr};
    std::atomic<double> value{0.0};
};

struct State
{
    StageState stages[StageCount];
    AnomalySlot ring[anomalyRingSize];
    std::atomic<quint64> anomalyNext{0};
    std::atomic<qint64> startNs{0};
    std::atomic<qint64> untilNs{0};
    std::atomic<qint64> startEpochMs{0};
    std::atomic<qint64> stoppedNs{0}; // 0 while running
};

State &state()
{
    static State s;
    return s;
}

// True while diagnostics run; turns them off once their time is up.
bool live(qint64 now)
{
    if (!s_active.load(std::memory_order_relaxed))
        return false;
    State &s = state();
    if (now >= s.untilNs.load(std::memory_order_relaxed))
    {
        bool expected = true;
        if (s_active.compare_exchange_strong(expected, false))
            s.stoppedNs.store(s.untilNs.load(std::memory_order_relaxed));
        return false;
    }
    return true;
}

void recordAnomaly(qint64 now, Stage stage, const char *what, double value)
{
    State &s = state();
    s.stages[stage].anomalies.fetch_add(1, std::memory_order_relaxed);
    const quint64 idx = s.anomalyNext.fetch_add(1, std::memory_order_relaxed);
    AnomalySlot &slot = s.ring[idx % anomalyRingSize];
    slot.seq.store(0, std::memory_order_release);
    slot.tNs.store(now, std::memory_order_relaxed);
    slot.stage.store(int(stage), std::memory_order_relaxed);
    slot.what.store(what, std::memory_order_relaxed);
    slot.value.store(value, std::memory_order_relaxed);
    slot.seq.store(idx + 1, std::memory_order_release);
}

void recordValue(StageState &st, qint64 value)
{
    st.values.fetch_add(1, std::memory_order_relaxed);
    st.valueLast.store(value, std::memory_order_relaxed);
    st.valueSum.fetch_add(value, std::memory_order_relaxed);
    atomicMin(st.valueMin, value);
    atomicMax(st.valueMax, value);
}

// Shared by the int16 and float continuity checks. sample(i) returns the
// first-channel sample of frame i as a fraction of full scale.
template <typename SampleAt> void checkContinuity(Stage stage, int frames, SampleAt sample)
{
    if (frames < 2)
        return;
    const qint64 now = nowNs();
    if (!live(now))
        return;
    StageState &st = state().stages[stage];

    float maxStep = 0.0f;
    float maxAbs = std::fabs(sample(0));
    float prev = sample(0);
    for (int i = 1; i < frames; ++i)
    {
        const float v = sample(i);
        maxStep = std::max(maxStep, std::fabs(v - prev));
        maxAbs = std::max(maxAbs, std::fabs(v));
        prev = v;
    }

    st.contBlocks.fetch_add(1, std::memory_order_relaxed);
    if (maxAbs == 0.0f)
        st.contSilent.fetch_add(1, std::memory_order_relaxed);

    if (st.contHaveLast.load(std::memory_order_relaxed))
    {
        const float jump = std::fabs(sample(0) - st.contLast.load(std::memory_order_relaxed));
        const float ref = std::max(maxStep, st.contPrevMaxStep.load(std::memory_order_relaxed));
        if (jump > jumpFactor * ref && jump > jumpMinimum)
        {
            st.contJumps.fetch_add(1, std::memory_order_relaxed);
            recordAnomaly(now, stage, "discontinuity between blocks (value: jump, fraction of full scale)", jump);
        }
    }
    st.contLast.store(prev, std::memory_order_relaxed);
    st.contPrevMaxStep.store(maxStep, std::memory_order_relaxed);
    st.contHaveLast.store(true, std::memory_order_relaxed);
}

QString isoTime(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs).toString(Qt::ISODateWithMs);
}

} // namespace

namespace McpAudioDiag
{

void event(Stage stage, qint64 value)
{
    const qint64 now = nowNs();
    if (!live(now))
        return;
    StageState &st = state().stages[stage];
    st.count.fetch_add(1, std::memory_order_relaxed);
    recordValue(st, value);

    const qint64 last = st.lastNs.exchange(now, std::memory_order_relaxed);
    if (last < 0 || now <= last)
        return;
    const quint64 us = quint64((now - last) / 1000);

    // Late check against the mean so far, before this interval is added.
    const quint64 n = st.intervals.load(std::memory_order_relaxed);
    if (n >= lateMinIntervals)
    {
        const double meanUs = double(st.intervalSumUs.load(std::memory_order_relaxed)) / double(n);
        if (double(us) > lateFactor * meanUs && double(us) > meanUs + double(lateMarginUs))
        {
            st.late.fetch_add(1, std::memory_order_relaxed);
            recordAnomaly(now, stage, "late: interval well above the mean (value: ms)", double(us) / 1000.0);
        }
    }

    st.intervals.fetch_add(1, std::memory_order_relaxed);
    st.intervalSumUs.fetch_add(us, std::memory_order_relaxed);
    atomicMin(st.intervalMinUs, us);
    atomicMax(st.intervalMaxUs, us);
    const quint64 ms = us / 1000;
    int b = 0;
    while (b < histBuckets - 1 && ms >= quint64(histEdgesMs[b]))
        ++b;
    st.hist[b].fetch_add(1, std::memory_order_relaxed);
}

void gauge(Stage stage, qint64 value)
{
    if (!live(nowNs()))
        return;
    StageState &st = state().stages[stage];
    st.count.fetch_add(1, std::memory_order_relaxed);
    recordValue(st, value);
}

void anomaly(Stage stage, const char *what, double value)
{
    const qint64 now = nowNs();
    if (!live(now))
        return;
    recordAnomaly(now, stage, what, value);
}

void continuityInt16(Stage stage, const char *data, int bytes, int channels)
{
    const int ch = qMax(1, channels);
    const int frames = bytes / int(sizeof(qint16)) / ch;
    if (!data || frames < 2)
        return;
    const uchar *p = reinterpret_cast<const uchar *>(data);
    checkContinuity(stage, frames, [p, ch](int i) {
        return float(qFromLittleEndian<qint16>(p + size_t(i) * size_t(ch) * sizeof(qint16))) / 32768.0f;
    });
}

void continuityFloat(Stage stage, const float *samples, int count, int channels)
{
    const int ch = qMax(1, channels);
    const int frames = count / ch;
    if (!samples || frames < 2)
        return;
    checkContinuity(stage, frames, [samples, ch](int i) { return samples[size_t(i) * size_t(ch)]; });
}

void sequence16(Stage stage, quint16 seq)
{
    const qint64 now = nowNs();
    if (!live(now))
        return;
    StageState &st = state().stages[stage];
    st.seqPackets.fetch_add(1, std::memory_order_relaxed);
    const qint32 last = st.seqLast.exchange(qint32(seq), std::memory_order_relaxed);
    if (last < 0)
        return;
    const quint16 diff = quint16(seq - quint16(last));
    if (diff == 1)
        return;
    if (diff == 0)
    {
        st.seqRepeated.fetch_add(1, std::memory_order_relaxed);
        recordAnomaly(now, stage, "repeated sequence number", seq);
    }
    else if (diff < 0x8000)
    {
        st.seqMissing.fetch_add(diff - 1, std::memory_order_relaxed);
        recordAnomaly(now, stage, "sequence gap (value: packets missing)", diff - 1);
    }
    else
    {
        st.seqOutOfOrder.fetch_add(1, std::memory_order_relaxed);
        recordAnomaly(now, stage, "sequence went backwards (value: packets)", 0x10000 - int(diff));
    }
}

void gaugeAgeMs(Stage stage, const QTime &created)
{
    if (!created.isValid())
        return;
    int ms = created.msecsTo(QTime::currentTime());
    if (ms < -12 * 3600 * 1000)
        ms += 24 * 3600 * 1000; // made before midnight, sent after it
    gauge(stage, ms);
}

void start(int seconds, bool clear)
{
    State &s = state();
    const qint64 now = nowNs();
    const bool wasActive = live(now);
    if (clear || !wasActive)
    {
        if (clear)
        {
            for (auto &st : s.stages)
                st.reset();
            for (auto &slot : s.ring)
                slot.seq.store(0);
            s.anomalyNext.store(0);
        }
        s.startNs.store(now);
        s.startEpochMs.store(QDateTime::currentMSecsSinceEpoch());
    }
    s.untilNs.store(now + qint64(qBound(1, seconds, maxSeconds)) * 1000000000ll);
    s.stoppedNs.store(0);
    s_active.store(true);
}

void stop()
{
    State &s = state();
    if (s_active.exchange(false))
        s.stoppedNs.store(nowNs());
}

QJsonObject status()
{
    State &s = state();
    const qint64 now = nowNs();
    const bool on = live(now);
    const qint64 startNs = s.startNs.load();
    const qint64 endNs = on ? now : (s.stoppedNs.load() ? s.stoppedNs.load() : now);
    QJsonObject o;
    o["active"] = on;
    o["remainingSeconds"] = on ? double(s.untilNs.load() - now) / 1e9 : 0.0;
    if (startNs > 0)
    {
        o["startedAt"] = isoTime(s.startEpochMs.load());
        o["recordedSeconds"] = double(endNs - startNs) / 1e9;
    }
    return o;
}

QJsonObject report(int maxAnomalies, bool reset)
{
    State &s = state();
    QJsonObject o = status();
    const qint64 startNs = s.startNs.load();
    const qint64 startEpochMs = s.startEpochMs.load();
    const double seconds = o.value("recordedSeconds").toDouble();

    QJsonObject stages;
    for (int i = 0; i < StageCount; ++i)
    {
        StageState &st = s.stages[i];
        const quint64 count = st.count.load();
        const quint64 anomalies = st.anomalies.load();
        if (count == 0 && anomalies == 0 && st.contBlocks.load() == 0 && st.seqPackets.load() == 0)
            continue;

        QJsonObject so;
        so["description"] = QString::fromLatin1(stageInfo[i].description);
        so["count"] = double(count);
        if (seconds > 0)
            so["perSecond"] = double(count) / seconds;

        const quint64 n = st.intervals.load();
        if (n > 0)
        {
            QJsonObject iv;
            iv["count"] = double(n);
            iv["meanMs"] = double(st.intervalSumUs.load()) / double(n) / 1000.0;
            iv["minMs"] = double(st.intervalMinUs.load()) / 1000.0;
            iv["maxMs"] = double(st.intervalMaxUs.load()) / 1000.0;
            iv["late"] = double(st.late.load());
            QJsonObject hist;
            for (int b = 0; b < histBuckets; ++b)
            {
                const quint64 h = st.hist[b].load();
                if (!h)
                    continue;
                const QString key = b < histBuckets - 1 ? QStringLiteral("<%1").arg(histEdgesMs[b])
                                                        : QStringLiteral(">=%1").arg(histEdgesMs[histBuckets - 2]);
                hist[key] = double(h);
            }
            iv["histogramMs"] = hist;
            so["intervals"] = iv;
        }

        const quint64 values = st.values.load();
        if (values > 0)
        {
            QJsonObject v;
            v["last"] = double(st.valueLast.load());
            v["min"] = double(st.valueMin.load());
            v["max"] = double(st.valueMax.load());
            v["mean"] = double(st.valueSum.load()) / double(values);
            so["value"] = v;
        }

        if (st.contBlocks.load() > 0)
        {
            QJsonObject c;
            c["blocksChecked"] = double(st.contBlocks.load());
            c["discontinuities"] = double(st.contJumps.load());
            c["silentBlocks"] = double(st.contSilent.load());
            so["continuity"] = c;
        }

        if (st.seqPackets.load() > 0)
        {
            QJsonObject q;
            q["packets"] = double(st.seqPackets.load());
            q["missing"] = double(st.seqMissing.load());
            q["repeated"] = double(st.seqRepeated.load());
            q["backwards"] = double(st.seqOutOfOrder.load());
            so["sequence"] = q;
        }

        so["anomalies"] = double(anomalies);
        stages[QString::fromLatin1(stageInfo[i].name)] = so;
    }
    o["stages"] = stages;

    const quint64 next = s.anomalyNext.load();
    const quint64 kept = qMin<quint64>(next, anomalyRingSize);
    const quint64 want = qMin<quint64>(kept, quint64(qMax(0, maxAnomalies)));
    QJsonArray list;
    for (quint64 idx = next - want; idx < next; ++idx)
    {
        AnomalySlot &slot = s.ring[idx % anomalyRingSize];
        if (slot.seq.load(std::memory_order_acquire) != idx + 1)
            continue;
        const qint64 tNs = slot.tNs.load(std::memory_order_relaxed);
        const int stage = slot.stage.load(std::memory_order_relaxed);
        const char *what = slot.what.load(std::memory_order_relaxed);
        const double value = slot.value.load(std::memory_order_relaxed);
        if (slot.seq.load(std::memory_order_acquire) != idx + 1 || stage < 0 || stage >= StageCount)
            continue;
        QJsonObject a;
        const double atMs = double(tNs - startNs) / 1e6;
        a["atMs"] = atMs;
        a["time"] = isoTime(startEpochMs + qint64(atMs));
        a["stage"] = QString::fromLatin1(stageInfo[stage].name);
        a["what"] = what ? QString::fromLatin1(what) : QString();
        a["value"] = value;
        list.append(a);
    }
    o["anomaliesTotal"] = double(next);
    o["anomaliesKept"] = double(kept);
    o["anomalies"] = list;
    o["note"] = QStringLiteral(
        "atMs is milliseconds since diagnostics started (startedAt). Intervals are between consecutive runs of a "
        "stage. 'late' marks intervals over 2.5x the mean and 15 ms above it. A discontinuity is a jump between "
        "blocks over 2x the steepest step inside them and at least 1% of full scale (first channel only).");

    if (reset)
    {
        for (auto &st : s.stages)
            st.reset();
        for (auto &slot : s.ring)
            slot.seq.store(0);
        s.anomalyNext.store(0);
        s.startNs.store(nowNs());
        s.startEpochMs.store(QDateTime::currentMSecsSinceEpoch());
    }
    return o;
}

} // namespace McpAudioDiag
