#include "icomudpbase.h"

#include <QtTest>

#include <chrono>
#include <future>

// Malformed or unlucky packets on the Icom LAN protocol must not hang or crash
// the receive path.
class IcomUdpBaseTest : public QObject
{
    Q_OBJECT

private slots:
    void gapEndingAtFFFFFinishes();
    void singleRetransmitWithEmptyBuffer();
    void multipleRetransmitWithEmptyBuffer();
    void oddLengthRetransmitIgnoresTrailingByte();

private:
    static QByteArray controlPacket(quint32 len, quint16 type, quint16 seq);
    static void receiveWithin(icomUdpBase& base, const QByteArray& r, int ms);
};

QByteArray IcomUdpBaseTest::controlPacket(quint32 len, quint16 type, quint16 seq)
{
    control_packet p;
    memset(p.packet, 0, sizeof(p));
    p.len = len;
    p.type = type;
    p.seq = seq;
    QByteArray r(p.packet, sizeof(p));
    r.append(QByteArray(int(len) - r.size(), '\0'));
    return r;
}

// Fails the test, rather than hanging the whole run, if dataReceived() never returns.
void IcomUdpBaseTest::receiveWithin(icomUdpBase& base, const QByteArray& r, int ms)
{
    auto done = std::async(std::launch::async, [&base, r]() { base.dataReceived(r); });
    if (done.wait_for(std::chrono::milliseconds(ms)) != std::future_status::ready)
        qFatal("icomUdpBase::dataReceived() did not return");
}

void IcomUdpBaseTest::gapEndingAtFFFFFinishes()
{
    icomUdpBase base;
    base.init(0);
    // Data packets: any length other than a control or ping packet.
    receiveWithin(base, controlPacket(0x18, 0x00, 0xFFF0), 2000);
    receiveWithin(base, controlPacket(0x18, 0x00, 0xFFFF), 2000);

    QCOMPARE(base.rxMissing.size(), 0xFFFF - 0xFFF0 - 1);
    QVERIFY(base.rxMissing.contains(0xFFF1));
    QVERIFY(base.rxMissing.contains(0xFFFE));
    QVERIFY(base.rxSeqBuf.contains(0xFFFF));
}

void IcomUdpBaseTest::singleRetransmitWithEmptyBuffer()
{
    icomUdpBase base;
    base.init(0);
    QVERIFY(base.txSeqBuf.isEmpty());
    receiveWithin(base, controlPacket(CONTROL_SIZE, 0x01, 0x1234), 2000);
}

void IcomUdpBaseTest::multipleRetransmitWithEmptyBuffer()
{
    icomUdpBase base;
    base.init(0);
    QByteArray r = controlPacket(CONTROL_SIZE + 4, 0x01, 0);
    r[CONTROL_SIZE] = 0x34;
    r[CONTROL_SIZE + 1] = 0x12;
    r[CONTROL_SIZE + 2] = 0x35;
    r[CONTROL_SIZE + 3] = 0x12;
    receiveWithin(base, r, 2000);
}

void IcomUdpBaseTest::oddLengthRetransmitIgnoresTrailingByte()
{
    icomUdpBase base;
    base.init(0);
    base.sendControl(true, 0, 0); // Puts sequence 1 in the send buffer.
    QVERIFY(base.txSeqBuf.contains(1));

    // Asks for sequence 1, then one stray byte that is not a sequence number.
    QByteArray r = controlPacket(CONTROL_SIZE + 3, 0x01, 0);
    r[CONTROL_SIZE] = 0x01;
    r[CONTROL_SIZE + 1] = 0x00;
    r[CONTROL_SIZE + 2] = 0x01;
    receiveWithin(base, r, 2000);
    QCOMPARE(int(base.txSeqBuf.value(1).retransmitCount), 1);
}

int runIcomUdpBaseTests(int argc, char **argv)
{
    IcomUdpBaseTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_icomudpbase.moc"
