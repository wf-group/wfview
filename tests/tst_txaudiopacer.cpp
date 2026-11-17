#include "txaudiopacer.h"

#include <QElapsedTimer>
#include <cmath>
#include <QSignalSpy>
#include <QtTest>

// The TX audio pacer sends network audio on a steady clock, fades audio in
// where it starts after silence and out where it runs out, and reports when
// the last of it goes out so PTT off can wait for it.
class TxAudioPacerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void fadesShortTransmission();
    void fadesInAfterSilence();
    void pacesPackets();
    void unkeyIsImmediateWhenIdle();
    void unkeyWaitsForAudio();
    void unkeyGivesUp();
    void silencePerCodec();
    void measuresRadioClock();
    void radioClockSurvivesLostPacket();
    void radioClockIgnoresDelayedPackets();
    void driftControlHoldsLevel();
    void driftControlDoesNotWindUp();
    void resampledToneIsContinuous();
    void resamplerFollowsChangingRatio();
    void longStreamStaysClean();

private:
    struct Sent {
        QByteArray data;
        qint64 ms;
    };
    static QByteArray constantPcm16(int frames, qint16 value);
    static QVector<qint16> samples(const QVector<Sent> &packets);
};

static constexpr int rate = 16000;
static constexpr int packetBytes = 640; // 20 ms at 16 kHz mono 16 bit
static constexpr int fadeFrames = rate * txAudioFade::fadeMs / 1000;

QByteArray TxAudioPacerTest::constantPcm16(int frames, qint16 value)
{
    QByteArray out(frames * 2, Qt::Uninitialized);
    auto *s = reinterpret_cast<qint16 *>(out.data());
    for (int i = 0; i < frames; ++i)
        s[i] = value;
    return out;
}

QVector<qint16> TxAudioPacerTest::samples(const QVector<Sent> &packets)
{
    QVector<qint16> out;
    for (const Sent &p : packets) {
        const auto *s = reinterpret_cast<const qint16 *>(p.data.constData());
        for (int i = 0; i < p.data.size() / 2; ++i)
            out.append(s[i]);
    }
    return out;
}

void TxAudioPacerTest::init()
{
    for (int s = 0; s < txAudioDrain::StageCount; ++s)
        txAudioDrain::report(txAudioDrain::Stage(s), 0);
}

void TxAudioPacerTest::fadesShortTransmission()
{
    // 50 ms is shorter than the prebuffer: it goes out once the input stops,
    // faded in and out and padded to whole packets.
    QVector<Sent> sent;
    QElapsedTimer t;
    t.start();
    txAudioPacer pacer(0x04, rate, packetBytes, [&](const QByteArray &p) { sent.append({p, t.elapsed()}); });
    QVERIFY(pacer.enabled());
    const int frames = 800;
    pacer.push(constantPcm16(frames, 10000));
    QTRY_COMPARE_WITH_TIMEOUT(sent.size(), 3, 1000);
    QTest::qWait(100);
    QCOMPARE(sent.size(), 3);
    for (const Sent &p : sent)
        QCOMPARE(p.data.size(), packetBytes);

    const QVector<qint16> s = samples(sent);
    QVERIFY(qAbs(s[0]) < 100);                 // faded in
    QCOMPARE(s[fadeFrames], qint16(10000));     // full level after the fade
    QCOMPARE(s[frames - fadeFrames - 1], qint16(10000));
    QVERIFY(qAbs(s[frames - 1]) < 100);         // faded out
    for (int i = frames; i < s.size(); ++i)     // padded with silence
        QCOMPARE(s[i], qint16(0));
    // Monotonic fades, no steps.
    for (int i = 1; i < fadeFrames; ++i) {
        QVERIFY(s[i] >= s[i - 1]);
        QVERIFY(s[frames - i] <= s[frames - i - 1]);
    }
}

void TxAudioPacerTest::fadesInAfterSilence()
{
    QVector<Sent> sent;
    QElapsedTimer t;
    t.start();
    txAudioPacer pacer(0x04, rate, packetBytes, [&](const QByteArray &p) { sent.append({p, t.elapsed()}); });
    // 100 ms: five packets, and the resampler's last few samples padded
    // into a sixth.
    pacer.push(constantPcm16(1600, 8000));
    QTRY_VERIFY_WITH_TIMEOUT(sent.size() >= 5, 1000);
    QTest::qWait(150); // the input stops and the pacer runs dry
    sent.clear();

    pacer.push(constantPcm16(1600, 8000));
    QTRY_VERIFY_WITH_TIMEOUT(sent.size() >= 5, 1000);
    const QVector<qint16> s = samples(sent);
    QVERIFY(qAbs(s[0]) < 100);
    QCOMPARE(s[fadeFrames], qint16(8000));
}

void TxAudioPacerTest::pacesPackets()
{
    // A 200 ms burst goes out one packet every 20 ms, not all at once.
    QVector<Sent> sent;
    QElapsedTimer t;
    t.start();
    txAudioPacer pacer(0x04, rate, packetBytes, [&](const QByteArray &p) { sent.append({p, t.elapsed()}); });
    pacer.push(constantPcm16(3200, 1000));
    QTRY_VERIFY_WITH_TIMEOUT(sent.size() >= 10, 2000);
    const qint64 span = sent[9].ms - sent[0].ms;
    QVERIFY2(span >= 160 && span <= 260, qPrintable(QString::number(span)));

    // Each packet leaves close to its exact time, not up to a millisecond
    // late on the timer's tick.
    QVector<qint64> sentNs;
    QElapsedTimer ns;
    ns.start();
    txAudioPacer precise(0x04, rate, packetBytes, [&](const QByteArray &) { sentNs.append(ns.nsecsElapsed()); });
    precise.push(constantPcm16(3200, 1000));
    // A real event loop: QTest::qWait sleeps 10 ms between rounds, so the
    // pacer's 1 ms timer would only tick every 10 ms.
    QEventLoop loop;
    QTimer::singleShot(800, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(sentNs.size() >= 10);
    int close = 0;
    QStringList devs;
    for (int i = 1; i < 10; ++i) {
        const qint64 dev = sentNs[i] - sentNs[0] - i * 20000000ll;
        devs << QString::number(dev / 1000.0, 'f', 0);
        if (qAbs(dev) < 200000)
            ++close;
    }
    qDebug() << "send time deviations (us):" << devs.join(' ');
    QVERIFY2(close >= 8, qPrintable(QString::number(close)));
}

void TxAudioPacerTest::unkeyIsImmediateWhenIdle()
{
    txUnkeyWaiter waiter;
    QSignalSpy ready(&waiter, &txUnkeyWaiter::ready);
    QElapsedTimer t;
    t.start();
    waiter.start();
    QVERIFY(waiter.waiting());
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 500);
    QVERIFY2(t.elapsed() < 100, qPrintable(QString::number(t.elapsed())));
    QVERIFY(!waiter.waiting());
}

void TxAudioPacerTest::unkeyWaitsForAudio()
{
    // The last audio goes out 200 ms from now: PTT off follows it by the
    // radio's buffer time.
    txAudioDrain::report(txAudioDrain::Network, txAudioDrain::nowMs() + 200);
    txUnkeyWaiter waiter;
    QSignalSpy ready(&waiter, &txUnkeyWaiter::ready);
    QElapsedTimer t;
    t.start();
    waiter.start();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 2000);
    const qint64 waited = t.elapsed();
    QVERIFY2(waited >= 200 + txUnkeyWaiter::defaultRadioBufferMs - 5 && waited < 500, qPrintable(QString::number(waited)));

    // A radio with a longer buffer (rig file TxAudioBufferMs) waits longer.
    txAudioDrain::report(txAudioDrain::Network, txAudioDrain::nowMs());
    t.restart();
    waiter.start(300);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 2000);
    QVERIFY2(t.elapsed() >= 295 && t.elapsed() < 500, qPrintable(QString::number(t.elapsed())));

    // Cancelled (keyed again), it does not fire.
    txAudioDrain::report(txAudioDrain::TciInput, txAudioDrain::nowMs() + 100);
    waiter.start();
    waiter.cancel();
    QTest::qWait(300);
    QCOMPARE(ready.count(), 2);
}

void TxAudioPacerTest::unkeyGivesUp()
{
    // Audio that never stops does not hold the radio keyed.
    txAudioDrain::report(txAudioDrain::Network, txAudioDrain::nowMs() + 60000);
    txUnkeyWaiter waiter;
    QSignalSpy ready(&waiter, &txUnkeyWaiter::ready);
    QElapsedTimer t;
    t.start();
    waiter.start();
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, txUnkeyWaiter::maxWaitMs + 1000);
    QVERIFY(t.elapsed() >= txUnkeyWaiter::maxWaitMs - 5);
}

void TxAudioPacerTest::silencePerCodec()
{
    QCOMPARE(txAudioFade::silence(0x04, 3), QByteArray(6, char(0)));
    QCOMPARE(txAudioFade::silence(0x10, 2), QByteArray(8, char(0)));
    QCOMPARE(txAudioFade::silence(0x02, 3), QByteArray(3, char(0x80)));
    QCOMPARE(txAudioFade::silence(0x01, 3), QByteArray(3, char(0xFF)));

    // 8-bit PCM fades towards its midpoint.
    QByteArray u8(40, char(228)); // +100
    txAudioFade::codecFrames(u8.data(), 40, 0x02, true);
    QVERIFY(qAbs(int(quint8(u8[0])) - 128) <= 1);
    QCOMPARE(int(quint8(u8[39])), 228 - 0); // last frame of the fade is near full
}

void TxAudioPacerTest::measuresRadioClock()
{
    // A radio whose clock runs 170 ppm slow sends 20 ms packets every
    // 20.0034 ms, each up to 1 ms late on the network.
    radioAudioClock clock;
    QVERIFY(!clock.locked());
    QCOMPARE(clock.ratio(), 1.0);
    quint32 seed = 1;
    auto jitter = [&seed]() { seed = seed * 1664525u + 1013904223u; return qint64(seed >> 12) % 1000000; };
    const double period = 20e6 * (1.0 + 170e-6);
    for (int i = 0; i < 50 * 40; ++i)
        clock.packet(320, 16000, qint64(1e9 + i * period) + jitter());
    QVERIFY(clock.locked());
    QVERIFY2(qAbs(clock.ppm() - 170.0) < 5.0, qPrintable(QString::number(clock.ppm())));
}

void TxAudioPacerTest::radioClockSurvivesLostPacket()
{
    radioAudioClock clock;
    const double period = 20e6 * (1.0 - 50e-6);
    qint64 t = 0;
    for (int i = 0; i < 50 * 30; ++i) {
        t = qint64(1e9 + i * period);
        if (i == 50 * 15)
            continue; // lost on the network
        clock.packet(320, 16000, t);
    }
    QVERIFY(clock.locked());
    QVERIFY2(qAbs(clock.ppm() + 50.0) < 5.0, qPrintable(QString::number(clock.ppm())));
    // A pause in the radio's audio keeps the estimate.
    for (int i = 0; i < 50 * 5; ++i)
        clock.packet(320, 16000, t + qint64(5e9 + i * period));
    QVERIFY(clock.locked());
    QVERIFY2(qAbs(clock.ppm() + 50.0) < 5.0, qPrintable(QString::number(clock.ppm())));
}

void TxAudioPacerTest::radioClockIgnoresDelayedPackets()
{
    // The radio runs 7 ppm slow. While transmitting, the receiving thread
    // waits up to 4 ms before each send, on a cycle drifting against the
    // radio's: packets that arrive during a wait are handled at its end.
    radioAudioClock clock;
    const double radioPeriod = 20e6 * (1.0 + 7e-6);
    const double sendPeriod = 20e6 * (1.0 - 30e-6);
    double nextSend = 1e9 + 3e6;
    for (int i = 0; i < 50 * 90; ++i) {
        const qint64 arrival = qint64(1e9 + i * radioPeriod);
        qint64 handled = arrival;
        const bool transmitting = i > 50 * 20;
        if (transmitting) {
            while (nextSend < arrival - 4e6)
                nextSend += sendPeriod;
            if (arrival > nextSend - 4e6 && arrival < nextSend) {
                handled = qint64(nextSend);   // held up by the wait
                clock.busyUntil(handled);
            }
        }
        clock.packet(320, 16000, handled);
    }
    QVERIFY(clock.locked());
    QVERIFY2(qAbs(clock.ppm() - 7.0) < 3.0, qPrintable(QString::number(clock.ppm())));
}

void TxAudioPacerTest::driftControlHoldsLevel()
{
    // Input 300 ppm fast against the output: without control the buffer would
    // grow by 0.3 ms a second, 54 ms over three minutes.
    txDriftControl drift;
    drift.startStream();
    double level = 0.060;
    double target = 0;
    double worst = 0;
    for (int second = 0; second < 600; ++second) {
        for (int p = 0; p < 50; ++p) {
            level += (0.02 * (1.0 + 300e-6) * drift.ratio() - 0.02);
            drift.level(level);
        }
        drift.update();
        if (second == txDriftControl::settleSeconds)
            target = level; // about where it took its target
        if (second > 10)
            worst = qMax(worst, qAbs(level - target));
    }
    // A slow loop, so that noise in the level does not wobble the pitch: it
    // lets the level wander a little before the integral takes over.
    QVERIFY2(worst < 0.020, qPrintable(QString::number(worst)));
    QVERIFY2(qAbs(level - target) < 0.001, qPrintable(QString::number(level - target)));
    QVERIFY2(qAbs(drift.ratio() * (1.0 + 300e-6) - 1.0) < 20e-6, qPrintable(QString::number(drift.ratio(), 'g', 10)));
}

void TxAudioPacerTest::driftControlDoesNotWindUp()
{
    // The first seconds of a stream swing by 100 ms while what feeds the
    // pacer settles; the clocks are the same. That must not leave a lasting
    // correction, as it did on the IC-7610 (-1550 ppm after two short
    // transmissions).
    txDriftControl drift;
    for (int stream = 0; stream < 3; ++stream) {
        drift.startStream();
        double level = 0.060;
        for (int second = 0; second < 5; ++second) {
            for (int p = 0; p < 50; ++p) {
                const double swing = second < 2 ? 0.1 * ((p / 10) % 2) : 0.0;
                level += 0.02 * drift.ratio() - 0.02;
                drift.level(level + swing);
            }
            drift.update();
        }
    }
    QVERIFY2(qAbs(drift.ratio() - 1.0) < 100e-6, qPrintable(QString::number((drift.ratio() - 1.0) * 1e6)));

    // However wound up, a new clock starts from no correction.
    drift.clockChanged();
    QCOMPARE(drift.ratio(), 1.0);
    drift.startStream();
    QCOMPARE(drift.ratio(), 1.0);
}

void TxAudioPacerTest::resampledToneIsContinuous()
{
    // 1 kHz through the pacer: the output must be the same clean tone.
    QVector<Sent> sent;
    QElapsedTimer t;
    t.start();
    txAudioPacer pacer(0x04, rate, packetBytes, [&](const QByteArray &p) { sent.append({p, t.elapsed()}); });
    const int frames = 6400; // 400 ms, within the pacer's buffer limit
    QByteArray tone(frames * 2, Qt::Uninitialized);
    auto *s = reinterpret_cast<qint16 *>(tone.data());
    for (int i = 0; i < frames; ++i)
        s[i] = qint16(std::lrint(8000.0 * std::sin(2 * M_PI * 1000.0 * i / rate)));
    for (int i = 0; i < frames * 2; i += 3200)
        pacer.push(tone.mid(i, 3200));
    QTRY_VERIFY_WITH_TIMEOUT(sent.size() >= 20, 3000);
    const QVector<qint16> out = samples(sent);
    // Away from the fades, every sample is where a 1 kHz sine of the same
    // amplitude says it should be (phase fitted from the samples).
    double worst = 0;
    for (int i = 200; i < out.size() - 400; ++i) {
        const double predicted = 2 * std::cos(2 * M_PI * 1000.0 / rate) * out[i - 1] - out[i - 2];
        worst = qMax(worst, qAbs(predicted - out[i]));
    }
    QVERIFY2(worst < 60, qPrintable(QString::number(worst)));
}

void TxAudioPacerTest::resamplerFollowsChangingRatio()
{
    // The ratio changes every 50 ms, across 1 and back, while a 1 kHz tone
    // goes through. The output stays a clean tone at the same level, and its
    // length follows the ratios.
    txFineResampler rs(1);
    const double amp = 8000;
    double phase = 0, expectedFrames = 0;
    QByteArray out;
    const double ratios[] = {1.0, 1.002, 0.998, 1.0005, 0.9995, 1.01, 0.99};
    for (int block = 0; block < 140; ++block) {
        const double r = ratios[block % 7];
        rs.setRatio(r);
        QByteArray in(800 * 2, Qt::Uninitialized);
        auto *s = reinterpret_cast<qint16 *>(in.data());
        for (int i = 0; i < 800; ++i, phase += 2 * M_PI * 1000.0 / rate)
            s[i] = qint16(std::lrint(amp * std::sin(phase)));
        out.append(rs.process(in));
        expectedFrames += 800 * r;
    }
    out.append(rs.flush());
    const int n = int(out.size()) / 2;
    QVERIFY2(qAbs(n - expectedFrames) < 4, qPrintable(QString("%1 vs %2").arg(n).arg(expectedFrames)));
    const auto *o = reinterpret_cast<const qint16 *>(out.constData());
    double worstStep = 0, peak = 0;
    for (int i = 100; i < n - 100; ++i) {
        // A sine's second difference is -(2 - 2cos w) times the sample; with
        // a frequency up to 1% off from the ratio, a little more or less.
        const double second = double(o[i + 1]) - 2.0 * o[i] + o[i - 1];
        const double w = 2 * M_PI * 1000.0 / rate;
        worstStep = qMax(worstStep, qAbs(second + (2 - 2 * std::cos(w)) * o[i]));
        peak = qMax(peak, double(qAbs(o[i])));
    }
    QVERIFY2(qAbs(peak - amp) < amp * 0.01, qPrintable(QString::number(peak)));
    QVERIFY2(worstStep < amp * 0.01, qPrintable(QString::number(worstStep)));
}

void TxAudioPacerTest::longStreamStaysClean()
{
    // Four seconds in 100 ms chunks, as a sound card delivers it: the drift
    // control updates the resampler three times along the way.
    QVector<Sent> sent;
    QElapsedTimer t;
    t.start();
    txAudioPacer pacer(0x04, rate, packetBytes, [&](const QByteArray &p) { sent.append({p, t.elapsed()}); });
    const double amp = 8000;
    double phase = 0;
    for (int chunk = 0; chunk < 40; ++chunk) {
        QByteArray in(1600 * 2, Qt::Uninitialized);
        auto *s = reinterpret_cast<qint16 *>(in.data());
        for (int i = 0; i < 1600; ++i, phase += 2 * M_PI * 1000.0 / rate)
            s[i] = qint16(std::lrint(amp * std::sin(phase)));
        pacer.push(in);
        QTest::qWait(100);
    }
    QTest::qWait(400);
    const QVector<qint16> o = samples(sent);
    QVERIFY2(o.size() > 60000, qPrintable(QString::number(o.size())));
    const double w = 2 * M_PI * 1000.0 / rate;
    double worst = 0, peak = 0;
    int last = o.size() - 1;
    while (last > 0 && o[last] == 0)
        --last; // the padding after the faded end
    for (int i = 200; i < last - 200; ++i) {
        const double second = double(o[i + 1]) - 2.0 * o[i] + o[i - 1];
        const double e = qAbs(second + (2 - 2 * std::cos(w)) * o[i]);
        if (e > amp * 0.01)
            qWarning() << "glitch at frame" << i << "of" << last << "error" << e << "around" << o.mid(i - 3, 7);
        worst = qMax(worst, e);
        peak = qMax(peak, double(qAbs(o[i])));
    }
    QVERIFY2(qAbs(peak - amp) < amp * 0.01, qPrintable(QString::number(peak)));
    QVERIFY2(worst < amp * 0.01, qPrintable(QString::number(worst)));
}

int runTxAudioPacerTests(int argc, char **argv)
{
    TxAudioPacerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_txaudiopacer.moc"
