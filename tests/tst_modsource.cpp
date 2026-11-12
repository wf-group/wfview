#include <QtTest>

#include "rigidentities.h"

class ModSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void rigInputComparisonIncludesRegister();
    void rigsWithoutDataModeStartAtDataOff();
    void rigsWithSeparateDataModeRemainUnknownUntilReported();
    void nullRigCapsKeepDataModeUnknown();
};

void ModSourceTest::rigInputComparisonIncludesRegister()
{
    const rigInput micFront(inputMic, 10, QStringLiteral("MIC"));
    const rigInput micRear(inputMic, 0, QStringLiteral("MIC"));
    const rigInput micFrontCopy(inputMic, 10, QStringLiteral("MIC"));

    QVERIFY(micFront != micRear);
    QVERIFY(micFront == micFrontCopy);
}

void ModSourceTest::rigsWithoutDataModeStartAtDataOff()
{
    rigCapabilities caps;
    caps.manufacturer = manufKenwood;
    caps.commands.insert(funcDATAOffMod, funcType());
    caps.commands.insert(funcDATA1Mod, funcType());

    QCOMPARE(initialDataModeForRig(&caps), uchar(0));
}

void ModSourceTest::rigsWithSeparateDataModeRemainUnknownUntilReported()
{
    rigCapabilities caps;
    caps.manufacturer = manufIcom;
    caps.commands.insert(funcDataMode, funcType());

    QCOMPARE(initialDataModeForRig(&caps), uchar(0xff));

    caps.commands.clear();
    caps.manufacturer = manufYaesu;
    caps.commands.insert(funcDataModeWithFilter, funcType());

    QCOMPARE(initialDataModeForRig(&caps), uchar(0xff));
}

void ModSourceTest::nullRigCapsKeepDataModeUnknown()
{
    QCOMPARE(initialDataModeForRig(nullptr), uchar(0xff));
}

int runModSourceTests(int argc, char **argv)
{
    ModSourceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_modsource.moc"
