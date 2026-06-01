#ifndef VSPHANDLER_H
#define VSPHANDLER_H

#include <QObject>

#include <QMutex>
#include <QDataStream>
#include <QIODevice>
#include <QSocketNotifier>
#include <QtSerialPort/QSerialPort>

#include "rigidentities.h"
#include "wfviewtypes.h"
#include "cachingqueue.h"

// This class abstracts the comm port in a useful way and connects to
// the command creator and command parser.

class vspHandler : public QObject
{
    Q_OBJECT

public:
    explicit vspHandler(QString portName, QObject* parent = nullptr);
    vspHandler(QString portName, quint32 baudRate);
    bool serialError;

    ~vspHandler();

private slots:
    void receiveDataIn(int fd); // from physical port
    void receiveDataFromRigToVsp(const QByteArray& data);
    void debugThis();
    void receiveRigCaps(rigCapabilities* rigCaps);

signals:
    void haveTextMessage(QString message); // status, debug only
    void haveDataFromPort(QByteArray data); // emit this when we have data, connect to rigcommander
    void havePortError(errorType err);
    void haveStatusUpdate(const QString text);

private:
    void setupVsp();
    void openPort();
    void closePort();

    void sendDataOut(const QByteArray& writeData); // out to radio
    void debugMe();
    void hexPrint();

    //QDataStream stream;
    QByteArray outPortData;
    QByteArray inPortData;

    //QDataStream outStream;
    //QDataStream inStream;

    quint8 buffer[256];

    QString portName;
    QSerialPort* port = nullptr;
    qint32 baudRate;
    quint8 stopBits;
    bool rolledBack;

    int ptfd; // pseudo-terminal file desc.
    int ptKeepAlive=0; // Used to keep the pty alive after client disconnects.
    bool havePt;
    QString ptDevSlave;

    bool isConnected=false; // port opened
    mutable QMutex mutex;
    void printHex(const QByteArray& pdata, bool printVert, bool printHoriz);
    bool disableTransceive = false;
    QSocketNotifier *ptReader = nullptr;
    quint16 civId=0;
    rigCapabilities* rigCaps = nullptr;
    cachingQueue* queue = nullptr;
};

#endif // VSPHANDLER_H
