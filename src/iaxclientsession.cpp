#include "iaxclientsession.h"

#include <QCryptographicHash>
#include <QHostInfo>
#include <QRandomGenerator>
#include <QtEndian>

#include <cstring>

namespace {
constexpr int fullFrameHeaderSize = 12;
constexpr int miniFrameHeaderSize = 4;
constexpr int retransmitMs = 500;
constexpr int maxRetries = 6;
constexpr int maxDatagramsPerReadPass = 64;
constexpr quint32 ulawFormat = 0x00000004;
constexpr quint8 controlAnswer = 4;
constexpr auto applicationPayloadPrefix = "WFVIEW-DATA:";
}

QByteArray iax::encodeFullFrame(const Frame &frame)
{
    QByteArray out;
    out.resize(fullFrameHeaderSize + frame.payload.size());
    char *data = out.data();
    qToBigEndian<quint16>(quint16(frame.sourceCall | 0x8000), data);
    qToBigEndian<quint16>(frame.destCall, data + 2);
    qToBigEndian<quint32>(frame.timestamp, data + 4);
    data[8] = char(frame.outgoingSeq);
    data[9] = char(frame.incomingSeq);
    data[10] = char(frame.type);
    data[11] = char(frame.subclass);
    if (!frame.payload.isEmpty()) {
        memcpy(data + fullFrameHeaderSize, frame.payload.constData(), size_t(frame.payload.size()));
    }
    return out;
}

QByteArray iax::encodeMiniFrame(quint16 sourceCall, quint16 timestamp, const QByteArray &payload)
{
    QByteArray out;
    out.resize(miniFrameHeaderSize + payload.size());
    char *data = out.data();
    qToBigEndian<quint16>(quint16(sourceCall & 0x7fff), data);
    qToBigEndian<quint16>(timestamp, data + 2);
    if (!payload.isEmpty()) {
        memcpy(data + miniFrameHeaderSize, payload.constData(), size_t(payload.size()));
    }
    return out;
}

bool iax::decodeFrame(const QByteArray &packet, Frame *frame)
{
    if (packet.size() < miniFrameHeaderSize || frame == nullptr) {
        return false;
    }

    const quint16 source = qFromBigEndian<quint16>(packet.constData());
    if ((source & 0x8000) == 0) {
        frame->kind = FrameKind::Mini;
        frame->sourceCall = source & 0x7fff;
        frame->timestamp = qFromBigEndian<quint16>(packet.constData() + 2);
        frame->payload = packet.mid(miniFrameHeaderSize);
        return true;
    }

    if (packet.size() < fullFrameHeaderSize) {
        return false;
    }

    frame->kind = FrameKind::Full;
    frame->sourceCall = source & 0x7fff;
    frame->destCall = qFromBigEndian<quint16>(packet.constData() + 2) & 0x7fff;
    frame->timestamp = qFromBigEndian<quint32>(packet.constData() + 4);
    frame->outgoingSeq = quint8(packet[8]);
    frame->incomingSeq = quint8(packet[9]);
    frame->type = FrameType(quint8(packet[10]));
    frame->subclass = quint8(packet[11]);
    frame->payload = packet.mid(fullFrameHeaderSize);
    return true;
}

QByteArray iax::encodeInformationElement(InformationElement element, const QByteArray &value)
{
    QByteArray out;
    out.reserve(value.size() + 2);
    out.append(char(element));
    out.append(char(value.size()));
    out.append(value);
    return out;
}

QByteArray iax::encodeInformationElementString(InformationElement element, const QString &value)
{
    return encodeInformationElement(element, value.toUtf8());
}

QByteArray iax::encodeInformationElementUShort(InformationElement element, quint16 value)
{
    QByteArray data;
    data.resize(2);
    qToBigEndian<quint16>(value, data.data());
    return encodeInformationElement(element, data);
}

QByteArray iax::encodeInformationElementUInt(InformationElement element, quint32 value)
{
    QByteArray data;
    data.resize(4);
    qToBigEndian<quint32>(value, data.data());
    return encodeInformationElement(element, data);
}

QMap<iax::InformationElement, QByteArray> iax::decodeInformationElements(const QByteArray &payload)
{
    QMap<InformationElement, QByteArray> elements;
    for (int pos = 0; pos + 2 <= payload.size();) {
        const InformationElement element = InformationElement(quint8(payload[pos]));
        const int len = quint8(payload[pos + 1]);
        pos += 2;
        if (pos + len > payload.size()) {
            break;
        }
        elements.insert(element, payload.mid(pos, len));
        pos += len;
    }
    return elements;
}

QString iax::informationElementString(const QMap<InformationElement, QByteArray> &elements, InformationElement element)
{
    return QString::fromUtf8(elements.value(element));
}

quint16 iax::informationElementUShort(const QMap<InformationElement, QByteArray> &elements, InformationElement element)
{
    const QByteArray data = elements.value(element);
    if (data.size() < 2) {
        return 0;
    }
    return qFromBigEndian<quint16>(data.constData());
}

IaxClientSession::IaxClientSession(QObject *parent) :
    QObject(parent)
{
    connect(&socket, &QUdpSocket::readyRead, this, &IaxClientSession::readPendingDatagrams);
    connect(&socket, &QUdpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::ConnectionRefusedError ||
            error == QAbstractSocket::RemoteHostClosedError) {
            emit debugMessage(QStringLiteral("IAX UDP socket transient error: %1").arg(socket.errorString()));
            return;
        }
        emit errorOccurred(socket.errorString());
    });
    connect(&retransmitTimer, &QTimer::timeout, this, &IaxClientSession::retransmitDueFrames);
    retransmitTimer.setInterval(100);
    connect(&lagTimer, &QTimer::timeout, this, &IaxClientSession::sendLagRequest);
    lagTimer.setInterval(2000);
    connect(&registrationTimer, &QTimer::timeout, this, &IaxClientSession::refreshRegistration);
    registrationTimer.setSingleShot(true);
    connect(&statsTimer, &QTimer::timeout, this, &IaxClientSession::emitStats);
    statsTimer.setInterval(1000);
}

bool IaxClientSession::isActive() const
{
    return active;
}

void IaxClientSession::connectToServer(const QString &host, quint16 port, const QString &newUsername,
                                       const QString &newPassword, const QString &newCalledNumber)
{
    username = newUsername;
    password = newPassword;
    calledNumber = newCalledNumber;
    callToken.clear();
    registered = false;
    purpose = SessionPurpose::OutboundCall;
    remotePort = port == 0 ? iax::defaultPort : port;
    remoteAddress = QHostAddress(host);
    if (remoteAddress.isNull()) {
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                remoteAddress = address;
                break;
            }
        }
    }

    if (remoteAddress.isNull()) {
        emit errorOccurred(QStringLiteral("Unable to resolve IAX host %1").arg(host));
        return;
    }

    if (socket.state() == QAbstractSocket::UnconnectedState &&
        !socket.bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred(socket.errorString());
        return;
    }

    startedAt = QDateTime::currentDateTimeUtc();
    sourceCall = nextSourceCall();
    registrationSourceCall = 0;
    registrationDestCall = 0;
    registrationOutgoingSeq = 0;
    registrationIncomingSeq = 0;
    qualifySourceCall = 0;
    qualifyDestCall = 0;
    qualifyOutgoingSeq = 0;
    qualifyIncomingSeq = 0;
    activeCallSourceCall = 0;
    destCall = 0;
    outgoingSeq = 0;
    incomingSeq = 0;
    pendingFrames.clear();
    resetStats();
    active = true;
    emit connectedChanged(false);

    sendNew();
    retransmitTimer.start();
    statsTimer.start();
    emit debugMessage(QStringLiteral("IAX NEW sent to %1:%2").arg(remoteAddress.toString()).arg(remotePort));
}

void IaxClientSession::registerWithServer(const QString &host, quint16 port, const QString &newUsername,
                                          const QString &newPassword, quint16 refreshSeconds)
{
    username = newUsername;
    password = newPassword;
    calledNumber.clear();
    callToken.clear();
    registered = false;
    purpose = SessionPurpose::Registration;
    registrationRefreshSeconds = refreshSeconds == 0 ? quint16(60) : refreshSeconds;
    remotePort = port == 0 ? iax::defaultPort : port;
    remoteAddress = QHostAddress(host);
    if (remoteAddress.isNull()) {
        const QHostInfo info = QHostInfo::fromName(host);
        for (const QHostAddress &address : info.addresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                remoteAddress = address;
                break;
            }
        }
    }

    if (remoteAddress.isNull()) {
        emit errorOccurred(QStringLiteral("Unable to resolve IAX host %1").arg(host));
        return;
    }

    if (socket.state() == QAbstractSocket::UnconnectedState &&
        !socket.bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred(socket.errorString());
        return;
    }

    startedAt = QDateTime::currentDateTimeUtc();
    sourceCall = nextSourceCall();
    registrationSourceCall = sourceCall;
    registrationDestCall = 0;
    registrationOutgoingSeq = 0;
    registrationIncomingSeq = 0;
    qualifySourceCall = 0;
    qualifyDestCall = 0;
    qualifyOutgoingSeq = 0;
    qualifyIncomingSeq = 0;
    activeCallSourceCall = 0;
    destCall = 0;
    outgoingSeq = 0;
    incomingSeq = 0;
    pendingFrames.clear();
    resetStats();
    active = true;

    sendRegisterRequest();
    retransmitTimer.start();
    statsTimer.start();
    emit debugMessage(QStringLiteral("IAX REGREQ sent to %1:%2").arg(remoteAddress.toString()).arg(remotePort));
}

void IaxClientSession::listenForDirectCalls(quint16 port, std::function<QStringList(const QString&)> passwordLookup)
{
    disconnectFromServer();
    directPasswordLookup = std::move(passwordLookup);
    purpose = SessionPurpose::DirectServer;
    remotePort = port == 0 ? iax::defaultPort : port;
    if (!socket.bind(QHostAddress::AnyIPv4, remotePort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred(socket.errorString());
        purpose = SessionPurpose::None;
        return;
    }

    active = true;
    startedAt = QDateTime::currentDateTimeUtc();
    retransmitTimer.start();
    statsTimer.start();
    emit debugMessage(QStringLiteral("IAX direct listener bound to UDP %1").arg(remotePort));
}

void IaxClientSession::disconnectFromServer()
{
    if (active) {
        iax::Frame frame;
        frame.sourceCall = sourceCall;
        frame.destCall = destCall;
        frame.timestamp = timestamp();
        frame.outgoingSeq = outgoingSeq++;
        frame.incomingSeq = incomingSeq;
        frame.type = iax::FrameType::Iax;
        frame.subclass = quint8(iax::IaxSubclass::Hangup);
        sendFullFrame(frame, false);
    }
    retransmitTimer.stop();
    lagTimer.stop();
    registrationTimer.stop();
    statsTimer.stop();
    pendingFrames.clear();
    socket.close();
    active = false;
    registered = false;
    registrationSourceCall = 0;
    registrationDestCall = 0;
    registrationOutgoingSeq = 0;
    registrationIncomingSeq = 0;
    qualifySourceCall = 0;
    qualifyDestCall = 0;
    qualifyOutgoingSeq = 0;
    qualifyIncomingSeq = 0;
    activeCallSourceCall = 0;
    activeCallConnected = false;
    purpose = SessionPurpose::None;
    emit connectedChanged(false);
    emit registrationChanged(false);
    emitStats();
}

void IaxClientSession::sendPayload(const QByteArray &payload)
{
    if (!active || destCall == 0) {
        emit errorOccurred(QStringLiteral("IAX session is not established"));
        return;
    }

    iax::Frame frame;
    frame.sourceCall = sourceCall;
    frame.destCall = destCall;
    frame.timestamp = timestamp();
    frame.outgoingSeq = outgoingSeq++;
    frame.incomingSeq = incomingSeq;
    frame.type = iax::FrameType::Text;
    frame.subclass = 0;
    frame.payload = encodeApplicationPayload(payload);
    sendFullFrame(frame, false);
    stats.textFramesTx++;
}

void IaxClientSession::sendRealtimePayload(const QByteArray &payload)
{
    if (!active || destCall == 0) {
        emit errorOccurred(QStringLiteral("IAX session is not established"));
        return;
    }

    const QByteArray packet = iax::encodeMiniFrame(sourceCall, quint16(timestamp()), encodeApplicationPayload(payload));
    socket.writeDatagram(packet, remoteAddress, remotePort);
    stats.miniFramesTx++;
    stats.textFramesTx++;
}

void IaxClientSession::readPendingDatagrams()
{
    readDrainQueued = false;

    int processed = 0;
    while (socket.hasPendingDatagrams()) {
        if (processed >= maxDatagramsPerReadPass) {
            if (!readDrainQueued) {
                readDrainQueued = true;
                QMetaObject::invokeMethod(this, &IaxClientSession::readPendingDatagrams, Qt::QueuedConnection);
            }
            return;
        }
        processed++;

        QByteArray packet;
        packet.resize(int(socket.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        socket.readDatagram(packet.data(), packet.size(), &sender, &senderPort);

        iax::Frame frame;
        if (!iax::decodeFrame(packet, &frame)) {
            stats.invalidIaxFrames++;
            emit debugMessage(QStringLiteral("Invalid IAX frame received"));
            continue;
        }

        if (frame.kind == iax::FrameKind::Mini) {
            stats.miniFramesRx++;
            QByteArray decoded;
            if (decodeApplicationPayload(frame.payload, &decoded)) {
                stats.textFramesRx++;
                emit payloadReceived(decoded);
            }
            continue;
        }

        stats.fullFramesRx++;
        if (isCallFrame(frame) && frame.type != iax::FrameType::Iax) {
            if (haveExpectedIncomingSeq && frame.outgoingSeq != expectedIncomingSeq) {
                stats.oSeqnoGaps += quint8(frame.outgoingSeq - expectedIncomingSeq);
            }
            expectedIncomingSeq = quint8(frame.outgoingSeq + 1);
            haveExpectedIncomingSeq = true;
        }

        if (frame.type == iax::FrameType::Iax &&
            frame.subclass == quint8(iax::IaxSubclass::Ack)) {
            if (pendingFrames.remove(pendingFrameKey(frame.destCall, frame.incomingSeq)) > 0) {
                stats.acksRx++;
            }
            continue;
        }

        if (purpose == SessionPurpose::DirectServer) {
            remoteAddress = sender;
            remotePort = senderPort;

            if (frame.type == iax::FrameType::Iax &&
                frame.subclass == quint8(iax::IaxSubclass::New)) {
                const auto elements = iax::decodeInformationElements(frame.payload);
                username = iax::informationElementString(elements, iax::InformationElement::Username).trimmed();
                directPasswords = directPasswordLookup ? directPasswordLookup(username) : QStringList();
                directPasswords.removeAll(QString());
                directPasswords.removeDuplicates();
                if (username.isEmpty() || directPasswords.isEmpty()) {
                    sendReject(frame, QStringLiteral("Authentication failed"), 21);
                    emit debugMessage(QStringLiteral("IAX direct rejected unknown user %1").arg(username));
                    continue;
                }

                sourceCall = nextSourceCall();
                activeCallSourceCall = sourceCall;
                destCall = frame.sourceCall;
                outgoingSeq = 0;
                incomingSeq = quint8(frame.outgoingSeq + 1);
                expectedIncomingSeq = incomingSeq;
                haveExpectedIncomingSeq = true;
                startedAt = QDateTime::currentDateTimeUtc();
                sendAck(frame);
                sendDirectAuthRequest(frame);
                continue;
            }

            if (frame.type == iax::FrameType::Iax &&
                frame.subclass == quint8(iax::IaxSubclass::AuthRep)) {
                const auto elements = iax::decodeInformationElements(frame.payload);
                const QByteArray digest = elements.value(iax::InformationElement::Md5Result);
                incomingSeq = quint8(frame.outgoingSeq + 1);
                sendAck(frame);
                bool authenticated = false;
                for (const QString &candidate : directPasswords) {
                    const QByteArray expected = QCryptographicHash::hash(directChallenge + candidate.toUtf8(),
                                                                          QCryptographicHash::Md5).toHex();
                    if (digest.compare(expected, Qt::CaseInsensitive) == 0) {
                        authenticated = true;
                        break;
                    }
                }
                if (!authenticated) {
                    sendReject(frame, QStringLiteral("Authentication failed"), 21);
                    emit debugMessage(QStringLiteral("IAX direct authentication failed for %1").arg(username));
                    continue;
                }

                acceptAuthenticatedDirectCall(frame);
                continue;
            }
        }

        if (purpose == SessionPurpose::Registration &&
            frame.type == iax::FrameType::Iax &&
            frame.subclass == quint8(iax::IaxSubclass::New)) {
            emit debugMessage(QStringLiteral("IAX incoming NEW received during station registration"));
            if (activeCallSourceCall != 0 || activeCallConnected) {
                if (activeCallConnected) {
                    emit connectedChanged(false);
                }
                clearActiveCallState();
                emit debugMessage(QStringLiteral("IAX cleared stale wfshare call state before accepting new call"));
            }
            acceptIncomingCall(frame);
            continue;
        }

        if (frame.type == iax::FrameType::Iax &&
            (frame.subclass == quint8(iax::IaxSubclass::Ping) ||
             frame.subclass == quint8(iax::IaxSubclass::Poke) ||
             frame.subclass == quint8(iax::IaxSubclass::LagRq) ||
             frame.subclass == quint8(iax::IaxSubclass::LagRp))) {
            if (frame.subclass == quint8(iax::IaxSubclass::Poke) && frame.destCall == 0) {
                sendPokeReply(frame);
                emit debugMessage(QStringLiteral("IAX PONG sent for POKE"));
                continue;
            }

            const bool isRegistrationMaintenance = purpose == SessionPurpose::Registration &&
                                                   registrationSourceCall != 0 &&
                                                   frame.destCall == registrationSourceCall;
            const bool isQualifyMaintenance = purpose == SessionPurpose::Registration &&
                                              qualifySourceCall != 0 &&
                                              frame.destCall == qualifySourceCall;
            if (isRegistrationMaintenance) {
                registrationIncomingSeq = quint8(frame.outgoingSeq + 1);
                sendRegistrationAck(frame);
                if (frame.subclass == quint8(iax::IaxSubclass::LagRp)) {
                    stats.lagMs = timestamp() >= frame.timestamp ? timestamp() - frame.timestamp : 0;
                } else {
                    sendRegistrationMaintenanceReply(frame,
                                                     frame.subclass == quint8(iax::IaxSubclass::LagRq) ?
                                                         iax::IaxSubclass::LagRp :
                                                         iax::IaxSubclass::Pong);
                }
            } else if (isQualifyMaintenance) {
                qualifyDestCall = frame.sourceCall;
                qualifyIncomingSeq = quint8(frame.outgoingSeq + 1);

                iax::Frame ack;
                ack.sourceCall = qualifySourceCall;
                ack.destCall = frame.sourceCall;
                ack.timestamp = frame.timestamp;
                ack.outgoingSeq = qualifyOutgoingSeq;
                ack.incomingSeq = qualifyIncomingSeq;
                ack.type = iax::FrameType::Iax;
                ack.subclass = quint8(iax::IaxSubclass::Ack);
                sendFullFrame(ack, false);

                if (frame.subclass == quint8(iax::IaxSubclass::LagRp)) {
                    stats.lagMs = timestamp() >= frame.timestamp ? timestamp() - frame.timestamp : 0;
                } else {
                    iax::Frame reply;
                    reply.sourceCall = qualifySourceCall;
                    reply.destCall = frame.sourceCall;
                    reply.timestamp = frame.timestamp;
                    reply.outgoingSeq = qualifyOutgoingSeq++;
                    reply.incomingSeq = qualifyIncomingSeq;
                    reply.type = iax::FrameType::Iax;
                    reply.subclass = frame.subclass == quint8(iax::IaxSubclass::LagRq) ?
                                         quint8(iax::IaxSubclass::LagRp) :
                                         quint8(iax::IaxSubclass::Pong);
                    sendFullFrame(reply, false);
                }
            } else {
                incomingSeq = quint8(frame.outgoingSeq + 1);
                sendAck(frame);
                if (frame.subclass == quint8(iax::IaxSubclass::LagRp)) {
                    stats.lagMs = timestamp() >= frame.timestamp ? timestamp() - frame.timestamp : 0;
                } else {
                    sendMaintenanceReply(frame,
                                         frame.subclass == quint8(iax::IaxSubclass::LagRq) ?
                                             iax::IaxSubclass::LagRp :
                                             iax::IaxSubclass::Pong);
                }
            }
            emit debugMessage(QStringLiteral("IAX maintenance reply sent for subclass %1").arg(frame.subclass));
            continue;
        }

        const bool isRegistrationFrame =
            purpose == SessionPurpose::Registration &&
            frame.type == iax::FrameType::Iax &&
            (frame.destCall == registrationSourceCall ||
             frame.subclass == quint8(iax::IaxSubclass::CallToken) ||
             frame.subclass == quint8(iax::IaxSubclass::RegAuth) ||
             frame.subclass == quint8(iax::IaxSubclass::RegAck) ||
             frame.subclass == quint8(iax::IaxSubclass::RegRej));

        if (isRegistrationFrame) {
            registrationDestCall = frame.sourceCall;
            registrationIncomingSeq = quint8(frame.outgoingSeq + 1);
            sendRegistrationAck(frame);

            if (frame.subclass == quint8(iax::IaxSubclass::CallToken)) {
                const auto elements = iax::decodeInformationElements(frame.payload);
                callToken = elements.value(iax::InformationElement::CallToken);
                registrationSourceCall = nextSourceCall();
                registrationDestCall = 0;
                registrationOutgoingSeq = 0;
                registrationIncomingSeq = 0;
                if (!activeCallConnected) {
                    sourceCall = registrationSourceCall;
                    startedAt = QDateTime::currentDateTimeUtc();
                }
                emit debugMessage(QStringLiteral("IAX CALLTOKEN received, retrying REGREQ"));
                sendRegisterRequest();
            } else if (frame.subclass == quint8(iax::IaxSubclass::RegAuth)) {
                emit debugMessage(QStringLiteral("IAX REGAUTH received"));
                sendRegisterRequest(&frame);
            } else if (frame.subclass == quint8(iax::IaxSubclass::RegAck)) {
                pendingFrames.clear();
                registered = true;
                const quint16 refresh = iax::informationElementUShort(iax::decodeInformationElements(frame.payload),
                                                                       iax::InformationElement::Refresh);
                if (refresh > 0) {
                    registrationRefreshSeconds = refresh;
                }
                registrationTimer.start(qMax(1000, int(registrationRefreshSeconds) * 800));
                emit registrationChanged(true);
                emit debugMessage(QStringLiteral("IAX REGACK received"));
            } else if (frame.subclass == quint8(iax::IaxSubclass::RegRej)) {
                emit errorOccurred(rejectReason(frame));
                disconnectFromServer();
                return;
            }
            continue;
        }

        if (purpose == SessionPurpose::Registration &&
            frame.type == iax::FrameType::Iax &&
            frame.subclass == quint8(iax::IaxSubclass::Hangup)) {
            const bool isActiveCallHangup = activeCallSourceCall != 0 &&
                                            frame.destCall == activeCallSourceCall;
            const bool isRegistrationHangup = registrationSourceCall != 0 &&
                                              frame.destCall == registrationSourceCall;
            const bool isQualifyHangup = qualifySourceCall != 0 &&
                                         frame.destCall == qualifySourceCall;

            if (isActiveCallHangup) {
                incomingSeq = quint8(frame.outgoingSeq + 1);
                sendAck(frame, activeCallSourceCall);
                if (activeCallConnected) {
                    emit connectedChanged(false);
                }
                clearActiveCallState();
                emit debugMessage(QStringLiteral("IAX wfshare call hung up"));
                return;
            }

            if (!isRegistrationHangup) {
                incomingSeq = quint8(frame.outgoingSeq + 1);
                sendAck(frame, isQualifyHangup ? qualifySourceCall : registrationSourceCall);
                if (isQualifyHangup) {
                    qualifySourceCall = 0;
                    qualifyDestCall = 0;
                    qualifyOutgoingSeq = 0;
                    qualifyIncomingSeq = 0;
                }
                emit debugMessage(QStringLiteral("IAX ignored stale wfshare call hangup"));
                return;
            }
        }

        const quint16 replySourceCall =
            (purpose == SessionPurpose::Registration && activeCallSourceCall != 0 &&
             frame.destCall == activeCallSourceCall) ?
                activeCallSourceCall :
                sourceCall;
        destCall = frame.sourceCall;
        incomingSeq = quint8(frame.outgoingSeq + 1);
        sendAck(frame, replySourceCall);

        if (frame.type == iax::FrameType::Iax && frame.subclass == quint8(iax::IaxSubclass::CallToken)) {
            const auto elements = iax::decodeInformationElements(frame.payload);
            callToken = elements.value(iax::InformationElement::CallToken);
            emit debugMessage(purpose == SessionPurpose::Registration
                              ? QStringLiteral("IAX CALLTOKEN received, retrying REGREQ")
                              : QStringLiteral("IAX CALLTOKEN received, retrying NEW"));
            pendingFrames.clear();
            if (purpose == SessionPurpose::Registration) {
                sourceCall = nextSourceCall();
                registrationSourceCall = sourceCall;
                activeCallSourceCall = 0;
                activeCallConnected = false;
                startedAt = QDateTime::currentDateTimeUtc();
            } else {
                sourceCall = nextSourceCall();
                startedAt = QDateTime::currentDateTimeUtc();
            }
            destCall = 0;
            outgoingSeq = 0;
            incomingSeq = 0;
            if (purpose == SessionPurpose::Registration) {
                sendRegisterRequest();
            } else {
                sendNew();
            }
        } else if (frame.type == iax::FrameType::Iax && frame.subclass == quint8(iax::IaxSubclass::AuthReq)) {
            sendAuthReply(frame);
        } else if (frame.type == iax::FrameType::Iax && frame.subclass == quint8(iax::IaxSubclass::RegAuth)) {
            emit debugMessage(QStringLiteral("IAX REGAUTH received"));
            sendRegisterRequest(&frame);
        } else if (frame.type == iax::FrameType::Iax && frame.subclass == quint8(iax::IaxSubclass::RegAck)) {
            pendingFrames.clear();
            registered = true;
            const quint16 refresh = iax::informationElementUShort(iax::decodeInformationElements(frame.payload),
                                                                   iax::InformationElement::Refresh);
            if (refresh > 0) {
                registrationRefreshSeconds = refresh;
            }
            registrationTimer.start(qMax(1000, int(registrationRefreshSeconds) * 800));
            emit registrationChanged(true);
            emit debugMessage(QStringLiteral("IAX REGACK received"));
        } else if (frame.type == iax::FrameType::Iax && frame.subclass == quint8(iax::IaxSubclass::Accept)) {
            pendingFrames.clear();
            emit debugMessage(QStringLiteral("IAX ACCEPT received"));
        } else if (frame.type == iax::FrameType::Control && frame.subclass == controlAnswer) {
            pendingFrames.clear();
            markConnectedCall();
            emit connectedChanged(true);
            emit debugMessage(QStringLiteral("IAX ANSWER received"));
        } else if (frame.type == iax::FrameType::Iax &&
                   (frame.subclass == quint8(iax::IaxSubclass::Reject) ||
                    frame.subclass == quint8(iax::IaxSubclass::Hangup) ||
                    frame.subclass == quint8(iax::IaxSubclass::RegRej))) {
            if (purpose == SessionPurpose::DirectServer &&
                frame.subclass == quint8(iax::IaxSubclass::Hangup)) {
                if (activeCallConnected) {
                    emit connectedChanged(false);
                }
                clearActiveCallState();
                emit debugMessage(QStringLiteral("IAX direct wfshare call hung up"));
                return;
            }
            emit errorOccurred(rejectReason(frame));
            disconnectFromServer();
            return;
        } else if (frame.type == iax::FrameType::Text) {
            QByteArray decoded;
            if (decodeApplicationPayload(frame.payload, &decoded)) {
                stats.textFramesRx++;
                emit payloadReceived(decoded);
            } else {
                emit debugMessage(QStringLiteral("IAX text frame ignored because it is not wfshare data"));
            }
        }
    }
}

void IaxClientSession::retransmitDueFrames()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = pendingFrames.begin(); it != pendingFrames.end();) {
        if (it.value().lastSent.msecsTo(now) < retransmitMs) {
            ++it;
            continue;
        }
        if (it.value().retries >= maxRetries) {
            retransmitTimer.stop();
            pendingFrames.clear();
            emit errorOccurred(QStringLiteral("IAX frame retransmit limit reached"));
            return;
        }
        socket.writeDatagram(it.value().packet, remoteAddress, remotePort);
        it.value().lastSent = now;
        it.value().retries++;
        stats.retries++;
        ++it;
    }
}

void IaxClientSession::sendFullFrame(iax::Frame frame, bool trackForRetransmit)
{
    const QByteArray packet = iax::encodeFullFrame(frame);
    socket.writeDatagram(packet, remoteAddress, remotePort);
    stats.fullFramesTx++;
    if (trackForRetransmit) {
        pendingFrames.insert(pendingFrameKey(frame.sourceCall, frame.outgoingSeq),
                             PendingFrame{packet, QDateTime::currentDateTimeUtc(), 0});
    }
}

void IaxClientSession::sendAck(const iax::Frame &frame, quint16 sourceCallOverride)
{
    iax::Frame ack;
    ack.sourceCall = sourceCallOverride == 0 ? sourceCall : sourceCallOverride;
    ack.destCall = frame.sourceCall;
    ack.timestamp = frame.timestamp;
    ack.outgoingSeq = outgoingSeq;
    ack.incomingSeq = incomingSeq;
    ack.type = iax::FrameType::Iax;
    ack.subclass = quint8(iax::IaxSubclass::Ack);
    sendFullFrame(ack, false);
    pendingFrames.remove(pendingFrameKey(ack.sourceCall, frame.incomingSeq));
}

void IaxClientSession::sendRegistrationAck(const iax::Frame &frame)
{
    iax::Frame ack;
    ack.sourceCall = registrationSourceCall;
    ack.destCall = frame.sourceCall;
    ack.timestamp = frame.timestamp;
    ack.outgoingSeq = registrationOutgoingSeq;
    ack.incomingSeq = registrationIncomingSeq;
    ack.type = iax::FrameType::Iax;
    ack.subclass = quint8(iax::IaxSubclass::Ack);
    sendFullFrame(ack, false);
    pendingFrames.remove(pendingFrameKey(ack.sourceCall, frame.incomingSeq));
}

void IaxClientSession::sendPokeReply(const iax::Frame &request)
{
    const bool trackQualifyCall = purpose == SessionPurpose::Registration;
    if (trackQualifyCall) {
        qualifySourceCall = nextSourceCall();
        qualifyDestCall = request.sourceCall;
        qualifyOutgoingSeq = 0;
        qualifyIncomingSeq = quint8(request.outgoingSeq + 1);
    }

    iax::Frame reply;
    reply.sourceCall = trackQualifyCall ? qualifySourceCall : nextSourceCall();
    reply.destCall = request.sourceCall;
    reply.timestamp = request.timestamp;
    reply.outgoingSeq = trackQualifyCall ? qualifyOutgoingSeq++ : 0;
    reply.incomingSeq = trackQualifyCall ? qualifyIncomingSeq : quint8(request.outgoingSeq + 1);
    reply.type = iax::FrameType::Iax;
    reply.subclass = quint8(iax::IaxSubclass::Pong);
    sendFullFrame(reply, false);
}

void IaxClientSession::sendMaintenanceReply(const iax::Frame &request, iax::IaxSubclass subclass,
                                            quint16 sourceCallOverride)
{
    iax::Frame reply;
    reply.sourceCall = sourceCallOverride == 0 ? sourceCall : sourceCallOverride;
    reply.destCall = request.sourceCall;
    reply.timestamp = request.timestamp;
    reply.outgoingSeq = outgoingSeq++;
    reply.incomingSeq = incomingSeq;
    reply.type = iax::FrameType::Iax;
    reply.subclass = quint8(subclass);
    sendFullFrame(reply, false);
}

void IaxClientSession::sendRegistrationMaintenanceReply(const iax::Frame &request, iax::IaxSubclass subclass)
{
    iax::Frame reply;
    reply.sourceCall = registrationSourceCall;
    reply.destCall = request.sourceCall;
    reply.timestamp = request.timestamp;
    reply.outgoingSeq = registrationOutgoingSeq++;
    reply.incomingSeq = registrationIncomingSeq;
    reply.type = iax::FrameType::Iax;
    reply.subclass = quint8(subclass);
    sendFullFrame(reply, false);
}

QByteArray IaxClientSession::encodeApplicationPayload(const QByteArray &payload) const
{
    return QByteArray(applicationPayloadPrefix) + payload.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

bool IaxClientSession::decodeApplicationPayload(const QByteArray &payload, QByteArray *decoded) const
{
    if (!payload.startsWith(applicationPayloadPrefix) || decoded == nullptr) {
        return false;
    }

    *decoded = QByteArray::fromBase64(payload.mid(int(strlen(applicationPayloadPrefix))),
                                      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return !decoded->isEmpty();
}

void IaxClientSession::sendNew()
{
    iax::Frame frame;
    frame.sourceCall = sourceCall;
    frame.timestamp = timestamp();
    frame.outgoingSeq = outgoingSeq++;
    frame.incomingSeq = incomingSeq;
    frame.type = iax::FrameType::Iax;
    frame.subclass = quint8(iax::IaxSubclass::New);
    frame.payload = callInformationElements();
    sendFullFrame(frame, true);
}

void IaxClientSession::sendAuthReply(const iax::Frame &request)
{
    const auto elements = iax::decodeInformationElements(request.payload);
    const quint16 authMethods = iax::informationElementUShort(elements, iax::InformationElement::AuthMethods);
    const QString challenge = iax::informationElementString(elements, iax::InformationElement::Challenge);

    if ((authMethods & iax::AuthMd5) == 0 || challenge.isEmpty()) {
        emit errorOccurred(QStringLiteral("IAX server did not offer MD5 challenge authentication"));
        return;
    }

    const QByteArray digest = QCryptographicHash::hash((challenge + password).toUtf8(), QCryptographicHash::Md5).toHex();

    iax::Frame frame;
    frame.sourceCall = sourceCall;
    frame.destCall = destCall;
    frame.timestamp = timestamp();
    frame.outgoingSeq = outgoingSeq++;
    frame.incomingSeq = incomingSeq;
    frame.type = iax::FrameType::Iax;
    frame.subclass = quint8(iax::IaxSubclass::AuthRep);
    frame.payload = iax::encodeInformationElementString(iax::InformationElement::Username, username) +
                    iax::encodeInformationElement(iax::InformationElement::Md5Result, digest);
    sendFullFrame(frame, true);
    emit debugMessage(QStringLiteral("IAX AUTHREP sent using MD5 challenge"));
}

void IaxClientSession::sendRegisterRequest(const iax::Frame *authRequest)
{
    QByteArray payload;
    payload += iax::encodeInformationElementString(iax::InformationElement::Username, username);
    payload += iax::encodeInformationElementUShort(iax::InformationElement::Refresh, registrationRefreshSeconds);
    payload += iax::encodeInformationElement(iax::InformationElement::CallToken, callToken);

    if (authRequest != nullptr) {
        const auto elements = iax::decodeInformationElements(authRequest->payload);
        const quint16 authMethods = iax::informationElementUShort(elements, iax::InformationElement::AuthMethods);
        const QString challenge = iax::informationElementString(elements, iax::InformationElement::Challenge);
        if ((authMethods & iax::AuthMd5) == 0 || challenge.isEmpty()) {
            emit errorOccurred(QStringLiteral("IAX registrar did not offer MD5 challenge authentication"));
            return;
        }

        const QByteArray digest = QCryptographicHash::hash((challenge + password).toUtf8(),
                                                           QCryptographicHash::Md5).toHex();
        payload += iax::encodeInformationElement(iax::InformationElement::Md5Result, digest);
    }

    const bool registrationMode = purpose == SessionPurpose::Registration;

    iax::Frame frame;
    frame.sourceCall = registrationMode ? registrationSourceCall : sourceCall;
    frame.destCall = registrationMode ? registrationDestCall : destCall;
    frame.timestamp = timestamp();
    frame.outgoingSeq = registrationMode ? registrationOutgoingSeq++ : outgoingSeq++;
    frame.incomingSeq = registrationMode ? registrationIncomingSeq : incomingSeq;
    frame.type = iax::FrameType::Iax;
    frame.subclass = quint8(iax::IaxSubclass::RegReq);
    frame.payload = payload;
    sendFullFrame(frame, !(registrationMode && activeCallConnected));
    emit debugMessage(authRequest == nullptr
                      ? QStringLiteral("IAX REGREQ sent")
                      : QStringLiteral("IAX authenticated REGREQ sent using MD5 challenge"));
}

void IaxClientSession::sendDirectAuthRequest(const iax::Frame &request)
{
    directChallenge = QByteArray::number(QRandomGenerator::global()->generate() & 0x7fffffff);

    iax::Frame frame;
    frame.sourceCall = sourceCall;
    frame.destCall = request.sourceCall;
    frame.timestamp = timestamp();
    frame.outgoingSeq = outgoingSeq++;
    frame.incomingSeq = incomingSeq;
    frame.type = iax::FrameType::Iax;
    frame.subclass = quint8(iax::IaxSubclass::AuthReq);
    frame.payload = iax::encodeInformationElementUShort(iax::InformationElement::AuthMethods, iax::AuthMd5) +
                    iax::encodeInformationElement(iax::InformationElement::Challenge, directChallenge) +
                    iax::encodeInformationElementString(iax::InformationElement::Username, username);
    sendFullFrame(frame, false);
    emit debugMessage(QStringLiteral("IAX direct AUTHREQ sent for %1").arg(username));
}

void IaxClientSession::acceptAuthenticatedDirectCall(const iax::Frame &request)
{
    Q_UNUSED(request)
    markConnectedCall();

    iax::Frame accept;
    accept.sourceCall = sourceCall;
    accept.destCall = destCall;
    accept.timestamp = timestamp();
    accept.outgoingSeq = outgoingSeq++;
    accept.incomingSeq = incomingSeq;
    accept.type = iax::FrameType::Iax;
    accept.subclass = quint8(iax::IaxSubclass::Accept);
    accept.payload = iax::encodeInformationElementUInt(iax::InformationElement::Format, ulawFormat);
    sendFullFrame(accept, false);

    iax::Frame answer;
    answer.sourceCall = sourceCall;
    answer.destCall = destCall;
    answer.timestamp = timestamp();
    answer.outgoingSeq = outgoingSeq++;
    answer.incomingSeq = incomingSeq;
    answer.type = iax::FrameType::Control;
    answer.subclass = controlAnswer;
    sendFullFrame(answer, false);

    emit connectedChanged(true);
    emit debugMessage(QStringLiteral("IAX direct wfshare call accepted for %1").arg(username));
}

void IaxClientSession::sendReject(const iax::Frame &request, const QString &cause, quint8 causeCode)
{
    iax::Frame reject;
    reject.sourceCall = sourceCall == 0 ? nextSourceCall() : sourceCall;
    reject.destCall = request.sourceCall;
    reject.timestamp = timestamp();
    reject.outgoingSeq = outgoingSeq++;
    reject.incomingSeq = quint8(request.outgoingSeq + 1);
    reject.type = iax::FrameType::Iax;
    reject.subclass = quint8(iax::IaxSubclass::Reject);
    reject.payload = iax::encodeInformationElementString(iax::InformationElement::Cause, cause) +
                     iax::encodeInformationElement(iax::InformationElement::CauseCode, QByteArray(1, char(causeCode)));
    sendFullFrame(reject, false);
}

void IaxClientSession::acceptIncomingCall(const iax::Frame &request)
{
    sourceCall = nextSourceCall();
    activeCallSourceCall = sourceCall;
    markConnectedCall();
    destCall = request.sourceCall;
    outgoingSeq = 0;
    incomingSeq = quint8(request.outgoingSeq + 1);
    expectedIncomingSeq = incomingSeq;
    haveExpectedIncomingSeq = true;
    startedAt = QDateTime::currentDateTimeUtc();

    sendAck(request);

    iax::Frame accept;
    accept.sourceCall = sourceCall;
    accept.destCall = destCall;
    accept.timestamp = timestamp();
    accept.outgoingSeq = outgoingSeq++;
    accept.incomingSeq = incomingSeq;
    accept.type = iax::FrameType::Iax;
    accept.subclass = quint8(iax::IaxSubclass::Accept);
    accept.payload = iax::encodeInformationElementUInt(iax::InformationElement::Format, ulawFormat);
    sendFullFrame(accept, false);

    iax::Frame answer;
    answer.sourceCall = sourceCall;
    answer.destCall = destCall;
    answer.timestamp = timestamp();
    answer.outgoingSeq = outgoingSeq++;
    answer.incomingSeq = incomingSeq;
    answer.type = iax::FrameType::Control;
    answer.subclass = controlAnswer;
    sendFullFrame(answer, false);

    emit connectedChanged(true);
    emit debugMessage(QStringLiteral("IAX incoming wfshare call accepted"));
}

QByteArray IaxClientSession::callInformationElements() const
{
    QByteArray payload;
    payload += iax::encodeInformationElementUShort(iax::InformationElement::Version, 2);
    payload += iax::encodeInformationElementString(iax::InformationElement::Username, username);
    if (!calledNumber.isEmpty()) {
        payload += iax::encodeInformationElementString(iax::InformationElement::CalledNumber, calledNumber);
    }
    payload += iax::encodeInformationElement(iax::InformationElement::CallToken, callToken);
    payload += iax::encodeInformationElementUInt(iax::InformationElement::Format, ulawFormat);
    payload += iax::encodeInformationElementUInt(iax::InformationElement::Capability, ulawFormat);
    return payload;
}

QString IaxClientSession::rejectReason(const iax::Frame &frame) const
{
    const auto elements = iax::decodeInformationElements(frame.payload);
    const QString cause = iax::informationElementString(elements, iax::InformationElement::Cause);
    const QByteArray causeCodeData = elements.value(iax::InformationElement::CauseCode);
    const int causeCode = causeCodeData.isEmpty() ? -1 : quint8(causeCodeData.constData()[0]);
    const QString action = (frame.subclass == quint8(iax::IaxSubclass::Reject) ||
                            frame.subclass == quint8(iax::IaxSubclass::RegRej)) ?
                               QStringLiteral("rejected") :
                               QStringLiteral("hung up");
    const QString purposeLabel = purpose == SessionPurpose::Registration ?
                                     QStringLiteral("IAX registration") :
                                     QStringLiteral("IAX session");

    if (!cause.isEmpty() && causeCode >= 0) {
        return QStringLiteral("%1 %2: %3 (%4)").arg(purposeLabel, action, cause).arg(causeCode);
    }
    if (!cause.isEmpty()) {
        return QStringLiteral("%1 %2: %3").arg(purposeLabel, action, cause);
    }
    if (causeCode >= 0) {
        return QStringLiteral("%1 %2 with cause code %3").arg(purposeLabel, action).arg(causeCode);
    }
    return QStringLiteral("%1 %2").arg(purposeLabel, action);
}

quint16 IaxClientSession::nextSourceCall()
{
    static quint16 lastCall = 1;
    lastCall++;
    if (lastCall == 0 || lastCall > 0x7fff) {
        lastCall = 1;
    }
    return lastCall;
}

quint32 IaxClientSession::timestamp() const
{
    return quint32(startedAt.msecsTo(QDateTime::currentDateTimeUtc()));
}

void IaxClientSession::resetStats()
{
    stats = IaxStats();
    expectedIncomingSeq = 0;
    haveExpectedIncomingSeq = false;
}

void IaxClientSession::markConnectedCall()
{
    activeCallConnected = true;
    if (!lagTimer.isActive()) {
        lagTimer.start();
    }
}

void IaxClientSession::clearActiveCallState()
{
    activeCallConnected = false;
    lagTimer.stop();
    activeCallSourceCall = 0;
    sourceCall = purpose == SessionPurpose::Registration ? registrationSourceCall : 0;
    destCall = 0;
    outgoingSeq = 0;
    incomingSeq = 0;
    expectedIncomingSeq = 0;
    haveExpectedIncomingSeq = false;
    pendingFrames.clear();
}

bool IaxClientSession::isCallFrame(const iax::Frame &frame) const
{
    if (frame.kind != iax::FrameKind::Full) {
        return false;
    }
    if (activeCallSourceCall != 0) {
        return frame.destCall == activeCallSourceCall;
    }
    return sourceCall != 0 && frame.destCall == sourceCall;
}

quint32 IaxClientSession::pendingFrameKey(quint16 sourceCallValue, quint8 outgoingSeqValue) const
{
    return (quint32(sourceCallValue & 0x7fff) << 8) | quint32(outgoingSeqValue);
}

void IaxClientSession::sendLagRequest()
{
    if (!active || !activeCallConnected || sourceCall == 0 || destCall == 0) {
        return;
    }

    iax::Frame frame;
    frame.sourceCall = sourceCall;
    frame.destCall = destCall;
    frame.timestamp = timestamp();
    frame.outgoingSeq = outgoingSeq++;
    frame.incomingSeq = incomingSeq;
    frame.type = iax::FrameType::Iax;
    frame.subclass = quint8(iax::IaxSubclass::LagRq);
    sendFullFrame(frame, false);
}

void IaxClientSession::refreshRegistration()
{
    if (!active || purpose != SessionPurpose::Registration || registrationSourceCall == 0) {
        return;
    }

    callToken.clear();
    registrationSourceCall = nextSourceCall();
    registrationDestCall = 0;
    registrationOutgoingSeq = 0;
    registrationIncomingSeq = 0;
    if (!activeCallConnected) {
        sourceCall = registrationSourceCall;
        startedAt = QDateTime::currentDateTimeUtc();
    }
    emit debugMessage(QStringLiteral("IAX refreshing registration with new REGREQ"));
    sendRegisterRequest();
}

void IaxClientSession::emitStats()
{
    IaxStats snapshot = stats;
    snapshot.pendingFullFrames = quint32(pendingFrames.size());
    emit statsChanged(snapshot);
}
