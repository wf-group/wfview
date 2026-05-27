#ifndef DIRECTRADIOTRANSPORT_H
#define DIRECTRADIOTRANSPORT_H

#include <QHostAddress>
#include <QScopedPointer>
#include <QTcpSocket>
#include <QUdpSocket>

#include "radiotransport.h"

class DirectTcpRadioTransport : public RadioTransport
{
    Q_OBJECT

public:
    explicit DirectTcpRadioTransport(RadioTransportChannel channel = RadioTransportChannel::Cat,
                                     QObject *parent = nullptr);

    QString name() const override;
    bool isConnected() const override;
    RadioTransportState state() const override;

public slots:
    void connectTransport(const RadioTransportEndpoint &endpoint) override;
    void disconnectTransport() override;
    void sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                      const QHostAddress &remoteAddress = QHostAddress(),
                      quint16 remotePort = 0) override;
    void sendStream(RadioTransportChannel channel, const QByteArray &data) override;

private slots:
    void readAvailable();
    void socketConnected();
    void socketDisconnected();
    void socketError(QAbstractSocket::SocketError error);

private:
    void setState(RadioTransportState newState);

    QTcpSocket socket;
    RadioTransportEndpoint endpoint;
    RadioTransportChannel streamChannel;
    RadioTransportState currentState = RadioTransportState::Disconnected;
};

class DirectUdpRadioTransport : public RadioTransport
{
    Q_OBJECT

public:
    explicit DirectUdpRadioTransport(RadioTransportChannel channel = RadioTransportChannel::Unknown,
                                     QObject *parent = nullptr);

    QString name() const override;
    bool isConnected() const override;
    RadioTransportState state() const override;

public slots:
    void connectTransport(const RadioTransportEndpoint &endpoint) override;
    void disconnectTransport() override;
    void sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                      const QHostAddress &remoteAddress = QHostAddress(),
                      quint16 remotePort = 0) override;
    void sendStream(RadioTransportChannel channel, const QByteArray &data) override;

private slots:
    void readAvailable();
    void socketError(QAbstractSocket::SocketError error);

private:
    void setState(RadioTransportState newState);

    QUdpSocket socket;
    RadioTransportEndpoint endpoint;
    RadioTransportChannel datagramChannel;
    QHostAddress defaultRemoteAddress;
    quint16 defaultRemotePort = 0;
    RadioTransportState currentState = RadioTransportState::Disconnected;
};

#endif // DIRECTRADIOTRANSPORT_H
