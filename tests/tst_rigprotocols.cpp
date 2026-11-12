#include <QtTest>

class RigProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void icomEncodesFrequencySetFrame();
    void icomEncodesPttSetFrame();
    void kenwoodEncodesFrequencySet();
    void kenwoodEncodesTunerCycle();
    void yaesuEncodesFrequencySet();
    void yaesuEncodesSubReceiverFrequencyGet();
    void yaesuEncodesTunerCycle();
};

namespace
{
QByteArray icomFrequencyPayload(quint64 hz, int byteCount = 5)
{
    QByteArray payload;
    for (int i = 0; i < byteCount; ++i) {
        uchar value = hz % 10;
        hz /= 10;
        value |= (hz % 10) << 4;
        hz /= 10;
        payload.append(char(value));
    }
    return payload;
}

QByteArray icomFrame(const QByteArray &command, const QByteArray &payload = QByteArray())
{
    return QByteArray::fromHex("FEFE94E1") + command + payload + QByteArray::fromHex("FD");
}

QByteArray asciiFrequencyCommand(const QByteArray &command, quint64 hz)
{
    return command + QString::number(hz).rightJustified(11, QLatin1Char('0')).toLatin1() + ";";
}
}

void RigProtocolTest::icomEncodesFrequencySetFrame()
{
    QCOMPARE(icomFrame(QByteArray::fromHex("05"), icomFrequencyPayload(14074000)),
             QByteArray::fromHex("FEFE94E1050040071400FD"));
}

void RigProtocolTest::icomEncodesPttSetFrame()
{
    QCOMPARE(icomFrame(QByteArray::fromHex("1C00"), QByteArray::fromHex("01")),
             QByteArray::fromHex("FEFE94E11C0001FD"));
}

void RigProtocolTest::kenwoodEncodesFrequencySet()
{
    QCOMPARE(asciiFrequencyCommand("FA", 14074000), QByteArray("FA00014074000;"));
}

void RigProtocolTest::kenwoodEncodesTunerCycle()
{
    QCOMPARE(QByteArray("AC") + QByteArray("111") + QByteArray(";"), QByteArray("AC111;"));
}

void RigProtocolTest::yaesuEncodesFrequencySet()
{
    QCOMPARE(asciiFrequencyCommand("FA", 14074000), QByteArray("FA00014074000;"));
}

void RigProtocolTest::yaesuEncodesSubReceiverFrequencyGet()
{
    QCOMPARE(QByteArray("FB;"), QByteArray("FB;"));
}

void RigProtocolTest::yaesuEncodesTunerCycle()
{
    QCOMPARE(QByteArray("AC") + QByteArray("003") + QByteArray(";"), QByteArray("AC003;"));
}

int runRigProtocolTests(int argc, char **argv)
{
    RigProtocolTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_rigprotocols.moc"
