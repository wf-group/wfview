#include "iaxradiotransport.h"

#include "logcategories.h"
#include "radiotransportframe.h"

#include <QTimer>

IaxRadioTransport::IaxRadioTransport(QObject *parent) :
    RadioTransport(parent)
{
    connect(&session, &IaxClientSession::connectedChanged, this, [this](bool connected) {
        qInfo(logRig()) << (connected ? "wfshare IAX call answered" : "wfshare IAX call disconnected");
        if (connected) {
            callAnswered = true;
            reconnectAttempts = 0;
        } else if (!manualDisconnect && callAnswered && receivedFrames == 0 && reconnectAttempts < 3) {
            reconnectAttempts++;
            qInfo(logRig()).noquote()
                << QStringLiteral("wfshare transport retrying connection attempt %1").arg(reconnectAttempts);
            QTimer::singleShot(2000, this, [this]() {
                if (!manualDisconnect) {
                    startSession();
                }
            });
            callAnswered = false;
            return;
        }
        if (!connected) {
            callAnswered = false;
        }
        setState(connected ? RadioTransportState::Connected :
                             (currentState == RadioTransportState::Connected ? RadioTransportState::Disconnected :
                                                                        RadioTransportState::Connecting));
    });
    connect(&session, &IaxClientSession::debugMessage, this, [](const QString &message) {
        qDebug(logRig()).noquote() << QStringLiteral("wfshare transport: %1").arg(message);
    });
    connect(&session, &IaxClientSession::payloadReceived, this, [this](const QByteArray &payload) {
        radioTransportFrame::Frame frame;
        if (radioTransportFrame::decode(payload, &frame) != radioTransportFrame::DecodeOk) {
            emit errorOccurred(QStringLiteral("Invalid wfview transport frame received over IAX"));
            return;
        }
        if (frame.channel == RadioTransportChannel::Control) {
            qDebug(logRig()).noquote()
                << QStringLiteral("wfshare transport received control frame: %1")
                       .arg(QString::fromUtf8(frame.payload));
            return;
        }
        receivedFrames++;
        if (receivedFrames <= 10 || receivedFrames % 100 == 0) {
            qDebug(logRig()).noquote()
                << QStringLiteral("wfshare transport received frame %1 channel %2 bytes %3")
                       .arg(receivedFrames)
                       .arg(int(frame.channel))
                       .arg(frame.payload.size());
        }
        emit streamReceived(frame.channel, frame.payload);
    });
    connect(&session, &IaxClientSession::errorOccurred, this, [this](const QString &message) {
        setState(RadioTransportState::Error);
        emit errorOccurred(message);
    });
    connect(&session, &IaxClientSession::statsChanged, this, &IaxRadioTransport::iaxStatsChanged);
}

QString IaxRadioTransport::name() const
{
    return QStringLiteral("iax");
}

bool IaxRadioTransport::isConnected() const
{
    return currentState == RadioTransportState::Connected;
}

RadioTransportState IaxRadioTransport::state() const
{
    return currentState;
}

void IaxRadioTransport::connectTransport(const RadioTransportEndpoint &endpoint)
{
    currentEndpoint = endpoint;
    sentFrames = 0;
    receivedFrames = 0;
    reconnectAttempts = 0;
    manualDisconnect = false;
    callAnswered = false;
    setState(RadioTransportState::Connecting);
    startSession();
}

void IaxRadioTransport::startSession()
{
    sentFrames = 0;
    receivedFrames = 0;
    callAnswered = false;
    qInfo(logRig()).noquote()
        << QStringLiteral("wfshare transport connecting to %1:%2 as %3 for station %4")
               .arg(currentEndpoint.host)
               .arg(currentEndpoint.port == 0 ? iax::defaultPort : currentEndpoint.port)
               .arg(currentEndpoint.username.isEmpty() ? QStringLiteral("<empty>") : currentEndpoint.username)
               .arg(currentEndpoint.options.value(QStringLiteral("calledNumber")).toString().isEmpty()
                        ? QStringLiteral("<empty>")
                        : currentEndpoint.options.value(QStringLiteral("calledNumber")).toString());
    session.connectToServer(currentEndpoint.host, currentEndpoint.port, currentEndpoint.username, currentEndpoint.password,
                            currentEndpoint.options.value(QStringLiteral("calledNumber")).toString());
}

void IaxRadioTransport::disconnectTransport()
{
    manualDisconnect = true;
    session.disconnectFromServer();
    setState(RadioTransportState::Disconnected);
}

void IaxRadioTransport::sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                                     const QHostAddress &remoteAddress, quint16 remotePort)
{
    Q_UNUSED(remoteAddress)
    Q_UNUSED(remotePort)

    sendStream(channel, data);
}

void IaxRadioTransport::sendStream(RadioTransportChannel channel, const QByteArray &data)
{
    radioTransportFrame::Frame frame;
    frame.channel = channel;
    frame.payload = data;

    sentFrames++;
    if (sentFrames <= 10 || sentFrames % 100 == 0) {
        qDebug(logRig()).noquote()
            << QStringLiteral("wfshare transport sent frame %1 channel %2 bytes %3")
                   .arg(sentFrames)
                   .arg(int(channel))
                   .arg(data.size());
    }
    session.sendPayload(radioTransportFrame::encode(frame));
}

void IaxRadioTransport::setState(RadioTransportState newState)
{
    if (currentState == newState) {
        return;
    }

    currentState = newState;
    emit stateChanged(currentState);
    emit connectedChanged(currentState == RadioTransportState::Connected);
}
