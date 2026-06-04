#ifndef RIGSERVER_H
#define RIGSERVER_H

#include <QObject>

#include <QDebug>
#include <QHostAddress>

#include "packettypes.h"
#include "rigidentities.h"
#include "rigcommander.h"
#include "rtpaudio.h"

struct SERVERUSER {
	QString username;
	QString password;
	quint8 userType;
};


struct RIGCONFIG {
	QString serialPort;
	quint32 baudRate;
    quint16 civAddr;
	bool civIsRadioModel;
	bool hasWiFi = false;
	bool hasEthernet=false;
    pttType_t pttType=pttCIV;
	audioSetup rxAudioSetup;
	audioSetup txAudioSetup;    
	QString modelName;
	QString rigName;
#pragma pack(push, 1)
	union {
		struct {
			quint8 unused[7];        // 0x22
			quint16 commoncap;      // 0x27
			quint8 unusedb;           // 0x29
			quint8 macaddress[6];     // 0x2a
        };
		quint8 guid[GUIDLEN];                  // 0x20
	};
#pragma pack(pop)
	bool rigAvailable=false;
    rigCapabilities* rigCaps = nullptr;
	rigCommander* rig = nullptr;
	QThread* rigThread = nullptr;
    audioHandlerBase* rxaudio = nullptr;
	QThread* rxAudioThread = nullptr;
    audioHandlerBase* txaudio = nullptr;
	QThread* txAudioThread = nullptr;
	QTimer* rxAudioTimer = nullptr;
	QTimer* connectTimer = nullptr;
    rtpAudio* rtp = nullptr;
    QThread* rtpThread = nullptr;
    quint8 waterfallFormat;
     quint64 queueInterval=1000;
};


struct SERVERCONFIG {
	bool enabled;
    bool disableUI;
	bool lan;
	quint16 controlPort;
	quint16 civPort;
	quint16 audioPort;
    quint16 scopePort;
    QString listenAddress;
	int audioOutput;
	int audioInput;
	quint8 resampleQuality;
	quint32 baudRate;
	QList <SERVERUSER> users;
	QList <RIGCONFIG*> rigs;
};
Q_DECLARE_METATYPE(SERVERCONFIG)

enum prefServerItem {
    s_enabled = 1 << 0,
    s_disableui = 1 << 1,
    s_lan = 1 << 2,
    s_controlPort = 1 << 3,
    s_civPort = 1 << 4,
    s_audioPort = 1 << 5,
    s_scopePort = 1 << 6,
    s_audioOutput = 1 << 7,
    s_audioInput = 1 << 8,
    s_resampleQuality = 1 << 9,
    s_baudRate = 1 << 10,
    s_users = 1 << 11,
    s_rigs = 1 << 12,
    s_listenAddress = 1 << 13,
    s_all = 1 << 14
};

Q_DECLARE_FLAGS(prefServerItems, prefServerItem)
Q_FLAGS(prefServerItems)


class rigServer : public QObject
{
	Q_OBJECT

public:
    explicit rigServer(SERVERCONFIG* config, QObject* parent = nullptr);
    ~rigServer();

public slots:
    virtual void init();
    virtual void dataForServer(QByteArray);
    virtual void receiveAudioData(const audioPacket &data);

    void receiveRigCaps(rigCapabilities* caps);

signals:
	void haveDataFromServer(QByteArray);
	void haveAudioData(audioPacket data);
	void haveNetworkStatus(networkStatus);

	void setupTxAudio(audioSetup);
	void setupRxAudio(audioSetup);

protected:
	SERVERCONFIG *config;
    cachingQueue *queue;
    networkStatus status;
    QHostAddress serverListenAddress() const;
    QString serverListenHardwareAddress(const QHostAddress& address) const;
    quint32 serverEndpointId(const QHostAddress& address, quint16 port) const;

private:
    // rigServer should have no private vars as it is only ever subclassed.

};


#endif // RIGSERVER_H
