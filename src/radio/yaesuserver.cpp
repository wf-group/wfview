// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "yaesuserver.h"

#include <QRandomGenerator>
#include <cstring>
#include <utility>

#include "logcategories.h"

yaesuServer::yaesuServer(SERVERCONFIG* config, rigServer* parent) :
    rigServer(config,parent)
{
    qInfo(logRigServer()) << "Creating instance of Yaesu server";
}

void yaesuServer::init()
{
    connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),this, SLOT(receiveRigCaps(rigCapabilities*)));

    const QHostAddress listenAddress = serverListenAddress();

    controlSocket = new QUdpSocket(this);
    catSocket = new QUdpSocket(this);
    audioSocket = new QUdpSocket(this);
    scopeSocket = new QUdpSocket(this);

    if (!controlSocket->bind(listenAddress, config->controlPort)) {
        qWarning(logRigServer()) << "Yaesu server could not bind control" << listenAddress.toString() << config->controlPort << controlSocket->errorString();
        return;
    }
    if (!catSocket->bind(listenAddress, config->civPort)) {
        qWarning(logRigServer()) << "Yaesu server could not bind CAT" << listenAddress.toString() << config->civPort << catSocket->errorString();
        return;
    }
    if (!audioSocket->bind(listenAddress, config->audioPort)) {
        qWarning(logRigServer()) << "Yaesu server could not bind audio" << listenAddress.toString() << config->audioPort << audioSocket->errorString();
        return;
    }
    if (!scopeSocket->bind(listenAddress, config->scopePort)) {
        qWarning(logRigServer()) << "Yaesu server could not bind scope" << listenAddress.toString() << config->scopePort << scopeSocket->errorString();
        return;
    }

    connect(controlSocket, &QUdpSocket::readyRead, this, &yaesuServer::incomingControl);
    connect(catSocket, &QUdpSocket::readyRead, this, &yaesuServer::incomingCat);
    connect(audioSocket, &QUdpSocket::readyRead, this, &yaesuServer::incomingAudio);
    connect(scopeSocket, &QUdpSocket::readyRead, this, &yaesuServer::incomingScope);

    qInfo(logRigServer()) << "Yaesu server bound control" << listenAddress.toString() << config->controlPort
                          << "CAT" << config->civPort
                          << "audio" << config->audioPort
                          << "scope" << config->scopePort;
}

yaesuServer::~yaesuServer()
{
    qInfo(logRigServer()) << "Closing Yaesu server";
    qDeleteAll(clients);
    clients.clear();
}

void yaesuServer::dataForServer(QByteArray d)
{
    if (d.isEmpty()) {
        return;
    }

    yaesuR2C_CatDataFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.hdr.device = UsbCat;
    frame.hdr.msgtype = R2C_Data;
    frame.hdr.len = sizeof(frame.data.packet_id) + sizeof(frame.data.reserved) + quint16(d.size());
    frame.data.packet_id = txCatPacketId++;
    memcpy(frame.data.data, d.constData(), qMin(d.size(), int(sizeof(frame.data.data))));

    const qsizetype len = sizeof(frame) - sizeof(frame.data.data) + qMin(d.size(), int(sizeof(frame.data.data)));
    for (auto it = clients.cbegin(); it != clients.cend(); ++it) {
        CLIENT* client = it.value();
        if (client == nullptr || !client->authenticated || client->catPort == 0) {
            continue;
        }
        sendCatFrame(client->ipAddress, client->catPort, &frame, len);
    }
}

void yaesuServer::receiveAudioData(const audioPacket& d)
{
    Q_UNUSED(d)
}

void yaesuServer::incomingControl()
{
    while (controlSocket->hasPendingDatagrams()) {
        PendingDatagram datagram;
        datagram.payload.resize(int(controlSocket->pendingDatagramSize()));
        controlSocket->readDatagram(datagram.payload.data(), datagram.payload.size(), &datagram.address, &datagram.port);
        processControl(datagram);
    }
}

void yaesuServer::incomingCat()
{
    while (catSocket->hasPendingDatagrams()) {
        PendingDatagram datagram;
        datagram.payload.resize(int(catSocket->pendingDatagramSize()));
        catSocket->readDatagram(datagram.payload.data(), datagram.payload.size(), &datagram.address, &datagram.port);
        processCat(datagram);
    }
}

void yaesuServer::incomingAudio()
{
    while (audioSocket->hasPendingDatagrams()) {
        PendingDatagram datagram;
        datagram.payload.resize(int(audioSocket->pendingDatagramSize()));
        audioSocket->readDatagram(datagram.payload.data(), datagram.payload.size(), &datagram.address, &datagram.port);
        processAudio(datagram);
    }
}

void yaesuServer::incomingScope()
{
    while (scopeSocket->hasPendingDatagrams()) {
        PendingDatagram datagram;
        datagram.payload.resize(int(scopeSocket->pendingDatagramSize()));
        scopeSocket->readDatagram(datagram.payload.data(), datagram.payload.size(), &datagram.address, &datagram.port);
        processScope(datagram);
    }
}

void yaesuServer::processControl(const PendingDatagram& datagram)
{
    const QByteArray decoded = decodeFrame(datagram.payload);
    if (decoded.size() < int(sizeof(yaesuFrameHeader))) {
        return;
    }

    const auto* header = reinterpret_cast<const yaesuFrameHeader*>(decoded.constData());
    switch (header->msgtype) {
    case C2R_Login:
    {
        if (decoded.size() < int(sizeof(yaesuC2R_LoginFrame)) ||
            !authenticate(reinterpret_cast<const yaesuC2R_LoginFrame*>(decoded.constData()))) {
            qInfo(logRigServer()) << "Yaesu server rejected login from" << datagram.address.toString();
            return;
        }

        CLIENT* client = new CLIENT();
        client->connected = true;
        client->authenticated = true;
        client->ipAddress = datagram.address;
        client->port = datagram.port;
        client->timeConnected = QDateTime::currentDateTime();
        client->lastHeard = client->timeConnected;
        memset(client->guid, 0, GUIDLEN);
        if (!config->rigs.isEmpty()) {
            memcpy(client->guid, config->rigs.first()->guid, GUIDLEN);
        }

        quint32 session = nextSession++;
        if (session == 0) {
            session = nextSession++;
        }
        clients.insert(session, client);

        yaesuR2C_LoginReplyFrame reply;
        memset(&reply, 0, sizeof(reply));
        reply.hdr.device = ScuLan;
        reply.hdr.msgtype = R2C_LoginReply;
        reply.hdr.len = sizeof(reply) - sizeof(reply.hdr);
        reply.session = session;
        reply.vala = 1;
        reply.valb = 1;
        reply.valc = 1;
        sendControlFrame(datagram.address, datagram.port, &reply, sizeof(reply));
        qInfo(logRigServer()) << "Yaesu server login accepted from" << datagram.address.toString() << "session" << session;
        break;
    }
    case C2R_Logout:
    {
        if (decoded.size() >= int(sizeof(yaesuC2R_LogoutFrame))) {
            const auto* logout = reinterpret_cast<const yaesuC2R_LogoutFrame*>(decoded.constData());
            delete clients.take(logout->session);
            sendResult(controlSocket, datagram.address, datagram.port, ScuLan, R2C_LogoutReply, 1);
        }
        break;
    }
    case C2R_Open:
    {
        if (decoded.size() >= int(sizeof(yaesuC2R_DeviceOpenFrame))) {
            const auto* open = reinterpret_cast<const yaesuC2R_DeviceOpenFrame*>(decoded.constData());
            CLIENT* client = clientForSession(open->session);
            if (client != nullptr) {
                if (header->device == UsbCat) {
                    client->catPort = open->netPort;
                } else if (header->device == UsbAudio) {
                    client->audioPort = open->netPort;
                } else if (header->device == Scope) {
                    client->scopePort = open->netPort;
                }
                sendResult(controlSocket, datagram.address, datagram.port, header->device, R2C_OpenReply, 1);
            }
        }
        break;
    }
    case C2R_Data:
    {
        if (decoded.size() >= int(sizeof(yaesuControlCommandFrame))) {
            const auto* command = reinterpret_cast<const yaesuControlCommandFrame*>(decoded.constData());
            CLIENT* client = clientForSession(command->session);
            if (client != nullptr && !strncmp(command->text, "putfile catcmd_table", 20)) {
                yaesuControlCommandFrame ready;
                memset(&ready, 0, sizeof(ready));
                ready.hdr.device = ScuLan;
                ready.hdr.msgtype = R2C_Data;
                ready.hdr.len = sizeof(ready) - sizeof(ready.hdr);
                ready.session = command->session;
                memcpy(ready.text, "Ready", 5);
                sendControlFrame(datagram.address, datagram.port, &ready, sizeof(ready));
            } else if (client != nullptr) {
                yaesuControlCommandFrame receive;
                memset(&receive, 0, sizeof(receive));
                receive.hdr.device = ScuLan;
                receive.hdr.msgtype = R2C_Data;
                receive.hdr.len = sizeof(receive) - sizeof(receive.hdr);
                receive.session = command->session;
                memcpy(receive.text, "Receive", 7);
                sendControlFrame(datagram.address, datagram.port, &receive, sizeof(receive));
            }
        }
        break;
    }
    case C2R_HealthCheck:
        sendHealth(controlSocket, datagram.address, datagram.port, ScuLan);
        break;
    default:
        qDebug(logRigServer()) << "Yaesu control ignored message" << header->msgtype << "device" << header->device;
        break;
    }
}

void yaesuServer::processCat(const PendingDatagram& datagram)
{
    const QByteArray decoded = decodeFrame(datagram.payload);
    if (decoded.size() < int(sizeof(yaesuFrameHeader))) {
        return;
    }

    const auto* header = reinterpret_cast<const yaesuFrameHeader*>(decoded.constData());
    switch (header->msgtype) {
    case C2R_Connect:
    {
        if (decoded.size() >= int(sizeof(yaesuResultFrame))) {
            auto* connect = reinterpret_cast<const yaesuResultFrame*>(decoded.constData());
            CLIENT* client = clientForSession(connect->result);
            if (client != nullptr) {
                client->catPort = datagram.port;
                sendResult(catSocket, datagram.address, datagram.port, UsbCat, R2C_ConnectReply, 1);
            }
        }
        break;
    }
    case C2R_CatRate:
        break;
    case C2R_Data:
    {
        if (decoded.size() >= int(sizeof(yaesuC2R_CatDataFrame))) {
            const auto* frame = reinterpret_cast<const yaesuC2R_CatDataFrame*>(decoded.constData());
            CLIENT* client = clientForSession(frame->session);
            if (client == nullptr) {
                client = clientForEndpoint(datagram.address, datagram.port, &CLIENT::catPort);
            }
            if (client != nullptr) {
                client->catPort = datagram.port;
                const qsizetype dataLen = decoded.size() - qsizetype(sizeof(frame->hdr)) - qsizetype(sizeof(frame->session)) -
                                          qsizetype(sizeof(frame->data.packet_id)) - qsizetype(sizeof(frame->data.reserved));
                if (dataLen > 0) {
                    emit haveDataFromServer(QByteArray(reinterpret_cast<const char*>(frame->data.data), dataLen));
                }
            }
        }
        break;
    }
    case C2R_HealthCheck:
        sendHealth(catSocket, datagram.address, datagram.port, UsbCat);
        break;
    default:
        qDebug(logRigServer()) << "Yaesu CAT ignored message" << header->msgtype << "device" << header->device;
        break;
    }
}

void yaesuServer::processAudio(const PendingDatagram& datagram)
{
    const QByteArray decoded = decodeFrame(datagram.payload);
    if (decoded.size() < int(sizeof(yaesuFrameHeader))) {
        return;
    }

    const auto* header = reinterpret_cast<const yaesuFrameHeader*>(decoded.constData());
    switch (header->msgtype) {
    case C2R_Connect:
        sendResult(audioSocket, datagram.address, datagram.port, UsbAudio, R2C_ConnectReply, 1);
        break;
    case C2R_HealthCheck:
        sendHealth(audioSocket, datagram.address, datagram.port, UsbAudio);
        break;
    case C2R_Data:
        break;
    default:
        qDebug(logRigServer()) << "Yaesu audio ignored message" << header->msgtype << "device" << header->device;
        break;
    }
}

void yaesuServer::processScope(const PendingDatagram& datagram)
{
    const QByteArray decoded = decodeFrame(datagram.payload);
    if (decoded.size() < int(sizeof(yaesuFrameHeader))) {
        return;
    }

    const auto* header = reinterpret_cast<const yaesuFrameHeader*>(decoded.constData());
    switch (header->msgtype) {
    case C2R_Connect:
        sendResult(scopeSocket, datagram.address, datagram.port, Scope, R2C_ConnectReply, 1);
        break;
    case C2R_HealthCheck:
        sendHealth(scopeSocket, datagram.address, datagram.port, Scope);
        break;
    default:
        qDebug(logRigServer()) << "Yaesu scope ignored message" << header->msgtype << "device" << header->device;
        break;
    }
}

bool yaesuServer::authenticate(const yaesuC2R_LoginFrame* login) const
{
    const auto boundedLength = [](const char* text, size_t maxLen) -> int {
        const void* end = memchr(text, '\0', maxLen);
        return int(end == nullptr ? maxLen : static_cast<const char*>(end) - text);
    };
    const QString username = QString::fromLatin1(login->username, boundedLength(login->username, sizeof(login->username))).trimmed();
    const QString password = QString::fromLatin1(login->pw, boundedLength(login->pw, sizeof(login->pw)));
    QByteArray encodedPassword;
    passcode(password, encodedPassword);

    for (const SERVERUSER& user : config->users) {
        if (user.username == username && (user.password == password || user.password == encodedPassword)) {
            return true;
        }
    }
    return config->users.isEmpty();
}

yaesuServer::CLIENT* yaesuServer::clientForSession(quint32 session)
{
    return clients.value(session, nullptr);
}

yaesuServer::CLIENT* yaesuServer::clientForEndpoint(const QHostAddress& address, quint16 port, quint16 CLIENT::*memberPort)
{
    for (CLIENT* client : std::as_const(clients)) {
        if (client != nullptr && client->ipAddress == address && (client->*memberPort) == port) {
            return client;
        }
    }
    return nullptr;
}

void yaesuServer::sendControlFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len)
{
    sendFrame(controlSocket, address, port, data, len);
}

void yaesuServer::sendCatFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len)
{
    sendFrame(catSocket, address, port, data, len);
}

void yaesuServer::sendAudioFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len)
{
    sendFrame(audioSocket, address, port, data, len);
}

void yaesuServer::sendScopeFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len)
{
    sendFrame(scopeSocket, address, port, data, len);
}

void yaesuServer::sendFrame(QUdpSocket* socket, const QHostAddress& address, quint16 port, const void* data, qsizetype len)
{
    if (socket == nullptr || address.isNull() || port == 0 || data == nullptr || len <= 0) {
        return;
    }
    const QByteArray encoded = encodeFrame(data, len);
    socket->writeDatagram(encoded, address, port);
}

QByteArray yaesuServer::decodeFrame(const QByteArray& encoded) const
{
    if (encoded.size() < 8) {
        return {};
    }

    const auto* frame = reinterpret_cast<const yaesuEncodedFrame*>(encoded.constData());
    if (frame->type != 0x5a5a || frame->encodingType != 0x100) {
        return {};
    }

    const qsizetype dataLen = encoded.size() - 8;
    if (dataLen > YAESU_MAX_FRAME_SIZE) {
        return {};
    }

    QByteArray decoded;
    decoded.resize(dataLen);
    const quint8 encKey = frame->firstKey ^ frame->secondKey;
    for (qsizetype i = 0; i < dataLen; ++i) {
        quint8 ib = quint8(i);
        ib = ~ib + 1;
        decoded[i] = char(encKey ^ ib ^ frame->encodedData[i]);
    }
    return decoded;
}

QByteArray yaesuServer::encodeFrame(const void* data, qsizetype len) const
{
    if (data == nullptr || len <= 0 || len > YAESU_MAX_FRAME_SIZE) {
        return {};
    }

    QByteArray encoded;
    encoded.resize(int(len + 8));
    auto* frame = reinterpret_cast<yaesuEncodedFrame*>(encoded.data());
    frame->type = 0x5a5a;
    frame->encodingType = 0x100;
    frame->firstKey = quint8(QRandomGenerator::global()->bounded(0x100));
    frame->secondKey = quint8(QRandomGenerator::global()->bounded(0x100));
    frame->length = quint16(len + 4);
    const quint8 encKey = frame->firstKey ^ frame->secondKey;
    const auto* raw = reinterpret_cast<const quint8*>(data);
    for (qsizetype i = 0; i < len; ++i) {
        quint8 ib = quint8(i);
        ib = ~ib + 1;
        frame->encodedData[i] = encKey ^ ib ^ raw[i];
    }
    return encoded;
}

void yaesuServer::sendResult(QUdpSocket* socket, const QHostAddress& address, quint16 port, yaesuDeviceId device, yaesuMessageId message, quint32 result)
{
    yaesuResultFrame reply;
    memset(&reply, 0, sizeof(reply));
    reply.hdr.device = device;
    reply.hdr.msgtype = message;
    reply.hdr.len = sizeof(reply) - sizeof(reply.hdr);
    reply.result = result;
    sendFrame(socket, address, port, &reply, sizeof(reply));
}

void yaesuServer::sendHealth(QUdpSocket* socket, const QHostAddress& address, quint16 port, yaesuDeviceId device)
{
    yaesuR2C_HeartbeatFrame reply;
    memset(&reply, 0, sizeof(reply));
    reply.hdr.device = device;
    reply.hdr.msgtype = R2C_HealthReply;
    reply.hdr.len = sizeof(reply) - sizeof(reply.hdr);
    reply.result = 1;
    sendFrame(socket, address, port, &reply, sizeof(reply));
}
