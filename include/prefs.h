#ifndef PREFS_H
#define PREFS_H

#include <QString>
#include <QColor>
#include <QMap>
#include "audioconverter.h"
#include "cluster.h"
#include "rigidentities.h"
#include "wfviewtypes.h"
#include "txaudioprocessor.h"  // for EQ_BANDS constant

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
    if_all = 1 << 27
};

enum prefColItem {
    col_grid = 1 << 0,
    col_axis = 1 << 1,
    col_text = 1 << 2,
    col_plotBackground = 1 << 3,
    col_spectrumLine = 1 << 4,
    col_spectrumFill = 1 << 5,
    col_useSpectrumFillGradient = 1 << 6,
    col_spectrumFillTop = 1 << 7,
    col_spectrumFillBot = 1 << 8,
    col_underlayLine = 1 << 9,
    col_underlayFill = 1 << 10,
    col_useUnderlayFillGradient = 1 << 11,
    col_underlayFillTop = 1 << 12,
    col_underlayFillBot = 1 << 13,
    col_tuningLine = 1 << 14,
    col_passband = 1 << 15,
    col_pbtIndicator = 1 << 16,
    col_meterLevel = 1 << 17,
    col_meterAverage = 1 << 18,
    col_meterPeakLevel = 1 << 19,
    col_meterHighScale = 1 << 20,
    col_meterScale = 1 << 21,
    col_meterText = 1 << 22,
    col_waterfallBack = 1 << 23,
    col_waterfallGrid = 1 << 24,
    col_waterfallAxis = 1 << 25,
    col_waterfallText = 1 << 26,
    col_clusterSpots = 1 << 27,
    col_buttonOff = 1 << 28,
    col_buttonOn = 1 << 29,
    col_all = 1 << 30
};

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
    rs_all = 1 << 12
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
    ra_localAFgain = 1 << 8,
    ra_audioSystem = 1 << 9,
    ra_manufacturer = 1 << 10,
    ra_all = 1 << 11
};

enum prefCtItem {
    ct_enablePTT = 1 << 0,
    ct_niceTS = 1 << 1,
    ct_automaticSidebandSwitching = 1 << 2,
    ct_enableUSBControllers = 1 << 3,
    ct_USBControllersSetup = 1 << 4,
    ct_USBControllersReset = 1 << 5,
    ct_all = 1 << 6
};

enum prefLanItem {

    l_enableLAN = 1 << 0,
    l_enableRigCtlD = 1 << 1,
    l_rigCtlPort = 1 << 2,
    l_tcpPort = 1 << 3,
    l_tciPort = 1 << 4,
    l_waterfallFormat = 1 << 5,
    l_all = 1 << 6
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
    cl_all = 1 << 11
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
    u_all = 1 << 19
};



// ─────────────────────────────────────────────────────────────────────────────
// TX Audio Processing preferences
// All defaults produce a flat/bypass signal (no DSP applied).
// ─────────────────────────────────────────────────────────────────────────────
struct txAudioProcessingPrefs {
    bool  bypass        = true;   // master bypass — skips all DSP and gain
    bool  compEnabled   = false;
    bool  eqEnabled     = false;
    bool  eqFirst       = false;    // true = EQ→Comp, false = Comp→EQ
    float inputGainDB   = 10.0f;    // -20 to +20 dB
    float outputGainDB  = 0.0f;    // -20 to +20 dB
    float eqBands[TxAudioProcessor::EQ_BANDS] = {};  // dB, default all 0
    float compPeakLimit = -10.0f;  // dB, -30 to 0
    float compRelease   = 0.1f;    // seconds, 0.01 to 1.0
    float compFastRatio = 0.2f;    // 0.0 to 1.0, higher values are less compressed
    float compSlowRatio = 0.2f;    // 0.0 to 1.0, higher values are less compressed
    bool  sidetoneEnabled = false;
    float sidetoneLevel   = 0.5f;  // 0.0 to 1.0
    bool  muteRx          = false; // mute RX audio while self-monitoring, not saved to preferences
    bool  spectrumEnabled       = false; // enable TX spectrum display
    int   spectrumFPS           = 10;   // repaint rate; 30 fps = 3 FFTs per 100 ms audio block
    bool  specInhibitDuringRx   = true; // pause TX spectrum while not transmitting

    // Noise gate — runs before input gain on the raw microphone signal.
    bool  gateEnabled   = false;
    float gateThreshold = -65.0f; // dBFS, -70 .. 0
    float gateAttack    =  5.0f; // ms, 0.01 .. 1000
    float gateHold      = 40.0f; // ms, 2 .. 2000
    float gateDecay     = 20.0f; // ms, 2 .. 4000
    float gateRange     = -90.0f; // dB attenuation when closed, -90 .. 0
    float gateLfCutoff  =  380.0f; // Hz, key highpass, 20 .. 4000
    float gateHfCutoff  = 2700.0f;// Hz, key lowpass,  200 .. 20000
};

// ─────────────────────────────────────────────────────────────────────────────
// RX Audio Processing preferences
// Defaults mirror the algorithm defaults from the demo programs.
// ─────────────────────────────────────────────────────────────────────────────

// Which NR algorithm is active
enum class RxNrMode { None = 0, Speex = 1, Anr = 2 };

struct rxAudioProcessingPrefs {
    bool       bypass      = true;  // master bypass — skips NR and output gain
    bool       nrEnabled   = false;  // enable noise reduction
    RxNrMode   nrMode      = RxNrMode::None;

    // ── Channel selection (stereo only; ignored for mono) ──────────────────
    // 1=left only, 2=right only, 3=sum to mono
    int  channelSelect = 3;

    // ── Speex parameters ─────────────────────────────────────────────────────
    int  speexSuppression  = -30;    // max noise attenuation dB (negative, e.g. -30)
    int  speexBandsPreset  =  3;     // index into band_presets[] in filterbank.h
    int  speexFrameMs      = 20;     // frame size in ms (10 or 20 typical)
    bool speexAgc            = false;
    float speexAgcLevel      = 8000.0f;
    int   speexAgcMaxGain    = 30;   // dB
    bool  speexVad           = false;
    int   speexVadProbStart  = 85;   // 0–100 %; probability to enter voice state
    int   speexVadProbCont   = 65;   // 0–100 %; probability to stay in voice state
    float speexSnrDecay       = 0.7f;   // 0.0–0.95; zeta smoothing (lower = faster recovery)
    float speexNoiseUpdateRate= 0.03f;  // 0.01–0.5; noise floor adaptation speed
    float speexPriorBase      = 0.1f;   // 0.05–0.5; min weight on current observation

    // ── ANR (Audacity Noise Reduction) parameters ─────────────────────────────
    double anrNoiseReductionDb =  20.0;  // dB of suppression, 0–48
    double anrSensitivity      =   1.1;  // –log10(prob), 0–24
    int    anrFreqSmoothing    =   4;    // frequency-smoothing bands, 0–6

    // ── RX Equalizer (TriplePara: low shelf, low-mid, high-mid, high shelf) ──
    static constexpr int RX_EQ_BANDS = 4;
    bool  eqEnabled     = false;     // master EQ enable
    float eqGain[RX_EQ_BANDS]  = {0.0f, 0.0f, 0.0f, 0.0f};  // dB, ±6
    float eqFreq[RX_EQ_BANDS]  = {100.0f, 800.0f, 2000.0f, 3500.0f};  // Hz
    float eqQ[RX_EQ_BANDS]     = {1.0f, 1.0f, 1.0f, 1.0f};  // Q for mid bands; slope for shelves

    // ── Output gain ──────────────────────────────────────────────────────────
    float outputGainDB = 0.0f;       // -6 to +20 dB post-NR gain

    // ── Spectrum display ─────────────────────────────────────────────────────
    bool spectrumEnabled       = false;  // enable RX spectrum display
    int  spectrumFPS           = 10;     // repaint rate (fps)
    bool specInhibitDuringTx   = true;   // pause RX spectrum while transmitting
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
    underlay_t underlayMode = underlayNone;
    int underlayBufferSize = 64;
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
    quint8 radioCIVAddr;
    bool CIVisRadioModel;
    pttType_t pttType;
    int polling_ms;
    QString serialPortRadio;
    quint32 serialPortBaud;
    QString virtualSerialPort;
    quint8 localAFgain;
    audioType audioSystem;

    // Controls:
    bool enablePTT;
    bool niceTS;
    bool automaticSidebandSwitching = true;
    bool enableUSBControllers;

    // LAN:
    bool enableLAN;
    bool enableRigCtlD;
    quint16 rigCtlPort;
    quint16 tcpPort;
    quint16 tciPort=50001;
    quint8 waterfallFormat;

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
