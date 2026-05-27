#ifndef IAXRADIOTRANSPORT_H
#define IAXRADIOTRANSPORT_H

#include "iaxclientsession.h"
#include "radiotransport.h"

class IaxRadioTransport : public RadioTransport
{
    Q_OBJECT

public:
    explicit IaxRadioTransport(QObject *parent = nullptr);

    QString name() const override;
    bool isConnected() const override;
    RadioTransportState state() const override;

signals:
    void iaxStatsChanged(IaxStats stats);

public slots:
    void connectTransport(const RadioTransportEndpoint &endpoint) override;
    void disconnectTransport() override;
    void sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                      const QHostAddress &remoteAddress = QHostAddress(),
                      quint16 remotePort = 0) override;
    void sendStream(RadioTransportChannel channel, const QByteArray &data) override;

private:
    void startSession();
    void setState(RadioTransportState newState);

    IaxClientSession session;
    RadioTransportEndpoint currentEndpoint;
    RadioTransportState currentState = RadioTransportState::Disconnected;
    quint32 sentFrames = 0;
    quint32 receivedFrames = 0;
    int reconnectAttempts = 0;
    bool manualDisconnect = false;
    bool callAnswered = false;
};

#endif // IAXRADIOTRANSPORT_H
