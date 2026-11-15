// Class for all (pseudo) serial communications
#ifndef ICOMUDPCIVDATA_H
#define ICOMUDPCIVDATA_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUuid>

// Allow easy endian-ness conversions
#include <QtEndian>

// Needed for audio
#include <QBuffer>
#include <QThread>

#include <QDebug>

#include "packettypes.h"

#include "icomudpbase.h"

class icomUdpCivData : public icomUdpBase
{
	Q_OBJECT

public:
	icomUdpCivData(QHostAddress local, QHostAddress ip, quint16 civPort, bool splitWf, quint16 lport);
	~icomUdpCivData();
	// Shutdown: tells the radio to stop sending CI-V (the destructor then skips it).
	void sendClose();
	QMutex serialmutex;

signals:
	int receive(QByteArray);

public slots:
	void send(QByteArray d);


private:
	void watchdog();
	void dataReceived();
	void sendOpenClose(bool close);

	QTimer* startCivDataTimer = nullptr;
	bool splitWaterfall = false;
	bool closeSent = false;
};


#endif
