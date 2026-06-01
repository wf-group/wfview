#ifndef YAESUCOMMANDER_H
#define YAESUCOMMANDER_H
#include "rigcommander.h"
#include "rtpaudio.h"
#include <QSerialPort>
#include <ft4222handler.h>

#include "yaesuudpcontrol.h"
#include "yaesuudpcat.h"
#include "yaesuudpaudio.h"
#include "yaesuudpscope.h"

#define audioLevelBufferSize (4)

// This file figures out what to send to the comm and also
// parses returns into useful things.

#define NUMTOASCII  48

class yaesuCommander : public rigCommander
{
    Q_OBJECT

public:
    explicit yaesuCommander(rigCommander* parent=nullptr);
    explicit yaesuCommander(quint8 guid[GUIDLEN], rigCommander* parent = nullptr);
    ~yaesuCommander();

public slots:
    void process() override;
    void serialCommSetup(rigTypedef rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, quint16 tcp, quint8 wf) override;
    void networkCommSetup(rigTypedef rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp) override;

    void lanConnected();
    void lanDisconnected();

    void closeComm() override;
    void setPTTType(pttType_t) override;

    // Power:
    void powerOn() override;
    void powerOff() override;


    // Rig ID and CIV:
    void setRigID(quint16 rigID) override;
    void setCIVAddr(quint16 civAddr) override;

    // UDP:
    void handleNewData(const QByteArray& data) override;
    void receiveBaudRate(quint32 baudrate) override;
    void receiveTxAudioData(const audioPacket &packet) override;

    void receiveCatDataFromRig(QByteArray in);
    void receiveScopeDataFromRig(QByteArray in);
    void receiveAudioDataFromRig(QByteArray in);


    // Housekeeping:
    void receiveCommand(funcs func, QVariant value, uchar receiver) override;

    // Serial:
    void serialPortError(QSerialPort::SerialPortError err);
    void receiveDataFromRig();


    void parseData(QByteArray dataInput);
    void haveScopeData(QByteArray d);
    void dataForRig(QByteArray data);

signals:
    // All other signals are defined in rigcommander.h as they should be common for all rig types.
    void initUdpControl();
    void initScope();
    void setScopePoll(quint64 poll);
    void sendDataToRig(QByteArray d);

private:
    void commonSetup();
    funcType getCommand(funcs func, QByteArray &payload, int value, uchar receiver=0);
    bool parseMemory(QByteArray data, QVector<memParserFormat>* memParser, memoryType* mem);

    mutable QMutex serialMutex;
    QIODevice *port=nullptr;
    bool portConnected=false;
    bool isTransmitting = false;
    QByteArray lastSentCommand;

    vspHandler* vspPort = nullptr;
    tcpServer* tcp = nullptr;

    yaesuUdpControl* control = nullptr;
    QThread* controlThread = nullptr;
    yaesuUdpCat* cat = nullptr;
    QThread* catThread = nullptr;
    yaesuUdpAudio* audio = nullptr;
    QThread* audioThread = nullptr;
    yaesuUdpScope* scope = nullptr;
    QThread* scopeThread = nullptr;

    quint16 rigCivAddr;
    QString vsp;
    quint16 tcpPort;
    quint8 wf;

    QString rigSerialPort;
    quint32 rigBaudRate;

    scopeData currentScope;
    bool loginRequired = false;
    bool audioStarted = false;

    bool network = false;

    QByteArray partial;

    connectionType_t connType = connectionUSB;

    bool aiModeEnabled=false;
    ushort scopeSplit=0;

    genericType scopeMode;
    centerSpanData scopeSpan;
    ft4222Handler* ft4222 = nullptr;
};




#endif // YAESUCOMMANDER_H
