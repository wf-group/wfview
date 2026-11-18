#include <QtTest>

#include "yaesuvalues.h"

class YaesuValuesTest : public QObject
{
    Q_OBJECT

private slots:
    void delayCodesMatchTable();
    void delayMsPicksNearestCode();
};

void YaesuValuesTest::delayCodesMatchTable()
{
    QCOMPARE(yaesuDelayCodeToMs(0), 30);
    QCOMPARE(yaesuDelayCodeToMs(5), 250);
    QCOMPARE(yaesuDelayCodeToMs(6), 300);
    QCOMPARE(yaesuDelayCodeToMs(7), 400);
    QCOMPARE(yaesuDelayCodeToMs(33), 3000);
    // Out of range codes are clamped.
    QCOMPARE(yaesuDelayCodeToMs(-1), 30);
    QCOMPARE(yaesuDelayCodeToMs(40), 3000);
}

void YaesuValuesTest::delayMsPicksNearestCode()
{
    for (int code = 0; code <= 33; ++code)
        QCOMPARE(yaesuDelayMsToCode(yaesuDelayCodeToMs(code)), code);
    QCOMPARE(yaesuDelayMsToCode(0), 0);
    QCOMPARE(yaesuDelayMsToCode(60), 1);
    QCOMPARE(yaesuDelayMsToCode(340), 6);
    QCOMPARE(yaesuDelayMsToCode(360), 7);
    QCOMPARE(yaesuDelayMsToCode(10000), 33);
}

int runYaesuValuesTests(int argc, char **argv)
{
    YaesuValuesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesuvalues.moc"
