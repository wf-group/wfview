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
#include "MemoriesModel.h"

#include "rigcommander.h"
#include "icomcommander.h"
#include "kenwoodcommander.h"
#include "yaesucommander.h"
#include "themebridge.h"

#include "SettingsController.h"

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(MemoriesModel* memoriesModel READ memoriesModel NOTIFY memoriesModelChanged)
    Q_PROPERTY(bool slowLoad READ slowLoad WRITE setSlowLoad NOTIFY slowLoadChanged)

public:

    enum connectionStatus_t { connDisconnected, connConnecting, connConnected };
    Q_ENUM (connectionStatus_t)

    enum underlay_t { underlayNone, underlayPeakHold, underlayPeakBuffer, underlayAverageBuffer };
    Q_ENUM (underlay_t)

    enum connectionType_t { connectionUSB, connectionLAN, connectionWiFi, connectionWAN };
    Q_ENUM (connectionType_t)

    Q_PROPERTY(QString windowTitle READ getWindowTitle WRITE setWindowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(int receiverCount READ receiverCount NOTIFY receiverCountChanged)
    Q_PROPERTY(SettingsController* settings READ getSettings CONSTANT)
    Q_PROPERTY(connectionStatus_t connStatus READ getConnStatus WRITE setConnStatus NOTIFY connStatusChanged)
    Q_PROPERTY(ThemeBridge* theme READ theme CONSTANT)

    Q_PROPERTY(quint64 stepSize READ getStepSize WRITE setStepSize NOTIFY stepSizeChanged)

    Q_PROPERTY(QVariantMap uiSpecs READ getUiSpecs NOTIFY uiSpecsChanged)    

    Q_DECLARE_FLAGS(prefIfItems, prefIfItem)
    Q_DECLARE_FLAGS(prefColItems, prefColItem)
    Q_DECLARE_FLAGS(prefRsItems, prefRsItem)
    Q_DECLARE_FLAGS(prefRaItems ,prefRaItem)
    Q_DECLARE_FLAGS(prefCtItems, prefCtItem)
    Q_DECLARE_FLAGS(prefLanItems, prefLanItem)
    Q_DECLARE_FLAGS(prefClusterItems, prefClusterItem)
    Q_DECLARE_FLAGS(prefUDPItems, prefUDPItem)
    Q_DECLARE_FLAGS(prefServerItems, prefServerItem)
    Q_FLAGS(prefIfItems)
    Q_FLAGS(prefColItems)
    Q_FLAGS(prefRsItems)
    Q_FLAGS(prefRaItems)
    Q_FLAGS(prefCtItems)
    Q_FLAGS(prefLanItems)
    Q_FLAGS(prefClusterItems)
    Q_FLAGS(prefUDPItems)
    Q_FLAGS(prefServerItems)

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

    ThemeBridge* theme() { return &m_theme; }

    Q_INVOKABLE void syncTheme(QObject* obj) {
        m_theme.syncFromAppPalette();
    }

    connectionStatus_t getConnStatus() { return connStatus;}

    void setConnStatus(connectionStatus_t c) {
        if (c != connStatus) {
            connStatus =c;
            emit connStatusChanged();
        }
    }

    quint64 getStepSize() { return stepSize;}
    void setStepSize(quint64 s);
    QVariantMap getUiSpecs() const { return uiSpecs; }

    MemoriesModel* memoriesModel() const { return m_memoriesModel; }
    bool slowLoad() const { return m_slowLoad; }

    void setSlowLoad(bool value) {
        if (m_slowLoad != value) {
            m_slowLoad = value;
            emit slowLoadChanged();

            if (m_slowLoad && !m_memoriesModel) {
                createMemoriesModel();
            }
        }
    }

    Q_INVOKABLE void openMemories() {
        if (!m_memoriesModel) {
            createMemoriesModel();
        }
    }

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
    void connStatusChanged();

    void stepSizeChanged();
    void uiSpecsChanged();

    // Memories
    void memoryReceived(memoryType mem);
    void memoryNameReceived(memoryTagType tag);
    void memorySplitReceived(memorySplitType split);

    void memoriesModelChanged();
    void slowLoadChanged();

public slots:
    void onRadioPacket(const QByteArray &packet);
    void setWindowTitle(const QString &t);
    void connectionHandler();
    void shutdown();

private slots:
    void receiveValueFromQueue(cacheItem c);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveCommReady();

    void ifChanged(prefIfItems items);
    void colChanged(prefColItems items);
    //void rsChanged(prefRsItems items);
    //void raChanged(prefRaItems items);
    //void ctChanged(prefCtItems items);
    //void lanChanged(prefLanItems items);
    //void clusterChanged(prefClusterItems items);
    //void udpChanged(prefUDPItems items);
    //void serverChanged(prefServerItems items);

private:
    void buildUiSpecs();
    QVariantMap uiSpecs;

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

    ThemeBridge m_theme;

    quint64 stepSize=100; // should this be a setting?

    void createMemoriesModel() {
        if (!m_memoriesModel) {
            m_memoriesModel = new MemoriesModel(this);
            // Set any properties needed
            m_memoriesModel->setIsAdmin(false);
            m_memoriesModel->setSlowLoad(m_slowLoad);
            m_memoriesModel->populate();
            emit memoriesModelChanged();
        }
    }

    MemoriesModel* m_memoriesModel = nullptr;
    bool m_slowLoad = false;

    QVariantMap frequencyDisplay;
};

#endif // MAINCONTROLLER_H
