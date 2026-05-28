#ifndef TCISERVER_H
#define TCISERVER_H

#include <QObject>
#include <QtWebSockets/QtWebSockets>
#include <QtWebSockets/QWebSocketServer>

#include "audioconverter.h"
#include "cachingqueue.h"

/* Opus and Eigen */
#ifndef Q_OS_LINUX
#include "opus.h"
#include <Eigen/Eigen>
#else
#include "opus/opus.h"
#include <eigen3/Eigen/Eigen>
#endif

#define TCI_AUDIO_SAMPLES 2048
#define TCI_AUDIO_LENGTH 4096
struct tciCommandStruct
{
    const char *str;
    funcs func;
    valueType arg1;
    valueType arg2;
    valueType arg3;
};


class tciServer : public QObject
{
    Q_OBJECT

    static constexpr quint32 iqHeaderSize = 16u*sizeof(quint32);
    static constexpr quint32 iqBufferSize = 2048u;

    typedef enum
    {
        IqInt16 = 0,
        IqInt24,
        IqInt32,
        IqFloat32,
        IqFloat64
    }iqDataType;

    typedef enum
    {
        IqStream = 0,
        RxAudioStream,
        TxAudioStream,
        TxChrono,
    }streamType;

    typedef struct
    {
        quint32 receiver;
        quint32 sampleRate;
        quint32 format;
        quint32 codec;
        quint32 crc;
        quint32 length;
        quint32 type;
        quint32 channels;
        quint32 reserv[8];
        float   data[TCI_AUDIO_LENGTH];
    }dataStream;

    struct connStatus {
        bool connected=true;
        bool rxaudio=false;
        bool txaudio=false;
        bool iqaudio=false;
        bool lineout=false;
        bool rxSensors=false;
        bool txSensors=false;
        int audioSampleRate=48000;
        int audioChannels=2;
        int audioSamples=2048;
        int txAudioBuffering=50;
        iqDataType audioSampleType=IqFloat32;
    };


public:
    explicit tciServer(QObject *parent = nullptr);
    ~tciServer();

signals:
    void closed();
    void sendTCIAudio(QByteArray audio);
    void transmitRequested(bool transmit);

public slots:
    void receiveTCIAudio(audioPacket audio);
    void receiveRigCaps(rigCapabilities* caps);
    void init(quint16 port);


private slots:
    void onNewConnection();
    void processIncomingTextMessage(QString message);
    void processIncomingBinaryMessage(QByteArray message);
    void socketDisconnected();
    void receiveCache(cacheItem item);
    void setupTxPacket(int packetLen);

private:
    int getValueRange(funcs func,char min=0, char max=0, uchar rx=0);
    int fromTciRange(funcs func, int value, int min, int max) const;
    int toTciRange(funcs func, int min, int max, uchar rx=0) const;
    void sendInitialState(QWebSocket *socket);
    void processCommand(QWebSocket *client, const QString &rawCommand);
    QString commandReply(const QString &command, const QStringList &args) const;
    bool setCommandValue(const QString &command, const QStringList &args);
    void updateClientState(QWebSocket *client, const QString &command, const QStringList &args);
    void broadcastText(const QString &message, QWebSocket *except = nullptr);
    QVariant cacheValue(funcs func, uchar receiver = 0) const;
    bool cacheBool(funcs func, uchar receiver = 0, bool fallback = false) const;
    int cacheInt(funcs func, uchar receiver = 0, int fallback = 0) const;
    double cacheDouble(funcs func, uchar receiver = 0, double fallback = 0.0) const;
    freqt cacheFreq(funcs func, uchar receiver = 0) const;
    modeInfo cacheMode(funcs func, uchar receiver = 0) const;
    void queueIfSupported(funcs func, const QVariant &value, uchar receiver = 0, bool unique = false);

    QWebSocketServer *server;
    QMap<QWebSocket *, connStatus> clients;
    cachingQueue *queue;
    QByteArray rxAudioData;
    QByteArray txAudioData;
    QByteArray txChrono;
    quint32 lastTxAudioSampleRate = 0;
    quint8 lastTxAudioChannels = 0;
    qsizetype lastTxAudioPayloadSize = 0;
    quint64 txAudioFrameCount = 0;
    bool loggedFirstTxAudioFrame = false;
    quint32 lastRxAudioSampleRate = 0;
    quint8 lastRxAudioChannels = 0;
    qsizetype lastRxAudioPayloadSize = 0;
    quint64 rxAudioFrameCount = 0;
    bool loggedFirstRxAudioFrame = false;
    rigCapabilities* rigCaps = nullptr;
    QString tciMode(modeInfo m) const;
    modeInfo rigMode(const QString &mode, funcs modeFunc, uchar receiver) const;
    int dBmConversion = 73;
};

#endif // TCISERVER_H
