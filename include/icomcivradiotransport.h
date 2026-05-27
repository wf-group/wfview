#ifndef ICOMCIVRADIOTRANSPORT_H
#define ICOMCIVRADIOTRANSPORT_H

#include <QHostAddress>

#include "icomudpcivdata.h"
#include "radiotransport.h"

class IcomCivRadioTransport : public RadioTransport
{
    Q_OBJECT

public:
    IcomCivRadioTransport(QHostAddress localAddress, QHostAddress radioAddress,
                          quint16 civPort, bool splitWaterfall, quint16 localPort,
                          QObject *parent = nullptr);
    ~IcomCivRadioTransport() override;

    QString name() const override;
    bool isConnected() const override;
    RadioTransportState state() const override;

    quint32 packetsSent() const;
    quint32 packetsLost() const;

public slots:
    void connectTransport(const RadioTransportEndpoint &endpoint) override;
    void disconnectTransport() override;
    void sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                      const QHostAddress &remoteAddress = QHostAddress(),
                      quint16 remotePort = 0) override;
    void sendStream(RadioTransportChannel channel, const QByteArray &data) override;

private slots:
    void receiveCivBytes(QByteArray data);

private:
    void setState(RadioTransportState newState);

    icomUdpCivData *civ = nullptr;
    RadioTransportState currentState = RadioTransportState::Disconnected;
};

#endif // ICOMCIVRADIOTRANSPORT_H
