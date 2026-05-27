#ifndef RADIOTRANSPORT_H
#define RADIOTRANSPORT_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVariantMap>

enum class RadioTransportState {
    Disconnected,
    Connecting,
    Connected,
    Closing,
    Error
};
Q_DECLARE_METATYPE(RadioTransportState)

enum class RadioTransportChannel {
    Control,
    Civ,
    Cat,
    AudioRx,
    AudioTx,
    Scope,
    Discovery,
    Unknown
};
Q_DECLARE_METATYPE(RadioTransportChannel)

struct RadioTransportEndpoint {
    QString host;
    quint16 port = 0;
    QString username;
    QString password;
    QString clientName;
    QVariantMap options;
};
Q_DECLARE_METATYPE(RadioTransportEndpoint)

class RadioTransport : public QObject
{
    Q_OBJECT

public:
    explicit RadioTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~RadioTransport() override = default;

    virtual QString name() const = 0;
    virtual bool isConnected() const = 0;
    virtual RadioTransportState state() const = 0;

public slots:
    virtual void connectTransport(const RadioTransportEndpoint &endpoint) = 0;
    virtual void disconnectTransport() = 0;

    virtual void sendDatagram(RadioTransportChannel channel, const QByteArray &data,
                              const QHostAddress &remoteAddress = QHostAddress(),
                              quint16 remotePort = 0) = 0;
    virtual void sendStream(RadioTransportChannel channel, const QByteArray &data) = 0;

signals:
    void stateChanged(RadioTransportState state);
    void connectedChanged(bool connected);
    void datagramReceived(RadioTransportChannel channel, QByteArray data,
                          QHostAddress remoteAddress, quint16 remotePort);
    void streamReceived(RadioTransportChannel channel, QByteArray data);
    void errorOccurred(QString message);
};

#endif // RADIOTRANSPORT_H
