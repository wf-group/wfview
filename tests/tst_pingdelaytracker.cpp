#include <QtTest>

#include "pingdelaytracker.h"

#include <functional>

class PingDelayTrackerTest : public QObject
{
    Q_OBJECT

private slots:
    void firstPingIsZero();
    void steadyLinkStaysNearZero();
    void fastPeerClockDoesNotAccumulate();
    void slowPeerClockDoesNotAccumulate();
    void shortSpikeIsReportedThenRecovers();
    void sustainedShiftIsAbsorbed();
    void lateFirstPingIsCorrected();
    void peerClockStepBackIsAbsorbed();
    void peerClockStepForwardIsImmediate();
    void midnightWrapIsSeamless();
    void dropThreshold();
};

namespace
{
constexpr qint64 PingPeriodMs = 100;

// Simulates pings sent every 100 ms. The peer's clock starts at peerStartMs
// and runs fast by driftPpm (negative = slow). delayAt(t) gives the one-way
// network delay for a ping sent at local time t.
struct Link
{
    qint64 peerStartMs = 12 * 3600 * 1000;
    double driftPpm = 0.0;
    std::function<int(qint64)> delayAt = [](qint64) { return 30; };
    qint64 stepAtMs = -1;
    qint64 stepMs = 0;

    quint32 peerStamp(qint64 sendMs) const
    {
        qint64 stamp = peerStartMs + sendMs + qint64(sendMs * driftPpm / 1e6);
        if (stepAtMs >= 0 && sendMs >= stepAtMs)
            stamp += stepMs;
        stamp %= PingDelayTracker::DayMs;
        return quint32(stamp);
    }

    // Runs pings from fromMs to toMs and returns the largest excess seen in
    // [checkFromMs, toMs).
    int run(PingDelayTracker& t, qint64 fromMs, qint64 toMs, qint64 checkFromMs = 0) const
    {
        int worst = 0;
        for (qint64 send = fromMs; send < toMs; send += PingPeriodMs) {
            const int e = t.update(send + delayAt(send), peerStamp(send));
            if (send >= checkFromMs)
                worst = qMax(worst, e);
        }
        return worst;
    }
};

int jitter(qint64 sendMs)
{
    // Deterministic 30..50 ms delay.
    return 30 + int((sendMs / PingPeriodMs * 7919) % 21);
}
}

void PingDelayTrackerTest::firstPingIsZero()
{
    PingDelayTracker t;
    QVERIFY(!t.isValid());
    QCOMPARE(t.update(1000, 5000), 0);
    QVERIFY(t.isValid());
}

void PingDelayTrackerTest::steadyLinkStaysNearZero()
{
    PingDelayTracker t;
    Link link;
    link.delayAt = jitter;
    // Excess may never exceed the jitter range.
    QVERIFY(link.run(t, 0, 3600 * 1000) <= 20);
}

void PingDelayTrackerTest::fastPeerClockDoesNotAccumulate()
{
    // Old code: baseline never adapted, so error grew by 3.6 ms/min at 1000 ppm.
    for (const double ppm : {50.0, 200.0, 1000.0}) {
        PingDelayTracker t;
        Link link;
        link.driftPpm = ppm;
        link.delayAt = jitter;
        QVERIFY2(link.run(t, 0, 3600 * 1000) <= 20, qPrintable(QString::number(ppm)));
    }
}

void PingDelayTrackerTest::slowPeerClockDoesNotAccumulate()
{
    for (const double ppm : {-50.0, -200.0, -1000.0}) {
        PingDelayTracker t;
        Link link;
        link.driftPpm = ppm;
        link.delayAt = jitter;
        // At most two windows of drift (~20 s * 1000 ppm = ~20 ms) on top of jitter.
        QVERIFY2(link.run(t, 0, 3600 * 1000) <= 20 + 25, qPrintable(QString::number(ppm)));
    }
}

void PingDelayTrackerTest::shortSpikeIsReportedThenRecovers()
{
    // This is the case that latched the old server: one 300 ms spike.
    PingDelayTracker t;
    Link link;
    link.delayAt = [](qint64 send) { return (send >= 60000 && send < 63000) ? 330 : 30; };

    QCOMPARE(link.run(t, 0, 60000), 0);
    QVERIFY(link.run(t, 60000, 63000) >= 290);
    QCOMPARE(link.run(t, 63000, 600000), 0);
}

void PingDelayTrackerTest::sustainedShiftIsAbsorbed()
{
    // A route change adds 200 ms permanently. It is reported until the
    // baseline catches up (at most two windows), then excess returns to 0.
    PingDelayTracker t;
    Link link;
    link.delayAt = [](qint64 send) { return send >= 60000 ? 230 : 30; };

    link.run(t, 0, 60000);
    QVERIFY(link.run(t, 60000, 61000) >= 190);
    QCOMPARE(link.run(t, 61000, 300000, 60000 + 2 * PingDelayTracker::WindowMs + PingPeriodMs), 0);
}

void PingDelayTrackerTest::lateFirstPingIsCorrected()
{
    PingDelayTracker t;
    Link link;
    link.delayAt = [](qint64 send) { return send == 0 ? 500 : 30; };
    QCOMPARE(link.run(t, 0, 60000, PingPeriodMs), 0);
}

void PingDelayTrackerTest::peerClockStepBackIsAbsorbed()
{
    // Peer clock stepped back 1 s by NTP: its pings now look 1 s late.
    PingDelayTracker t;
    Link link;
    link.stepAtMs = 60000;
    link.stepMs = -1000;

    link.run(t, 0, 60000);
    QCOMPARE(link.run(t, 60000, 300000, 60000 + 2 * PingDelayTracker::WindowMs + PingPeriodMs), 0);
}

void PingDelayTrackerTest::peerClockStepForwardIsImmediate()
{
    // Peer clock stepped forward 1 s: its pings look early, baseline follows at once.
    PingDelayTracker t;
    Link link;
    link.stepAtMs = 60000;
    link.stepMs = 1000;

    QCOMPARE(link.run(t, 0, 300000), 0);
}

void PingDelayTrackerTest::midnightWrapIsSeamless()
{
    PingDelayTracker t;
    Link link;
    link.peerStartMs = PingDelayTracker::DayMs - 30000; // 23:59:30
    link.delayAt = jitter;
    QVERIFY(link.run(t, 0, 120000) <= 20);
}

void PingDelayTrackerTest::dropThreshold()
{
    QCOMPARE(PingDelayTracker::dropThresholdMs(20), 40);
    QCOMPARE(PingDelayTracker::dropThresholdMs(60), 90);
    QCOMPARE(PingDelayTracker::dropThresholdMs(150), 225);
}

int runPingDelayTrackerTests(int argc, char **argv)
{
    PingDelayTrackerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pingdelaytracker.moc"
