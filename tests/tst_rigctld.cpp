#include "cachingqueue.h"
#include "rigctld.h"

#include <QElapsedTimer>
#include <QTcpSocket>
#include <QtTest>

class RigCtlDTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void queuesShortFormMorseText();
    void queuesLongFormMorseText();
    void queuesNumericMorseText();
    void preservesMorseSpacing_data();
    void preservesMorseSpacing();
    void queuesStopMorse();
    void pauseDoesNotBlockEventLoop();
    void closesClientThatNeverSendsNewline();

private:
    QByteArray sendRigctlLine(const QByteArray& line);
    QString queuedMorseText() const;

    rigCtlD server;
    cachingQueue* queue = nullptr;
    rigCapabilities caps;
};

void RigCtlDTest::initTestCase()
{
    queue = cachingQueue::getInstance();
    queue->interval(600000);

    caps.modelName = QStringLiteral("Rigctl Test Rig");
    caps.rigctlModel = 2;
    caps.modelID = 1;
    caps.numReceiver = 1;
    caps.numVFO = 1;
    caps.hasTransmit = true;
    caps.commands.insert(funcSendCW, funcType(funcSendCW, QStringLiteral("Send CW"), QByteArray(), 0, 0, false, false, false, true, 0, false));
    queue->setRigCaps(&caps);

    QVERIFY(server.startServer(0) == 0);
    QVERIFY(server.serverPort() > 0);
}

void RigCtlDTest::cleanupTestCase()
{
    server.stopServer();
    queue->clear();
    queue->clearCache();
    queue->setRigCaps(nullptr);
    queue->interval(-1);
}

void RigCtlDTest::init()
{
    queue->clear();
    queue->clearCache();
    queue->setRigCaps(&caps);
}

QByteArray RigCtlDTest::sendRigctlLine(const QByteArray& line)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    if (!socket.waitForConnected(1000)) {
        QTest::qFail(qPrintable(socket.errorString()), __FILE__, __LINE__);
        return {};
    }

    socket.write(line + '\n');
    if (!socket.waitForBytesWritten(1000)) {
        QTest::qFail(qPrintable(socket.errorString()), __FILE__, __LINE__);
        return {};
    }
    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    while (!response.contains("RPRT") && timer.elapsed() < 1000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (socket.bytesAvailable() || socket.waitForReadyRead(20)) {
            response += socket.readAll();
        }
    }

    if (!response.contains("RPRT")) {
        QTest::qFail(qPrintable(socket.errorString()), __FILE__, __LINE__);
    }

    socket.disconnectFromHost();
    return response;
}

QString RigCtlDTest::queuedMorseText() const
{
    QString text;
    const auto* items = queue->getQueueItems();
    for (auto it = items->cbegin(); it != items->cend(); ++it) {
        if (it->command == funcSendCW) {
            text = it->param.toString();
            break;
        }
    }
    queue->unlockMutex();
    return text;
}

void RigCtlDTest::queuesShortFormMorseText()
{
    const QByteArray response = sendRigctlLine("b CQ TEST");
    QVERIFY2(response.contains("RPRT 0"), response.constData());
    QCOMPARE(queuedMorseText(), QStringLiteral("CQ TEST"));
}

void RigCtlDTest::queuesLongFormMorseText()
{
    const QByteArray response = sendRigctlLine("\\send_morse CQ TEST");
    QVERIFY2(response.contains("RPRT 0"), response.constData());
    QCOMPARE(queuedMorseText(), QStringLiteral("CQ TEST"));
}

void RigCtlDTest::queuesNumericMorseText()
{
    const QByteArray response = sendRigctlLine("\\send_morse 73");
    QVERIFY2(response.contains("RPRT 0"), response.constData());
    QCOMPARE(queuedMorseText(), QStringLiteral("73"));
}

void RigCtlDTest::preservesMorseSpacing_data()
{
    QTest::addColumn<QByteArray>("line");

    QTest::newRow("short form") << QByteArray("b CQ  TEST");
    QTest::newRow("long form") << QByteArray("\\send_morse CQ  TEST");
}

void RigCtlDTest::preservesMorseSpacing()
{
    QFETCH(QByteArray, line);

    const QByteArray response = sendRigctlLine(line);
    QVERIFY2(response.contains("RPRT 0"), response.constData());
    QCOMPARE(queuedMorseText(), QStringLiteral("CQ  TEST"));
}

void RigCtlDTest::queuesStopMorse()
{
    const QByteArray response = sendRigctlLine("\\stop_morse");
    QVERIFY2(response.contains("RPRT 0"), response.constData());
    QCOMPARE(queuedMorseText(), QString());
}

// \pause must hold only this client: the event loop keeps running, the reply
// waits for the pause, and the commands after it still run.
void RigCtlDTest::pauseDoesNotBlockEventLoop()
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(socket.waitForConnected(1000));
    socket.write("\\pause 1\n\\send_morse 73\n");
    QVERIFY(socket.waitForBytesWritten(1000));

    QByteArray response;
    QElapsedTimer timer;
    timer.start();
    qint64 longestStallMs = 0;
    while (response.count("RPRT") < 2 && timer.elapsed() < 3000) {
        QElapsedTimer stall;
        stall.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        longestStallMs = qMax(longestStallMs, stall.elapsed());
        if (socket.bytesAvailable() || socket.waitForReadyRead(20))
            response += socket.readAll();
        if (timer.elapsed() < 800)
            QVERIFY2(!response.contains("RPRT"), response.constData());
    }

    QVERIFY2(longestStallMs < 500, qPrintable(QString::number(longestStallMs)));
    QCOMPARE(response.count("RPRT 0"), 2);
    QVERIFY(timer.elapsed() >= 900);
    QCOMPARE(queuedMorseText(), QStringLiteral("73"));
    socket.disconnectFromHost();
}

void RigCtlDTest::closesClientThatNeverSendsNewline()
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(socket.waitForConnected(1000));
    socket.write(QByteArray(128 * 1024, 'A'));

    QElapsedTimer timer;
    timer.start();
    while (socket.state() == QAbstractSocket::ConnectedState && timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
}

int runRigCtlDTests(int argc, char **argv)
{
    RigCtlDTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_rigctld.moc"
