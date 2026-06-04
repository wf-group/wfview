#ifndef YAESUSERVER_H
#define YAESUSERVER_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QByteArray>
#include <QList>
#include <QVector>
#include <QMap>
#include <QSet>

// Allow easy endian-ness conversions
#include <QtEndian>

#include <QDebug>

#include "rigserver.h"
#include "packettypes.h"
#include "rigidentities.h"
#include "audiohandler.h"
#include "icomudpbase.h"
#include "rigcommander.h"

class yaesuServer : public rigServer
{
	Q_OBJECT

public:

    yaesuServer(SERVERCONFIG* config, rigServer* parent = nullptr);
    ~yaesuServer();

public slots:
	void init();
	void dataForServer(QByteArray);
	void receiveAudioData(const audioPacket &data);

private:

	struct CLIENT {
		bool connected = false;
        bool authenticated = false;
		QString type;
		QHostAddress ipAddress;
		quint16 port;
        quint16 catPort = 0;
        quint16 audioPort = 0;
        quint16 scopePort = 0;
        QByteArray clientName;
		QDateTime	timeConnected;
		QDateTime lastHeard;
		bool isStreaming;
		quint16 txBufferLen;
        quint8 guid[GUIDLEN];

	};

    struct PendingDatagram {
        QHostAddress address;
        quint16 port = 0;
        QByteArray payload;
    };

    void incomingControl();
    void incomingCat();
    void incomingAudio();
    void incomingScope();
    void processControl(const PendingDatagram& datagram);
    void processCat(const PendingDatagram& datagram);
    void processAudio(const PendingDatagram& datagram);
    void processScope(const PendingDatagram& datagram);
    bool authenticate(const yaesuC2R_LoginFrame* login) const;
    CLIENT* clientForSession(quint32 session);
    CLIENT* clientForEndpoint(const QHostAddress& address, quint16 port, quint16 CLIENT::*memberPort);
    void sendControlFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len);
    void sendCatFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len);
    void sendAudioFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len);
    void sendScopeFrame(const QHostAddress& address, quint16 port, const void* data, qsizetype len);
    void sendFrame(QUdpSocket* socket, const QHostAddress& address, quint16 port, const void* data, qsizetype len);
    QByteArray decodeFrame(const QByteArray& encoded) const;
    QByteArray encodeFrame(const void* data, qsizetype len) const;
    void sendResult(QUdpSocket* socket, const QHostAddress& address, quint16 port, yaesuDeviceId device, yaesuMessageId message, quint32 result);
    void sendHealth(QUdpSocket* socket, const QHostAddress& address, quint16 port, yaesuDeviceId device);

    QUdpSocket* controlSocket = nullptr;
    QUdpSocket* catSocket = nullptr;
    QUdpSocket* audioSocket = nullptr;
    QUdpSocket* scopeSocket = nullptr;
    QMap<quint32, CLIENT*> clients;
    quint32 nextSession = 1;
    quint16 txCatPacketId = 0;
};


#endif // YAESUSERVER_H
