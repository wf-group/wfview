#include "radiotransportframe.h"

#include <QtTest>

class RadioTransportFrameTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsOpaqueCivPayload();
    void reportsIncompleteHeader();
    void reportsIncompletePayload();
    void rejectsInvalidMagic();
};

void RadioTransportFrameTest::roundTripsOpaqueCivPayload()
{
    radioTransportFrame::Frame in;
    in.channel = RadioTransportChannel::Civ;
    in.sequence = 42;
    in.payload = QByteArray::fromHex("fefe94e01900fd");

    const QByteArray encoded = radioTransportFrame::encode(in);

    radioTransportFrame::Frame out;
    int bytesUsed = 0;
    QCOMPARE(radioTransportFrame::decode(encoded, &out, &bytesUsed), radioTransportFrame::DecodeOk);
    QCOMPARE(bytesUsed, encoded.size());
    QCOMPARE(out.channel, RadioTransportChannel::Civ);
    QCOMPARE(out.sequence, quint32(42));
    QCOMPARE(out.payload, in.payload);
}

void RadioTransportFrameTest::reportsIncompleteHeader()
{
    radioTransportFrame::Frame out;
    QCOMPARE(radioTransportFrame::decode(QByteArrayLiteral("WF"), &out), radioTransportFrame::DecodeIncomplete);
}

void RadioTransportFrameTest::reportsIncompletePayload()
{
    radioTransportFrame::Frame in;
    in.channel = RadioTransportChannel::Cat;
    in.sequence = 7;
    in.payload = QByteArrayLiteral("FA;");

    const QByteArray truncated = radioTransportFrame::encode(in).left(15);

    radioTransportFrame::Frame out;
    QCOMPARE(radioTransportFrame::decode(truncated, &out), radioTransportFrame::DecodeIncomplete);
}

void RadioTransportFrameTest::rejectsInvalidMagic()
{
    QByteArray encoded = radioTransportFrame::encode({RadioTransportChannel::Cat, 1, QByteArrayLiteral("ID;")});
    encoded[0] = 'X';

    radioTransportFrame::Frame out;
    QCOMPARE(radioTransportFrame::decode(encoded, &out), radioTransportFrame::DecodeInvalid);
}

int runRadioTransportFrameTests(int argc, char **argv)
{
    RadioTransportFrameTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_radiotransportframe.moc"
