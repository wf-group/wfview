#include "directradiotransport.h"

DirectTcpRadioTransport::DirectTcpRadioTransport(RadioTransportChannel channel, QObject *parent) :
    RadioTransport(parent),
    streamChannel(channel)
{
    connect(&socket, &QTcpSocket::readyRead, this, &DirectTcpRadioTransport::readAvailable);
    connect(&socket, &QTcpSocket::connected, this, &DirectTcpRadioTransport::socketConnected);
    connect(&socket, &QTcpSocket::disconnected, this, &DirectTcpRadioTransport::socketDisconnected);
    connect(&socket, &QTcpSocket::errorOccurred, this, &DirectTcpRadioTransport::socketError);
}

QString DirectTcpRadioTransport::name() const
{
    return QStringLiteral("direct-tcp");
}

bool DirectTcpRadioTransport::isConnected() const
{
    return socket.state() == QAbstractSocket::ConnectedState;
}

RadioTransportState DirectTcpRadioTransport::state() const
{
    return currentState;
}

void DirectTcpRadioTransport::connectTransport(const RadioTransportEndpoint &newEndpoint)
{
    endpoint = newEndpoint;
    setState(RadioTransportState::Connecting);
    socket.connectToHost(endpoint.host, endpoint.port);
}

void DirectTcpRadioTransport::disconnectTransport()
{
    if (socket.state() == QAbstractSocket::UnconnectedState) {
        setState(RadioTransportState::Disconnected);
        return;
    }

    setState(RadioTransportState::Closing);
    socket.disconnectFromHost();
}

void DirectTcpRadioTransport::sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                                           const QHostAddress &remoteAddress, quint16 remotePort)
{
    Q_UNUSED(channel)
    Q_UNUSED(data)
    Q_UNUSED(remoteAddress)
    Q_UNUSED(remotePort)
    emit errorOccurred(QStringLiteral("direct-tcp does not support datagrams"));
}

void DirectTcpRadioTransport::sendStream(RadioTransportChannel channel, const QByteArray &data)
{
    if (channel != streamChannel && channel != RadioTransportChannel::Unknown) {
        emit errorOccurred(QStringLiteral("direct-tcp received data for the wrong channel"));
        return;
    }

    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("direct-tcp is not connected"));
        return;
    }

    socket.write(data);
}

void DirectTcpRadioTransport::readAvailable()
{
    emit streamReceived(streamChannel, socket.readAll());
}

void DirectTcpRadioTransport::socketConnected()
{
    setState(RadioTransportState::Connected);
}

void DirectTcpRadioTransport::socketDisconnected()
{
    setState(RadioTransportState::Disconnected);
}

void DirectTcpRadioTransport::socketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit errorOccurred(socket.errorString());
    setState(RadioTransportState::Error);
}

void DirectTcpRadioTransport::setState(RadioTransportState newState)
{
    if (currentState == newState) {
        return;
    }

    currentState = newState;
    emit stateChanged(currentState);
    emit connectedChanged(currentState == RadioTransportState::Connected);
}

DirectUdpRadioTransport::DirectUdpRadioTransport(RadioTransportChannel channel, QObject *parent) :
    RadioTransport(parent),
    datagramChannel(channel)
{
    connect(&socket, &QUdpSocket::readyRead, this, &DirectUdpRadioTransport::readAvailable);
    connect(&socket, &QUdpSocket::errorOccurred, this, &DirectUdpRadioTransport::socketError);
}

QString DirectUdpRadioTransport::name() const
{
    return QStringLiteral("direct-udp");
}

bool DirectUdpRadioTransport::isConnected() const
{
    return socket.state() == QAbstractSocket::BoundState;
}

RadioTransportState DirectUdpRadioTransport::state() const
{
    return currentState;
}

void DirectUdpRadioTransport::connectTransport(const RadioTransportEndpoint &newEndpoint)
{
    endpoint = newEndpoint;
    defaultRemotePort = endpoint.port;
    defaultRemoteAddress = QHostAddress(endpoint.host);

    const quint16 localPort = quint16(endpoint.options.value(QStringLiteral("localPort"), 0).toUInt());
    const bool bound = socket.bind(QHostAddress::AnyIPv4, localPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    setState(bound ? RadioTransportState::Connected : RadioTransportState::Error);
    if (!bound) {
        emit errorOccurred(socket.errorString());
    }
}

void DirectUdpRadioTransport::disconnectTransport()
{
    socket.close();
    setState(RadioTransportState::Disconnected);
}

void DirectUdpRadioTransport::sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                                           const QHostAddress &remoteAddress, quint16 remotePort)
{
    if (channel != datagramChannel && datagramChannel != RadioTransportChannel::Unknown) {
        emit errorOccurred(QStringLiteral("direct-udp received data for the wrong channel"));
        return;
    }

    const QHostAddress destinationAddress = remoteAddress.isNull() ? defaultRemoteAddress : remoteAddress;
    const quint16 destinationPort = remotePort == 0 ? defaultRemotePort : remotePort;
    if (destinationAddress.isNull() || destinationPort == 0) {
        emit errorOccurred(QStringLiteral("direct-udp destination is not set"));
        return;
    }

    socket.writeDatagram(data, destinationAddress, destinationPort);
}

void DirectUdpRadioTransport::sendStream(RadioTransportChannel channel, const QByteArray &data)
{
    sendDatagram(channel, data);
}

void DirectUdpRadioTransport::readAvailable()
{
    while (socket.hasPendingDatagrams()) {
        QByteArray data;
        data.resize(int(socket.pendingDatagramSize()));

        QHostAddress senderAddress;
        quint16 senderPort = 0;
        socket.readDatagram(data.data(), data.size(), &senderAddress, &senderPort);
        emit datagramReceived(datagramChannel, data, senderAddress, senderPort);
    }
}

void DirectUdpRadioTransport::socketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit errorOccurred(socket.errorString());
    setState(RadioTransportState::Error);
}

void DirectUdpRadioTransport::setState(RadioTransportState newState)
{
    if (currentState == newState) {
        return;
    }

    currentState = newState;
    emit stateChanged(currentState);
    emit connectedChanged(currentState == RadioTransportState::Connected);
}
