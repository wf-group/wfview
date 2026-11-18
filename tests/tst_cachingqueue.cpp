#include "cachingqueue.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QtTest>

// The command queue must keep working whatever is added to it: floods of
// immediate commands, reads asked for in a tight loop, and reads the radio
// cannot do. These tests drive the real singleton.
class CachingQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void mergesDuplicateReads();
    void keepsEverySet();
    void refusesUnreadableReads();
    void keepsRemappedReads();
    void delReadsKeepsSets();
    void getCacheLimitsRefreshRate();
    void routinePollingSurvivesImmediateFlood();
    void warnsAboutBacklog();
    void transmitPollIntervalRules();

private:
    int waiting(queuePriority prio, funcs func, bool reads = true);
    void stopQueue();

    cachingQueue* queue = nullptr;
    rigCapabilities caps;
};

static funcType makeCmd(funcs f, bool get, bool set)
{
    return funcType(f, funcString[f], QByteArray("XX"), 0, 255, false, false, get, set, 3, false);
}

void CachingQueueTest::initTestCase()
{
    queue = cachingQueue::getInstance();

    caps.modelName = QStringLiteral("Queue Test Rig");
    caps.modelID = 1;
    caps.numReceiver = 1;
    caps.numVFO = 1;
    caps.hasTransmit = true;
    caps.commands.insert(funcFreq, makeCmd(funcFreq, true, true));
    caps.commands.insert(funcSMeter, makeCmd(funcSMeter, true, false));
    caps.commands.insert(funcAfGain, makeCmd(funcAfGain, true, true));
    // Like the TS-890S: the SWR meter can be enabled but not read on its own.
    caps.commands.insert(funcSWRMeter, makeCmd(funcSWRMeter, false, true));
    // Yaesu reads this through a mode-specific command.
    caps.commands.insert(funcDATAOffMod, makeCmd(funcDATAOffMod, false, true));
}

void CachingQueueTest::stopQueue()
{
    // A long interval keeps the queue thread from taking items during a test.
    queue->interval(600000);
}

void CachingQueueTest::cleanupTestCase()
{
    queue->clear();
    queue->clearCache();
    queue->setRigCaps(nullptr);
    queue->interval(-1);
    queue->resetStats();
}

void CachingQueueTest::init()
{
    stopQueue();
    queue->clear();
    queue->clearCache();
    queue->setRigCaps(&caps);
    queue->resetStats();
}

int CachingQueueTest::waiting(queuePriority prio, funcs func, bool reads)
{
    int n = 0;
    const auto* items = queue->getQueueItems();
    for (auto it = items->cbegin(); it != items->cend(); ++it) {
        if (it.key() == prio && it->command == func && it->param.isValid() != reads)
            ++n;
    }
    queue->unlockMutex();
    return n;
}

void CachingQueueTest::mergesDuplicateReads()
{
    for (int i = 0; i < 500; ++i)
        queue->add(priorityImmediate, funcFreq, false, 0);
    QCOMPARE(waiting(priorityImmediate, funcFreq), 1);
    QCOMPARE(queue->getStats().coalesced, quint64(499));

    // A different receiver is a different read.
    queue->add(priorityImmediate, funcFreq, false, 1);
    QCOMPARE(queue->getStats().totalWaiting, 2);

    // Adding a recurring poll does not add another immediate read when one is waiting.
    queue->add(priorityHighest, funcFreq, true, 0);
    QCOMPARE(waiting(priorityImmediate, funcFreq), 2);
    QCOMPARE(waiting(priorityHighest, funcFreq), 1);
}

void CachingQueueTest::keepsEverySet()
{
    for (int i = 0; i < 20; ++i)
        queue->add(priorityImmediate, queueItem(funcAfGain, QVariant::fromValue<uchar>(uchar(i)), false, 0));
    QCOMPARE(waiting(priorityImmediate, funcAfGain, false), 20);
}

void CachingQueueTest::refusesUnreadableReads()
{
    for (int i = 0; i < 100; ++i) {
        queue->add(priorityImmediate, funcSWRMeter, false, 0);
        queue->getCache(funcSWRMeter, 0);
    }
    queue->add(priorityHighest, funcSWRMeter, true, 0);
    QCOMPARE(waiting(priorityImmediate, funcSWRMeter), 0);
    QCOMPARE(waiting(priorityHighest, funcSWRMeter), 0);
    QVERIFY(queue->getStats().unreadableRejected >= 100);

    // It can still be set (the TS-890S enables the SWR meter this way).
    queue->add(priorityImmediate, queueItem(funcSWRMeter, QVariant::fromValue<uchar>(1), false, 0));
    QCOMPARE(waiting(priorityImmediate, funcSWRMeter, false), 1);
}

void CachingQueueTest::keepsRemappedReads()
{
    queue->add(priorityImmediate, funcDATAOffMod, false, 0);
    QCOMPARE(waiting(priorityImmediate, funcDATAOffMod), 1);
}

void CachingQueueTest::delReadsKeepsSets()
{
    queue->add(priorityHighest, funcAfGain, true, 0);
    queue->add(priorityMedium, funcAfGain, false, 0);
    queue->add(priorityImmediate, queueItem(funcAfGain, QVariant::fromValue<uchar>(7), false, 0));
    QCOMPARE(queue->getStats().totalWaiting, 4); // recurring + its immediate read + read + set

    queue->delReads(funcAfGain, 0);
    QCOMPARE(queue->getStats().totalWaiting, 1);
    QCOMPARE(waiting(priorityImmediate, funcAfGain, false), 1);
}

void CachingQueueTest::getCacheLimitsRefreshRate()
{
    queue->getCache(funcAfGain, 0);
    QCOMPARE(waiting(priorityImmediate, funcAfGain), 1);

    // As if the queue had sent it: the read is no longer waiting, but was just requested.
    queue->clear();
    for (int i = 0; i < 1000; ++i)
        queue->getCache(funcAfGain, 0);
    QCOMPARE(waiting(priorityImmediate, funcAfGain), 0);
    QVERIFY(queue->getStats().refreshesSkipped >= 1000);

    QTest::qWait(300);
    queue->getCache(funcAfGain, 0);
    QCOMPARE(waiting(priorityImmediate, funcAfGain), 1);
}

void CachingQueueTest::routinePollingSurvivesImmediateFlood()
{
    QObject context;
    QMutex lock;
    int sMeterSent = 0;
    int afGainSent = 0;
    connect(queue, &cachingQueue::haveCommand, &context,
            [&](funcs func, QVariant, uchar) {
                QMutexLocker l(&lock);
                if (func == funcSMeter)
                    ++sMeterSent;
                else if (func == funcAfGain)
                    ++afGainSent;
            }, Qt::DirectConnection);

    queue->add(priorityHighest, funcSMeter, true, 0);
    queue->interval(2);

    // Keep the immediate list full of sets for a while, faster than it can be emptied.
    QElapsedTimer t;
    t.start();
    uchar v = 0;
    while (t.elapsed() < 600) {
        for (int i = 0; i < 4; ++i)
            queue->add(priorityImmediate, queueItem(funcAfGain, QVariant::fromValue<uchar>(v++), false, 0));
        QTest::qWait(2);
    }
    stopQueue();
    disconnect(queue, nullptr, &context, nullptr);

    QMutexLocker l(&lock);
    const queueStats st = queue->getStats();
    QVERIFY2(st.immediateWaiting > 0, "the flood did not keep immediate commands waiting");
    QVERIFY2(afGainSent > 20, qPrintable(QStringLiteral("only %1 immediate commands sent").arg(afGainSent)));
    // Without the fairness rule only the first (immediate) S-meter read would go out.
    QVERIFY2(sMeterSent >= 5, qPrintable(QStringLiteral("S-meter polled only %1 times during the flood").arg(sMeterSent)));
    QVERIFY(st.fairnessTurns >= 4);
    // Immediate commands still get most of the slots.
    QVERIFY(afGainSent > 2 * sMeterSent);
}

void CachingQueueTest::warnsAboutBacklog()
{
    // Expected first: a tick left over from the previous test may check the backlog at any time.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("Command queue backlog: \\d+ immediate commands waiting"));
    for (int i = 0; i < 80; ++i)
        queue->add(priorityImmediate, queueItem(funcAfGain, QVariant::fromValue<uchar>(uchar(i)), false, 0));
    queue->add(priorityHighest, funcSMeter, true, 0);

    queue->interval(1000); // a slow tick, so the backlog is still there when it is checked
    QTRY_VERIFY_WITH_TIMEOUT(queue->getStats().warnings >= 1, 3000);
    stopQueue();
    QVERIFY(queue->getStats().immediateHighWater >= 80);
}

void CachingQueueTest::transmitPollIntervalRules()
{
    // Slower only while transmitting, over a network, on a rig that asks for it.
    QCOMPARE(transmitPollInterval(25, true, true, true, 4), qint64(100));
    QCOMPARE(transmitPollInterval(25, false, true, true, 4), qint64(25));
    QCOMPARE(transmitPollInterval(25, true, false, true, 4), qint64(25));  // serial
    QCOMPARE(transmitPollInterval(25, true, true, false, 4), qint64(25));  // rig file off
    // Manual polling is multiplied the same way.
    QCOMPARE(transmitPollInterval(60, true, true, true, 4), qint64(240));
    // The factor is kept within 1..10.
    QCOMPARE(transmitPollInterval(25, true, true, true, 0), qint64(25));
    QCOMPARE(transmitPollInterval(25, true, true, true, 50), qint64(250));
    // A stopped queue stays stopped.
    QCOMPARE(transmitPollInterval(-1, true, true, true, 4), qint64(-1));
    QCOMPARE(transmitPollInterval(0, true, true, true, 4), qint64(0));
}

int runCachingQueueTests(int argc, char **argv)
{
    CachingQueueTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_cachingqueue.moc"
