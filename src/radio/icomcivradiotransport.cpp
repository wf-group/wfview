#include "icomcivradiotransport.h"

IcomCivRadioTransport::IcomCivRadioTransport(QHostAddress localAddress, QHostAddress radioAddress,
                                             quint16 civPort, bool splitWaterfall, quint16 localPort,
                                             QObject *parent) :
    RadioTransport(parent),
    civ(new icomUdpCivData(localAddress, radioAddress, civPort, splitWaterfall, localPort))
{
    connect(civ, &icomUdpCivData::receive, this, &IcomCivRadioTransport::receiveCivBytes);
    setState(RadioTransportState::Connected);
}

IcomCivRadioTransport::~IcomCivRadioTransport()
{
    delete civ;
    civ = nullptr;
    setState(RadioTransportState::Disconnected);
}

QString IcomCivRadioTransport::name() const
{
    return QStringLiteral("icom-civ-udp");
}

bool IcomCivRadioTransport::isConnected() const
{
    return civ != nullptr && currentState == RadioTransportState::Connected;
}

RadioTransportState IcomCivRadioTransport::state() const
{
    return currentState;
}

quint32 IcomCivRadioTransport::packetsSent() const
{
    return civ != nullptr ? civ->packetsSent : 0;
}

quint32 IcomCivRadioTransport::packetsLost() const
{
    return civ != nullptr ? civ->packetsLost : 0;
}

void IcomCivRadioTransport::connectTransport(const RadioTransportEndpoint &endpoint)
{
    Q_UNUSED(endpoint)

    if (civ != nullptr) {
        setState(RadioTransportState::Connected);
        return;
    }

    setState(RadioTransportState::Error);
    emit errorOccurred(QStringLiteral("icom-civ-udp cannot reconnect after construction"));
}

void IcomCivRadioTransport::disconnectTransport()
{
    if (civ == nullptr) {
        setState(RadioTransportState::Disconnected);
        return;
    }

    setState(RadioTransportState::Closing);
    delete civ;
    civ = nullptr;
    setState(RadioTransportState::Disconnected);
}

void IcomCivRadioTransport::sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                                         const QHostAddress &remoteAddress, quint16 remotePort)
{
    Q_UNUSED(remoteAddress)
    Q_UNUSED(remotePort)

    sendStream(channel, data);
}

void IcomCivRadioTransport::sendStream(RadioTransportChannel channel, const QByteArray &data)
{
    if (channel != RadioTransportChannel::Civ && channel != RadioTransportChannel::Unknown) {
        emit errorOccurred(QStringLiteral("icom-civ-udp received data for the wrong channel"));
        return;
    }

    if (civ == nullptr) {
        emit errorOccurred(QStringLiteral("icom-civ-udp is not connected"));
        return;
    }

    civ->send(data);
}

void IcomCivRadioTransport::receiveCivBytes(QByteArray data)
{
    emit streamReceived(RadioTransportChannel::Civ, data);
}

void IcomCivRadioTransport::setState(RadioTransportState newState)
{
    if (currentState == newState) {
        return;
    }

    currentState = newState;
    emit stateChanged(currentState);
    emit connectedChanged(currentState == RadioTransportState::Connected);
}
