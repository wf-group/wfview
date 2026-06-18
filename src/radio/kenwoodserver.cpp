// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "kenwoodserver.h"
#include "logcategories.h"

kenwoodServer::kenwoodServer(SERVERCONFIG* config, rigServer* parent) :
    rigServer(config,parent)
{
    qInfo(logRigServer()) << "Creating instance of Kenwood server";
}

void kenwoodServer::init()
{
    connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),this, SLOT(receiveRigCaps(rigCapabilities*)));

    server = new QTcpServer(this);

    const QHostAddress listenAddress = serverListenAddress();
    if (!server->listen(listenAddress, config->controlPort)) {
        qInfo(logRigServer()) << "could not start on" << listenAddress.toString() << "port" << config->controlPort;
        return;
    }
    else
    {
        qInfo(logRigServer()) << "started on" << listenAddress.toString() << "port" << config->controlPort;
    }
    connect(server, &QTcpServer::newConnection, this, &kenwoodServer::newConnection);
}

kenwoodServer::~kenwoodServer()
{
    // We should probably step through all connected clients (just in case)

    qInfo(logRigServer()) << "Stopping audio streaming";
    foreach (RIGCONFIG* radio, config->rigs)
    {
        if (radio->rtpThread != nullptr) {
            radio->rtpThread->quit();
            radio->rtpThread->wait();
            radio->rtp = nullptr;
            radio->rtpThread = nullptr;
        }
    }

    /*
    for (auto it = clients.begin(); it != clients.end(); it++)
    {
        it.key()->close();
        delete it.key();
        delete it.value();
    }

    if (server != nullptr)
    {
        server->close();
    }

    */

}

void kenwoodServer::dataForServer(QByteArray d)
{
    kenwoodCommander* sender = qobject_cast<kenwoodCommander*>(QObject::sender());
    QByteArray guid(GUIDLEN, 0);

    if (sender == nullptr)
    {
        qInfo(logRigServer()) << "Object type didn't match!";
        //return;
    } else {
        guid = QByteArray(reinterpret_cast<const char*>(sender->getGUID()), GUIDLEN);
    }

    //qInfo(logRigServer()) << "Radio reply:" << d;
    const QList<QByteArray> replies = translateRigDataForClient(d, guid);
    if (replies.isEmpty()) {
        return;
    }

    for (auto it = clients.begin(); it != clients.end(); it++)
    {
        QTcpSocket* socket = it.key();
        CLIENT* client = it.value();
        if (socket != nullptr && client != nullptr && socket->isOpen() && client->connected && client->authenticated)
        {
            for (const QByteArray& reply: replies) {
                socket->write(reply);
            }
        }
    }

    return;
}


void kenwoodServer::receiveAudioData(const audioPacket& d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());
    quint8 guid[GUIDLEN];
    if (sender != nullptr)
    {
        memcpy(guid, sender->getGUID(), GUIDLEN);
    }
    else {
        memcpy(guid, d.guid, GUIDLEN);
    }


    return;
}

void kenwoodServer::receiveScopeData(QByteArray d)
{
    Q_UNUSED(d)
    // Kenwood scope data is carried on the main TCP/CAT stream, so dataForServer()
    // handles DD2/DD3 translation and fan-out.
}

void kenwoodServer::newConnection()
{
    // A new client has connected!
    CLIENT* current = new CLIENT();
    QTcpSocket* socket = server->nextPendingConnection();
    connect(socket, &QTcpSocket::disconnected, socket, [this,socket] { disconnected(socket);});
    connect(socket, &QTcpSocket::readyRead, socket, [this,socket] { readyRead(socket);});

    current->ipAddress = socket->peerAddress();

    clients.insert(socket,current);
    qInfo(logRigServer()) << "Client connected!" << socket->peerAddress() << ":" << socket->peerPort();
}

void kenwoodServer::disconnected(QTcpSocket* socket)
{
    // Client has disconnected!
    qInfo(logRigServer()) << "Client disconnected!" << socket->peerAddress() << ":" << socket->peerPort();
    auto it = clients.find(socket);
    if (it != clients.end()) {
        stopAudioForClient(it.value());
    }
    if (socket->isOpen())
        socket->close();
    clients.remove(socket);
}

void kenwoodServer::stopAudioForClient(CLIENT* client)
{
    if (client != nullptr) {
        client->isStreaming = false;
    }

    foreach (RIGCONFIG* radio, config->rigs)
    {
        if (radio->rtpThread != nullptr) {
            radio->rtpThread->quit();
            radio->rtpThread->wait();
            radio->rtp = nullptr;
            radio->rtpThread = nullptr;
        }
    }
}

bool kenwoodServer::authenticateClient(QTcpSocket* socket, CLIENT* c, const QByteArray& command)
{
    if (command.size() < 9) {
        qInfo(logRigServer()) << "Invalid Kenwood login command received" << command;
        socket->write(QString("##ID0;").toLatin1());
        return false;
    }

    uchar type = command.mid(4,1).toShort();
    uchar userlen = command.mid(5,2).toUShort();
    uchar passlen = command.mid(7,2).toUShort();
    if (command.size() < 9 + userlen + passlen) {
        qInfo(logRigServer()) << "Incomplete Kenwood login command received" << command;
        socket->write(QString("##ID0;").toLatin1());
        return false;
    }

    QString username = command.mid(9,userlen);
    QString password = command.mid(9+userlen,passlen);
    QByteArray passcomp;
    passcode(password, passcomp);
    qInfo(logRigServer()) << c->ipAddress.toString() << ": Received 'login'";
    foreach(SERVERUSER user, config->users)
    {
        if (user.username == username && user.password == passcomp)
        {
            c->authenticated = true;
            c->user = username;
            // Usertypes 0=admin, 1=user, 2=rx only.
            if (user.userType == 0 && type == 0)
            {
                c->admin = true;
                c->tx = true;
            } else if (user.userType == 1 && type == 1)
            {
                c->admin=false;
                c->tx=true;
            }
            else if (user.userType == 2 && type == 1)
            {
                c->admin=false;
                c->tx=false;
            } else
            {
                qInfo(logRigServer()) << "User type did not match stored type";
                c->authenticated = false;
                socket->write(QString("##ID0;").toLatin1());
                return false;
            }
            qInfo(logRigServer()) << "User" << username << "logged-in successfully";
            socket->write(QString("##ID1;##UE1;##TI%0;").arg(uchar(c->tx)).toLatin1());
            return true;
        }
    }

    qInfo(logRigServer()) << "User" << username << "invalid password";
    socket->write(QString("##ID0;").toLatin1());
    return false;
}

bool kenwoodServer::handleServerCommand(QTcpSocket* socket, CLIENT* c, const QByteArray& command)
{
    if (command == "##CN" || command.startsWith("##CN")) {
        socket->write(QString("##CN1;").toLatin1());
        c->connected = true;
        return true;
    }

    if (command.startsWith("##ID")) {
        if (c->authenticated) {
            socket->write(QString("##ID1;##UE1;##TI%0;").arg(uchar(c->tx)).toLatin1());
            return true;
        }

        if (authenticateClient(socket, c, command)) {
            return true;
        }

        return true;
    }

    if (command.startsWith("##UE")) {
        socket->write(QString("##UE1;").toLatin1());
        return true;
    }

    if (command.startsWith("##TI")) {
        socket->write(QString("##TI%0;").arg(uchar(c->tx)).toLatin1());
        return true;
    }

    if (command.startsWith("##KN30")) {
        if (command.size() > 6) {
            socket->write(command + ";");
        } else {
            socket->write(QString("##KN30000;").toLatin1());
        }
        return true;
    }

    return false;
}

RIGCONFIG* kenwoodServer::radioForClient(const CLIENT* client) const
{
    if (config->rigs.size() == 1) {
        return config->rigs.first();
    }

    if (client == nullptr) {
        return nullptr;
    }

    foreach (RIGCONFIG* radio, config->rigs) {
        if (radio != nullptr && !memcmp(radio->guid, client->guid, GUIDLEN)) {
            return radio;
        }
    }

    return nullptr;
}

bool kenwoodServer::translateNetworkCommandForRig(const CLIENT* client, QByteArray& command) const
{
    RIGCONFIG* radio = radioForClient(client);
    if (radio == nullptr || radio->rigCaps == nullptr) {
        if (command.startsWith("##") || command.startsWith("DD")) {
            qDebug(logRigServer()) << "Dropping Kenwood command because rigCaps are not available" << command;
            return false;
        }
        return true;
    }

    if (!radio->rigCaps->hasSpectrum &&
        (command.startsWith("##DD2") || command.startsWith("##DD3") ||
         command.startsWith("DD0") || command.startsWith("DD1") ||
         command.startsWith("DD2") || command.startsWith("DD3") || command.startsWith("DD4"))) {
        qDebug(logRigServer()) << "Dropping Kenwood scope command for non-scope rig" << command;
        return false;
    }

    if (!config->lan) {
        if (command.startsWith("DD0") && command.size() > 3) {
            const char value = command.at(3);
            if (value == '1' || value == '2' || value == '3') {
                command = "DD04";
            }
            return true;
        }

        if (command.startsWith("DD1") && command.size() > 3) {
            const char value = command.at(3);
            if (value == '1') {
                command = "DD12";
            }
            return true;
        }
    }

    if (!command.startsWith("##")) {
        return true;
    }

    // wfview clients ask for LAN scope data as ##DD2, but a locally attached TS-890S
    // expects the USB/serial CAT form DD2. Only forward it when the attached rig
    // actually has scope support.
    if (command.startsWith("##DD2")) {
        if (!config->lan) {
            command.remove(0, 2);
        }
        return true;
    }

    if (command.startsWith("##DD3")) {
        if (!config->lan) {
            command.remove(0, 2);
        }
        return true;
    }

    qDebug(logRigServer()) << "Dropping unhandled Kenwood network command" << command;
    return false;
}

QList<QByteArray> kenwoodServer::translateRigDataForClient(const QByteArray& data, const QByteArray& guid)
{
    QList<QByteArray> replies;
    QByteArray& buffer = rigDataBuffers[guid];
    buffer.append(data);

    int commandEnd = buffer.indexOf(';');
    while (commandEnd >= 0) {
        QByteArray command = buffer.left(commandEnd).trimmed();
        buffer.remove(0, commandEnd + 1);
        commandEnd = buffer.indexOf(';');

        if (command.isEmpty()) {
            continue;
        }

        QByteArray assembled;
        if (!config->lan && assembleScopeCommand(bandscopeBuffers, guid, command, "##DD2", 32, 40, 1280, assembled)) {
            if (!assembled.isEmpty()) {
                replies.append(assembled);
            }
            continue;
        }

        if (!config->lan && assembleScopeCommand(filterScopeBuffers, guid, command, "##DD3", 12, 38, 426, assembled)) {
            if (!assembled.isEmpty()) {
                replies.append(assembled);
            }
            continue;
        }

        replies.append(command + ";");
    }

    return replies;
}

bool kenwoodServer::assembleScopeCommand(QMap<QByteArray, QByteArray>& buffers, const QByteArray& guid,
                                         const QByteArray& command, const QByteArray& lanCommand,
                                         int chunks, int chunkDigits, int lanDigits, QByteArray& output)
{
    const QByteArray usbCommand = lanCommand.mid(2);
    if (!command.startsWith(usbCommand)) {
        return false;
    }

    const int prefixLen = usbCommand.size();
    if (command.size() < prefixLen + 2) {
        return true;
    }

    bool ok = false;
    const int split = command.mid(prefixLen, 2).toInt(&ok);
    if (!ok || split < 0 || split >= chunks) {
        return true;
    }

    const QByteArray chunk = command.mid(prefixLen + 2);
    const int offset = split * chunkDigits;
    const int copyLen = qMin(chunk.size(), lanDigits - offset);
    if (copyLen <= 0) {
        return true;
    }

    QByteArray& buffer = buffers[guid];
    if (split == 0 || buffer.size() != lanDigits) {
        buffer = QByteArray(lanDigits, '0');
    }

    memcpy(buffer.data() + offset, chunk.constData(), size_t(copyLen));
    if (split == chunks - 1) {
        output = lanCommand + buffer.left(lanDigits) + ";";
        buffer.clear();
    }

    return true;
}

void kenwoodServer::readyRead(QTcpSocket* socket)
{
    // Data from client
    auto it = clients.find(socket);
    if (it == clients.end())
    {
        qWarning(logRigServer()) << "Client not found, this shouldn't happen!";
        disconnected(socket);
        return;
    }

    CLIENT *c = it.value();
    c->buffer.append(socket->readAll());

    c->lastHeard = QDateTime::currentDateTime();

    int commandEnd = c->buffer.indexOf(';');
    while (commandEnd >= 0)
    {
        QByteArray d = c->buffer.left(commandEnd).trimmed();
        c->buffer.remove(0, commandEnd + 1);
        commandEnd = c->buffer.indexOf(';');

        if (d.isEmpty())
            continue;

        // We have at least one full command, so process it
        if (!c->connected)
        {
            if (!handleServerCommand(socket, c, d))
            {
                disconnected(socket);
                return;
            }
            continue;
        } else if (!c->authenticated) {
            if (!handleServerCommand(socket, c, d) || !c->authenticated) {
                qInfo(logRigServer()) << "Invalid command received" << d;
                disconnected(socket);
                return;
            }
            continue;
        }

        if (handleServerCommand(socket, c, d)) {
            continue;
        }

            // If we got here then user is both connected and authenticated.
            if (d.startsWith("##VP")) {
                //User is requesting voip (audio) control
                uchar vp = uchar(d.mid(4,1).toUShort());
                foreach (RIGCONFIG* radio, config->rigs)
                {
                    if ((!memcmp(radio->guid, c->guid, GUIDLEN) || config->rigs.size()==1) && radio->rtp == nullptr && !config->lan)
                    {
                        if (!c->isStreaming && vp)
                        {
                            if (vp==1) {
                                qInfo(logRigServer()) << "Starting high quality audio streaming";
                                radio->txAudioSetup.codec = 4; //c->txCodec;
                                radio->txAudioSetup.sampleRate= 16000; //c->txSampleRate;
                                radio->rxAudioSetup.codec = 4; //c->rxCodec;
                                radio->rxAudioSetup.sampleRate = 16000; //c->rxSampleRate;
                            } else if (vp==2){
                                qInfo(logRigServer()) << "Starting low quality audio streaming";
                                radio->txAudioSetup.codec = 128; //c->txCodec;
                                radio->txAudioSetup.sampleRate= 16000; //c->txSampleRate;
                                radio->rxAudioSetup.codec = 128; //c->rxCodec;
                                radio->rxAudioSetup.sampleRate = 16000; //c->rxSampleRate;
                            } else if (vp==3){
                                qInfo(logRigServer()) << "Starting Opus audio streaming";
                                radio->txAudioSetup.codec = 64; //c->txCodec;
                                radio->txAudioSetup.sampleRate= 16000; //c->txSampleRate;
                                radio->rxAudioSetup.codec = 64; //c->rxCodec;
                                radio->rxAudioSetup.sampleRate = 16000; //c->rxSampleRate;
                            }
                            // This is the first client, so stop running the queue.
                            radio->queueInterval = queue->interval();
                            if (config->disableUI)
                            {
                                queue->interval(-1);
                            }

                            radio->txAudioSetup.isinput = false;
                            radio->txAudioSetup.latency = 150; //c->txBufferLen;

                            radio->rxAudioSetup.latency = 150; //c->txBufferLen;
                            radio->rxAudioSetup.isinput = true;
                            memcpy(radio->rxAudioSetup.guid, radio->guid, GUIDLEN);

                            outAudio.isinput = false;
                            radio->rtp = new rtpAudio(c->ipAddress.toString(),config->audioPort,radio->txAudioSetup, radio->rxAudioSetup);
                            radio->rtpThread = new QThread(this);
                            radio->rtpThread->setObjectName("RTP()");
                            radio->rtp->moveToThread(radio->rtpThread);
                            connect(this, SIGNAL(initRtpAudio()), radio->rtp, SLOT(init()));
                            connect(radio->rtpThread, SIGNAL(finished()), radio->rtp, SLOT(deleteLater()));
                            //connect(this, SIGNAL(haveChangeLatency(quint16)), radio->rtp, SLOT(changeLatency(quint16)));
                            //connect(this, SIGNAL(haveSetVolume(quint8)), radio->rtp, SLOT(setVolume(quint8)));
                            // Audio from UDP
                            //connect(radio->rtp, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
                            //QObject::connect(radio->rtp, SIGNAL(haveOutLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getOutLevels(quint16, quint16, quint16, quint16, bool, bool)));
                            //QObject::connect(radio->rtp, SIGNAL(haveInLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getInLevels(quint16, quint16, quint16, quint16, bool, bool)));
                            radio->rtpThread->start(QThread::TimeCriticalPriority);
                            emit initRtpAudio();
                            c->isStreaming = true;
                        }
                        else
                        {
                            qInfo(logRigServer()) << "Stopping audio streaming";
                            stopAudioForClient(c);
                        }
                        socket->write(QString("##VP%0;").arg(uchar(vp)).toLatin1());

                    }
                }
            } else {
                // I think all other commands need to be forwarded to the radio?
                // We have removed the semicolon, so add it back.
                if (!translateNetworkCommandForRig(c, d)) {
                    continue;
                }
                d.append(QString(";").toLatin1());
                //qInfo(logRigServer()) << "Sending:" << d;
                emit haveDataFromServer(d);
            }
    }
}
