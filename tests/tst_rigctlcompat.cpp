#include "rigctlcompat.h"

#include <QtTest>

class RigCtlCompatTest : public QObject
{
    Q_OBJECT

private slots:
    void mapsZeroBasedRigAntennasToHamlibMask();
    void mapsOneBasedRigAntennasToHamlibMask();
    void convertsHamlibIndexBackToRigNumber();
    void detectsVfoNamesCaseInsensitively();
    void mapsAgcEnums();
    void formatsSsbBandwidths();
    void formatsCwAndDigitalBandwidths();
    void formatsAmBandwidths();
    void formatsFmBandwidths();
};

void RigCtlCompatTest::mapsZeroBasedRigAntennasToHamlibMask()
{
    const QList<int> rigNumbers{0, 1};
    QCOMPARE(rigctlcompat::hamlibAntennaIndex(0, rigNumbers), 0);
    QCOMPARE(rigctlcompat::hamlibAntennaIndex(1, rigNumbers), 1);
    QCOMPARE(rigctlcompat::antennaMask(rigNumbers), quint32(0x3));
}

void RigCtlCompatTest::mapsOneBasedRigAntennasToHamlibMask()
{
    const QList<int> rigNumbers{1, 2};
    QCOMPARE(rigctlcompat::hamlibAntennaIndex(1, rigNumbers), 0);
    QCOMPARE(rigctlcompat::hamlibAntennaIndex(2, rigNumbers), 1);
    QCOMPARE(rigctlcompat::antennaMask(rigNumbers), quint32(0x3));
}

void RigCtlCompatTest::convertsHamlibIndexBackToRigNumber()
{
    QCOMPARE(rigctlcompat::rigAntennaNumber(1, QList<int>({0, 1})), quint8(1));
    QCOMPARE(rigctlcompat::rigAntennaNumber(1, QList<int>({1, 2})), quint8(2));
    QCOMPARE(rigctlcompat::antennaName(1), QStringLiteral("ANT2"));
    QCOMPARE(rigctlcompat::antennaIndexFromName(QStringLiteral("ant2")), quint8(1));
}

void RigCtlCompatTest::detectsVfoNamesCaseInsensitively()
{
    QVERIFY(rigctlcompat::isVfoName(QStringLiteral("Main")));
    QVERIFY(rigctlcompat::isVfoName(QStringLiteral("sub")));
    QVERIFY(rigctlcompat::isVfoName(QStringLiteral("VFOA")));
    QVERIFY(!rigctlcompat::isVfoName(QStringLiteral("ANT1")));
}

void RigCtlCompatTest::mapsAgcEnums()
{
    // Icom: 00 OFF, 01 FAST, 02 MID, 03 SLOW
    const QList<rigctlcompat::AgcEntry> icom = {
        {0, QStringLiteral("OFF"), false, false},
        {1, QStringLiteral("FAST"), false, false},
        {2, QStringLiteral("MID"), false, false},
        {3, QStringLiteral("SLOW"), false, false}};
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(0, icom), 0);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(2, icom), 1);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(5, icom), 2);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(3, icom), 3);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(8, icom), 2); // ON falls back to MID
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(1, icom), -1);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(1, icom), 2);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(3, icom), 3);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(4, icom), -1);

    // Kenwood TS-890: 4 turns AGC back on and is never reported
    const QList<rigctlcompat::AgcEntry> kenwood = {
        {0, QStringLiteral("OFF"), false, false},
        {1, QStringLiteral("SLOW"), false, false},
        {2, QStringLiteral("MID"), false, false},
        {3, QStringLiteral("FAST"), false, false},
        {4, QStringLiteral("ON"), true, false}};
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(8, kenwood), 4);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(2, kenwood), 3);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(1, kenwood), 3);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(4, kenwood), -1);

    // Yaesu: AUTO reads back as one of three values
    const QList<rigctlcompat::AgcEntry> yaesu = {
        {0, QStringLiteral("OFF"), false, false},
        {4, QStringLiteral("AUTO"), false, false},
        {5, QStringLiteral("AUTO (MID)"), false, true},
        {6, QStringLiteral("AUTO (SLOW)"), false, true}};
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(6, yaesu), 4);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(5, yaesu), 6);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(6, yaesu), 6);
}

void RigCtlCompatTest::formatsSsbBandwidths()
{
    QCOMPARE(rigctlcompat::modeBandwidthResponse(QStringLiteral("USB")),
             QStringList({QStringLiteral("Mode=USB"), QStringLiteral("Normal=2400Hz"), QStringLiteral("Narrow=1800Hz"), QStringLiteral("Wide=3000Hz")}));
}

void RigCtlCompatTest::formatsCwAndDigitalBandwidths()
{
    QCOMPARE(rigctlcompat::modeBandwidthResponse(QStringLiteral("CW-R")),
             QStringList({QStringLiteral("Mode=CW-R"), QStringLiteral("Normal=500Hz"), QStringLiteral("Narrow=250Hz"), QStringLiteral("Wide=1200Hz")}));
    QCOMPARE(rigctlcompat::modeBandwidths(QStringLiteral("RTTY")).wide, 1200);
}

void RigCtlCompatTest::formatsAmBandwidths()
{
    const rigctlcompat::ModeBandwidths bandwidths = rigctlcompat::modeBandwidths(QStringLiteral("AM"));
    QCOMPARE(bandwidths.normal, 6000);
    QCOMPARE(bandwidths.narrow, 3000);
    QCOMPARE(bandwidths.wide, 9000);
}

void RigCtlCompatTest::formatsFmBandwidths()
{
    const rigctlcompat::ModeBandwidths bandwidths = rigctlcompat::modeBandwidths(QStringLiteral("FM-D"));
    QCOMPARE(bandwidths.normal, 10000);
    QCOMPARE(bandwidths.narrow, 7000);
    QCOMPARE(bandwidths.wide, 15000);
}

int runRigCtlCompatTests(int argc, char **argv)
{
    RigCtlCompatTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_rigctlcompat.moc"
