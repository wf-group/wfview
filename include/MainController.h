#ifndef MAINCONTROLLER_H
#define MAINCONTROLLER_H

#include <QObject>
#include <QSettings>
#include <QShortcut>

#include "ReceiverController.h"
#include "RigCreatorController.h"
#include "prefs.h"
#include "rigctld.h"
#include "tciserver.h"
#include "wfviewtypes.h"
#include "rigserver.h"

#include "rigcommander.h"
#include "icomcommander.h"
#include "kenwoodcommander.h"
#include "yaesucommander.h"

#include "SettingsController.h"

class MainController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString windowTitle READ getWindowTitle WRITE setWindowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(int receiverCount READ receiverCount NOTIFY receiverCountChanged)
    Q_PROPERTY(SettingsController* settings READ getSettings CONSTANT)
public:
    explicit MainController(QString settingsFile, QString logFileName, bool debugMode, QObject *parent=nullptr);

    ~MainController() override { shutdown(); } // still fine as a backup

    QString getWindowTitle() const { return windowTitle; }

    int receiverCount() const { return receivers.size(); }

    Q_INVOKABLE QObject* receiver(int i) const {
        if (i < 0 || i >= receivers.size()) return nullptr;
        return receivers[i];
    }


    Q_INVOKABLE bool isReceiverDetached(int i) const {
        if (i < 0 || i >= detached.size()) return false;
        return detached[i];
    }

    Q_INVOKABLE void setReceiverDetached(int i, bool v) {
        if (i < 0 || i >= detached.size()) return;
        if (detached[i] == v) return;
        detached[i] = v;
        emit receiverDetachedChanged(i, v);
    }
    SettingsController* getSettings() const { return m_settings.get(); }

signals:
    void windowTitleChanged();
    void receiverCountChanged();
    void receiverDetachedChanged(int index, bool detached);
    void detachedChanged();

    void sendSerialCommSetup(rigTypedef rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp, quint16 tcp, quint8 wf);
    void sendNetworkCommSetup(rigTypedef rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp);
    void sendCloseComm();

    void setCIVAddr(quint16 newRigCIVAddr);
    void setRigID(quint16 rigID);
    void setPTTType(pttType_t enabled);

    void sendPowerOn();
    void sendPowerOff();

public slots:
    void onRadioPacket(const QByteArray &packet);
    void setWindowTitle(const QString &t);
    void connectionHandler();
    void shutdown();

private slots:
    void receiveValueFromQueue(cacheItem c);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveCommReady();


private:

    //void setDefPrefs();
    //void loadSettings(QString file); // Look for saved preferences
    void setManufacturer(manufacturersType_t man);
    void setAppTheme(bool isCustom);
    void startRigConnection();
    void getInitialRigState();
    void setInitialTiming();


    QString windowTitle = "wfview";
    QVector<ReceiverController*> receivers;
    QVector<bool> detached;


    QSettings* settings = nullptr;

    preferences* prefs;
    udpPreferences* udpPrefs;
    SERVERCONFIG* serverConfig;

    QHash<quint16,rigInfo> rigList;
    rigCapabilities* rigCaps = nullptr;

    cachingQueue* queue;
    rigServer* server = nullptr;
    QThread* serverThread = nullptr;
    rigCtlD* rigCtl = nullptr;
    tciServer* tci = nullptr;
    QThread* tciThread = nullptr;

    rigCommander * rig = nullptr;
    QThread* rigThread = nullptr;

    connectionStatus_t connStatus = connDisconnected;

    bool shuttingDown = false;

    int cmdInterval_ms = 100;
    int cmdStartupInterval_ms = 250;

    std::unique_ptr<SettingsController> m_settings;

};

#endif // MAINCONTROLLER_H
