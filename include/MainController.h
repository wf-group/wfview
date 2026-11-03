#ifndef MAINCONTROLLER_H
#define MAINCONTROLLER_H

#include <QDateTime>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <QVersionNumber>
#include <functional>

#include "ReceiverController.h"
#include "RigCreatorController.h"
#include "SelectRadioController.h"
#include "CWSenderController.h"
#include "RttyController.h"

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
#include "audiorouting.h"

class MainController : public QObject {
    Q_OBJECT

    Q_PROPERTY(MemoriesModel* memoriesModel READ memoriesModel NOTIFY memoriesModelChanged)
    Q_PROPERTY(int runningMacroId READ runningMacroId NOTIFY runningMacroChanged)
    // Non-empty while on-disconnect commands/macro run before the connection closes.
    Q_PROPERTY(QString closingActionsText READ closingActionsText NOTIFY closingActionsTextChanged)
    Q_PROPERTY(bool slowLoad READ slowLoad WRITE setSlowLoad NOTIFY slowLoadChanged)

    Q_PROPERTY(CWSenderController* cwSender READ cwSender CONSTANT)
    Q_PROPERTY(RttyController* rtty READ rtty CONSTANT)
    Q_PROPERTY(SelectRadioController* selectRadio READ selectRadio CONSTANT)
    // Qt 6.9+ Controls give TextField/TextArea their own right-click edit menu.
    Q_PROPERTY(bool nativeTextContextMenu READ nativeTextContextMenu CONSTANT)


public:

    enum connectionStatus_t { connDisconnected, connConnecting, connConnected };
    Q_ENUM (connectionStatus_t)

    enum underlay_t { underlayNone, underlayPeak, underlayAverage };
    Q_ENUM (underlay_t)

    enum connectionType_t { connectionUSB, connectionLAN, connectionWiFi, connectionWAN };
    Q_ENUM (connectionType_t)

    // System rig folders in search order; the first one that exists is used.
    static QStringList systemRigLocationCandidates();
    static QString systemRigLocation();

    Q_PROPERTY(QString windowTitle READ getWindowTitle WRITE setWindowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(QString footerMessageText READ footerMessageText NOTIFY footerMessageTextChanged)
    Q_PROPERTY(QString radioStatusText READ radioStatusText NOTIFY radioStatusTextChanged)
    Q_PROPERTY(QString rigModelName READ rigModelName NOTIFY rigModelNameChanged)
    Q_PROPERTY(QString clusterOutputText READ clusterOutputText NOTIFY clusterOutputTextChanged)
    Q_PROPERTY(int clusterStatus READ clusterStatus NOTIFY clusterStatusChanged)
    Q_PROPERTY(QString clusterStatusText READ clusterStatusText NOTIFY clusterStatusChanged)
    Q_PROPERTY(int receiverCount READ receiverCount NOTIFY receiverCountChanged)
    Q_PROPERTY(int currentReceiver READ getCurrentReceiver NOTIFY currentReceiverChanged)
    Q_PROPERTY(SettingsController* settings READ getSettings CONSTANT)
    Q_PROPERTY(connectionStatus_t connStatus READ getConnStatus WRITE setConnStatus NOTIFY connStatusChanged)
    Q_PROPERTY(ThemeBridge* theme READ theme CONSTANT)

    Q_PROPERTY(quint64 stepSize READ getStepSize WRITE setStepSize NOTIFY stepSizeChanged)

    Q_PROPERTY(QVariantMap uiSpecs READ getUiSpecs NOTIFY uiSpecsChanged)    
    Q_PROPERTY(int txPower READ txPower WRITE setTxPower NOTIFY txPowerChanged)
    Q_PROPERTY(int localAfGain READ localAfGain WRITE setLocalAfGain NOTIFY localAfGainChanged)
    Q_PROPERTY(bool localAudioAvailable READ localAudioAvailable NOTIFY localAudioAvailableChanged)
    Q_PROPERTY(int rxAudioLevel READ rxAudioLevel NOTIFY audioLevelsChanged)
    Q_PROPERTY(int txAudioLevel READ txAudioLevel NOTIFY audioLevelsChanged)
    Q_PROPERTY(int monitorGain READ monitorGain WRITE setMonitorGain NOTIFY monitorGainChanged)
    Q_PROPERTY(bool monitorEnabled READ monitorEnabled WRITE setMonitorEnabled NOTIFY monitorEnabledChanged)
    Q_PROPERTY(int micGain READ micGain WRITE setMicGain NOTIFY micGainChanged)
    Q_PROPERTY(QString modGainLabel READ modGainLabel NOTIFY modSourcesChanged)
    Q_PROPERTY(int modGainMin READ modGainMin NOTIFY modSourcesChanged)
    Q_PROPERTY(int modGainMax READ modGainMax NOTIFY modSourcesChanged)
    Q_PROPERTY(int modSourceRevision READ modSourceRevision NOTIFY modSourcesChanged)
    Q_PROPERTY(int ritFrequency READ ritFrequency WRITE setRitFrequency NOTIFY ritFrequencyChanged)
    Q_PROPERTY(bool transmitting READ transmitting WRITE setTransmit NOTIFY transmittingChanged)
    Q_PROPERTY(bool rxMuted READ rxMuted WRITE setRxMuted NOTIFY rxMutedChanged)
    Q_PROPERTY(bool anrProfiling READ anrProfiling NOTIFY anrProfileStateChanged)
    Q_PROPERTY(bool anrHasProfile READ anrHasProfile NOTIFY anrProfileStateChanged)
    Q_PROPERTY(int anrProfileSecondsLeft READ anrProfileSecondsLeft NOTIFY anrProfileStateChanged)
    // ANR noise profile as dB per linear FFT bin; bin i is at i * sampleRate / windowSize Hz.
    Q_PROPERTY(QVariantList anrNoiseProfileBins READ anrNoiseProfileBins NOTIFY anrNoiseProfileChanged)
    Q_PROPERTY(double anrNoiseProfileSampleRate READ anrNoiseProfileSampleRate NOTIFY anrNoiseProfileChanged)
    Q_PROPERTY(int anrNoiseProfileWindowSize READ anrNoiseProfileWindowSize NOTIFY anrNoiseProfileChanged)
    Q_PROPERTY(bool tunerEnabled READ tunerEnabled WRITE setTunerEnabled NOTIFY tunerEnabledChanged)
    Q_PROPERTY(bool ritEnabled READ ritEnabled WRITE setRitEnabled NOTIFY ritEnabledChanged)
    Q_PROPERTY(bool dualScope READ dualScope WRITE setDualScope NOTIFY dualScopeChanged)
    Q_PROPERTY(bool dualWatch READ dualWatch WRITE setDualWatch NOTIFY dualWatchChanged)
    Q_PROPERTY(bool splitEnabled READ splitEnabled WRITE setSplitEnabled NOTIFY splitEnabledChanged)
    Q_PROPERTY(bool compressorEnabled READ compressorEnabled WRITE setCompressorEnabled NOTIFY compressorEnabledChanged)
    Q_PROPERTY(bool voxEnabled READ voxEnabled WRITE setVoxEnabled NOTIFY voxEnabledChanged)
    Q_PROPERTY(double optionalMeter2Level READ optionalMeter2Level NOTIFY optionalMetersChanged)
    Q_PROPERTY(double optionalMeter3Level READ optionalMeter3Level NOTIFY optionalMetersChanged)
    Q_PROPERTY(int meterLevelsRevision READ meterLevelsRevision NOTIFY meterLevelsChanged)
    Q_PROPERTY(QVariantMap repeaterState READ repeaterState NOTIFY repeaterStateChanged)
    Q_PROPERTY(QVariantList repeaterToneModel READ repeaterToneOptions NOTIFY repeaterModelsChanged)
    Q_PROPERTY(QVariantList repeaterDtcsModel READ repeaterDtcsOptions NOTIFY repeaterModelsChanged)
    // Non-empty when no rig definition files were found; lists the folders searched.
    Q_PROPERTY(QString rigFilesProblem READ rigFilesProblem NOTIFY rigFilesProblemChanged)
    Q_PROPERTY(bool serverRunning READ serverRunning NOTIFY serverStatusChanged)
    Q_PROPERTY(QString serverStatusText READ serverStatusText NOTIFY serverStatusChanged)
    Q_PROPERTY(int serverUsersLoggedIn READ serverUsersLoggedIn NOTIFY serverStatusChanged)
    Q_PROPERTY(int serverTotalLogins READ serverTotalLogins NOTIFY serverStatusChanged)

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
    QString rigFilesProblem() const { return m_rigFilesProblem; }
    bool serverRunning() const { return server != nullptr && m_serverListening; }
    QString serverStatusText() const;
    int serverUsersLoggedIn() const;
    int serverTotalLogins() const { return m_serverTotalLogins; }
    QString rigModelName() const { return m_rigModelName; }
    QString clusterOutputText() const { return m_clusterOutputText; }
    int clusterStatus() const { return m_clusterStatus; }
    QString clusterStatusText() const;

    int receiverCount() const { return receivers.size(); }
    int getCurrentReceiver() const { return currentReceiver; }

    Q_INVOKABLE void updateApplicationPalette();
    // Called by MainWindow.qml once it can show dialogs. Problem alerts
    // raised before then (the startup connect runs before QML loads) are
    // held and emitted here.
    Q_INVOKABLE void windowReady();

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
    RttyController* rtty() const { return m_rtty.get(); }

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
        if (!rigCaps || !rigCaps->hasTransmit || !rigCaps->commands.contains(funcSendCW))
            return;
        m_cwSender->setVisible(true);
    }

    // RTTY decode works with any radio; transmit needs PTT and TX audio.
    Q_INVOKABLE void showRtty()
    {
        m_rtty->setVisible(true);
    }


    connectionStatus_t getConnStatus() { return connStatus;}

    void setConnStatus(connectionStatus_t c) {
        if (c != connStatus) {
            if (c == connDisconnected)
                resetTxAudioOwnership();
            connStatus =c;
            emit connStatusChanged();
            updateAudioRouteState();
        }
    }

    quint64 getStepSize() { return stepSize;}
    void setStepSize(quint64 s);
    QVariantMap getUiSpecs() const { return uiSpecs; }
    int txPower() const { return m_txPower; }
    int localAfGain() const { return prefs ? int(prefs->localAFgain) : 0; }
    bool localAudioAvailable() const;
    int rxAudioLevel() const { return m_rxAudioLevel; }
    int txAudioLevel() const { return m_txAudioLevel; }
    int monitorGain() const { return m_monitorGain; }
    bool monitorEnabled() const { return m_monitorEnabled; }
    int micGain() const { return m_micGain; }
    QString modGainLabel() const { return m_modGainLabel; }
    int modGainMin() const { return m_modGainMin; }
    int modGainMax() const { return m_modGainMax; }
    int modSourceRevision() const { return m_modSourceRevision; }
    int ritFrequency() const { return m_ritFrequency; }
    bool transmitting() const { return m_transmitting; }
    bool rxMuted() const { return m_rxMuted; }
    bool anrProfiling() const { return m_anrProfiling; }
    bool anrHasProfile() const { return m_anrHasProfile; }
    int anrProfileSecondsLeft() const { return m_anrProfileSecondsLeft; }
    QVariantList anrNoiseProfileBins() const { return m_anrNoiseProfileBins; }
    double anrNoiseProfileSampleRate() const { return m_anrNoiseProfileSampleRate; }
    int anrNoiseProfileWindowSize() const { return m_anrNoiseProfileWindowSize; }
    bool tunerEnabled() const { return m_tunerEnabled; }
    bool ritEnabled() const { return m_ritEnabled; }
    bool dualScope() const { return m_dualScope; }
    bool dualWatch() const { return m_dualWatch; }
    bool splitEnabled() const { return m_splitEnabled; }
    bool compressorEnabled() const { return m_compressorEnabled; }
    bool voxEnabled() const { return m_voxEnabled; }
    double optionalMeter2Level() const { return m_optionalMeter2Level; }
    double optionalMeter3Level() const { return m_optionalMeter3Level; }
    int meterLevelsRevision() const { return m_meterLevelsRevision; }

    MemoriesModel* memoriesModel() const { return m_memoriesModel; }
    bool slowLoad() const { return m_slowLoad; }
    bool nativeTextContextMenu() const { return QVersionNumber::fromString(QString::fromLatin1(qVersion())) >= QVersionNumber(6, 9); }

    void setSlowLoad(bool value) {
        if (m_slowLoad != value) {
            m_slowLoad = value;
            emit slowLoadChanged();

            if (m_slowLoad && !m_memoriesModel) {
                createMemoriesModel();
            }
        }
    }

    Q_INVOKABLE bool canOpenMemories() const {
        return rigCaps && rigCaps->commands.contains(funcMemoryContents);
    }

    Q_INVOKABLE void openMemories() {
        if (!canOpenMemories())
            return;
        if (!m_memoriesModel) {
            createMemoriesModel();
        }
    }

    Q_INVOKABLE void applyAudioProcessingSettings();
    Q_INVOKABLE void ensureAudioProcessors();
    Q_INVOKABLE void startAnrNoiseProfile();
    Q_INVOKABLE void stopAnrNoiseProfile();
    Q_INVOKABLE void setRxMuted(bool muted);   // runtime-only; not persisted
    Q_INVOKABLE void powerOn();
    Q_INVOKABLE void powerOff();
    Q_INVOKABLE void toggleTransmit();
    Q_INVOKABLE void tuneNow();
    Q_INVOKABLE void setFrequencyLock(bool locked);
    Q_INVOKABLE void selectMainSub();
    Q_INVOKABLE void swapMainSub();
    Q_INVOKABLE void equalizeMainSub();
    Q_INVOKABLE QVariantList optionalMeterOptions() const;
    Q_INVOKABLE QVariantList shortcutCommandOptions() const;
    Q_INVOKABLE QVariantList shortcutAppCommandOptions() const;
    Q_INVOKABLE QVariantList shortcutRadioCommandOptions() const;
    Q_INVOKABLE void runShortcutCommand(const QString& commandName, int action, int value, int receiver);
    Q_INVOKABLE void runShortcutAppAction(const QString& commandName);
    Q_INVOKABLE QVariantList macroCommandOptions() const;
    Q_INVOKABLE QString macroRawCommandHint() const;
    Q_INVOKABLE void runMacro(int macroId);
    Q_INVOKABLE void stopMacro();
    int runningMacroId() const { return m_runningMacroId; }
    QString closingActionsText() const { return m_closingActionsText; }
    Q_INVOKABLE QVariantMap optionalMeterExtremities(int meterType) const;
    Q_INVOKABLE bool isOptionalMeterAvailable(int meterType) const;
    Q_INVOKABLE void setOptionalMeterType(int slot, int meterType);
    // Shared meter polling: each consumer (meter slot, vintage meter, pop-out)
    // names the meter it shows; a meter is polled while anyone wants it.
    // meterNone releases. meterS (S+Power) follows the primary meter, so it
    // needs no extra polling.
    Q_INVOKABLE void requestMeter(const QString& consumer, int meterType);
    // Last level received for a meter type, shared by every meter showing it.
    Q_INVOKABLE double meterLevel(int meterType) const;
    Q_INVOKABLE QVariantList modSourceOptions(int dataMode) const;
    Q_INVOKABLE int modSourceReg(int dataMode) const;
    Q_INVOKABLE bool modSourceSupported(int dataMode) const;
    Q_INVOKABLE void setModSource(int dataMode, int reg);
    Q_INVOKABLE QVariantList repeaterToneOptions() const;
    Q_INVOKABLE QVariantList repeaterDtcsOptions() const;
    Q_INVOKABLE void refreshRepeaterState();
    Q_INVOKABLE void setRepeaterDuplex(int mode);
    Q_INVOKABLE void setRepeaterToneMode(int mode, bool secondaryVfo = false);
    Q_INVOKABLE void setRepeaterToneFrequency(int tone, bool secondaryVfo = false);
    Q_INVOKABLE void setRepeaterTsqlFrequency(int tone, bool secondaryVfo = false);
    Q_INVOKABLE void setRepeaterDtcsCode(int tone, bool txInvert, bool rxInvert);
    Q_INVOKABLE void setRepeaterOffsetMHz(const QString& mhz);
    Q_INVOKABLE void setRepeaterSplitFromOffsetKHz(const QString& khz, bool plus, bool autoEnableSplit);
    Q_INVOKABLE void setRepeaterSplitTxFrequencyMHz(const QString& mhz, bool autoEnableSplit);
    Q_INVOKABLE void setRepeaterQuickSplit(bool enabled);
    Q_INVOKABLE QString platformName() const;
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
    void appShortcutActivated(QString commandName, int receiver);
    void pttTimeoutExpired(int seconds);
    // A configured audio device is blank, missing, or could not be opened.
    void audioDeviceProblem(QString headline, QString detail);
    // The radio serial port could not be used when connecting.
    void serialPortProblem(QString headline, QString detail);
    void rigFilesProblemChanged();
    void serverStatusChanged();
    void runningMacroChanged();
    void closingActionsTextChanged();
    void receiverCountChanged();
    void currentReceiverChanged();
    void receiverDetachedChanged(int index, bool detached);
    void detachedChanged();

    void sendSerialCommSetup(rigTypedef rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate,QString vsp, bool vspUseQueue, quint16 tcp, quint8 wf);
    void sendNetworkCommSetup(rigTypedef rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, bool vspUseQueue, quint16 tcp);
    void sendWfShareCommSetup(rigTypedef rigList, quint16 rigCivAddr, QString host, quint16 port,
                              QString username, QString password, QString calledNumber,
                              audioSetup rxSetup, audioSetup txSetup);
    void sendCloseComm();
    void sendLocalAudioVolume(quint8 level);
    void sendTxAudioToRig(const audioPacket &packet);

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
    void audioLevelsChanged();
    void monitorGainChanged();
    void monitorEnabledChanged();
    void micGainChanged();
    void modSourcesChanged();
    void ritFrequencyChanged();
    void transmittingChanged();
    void rxMutedChanged();
    void anrProfileStateChanged();
    void anrNoiseProfileChanged();
    void tunerEnabledChanged();
    void ritEnabledChanged();
    void dualScopeChanged();
    void dualWatchChanged();
    void splitEnabledChanged();
    void compressorEnabledChanged();
    void voxEnabledChanged();
    void optionalMetersChanged();
    void meterLevelsChanged();
    void clusterOutputTextChanged();
    void clusterStatusChanged();
    void repeaterStateChanged();
    void repeaterModelsChanged();

public slots:
    void onRadioPacket(const QByteArray &packet);
    void setWindowTitle(const QString &t);
    void connectionHandler();
    void shutdown();
    Q_INVOKABLE void quitApplication();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void receiveValueFromQueue(cacheItem c);
    void receiveRigCaps(rigCapabilities* caps);
    void receiveStatusUpdate(networkStatus status);
    void receiveNetworkAudioLevels(networkAudioLevels levels);
    void routeRxAudioToSinks(const audioPacket &packet);
    void routeLocalTxAudioToRadio(const audioPacket &packet);
    void routeTciTxAudioToRadio(const audioPacket &packet);
    void handleTciTransmitRequested(bool transmit);
    void handleRigCtlTransmitRequested(bool transmit);
    void handleExternalTransmitRequested(bool transmit);
    void handlePttTimeout();
    void receiveCommReady();
    void receivePortError(errorType err);
    void handleRigConnectionLost();
    void handleAudioDeviceProblem(int role, bool isInput, int problem, QString deviceName, QString detail);
    void ctChanged(SettingsController::prefCtItems items);
    void clusterChanged(SettingsController::prefClusterItems items);
#if defined(USB_CONTROLLER)
    void changeFrequency(int value);
    void doShuttle(bool up, int steps);
#endif
    void buttonControl(const COMMAND* cmd);

    void ifChanged(prefIfItems items);
    void colChanged(prefColItems items);
    void lanChanged(SettingsController::prefLanItems items);
    //void rsChanged(prefRsItems items);
    void raChanged(prefRaItems items);
    //void ctChanged(prefCtItems items);
    //void clusterChanged(prefClusterItems items);
    //void udpChanged(prefUDPItems items);
    void serverChanged(SettingsController::prefServerItems items);

private:
#ifdef WFVIEW_MCP
    friend class McpHostWfview;
#endif
    void buildUiSpecs();
    void setRadioStatusText(const QString& text);
    void setFooterMessageText(const QString& text);
    void setRigModelName(const QString& modelName);
    funcs modSourceCommand(int dataMode) const;
    const rigInput* findModSourceInput(int dataMode, int reg) const;
    void applyConnectModSources();
    bool applyDisconnectModSources();
    int macroDurationMs(int macroId) const;
    void scheduleConnectMacro();
    int startDisconnectActions();
    void setClosingActionsText(const QString& text);
    void disconnectRig();
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
    void updatePrimaryMeterForTransmit(bool transmitting);
    void configureOptionalMeter(int slot, meter_t meterType);
    void receiveOptionalMeter(meter_t meterType, double level);
    funcs meterPollCommand(meter_t meterType) const;
    static uchar meterPollReceiver(meter_t meterType);
    bool meterRequested(funcs command, uchar receiver) const;
    bool meterTypeRequested(meter_t meterType) const;
    bool primaryMeterUses(funcs command, uchar receiver) const;
    void applyMeterRequest(const QString& consumer, meter_t meterType);
    void refreshMeterRequests();
    QVariantMap uiSpecs;
    QVariantMap repeaterState() const;

    //void setDefPrefs();
    //void loadSettings(QString file); // Look for saved preferences
    void setManufacturer(manufacturersType_t man);
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
    void receiveClusterStatus(int status);
    void setupRigCtlServer();
    void stopRigCtlServer();
    void setupTciServer();
    void stopTciServer();
    // The built-in radio server, for a radio on a serial port.
    void setupRadioServer();
    void stopRadioServer();
    void updateServerRadio();
    void sendUsersToServer();
    void handleServerLogin(const QString& username, const QString& address);
    void handleServerLogout(const QString& username, const QString& address);
    void publishServerStatus();
    // Yaesu servers leave opening the radio's audio to the application.
    void ensureServerRxAudio(quint8 codec, quint32 sampleRate);
    void ensureServerTxAudio(quint8 codec, quint32 sampleRate);
    void stopServerHostAudio();
    void setupTciRxBridge();
    void stopTciRxBridge();
    void setupUsbAudioBridge();
    void stopUsbAudioBridge();
    void setupLocalTxBridge();
    void stopLocalTxBridge();
    void setupTciUsbTxBridge();
    void setTxAudioSource(TxAudioInput source);
    bool requestTxAudioOwnership(PttOrigin origin, TxAudioInput source);
    bool releaseTxAudioOwnership(PttOrigin origin);
    void resetTxAudioOwnership();
    void setTxAudioInterlockError(PttOrigin requestedOrigin, TxAudioInput requestedSource);
    void clearTxAudioInterlockError();
    void setRxAudioLevel(int level);
    void setTxAudioLevel(int level);
    void publishTxAudioDiagnosticLevel(const audioPacket& packet, TxAudioInput source);
    audioHandlerBase* createAudioHandler(const audioSetup& setup);
    void disposeAudioHandler(audioHandlerBase*& handler, QThread*& thread);
    AudioRouteState currentAudioRoute() const;
    void updateAudioRouteState();
    void checkAudioDeviceConfiguration();
    bool usingSerialConnection() const;
    void reportSerialPortProblem(const QString& port, const QString& reason);
    void tuneFrequencyHz(qint64 hzDelta, int receiver = -1);
    quint64 parseRepeaterFrequencyHz(const QString& text, double multiplier, const QString& fieldName) const;
    toneInfo toneFromList(const std::vector<toneInfo>& tones, int tone, bool secondaryVfo = false) const;
    void setRepeaterAccessMode(rptAccessTxRx_t mode, bool secondaryVfo = false);
    void updateRepeaterToneMode(rptAccessTxRx_t mode);
    int combinedToneModeMax() const;
    void updateRepeaterOffset(freqt offset);


    QString windowTitle = "wfview";
    QString m_footerMessageText;
    // Radio/port errors shown in the footer expire after this timer fires.
    QTimer m_footerErrorTimer;
    QString m_footerErrorText;
    // Audio device warnings already shown for this connection (role:problem).
    QSet<QString> m_reportedAudioProblems;
    // A serial port warning was already given (or deliberately skipped) for
    // this connection; reconnect attempts repeat the same error.
    bool m_serialPortProblemReported = false;
    bool m_windowReady = false;
    QList<std::function<void()>> m_pendingWindowAlerts;
    void emitWhenWindowReady(std::function<void()> alert);
    QString m_rigFilesProblem;
    QString m_radioStatusText;
    QString m_rigModelName;
    QString m_clusterOutputText;
    qsizetype m_clusterOutputLines = 0;
    int m_clusterStatus = dxClusterClient::tcpNotConnected;
    QVector<ReceiverController*> receivers;
    QVector<bool> detached;


    QSettings* settings = nullptr;

    preferences* prefs;
    udpPreferences* udpPrefs;

    QHash<quint16,rigInfo> rigList;
    rigCapabilities* rigCaps = nullptr;

    cachingQueue* queue;
    rigServer* server = nullptr;
    QThread* serverThread = nullptr;
    struct serverSession {
        QString username;
        QString address;
        QDateTime since;
    };
    // Signals from a stopped server are ignored by comparing generations.
    int m_serverGeneration = 0;
    bool m_serverListening = false;
    QString m_serverError;
    QDateTime m_serverStartedAt;
    QList<serverSession> m_serverSessions;
    QSet<QString> m_serverSeenUsers;
    int m_serverTotalLogins = 0;
    rigCtlD* rigCtl = nullptr;
    tciServer* tci = nullptr;
    QThread* tciThread = nullptr;
    dxClusterClient* cluster = nullptr;
    QThread* clusterThread = nullptr;
    // Server fields are edited a keystroke at a time; wait for typing to settle before reconnecting.
    QTimer* clusterReconnectTimer = nullptr;

    rigCommander * rig = nullptr;
    QThread* rigThread = nullptr;
    TxAudioProcessor* txProc = nullptr;
    RxAudioProcessor* rxProc = nullptr;
    audioHandlerBase* usbRadioRxAudio = nullptr;
    QThread* usbRadioRxThread = nullptr;
    audioHandlerBase* usbRxOutputAudio = nullptr;
    QThread* usbRxOutputThread = nullptr;
    audioHandlerBase* tciRxOutputAudio = nullptr;
    QThread* tciRxOutputThread = nullptr;
    audioHandlerBase* usbTxInputAudio = nullptr;
    QThread* usbTxInputThread = nullptr;
    audioHandlerBase* usbRadioTxAudio = nullptr;
    QThread* usbRadioTxThread = nullptr;
    audioHandlerBase* tciTxInputAudio = nullptr;
    QThread* tciTxInputThread = nullptr;
    AudioRouteState m_audioRoute;
    TxAudioSourceArbiter m_txAudioArbiter;
    TxAudioInput m_activeTxAudioSource = TxAudioInput::None;
    int m_rxAudioLevel = 0;
    int m_txAudioLevel = 0;
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
    // Restarted on every user RIT change; radio reports arriving within
    // ritHoldoffMs of it are stale and would make the dial jump back.
    QElapsedTimer m_ritUserChange;
    static constexpr int ritHoldoffMs = 100;
    bool m_transmitting = false;
    bool m_rxMuted = false;   // runtime-only RX mute used with TX self-monitor
    // ANR noise-sample collection: runs for anrProfileSeconds unless stopped early.
    static constexpr int anrProfileSeconds = 5;
    QTimer m_anrProfileTimer;
    bool m_anrProfiling = false;
    bool m_anrHasProfile = false;
    int m_anrProfileSecondsLeft = 0;
    QVariantList m_anrNoiseProfileBins;
    double m_anrNoiseProfileSampleRate = 0.0;
    int m_anrNoiseProfileWindowSize = 0;
    bool m_tunerEnabled = false;
    bool m_ritEnabled = false;
    bool m_dualScope = false;
    bool m_dualWatch = false;
    bool m_splitEnabled = false;
    bool m_compressorEnabled = false;
    bool m_voxEnabled = false;
    duplexMode_t m_repeaterDuplexMode = dmSplitOff;
    rptAccessTxRx_t m_repeaterToneMode = ratrNN;
    int m_repeaterTone = 670;
    int m_repeaterTsql = 670;
    int m_repeaterDtcs = 23;
    bool m_repeaterDtcsTxInvert = false;
    bool m_repeaterDtcsRxInvert = false;
    quint64 m_repeaterOffsetHz = 0;
    bool m_repeaterQuickSplit = false;
    bool m_repeaterWarnNotFm = false;
    freqt m_repeaterMainFrequency;
    bool m_repeaterHaveMainFrequency = false;
    bool m_repeaterLastSplitPlus = false;
    double m_optionalMeter2Level = 0.0;
    double m_optionalMeter3Level = 0.0;
    QHash<QString, meter_t> m_meterRequests;
    QHash<int, double> m_meterLevels;
    int m_meterLevelsRevision = 0;
    QTimer radioClockSyncTimer;
    timekind radioClockTime;
    datekind radioClockDate;
    timekind radioClockUtcOffset;
    bool waitingToSetRadioClock = false;
    uchar currentReceiver = 0;
    bool freqLock = false;

    void runNextMacroStep();
    void executeMacroStep(const macroStep& step);
    bool executeMacroRadioStep(funcs func, const QString& parameter, QString* error);
    bool sendMacroRawCommand(const QString& text, QString* error);
    QTimer m_macroTimer;

    // Software PTT timeout for transmissions started from wfview or rigctld.
    void armPttTimeout();
    QTimer m_pttTimeoutTimer;
    QVector<macroStep> m_macroSteps;
    int m_macroStepIndex = 0;
    int m_runningMacroId = -1;
    // State captured when a macro starts, for the restore commands.
    uchar m_macroStartReceiver = 0;
    modeInfo m_macroStartMode;
    int m_macroStartTxPower = -1;

#if defined(USB_CONTROLLER)
    usbController *usbControllerDev = nullptr;
    QThread *usbControllerThread = nullptr;
#endif

    connectionStatus_t connStatus = connDisconnected;

    bool shuttingDown = false;
    bool m_disconnectPending = false;
    bool m_quitPending = false;
    QTimer m_connectMacroTimer;
    QDeadlineTimer m_disconnectDeadline;
    QString m_closingActionsText;

    int cmdInterval_ms = 100;
    int cmdStartupInterval_ms = 250;
    int wfShareCmdInterval_ms = 25;

    std::unique_ptr<SettingsController> m_settings;
    std::unique_ptr<SelectRadioController> m_selRad;
    std::unique_ptr<CWSenderController> m_cwSender;
    std::unique_ptr<RttyController> m_rtty;

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
