#include "yaesuvalues.h"

#include <QtTest>

class YaesuValuesTest : public QObject
{
    Q_OBJECT

private slots:
    void delayCodesMatchTable();
    void delayMsPicksNearestCode();
    void ifShiftFormat();
    void ifShiftParse();
    void scopeRefFormat();
    void scopeRefParse();
    void scopeWindowOutside();
    void vfoReportNewerRules();
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

void YaesuValuesTest::ifShiftFormat()
{
    QCOMPARE(yaesuFormatIfShift(0), QByteArray("+0000"));
    QCOMPARE(yaesuFormatIfShift(100), QByteArray("+0100"));
    QCOMPARE(yaesuFormatIfShift(-1200), QByteArray("-1200"));
    // Rounded to 20 Hz steps and clamped.
    QCOMPARE(yaesuFormatIfShift(109), QByteArray("+0100"));
    QCOMPARE(yaesuFormatIfShift(110), QByteArray("+0120"));
    QCOMPARE(yaesuFormatIfShift(-110), QByteArray("-0120"));
    QCOMPARE(yaesuFormatIfShift(5000), QByteArray("+1200"));
    QCOMPARE(yaesuFormatIfShift(-5000), QByteArray("-1200"));
}

void YaesuValuesTest::ifShiftParse()
{
    int hz = 1;
    QVERIFY(yaesuParseIfShift("+0000", &hz));
    QCOMPARE(hz, 0);
    QVERIFY(yaesuParseIfShift("-0340", &hz));
    QCOMPARE(hz, -340);
    QVERIFY(yaesuParseIfShift("+1200", &hz));
    QCOMPARE(hz, 1200);
    QVERIFY(!yaesuParseIfShift("0100", &hz));
    QVERIFY(!yaesuParseIfShift("+01", &hz));
    QVERIFY(!yaesuParseIfShift("+01x0", &hz));
}

void YaesuValuesTest::scopeRefFormat()
{
    QCOMPARE(yaesuFormatScopeRef(0), QByteArray("+00.0"));
    QCOMPARE(yaesuFormatScopeRef(55), QByteArray("+05.5"));
    QCOMPARE(yaesuFormatScopeRef(-300), QByteArray("-30.0"));
    QCOMPARE(yaesuFormatScopeRef(-5), QByteArray("-00.5"));
    // Rounded to 0.5 dB and clamped.
    QCOMPARE(yaesuFormatScopeRef(57), QByteArray("+05.5"));
    QCOMPARE(yaesuFormatScopeRef(58), QByteArray("+06.0"));
    QCOMPARE(yaesuFormatScopeRef(400), QByteArray("+30.0"));
}

void YaesuValuesTest::scopeRefParse()
{
    int level = 1;
    QVERIFY(yaesuParseScopeRef("+05.5", &level));
    QCOMPARE(level, 55);
    QVERIFY(yaesuParseScopeRef("-00.5", &level));
    QCOMPARE(level, -5);
    QVERIFY(yaesuParseScopeRef("-30.0", &level));
    QCOMPARE(level, -300);
    QVERIFY(!yaesuParseScopeRef("+055", &level));
    QVERIFY(!yaesuParseScopeRef("05.50", &level));
}

void YaesuValuesTest::scopeWindowOutside()
{
    // Window 28.200-28.300 MHz.
    QVERIFY(!yaesuVfoOutsideScopeWindow(28.250, 28.200, 0.100));
    QVERIFY(!yaesuVfoOutsideScopeWindow(28.200, 28.200, 0.100));
    QVERIFY(!yaesuVfoOutsideScopeWindow(28.300, 28.200, 0.100));
    QVERIFY(yaesuVfoOutsideScopeWindow(28.199, 28.200, 0.100));
    QVERIFY(yaesuVfoOutsideScopeWindow(28.301, 28.200, 0.100));
}

void YaesuValuesTest::vfoReportNewerRules()
{
    const qint64 limit = 1000;
    // No report yet.
    QVERIFY(!yaesuVfoReportNewer(-1, 28210000, 28200000, limit));
    // A recent report that differs from the frame is newer.
    QVERIFY(yaesuVfoReportNewer(30, 28210000, 28200000, limit));
    QVERIFY(yaesuVfoReportNewer(999, 28210000, 28200000, limit));
    // Once the frames carry the same frequency, they are current again.
    QVERIFY(!yaesuVfoReportNewer(30, 28210000, 28210000, limit));
    // An old report is not newer.
    QVERIFY(!yaesuVfoReportNewer(1000, 28210000, 28200000, limit));
}

int runYaesuValuesTests(int argc, char **argv)
{
    YaesuValuesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesuvalues.moc"
