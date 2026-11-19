// Checks the Yaesu rig file settings and commands the Yaesu code depends on.

#include <QDir>
#include <QSettings>
#include <QtTest>

class YaesuRigFilesTest : public QObject
{
    Q_OBJECT

private slots:
    void everyRigFileHasAVersion();
    void ft710Settings();
    void onlyTheFt710SwapsToneModes();
    void cwMemorySlotInRange();
    void autoNotchMatchesMainReceiver();
    void ftx1VersionRaised();
    void cwPitchIsInHz();
    void ft710ScopeWindowCommand();

private:
    struct Command
    {
        QString string;
        int min = 0;
        int max = 0;
        int bytes = 0;
    };
    static QString rigPath(const QString &file);
    // The command of this type in the rig file; false if there is none.
    static bool findCommand(const QString &file, const QString &type, Command *out);
};

QString YaesuRigFilesTest::rigPath(const QString &file)
{
    return QDir(QStringLiteral(WFVIEW_RIGS_DIR)).filePath(file);
}

bool YaesuRigFilesTest::findCommand(const QString &file, const QString &type, Command *out)
{
    QSettings rig(rigPath(file), QSettings::IniFormat);
    rig.beginGroup(QStringLiteral("Rig"));
    const int n = rig.beginReadArray(QStringLiteral("Commands"));
    bool found = false;
    for (int i = 0; i < n && !found; ++i)
    {
        rig.setArrayIndex(i);
        if (rig.value(QStringLiteral("Type")).toString() != type)
            continue;
        out->string = rig.value(QStringLiteral("String")).toString();
        out->min = rig.value(QStringLiteral("Min")).toInt();
        out->max = rig.value(QStringLiteral("Max")).toInt();
        out->bytes = rig.value(QStringLiteral("Bytes")).toInt();
        found = true;
    }
    rig.endArray();
    rig.endGroup();
    return found;
}

// QSettings reads an INI file's [General] section as top-level keys.
void YaesuRigFilesTest::everyRigFileHasAVersion()
{
    const QDir dir(QStringLiteral(WFVIEW_RIGS_DIR));
    const QStringList files = dir.entryList({QStringLiteral("*.rig")}, QDir::Files);
    QVERIFY(!files.isEmpty());
    for (const QString &f : files)
    {
        QSettings rig(dir.filePath(f), QSettings::IniFormat);
        QVERIFY2(rig.status() == QSettings::NoError, qPrintable(f));
        QVERIFY2(rig.value(QStringLiteral("Version")).toDouble() > 0, qPrintable(f));
    }
}

void YaesuRigFilesTest::ft710Settings()
{
    QSettings rig(rigPath(QStringLiteral("FT-710.rig")), QSettings::IniFormat);
    QCOMPARE(rig.value(QStringLiteral("Rig/ToneSquelchIsOne")).toBool(), true);
    QCOMPARE(rig.value(QStringLiteral("Rig/ScopeStatusBlock")).toBool(), true);
    QCOMPARE(rig.value(QStringLiteral("Rig/CwMemorySlot")).toInt(), 5);
}

// The FTX-1's CT uses 1 for tone and 2 for tone squelch, as wfview does.
void YaesuRigFilesTest::onlyTheFt710SwapsToneModes()
{
    const QDir dir(QStringLiteral(WFVIEW_RIGS_DIR));
    for (const QString &f : dir.entryList({QStringLiteral("*.rig")}, QDir::Files))
    {
        QSettings rig(dir.filePath(f), QSettings::IniFormat);
        const bool swaps = rig.value(QStringLiteral("Rig/ToneSquelchIsOne"), false).toBool();
        QVERIFY2(swaps == (f == QStringLiteral("FT-710.rig")), qPrintable(f));
    }
}

// Only memories 1-5 can be both written and played on the FT-710.
void YaesuRigFilesTest::cwMemorySlotInRange()
{
    const QDir dir(QStringLiteral(WFVIEW_RIGS_DIR));
    for (const QString &f : dir.entryList({QStringLiteral("*.rig")}, QDir::Files))
    {
        QSettings rig(dir.filePath(f), QSettings::IniFormat);
        if (!rig.contains(QStringLiteral("Rig/CwMemorySlot")))
            continue;
        const int slot = rig.value(QStringLiteral("Rig/CwMemorySlot")).toInt();
        QVERIFY2(slot >= 1 && slot <= 5, qPrintable(f));
    }
}

// BC answers BC P1 P2; P1 (the receiver) is part of the matched command, so
// the value is the one state digit.
void YaesuRigFilesTest::autoNotchMatchesMainReceiver()
{
    for (const QString &f : {QStringLiteral("FT-710.rig"), QStringLiteral("FTX-1.rig")})
    {
        Command c;
        QVERIFY2(findCommand(f, QStringLiteral("Auto Notch"), &c), qPrintable(f));
        QCOMPARE(c.string, QStringLiteral("BC0"));
        QCOMPARE(c.bytes, 1);
        QCOMPARE(c.min, 0);
        QCOMPARE(c.max, 1);
    }
}

// A user copy at the same version is preferred, so the version must rise with
// the CW pitch range change.
void YaesuRigFilesTest::ftx1VersionRaised()
{
    QSettings rig(rigPath(QStringLiteral("FTX-1.rig")), QSettings::IniFormat);
    QVERIFY(rig.value(QStringLiteral("Version")).toDouble() > 2.11);
}

// KP is sent as a step number, but wfview carries the pitch in Hz.
void YaesuRigFilesTest::cwPitchIsInHz()
{
    for (const QString &f : {QStringLiteral("FT-710.rig"), QStringLiteral("FTX-1.rig")})
    {
        Command c;
        QVERIFY2(findCommand(f, QStringLiteral("CW Pitch"), &c), qPrintable(f));
        QCOMPARE(c.string, QStringLiteral("KP"));
        QCOMPARE(c.min, 300);
        QCOMPARE(c.max, 1050);
        QCOMPARE(c.bytes, 2);
    }
}

// FD00n reads the scope window start: n = 1 Cursor, 2 Fixed.
void YaesuRigFilesTest::ft710ScopeWindowCommand()
{
    Command c;
    QVERIFY(findCommand(QStringLiteral("FT-710.rig"), QStringLiteral("Scope Fixed Edge Freq"), &c));
    QCOMPARE(c.string, QStringLiteral("FD00"));
    QCOMPARE(c.min, 1);
    QCOMPARE(c.max, 2);
    QCOMPARE(c.bytes, 10);
}

int runYaesuRigFilesTests(int argc, char **argv)
{
    YaesuRigFilesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesurigfiles.moc"
