#ifndef CLUSTER_H
#define CLUSTER_H

#include <QObject>
#include <QDebug>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QDomDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

#ifdef USESQL
#include <QSqlDatabase>
#include <QSqlQuery>
#endif

#ifdef USESQL
#include "database.h"
#endif

struct spotData {
    QString dxcall;
    double frequency;
    QString spottercall;
    QDateTime timestamp;
    QString mode;
    QString comment;
    int timeout; // lifetime in seconds, 0 = never expires
    bool current = false;
};
Q_DECLARE_METATYPE(spotData)

struct clusterSettings {
    QString server;
    int port=7300;
    QString userName;
    QString password;
    int timeout=60; // spot lifetime in seconds
    bool isdefault;
};

struct rangeValues {
    rangeValues() {};
    rangeValues(double low, double high): low(low), high(high) {};
    double low;
    double high;
};

class dxClusterClient : public QObject
{
    Q_OBJECT

public:
    enum tcpStatus {
        tcpNotConnected = 0,
        tcpConnecting,
        tcpConnectedNoTraffic,
        tcpConnectedTraffic,
        tcpError
    };

    explicit dxClusterClient(QObject* parent = nullptr);
    virtual ~dxClusterClient();
    static bool parseTcpSpotLine(const QString &line, int timeout, spotData *spot);

    static constexpr int tcpConnectTimeoutMs = 60000;
    static constexpr int spotLifetimeMin = 10;
    static constexpr int spotLifetimeMax = 999;

signals:
    void addSpot(spotData* spot);
    void deleteSpot(QString dxcall);
    void deleteOldSpots(int minutes);
    void sendOutput(QString text);
    void sendSpots(quint8 receiver, QList<spotData> spots);
    void sendSubSpots(QList<spotData> spots);
    void tcpStatusChanged(int status);

public slots:
    void udpDataReceived();
    void tcpDataReceived();
    void tcpDisconnected();
    void tcpConnected();
    void tcpSocketError();
    void tcpConnectTimedOut();
    void enableUdp(bool enable);
    void enableTcp(bool enable);
    void reconnectTcpIfChanged();
    void setUdpPort(int p) { udpPort = p; }
    void setTcpServerName(QString s) { tcpServerName = s; clearStaleStatus(); }
    void setTcpPort(int p) { tcpPort = p; clearStaleStatus(); }
    void setTcpUserName(QString s) { tcpUserName = s; clearStaleStatus(); }
    void setTcpPassword(QString s) { tcpPassword = s; clearStaleStatus(); }
    void setTcpTimeout(int p) { tcpTimeout = p; }
    void setUdpLifetime(int seconds) { udpLifetime = seconds; }
    void tcpCleanup();
    void freqRange(quint8 receiver, double low, double high);
    void enableSkimmerSpots(bool enable);
    void shutdown();

private:
    void sendTcpData(QString data);
    bool databaseOpen();
    void updateSpots();
    void storeSpot(spotData* spot);
    void openTcp();
    void closeTcp(bool sayBye);
    void failTcp(const QString &reason);
    void setTcpStatus(tcpStatus s);
    bool targetChanged() const;
    void clearStaleStatus();
    void ensureCleanupTimer();

    bool udpEnable;
    bool tcpEnable;
    QUdpSocket* udpSocket=nullptr;
    QTcpSocket* tcpSocket=nullptr;
    int udpPort;
    QString tcpServerName;
    int tcpPort;
    QString tcpUserName;
    QString tcpPassword;
    int tcpTimeout = 60;
    int udpLifetime = 0; // seconds, 0 = keep until the sender deletes the spot
    QString connectedServerName;
    int connectedPort = 0;
    QString connectedUserName;
    QString connectedPassword;
    tcpStatus status = tcpNotConnected;
    QDomDocument udpSpotReader;
    QRegularExpression tcpRegex;
    QString tcpReceiveBuffer;
    QMutex mutex;
    bool authenticated=false;
    QTimer* tcpCleanupTimer=nullptr;
    QTimer* tcpConnectTimer=nullptr;
#ifdef USESQL
    QSqlDatabase db;
#endif
    double lowMainFreq;
    double highMainFreq;
    double lowSubFreq;
    double highSubFreq;
    QMap<uchar,rangeValues> freqRanges;
    QMap<QString,spotData*> allSpots;
    bool skimmerSpots = false;
    bool shuttingDown = false;
};

Q_DECLARE_METATYPE(clusterSettings)

#endif
