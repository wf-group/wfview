// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "yaesuserver.h"

#include <QRandomGenerator>
#include <cstddef>
#include <cstring>
#include <utility>

#include "logcategories.h"

namespace {
quint8 codecForYaesuAudio(yaesuAudioFormat format, quint8 channels)
{
    switch (format) {
    case MuLaw:
        return channels == 2 ? quint8(32) : quint8(1);
    case OpusAudio:
        return channels == 2 ? quint8(65) : quint8(64);
    case AdpcmAudio:
        return quint8(128);
    case ShortLE:
    case ShortBE:
    case UnknownAudio:
    default:
        return channels == 2 ? quint8(16) : quint8(4);
    }
}

quint16 frameBytesForCodec(quint8 codec, quint32 sampleRate)
{
    if (sampleRate == 0)
        sampleRate = 16000;

    quint64 bytes = 0;
    switch (codec) {
    case 1:
        bytes = quint64(sampleRate) / 100;
        break;
    case 4:
        bytes = (quint64(sampleRate) * 2) / 100;
        break;
    case 16:
        bytes = (quint64(sampleRate) * 2 * 2) / 100;
        break;
    case 32:
        bytes = (quint64(sampleRate) * 2) / 100;
        break;
    case 64:
    case 65:
    case 128:
        return sizeof(yaesuAudioData::pcmData);
    default:
        return 0;
    }

    return quint16(qBound<quint64>(1, bytes, sizeof(yaesuAudioData::pcmData)));
}

bool packetizedCodec(quint8 codec)
{
    return codec == 64 || codec == 65 || codec == 128;
}
}

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
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    if (d.isEmpty()) {
        return;
    }

    yaesuR2C_CatDataFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.hdr.device = UsbCat;
    frame.hdr.msgtype = R2C_Data;
    frame.hdr.len = sizeof(frame.data.packet_id) + sizeof(frame.data.reserved) + quint16(d.size());
    frame.data.packet_id = ++txCatPacketId;
    memcpy(frame.data.data, d.constData(), qMin(d.size(), int(sizeof(frame.data.data))));

    const qsizetype len = sizeof(frame) - sizeof(frame.data.data) + qMin(d.size(), int(sizeof(frame.data.data)));
    for (auto it = clients.cbegin(); it != clients.cend(); ++it) {
        CLIENT* client = it.value();
        if (client == nullptr || !client->authenticated || client->catPort == 0) {
            continue;
        }
        if (sender != nullptr && memcmp(sender->getGUID(), client->guid, GUIDLEN) && config->rigs.size() > 1) {
            continue;
        }
        sendCatFrame(client->ipAddress, client->catPort, &frame, len);
    }
}

void yaesuServer::receiveScopeData(QByteArray d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    if (d.isEmpty()) {
        return;
    }

    yaesuR2C_ScopeDataFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.hdr.device = Scope;
    frame.hdr.msgtype = R2C_Data;
    frame.hdr.len = sizeof(frame.data);
    memcpy(frame.data, d.constData(), qMin(d.size(), int(sizeof(frame.data))));

    for (auto it = clients.cbegin(); it != clients.cend(); ++it) {
        CLIENT* client = it.value();
        if (client == nullptr || !client->authenticated || client->scopePort == 0) {
            continue;
        }
        if (sender != nullptr && memcmp(sender->getGUID(), client->guid, GUIDLEN) && config->rigs.size() > 1) {
            continue;
        }
        sendScopeFrame(client->ipAddress, client->scopePort, &frame, sizeof(frame));
    }
}

void yaesuServer::receiveAudioData(const audioPacket& d)
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());
    quint8 guid[GUIDLEN];
    if (sender != nullptr) {
        memcpy(guid, sender->getGUID(), GUIDLEN);
    } else {
        memcpy(guid, d.guid, GUIDLEN);
    }

    if (d.data.isEmpty()) {
        return;
    }

    for (auto it = clients.cbegin(); it != clients.cend(); ++it) {
        CLIENT* client = it.value();
        if (client == nullptr || !client->authenticated || client->audioPort == 0) {
            continue;
        }
        if (memcmp(guid, client->guid, GUIDLEN) && config->rigs.size() > 1) {
            continue;
        }

        if (packetizedCodec(client->audioCodec) && d.data.size() > int(sizeof(yaesuAudioData::pcmData))) {
            qWarning(logRigServer()) << "Yaesu server encoded RX audio packet too large"
                                     << d.data.size() << "bytes codec" << client->audioCodec;
            continue;
        }

        int pos = 0;
        while (pos < d.data.size()) {
            const QByteArray partial = packetizedCodec(client->audioCodec)
                                           ? d.data
                                           : d.data.mid(pos, client->audioFrameBytes);
            pos += partial.size();

            yaesuR2C_AudioData frame;
            memset(&frame, 0, sizeof(frame));
            frame.hdr.device = UsbAudio;
            frame.hdr.msgtype = R2C_Data;
            frame.data.seqNum = client->audioSeq++;
            frame.data.channels = client->audioChannels;
            frame.data.format = client->audioFormat;
            frame.data.sampleRate = client->audioSampleRate;
            frame.data.resendMode = 0x01;
            frame.data.playbackLatency = 1500;
            frame.data.recordLatency = 1500;
            frame.data.playbackVol = 100;
            frame.data.recordVol = 100;
            frame.data.pcmDataLen = quint16(partial.size());
            frame.hdr.len = sizeof(frame) - sizeof(frame.hdr) - sizeof(frame.data.pcmData) + frame.data.pcmDataLen;
            memcpy(frame.data.pcmData, partial.constData(), partial.size());

            const qsizetype len = sizeof(frame) - sizeof(frame.data.pcmData) + frame.data.pcmDataLen;
            sendAudioFrame(client->ipAddress, client->audioPort, &frame, len);
            if (packetizedCodec(client->audioCodec))
                break;
        }
    }
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
            CLIENT* client = clients.take(logout->session);
            releaseAudioForClient(client);
            delete client;
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
    case C2R_Close:
    {
        if (decoded.size() >= int(sizeof(yaesuSessionFrame))) {
            const auto* close = reinterpret_cast<const yaesuSessionFrame*>(decoded.constData());
            CLIENT* client = clientForSession(close->session);
            if (client != nullptr) {
                if (header->device == UsbCat) {
                    client->catPort = 0;
                } else if (header->device == UsbAudio) {
                    releaseAudioForClient(client);
                } else if (header->device == Scope) {
                    client->scopePort = 0;
                }
                sendResult(controlSocket, datagram.address, datagram.port, header->device, R2C_CloseReply, 1);
            }
        }
        break;
    }
    case C2R_Data:
    {
        const int minDataSize = int(sizeof(yaesuFrameHeader) + sizeof(quint32));
        if (decoded.size() >= minDataSize) {
            const quint32 session = *reinterpret_cast<const quint32*>(decoded.constData() + sizeof(yaesuFrameHeader));
            CLIENT* client = clientForSession(session);
            const int shortTextOffset = int(sizeof(yaesuFrameHeader) + sizeof(quint32) + sizeof(quint32));
            const QByteArray commandText = decoded.size() > shortTextOffset
                                               ? decoded.mid(shortTextOffset)
                                               : QByteArray();
            if (client != nullptr && commandText.startsWith("putfile catcmd_table")) {
                yaesuControlCommandFrame ready;
                memset(&ready, 0, sizeof(ready));
                ready.hdr.device = ScuLan;
                ready.hdr.msgtype = R2C_Data;
                ready.hdr.len = sizeof(ready) - sizeof(ready.hdr);
                ready.session = session;
                memcpy(ready.text, "Ready", 5);
                sendControlFrame(datagram.address, datagram.port, &ready, sizeof(ready));
            } else if (client != nullptr) {
                yaesuControlCommandFrame receive;
                memset(&receive, 0, sizeof(receive));
                receive.hdr.device = ScuLan;
                receive.hdr.msgtype = R2C_Data;
                receive.hdr.len = sizeof(receive) - sizeof(receive.hdr);
                receive.session = session;
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
    case C2R_Close:
    {
        CLIENT* client = clientForEndpoint(datagram.address, datagram.port, &CLIENT::catPort);
        if (client != nullptr) {
            client->catPort = 0;
        }
        sendResult(catSocket, datagram.address, datagram.port, UsbCat, R2C_CloseReply, 1);
        break;
    }
    case C2R_Data:
    {
        const int minCatDataSize = int(sizeof(yaesuFrameHeader) + sizeof(quint32) +
                                       sizeof(quint16) + sizeof(quint16));
        if (decoded.size() >= minCatDataSize) {
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
                    const QByteArray catData(reinterpret_cast<const char*>(frame->data.data), dataLen);
                    foreach (RIGCONFIG* radio, config->rigs) {
                        if (radio->rig == nullptr) {
                            continue;
                        }
                        if (!memcmp(radio->guid, client->guid, sizeof(radio->guid)) || config->rigs.size() == 1) {
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
                            QMetaObject::invokeMethod(radio->rig, [radio, catData]() {
                                radio->rig->dataFromServer(catData);
                            }, Qt::QueuedConnection);
#else
                            emit haveDataFromServer(catData);
#endif
                        }
                    }
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
        if (decoded.size() >= int(sizeof(yaesuResultFrame))) {
            const auto* connect = reinterpret_cast<const yaesuResultFrame*>(decoded.constData());
            CLIENT* client = clientForSession(connect->result);
            if (client != nullptr) {
                client->audioPort = datagram.port;
            }
        }
        sendResult(audioSocket, datagram.address, datagram.port, UsbAudio, R2C_ConnectReply, 1);
        break;
    case C2R_HealthCheck:
        sendHealth(audioSocket, datagram.address, datagram.port, UsbAudio);
        break;
    case C2R_Close:
    {
        CLIENT* client = clientForEndpoint(datagram.address, datagram.port, &CLIENT::audioPort);
        releaseAudioForClient(client);
        sendResult(audioSocket, datagram.address, datagram.port, UsbAudio, R2C_CloseReply, 1);
        break;
    }
    case C2R_Data:
        if (decoded.size() >= int(sizeof(yaesuFrameHeader) + sizeof(quint32))) {
            const quint32 session = *reinterpret_cast<const quint32*>(decoded.constData() + sizeof(yaesuFrameHeader));
            CLIENT* client = clientForSession(session);
            if (client == nullptr) {
                client = clientForEndpoint(datagram.address, datagram.port, &CLIENT::audioPort);
            }
            if (client != nullptr) {
                client->audioPort = datagram.port;
                const int minAudioDataSize = int(sizeof(yaesuFrameHeader) + sizeof(quint32) +
                                                 offsetof(yaesuAudioData, pcmData));
                if (decoded.size() >= minAudioDataSize) {
                    const auto* frame = reinterpret_cast<const yaesuC2R_AudioData*>(decoded.constData());
                    updateClientAudioFormat(client, frame->data);
                    requestAudioForClient(client);
                    if (frame->data.pcmDataLen > 0) {
                        const quint16 payloadLen = qMin<quint16>(frame->data.pcmDataLen, sizeof(frame->data.pcmData));
                        if (client->txBufferLen == 0) {
                            qDebug(logAudio()) << "Yaesu server received TX audio"
                                               << "codec" << client->audioCodec
                                               << "sampleRate" << client->audioSampleRate
                                               << "channels" << client->audioChannels
                                               << "bytes" << payloadLen;
                        }
                        client->txBufferLen = payloadLen;
                        audioPacket packet;
                        packet.seq = frame->data.seqNum;
                        packet.time = QTime::currentTime();
                        packet.sent = 0;
                        packet.data = QByteArray(reinterpret_cast<const char*>(frame->data.pcmData), payloadLen);
                        memcpy(packet.guid, client->guid, GUIDLEN);
                        packet.sampleRate = client->audioSampleRate;
                        packet.channels = client->audioChannels;
                        packet.codec = client->audioCodec;
                        emit haveAudioData(packet);
                    }
                }
            }
        }
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
        if (decoded.size() >= int(sizeof(yaesuResultFrame))) {
            const auto* connect = reinterpret_cast<const yaesuResultFrame*>(decoded.constData());
            CLIENT* client = clientForSession(connect->result);
            if (client != nullptr) {
                client->scopePort = datagram.port;
            }
        }
        sendResult(scopeSocket, datagram.address, datagram.port, Scope, R2C_ConnectReply, 1);
        break;
    case C2R_HealthCheck:
        sendHealth(scopeSocket, datagram.address, datagram.port, Scope);
        break;
    case C2R_Close:
    {
        CLIENT* client = clientForEndpoint(datagram.address, datagram.port, &CLIENT::scopePort);
        if (client != nullptr) {
            client->scopePort = 0;
        }
        sendResult(scopeSocket, datagram.address, datagram.port, Scope, R2C_CloseReply, 1);
        break;
    }
    default:
        qDebug(logRigServer()) << "Yaesu scope ignored message" << header->msgtype << "device" << header->device;
        break;
    }
}

void yaesuServer::updateClientAudioFormat(CLIENT* client, const yaesuAudioData& data)
{
    if (client == nullptr) {
        return;
    }

    const quint8 channels = data.channels == 2 ? quint8(2) : quint8(1);
    client->audioChannels = channels;
    switch (data.format) {
    case MuLaw:
    case OpusAudio:
    case AdpcmAudio:
        client->audioFormat = data.format;
        break;
    case ShortLE:
    case ShortBE:
    case UnknownAudio:
    default:
        client->audioFormat = ShortLE;
        break;
    }
    client->audioSampleRate = data.sampleRate != 0 ? data.sampleRate : quint32(16000);
    client->audioCodec = codecForYaesuAudio(client->audioFormat, client->audioChannels);
    client->audioFrameBytes = frameBytesForCodec(client->audioCodec, client->audioSampleRate);
}

void yaesuServer::requestAudioForClient(CLIENT* client)
{
    if (client == nullptr || !client->authenticated) {
        return;
    }
    if (client->isStreaming) {
        return;
    }

    client->isStreaming = true;
    emit requestRxAudioForGuid(QByteArray(reinterpret_cast<const char*>(client->guid), GUIDLEN),
                               client->audioCodec,
                               client->audioSampleRate);
}

void yaesuServer::releaseAudioForClient(CLIENT* client)
{
    if (client == nullptr || client->audioPort == 0) {
        return;
    }

    const QByteArray guid(reinterpret_cast<const char*>(client->guid), GUIDLEN);
    client->audioPort = 0;
    client->isStreaming = false;
    if (!hasAudioClientForGuid(reinterpret_cast<const quint8*>(guid.constData()))) {
        emit releaseRxAudioForGuid(guid);
    }
}

bool yaesuServer::hasAudioClientForGuid(const quint8* guid) const
{
    for (CLIENT* client : clients) {
        if (client == nullptr || !client->authenticated || client->audioPort == 0) {
            continue;
        }
        if (guid == nullptr || !memcmp(client->guid, guid, GUIDLEN) || config->rigs.size() == 1) {
            return true;
        }
    }
    return false;
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
        decoded[int(i)] = char(encKey ^ ib ^ frame->encodedData[i]);
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
