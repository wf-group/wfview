#ifndef IAXCLIENTSESSION_H
#define IAXCLIENTSESSION_H

#include <QByteArray>
#include <QDateTime>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <functional>

struct IaxStats {
    quint32 fullFramesTx = 0;
    quint32 fullFramesRx = 0;
    quint32 miniFramesTx = 0;
    quint32 miniFramesRx = 0;
    quint32 textFramesTx = 0;
    quint32 textFramesRx = 0;
    quint32 retries = 0;
    quint32 acksRx = 0;
    quint32 invalidIaxFrames = 0;
    quint32 oSeqnoGaps = 0;
    quint32 pendingFullFrames = 0;
    quint32 lagMs = 0;
};
Q_DECLARE_METATYPE(IaxStats)

namespace iax {

constexpr quint16 defaultPort = 4569;

enum class FrameKind { Full, Mini };
enum class FrameType : quint8 { Dtmf = 1, Voice = 2, Video = 3, Control = 4, Null = 5, Iax = 6, Text = 7 };
enum class IaxSubclass : quint8 {
    New = 1,
    Ping = 2,
    Pong = 3,
    Ack = 4,
    Hangup = 5,
    Reject = 6,
    Accept = 7,
    AuthReq = 8,
    AuthRep = 9,
    LagRq = 11,
    LagRp = 12,
    RegReq = 13,
    RegAuth = 14,
    RegAck = 15,
    RegRej = 16,
    Poke = 30,
    CallToken = 40
};

enum class InformationElement : quint8 {
    CalledNumber = 1,
    CallingNumber = 2,
    CallingName = 4,
    Username = 6,
    Capability = 8,
    Format = 9,
    Version = 11,
    AuthMethods = 14,
    Challenge = 15,
    Md5Result = 16,
    Refresh = 19,
    Cause = 22,
    CauseCode = 42,
    CallToken = 54
};

enum AuthMethod : quint16 {
    AuthPlaintext = 0x0001,
    AuthMd5 = 0x0002,
    AuthRsa = 0x0004
};

struct Frame {
    FrameKind kind = FrameKind::Full;
    quint16 sourceCall = 0;
    quint16 destCall = 0;
    quint32 timestamp = 0;
    quint8 outgoingSeq = 0;
    quint8 incomingSeq = 0;
    FrameType type = FrameType::Null;
    quint8 subclass = 0;
    QByteArray payload;
};

QByteArray encodeFullFrame(const Frame &frame);
QByteArray encodeMiniFrame(quint16 sourceCall, quint16 timestamp, const QByteArray &payload);
bool decodeFrame(const QByteArray &packet, Frame *frame);
QByteArray encodeInformationElement(InformationElement element, const QByteArray &value);
QByteArray encodeInformationElementString(InformationElement element, const QString &value);
QByteArray encodeInformationElementUShort(InformationElement element, quint16 value);
QByteArray encodeInformationElementUInt(InformationElement element, quint32 value);
QMap<InformationElement, QByteArray> decodeInformationElements(const QByteArray &payload);
QString informationElementString(const QMap<InformationElement, QByteArray> &elements, InformationElement element);
quint16 informationElementUShort(const QMap<InformationElement, QByteArray> &elements, InformationElement element);

}

class IaxClientSession : public QObject
{
    Q_OBJECT

public:
    explicit IaxClientSession(QObject *parent = nullptr);

    bool isActive() const;
    void listenForDirectCalls(quint16 port, std::function<QStringList(const QString&)> passwordLookup);

public slots:
    void connectToServer(const QString &host, quint16 port, const QString &username,
                         const QString &password, const QString &calledNumber);
    void registerWithServer(const QString &host, quint16 port, const QString &username,
                            const QString &password, quint16 refreshSeconds = 60);
    void disconnectFromServer();
    void sendPayload(const QByteArray &payload);
    void sendRealtimePayload(const QByteArray &payload);

signals:
    void connectedChanged(bool connected);
    void registrationChanged(bool registered);
    void payloadReceived(QByteArray payload);
    void errorOccurred(QString message);
    void debugMessage(QString message);
    void statsChanged(IaxStats stats);

private slots:
    void readPendingDatagrams();
    void retransmitDueFrames();
    void sendLagRequest();
    void refreshRegistration();
    void emitStats();

private:
    void sendFullFrame(iax::Frame frame, bool trackForRetransmit);
    void sendAck(const iax::Frame &frame, quint16 sourceCallOverride = 0);
    void sendRegistrationAck(const iax::Frame &frame);
    void sendPokeReply(const iax::Frame &request);
    void sendMaintenanceReply(const iax::Frame &request, iax::IaxSubclass subclass,
                              quint16 sourceCallOverride = 0);
    void sendRegistrationMaintenanceReply(const iax::Frame &request, iax::IaxSubclass subclass);
    QByteArray encodeApplicationPayload(const QByteArray &payload) const;
    bool decodeApplicationPayload(const QByteArray &payload, QByteArray *decoded) const;
    void sendNew();
    void sendAuthReply(const iax::Frame &request);
    void sendRegisterRequest(const iax::Frame *authRequest = nullptr);
    void acceptIncomingCall(const iax::Frame &request);
    void acceptAuthenticatedDirectCall(const iax::Frame &request);
    void sendDirectAuthRequest(const iax::Frame &request);
    void sendReject(const iax::Frame &request, const QString &cause, quint8 causeCode = 21);
    QByteArray callInformationElements() const;
    QString rejectReason(const iax::Frame &frame) const;
    quint16 nextSourceCall();
    quint32 timestamp() const;
    void resetStats();
    void markConnectedCall();
    void clearActiveCallState();
    bool isCallFrame(const iax::Frame &frame) const;
    quint32 pendingFrameKey(quint16 sourceCall, quint8 outgoingSeq) const;

    struct PendingFrame {
        QByteArray packet;
        QDateTime lastSent;
        int retries = 0;
    };

    enum class SessionPurpose {
        None,
        OutboundCall,
        Registration,
        DirectServer
    };

    QUdpSocket socket;
    QTimer retransmitTimer;
    QTimer lagTimer;
    QTimer registrationTimer;
    QTimer statsTimer;
    QHostAddress remoteAddress;
    quint16 remotePort = iax::defaultPort;
    QString username;
    QString password;
    QString calledNumber;
    QByteArray callToken;
    QByteArray directChallenge;
    QStringList directPasswords;
    std::function<QStringList(const QString&)> directPasswordLookup;
    quint16 registrationRefreshSeconds = 60;
    quint16 sourceCall = 0;
    quint16 registrationSourceCall = 0;
    quint16 registrationDestCall = 0;
    quint8 registrationOutgoingSeq = 0;
    quint8 registrationIncomingSeq = 0;
    quint16 qualifySourceCall = 0;
    quint16 qualifyDestCall = 0;
    quint8 qualifyOutgoingSeq = 0;
    quint8 qualifyIncomingSeq = 0;
    quint16 activeCallSourceCall = 0;
    quint16 destCall = 0;
    quint8 outgoingSeq = 0;
    quint8 incomingSeq = 0;
    QDateTime startedAt;
    QMap<quint32, PendingFrame> pendingFrames;
    IaxStats stats;
    quint8 expectedIncomingSeq = 0;
    bool haveExpectedIncomingSeq = false;
    bool active = false;
    bool registered = false;
    bool activeCallConnected = false;
    bool readDrainQueued = false;
    SessionPurpose purpose = SessionPurpose::None;
};

#endif // IAXCLIENTSESSION_H
