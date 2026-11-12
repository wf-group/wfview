#include "cluster.h"
#include "logcategories.h"

#include <utility>


dxClusterClient::dxClusterClient(QObject* parent):
    QObject(parent)
{
    qInfo(logCluster()) << "starting dxClusterClient()";
}

dxClusterClient::~dxClusterClient()
{
    qInfo(logCluster()) << "closing dxClusterClient()";
    enableUdp(false);
    enableTcp(false);
#ifdef USESQL
    database db;
    db.close();
#else
    QMap<QString, spotData*>::iterator spot = allSpots.begin();
    while (spot != allSpots.end())
    {
        delete spot.value(); // Stop memory leak?
        spot = allSpots.erase(spot);
    }
#endif
}

void dxClusterClient::enableUdp(bool enable)
{
    if (shuttingDown && enable)
        return;

    udpEnable = enable;
    if (enable)
    {
        if (udpSocket == nullptr)
        {
            udpSocket = new QUdpSocket(this);
            bool result = udpSocket->bind(QHostAddress::AnyIPv4, udpPort);
            qInfo(logCluster()) << "Starting udpSocket() on:" << udpPort << "Result:" << result;

            connect(udpSocket, SIGNAL(readyRead()), this, SLOT(udpDataReceived()), Qt::QueuedConnection);
        }
        ensureCleanupTimer();
    }
    else {
        if (udpSocket != nullptr)
        {
            qInfo(logCluster()) << "Stopping udpSocket() on:" << udpPort;

            udpSocket->disconnect();
            delete udpSocket;
            udpSocket = nullptr;
        }
    }
}

void dxClusterClient::enableTcp(bool enable)
{
    if (shuttingDown && enable)
        return;

    tcpEnable = enable;
    if (enable)
    {
        if (tcpServerName.trimmed().isEmpty()) {
            emit sendOutput(QString("\nNo TCP cluster server is selected.\n"));
            setTcpStatus(tcpNotConnected);
            return;
        }

        // Keep this deliberately tolerant. DXSpider, AR-Cluster and CC Cluster
        // all use the real-time "DX de <spotter>: <freq> <dx> <comment> <time>Z"
        // shape, but suffixes and extra text vary between servers.
        tcpRegex = QRegularExpression("^\\s*DX\\s+de\\s+(\\S+):\\s+([0-9]+(?:\\.[0-9]+)?)\\s+(\\S+)(?:\\s+(.*?))?\\s+(\\d{4})Z(?:\\s+.*)?$");
        tcpRegex.setPatternOptions(QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);

        if (tcpSocket == nullptr)
            openTcp();
    }
    else {
        if (tcpSocket != nullptr)
        {
            qInfo(logCluster()) << "Disconnecting tcpSocket() on:" << tcpPort;
            emit sendOutput(QString("\nDisconnecting from %0 %1\n").arg(tcpServerName).arg(tcpPort));
            closeTcp(true);
        }
        setTcpStatus(tcpNotConnected);
    }
}

void dxClusterClient::reconnectTcpIfChanged()
{
    if (shuttingDown || !tcpEnable)
        return;

    if (!targetChanged())
        return;

    qInfo(logCluster()) << "Cluster server settings changed, reconnecting to" << tcpServerName << tcpPort;
    if (tcpSocket != nullptr)
    {
        emit sendOutput(QString("\nDisconnecting from %0 %1\n").arg(connectedServerName).arg(connectedPort));
        closeTcp(true);
    }
    enableTcp(true);
}

void dxClusterClient::openTcp()
{
    tcpReceiveBuffer.clear();
    authenticated = false;
    connectedServerName = tcpServerName;
    connectedPort = tcpPort;
    connectedUserName = tcpUserName;
    connectedPassword = tcpPassword;

    tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, SIGNAL(readyRead()), this, SLOT(tcpDataReceived()), Qt::QueuedConnection);
    connect(tcpSocket, SIGNAL(disconnected()), this, SLOT(tcpDisconnected()));
    connect(tcpSocket, SIGNAL(connected()), this, SLOT(tcpConnected()));
#if (QT_VERSION >= QT_VERSION_CHECK(5,15,0))
    connect(tcpSocket, &QAbstractSocket::errorOccurred, this, &dxClusterClient::tcpSocketError);
#else
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, &dxClusterClient::tcpSocketError);
#endif

    if (tcpConnectTimer == nullptr)
    {
        tcpConnectTimer = new QTimer(this);
        tcpConnectTimer->setSingleShot(true);
        tcpConnectTimer->setInterval(tcpConnectTimeoutMs);
        connect(tcpConnectTimer, SIGNAL(timeout()), this, SLOT(tcpConnectTimedOut()));
    }
    tcpConnectTimer->start();
    ensureCleanupTimer();

    qInfo(logCluster()) << "Starting tcpSocket() to:" << tcpServerName << "port:" << tcpPort;
    emit sendOutput(QString("\nConnecting to %0 %1\n\n").arg(tcpServerName).arg(tcpPort));
    setTcpStatus(tcpConnecting);
    tcpSocket->connectToHost(tcpServerName, tcpPort);
}

void dxClusterClient::closeTcp(bool sayBye)
{
    if (tcpConnectTimer != nullptr)
        tcpConnectTimer->stop();

    if (tcpSocket != nullptr)
    {
        if (sayBye && tcpSocket->state() == QAbstractSocket::ConnectedState)
            sendTcpData(QString("bye\n"));
        tcpSocket->disconnect();
        tcpSocket->abort();
        tcpSocket->deleteLater();
        tcpSocket = nullptr;
    }
    tcpReceiveBuffer.clear();
    authenticated = false;
}

void dxClusterClient::failTcp(const QString &reason)
{
    qWarning(logCluster()) << "TCP cluster connection to" << connectedServerName << connectedPort << "failed:" << reason;
    emit sendOutput(QString("\nConnection to %0 %1 failed: %2\n").arg(connectedServerName).arg(connectedPort).arg(reason));
    closeTcp(false);
    setTcpStatus(tcpError);
}

void dxClusterClient::setTcpStatus(tcpStatus s)
{
    if (status == s)
        return;
    status = s;
    emit tcpStatusChanged(int(s));
}

bool dxClusterClient::targetChanged() const
{
    return tcpServerName != connectedServerName || tcpPort != connectedPort ||
           tcpUserName != connectedUserName || tcpPassword != connectedPassword;
}

void dxClusterClient::clearStaleStatus()
{
    // The status describes the last connection attempt. Once a different server
    // is selected it no longer applies, even before the reconnect starts.
    if (targetChanged())
        setTcpStatus(tcpNotConnected);
}

void dxClusterClient::ensureCleanupTimer()
{
    if (tcpCleanupTimer == nullptr)
    {
        tcpCleanupTimer = new QTimer(this);
        tcpCleanupTimer->setInterval(1000 * 5);
        connect(tcpCleanupTimer, SIGNAL(timeout()), this, SLOT(tcpCleanup()));
    }
    if (!tcpCleanupTimer->isActive())
        tcpCleanupTimer->start();
}

void dxClusterClient::tcpConnected()
{
    if (tcpConnectTimer != nullptr)
        tcpConnectTimer->stop();

    qInfo(logCluster()) << "Connected to TCP cluster" << connectedServerName << connectedPort;
    emit sendOutput(QString("Connected to %0 %1\n").arg(connectedServerName).arg(connectedPort));
    setTcpStatus(tcpConnectedNoTraffic);
}

void dxClusterClient::tcpSocketError()
{
    if (tcpSocket == nullptr)
        return;

    // A server closing an established session is handled by tcpDisconnected().
    if (status != tcpConnecting && tcpSocket->error() == QAbstractSocket::RemoteHostClosedError)
        return;

    failTcp(tcpSocket->errorString());
}

void dxClusterClient::tcpConnectTimedOut()
{
    if (tcpSocket == nullptr || tcpSocket->state() == QAbstractSocket::ConnectedState)
        return;

    failTcp(QString("no response after %0 seconds").arg(tcpConnectTimeoutMs / 1000));
}

void dxClusterClient::shutdown()
{
    shuttingDown = true;
    enableTcp(false);
    enableUdp(false);
}

bool dxClusterClient::parseTcpSpotLine(const QString &line, int timeout, spotData *spot)
{
    if (spot == nullptr)
        return false;

    const QString trimmed = line.trimmed();
    static const QRegularExpression spotRegex(
        QStringLiteral(R"(^DX\s+de\s+(\S+):\s+([0-9]+(?:\.[0-9]+)?)\s+(\S+)(?:\s+(.*?))?\s+(\d{4})Z(?:\s+.*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = spotRegex.match(trimmed);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    const double frequency = match.captured(2).toDouble(&ok);
    if (!ok || frequency <= 0.0)
        return false;

    spot->spottercall = match.captured(1);
    spot->frequency = frequency / 1000.0;
    spot->dxcall = match.captured(3);
    spot->comment = match.captured(4).trimmed();
    spot->timestamp = QDateTime::currentDateTimeUtc();
    spot->timeout = timeout;
    return !spot->spottercall.isEmpty() && !spot->dxcall.isEmpty();
}

void dxClusterClient::udpDataReceived()
{
    QHostAddress sender;
    quint16 port;
    QByteArray datagram;
    datagram.resize(udpSocket->pendingDatagramSize());
    udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &port);

    if (udpSpotReader.setContent(datagram))
    {
        QDomElement spot = udpSpotReader.firstChildElement("spot");
        if (spot.nodeName() == "spot")
        {
            // This is a spot?
            QString action = spot.firstChildElement("action").text();
            if (action == "add") {
                spotData* data = new spotData();
                data->dxcall = spot.firstChildElement("dxcall").text();
                data->frequency = spot.firstChildElement("frequency").text().toDouble() / 1000.0;
                data->spottercall = spot.firstChildElement("spottercall").text();
                // Lifetime runs from when the spot arrived here, the sender's timestamp
                // carries no time zone.
                data->timestamp = QDateTime::currentDateTimeUtc();
                data->mode = spot.firstChildElement("mode").text();
                data->comment = spot.firstChildElement("comment").text();
                data->timeout = udpLifetime;

#ifdef USESQL
                database db = database();
                db.query(QString("DELETE from spots where dxcall='%1'").arg(data->dxcall));
                QString query = QString("INSERT INTO spots(type,spottercall,frequency,dxcall,mode,comment,timestamp) VALUES('%1','%2',%3,'%4','%5','%6','%7')\n")
                    .arg("UDP").arg(data->spottercall).arg(data->frequency).arg(data->dxcall).arg(data->mode).arg(data->comment).arg(data->timestamp.toString("yyyy-MM-dd hh:mm:ss"));
                db.query(query);
                emit sendOutput(QString("<spot><action>add</action><dxcall>%1</dxcall><spottercall>%2</spottercall><frequency>%3</frequency><comment>%4</comment></spot>\n")
                    .arg(data->dxcall).arg(data->spottercall).arg(data->frequency).arg(data->comment));
                delete data;
#else
                emit sendOutput(QString("<spot><action>add</action><dxcall>%1</dxcall><spottercall>%2</spottercall><frequency>%3</frequency><comment>%4</comment></spot>\n")
                    .arg(data->dxcall).arg(data->spottercall).arg(data->frequency).arg(data->comment));
                storeSpot(data);
#endif

            }
            else if (action == "delete")
            {
                QString dxcall = spot.firstChildElement("dxcall").text();
                double frequency = spot.firstChildElement("frequency").text().toDouble() / 1000.0;

#ifdef USESQL
                database db = database();
                QString query=QString("DELETE from spots where dxcall='%1' AND frequency=%2").arg(dxcall).arg(frequency);
                db.query(query);
                qDebug(logCluster()) << query;
#else
                QMap<QString, spotData*>::iterator spot = allSpots.find(dxcall);
                while (spot != allSpots.end() && spot.key() == dxcall && spot.value()->frequency == frequency) 
                {
                    delete spot.value(); // Stop memory leak?
                    spot = allSpots.erase(spot);
                }
#endif
                emit sendOutput(QString("<spot><action>delete</action><dxcall>%1</dxcall<frequency>%3</frequency></spot>\n")
                    .arg(dxcall).arg(frequency));
            }
            updateSpots();
        }
    }
}


void dxClusterClient::tcpDataReceived()
{
    // readyRead is queued, so the socket may already have been closed.
    if (tcpSocket == nullptr)
        return;

    QString data = QString::fromUtf8(tcpSocket->readAll());
    if (!data.isEmpty() && status == tcpConnectedNoTraffic)
        setTcpStatus(tcpConnectedTraffic);

    emit sendOutput(data);
    tcpReceiveBuffer.append(data);

    QStringList lines = tcpReceiveBuffer.split(QRegularExpression("[\r\n]+"));
    if (!tcpReceiveBuffer.endsWith(QLatin1Char('\n')) && !tcpReceiveBuffer.endsWith(QLatin1Char('\r'))) {
        tcpReceiveBuffer = lines.takeLast();
    } else {
        tcpReceiveBuffer.clear();
    }

    bool parsedSpot = false;
    for (const QString &line : std::as_const(lines)) {
        spotData parsed;
        if (parseTcpSpotLine(line, tcpTimeout, &parsed)) {
            parsedSpot = true;
            spotData* spotDataEntry = new spotData(parsed);
            qDebug(logCluster()) << "Got spot:" << spotDataEntry->dxcall;

#ifdef USESQL
            database db = database();
            db.query(QString("DELETE from spots where dxcall='%1'").arg(spotDataEntry->dxcall));
            QString query = QString("INSERT INTO spots(type,spottercall,frequency,dxcall,comment,timestamp) VALUES('%1','%2',%3,'%4','%5','%6')\n")
                .arg("TCP").arg(spotDataEntry->spottercall).arg(spotDataEntry->frequency).arg(spotDataEntry->dxcall).arg(spotDataEntry->comment).arg(spotDataEntry->timestamp.toString("yyyy-MM-dd hh:mm:ss"));
            db.query(query);
            delete spotDataEntry;
#else
            storeSpot(spotDataEntry);
#endif
        }
    }

    if (parsedSpot) {
        if (!authenticated) {
            authenticated = true;
            if (skimmerSpots)
                enableSkimmerSpots(true);
        }
        updateSpots();
    } else if (!authenticated) {
        if (data.contains("login:", Qt::CaseInsensitive) || data.contains("call:", Qt::CaseInsensitive) || data.contains("callsign:", Qt::CaseInsensitive)) {
            sendTcpData(QString("%1\n").arg(tcpUserName));
            return;
        }
        if (data.contains("password:", Qt::CaseInsensitive)) {
            sendTcpData(QString("%1\n").arg(tcpPassword));
            return;
        }
        if (data.contains("Hello", Qt::CaseInsensitive)) {
            authenticated = true;
            enableSkimmerSpots(skimmerSpots);
        }
    }
}


void dxClusterClient::storeSpot(spotData* spot)
{
    // allSpots holds one entry per callsign. Overwrite an existing entry in place
    // rather than inserting over it, which would leak the old spotData.
    QMap<QString, spotData*>::iterator existing = allSpots.find(spot->dxcall);
    if (existing != allSpots.end()) {
        *existing.value() = *spot;
        delete spot;
    } else {
        allSpots.insert(spot->dxcall, spot);
    }
}

void dxClusterClient::sendTcpData(QString data)
{
    qDebug(logCluster()) << "Sending:" << data;
    if (tcpSocket != nullptr && tcpSocket->isValid() && tcpSocket->isOpen())
    {
        tcpSocket->write(data.toLatin1());
    }
    else
    {
        qInfo(logCluster()) << "socket not open!";
    }
}

void dxClusterClient::tcpCleanup()
{
    bool changed = false;
#ifdef USESQL
    database db = database();
    db.query(QString("DELETE FROM spots where timestamp < datetime('now', '-%1 seconds')").arg(tcpTimeout));
    changed = true;
#else
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QMap<QString, spotData*>::iterator spot = allSpots.begin();
    while (spot != allSpots.end()) {
        // A timeout of 0 means the spot stays until its sender deletes it.
        if (spot.value()->timeout > 0 && spot.value()->timestamp.addSecs(spot.value()->timeout) < now)
        {
            delete spot.value(); // Stop memory leak?
            spot = allSpots.erase(spot);
            changed = true;
        } 
        else
        {
            ++spot;
        }
    }
#endif
    if (changed)
        updateSpots();
}

void dxClusterClient::tcpDisconnected() {
    if (shuttingDown || !tcpEnable || tcpSocket == nullptr)
        return;

    qWarning(logCluster()) << "TCP Cluster server disconnected...";
    emit sendOutput(QString("\nDisconnected from %0 %1\n").arg(connectedServerName).arg(connectedPort));
    closeTcp(false);
    setTcpStatus(tcpNotConnected);

    // Need to start a timer and attempt reconnect.
}

void dxClusterClient::freqRange(quint8 receiver, double low, double high)
{
    freqRanges[receiver] = {low,high};
    if (receiver) {
        lowSubFreq = low;
        highSubFreq = high;
    } else {
        lowMainFreq = low;
        highMainFreq = high;
    }
    updateSpots();
}

void dxClusterClient::updateSpots()
{
    QList<spotData> spots;
#ifdef USESQL
    // Set the required frequency range.
    QString queryText = QString("SELECT * FROM spots WHERE frequency > %1 AND frequency < %2").arg(lowFreq).arg(highFreq);
    //QString queryText = QString("SELECT * FROM spots");
    database db;
    auto query = db.query(queryText);

    while (query.next()) {
        // Step through all current spots within range
        spotData s = spotData();
        s.dxcall = query.value(query.record().indexOf("dxcall")).toString();
        s.frequency = query.value(query.record().indexOf("frequency")).toDouble();
        spots.append(s);
    }
#else
    // Iterate through all available VFO frequency ranges and send all relevant spots.
    QMap<uchar, rangeValues>::iterator range = freqRanges.begin();
    while (range != freqRanges.end())
    {
        spots.clear();
        QMap<QString, spotData*>::iterator mainSpot = allSpots.begin();
        while (mainSpot != allSpots.end())
        {
            if (mainSpot.value()->frequency >= range->low && mainSpot.value()->frequency <= range->high)
            {
                spots.append(**mainSpot);
            }
            ++mainSpot;
        }
        emit sendSpots(range.key(),spots);
        //qDebug(logCluster()) << "Sending" << spots.size() << "DX spots to receiver" << range.key();
        ++range;
    }

#endif

}

void dxClusterClient::enableSkimmerSpots(bool enable)
{
    skimmerSpots = enable;
    if (authenticated) {
        if (skimmerSpots) {
            sendTcpData(QString("set/skimmer\n"));
            sendTcpData(QString("set Dx Filter Skimmer\n"));
        }
        else
        {
            sendTcpData(QString("unset/skimmer\n"));
            //sendTcpData(QString("set Dx Filter Not Skimmer\n"));
        }
        
    }
}
