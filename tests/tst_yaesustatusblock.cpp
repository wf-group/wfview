#include "yaesustatusblock.h"

#include <QDir>
#include <QSettings>
#include <QtTest>

class YaesuStatusBlockTest : public QObject
{
    Q_OBJECT

private slots:
    void shortDataGivesNothing();
    void decodesFlags();
    void decodesAttPreampAndAutoNotch();
    void leavesAutoNotchToCatInFm();
    void decodesNumbers();
    void decodesNegativeShiftAndClarifier();
    void confirmsOnlyWhenTwoFramesAgree();
    void reportsOnlyChanges();
    void onlyTheFt710OptsIn();
    void scopeModesMapToWindowModes();
    void scopeWindowFromCursorCapture();
    void scopeWindowFollowsCursorJump();
    void scopeWindowFromFixedCapture();
    void scopeWindowNotUsedInCenter();
    void scopeWindowNeedsAgreeingModeBytes();
    void scopeWindowShortDataIsUnknown();
    void scopeWindowEdgesNeedStartAndSpan();
    void scopeWindowConfirmsOnlyWhenTwoFramesAgree();
    void scopeWindowConfirmReset();
    void ft710SpansMatchCaptures();
};

namespace
{
QByteArray emptyBlock()
{
    return QByteArray(150, '\0');
}

void putU16(QByteArray &data, int i, quint16 v)
{
    data[i] = char(v >> 8);
    data[i + 1] = char(v & 0xff);
}

quint32 u32At(const QByteArray &data, int i)
{
    return (quint32(quint8(data.at(i))) << 24) | (quint32(quint8(data.at(i + 1))) << 16)
         | (quint32(quint8(data.at(i + 2))) << 8) | quint32(quint8(data.at(i + 3)));
}

// Status blocks (wfview's data[0..149], SPI offsets 2900-3049) from FT-710 scope captures, with the radio
// set up over CAT for each one.

// Cursor (Normal), 100 kHz span, VFO-A 14.030000. The window started at 14.008940 and the VFO is inside it.
const char cursorAt14030000[] =
    "000000000000000000000000000000000007100000c000088800050481140000460901f400000000801e002f00fa00000046"
    "500001030507090aff12030164f4001403000020202020202020202020202000000000030100f70018082000202020202020"
    "202020202020000000000b0800000000000000800018000000000000380005dc00d614b000d614b000d614b000d5c26c0000";

// The same after tuning to 14.120000, past the window: the radio moved it to 14.020000 (VFO - 100 kHz).
const char cursorAt14120000[] =
    "000000000000000000000000000000000007100000c000088800050481140000460901f400000000801e002f00fa00000046"
    "500001030507090aff12030164f4001412000020202020202020202020202000000000030100f70018082000202020202020"
    "20202020202000000000030800000000000000800018000000000000380005dc00d7744000d7744000d7744000d5eda00000";

// Fixed (Normal), VFO-A 14.205000. Fixed keeps its own 500 kHz span; the fixed edge is 14.000000 (FD002).
const char fixedAt14205000[] =
    "00000000000000000000000000000000000a100000c00008e800050481140000880901f400000000801e002f00fa00000046"
    "500002030507090aff12030164f4001420500020202020202020202020202000000000030100f70018082000202020202020"
    "20202020202000000000060800000000000000800018000000000000380005dc00d8c04800d8c04800d8c04800d59f800000";

// Center (Normal), 100 kHz span, VFO-A 14.205000. data[144] holds 14.105000 (VFO - span), not the edge.
const char centerAt14205000[] =
    "000000000000000000000000000000000004100000c00008e800050481140000060901f400000000801e002f00fa00000046"
    "500000030507090aff12030164f4001420500020202020202020202020202000000000030100f70018082000202020202020"
    "20202020202000000000010800000000000000800018000000000000380005dc00d8c04800d8c04800d8c04800d739a80000";

QByteArray capture(const char *hex)
{
    return QByteArray::fromHex(QByteArray(hex));
}

// VFO-A in the same block, as wfview reads it (data[132], u32 BE Hz).
quint32 captureVfoHz(const QByteArray &data)
{
    return u32At(data, 132);
}
}

void YaesuStatusBlockTest::shortDataGivesNothing()
{
    QVERIFY(yaesuStatusBlockValues(QByteArray(100, '\0')).isEmpty());
}

void YaesuStatusBlockTest::decodesFlags()
{
    QByteArray d = emptyBlock();
    QHash<funcs, QVariant> v = yaesuStatusBlockValues(d);
    QCOMPARE(v.value(funcTransceiverStatus).toBool(), false);
    QCOMPARE(v.value(funcSplitStatus).toBool(), false);
    QCOMPARE(v.value(funcLockFunction).toBool(), false);
    QCOMPARE(v.value(funcNoiseReduction).value<uchar>(), uchar(0));

    d[22] = char(0x80);
    d[18] = char(0x20);
    d[28] = char(0x50);
    d[111] = char(0x14);
    v = yaesuStatusBlockValues(d);
    QCOMPARE(v.value(funcTransceiverStatus).toBool(), true);
    QCOMPARE(v.value(funcSplitStatus).toBool(), true);
    QCOMPARE(v.value(funcAPFType).toBool(), true);
    QCOMPARE(v.value(funcManualNotch).toBool(), true);
    QCOMPARE(v.value(funcLockFunction).toBool(), true);
    QCOMPARE(v.value(funcNoiseReduction).value<uchar>(), uchar(1));
    QCOMPARE(v.value(funcTransceiverStatus).userType(), QMetaType::Bool);
}

void YaesuStatusBlockTest::decodesAttPreampAndAutoNotch()
{
    QByteArray d = emptyBlock();
    d[27] = char(0x1b); // ATT 3, preamp 2, auto notch on
    const QHash<funcs, QVariant> v = yaesuStatusBlockValues(d);
    QCOMPARE(v.value(funcAttenuator).value<uchar>(), uchar(3));
    QCOMPARE(v.value(funcPreamp).value<uchar>(), uchar(2));
    QCOMPARE(v.value(funcAutoNotch).toBool(), true);
}

void YaesuStatusBlockTest::leavesAutoNotchToCatInFm()
{
    QByteArray d = emptyBlock();
    d[27] = char(0x20);
    QVERIFY(!yaesuStatusBlockValues(d).contains(funcAutoNotch));
}

void YaesuStatusBlockTest::decodesNumbers()
{
    QByteArray d = emptyBlock();
    putU16(d, 42, 150); // notch 1500 Hz
    putU16(d, 46, 600); // IF shift +600 Hz
    d[48] = char(25); // APF level, middle of 0-50
    d[49] = char(60); // CW pitch 600 Hz
    d[121] = char(22); // 22 WPM
    const QHash<funcs, QVariant> v = yaesuStatusBlockValues(d);
    QCOMPARE(v.value(funcNotchFilter).value<ushort>(), ushort(150));
    QCOMPARE(short(v.value(funcIFShift).value<ushort>()), short(600));
    QCOMPARE(v.value(funcAPFLevel).value<ushort>(), ushort(128));
    QCOMPARE(v.value(funcCwPitch).value<ushort>(), ushort(600));
    QCOMPARE(v.value(funcKeySpeed).value<uchar>(), uchar(22));
}

void YaesuStatusBlockTest::decodesNegativeShiftAndClarifier()
{
    QByteArray d = emptyBlock();
    putU16(d, 46, quint16(qint16(-1200)));
    putU16(d, 82, quint16(qint16(-250)));
    const QHash<funcs, QVariant> v = yaesuStatusBlockValues(d);
    QCOMPARE(short(v.value(funcIFShift).value<ushort>()), short(-1200));
    QCOMPARE(v.value(funcRitFreq).value<short>(), short(-250));
}

void YaesuStatusBlockTest::confirmsOnlyWhenTwoFramesAgree()
{
    YaesuStatusConfirm c;
    QByteArray a = emptyBlock();
    QByteArray b = emptyBlock();
    b[121] = char(30);

    QHash<funcs, QVariant> confirmed;
    QVERIFY(c.update(yaesuStatusBlockValues(a), &confirmed).isEmpty());
    QVERIFY(confirmed.isEmpty());

    // A torn frame that disagrees with the one before is not reported.
    confirmed.clear();
    QHash<funcs, QVariant> changed = c.update(yaesuStatusBlockValues(b), &confirmed);
    QVERIFY(!changed.contains(funcKeySpeed));
    QVERIFY(!confirmed.contains(funcKeySpeed));

    confirmed.clear();
    changed = c.update(yaesuStatusBlockValues(b), &confirmed);
    QCOMPARE(changed.value(funcKeySpeed).value<uchar>(), uchar(30));
    QVERIFY(confirmed.contains(funcKeySpeed));
}

void YaesuStatusBlockTest::reportsOnlyChanges()
{
    YaesuStatusConfirm c;
    const QHash<funcs, QVariant> v = yaesuStatusBlockValues(emptyBlock());
    c.update(v);
    QVERIFY(!c.update(v).isEmpty());

    QHash<funcs, QVariant> confirmed;
    QVERIFY(c.update(v, &confirmed).isEmpty());
    QCOMPARE(confirmed.size(), v.size());
}

// The status block is only decoded for rig files that set ScopeStatusBlock; every other rig keeps today's
// behaviour.
void YaesuStatusBlockTest::onlyTheFt710OptsIn()
{
    const QDir dir(QStringLiteral(WFVIEW_RIGS_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.rig")}, QDir::Files);
    QVERIFY(files.contains(QStringLiteral("FT-710.rig")));
    for (const QString &f : files)
    {
        QSettings rig(dir.filePath(f), QSettings::IniFormat);
        const bool enabled = rig.value(QStringLiteral("Rig/ScopeStatusBlock"), false).toBool();
        QCOMPARE(enabled, f == QStringLiteral("FT-710.rig"));
    }
}

void YaesuStatusBlockTest::scopeModesMapToWindowModes()
{
    // FT-710.rig ScopeModes Num -> window mode.
    const quint8 expected[] = {
        yaesuScopeWindow::center, yaesuScopeWindow::cursor, yaesuScopeWindow::fixed, // 00-02 3DSS
        yaesuScopeWindow::center, yaesuScopeWindow::center, yaesuScopeWindow::center, // 03-05 Center
        yaesuScopeWindow::cursor, yaesuScopeWindow::cursor, yaesuScopeWindow::cursor, // 06-08 Cursor
        yaesuScopeWindow::fixed,  yaesuScopeWindow::fixed,                            // 09-0A Fixed
    };
    for (int m = 0; m < int(sizeof(expected)); ++m)
        QCOMPARE(yaesuScopeModeWindowMode(quint8(m)), expected[m]);
    QCOMPARE(yaesuScopeModeWindowMode(0x0b), yaesuScopeWindow::unknown);
    QCOMPARE(yaesuScopeModeWindowMode(0xff), yaesuScopeWindow::unknown);
}

void YaesuStatusBlockTest::scopeWindowFromCursorCapture()
{
    const QByteArray d = capture(cursorAt14030000);
    QCOMPARE(d.size(), 150);
    const yaesuScopeWindow w = yaesuScopeWindowValues(d);
    QCOMPARE(w.mode, yaesuScopeWindow::cursor);
    QCOMPARE(w.spanReg, quint8(6)); // data[32] is 0x46
    QCOMPARE(w.startHz, quint32(14008940));

    double start = 0.0;
    double end = 0.0;
    QVERIFY(yaesuScopeWindowEdges(w, 100000, &start, &end));
    QCOMPARE(start, 14.008940);
    QCOMPARE(end, 14.108940);
    const double vfo = captureVfoHz(d) / 1000000.0;
    QCOMPARE(vfo, 14.030000);
    QVERIFY(vfo >= start && vfo <= end);
}

void YaesuStatusBlockTest::scopeWindowFollowsCursorJump()
{
    const QByteArray d = capture(cursorAt14120000);
    const yaesuScopeWindow w = yaesuScopeWindowValues(d);
    QCOMPARE(w.mode, yaesuScopeWindow::cursor);
    QCOMPARE(w.spanReg, quint8(6));
    QCOMPARE(w.startHz, quint32(14020000));

    double start = 0.0;
    double end = 0.0;
    QVERIFY(yaesuScopeWindowEdges(w, 100000, &start, &end));
    QCOMPARE(start, 14.020000);
    QCOMPARE(end, 14.120000);
    // The radio put the cursor on the right edge.
    QCOMPARE(captureVfoHz(d) / 1000000.0, end);
}

void YaesuStatusBlockTest::scopeWindowFromFixedCapture()
{
    const QByteArray d = capture(fixedAt14205000);
    const yaesuScopeWindow w = yaesuScopeWindowValues(d);
    QCOMPARE(w.mode, yaesuScopeWindow::fixed);
    QCOMPARE(w.spanReg, quint8(8)); // data[32] is 0x88
    QCOMPARE(w.startHz, quint32(14000000));

    double start = 0.0;
    double end = 0.0;
    QVERIFY(yaesuScopeWindowEdges(w, 500000, &start, &end));
    QCOMPARE(start, 14.000000);
    QCOMPARE(end, 14.500000);
    QCOMPARE(captureVfoHz(d), quint32(14205000));
}

void YaesuStatusBlockTest::scopeWindowNotUsedInCenter()
{
    const QByteArray d = capture(centerAt14205000);
    const yaesuScopeWindow w = yaesuScopeWindowValues(d);
    QCOMPARE(w.mode, yaesuScopeWindow::center);
    QCOMPARE(w.spanReg, quint8(6));
    // data[144] is VFO - span in Center, which is not the display edge.
    QCOMPARE(w.startHz, quint32(14105000));

    double start = -1.0;
    double end = -1.0;
    QVERIFY(!yaesuScopeWindowEdges(w, 100000, &start, &end));
    QCOMPARE(start, -1.0);
    QCOMPARE(end, -1.0);
}

void YaesuStatusBlockTest::scopeWindowNeedsAgreeingModeBytes()
{
    QByteArray d = capture(cursorAt14030000);
    d[52] = char(yaesuScopeWindow::fixed); // scope mode 07 is Cursor
    QCOMPARE(yaesuScopeWindowValues(d).mode, yaesuScopeWindow::unknown);

    d = capture(cursorAt14030000);
    d[17] = char(0x0b); // not a ScopeModes Num
    QCOMPARE(yaesuScopeWindowValues(d).mode, yaesuScopeWindow::unknown);

    d[17] = char(0xff);
    d[52] = char(0xff);
    QCOMPARE(yaesuScopeWindowValues(d).mode, yaesuScopeWindow::unknown);

    double start = 0.0;
    double end = 0.0;
    QVERIFY(!yaesuScopeWindowEdges(yaesuScopeWindowValues(d), 100000, &start, &end));
}

void YaesuStatusBlockTest::scopeWindowShortDataIsUnknown()
{
    const QByteArray d = capture(cursorAt14030000).left(147);
    QCOMPARE(yaesuScopeWindowValues(d).mode, yaesuScopeWindow::unknown);
    QCOMPARE(yaesuScopeWindowValues(QByteArray()).mode, yaesuScopeWindow::unknown);
}

void YaesuStatusBlockTest::scopeWindowEdgesNeedStartAndSpan()
{
    yaesuScopeWindow w = yaesuScopeWindowValues(capture(cursorAt14030000));
    double start = 0.0;
    double end = 0.0;
    // A span the rig file does not list.
    QVERIFY(!yaesuScopeWindowEdges(w, 0, &start, &end));
    w.startHz = 0;
    QVERIFY(!yaesuScopeWindowEdges(w, 100000, &start, &end));
    QVERIFY(!yaesuScopeWindowEdges(yaesuScopeWindow(), 100000, &start, &end));
}

void YaesuStatusBlockTest::scopeWindowConfirmsOnlyWhenTwoFramesAgree()
{
    const yaesuScopeWindow before = yaesuScopeWindowValues(capture(cursorAt14030000));
    const yaesuScopeWindow after = yaesuScopeWindowValues(capture(cursorAt14120000));
    YaesuScopeWindowConfirm c;
    QCOMPARE(c.window().mode, yaesuScopeWindow::unknown);

    QVERIFY(!c.update(before));
    QCOMPARE(c.window().mode, yaesuScopeWindow::unknown);
    QVERIFY(c.update(before));
    QVERIFY(c.window() == before);
    QVERIFY(!c.update(before));

    // A single frame that disagrees (torn while it was rewritten) is not used, and the last window is kept.
    yaesuScopeWindow torn = before;
    torn.startHz = before.startHz ^ 0x00ff0000u;
    QVERIFY(torn != before && torn != after);
    QVERIFY(!c.update(torn));
    QVERIFY(c.window() == before);

    // The Cursor window moved: used from the second frame that carries it.
    QVERIFY(!c.update(after));
    QVERIFY(c.window() == before);
    QVERIFY(c.update(after));
    QVERIFY(c.window() == after);

    // Two frames whose mode bytes disagree make the window unknown, so the axis is centred again.
    QVERIFY(!c.update(yaesuScopeWindow()));
    QVERIFY(c.update(yaesuScopeWindow()));
    QCOMPARE(c.window().mode, yaesuScopeWindow::unknown);
}

void YaesuStatusBlockTest::scopeWindowConfirmReset()
{
    const yaesuScopeWindow w = yaesuScopeWindowValues(capture(fixedAt14205000));
    YaesuScopeWindowConfirm c;
    c.update(w);
    QVERIFY(c.update(w));
    c.reset();
    QCOMPARE(c.window().mode, yaesuScopeWindow::unknown);
    // After a reset the frame before it does not count.
    QVERIFY(!c.update(w));
    QVERIFY(c.update(w));
}

// The window's width is the rig file's span for data[32]'s low nibble; the captures pinned 20 kHz, 100 kHz
// and 500 kHz.
void YaesuStatusBlockTest::ft710SpansMatchCaptures()
{
    QSettings rig(QDir(QStringLiteral(WFVIEW_RIGS_DIR)).filePath(QStringLiteral("FT-710.rig")), QSettings::IniFormat);
    rig.beginGroup(QStringLiteral("Rig"));
    QHash<uint, uint> spans;
    const int n = rig.beginReadArray(QStringLiteral("Spans"));
    for (int i = 0; i < n; ++i)
    {
        rig.setArrayIndex(i);
        spans.insert(rig.value(QStringLiteral("Num")).toUInt(), rig.value(QStringLiteral("Freq")).toUInt());
    }
    rig.endArray();
    rig.endGroup();
    QCOMPARE(spans.value(4), 20000u);
    QCOMPARE(spans.value(6), 100000u);
    QCOMPARE(spans.value(8), 500000u);
}

int runYaesuStatusBlockTests(int argc, char **argv)
{
    YaesuStatusBlockTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesustatusblock.moc"
