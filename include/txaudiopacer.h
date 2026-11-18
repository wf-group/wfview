#ifndef TXAUDIOPACER_H
#define TXAUDIOPACER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

// Resamples 16-bit PCM by a ratio within a fraction of a percent of 1, which
// may change at any moment without a glitch: windowed-sinc interpolation
// with 16 taps, so it holds back only 8 frames (0.5 ms at 16 kHz). Speex's
// resampler rebuilds its filter, at a different length, whenever the ratio
// crosses 1, which corrupts its output mid-stream.
class txFineResampler
{
public:
    explicit txFineResampler(int channels);

    // Output frames per input frame, kept within 1 +/- 1%.
    void setRatio(double outPerIn);
    double ratio() const { return 1.0 / step; }
    // Forget the audio held: the next input starts a new stream.
    void reset();
    // Interleaved 16-bit PCM in and out.
    QByteArray process(const QByteArray &in);
    // The input frames still held back, at the end of a stream.
    QByteArray flush();

    static constexpr int taps = 16;
    static constexpr int phases = 256;

private:
    int channels;
    double step = 1.0;          // input frames per output frame
    double pos = 0;             // where the next output falls, in history frames
    std::vector<float> history; // interleaved input not yet used up
};

// Measures a radio's audio clock against this computer's, from the audio
// packets the radio sends: how much audio each holds and when it arrives.
//
// A radio plays network TX audio on its own clock. Sent on this computer's
// clock, which can differ by 20 ppm or, while time sync is steering it, 200
// ppm, the radio's buffer slowly fills or drains until it skips or repeats a
// whole packet; while the drift crosses the packet boundary it does so back
// and forth for seconds (heard as a burst of clicks). Timing TX packets by the
// radio's own clock avoids that. The radio's receive (and monitor) audio
// comes on that clock, and keeps coming while it transmits.
class radioAudioClock
{
public:
    // A packet of the radio's audio arrived now (steady clock, ns).
    void packet(int frames, quint32 sampleRate, qint64 nowNs);
    void packet(int frames, quint32 sampleRate);
    // The thread that receives the packets was busy until then (the pacer
    // waiting to send on time). Packets handled just after it may have
    // arrived during it: their audio counts, their arrival time does not.
    void busyUntil(qint64 nowNs) { busyEndNs = nowNs; }
    static qint64 steadyNs();
    static constexpr qint64 afterBusyNs = 300000;

    bool locked() const { return valid; }
    // This computer's time per unit of the radio's audio: above 1 when the
    // radio's clock runs slow. 1 until locked.
    double ratio() const { return valid ? currentRatio : 1.0; }
    double ppm() const { return (ratio() - 1.0) * 1e6; }

    // The earliest arrival in each second carries the least network delay.
    static constexpr int bucketMs = 1000;
    static constexpr int minBuckets = 10;   // seconds before the first estimate
    static constexpr int maxBuckets = 60;   // seconds the estimate covers
    static constexpr int gapMs = 250;       // a longer gap starts a new series
    static constexpr int stepUs = 5000;     // a jump this big (lost audio) too
    static constexpr double maxPpm = 2000;  // estimates beyond this are ignored

private:
    void restart();
    void closeBucket();

    struct Point { double audioNs; double offsetNs; };
    std::deque<Point> points;
    bool started = false;
    bool valid = false;
    double currentRatio = 1.0;
    qint64 startNs = 0;
    qint64 lastNs = 0;
    quint32 lastRate = 0;
    double audioNs = 0;
    qint64 bucket = -1;
    Point bucketMin {0, 0};
    bool bucketHas = false;
    qint64 busyEndNs = 0;
};

// Keeps the pacer's buffer level steady when its input and output run on
// different clocks, by setting a resampling ratio (output frames per input
// frame) close to 1. A slow PI loop on the buffer level, measured once a
// second: the input source (TCI on this computer's clock, a sound card on its
// own) and the radio can be a few hundred ppm apart.
class txDriftControl
{
public:
    // Call when a stream starts. Its first seconds, while the input and any
    // pacing before this one settle, only set the target level.
    void startStream();
    // The output clock changed (the radio's clock was found): what was
    // learnt about the difference no longer applies.
    void clockChanged();
    // Buffer level after each packet sent, in seconds of audio.
    void level(double seconds);
    // Once a second: returns the new ratio.
    double update();
    double ratio() const { return currentRatio; }

    static constexpr double kp = 0.02;      // per second of level error
    static constexpr double ki = 0.0004;    // per second of error per second
    static constexpr double maxAdjust = 0.002;
    // Clocks differ by a few hundred ppm at most; the integral (the learnt
    // difference) is kept within this, so a bad start cannot wind it up.
    static constexpr double maxIntegralAdjust = 0.0005;
    static constexpr int settleSeconds = 2;

private:
    double target = -1;
    double sum = 0;
    int count = 0;
    int seconds = 0;
    double integral = 0;    // kept between streams: the clocks stay apart
    double currentRatio = 1.0;
};

// Sends network TX audio to a radio at a steady rate.
//
// Audio inputs deliver audio in bursts (Qt about every 100 ms, PortAudio with
// jitter), and a LAN radio expects one short datagram at a time at the audio
// rate. Fed in bursts, the TS-890S and IC-7610 both distort their transmitted
// audio (heard as clicks). The pacer queues whatever arrives and passes it to
// the sender one packet at a time on a steady clock, after a prebuffer sized
// from the largest burst seen.
//
// Where the audio starts after silence it is faded in, and where it runs out
// the last of it is faded out and padded to a whole packet, so a stream that
// starts or stops mid-waveform does not click (see txAudioFade).
//
// Create it, and call push(), in the thread that sends the packets; the timer
// and the send callback run there too.
class txAudioPacer : public QObject
{
    Q_OBJECT

public:
    using SendFn = std::function<void(const QByteArray &payload)>;

    // codec is a wfview codec number; maxPacketBytes is the most audio one
    // datagram of this protocol carries. Codecs the pacer cannot time (Opus)
    // leave it disabled: check enabled() and send directly instead.
    txAudioPacer(quint8 codec, quint32 sampleRate, int maxPacketBytes, SendFn send, QObject *parent = nullptr);

    bool enabled() const { return packetNs > 0; }
    int packetBytes() const { return packetSize; }

    void push(const QByteArray &audio);

    // Time packets by this radio clock (owned by the caller, same thread)
    // once it is locked; until then, and without one, by this computer's.
    void setRadioClock(radioAudioClock *clock) { radioClock = clock; }

    // Bytes per frame for wfview's PCM and uLaw codecs, 0 for others.
    static int bytesPerFrame(quint8 codec);

    static constexpr int minPrebufferPackets = 3;
    static constexpr int maxPrebufferMs = 200;
    static constexpr int maxBufferMs = 500;
    // Input idle this long means the transmission ended: send what is left.
    // At least one and a half times the longest chunk seen, since a sound
    // card delivers about 100 ms at a time.
    static constexpr int endOfInputMs = 60;
    int endOfInputWaitMs() const;

    // The timer ticks about once a millisecond, but ticks can come 2 ms or
    // more apart. A packet due within this long is sent at its exact time:
    // the pacer sleeps, then spins the last part.
    static constexpr double preciseWindowNs = 4e6;
    static constexpr double spinNs = 500e3; // sleeps can wake a few hundred us late

private slots:
    void tick();

private:
    void sendNext(int bytes);
    void finishTail();
    void reportBusy();
    QByteArray resample(const QByteArray &in);
    double waitUntil(double clockNs);
    void logStreamEnd();

    SendFn send;
    QTimer *timer = nullptr;
    QElapsedTimer clock;
    QElapsedTimer lastArrival;
    QByteArray pending;
    double nextSendNs = 0;  // on clock
    qint64 packetNs = 0;
    int packetSize = 0;
    int frameBytes = 0;
    int bytesPerSecond = 0;
    int prebufferBytes = 0;
    int largestChunk = 0;
    int fadeFrames = 0;
    quint8 codec = 0;
    bool priming = true;
    bool fadeInNext = true; // the next audio follows silence
    quint64 underruns = 0;
    quint64 streamPackets = 0;

    radioAudioClock *radioClock = nullptr;
    // 16-bit PCM only: a slight resample to follow the radio's clock.
    std::unique_ptr<txFineResampler> resampler;
    int channels = 1;
    quint32 rate = 0;
    txDriftControl drift;
    bool clockWasLocked = false;
    QElapsedTimer driftTimer;
};

// Short raised-cosine fades for TX audio that starts or stops mid-waveform.
namespace txAudioFade {
    constexpr int fadeMs = 5;
    // Gain for frame i of an n-frame fade in (1 - gain for a fade out).
    float gain(int i, int n);
    // Fades the first (fadeIn) or last frames of interleaved float audio.
    void floatFrames(float *samples, int frames, int channels, bool fadeIn);
    // The same for wfview's linear PCM codecs. uLaw and others are left alone.
    void codecFrames(char *data, int frames, quint8 codec, bool fadeIn);
    // Frames of silence in a codec, for padding.
    QByteArray silence(quint8 codec, int frames);
}

// When the TX audio wfview still holds will have been sent to the radio.
// Each pacing stage reports the time its last buffered audio goes out, so PTT
// off can wait for it instead of cutting the end of a transmission off.
namespace txAudioDrain {
    enum Stage { TciInput, Network, StageCount };
    qint64 nowMs();                     // steady clock, ms
    void report(Stage stage, qint64 lastSendMs);
    qint64 lastSendMs();                // the latest of all stages
}

// Holds PTT off back until wfview has sent the TX audio it still holds and the
// radio has had time to play it (radioBufferMs, from the rig file's
// TxAudioBufferMs), then emits ready(). Returns at once when no audio is in
// flight. Lives in the thread that sends PTT off.
class txUnkeyWaiter : public QObject
{
    Q_OBJECT

public:
    explicit txUnkeyWaiter(QObject *parent = nullptr);

    void start(int radioBufferMs = defaultRadioBufferMs);
    void cancel();
    bool waiting() const { return timer->isActive(); }

    // Time for the radio to play the audio wfview sent last (its own buffer
    // and the network), unless the rig file says otherwise.
    static constexpr int defaultRadioBufferMs = 100;
    // Unkey after this long whatever the audio is doing.
    static constexpr int maxWaitMs = 1000;

signals:
    void ready();

private:
    void check();

    QTimer *timer = nullptr;
    QElapsedTimer since;
    int radioBufferMs = defaultRadioBufferMs;
};

#endif // TXAUDIOPACER_H
