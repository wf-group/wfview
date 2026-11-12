#include "vsphandler.h"
#include "logcategories.h"

#include <QDebug>
#include <QFile>

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#endif

// Copyright 2017-2024 Elliott H. Liggett & Phil Taylor

vspHandler::vspHandler(QString pty, QObject* parent) : QObject(parent)
{
    //constructor

    this->setObjectName("vspHandler");

    if (pty == "" || pty.toLower() == "none")
    {
        // Just return if VSP is not configured.
        return;
    }

    portName = pty;

#ifdef Q_OS_WIN
    // TODO: The following should become arguments and/or functions
    // Add signal/slot everywhere for comm port setup.
    // Consider how to "re-setup" and how to save the state for next time.
    baudRate = 115200;
    stopBits = 1;
    portName = pty;
#endif

    openPort();

}


void vspHandler::openPort()
{
    serialError = false;
    bool success=false;
#ifdef Q_OS_WIN
    port = new QSerialPort();
    port->setPortName(portName);
    port->setBaudRate(baudRate);
    port->setStopBits(QSerialPort::OneStop);// OneStop is other option
    success = port->open(QIODevice::ReadWrite);

    if (success) {
        connect(port, &QSerialPort::readyRead, this, std::bind(&vspHandler::receiveDataIn, this, (int)0));
    }
#else
    // Generic method in Linux/MacOS to find a pty
    ptfd = ::posix_openpt(O_RDWR | O_NONBLOCK);

    if (ptfd >=0)
    {
        qInfo(logSerial()) << "Opened pt device: " << ptfd << ", attempting to grant pt status";

        if (grantpt(ptfd))
        {
            qInfo(logSerial()) << "Failed to grantpt";
            return;
        }
        if (unlockpt(ptfd))
        {
            qInfo(logSerial()) << "Failed to unlock pt";
            return;
        }
        // we're good!
        qInfo(logSerial()) << "Opened pseudoterminal, slave name :" << ptsname(ptfd);

        // Open the slave device to keep alive.
        ptKeepAlive = open(ptsname(ptfd), O_RDONLY);

        ptReader = new QSocketNotifier(ptfd, QSocketNotifier::Read, this);
        connect(ptReader, &QSocketNotifier::activated,
                this, &vspHandler::receiveDataIn);

        success=true;
    }
#endif

    if (!success)
    {
        ptfd = 0;
        qWarning(logSerial()) << "Could not open pseudo terminal port, please restart.";
        isConnected = false;
        serialError = true;
        emit havePortError(errorType(portName, "Could not open pseudo terminal port. Please restart."));
        return;
    }


#ifndef Q_OS_WIN
    ptDevSlave = QString::fromLocal8Bit(ptsname(ptfd));

    if (portName != "" && portName.toLower() != "none")
    {
        if (!QFile::link(ptDevSlave, portName))
        {
            qInfo(logSerial()) << "Error creating link to" << ptDevSlave << "from" << portName;
        } else {
            qInfo(logSerial()) << "Created link to" << ptDevSlave << "from" << portName;
        }
    }
#endif

    isConnected = true;
}

vspHandler::~vspHandler()
{
    this->closePort();
}

void vspHandler::receiveDataFromRigToVsp(const QByteArray& data)
{
    if (isConnected)
        sendDataOut(data);
}

void vspHandler::sendDataOut(const QByteArray& writeData)
{
    qint64 bytesWritten = 0;

    //qInfo(logSerial()) << "Data to pseudo term:" << writeData.toHex(' ');
    //printHex(writeData, false, true);
    if (isConnected) {
        mutex.lock();
#ifdef Q_OS_WIN
        bytesWritten = port->write(writeData);
#else
        bytesWritten = ::write(ptfd, writeData.constData(), writeData.size());
#endif
        if (bytesWritten != writeData.length()) {
            qInfo(logSerial()) << "bytesWritten: " << bytesWritten << " length of byte array: " << writeData.length()\
                << " size of byte array: " << writeData.size()\
                << " Wrote all bytes? " << (bool)(bytesWritten == (qint64)writeData.size());
        }
        mutex.unlock();
    }
}

void vspHandler::receiveDataIn(int fd) {

#ifndef Q_OS_WIN
    ssize_t available = 255; // Read up to 'available' bytes
#else
    Q_UNUSED(fd);
#endif

   // Linux will correctly return the number of available bytes with the FIONREAD ioctl
   // Sadly MacOS always returns zero!
#ifdef Q_OS_LINUX
    int ret = ::ioctl(fd, FIONREAD, (char *) &available);
    if (ret != 0)
        return;
#endif

#ifdef Q_OS_WIN
    port->startTransaction();
    inPortData = port->readAll();
#else
    inPortData.resize(available);
    ssize_t got = ::read(fd, inPortData.data(), available);
    int err = errno;
    if (got < 0) {
        qInfo(logSerial()) << tr("Read failed: %1").arg(QString::fromLatin1(strerror(err)));
        return;
    }
    inPortData.resize(got);
#endif

#ifdef Q_OS_WIN
    port->commitTransaction();
#endif

    if (!inPortData.isEmpty())
        emit haveDataFromPort(inPortData);
}



void vspHandler::closePort()
{
#ifdef Q_OS_WIN
    if (port != nullptr)
    {
        port->close();
        delete port;
    }
#else
    if (isConnected && portName != "" && portName.toLower() != "none")
    {
        QFile::remove(portName);
    }

    if (ptKeepAlive > 0) {
        close(ptKeepAlive);
    }
#endif
    isConnected = false;
}


void vspHandler::debugThis()
{
    // Do not use, function is for debug only and subject to change.
    qInfo(logSerial()) << "comm debug called.";

    inPortData = port->readAll();
    emit haveDataFromPort(inPortData);
}



void vspHandler::printHex(const QByteArray& pdata, bool printVert, bool printHoriz)
{
    qDebug(logSerial()) << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for (int i = 0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i, 8, 10, QChar('0')).arg((quint8)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((quint8)pdata[i], 2, 16, QChar('0')));
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if (printVert)
    {
        for (int i = 0; i < strings.length(); i++)
        {
            //sdata = QString(strings.at(i));
            qDebug(logSerial()) << strings.at(i);
        }
    }

    if (printHoriz)
    {
        qDebug(logSerial()) << index;
        qDebug(logSerial()) << sdata;
    }
    qDebug(logSerial()) << "----- End hex dump -----";
}
