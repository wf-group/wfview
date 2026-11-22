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

int runYaesuStatusBlockTests(int argc, char **argv)
{
    YaesuStatusBlockTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesustatusblock.moc"
