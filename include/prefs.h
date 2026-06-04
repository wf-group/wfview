#ifndef PREFS_H
#define PREFS_H

#include <QString>
#include <QColor>
#include <QMap>
#include <QVector>
#include "audioconverter.h"
#include "cluster.h"
#include "rigidentities.h"
#include "wfviewtypes.h"

struct shortcutPreference {
    bool enabled = true;
    QString sequence;
    QString command;
    int action = 0;
    int value = 0;
    int receiver = -1;
};

enum prefIfItem {
    if_useFullScreen = 1 << 0,
    if_useSystemTheme = 1 << 1,
    if_drawPeaks = 1 << 2,
    if_underlayMode = 1 << 3,
    if_underlayBufferSize = 1 << 4,
    if_wfAntiAlias = 1 << 5,
    if_wfInterpolate = 1 << 6,
    if_wftheme = 1 << 7,
    if_plotFloor = 1 << 8,
    if_plotCeiling = 1 << 9,
    if_stylesheetPath = 1 << 10,
    if_wflength = 1 << 11,
    if_confirmExit = 1 << 12,
    if_confirmPowerOff = 1 << 13,
    if_meter1Type = 1 << 14,
    if_meter2Type = 1 << 15,
    if_meter3Type = 1 << 16,
    if_compMeterReverse = 1 << 17,
    if_clickDragTuningEnable = 1 << 18,
    if_currentColorPresetNumber = 1 << 19,
    if_rigCreatorEnable = 1 << 20,
    if_frequencyUnits = 1 << 21,
    if_region = 1 << 22,
    if_showBands = 1 << 23,
    if_separators = 1 << 24,
    if_forceVfoMode = 1 << 25,
    if_autoPowerOn = 1 << 26,
    if_peakDecay = 1 << 27,
    if_all = ~0
};

// Allow 63 different colors
enum prefColItem : qint64 {
    col_grid = 1LL << 0,
    col_axis = 1LL << 1,
    col_text = 1LL << 2,
    col_plotBackground = 1LL << 3,
    col_spectrumLine = 1LL << 4,
    col_spectrumFill = 1LL << 5,
    col_useSpectrumFillGradient = 1LL << 6,
    col_spectrumFillTop = 1LL << 7,
    col_spectrumFillBot = 1LL << 8,
    col_underlayLine = 1LL << 9,
    col_underlayFill = 1LL << 10,
    col_useUnderlayFillGradient = 1LL << 11,
    col_underlayFillTop = 1LL << 12,
    col_underlayFillBot = 1LL << 13,
    col_tuningLine = 1LL << 14,
    col_passband = 1LL << 15,
    col_pbtIndicator = 1LL << 16,
    col_meterLevel = 1LL << 17,
    col_meterAverage = 1LL << 18,
    col_meterPeakLevel = 1LL << 19,
    col_meterHighScale = 1LL << 20,
    col_meterScale = 1LL << 21,
    col_meterText = 1LL << 22,
    col_waterfallBack = 1LL << 23,
    col_waterfallGrid = 1LL << 24,
    col_waterfallAxis = 1LL << 25,
    col_waterfallText = 1LL << 26,
    col_clusterSpots = 1LL << 27,
    col_buttonOff = 1LL << 28,
    col_buttonOn = 1LL << 29,
    col_window = 1LL << 30,
    col_windowText = 1LL << 31,
    col_base = 1LL << 30,
    col_alternateBase = 1LL << 32,
    col_button = 1LL << 33,
    col_buttonText = 1LL << 34,
    col_brightText = 1LL << 35,
    col_mainText = 1LL << 36,
    col_light = 1LL << 37,
    col_midLight = 1LL << 38,
    col_dark = 1LL << 39,
    col_mid = 1LL << 40,
    col_shadow = 1LL << 41,
    col_highlight = 1LL << 42,
    col_highlightedText = 1LL << 43,
    col_link = 1LL << 44,
    col_linkVisited = 1LL << 45,
    col_toolTipBase = 1LL << 46,
    col_toolTipText = 1LL << 47,
    col_placeholder = 1LL << 48,
    col_all = ~0LL
};
using prefColItems = qint64;

enum prefRsItem {
    rs_dataOffMod = 1 << 0,
    rs_data1Mod = 1 << 1,
    rs_data2Mod = 1 << 2,
    rs_data3Mod = 1 << 3,
    rs_setClock = 1 << 4,
    rs_clockUseUtc = 1 << 5,
    rs_setRadioTime = 1 << 6,
    rs_pttOn = 1 << 7,
    rs_pttOff = 1 << 8,
    rs_satOps = 1 << 9,
    rs_adjRef = 1 << 10,
    rs_debug = 1 << 11,
    rs_all = ~0
};

enum prefRaItem {
    ra_radioCIVAddr = 1 << 0,
    ra_CIVisRadioModel = 1 << 1,
    ra_pttType = 1 << 2,
    ra_polling_ms = 1 << 3,
    ra_serialEnabled = 1 << 4,
    ra_serialPortRadio = 1 << 5,
    ra_serialPortBaud = 1 << 6,
    ra_virtualSerialPort = 1 << 7,
    ra_virtualSerialPortUseQueue = 1 << 8,
    ra_localAFgain = 1 << 9,
    ra_audioSystem = 1 << 10,
    ra_manufacturer = 1 << 11,
    ra_usbAudio = 1 << 12,
    ra_usbAudioRx = 1 << 13,
    ra_usbAudioTx = 1 << 14,
    ra_all = ~0
};

enum prefCtItem {
    ct_enablePTT = 1 << 0,
    ct_niceTS = 1 << 1,
    ct_automaticSidebandSwitching = 1 << 2,
    ct_enableUSBControllers = 1 << 3,
    ct_USBControllersSetup = 1 << 4,
    ct_USBControllersReset = 1 << 5,
    ct_all = ~0
};

enum prefLanItem {
    l_enableLAN = 1 << 0,
    l_enableRigCtlD = 1 << 1,
    l_rigCtlPort = 1 << 2,
    l_tcpPort = 1 << 3,
    l_tciPort = 1 << 4,
    l_waterfallFormat = 1 << 5,
    l_all = ~0
};

enum prefClusterItem {
    cl_clusterUdpEnable = 1 << 0,
    cl_clusterTcpEnable = 1 << 1,
    cl_clusterUdpPort = 1 << 2,
    cl_clusterTcpServerName = 1 << 3,
    cl_clusterTcpUserName = 1 << 4,
    cl_clusterTcpPassword = 1 << 5,
    cl_clusterTcpPort = 1 << 6,
    cl_clusterTimeout = 1 << 7,
    cl_clusterSkimmerSpotsEnable = 1 << 8,
    cl_clusterTcpConnect = 1 << 9,
    cl_clusterTcpDisconnect = 1 << 10,
    cl_all = ~0
};

enum prefUDPItem {
    u_enabled = 1 << 0,
    u_ipAddress = 1 << 1,
    u_controlLANPort = 1 << 2,
    u_serialLANPort = 1 << 3,
    u_audioLANPort = 1 << 4,
    u_scopeLANPort = 1 << 5,
    u_username = 1 << 6,
    u_password = 1 << 7,
    u_clientName = 1 << 8,
    u_halfDuplex = 1 << 9,
    u_sampleRate = 1 << 10,
    u_rxCodec = 1 << 11,
    u_txCodec = 1 << 12,
    u_rxLatency = 1 << 13,
    u_txLatency = 1 << 14,
    u_audioInput = 1 << 15,
    u_audioOutput = 1 << 16,
    u_connectionType = 1 << 17,
    u_adminLogin = 1 << 18,
    u_all = ~0
};

static constexpr int TX_AUDIO_EQ_BANDS = 15;

struct txAudioProcessingPrefs {
    bool  bypass        = true;
    bool  compEnabled   = false;
    bool  eqEnabled     = false;
    bool  eqFirst       = false;
    float inputGainDB   = 10.0f;
    float outputGainDB  = 0.0f;
    float eqBands[TX_AUDIO_EQ_BANDS] = {};
    float compPeakLimit = -10.0f;
    float compRelease   = 0.1f;
    float compFastRatio = 0.2f;
    float compSlowRatio = 0.2f;
    bool  sidetoneEnabled = false;
    float sidetoneLevel   = 0.5f;
    bool  spectrumEnabled       = false;
    int   spectrumFPS           = 10;
    bool  specInhibitDuringRx   = true;
    bool  gateEnabled   = false;
    float gateThreshold = -65.0f;
    float gateAttack    = 5.0f;
    float gateHold      = 40.0f;
    float gateDecay     = 20.0f;
    float gateRange     = -90.0f;
    float gateLfCutoff  = 380.0f;
    float gateHfCutoff  = 2700.0f;
};

enum class RxNrMode { None = 0, Speex = 1, Anr = 2 };

struct rxAudioProcessingPrefs {
    bool       bypass      = true;
    bool       nrEnabled   = false;
    RxNrMode   nrMode      = RxNrMode::None;
    int        channelSelect = 3;
    int        speexSuppression  = -30;
    int        speexBandsPreset  = 3;
    int        speexFrameMs      = 20;
    bool       speexAgc          = false;
    float      speexAgcLevel     = 8000.0f;
    int        speexAgcMaxGain   = 30;
    bool       speexVad          = false;
    int        speexVadProbStart = 85;
    int        speexVadProbCont  = 65;
    float      speexSnrDecay       = 0.7f;
    float      speexNoiseUpdateRate= 0.03f;
    float      speexPriorBase      = 0.1f;
    double     anrNoiseReductionDb = 20.0;
    double     anrSensitivity      = 1.1;
    int        anrFreqSmoothing    = 4;
    static constexpr int RX_EQ_BANDS = 4;
    bool       eqEnabled = false;
    float      eqGain[RX_EQ_BANDS] = {0.0f, 0.0f, 0.0f, 0.0f};
    float      eqFreq[RX_EQ_BANDS] = {100.0f, 800.0f, 2000.0f, 3500.0f};
    float      eqQ[RX_EQ_BANDS]    = {1.0f, 1.0f, 1.0f, 1.0f};
    float      outputGainDB = 0.0f;
    bool       spectrumEnabled     = false;
    int        spectrumFPS         = 10;
    bool       specInhibitDuringTx = true;
};


struct preferences {
    // Program:
    QString version;
    int majorVersion = 0;
    int minorVersion = 0;
    QString gitShort;
    bool hasRunSetup = false;
    bool settingsChanged = false;
    QString pastebinHost = QString("termbin.com");
    int pastebinPort = 9999;

    // Interface:
    bool useFullScreen;
    bool useSystemTheme;
    int wfEnable;
    bool drawPeaks;
    underlay_t underlayMode = underlayAverage;
    int underlayBufferSize = 80;
    int peakDecay = 50;
    bool wfAntiAlias;
    bool wfInterpolate;
    int mainWfTheme;
    int subWfTheme;
    int mainPlotFloor;
    int subPlotFloor;
    int mainPlotCeiling;
    int subPlotCeiling;
    unsigned int mainWflength;
    unsigned int subWflength;
    int scopeScrollX;
    int scopeScrollY;
    QString stylesheetPath;
    bool confirmExit;
    bool confirmPowerOff;
    bool confirmSettingsChanged;
    bool confirmMemories;
    meter_t meter1Type;
    meter_t meter2Type;
    meter_t meter3Type;
    bool compMeterReverse = false;
    bool clickDragTuningEnable;
    int currentColorPresetNumber = 0;
    bool rigCreatorEnable = false;
    int frequencyUnits = 3;
    QString region;
    bool showBands;

    // Radio:
    manufacturersType_t manufacturer;
    quint16 radioCIVAddr;
    bool CIVisRadioModel;
    pttType_t pttType;
    int polling_ms;
    QString serialPortRadio;
    quint32 serialPortBaud;
    QString virtualSerialPort;
    bool virtualSerialPortUseQueue = false;
    quint8 localAFgain;
    audioType audioSystem;
    bool enableUsbAudio = false;
    audioSetup usbRxSetup;
    audioSetup usbTxSetup;

    // Controls:
    bool enablePTT;
    bool niceTS;
    bool automaticSidebandSwitching = true;
    bool enableUSBControllers;
    QVector<shortcutPreference> shortcuts;

    // LAN:
    bool enableLAN;
    bool enableRigCtlD;
    quint16 rigCtlPort;
    quint16 tcpPort;
    quint16 tciPort=50001;
    quint8 waterfallFormat;

    // Experimental wfshare remote transport:
    bool wfShareEnabled = false;
    bool wfShareDirectMode = false;
    QString wfShareHost;
    quint16 wfSharePort = 4569;
    QString wfShareUsername;
    QString wfSharePassword;
    QString wfShareCalledNumber;

    // Cluster:
    QList<clusterSettings> clusters;
    bool clusterUdpEnable;
    bool clusterTcpEnable;
    int clusterUdpPort;
    int clusterTcpPort = 7300;
    int clusterTimeout; // used?
    bool clusterSkimmerSpotsEnable; // where is this used?
    QString clusterTcpServerName;
    QString clusterTcpUserName;
    QString clusterTcpPassword;

    // CW Sender settings
    bool cwCutNumbers=false;
    bool cwSendImmediate=false;
    bool cwSidetoneEnabled=true;
    int cwSidetoneLevel=50;
    QStringList cwMacroList;

    // Temporary settings
    rigInput inputSource[4];
    bool useUTC = false;

    bool setRadioTime = false;

    audioSetup rxSetup;
    audioSetup txSetup;
    txAudioProcessingPrefs txAudioProc;
    rxAudioProcessingPrefs rxAudioProc;

    QChar decimalSeparator;
    QChar groupSeparator;
    bool forceVfoMode;
    bool autoPowerOn;
};

#endif // PREFS_H
