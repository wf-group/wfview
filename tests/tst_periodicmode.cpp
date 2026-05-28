#include <QtTest>

#include "wfviewtypes.h"

class PeriodicModeTest : public QObject
{
    Q_OBJECT

private slots:
    void unrestrictedCommandIsAllowed();
    void restrictedCommandMatchesModeCaseInsensitively();
    void restrictedCommandRejectsOtherModes();
    void receiverSpecificRestrictionsDoNotAffectOtherReceivers();
    void allReceiverRestrictionAppliesToEveryReceiver();
};

namespace
{
QVector<periodicType> samplePeriodicCommands()
{
    return {
        periodicType(funcAfGain, QStringLiteral("Medium High"), 4, char(-1)),
        periodicType(funcFilterShape, QStringLiteral("Medium"), 3, char(0), {"USB", "LSB", "CW", "CW-R"}),
        periodicType(funcCwPitch, QStringLiteral("Medium"), 3, char(-1), {"CW", "CW-R"}),
        periodicType(funcPBTInner, QStringLiteral("Medium"), 3, char(1), {"USB"})
    };
}
}

void PeriodicModeTest::unrestrictedCommandIsAllowed()
{
    const auto periodic = samplePeriodicCommands();
    QVERIFY(periodicCommandAllowedForMode(periodic, funcAfGain, 0, QStringLiteral("AM")));
    QVERIFY(periodicCommandAllowedForMode(periodic, funcAfGain, 1, QStringLiteral("FM")));
}

void PeriodicModeTest::restrictedCommandMatchesModeCaseInsensitively()
{
    const auto periodic = samplePeriodicCommands();
    QVERIFY(periodicCommandAllowedForMode(periodic, funcFilterShape, 0, QStringLiteral(" usb ")));
}

void PeriodicModeTest::restrictedCommandRejectsOtherModes()
{
    const auto periodic = samplePeriodicCommands();
    QVERIFY(!periodicCommandAllowedForMode(periodic, funcFilterShape, 0, QStringLiteral("AM")));
    QVERIFY(!periodicCommandAllowedForMode(periodic, funcCwPitch, 0, QStringLiteral("USB")));
}

void PeriodicModeTest::receiverSpecificRestrictionsDoNotAffectOtherReceivers()
{
    const auto periodic = samplePeriodicCommands();
    QVERIFY(periodicCommandAllowedForMode(periodic, funcPBTInner, 0, QStringLiteral("AM")));
    QVERIFY(!periodicCommandAllowedForMode(periodic, funcPBTInner, 1, QStringLiteral("AM")));
    QVERIFY(periodicCommandAllowedForMode(periodic, funcPBTInner, 1, QStringLiteral("USB")));
}

void PeriodicModeTest::allReceiverRestrictionAppliesToEveryReceiver()
{
    const auto periodic = samplePeriodicCommands();
    QVERIFY(periodicCommandAllowedForMode(periodic, funcCwPitch, 0, QStringLiteral("CW-R")));
    QVERIFY(periodicCommandAllowedForMode(periodic, funcCwPitch, 1, QStringLiteral("CW-R")));
    QVERIFY(!periodicCommandAllowedForMode(periodic, funcCwPitch, 1, QStringLiteral("FM")));
}

int runPeriodicModeTests(int argc, char **argv)
{
    PeriodicModeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_periodicmode.moc"
