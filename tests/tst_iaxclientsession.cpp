#include "iaxclientsession.h"

#include <QtTest>

class IaxClientSessionTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsFullFrame();
    void roundTripsMiniFrame();
    void roundTripsInformationElements();
    void rejectsShortFrames();
    void directModeAuthenticatesAndConnects();
    void directModeDoesNotCountLagRequestAsSequenceGap();
    void directModeAcknowledgesReliablePayload();
};

void IaxClientSessionTest::roundTripsFullFrame()
{
    iax::Frame in;
    in.sourceCall = 42;
    in.destCall = 7;
    in.timestamp = 123456;
    in.outgoingSeq = 3;
    in.incomingSeq = 2;
    in.type = iax::FrameType::Iax;
    in.subclass = quint8(iax::IaxSubclass::New);
    in.payload = QByteArrayLiteral("wfview");

    iax::Frame out;
    QVERIFY(iax::decodeFrame(iax::encodeFullFrame(in), &out));
    QCOMPARE(out.kind, iax::FrameKind::Full);
    QCOMPARE(out.sourceCall, in.sourceCall);
    QCOMPARE(out.destCall, in.destCall);
    QCOMPARE(out.timestamp, in.timestamp);
    QCOMPARE(out.outgoingSeq, in.outgoingSeq);
    QCOMPARE(out.incomingSeq, in.incomingSeq);
    QCOMPARE(out.type, in.type);
    QCOMPARE(out.subclass, in.subclass);
    QCOMPARE(out.payload, in.payload);
}

void IaxClientSessionTest::roundTripsMiniFrame()
{
    const QByteArray payload = QByteArrayLiteral("payload");
    iax::Frame out;
    QVERIFY(iax::decodeFrame(iax::encodeMiniFrame(11, 22, payload), &out));
    QCOMPARE(out.kind, iax::FrameKind::Mini);
    QCOMPARE(out.sourceCall, quint16(11));
    QCOMPARE(out.timestamp, quint32(22));
    QCOMPARE(out.payload, payload);
}

void IaxClientSessionTest::roundTripsInformationElements()
{
    const QByteArray encoded =
        iax::encodeInformationElementString(iax::InformationElement::Username, QStringLiteral("wfview")) +
        iax::encodeInformationElementString(iax::InformationElement::Challenge, QStringLiteral("abc123")) +
        iax::encodeInformationElementUShort(iax::InformationElement::AuthMethods, iax::AuthMd5);

    const auto elements = iax::decodeInformationElements(encoded);
    QCOMPARE(iax::informationElementString(elements, iax::InformationElement::Username), QStringLiteral("wfview"));
    QCOMPARE(iax::informationElementString(elements, iax::InformationElement::Challenge), QStringLiteral("abc123"));
    QCOMPARE(iax::informationElementUShort(elements, iax::InformationElement::AuthMethods), quint16(iax::AuthMd5));
}

void IaxClientSessionTest::rejectsShortFrames()
{
    iax::Frame out;
    QVERIFY(!iax::decodeFrame(QByteArrayLiteral("\x80\x01\x00"), &out));
}

void IaxClientSessionTest::directModeAuthenticatesAndConnects()
{
    QUdpSocket probe;
    QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
    const quint16 port = probe.localPort();
    probe.close();

    IaxClientSession server;
    IaxClientSession client;
    QSignalSpy serverConnected(&server, &IaxClientSession::connectedChanged);
    QSignalSpy clientConnected(&client, &IaxClientSession::connectedChanged);
    QVERIFY(serverConnected.isValid());
    QVERIFY(clientConnected.isValid());

    server.listenForDirectCalls(port, [](const QString &username) {
        return username == QStringLiteral("wfuser") ? QStringList{QStringLiteral("wfpass")} : QStringList();
    });
    serverConnected.clear();
    clientConnected.clear();

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           port,
                           QStringLiteral("wfuser"),
                           QStringLiteral("wfpass"),
                           QString());

    QTRY_VERIFY_WITH_TIMEOUT(!clientConnected.isEmpty() && clientConnected.last().at(0).toBool(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!serverConnected.isEmpty() && serverConnected.last().at(0).toBool(), 2000);

    client.disconnectFromServer();
    server.disconnectFromServer();
}

void IaxClientSessionTest::directModeDoesNotCountLagRequestAsSequenceGap()
{
    QUdpSocket probe;
    QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
    const quint16 port = probe.localPort();
    probe.close();

    IaxClientSession server;
    IaxClientSession client;
    QSignalSpy serverConnected(&server, &IaxClientSession::connectedChanged);
    QSignalSpy clientConnected(&client, &IaxClientSession::connectedChanged);
    QSignalSpy serverPayload(&server, &IaxClientSession::payloadReceived);
    QSignalSpy serverStats(&server, &IaxClientSession::statsChanged);
    QVERIFY(serverConnected.isValid());
    QVERIFY(clientConnected.isValid());
    QVERIFY(serverPayload.isValid());
    QVERIFY(serverStats.isValid());

    server.listenForDirectCalls(port, [](const QString &username) {
        return username == QStringLiteral("wfuser") ? QStringList{QStringLiteral("wfpass")} : QStringList();
    });
    serverConnected.clear();
    clientConnected.clear();
    serverStats.clear();

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           port,
                           QStringLiteral("wfuser"),
                           QStringLiteral("wfpass"),
                           QString());

    QTRY_VERIFY_WITH_TIMEOUT(!clientConnected.isEmpty() && clientConnected.last().at(0).toBool(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!serverConnected.isEmpty() && serverConnected.last().at(0).toBool(), 2000);

    client.sendPayload(QByteArrayLiteral("first"));
    QTRY_COMPARE_WITH_TIMEOUT(serverPayload.size(), 1, 1000);

    QTest::qWait(2500);

    client.sendPayload(QByteArrayLiteral("second"));
    QTRY_COMPARE_WITH_TIMEOUT(serverPayload.size(), 2, 1000);

    IaxStats latest;
    QTRY_VERIFY_WITH_TIMEOUT(!serverStats.isEmpty(), 1500);
    latest = qvariant_cast<IaxStats>(serverStats.last().at(0));
    QCOMPARE(latest.oSeqnoGaps, quint32(0));

    client.disconnectFromServer();
    server.disconnectFromServer();
}

void IaxClientSessionTest::directModeAcknowledgesReliablePayload()
{
    QUdpSocket probe;
    QVERIFY(probe.bind(QHostAddress::LocalHost, 0));
    const quint16 port = probe.localPort();
    probe.close();

    IaxClientSession server;
    IaxClientSession client;
    QSignalSpy serverConnected(&server, &IaxClientSession::connectedChanged);
    QSignalSpy clientConnected(&client, &IaxClientSession::connectedChanged);
    QSignalSpy serverPayload(&server, &IaxClientSession::payloadReceived);
    QSignalSpy clientStats(&client, &IaxClientSession::statsChanged);
    QVERIFY(serverConnected.isValid());
    QVERIFY(clientConnected.isValid());
    QVERIFY(serverPayload.isValid());
    QVERIFY(clientStats.isValid());

    server.listenForDirectCalls(port, [](const QString &username) {
        return username == QStringLiteral("wfuser") ? QStringList{QStringLiteral("wfpass")} : QStringList();
    });
    serverConnected.clear();
    clientConnected.clear();
    clientStats.clear();

    client.connectToServer(QStringLiteral("127.0.0.1"),
                           port,
                           QStringLiteral("wfuser"),
                           QStringLiteral("wfpass"),
                           QString());

    QTRY_VERIFY_WITH_TIMEOUT(!clientConnected.isEmpty() && clientConnected.last().at(0).toBool(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!serverConnected.isEmpty() && serverConnected.last().at(0).toBool(), 2000);

    client.sendPayload(QByteArrayLiteral("reliable"), true);
    QTRY_COMPARE_WITH_TIMEOUT(serverPayload.size(), 1, 1000);

    QTRY_VERIFY_WITH_TIMEOUT(!clientStats.isEmpty(), 1500);
    QTRY_VERIFY_WITH_TIMEOUT(qvariant_cast<IaxStats>(clientStats.last().at(0)).acksRx > 0 &&
                             qvariant_cast<IaxStats>(clientStats.last().at(0)).pendingFullFrames == 0,
                             1500);

    client.disconnectFromServer();
    server.disconnectFromServer();
}

int runIaxClientSessionTests(int argc, char **argv)
{
    IaxClientSessionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_iaxclientsession.moc"
