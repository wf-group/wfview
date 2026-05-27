#ifndef MAINCONTROLLER_H
#define MAINCONTROLLER_H

#include <QObject>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include "ReceiverController.h"
#include "RigCreatorController.h"
#include "SelectRadioController.h"
#include "CWSenderController.h"

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
#include "txaudioprocessor.h"
#include "rxaudioprocessor.h"
#include "audiohandler.h"

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(MemoriesModel* memoriesModel READ memoriesModel NOTIFY memoriesModelChanged)
    Q_PROPERTY(bool slowLoad READ slowLoad WRITE setSlowLoad NOTIFY slowLoadChanged)

    Q_PROPERTY(CWSenderController* cwSender READ cwSender CONSTANT)
    Q_PROPERTY(SelectRadioController* selectRadio READ selectRadio CONSTANT)


public:

    enum connectionStatus_t { connDisconnected, connConnecting, connConnected };
    Q_ENUM (connectionStatus_t)

    enum underlay_t { underlayNone, underlayPeakHold, underlayPeakBuffer, underlayAverageBuffer };
    Q_ENUM (underlay_t)

    enum connectionType_t { connectionUSB, connectionLAN, connectionWiFi, connectionWAN };
    Q_ENUM (connectionType_t)

    Q_PROPERTY(QString windowTitle READ getWindowTitle WRITE setWindowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(QString footerMessageText READ footerMessageText NOTIFY footerMessageTextChanged)
    Q_PROPERTY(QString radioStatusText READ radioStatusText NOTIFY radioStatusTextChanged)
    Q_PROPERTY(QString rigModelName READ rigModelName NOTIFY rigModelNameChanged)
    Q_PROPERTY(QString clusterOutputText READ clusterOutputText NOTIFY clusterOutputTextChanged)
    Q_PROPERTY(int receiverCount READ receiverCount NOTIFY receiverCountChanged)
    Q_PROPERTY(SettingsController* settings READ getSettings CONSTANT)
    Q_PROPERTY(connectionStatus_t connStatus READ getConnStatus WRITE setConnStatus NOTIFY connStatusChanged)
    Q_PROPERTY(ThemeBridge* theme READ theme CONSTANT)

    Q_PROPERTY(quint64 stepSize READ getStepSize WRITE setStepSize NOTIFY stepSizeChanged)

    Q_PROPERTY(QVariantMap uiSpecs READ getUiSpecs NOTIFY uiSpecsChanged)    
    Q_PROPERTY(int txPower READ txPower WRITE setTxPower NOTIFY txPowerChanged)
    Q_PROPERTY(int localAfGain READ localAfGain WRITE setLocalAfGain NOTIFY localAfGainChanged)
    Q_PROPERTY(bool localAudioAvailable READ localAudioAvailable NOTIFY localAudioAvailableChanged)
    Q_PROPERTY(int monitorGain READ monitorGain WRITE setMonitorGain NOTIFY monitorGainChanged)
    Q_PROPERTY(bool monitorEnabled READ monitorEnabled WRITE setMonitorEnabled NOTIFY monitorEnabledChanged)
    Q_PROPERTY(int micGain READ micGain WRITE setMicGain NOTIFY micGainChanged)
    Q_PROPERTY(QString modGainLabel READ modGainLabel NOTIFY modSourcesChanged)
    Q_PROPERTY(int modGainMin READ modGainMin NOTIFY modSourcesChanged)
    Q_PROPERTY(int modGainMax READ modGainMax NOTIFY modSourcesChanged)
    Q_PROPERTY(int modSourceRevision READ modSourceRevision NOTIFY modSourcesChanged)
    Q_PROPERTY(int ritFrequency READ ritFrequency WRITE setRitFrequency NOTIFY ritFrequencyChanged)
    Q_PROPERTY(bool transmitting READ transmitting WRITE setTransmit NOTIFY transmittingChanged)
    Q_PROPERTY(bool tunerEnabled READ tunerEnabled WRITE setTunerEnabled NOTIFY tunerEnabledChanged)
    Q_PROPERTY(bool ritEnabled READ ritEnabled WRITE setRitEnabled NOTIFY ritEnabledChanged)
    Q_PROPERTY(bool dualScope READ dualScope WRITE setDualScope NOTIFY dualScopeChanged)
    Q_PROPERTY(bool dualWatch READ dualWatch WRITE setDualWatch NOTIFY dualWatchChanged)
    Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY splitEnabledChanged)
    Q_PROPERTY(bool compressorEnabled READ compressorEnabled WRITE setCompressorEnabled NOTIFY compressorEnabledChanged)
    Q_PROPERTY(bool voxEnabled READ voxEnabled WRITE setVoxEnabled NOTIFY voxEnabledChanged)
    Q_PROPERTY(double optionalMeter2Level READ optionalMeter2Level NOTIFY optionalMetersChanged)
    Q_PROPERTY(double optionalMeter3Level READ optionalMeter3Level NOTIFY optionalMetersChanged)

    Q_DECLARE_FLAGS(prefIfItems, prefIfItem)
    Q_DECLARE_FLAGS(prefRsItems, prefRsItem)
    Q_DECLARE_FLAGS(prefRaItems ,prefRaItem)
    Q_DECLARE_FLAGS(prefCtItems, prefCtItem)
    Q_DECLARE_FLAGS(prefLanItems, prefLanItem)
    Q_DECLARE_FLAGS(prefClusterItems, prefClusterItem)
    Q_DECLARE_FLAGS(prefUDPItems, prefUDPItem)
    Q_DECLARE_FLAGS(prefServerItems, prefServerItem)
    Q_FLAGS(prefIfItems)
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
    QString footerMessageText() const { return m_footerMessageText; }
    QString radioStatusText() const { return m_radioStatusText; }
    QString rigModelName() const { return m_rigModelName; }
    QString clusterOutputText() const { return m_clusterOutputText; }

    int receiverCount() const { return receivers.size(); }

    Q_INVOKABLE void updateApplicationPalette();

    Q_INVOKABLE QObject* receiver(int i) const {
        if (i < 0 || i >= receivers.size()) return nullptr;
        return receivers[i];
    }


    Q_INVOKABLE bool isReceiverDetached(int i) const {
        if (i < 0 || i >= detached.size()) return false;
        return detached[i];
    }

    Q_INVOKABLE void setReceiverDetached(int i, bool v);

    SettingsController* getSettings() const { return m_settings.get(); }
    SelectRadioController* selectRadio() const { return m_selRad.get(); }
    CWSenderController* cwSender() const { return m_cwSender.get(); }

    ThemeBridge* theme() { return &m_theme; }

    Q_INVOKABLE void syncTheme(QObject* obj) {
        Q_UNUSED(obj)
        m_theme.syncFromAppPalette();
    }


    // Method to show the radio selector
    Q_INVOKABLE void showSelectRadio() {
        m_selRad->setVisible(true);
    }

    Q_INVOKABLE void showCWSender()
    {
        m_cwSender->setVisible(true);
    }


    connectionStatus_t getConnStatus() { return connStatus;}

    void setConnStatus(connectionStatus_t c) {
        if (c != connStatus) {
            connStatus =c;
            emit connStatusChanged();
            emit localAudioAvailableChanged();
        }
    }

    quint64 getStepSize() { return stepSize;}
    void setStepSize(quint64 s);
    QVariantMap getUiSpecs() const { return uiSpecs; }
    int txPower() const { return m_txPower; }
    int localAfGain() const { return prefs ? int(prefs->localAFgain) : 0; }
    bool localAudioAvailable() const;
    int monitorGain() const { return m_monitorGain; }
    bool monitorEnabled() const { return m_monitorEnabled; }
    int micGain() const { return m_micGain; }
    QString modGainLabel() const { return m_modGainLabel; }
    int modGainMin() const { return m_modGainMin; }
    int modGainMax() const { return m_modGainMax; }
    int modSourceRevision() const { return m_modSourceRevision; }
    int ritFrequency() const { return m_ritFrequency; }
    bool transmitting() const { return m_transmitting; }
    bool tunerEnabled() const { return m_tunerEnabled; }
    bool ritEnabled() const { return m_ritEnabled; }
    bool dualScope() const { return m_dualScope; }
    bool dualWatch() const { return m_dualWatch; }
    bool splitEnabled() const { return m_splitEnabled; }
    bool compressorEnabled() const { return m_compressorEnabled; }
    bool voxEnabled() const { return m_voxEnabled; }
    double optionalMeter2Level() const { return m_optionalMeter2Level; }
    double optionalMeter3Level() const { return m_optionalMeter3Level; }

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

    Q_INVOKABLE void applyAudioProcessingSettings();
    Q_INVOKABLE void ensureAudioProcessors();
    Q_INVOKABLE void startAnrNoiseProfile();
    Q_INVOKABLE void stopAnrNoiseProfile();
    Q_INVOKABLE void powerOn();
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void toggleTransmit();
    Q_INVOKABLE void tuneNow();
    Q_INVOKABLE void setFrequencyLock(bool locked);
    Q_INVOKABLE void selectMainSub();
    Q_INVOKABLE void swapMainSub();
    Q_INVOKABLE void equalizeMainSub();
    Q_INVOKABLE QVariantList optionalMeterOptions() const;
    Q_INVOKABLE QVariantMap optionalMeterExtremities(int meterType) const;
    Q_INVOKABLE bool isOptionalMeterAvailable(int meterType) const;
    Q_INVOKABLE void setOptionalMeterType(int slot, int meterType);
    Q_INVOKABLE QVariantList modSourceOptions(int dataMode) const;
    Q_INVOKABLE int modSourceReg(int dataMode) const;
    Q_INVOKABLE bool modSourceSupported(int dataMode) const;
    Q_INVOKABLE void setModSource(int dataMode, int reg);
    Q_INVOKABLE void syncRadioClock();
    Q_INVOKABLE void connectCluster();
    Q_INVOKABLE void disconnectCluster();
    Q_INVOKABLE void clearClusterOutput();
    Q_INVOKABLE void testWfShareConnection();
    Q_INVOKABLE void resetUsbControllers();
    Q_INVOKABLE void revertSettingsToDefault();
    Q_INVOKABLE QVariantMap restoredMainWindowGeometry() const;
    Q_INVOKABLE void saveMainWindowGeometry(int x, int y, int width, int height, bool maximized);

public slots:
    void setTxPower(int value);
    void setLocalAfGain(int value);
    void setMonitorGain(int value);
    void setMonitorEnabled(bool enabled);
    void setMicGain(int value);
    void setRitFrequency(int value);
    void setTransmit(bool enabled);
    void setTunerEnabled(bool enabled);
    void setRitEnabled(bool enabled);
    void setDualScope(bool enabled);
    void setDualWatch(bool enabled);
    void setSplitEnabled(bool enabled);
    void setCompressorEnabled(bool enabled);
    void setVoxEnabled(bool enabled);

signals:
    void windowTitleChanged();
    void footerMessageTextChanged();
    void radioStatusTextChanged();
    void rigModelNameChanged();
    void receiverCountChanged();
    void receiverDetachedChanged(int index, bool detached);
    void detachedChanged();

    void sendSerialCommSetup(rigTypedef rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp, quint16 tcp, quint8 wf);
    void sendNetworkCommSetup(rigTypedef rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcp);
    void sendWfShareCommSetup(rigTypedef rigList, quint16 rigCivAddr, QString host, quint16 port,
                              QString username, QString password, QString calledNumber,
                              audioSetup rxSetup, audioSetup txSetup);
    void sendCloseComm();
    void sendLocalAudioVolume(quint8 level);

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
    void txAudioSpectrumChanged(QVariantList input, QVariantList output, double sampleRate);
    void rxAudioSpectrumChanged(QVariantList input, QVariantList output, double sampleRate);
    void txAudioMetersChanged(double input, double output, double gainReduction);
    void rxAudioMetersChanged(double input, double output);
    void audioProcessingBlocksChanged(quint64 txBlocks, quint64 rxBlocks);
    void audioProcessingSpectrumStateChanged(bool txEnabled, bool rxEnabled);
    void txPowerChanged();
    void localAfGainChanged();
    void localAudioAvailableChanged();
    void monitorGainChanged();
    void monitorEnabledChanged();
    void micGainChanged();
    void modSourcesChanged();
    void ritFrequencyChanged();
    void transmittingChanged();
    void tunerEnabledChanged();
    void ritEnabledChanged();
    void dualScopeChanged();
    void dualWatchChanged();
    void splitEnabledChanged();
    void compressorEnabledChanged();
    void voxEnabledChanged();
    void optionalMetersChanged();
    void clusterOutputTextChanged();

public slots:
    void onRadioPacket(const QByteArray &packet);
    void setWindowTitle(const QString &t);
    void connectionHandler();
    void shutdown();
    Q_INVOKABLE void quitApplication();

private slots:
    void receiveValueFromQueue(cacheItem c);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveStatusUpdate(networkStatus status);
    void receiveCommReady();
    void receivePortError(errorType err);
    void ctChanged(SettingsController::prefCtItems items);
    void clusterChanged(SettingsController::prefClusterItems items);
#if defined(USB_CONTROLLER)
    void changeFrequency(int value);
    void doShuttle(bool up, quint8 level);
    void buttonControl(const COMMAND* cmd);
#endif

    void ifChanged(prefIfItems items);
    void colChanged(prefColItems items);
    void lanChanged(SettingsController::prefLanItems items);
    //void rsChanged(prefRsItems items);
    void raChanged(prefRaItems items);
    //void ctChanged(prefCtItems items);
    //void clusterChanged(prefClusterItems items);
    //void udpChanged(prefUDPItems items);
    //void serverChanged(prefServerItems items);

private:
    void buildUiSpecs();
    void setRadioStatusText(const QString& text);
    void setFooterMessageText(const QString& text);
    void setRigModelName(const QString& modelName);
    funcs modSourceCommand(int dataMode) const;
    funcType inputLevelCommand(inputTypes input) const;
    uchar currentDataMode() const;
    void receiveModInput(const rigInput& input, int dataMode);
    void processModLevel(inputTypes input, int level);
    void updateCurrentModSource(bool requestLevel);
    void handleDataModeChanged(int receiver);
#if defined(USB_CONTROLLER)
    void setupUsbControllerDevice();
    void stopUsbControllerDevice();
#endif
    funcs meterCommandForType(meter_t meterType) const;
    void configureOptionalMeter(int slot, meter_t meterType);
    void receiveOptionalMeter(meter_t meterType, double level);
    QVariantMap uiSpecs;

    //void setDefPrefs();
    //void loadSettings(QString file); // Look for saved preferences
    void setManufacturer(manufacturersType_t man);
    void setAppTheme(bool isCustom);
    void openRigConnection();
    void startRigConnection();
    void getInitialRigState();
    void setInitialTiming();
    void applyTxAudioProcPrefs(const txAudioProcessingPrefs& p);
    void applyRxAudioProcPrefs(const rxAudioProcessingPrefs& p);
    void prepareRadioClockSync();
    void sendRadioClockSync();
    void setupClusterClient();
    void stopClusterClient();
    void applyClusterSettingsToClient(bool includeTcpEnable);
    void appendClusterOutput(const QString& text);
    void receiveClusterSpots(quint8 receiver, const QList<spotData> &spots);
    void setupRigCtlServer();
    void stopRigCtlServer();
    void setupTciServer();
    void stopTciServer();
    void setupUsbAudioBridge();
    void stopUsbAudioBridge();
    audioHandlerBase* createAudioHandler(const audioSetup& setup);
    void disposeAudioHandler(audioHandlerBase*& handler, QThread*& thread);


    QString windowTitle = "wfview";
    QString m_footerMessageText;
    QString m_radioStatusText;
    QString m_rigModelName;
    QString m_clusterOutputText;
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
    dxClusterClient* cluster = nullptr;
    QThread* clusterThread = nullptr;

    rigCommander * rig = nullptr;
    QThread* rigThread = nullptr;
    TxAudioProcessor* txProc = nullptr;
    RxAudioProcessor* rxProc = nullptr;
    audioHandlerBase* usbRadioRxAudio = nullptr;
    QThread* usbRadioRxThread = nullptr;
    audioHandlerBase* usbRxOutputAudio = nullptr;
    QThread* usbRxOutputThread = nullptr;
    audioHandlerBase* usbTxInputAudio = nullptr;
    QThread* usbTxInputThread = nullptr;
    audioHandlerBase* usbRadioTxAudio = nullptr;
    QThread* usbRadioTxThread = nullptr;
    double m_txAudioInputLevel = 0.0;
    double m_txAudioOutputLevel = 0.0;
    double m_txAudioGainReduction = 0.0;
    double m_rxAudioInputLevel = 0.0;
    double m_rxAudioOutputLevel = 0.0;
    quint64 m_txAudioBlocks = 0;
    quint64 m_rxAudioBlocks = 0;
    bool m_txSpectrumEnabled = false;
    bool m_rxSpectrumEnabled = false;
    bool m_audioProcessorSignalsConnected = false;
    int m_txPower = 0;
    int m_monitorGain = 0;
    bool m_monitorEnabled = false;
    int m_micGain = 0;
    QString m_modGainLabel = "Mic";
    int m_modGainMin = 0;
    int m_modGainMax = 255;
    int m_modSourceRevision = 0;
    rigInput m_currentModSrc[4];
    int m_ritFrequency = 0;
    bool m_transmitting = false;
    bool m_tunerEnabled = false;
    bool m_ritEnabled = false;
    bool m_dualScope = false;
    bool m_dualWatch = false;
    bool m_splitEnabled = false;
    bool m_compressorEnabled = false;
    bool m_voxEnabled = false;
    double m_optionalMeter2Level = 0.0;
    double m_optionalMeter3Level = 0.0;
    QTimer radioClockSyncTimer;
    timekind radioClockTime;
    datekind radioClockDate;
    timekind radioClockUtcOffset;
    bool waitingToSetRadioClock = false;
    uchar currentReceiver = 0;
    bool freqLock = false;

#if defined(USB_CONTROLLER)
    usbController *usbControllerDev = nullptr;
    QThread *usbControllerThread = nullptr;
#endif

    connectionStatus_t connStatus = connDisconnected;

    bool shuttingDown = false;

    int cmdInterval_ms = 100;
    int cmdStartupInterval_ms = 250;
    int wfShareCmdInterval_ms = 25;

    std::unique_ptr<SettingsController> m_settings;
    std::unique_ptr<SelectRadioController> m_selRad;
    std::unique_ptr<CWSenderController> m_cwSender;

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
    QPalette palette;
    QPalette systemPalette;
};

#endif // MAINCONTROLLER_H
