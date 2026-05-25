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
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(0), 0);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(1), 1);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(2), 3);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(5), 8);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(3), 13);
    QCOMPARE(rigctlcompat::rigAgcFromHamlib(6), -1);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(0), 0);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(1), 1);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(2), 4);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(3), 2);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(8), 5);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(12), 4);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(13), 3);
    QCOMPARE(rigctlcompat::hamlibAgcFromRig(14), -1);
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
