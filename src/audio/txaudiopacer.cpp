#include "txaudiopacer.h"
#include "logcategories.h"

#include <chrono>
#include <cmath>
#include <thread>

qint64 radioAudioClock::steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

namespace {

// Interpolation filter: for each of phases + 1 fractional positions, the
// taps weights of a Blackman-windowed sinc, normalised to unity gain.
const std::vector<float> &interpolationTable()
{
    static const std::vector<float> table = [] {
        constexpr int taps = txFineResampler::taps, phases = txFineResampler::phases;
        constexpr double pi = 3.14159265358979323846;
        std::vector<float> t(size_t((phases + 1) * taps));
        for (int p = 0; p <= phases; ++p) {
            const double frac = double(p) / phases;
            double sum = 0;
            std::vector<double> w(taps);
            for (int j = 0; j < taps; ++j) {
                const double d = double(j - (taps / 2 - 1)) - frac; // distance from the output point
                const double sinc = d == 0 ? 1.0 : std::sin(pi * d) / (pi * d);
                const double x = (d + taps / 2.0) / taps;           // 0..1 across the window
                const double win = (x <= 0 || x >= 1) ? 0.0
                                   : 0.42 - 0.5 * std::cos(2 * pi * x) + 0.08 * std::cos(4 * pi * x);
                w[size_t(j)] = sinc * win;
                sum += w[size_t(j)];
            }
            for (int j = 0; j < taps; ++j)
                t[size_t(p * taps + j)] = float(w[size_t(j)] / sum);
        }
        return t;
    }();
    return table;
}
}

// ---------------------------------------------------------------------------

txFineResampler::txFineResampler(int channels) : channels(qMax(1, channels))
{
    reset();
}

void txFineResampler::setRatio(double outPerIn)
{
    step = 1.0 / qBound(0.99, outPerIn, 1.01);
}

void txFineResampler::reset()
{
    // Silence before the first frame, so the first output is that frame.
    history.assign(size_t((taps / 2 - 1) * channels), 0.0f);
    pos = taps / 2 - 1;
}

QByteArray txFineResampler::process(const QByteArray &in)
{
    const int inFrames = int(in.size()) / (2 * channels);
    const auto *s = reinterpret_cast<const qint16 *>(in.constData());
    history.reserve(history.size() + size_t(inFrames * channels));
    for (int i = 0; i < inFrames * channels; ++i)
        history.push_back(float(s[i]));

    const int haveFrames = int(history.size()) / channels;
    const std::vector<float> &table = interpolationTable();
    QByteArray out;
    out.reserve(int((inFrames / step + 2) * 2 * channels));
    // The output at pos needs frames floor(pos) - 7 .. floor(pos) + 8.
    while (int(pos) + taps / 2 < haveFrames) {
        const int base = int(pos) - (taps / 2 - 1);
        const double ph = (pos - std::floor(pos)) * phases;
        const int p = qMin(int(ph), phases - 1);
        const float f = float(ph - p);
        const float *w0 = &table[size_t(p * taps)];
        const float *w1 = w0 + taps;
        for (int c = 0; c < channels; ++c) {
            float acc = 0;
            for (int j = 0; j < taps; ++j) {
                const float w = w0[j] + f * (w1[j] - w0[j]);
                acc += w * history[size_t((base + j) * channels + c)];
            }
            const qint16 v = qint16(qBound(-32768L, std::lrintf(acc), 32767L));
            out.append(reinterpret_cast<const char *>(&v), 2);
        }
        pos += step;
    }
    // Keep only the frames later outputs still need.
    const int drop = qMax(0, int(pos) - (taps / 2 - 1));
    if (drop > 0) {
        history.erase(history.begin(), history.begin() + std::ptrdiff_t(drop * channels));
        pos -= drop;
    }
    return out;
}

QByteArray txFineResampler::flush()
{
    QByteArray tail = process(QByteArray(taps / 2 * 2 * channels, char(0)));
    reset();
    return tail;
}

// ---------------------------------------------------------------------------

void radioAudioClock::packet(int frames, quint32 sampleRate)
{
    packet(frames, sampleRate, steadyNs());
}

void radioAudioClock::packet(int frames, quint32 sampleRate, qint64 nowNs)
{
    if (frames <= 0 || sampleRate == 0)
        return;
    if (started && (sampleRate != lastRate || nowNs - lastNs > qint64(gapMs) * 1000000))
        restart(); // the stream stopped or changed: measure afresh, keep the estimate
    if (!started) {
        started = true;
        startNs = nowNs;
        audioNs = 0;
        bucket = -1;
    }
    lastNs = nowNs;
    lastRate = sampleRate;

    // How late this packet is against the radio's audio so far: network delay
    // plus the clock difference, which grows steadily.
    const double offset = double(nowNs - startNs) - audioNs;
    const qint64 b = (nowNs - startNs) / (qint64(bucketMs) * 1000000);
    if (b != bucket) {
        closeBucket();
        bucket = b;
        bucketHas = false;
    }
    const bool timed = nowNs - busyEndNs > afterBusyNs;
    if (timed && (!bucketHas || offset < bucketMin.offsetNs)) {
        bucketMin = {audioNs, offset};
        bucketHas = true;
    }
    audioNs += double(frames) * 1e9 / double(sampleRate);
}

void radioAudioClock::restart()
{
    started = false;
    points.clear();
    bucket = -1;
}

void radioAudioClock::closeBucket()
{
    if (bucket < 0 || !bucketHas)
        return; // no packet in that second could be timed

    // Fit offset = a + slope * audio time; the slope is the clock difference.
    auto fit = [this](double &a, double &slope) {
        const double n = double(points.size());
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const Point &p : points) {
            sx += p.audioNs;
            sy += p.offsetNs;
            sxx += p.audioNs * p.audioNs;
            sxy += p.audioNs * p.offsetNs;
        }
        const double d = n * sxx - sx * sx;
        if (d <= 0)
            return false;
        slope = (n * sxy - sx * sy) / d;
        a = (sy - slope * sx) / n;
        return true;
    };

    double a = 0, slope = 0;
    if (points.size() >= size_t(minBuckets) && fit(a, slope) &&
        std::abs(bucketMin.offsetNs - (a + slope * bucketMin.audioNs)) > stepUs * 1000.0) {
        // Audio went missing (a lost packet) or the clock jumped.
        points.clear();
    }
    points.push_back(bucketMin);
    while (points.size() > size_t(maxBuckets))
        points.pop_front();
    if (points.size() >= size_t(minBuckets) && fit(a, slope) && std::abs(slope) * 1e6 < maxPpm) {
        const bool first = !valid;
        currentRatio = 1.0 + slope;
        valid = true;
        if (first)
            qInfo(logAudio()) << "Radio audio clock measured:" << QString::number(slope * 1e6, 'f', 1)
                              << "ppm against this computer; TX audio will follow it";
    }
}

// ---------------------------------------------------------------------------

void txDriftControl::startStream()
{
    target = -1;
    sum = 0;
    count = 0;
    seconds = 0;
    // Start from the learnt clock difference alone.
    currentRatio = 1.0 - ki * integral;
}

void txDriftControl::clockChanged()
{
    integral = 0;
    currentRatio = 1.0;
}

void txDriftControl::level(double seconds)
{
    sum += seconds;
    ++count;
}

double txDriftControl::update()
{
    if (count == 0)
        return currentRatio;
    const double mean = sum / count;
    sum = 0;
    count = 0;
    if (++seconds <= settleSeconds)
        return currentRatio;
    if (target < 0) {
        target = mean; // the level once settled is the target
        return currentRatio;
    }
    // Too much buffered: the input runs fast against the output, so make
    // fewer output frames per input frame.
    const double error = mean - target;
    integral = qBound(-maxIntegralAdjust / ki, integral + error, maxIntegralAdjust / ki);
    const double adjust = qBound(-maxAdjust, kp * error + ki * integral, maxAdjust);
    currentRatio = 1.0 - adjust;
    return currentRatio;
}

// ---------------------------------------------------------------------------

int txAudioPacer::bytesPerFrame(quint8 codec)
{
    switch (codec) {
    case 0x01: // uLaw 1ch
    case 0x02: // PCM 1ch 8 bit
        return 1;
    case 0x04: // PCM 1ch 16 bit
    case 0x08: // PCM 2ch 8 bit
    case 0x20: // uLaw 2ch
        return 2;
    case 0x10: // PCM 2ch 16 bit
        return 4;
    default:   // Opus and other encoded formats
        return 0;
    }
}

txAudioPacer::txAudioPacer(quint8 codec, quint32 sampleRate, int maxPacketBytes, SendFn send, QObject *parent)
    : QObject(parent), send(std::move(send)), codec(codec), rate(sampleRate)
{
    frameBytes = bytesPerFrame(codec);
    bytesPerSecond = int(sampleRate) * frameBytes;
    if (frameBytes <= 0 || bytesPerSecond <= 0 || maxPacketBytes < frameBytes)
        return; // disabled

    // 20 ms of audio per packet, or as much as the protocol allows.
    packetSize = qMin(maxPacketBytes, bytesPerSecond / 50);
    packetSize -= packetSize % frameBytes;
    packetNs = qint64(packetSize) * 1000000000ll / bytesPerSecond;
    prebufferBytes = minPrebufferPackets * packetSize;
    fadeFrames = int(qint64(sampleRate) * txAudioFade::fadeMs / 1000);

    // 16-bit PCM can follow the radio's clock exactly; other codecs are sent
    // on this computer's clock.
    if (codec == 0x04 || codec == 0x10) {
        channels = codec == 0x10 ? 2 : 1;
        resampler = std::make_unique<txFineResampler>(channels);
    }

    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(1);
    connect(timer, &QTimer::timeout, this, &txAudioPacer::tick);
    timer->start();

    qInfo(logAudio()) << "TX audio paced:" << packetSize << "byte packets every" << packetNs / 1000000.0 << "ms"
                      << (resampler ? "(follows the radio's clock)" : "");
}

QByteArray txAudioPacer::resample(const QByteArray &in)
{
    return resampler ? resampler->process(in) : in;
}

void txAudioPacer::push(const QByteArray &audio)
{
    if (audio.isEmpty())
        return;
    if (!enabled()) {
        send(audio);
        return;
    }

    // Prebuffer enough to cover the gap between bursts: one and a half times
    // the largest burst seen, within limits.
    if (audio.size() > largestChunk) {
        largestChunk = int(audio.size());
        const int maxPre = int(qint64(bytesPerSecond) * maxPrebufferMs / 1000);
        const int wanted = qBound(minPrebufferPackets * packetSize, largestChunk * 3 / 2, maxPre);
        prebufferBytes = (wanted + packetSize - 1) / packetSize * packetSize;
    }

    if (fadeInNext && resampler)
        resampler->reset(); // a new stream
    const QByteArray chunk = resample(audio);

    const int start = int(pending.size());
    pending.append(chunk);
    lastArrival.start();

    // Audio that follows silence starts wherever its waveform happens to be.
    if (fadeInNext) {
        const int frames = qMin(fadeFrames, int(chunk.size()) / frameBytes);
        txAudioFade::codecFrames(pending.data() + start, frames, codec, true);
        fadeInNext = false;
    }

    const int maxBytes = int(qint64(bytesPerSecond) * maxBufferMs / 1000);
    if (pending.size() > maxBytes) {
        const int drop = (int(pending.size()) - maxBytes + packetSize - 1) / packetSize * packetSize;
        pending.remove(0, drop);
        qDebug(logAudio()) << "TX audio pacer over" << maxBufferMs << "ms, dropped" << drop << "bytes";
    }
    reportBusy();
}

int txAudioPacer::endOfInputWaitMs() const
{
    if (bytesPerSecond <= 0)
        return endOfInputMs;
    return qMax(endOfInputMs, int(qint64(largestChunk) * 1500 / bytesPerSecond));
}

void txAudioPacer::sendNext(int bytes)
{
    const QByteArray payload = pending.left(bytes);
    pending.remove(0, bytes);
    send(payload);
}

void txAudioPacer::finishTail()
{
    // The audio ran out part way through a packet. Fade out what is left and
    // pad it with silence to a whole packet, so the radio hears the audio end
    // rather than a cut; audio that comes later is faded in.
    if (resampler) {
        // The resampler still holds the last half millisecond: push it out.
        pending.append(resampler->flush());
    }
    const int frames = int(pending.size()) / frameBytes;
    pending.truncate(frames * frameBytes);
    const int n = qMin(fadeFrames, frames);
    txAudioFade::codecFrames(pending.data() + (frames - n) * frameBytes, n, codec, false);
    const int partial = int(pending.size()) % packetSize;
    if (partial)
        pending.append(txAudioFade::silence(codec, (packetSize - partial) / frameBytes));
    fadeInNext = true;
}

void txAudioPacer::logStreamEnd()
{
    if (streamPackets < 50)
        return;
    qDebug(logAudio()) << "TX audio stream ended:" << streamPackets << "packets,"
                       << (radioClock && radioClock->locked()
                               ? QStringLiteral("timed by the radio's clock (%1 ppm)").arg(radioClock->ppm(), 0, 'f', 1)
                               : QStringLiteral("timed by this computer's clock"))
                       << (resampler ? QStringLiteral("resampled %1 ppm").arg((drift.ratio() - 1.0) * 1e6, 0, 'f', 1)
                                     : QString());
}

void txAudioPacer::tick()
{
    const bool inputStopped = lastArrival.isValid() && lastArrival.elapsed() > endOfInputWaitMs();

    if (priming) {
        if (pending.isEmpty())
            return;
        if (pending.size() < prebufferBytes) {
            if (!inputStopped)
                return;
            // A transmission shorter than the prebuffer: send it now.
            finishTail();
        }
        priming = false;
        clock.start();
        nextSendNs = 0;
        streamPackets = 0;
        if (radioClock && radioClock->locked() != clockWasLocked) {
            clockWasLocked = radioClock->locked();
            drift.clockChanged();
        }
        drift.startStream();
        driftTimer.start();
    }

    // Each packet is due one packet's worth of the radio's time after the
    // last, so the radio's buffer neither fills nor drains. The first packet
    // goes out at once.
    const double period = double(packetNs) * (radioClock ? radioClock->ratio() : 1.0);
    double now = double(clock.nsecsElapsed());
    if (now - nextSendNs > 3 * period)
        nextSendNs = now - 2 * period; // held up: send a few now, not the whole backlog

    // Send on time to a small fraction of a millisecond. The radio plays
    // from its buffer on a 20 ms cycle with a sharp edge in it, at a point
    // that changes from one connection to the next; packets that arrive
    // within a millisecond of it, some just before and some just after, make
    // it skip back and forth for as long as the transmission lasts (bursts of
    // clicks). Waiting for the timer alone would leave them up to 1 ms late.
    if (nextSendNs > now && nextSendNs - now <= preciseWindowNs && pending.size() >= packetSize)
        now = waitUntil(nextSendNs);

    bool sent = false;
    while (now >= nextSendNs) {
        if (pending.size() < packetSize) {
            if (!pending.isEmpty()) {
                // Ran out mid-packet: send the rest, faded out, on time.
                finishTail();
                continue;
            }
            // Ran dry: the transmission ended, or the input fell behind.
            // Collect the prebuffer again; the next audio is faded in.
            if (!inputStopped) {
                ++underruns;
                qDebug(logAudio()) << "TX audio pacer underrun; prebuffer" << prebufferBytes
                                   << "bytes, underruns" << underruns;
            }
            logStreamEnd();
            priming = true;
            fadeInNext = true;
            break;
        }
        sendNext(packetSize);
        nextSendNs += period;
        ++streamPackets;
        sent = true;
        drift.level(double(pending.size()) / double(bytesPerSecond));
    }
    if (sent)
        reportBusy();

    // Follow the difference between the input's clock and the output's.
    if (resampler && !priming && driftTimer.isValid() && driftTimer.elapsed() >= 1000) {
        driftTimer.restart();
        resampler->setRatio(drift.update());
    }
}

double txAudioPacer::waitUntil(double clockNs)
{
#if !defined(Q_OS_WIN)
    // Sleeping wakes within a few tens of microseconds here.
    const double remain = clockNs - double(clock.nsecsElapsed());
    if (remain > spinNs)
        std::this_thread::sleep_for(std::chrono::nanoseconds(qint64(remain - spinNs)));
#else
    // Sleeps can run a millisecond over: sleep in steps, spin the last 2 ms.
    while (clockNs - double(clock.nsecsElapsed()) > 2.5e6)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    double now = double(clock.nsecsElapsed());
    while (now < clockNs)
        now = double(clock.nsecsElapsed());
    if (radioClock)
        radioClock->busyUntil(radioAudioClock::steadyNs());
    return now;
}

void txAudioPacer::reportBusy()
{
    const qint64 now = txAudioDrain::nowMs();
    qint64 last = now; // nothing held: the last audio went out now
    if (!pending.isEmpty()) {
        const double period = double(packetNs) * (radioClock ? radioClock->ratio() : 1.0);
        const qint64 packets = (pending.size() + packetSize - 1) / packetSize;
        qint64 first;
        if (priming) // starts once the prebuffer fills, or the input stops
            first = now + qMax<qint64>(0, endOfInputWaitMs() + 1 - lastArrival.elapsed());
        else
            first = now + qMax<qint64>(0, qint64((nextSendNs - double(clock.nsecsElapsed())) / 1e6));
        last = first + qint64(double(packets - 1) * period / 1e6);
    }
    txAudioDrain::report(txAudioDrain::Network, last);
}

// ---------------------------------------------------------------------------

float txAudioFade::gain(int i, int n)
{
    if (n <= 0)
        return 1.0f;
    constexpr float pi = 3.14159265358979f;
    return 0.5f - 0.5f * std::cos(pi * (float(i) + 0.5f) / float(n));
}

void txAudioFade::floatFrames(float *samples, int frames, int channels, bool fadeIn)
{
    if (!samples || frames <= 0 || channels <= 0)
        return;
    for (int i = 0; i < frames; ++i) {
        const float g = fadeIn ? gain(i, frames) : gain(frames - 1 - i, frames);
        for (int c = 0; c < channels; ++c)
            samples[i * channels + c] *= g;
    }
}

void txAudioFade::codecFrames(char *data, int frames, quint8 codec, bool fadeIn)
{
    if (!data || frames <= 0)
        return;
    switch (codec) {
    case 0x04:   // PCM 16 bit
    case 0x10: {
        const int channels = codec == 0x10 ? 2 : 1;
        auto *s = reinterpret_cast<qint16 *>(data);
        for (int i = 0; i < frames; ++i) {
            const float g = fadeIn ? gain(i, frames) : gain(frames - 1 - i, frames);
            for (int c = 0; c < channels; ++c) {
                qint16 &v = s[i * channels + c];
                v = qint16(std::lrintf(float(v) * g));
            }
        }
        break;
    }
    case 0x02:   // PCM 8 bit, unsigned
    case 0x08: {
        const int channels = codec == 0x08 ? 2 : 1;
        auto *s = reinterpret_cast<quint8 *>(data);
        for (int i = 0; i < frames; ++i) {
            const float g = fadeIn ? gain(i, frames) : gain(frames - 1 - i, frames);
            for (int c = 0; c < channels; ++c) {
                quint8 &v = s[i * channels + c];
                v = quint8(128 + std::lrintf(float(int(v) - 128) * g));
            }
        }
        break;
    }
    default:     // uLaw and encoded formats: left as they are
        break;
    }
}

QByteArray txAudioFade::silence(quint8 codec, int frames)
{
    const int bytes = qMax(0, frames) * txAudioPacer::bytesPerFrame(codec);
    switch (codec) {
    case 0x02:
    case 0x08:
        return QByteArray(bytes, char(0x80));
    case 0x01:
    case 0x20:
        return QByteArray(bytes, char(0xFF)); // uLaw zero
    default:
        return QByteArray(bytes, char(0));
    }
}

// ---------------------------------------------------------------------------

namespace {
std::atomic<qint64> lastSend[txAudioDrain::StageCount] = {};
}

qint64 txAudioDrain::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void txAudioDrain::report(Stage stage, qint64 lastSendMs)
{
    if (stage >= 0 && stage < StageCount)
        lastSend[stage].store(lastSendMs, std::memory_order_relaxed);
}

qint64 txAudioDrain::lastSendMs()
{
    qint64 latest = 0;
    for (const auto &s : lastSend)
        latest = qMax(latest, s.load(std::memory_order_relaxed));
    return latest;
}

// ---------------------------------------------------------------------------

txUnkeyWaiter::txUnkeyWaiter(QObject *parent) : QObject(parent)
{
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(5);
    connect(timer, &QTimer::timeout, this, &txUnkeyWaiter::check);
}

void txUnkeyWaiter::start(int radioBufferMs)
{
    this->radioBufferMs = qBound(0, radioBufferMs, maxWaitMs);
    since.start();
    timer->start();
}

void txUnkeyWaiter::cancel()
{
    timer->stop();
}

void txUnkeyWaiter::check()
{
    // Audio still held by one stage reaches the next within a few ms, and
    // its later send time then counts too.
    const qint64 sinceLastSend = txAudioDrain::nowMs() - txAudioDrain::lastSendMs();
    if (sinceLastSend < radioBufferMs && since.elapsed() < maxWaitMs)
        return;
    timer->stop();
    qDebug(logAudio()) << "PTT off after" << since.elapsed() << "ms waiting for TX audio";
    emit ready();
}
