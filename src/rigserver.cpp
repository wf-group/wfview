// Copyright 2017-2025 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

#include "rigserver.h"
#include "logcategories.h"

#include <QAbstractSocket>
#include <QHostInfo>
#include <QNetworkInterface>

rigServer::rigServer(SERVERCONFIG* config, QObject* parent) :
    QObject(parent), config(config)
{
    qInfo(logRigServer()) << "Creating instance of rigServer";
    queue=cachingQueue::getInstance();
}

rigServer::~rigServer()
{
    qInfo(logRigServer()) << "Closing instance of rigServer";
}

void rigServer::init()
{
    qWarning(logRigServer()) << "init() not implemented by server type";
}

void rigServer::receiveRigCaps(rigCapabilities* caps)
{
    if (caps == nullptr)
        return;

    foreach (RIGCONFIG* rig, config->rigs) {
        if (!memcmp(rig->guid, caps->guid, GUIDLEN) || config->rigs.size()==1) {
            // Matching rig, fill-in missing details
            qInfo(logRigServer()) << "Received new rigCaps for:" << caps->modelName << "CIV:" << QString::number(caps->modelID,16);
            rig->rigAvailable = true;
            rig->modelName = caps->modelName;
            rig->civAddr = caps->modelID;
            if (rig->rigName == "<NONE>") {
                rig->rigName = caps->modelName;
            }

            qInfo(logRigServer()) << "Model Number:" << rig->civAddr;
            qInfo(logRigServer()) << "Model:" << rig->modelName;
            qInfo(logRigServer()) << "Name:" << rig->rigName;
            qInfo(logRigServer()).noquote() << QString("GUID: {%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
               .arg(rig->guid[0], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[1], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[2], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[3], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[4], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[5], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[6], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[7], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[8], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[9], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[10], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[11], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[12], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[13], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[14], 2, 16, QLatin1Char('0'))
               .arg(rig->guid[15], 2, 16, QLatin1Char('0'));
        }
    }
}

QHostAddress rigServer::serverListenAddress() const
{
    const QString configured = config ? config->listenAddress.trimmed() : QString();
    if (!configured.isEmpty()) {
        const QHostAddress address(configured);
        if (!address.isNull() && !address.isLoopback()) {
            return address;
        }

        qWarning(logRigServer()) << "Configured server listen address is not usable, falling back to automatic selection:" << configured;
    }

    QList<QHostAddress> linkLocal;
    QList<QHostAddress> ipv6Candidates;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& netInterface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = netInterface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = netInterface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            const QHostAddress address = entry.ip();
            if (address.isLoopback() || address.isNull()) {
                continue;
            }

            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                if (address.toString().startsWith(QLatin1String("169.254."))) {
                    linkLocal.append(address);
                    continue;
                }

                return address;
            }

            if (address.protocol() == QAbstractSocket::IPv6Protocol) {
                ipv6Candidates.append(address);
                continue;
            }
        }
    }

    if (!linkLocal.isEmpty())
        return linkLocal.first();

    if (!ipv6Candidates.isEmpty())
        return ipv6Candidates.first();

    const QList<QHostAddress> hostList = QHostInfo::fromName(QHostInfo::localHostName()).addresses();
    for (const QHostAddress& address : hostList) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            return address;
    }
    for (const QHostAddress& address : hostList) {
        if (address.protocol() == QAbstractSocket::IPv6Protocol && !address.isLoopback())
            return address;
    }

    qWarning(logRigServer()) << "No non-loopback IPv4 server listen address found, binding to localhost";
    return QHostAddress(QHostAddress::LocalHost);
}

QString rigServer::serverListenHardwareAddress(const QHostAddress& address) const
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& netInterface : interfaces) {
        const QList<QNetworkAddressEntry> entries = netInterface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (entry.ip() == address)
                return netInterface.hardwareAddress();
        }
    }

    for (const QNetworkInterface& netInterface : interfaces) {
        if (!(netInterface.flags() & QNetworkInterface::IsLoopBack) &&
            !netInterface.hardwareAddress().isEmpty()) {
            return netInterface.hardwareAddress();
        }
    }

    return QString();
}

quint32 rigServer::serverEndpointId(const QHostAddress& address, quint16 port) const
{
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 addr = address.toIPv4Address();
        return (addr >> 8 & 0xff) << 24 | (addr & 0xff) << 16 | (port & 0xffff);
    }

    const quint32 high = qHash(address.toString()) & 0xffff;
    const quint32 id = (high << 16) | (port & 0xffff);
    return id == 0 ? port : id;
}

void rigServer::dataForServer(QByteArray d)
{
    Q_UNUSED(d)
    qWarning(logRigServer()) << "dataForServer() not implemented by server type";
    return;
}


void rigServer::receiveAudioData(const audioPacket& d)
{
    Q_UNUSED(d)
    qWarning(logRigServer()) << "receiveAudioData() not implemented by server type";
    return;
}
