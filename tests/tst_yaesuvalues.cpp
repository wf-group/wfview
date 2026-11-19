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
    void cwPitchStepsAndHz();
    void toneModeSwap();
    void cwMemoryChunks();
    void cwKeyingTime();
    void scopeWindowOutside();
    void scopeWindowReadRules();
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

void YaesuValuesTest::cwPitchStepsAndHz()
{
    // KP00 is 300 Hz and KP75 is 1050 Hz, in 10 Hz steps.
    QCOMPARE(yaesuCwPitchHzFromSteps(0), 300);
    QCOMPARE(yaesuCwPitchHzFromSteps(30), 600);
    QCOMPARE(yaesuCwPitchHzFromSteps(75), 1050);
    for (int steps = 0; steps <= 75; ++steps)
        QCOMPARE(yaesuCwPitchStepsFromHz(yaesuCwPitchHzFromSteps(steps)), steps);
    // Hz between steps rounds to the nearest step.
    QCOMPARE(yaesuCwPitchStepsFromHz(604), 30);
    QCOMPARE(yaesuCwPitchStepsFromHz(605), 31);
}

void YaesuValuesTest::toneModeSwap()
{
    QCOMPARE(yaesuSwapToneMode(0), quint8(0));
    QCOMPARE(yaesuSwapToneMode(1), quint8(2));
    QCOMPARE(yaesuSwapToneMode(2), quint8(1));
    QCOMPARE(yaesuSwapToneMode(3), quint8(3));
    QCOMPARE(yaesuSwapToneMode(4), quint8(4));
    for (quint8 mode = 0; mode < 10; ++mode)
        QCOMPARE(yaesuSwapToneMode(yaesuSwapToneMode(mode)), mode);
}

void YaesuValuesTest::cwMemoryChunks()
{
    // Text that fits is sent whole.
    QCOMPARE(yaesuCwMemoryChunkLength("CQ CQ DE N0RC", 49), 13);
    QCOMPARE(yaesuCwMemoryChunkLength(QByteArray(49, 'E'), 49), 49);
    QCOMPARE(yaesuCwMemoryChunkLength(QByteArray(), 49), 0);
    // Longer text ends after the last space that fits.
    QCOMPARE(yaesuCwMemoryChunkLength("ABCD EFGH IJKL", 10), 10);
    QCOMPARE(yaesuCwMemoryChunkLength("ABCD EFGH IJKL", 9), 5);
    QCOMPARE(yaesuCwMemoryChunkLength("ABCD EFGH IJKL", 6), 5);
    // With no space to split at, it is cut at the limit.
    QCOMPARE(yaesuCwMemoryChunkLength(QByteArray(60, 'E'), 49), 49);
    // A space only at the very start does not make an empty memory.
    QCOMPARE(yaesuCwMemoryChunkLength(" ABCDEFGHIJ", 5), 5);

    // Splitting a long message never loses or reorders characters.
    QByteArray pending = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 THE QUICK BROWN FOX";
    QByteArray rebuilt;
    while (!pending.isEmpty())
    {
        const int n = yaesuCwMemoryChunkLength(pending, 20);
        QVERIFY(n > 0 && n <= 20);
        rebuilt += pending.left(n);
        pending.remove(0, n);
    }
    QCOMPARE(rebuilt, QByteArray("THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 THE QUICK BROWN FOX"));
}

void YaesuValuesTest::cwKeyingTime()
{
    // PARIS is 50 dits including its word space, so one PARIS at 1 wpm is 60 s.
    const int paris = yaesuCwKeyingMs("PARIS ", 1, 3.0);
    QVERIFY(qAbs(paris - 60000) <= 1200);
    // Time scales inversely with speed.
    QVERIFY(qAbs(yaesuCwKeyingMs("PARIS ", 20, 3.0) - paris / 20) <= 60);
    // A heavier dash ratio takes longer.
    QVERIFY(yaesuCwKeyingMs("TTTT", 20, 4.0) > yaesuCwKeyingMs("TTTT", 20, 3.0));
    QCOMPARE(yaesuCwKeyingMs(QByteArray(), 20, 3.0), 0);
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

void YaesuValuesTest::scopeWindowReadRules()
{
    // Never read: read now.
    QVERIFY(yaesuScopeWindowReadDue(false, false, -1));
    QVERIFY(yaesuScopeWindowReadDue(true, false, -1));
    // Known and the VFO inside: read again only after a second.
    QVERIFY(!yaesuScopeWindowReadDue(true, false, 500));
    QVERIFY(!yaesuScopeWindowReadDue(true, false, 1000));
    QVERIFY(yaesuScopeWindowReadDue(true, false, 1001));
    // Unknown, or the VFO outside: read, but at most every 200 ms.
    QVERIFY(!yaesuScopeWindowReadDue(false, false, 100));
    QVERIFY(!yaesuScopeWindowReadDue(true, true, 200));
    QVERIFY(yaesuScopeWindowReadDue(false, false, 201));
    QVERIFY(yaesuScopeWindowReadDue(true, true, 201));
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
