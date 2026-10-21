#include "MainController.h"

#include "audiohandlertciinput.h"
#include "audiohandlertcioutput.h"
#include "iaxclientsession.h"
#include "icomserver.h"
#include "kenwoodserver.h"
#include "yaesuserver.h"
#include "logcategories.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <memory>

static QVariantList spectrumToVariantList(const QVector<double>& bins)
{
    QVariantList out;
    out.reserve(bins.size());
    for (double v : bins)
        out.append(v);
    return out;
}

static QString txAudioInputName(TxAudioInput input)
{
    switch (input) {
    case TxAudioInput::None:
        return QStringLiteral("none");
    case TxAudioInput::LocalInput:
        return QStringLiteral("local-input");
    case TxAudioInput::WfShare:
        return QStringLiteral("wfshare");
    case TxAudioInput::Tci:
        return QStringLiteral("tci");
    }
    return QStringLiteral("unknown");
}

static QString pttOriginName(PttOrigin origin)
{
    switch (origin) {
    case PttOrigin::None:
        return QStringLiteral("none");
    case PttOrigin::Radio:
        return QStringLiteral("radio");
    case PttOrigin::Wfview:
        return QStringLiteral("wfview");
    case PttOrigin::ExternalCiv:
        return QStringLiteral("external-civ");
    case PttOrigin::RigCtl:
        return QStringLiteral("rigctl");
    case PttOrigin::Tci:
        return QStringLiteral("tci");
    }
    return QStringLiteral("unknown");
}

static constexpr auto shortcutDebugWindow = "app.debug";
static constexpr auto shortcutRaiseMainWindow = "app.raiseMainWindow";
static constexpr auto shortcutOpenSettings = "app.openSettings";
static constexpr auto shortcutToggleFullscreen = "app.toggleFullscreen";
static constexpr auto shortcutQuitApplication = "app.quit";
static constexpr auto shortcutOpenCwSender = "app.openCwSender";
static constexpr auto shortcutCloseWindow = "app.closeWindow";
static constexpr auto shortcutOpenMemories = "app.openMemories";
static constexpr auto shortcutPopoutReceiver = "app.popoutReceiver";
static constexpr auto shortcutPopinReceiver = "app.popinReceiver";
static constexpr auto shortcutEnterFrequency = "app.enterFrequency";
static constexpr auto macroCommandPrefix = "app.macro.";
static constexpr auto macroRawCommand = "raw";
static constexpr auto macroPttOn = "ptt.on";
static constexpr auto macroPttOff = "ptt.off";
// Delay after the initial rig state queries before running the on-connect macro.
static constexpr int connectMacroDelayMs = 1000;
// Time for on-disconnect commands to reach the radio before the connection closes.
static constexpr int disconnectCommandWaitMs = 250;
// Extra time after an on-disconnect macro's last step, so its commands are sent.
static constexpr int disconnectMacroMarginMs = 500;
static constexpr auto macroPttToggle = "ptt.toggle";
static constexpr auto macroRestoreMode = "restore.mode";
static constexpr auto macroRestoreTxPower = "restore.txPower";

static funcs shortcutFuncFromName(const QString& commandName)
{
    for (int i = 0; i < int(funcLastFunc); ++i) {
        if (commandName == funcString[i])
            return funcs(i);
    }
    return funcNone;
}

static QString portableShortcutText(const QKeySequence& sequence)
{
    QString text = sequence.toString(QKeySequence::PortableText);
    if (text == QLatin1String("+"))
        return QStringLiteral("Plus");
    if (text == QLatin1String("Shift++"))
        return QStringLiteral("Shift+Plus");
    if (text == QLatin1String("Ctrl++"))
        return QStringLiteral("Ctrl+Plus");
    if (text == QLatin1String("Alt++"))
        return QStringLiteral("Alt+Plus");
    if (text == QLatin1String("Ctrl+Shift++") || text == QLatin1String("Shift+Ctrl++"))
        return QStringLiteral("Ctrl+Shift+Plus");
    return text;
}

static int fixedFrequencyShortcutHz(const QString& sequence, int configuredValue)
{
    if (sequence == QLatin1String("-") || sequence == QLatin1String("Plus") ||
        sequence == QLatin1String("J") || sequence == QLatin1String("K") ||
        sequence == QLatin1String("Shift+-") || sequence == QLatin1String("Shift+Plus") ||
        sequence == QLatin1String("Shift+J") || sequence == QLatin1String("Shift+K") ||
        sequence == QLatin1String("Ctrl+-") || sequence == QLatin1String("Ctrl+Plus") ||
        sequence == QLatin1String("Ctrl+J") || sequence == QLatin1String("Ctrl+K") ||
        sequence == QLatin1String("PgUp") || sequence == QLatin1String("PgDown"))
        return qMax(1, qAbs(configuredValue));

    return 0;
}

static bool isMacroLevelFunc(funcs func)
{
    switch (func) {
    case funcAfGain:
    case funcRfGain:
    case funcSquelch:
    case funcRFPower:
    case funcMicGain:
    case funcLANModLevel:
    case funcUSBModLevel:
    case funcACCAModLevel:
    case funcACCBModLevel:
    case funcCompressorLevel:
    case funcNBLevel:
    case funcNRLevel:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        return true;
    default:
        return false;
    }
}

static bool isMacroFrequencyFunc(funcs func)
{
    return func == funcFreq || func == funcSelectedFreq || func == funcFreqSet
           || func == funcVFOFrequency || func == funcUnselectedFreq;
}

static bool isMacroModeFunc(funcs func)
{
    return func == funcMode || func == funcModeSet || func == funcSelectedMode || func == funcUnselectedMode;
}

static bool parseMacroBool(const QString& text, bool* out)
{
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("on") || t == QLatin1String("true") || t == QLatin1String("yes")
        || t == QLatin1String("enable") || t == QLatin1String("enabled") || t == QLatin1String("1")) {
        *out = true;
        return true;
    }
    if (t == QLatin1String("off") || t == QLatin1String("false") || t == QLatin1String("no")
        || t == QLatin1String("disable") || t == QLatin1String("disabled") || t == QLatin1String("0")) {
        *out = false;
        return true;
    }
    return false;
}

// Accepts "7.215" (MHz), "7215 kHz", "7215000 Hz" or "7.215 MHz". A bare number above
// 100000 is taken as Hz so a pasted Hz value also works.
static bool parseMacroFrequencyHz(const QString& text, quint64* hz)
{
    QString t = text.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char(','));
    double scale = 0;
    if (t.endsWith(QLatin1String("mhz"))) {
        t.chop(3);
        scale = 1e6;
    } else if (t.endsWith(QLatin1String("khz"))) {
        t.chop(3);
        scale = 1e3;
    } else if (t.endsWith(QLatin1String("hz"))) {
        t.chop(2);
        scale = 1;
    } else if (t.endsWith(QLatin1Char('m'))) {
        t.chop(1);
        scale = 1e6;
    } else if (t.endsWith(QLatin1Char('k'))) {
        t.chop(1);
        scale = 1e3;
    }

    bool ok = false;
    const double value = t.toDouble(&ok);
    if (!ok || value <= 0)
        return false;
    if (scale == 0)
        scale = value > 100000 ? 1 : 1e6;

    const double result = std::round(value * scale);
    if (result < 1 || result > 1e12)
        return false;
    *hz = quint64(result);
    return true;
}

static bool focusedControlAcceptsTextInput()
{
    if (!QGuiApplication::focusObject())
        return false;

    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    if (!inputMethod)
        return false;

    return inputMethod->queryFocusObject(Qt::ImEnabled, QVariant()).toBool();
}

MainController::MainController(QString settingsFile, QString logFileName, bool debugMode, QObject *p)
    : QObject(p)
    , m_settings(std::make_unique<SettingsController>(settingsFile,this))
    , m_selRad(std::make_unique<SelectRadioController>())
    , m_cwSender(std::make_unique<CWSenderController>())
{
    Q_UNUSED(logFileName)
    Q_UNUSED(debugMode)

    systemPalette = QGuiApplication::palette();

    // setup an initial uiSpecs

    uiSpecs["program"] = QVariantMap{
        {"wfviewVersion",QString(WFVIEW_VERSION)},
        {"gitShort",QString(GITSHORT)},
        {"buildDate",QString(__DATE__)},
        {"buildTime",QString(__TIME__)},
        {"buildUser",QString(UNAME)},
        {"buildHost",QString(HOST)}
    };
    emit uiSpecsChanged();

    prefs = m_settings->getPrefs();
    udpPrefs = m_settings->getUdpPrefs();
    qApp->installEventFilter(this);

    // Audio handlers report device failures from their own threads.
    connect(audioDeviceNotifier::instance(), &audioDeviceNotifier::deviceProblem,
            this, &MainController::handleAudioDeviceProblem, Qt::QueuedConnection);
    m_cwSender->loadSettings(prefs->cwCutNumbers,
                             prefs->cwSendImmediate,
                             prefs->cwSidetoneEnabled,
                             prefs->cwSidetoneLevel,
                             prefs->cwMacroList);
    connect(m_cwSender.get(), &CWSenderController::keyerSettingsChanged, this, [this]() {
        if (!prefs || !m_settings)
            return;

        prefs->cwCutNumbers = m_cwSender->cutNumbers();
        prefs->cwSendImmediate = m_cwSender->sendImmediate();
        prefs->cwSidetoneEnabled = m_cwSender->sidetoneEnable();
        prefs->cwSidetoneLevel = m_cwSender->sidetoneLevel();
        prefs->cwMacroList = m_cwSender->getMacroText();
        m_settings->markDirty();
    });
    setManufacturer(prefs->manufacturer);
    setAppTheme(prefs->useSystemTheme);
    qDebug(logGui()) << "Theme text color:" << m_theme.text().name();

    m_theme.syncFromAppPalette();

    // We should be the first thing to request the queue so need to delete when finished
    queue = cachingQueue::getInstance(this);
    connect(queue,&cachingQueue::sendValue,this,&MainController::receiveValueFromQueue);
    //connect(queue,SIGNAL(sendMessage(QString)),this,SLOT(showStatusBarText(QString)));
    //connect(queue,SIGNAL(intervalUpdated(qint64)),this,SLOT(updatedQueueInterval(qint64)));

    connect(queue, &cachingQueue::finished, queue, &cachingQueue::deleteLater);
    // Make sure we know about any changes to rigCaps
    connect(queue, &cachingQueue::rigCapsUpdated, this, &MainController::receiveRigCaps);
    m_macroTimer.setSingleShot(true);
    connect(&m_macroTimer, &QTimer::timeout, this, &MainController::runNextMacroStep);
    m_connectMacroTimer.setSingleShot(true);
    connect(&m_connectMacroTimer, &QTimer::timeout, this, [this]() {
        if (prefs && prefs->runMacroOnConnect && connStatus == connectionStatus_t::connConnected) {
            qInfo(logSystem()) << "Running on-connect macro" << prefs->connectMacroId;
            runMacro(prefs->connectMacroId);
        }
    });
    m_pttTimeoutTimer.setSingleShot(true);
    connect(&m_pttTimeoutTimer, &QTimer::timeout, this, &MainController::handlePttTimeout);
    m_footerErrorTimer.setSingleShot(true);
    connect(&m_footerErrorTimer, &QTimer::timeout, this, [this]() {
        // Only clear the error if nothing newer has replaced it.
        if (m_footerMessageText == m_footerErrorText)
            setFooterMessageText(QString());
        m_footerErrorText.clear();
    });
    radioClockSyncTimer.setSingleShot(true);
    connect(&radioClockSyncTimer, &QTimer::timeout, this, &MainController::sendRadioClockSync);

    connect(m_settings.get(), &SettingsController::colChanged, this, &MainController::colChanged);
    connect(m_settings.get(), &SettingsController::ifChanged, this, &MainController::ifChanged);
    connect(m_settings.get(), &SettingsController::ctChanged, this, &MainController::ctChanged);
    connect(m_settings.get(), &SettingsController::raChanged, this, &MainController::raChanged);
    connect(m_settings.get(), &SettingsController::lanChanged, this, &MainController::lanChanged);
    connect(m_settings.get(), &SettingsController::clusterChanged, this, &MainController::clusterChanged);
    connect(m_settings.get(), &SettingsController::serverChanged, this, &MainController::serverChanged);
    connect(m_settings.get(), &SettingsController::audioProcChanged, this, &MainController::applyAudioProcessingSettings);
    queue->interval(prefs->wfShareEnabled ? wfShareCmdInterval_ms : cmdStartupInterval_ms);

#if defined(USB_CONTROLLER)
    if (prefs->enableUSBControllers)
        QTimer::singleShot(0, this, &MainController::setupUsbControllerDevice);
#endif

    ensureAudioProcessors();
    setupRigCtlServer();
    setupTciServer();
    setupClusterClient();
    if (prefs->hasRunSetup && !m_settings->upgradedFromLegacyVersion()) {
        if (prefs->wfShareEnabled) {
            QTimer::singleShot(3000, this, &MainController::openRigConnection);
        } else {
            openRigConnection();
        }
    }

}

void MainController::updateApplicationPalette()
{
    colorPrefsType colors = m_settings->getCurrentColorPreset();

    qDebug(logGui()) << "Color preset"
                     << "window" << colors.window
                     << "button" << colors.button
                     << "buttonText" << colors.buttonText;

    for (auto *r : std::as_const(receivers)) {
        if (r) r->setColors(colors);
    }

    if (!prefs->useSystemTheme) {
    // Active (normal) state
        palette.setColor(QPalette::Active, QPalette::Window, colors.window);
        palette.setColor(QPalette::Active, QPalette::WindowText, colors.windowText);
        palette.setColor(QPalette::Active, QPalette::Base, colors.base);
        palette.setColor(QPalette::Active, QPalette::AlternateBase, colors.alternateBase);
        palette.setColor(QPalette::Active, QPalette::Text, colors.mainText);
        palette.setColor(QPalette::Active, QPalette::Button, colors.button);
        palette.setColor(QPalette::Active, QPalette::ButtonText, colors.buttonText);
        palette.setColor(QPalette::Active, QPalette::BrightText, colors.brightText);
        palette.setColor(QPalette::Active, QPalette::Light, colors.light);
        palette.setColor(QPalette::Active, QPalette::Midlight, colors.midLight);
        palette.setColor(QPalette::Active, QPalette::Dark, colors.dark);
        palette.setColor(QPalette::Active, QPalette::Mid, colors.mid);
        palette.setColor(QPalette::Active, QPalette::Shadow, colors.shadow);
        palette.setColor(QPalette::Active, QPalette::Highlight, colors.highlight);
        palette.setColor(QPalette::Active, QPalette::HighlightedText, colors.highlightedText);
        palette.setColor(QPalette::Active, QPalette::Link, colors.link);
        palette.setColor(QPalette::Active, QPalette::LinkVisited, colors.linkVisited);
        palette.setColor(QPalette::Active, QPalette::ToolTipBase, colors.toolTipBase);
        palette.setColor(QPalette::Active, QPalette::ToolTipText, colors.toolTipText);
        palette.setColor(QPalette::Active, QPalette::PlaceholderText, colors.placeholder);

        // Inactive state (same as active)
        palette.setColor(QPalette::Inactive, QPalette::Window, colors.window);
        palette.setColor(QPalette::Inactive, QPalette::WindowText, colors.windowText);
        palette.setColor(QPalette::Inactive, QPalette::Base, colors.base);
        palette.setColor(QPalette::Inactive, QPalette::AlternateBase, colors.alternateBase);
        palette.setColor(QPalette::Inactive, QPalette::Text, colors.mainText);
        palette.setColor(QPalette::Inactive, QPalette::Button, colors.button);
        palette.setColor(QPalette::Inactive, QPalette::ButtonText, colors.buttonText);
        palette.setColor(QPalette::Inactive, QPalette::BrightText, colors.brightText);
        palette.setColor(QPalette::Inactive, QPalette::Light, colors.light);
        palette.setColor(QPalette::Inactive, QPalette::Midlight, colors.midLight);
        palette.setColor(QPalette::Inactive, QPalette::Dark, colors.dark);
        palette.setColor(QPalette::Inactive, QPalette::Mid, colors.mid);
        palette.setColor(QPalette::Inactive, QPalette::Shadow, colors.shadow);
        palette.setColor(QPalette::Inactive, QPalette::Highlight, colors.highlight);
        palette.setColor(QPalette::Inactive, QPalette::HighlightedText, colors.highlightedText);
        palette.setColor(QPalette::Inactive, QPalette::Link, colors.link);
        palette.setColor(QPalette::Inactive, QPalette::LinkVisited, colors.linkVisited);
        palette.setColor(QPalette::Inactive, QPalette::ToolTipBase, colors.toolTipBase);
        palette.setColor(QPalette::Inactive, QPalette::ToolTipText, colors.toolTipText);
        palette.setColor(QPalette::Inactive, QPalette::PlaceholderText, colors.placeholder);

        // Disabled state - calculate grayed-out versions
        QColor disabledWindowText = colors.windowText.darker(250);
        QColor disabledText = colors.mainText.darker(250);
        QColor disabledButtonText = colors.buttonText.darker(250);
        QColor disabledButton = colors.button.darker(130);
        QColor disabledBase = colors.base.darker(110);
        QColor disabledHighlight = colors.highlight.darker(150);
        QColor disabledPlaceholder = colors.placeholder.darker(150);

        palette.setColor(QPalette::Disabled, QPalette::Window, colors.window);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledWindowText);
        palette.setColor(QPalette::Disabled, QPalette::Base, disabledBase);
        palette.setColor(QPalette::Disabled, QPalette::AlternateBase, disabledBase);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Button, disabledButton);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledButtonText);
        palette.setColor(QPalette::Disabled, QPalette::BrightText, colors.brightText);  // Usually stays bright
        palette.setColor(QPalette::Disabled, QPalette::Light, colors.light.darker(120));
        palette.setColor(QPalette::Disabled, QPalette::Midlight, colors.midLight.darker(120));
        palette.setColor(QPalette::Disabled, QPalette::Dark, colors.dark);
        palette.setColor(QPalette::Disabled, QPalette::Mid, colors.mid);
        palette.setColor(QPalette::Disabled, QPalette::Shadow, colors.shadow);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, disabledHighlight);
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Link, colors.link.darker(150));
        palette.setColor(QPalette::Disabled, QPalette::LinkVisited, colors.linkVisited.darker(150));
        palette.setColor(QPalette::Disabled, QPalette::ToolTipBase, colors.toolTipBase);  // Tooltips usually don't change
        palette.setColor(QPalette::Disabled, QPalette::ToolTipText, colors.toolTipText);
        palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledPlaceholder);
        QGuiApplication::setPalette(palette);

    } else {
        QGuiApplication::setPalette(systemPalette);
    }
    qDebug(logGui()) << "Palette text colors"
                     << "window" << palette.color(QPalette::Active, QPalette::WindowText)
                     << "disabledWindow" << palette.color(QPalette::Disabled, QPalette::WindowText)
                     << "button" << palette.color(QPalette::Active, QPalette::ButtonText)
                     << "disabledButton" << palette.color(QPalette::Disabled, QPalette::ButtonText);
}

void MainController::ensureAudioProcessors()
{
    if (!txProc) {
        txProc = new TxAudioProcessor(this);
    }

    if (!rxProc) {
        rxProc = new RxAudioProcessor(this);
        rxProc->setMuted(m_rxMuted);
        if (m_cwSender)
            rxProc->setCwSidetone(m_cwSender->sidetoneGenerator());

        // Per-radio ANR noise profile store, derived from the settings file.
        // For an explicit --settings file: same directory and base name
        // (e.g. ic7610.conf → ic7610.noise). For the default QSettings
        // (macOS plist / Windows registry / Linux ~/.config) use AppDataLocation
        // so the file always lands in a real writable directory.
        QString noisePath;
        const QString sf = m_settings ? m_settings->settingsFileName() : QString();
        const QFileInfo fi(sf);
        // Test for an ordinary file path — registry keys don't have a readable suffix.
        if (!sf.isEmpty() && fi.suffix().length() >= 2 && fi.suffix().length() <= 5
                && fi.absoluteDir().exists()) {
            noisePath = fi.absoluteDir().absoluteFilePath(fi.baseName() + ".noise");
        } else {
            QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (appData.isEmpty())
                appData = QDir::homePath();
            QDir().mkpath(appData);
            noisePath = appData + "/wfview.noise";
        }
        rxProc->setNoiseStorePath(noisePath);
    }

    if (!m_audioProcessorSignalsConnected) {
        connect(txProc, &TxAudioProcessor::haveSidetoneFloat,
                rxProc, &RxAudioProcessor::injectSidetone,
                Qt::DirectConnection);
        connect(txProc, &TxAudioProcessor::txSpectrumBins,
                this, [this](QVector<double> inBins, QVector<double> outBins, float rawSR) {
                    if (prefs->txAudioProc.specInhibitDuringRx && !m_transmitting)
                        return;
                    emit txAudioSpectrumChanged(spectrumToVariantList(inBins),
                                                spectrumToVariantList(outBins),
                                                rawSR);
                });
        connect(txProc, &TxAudioProcessor::txInputLevel,
                this, [this](float peak) {
                    ++m_txAudioBlocks;
                    if ((m_txAudioBlocks % 20) == 0)
                        emit audioProcessingBlocksChanged(m_txAudioBlocks, m_rxAudioBlocks);
                    m_txAudioInputLevel = peak;
                    emit txAudioMetersChanged(m_txAudioInputLevel, m_txAudioOutputLevel, m_txAudioGainReduction);
                });
        connect(txProc, &TxAudioProcessor::txOutputLevel,
                this, [this](float peak) {
                    m_txAudioOutputLevel = peak;
                    emit txAudioMetersChanged(m_txAudioInputLevel, m_txAudioOutputLevel, m_txAudioGainReduction);
                });
        connect(txProc, &TxAudioProcessor::txGainReduction,
                this, [this](float linearGain) {
                    m_txAudioGainReduction = qBound(0.0, (1.0 - double(linearGain)) * 100.0, 100.0);
                    emit txAudioMetersChanged(m_txAudioInputLevel, m_txAudioOutputLevel, m_txAudioGainReduction);
                });
        connect(rxProc, &RxAudioProcessor::rxSpectrumBins,
                this, [this](QVector<double> inBins, QVector<double> outBins, float rawSR) {
                    if (prefs->rxAudioProc.specInhibitDuringTx && m_transmitting)
                        return;
                    emit rxAudioSpectrumChanged(spectrumToVariantList(inBins),
                                                spectrumToVariantList(outBins),
                                                rawSR);
                });
        connect(rxProc, &RxAudioProcessor::rxInputLevel,
                this, [this](float peak) {
                    ++m_rxAudioBlocks;
                    if ((m_rxAudioBlocks % 20) == 0)
                        emit audioProcessingBlocksChanged(m_txAudioBlocks, m_rxAudioBlocks);
                    m_rxAudioInputLevel = peak;
                    emit rxAudioMetersChanged(m_rxAudioInputLevel, m_rxAudioOutputLevel);
                });
        connect(rxProc, &RxAudioProcessor::rxOutputLevel,
                this, [this](float peak) {
                    m_rxAudioOutputLevel = peak;
                    emit rxAudioMetersChanged(m_rxAudioInputLevel, m_rxAudioOutputLevel);
                });
        // A mode change may load (or lack) a stored profile for the new mode.
        connect(rxProc, &RxAudioProcessor::anrModeProfileStatus,
                this, [this](QString, bool hasProfile) {
                    if (m_anrHasProfile != hasProfile) {
                        m_anrHasProfile = hasProfile;
                        emit anrProfileStateChanged();
                    }
                });
        connect(rxProc, &RxAudioProcessor::anrNoiseProfileBins,
                this, [this](QVector<double> bins, double sampleRate, int windowSize) {
                    m_anrNoiseProfileBins = spectrumToVariantList(bins);
                    m_anrNoiseProfileSampleRate = sampleRate;
                    m_anrNoiseProfileWindowSize = windowSize;
                    emit anrNoiseProfileChanged();
                });
        m_anrProfileTimer.setInterval(1000);
        connect(&m_anrProfileTimer, &QTimer::timeout, this, [this]() {
            if (--m_anrProfileSecondsLeft <= 0)
                stopAnrNoiseProfile();
            else
                emit anrProfileStateChanged();
        });
        m_anrHasProfile = rxProc->anrHasProfile();
        m_audioProcessorSignalsConnected = true;
    }

    applyAudioProcessingSettings();
    prefs->txSetup.txProc = txProc;
    prefs->rxSetup.rxProc = rxProc;
}

void MainController::applyAudioProcessingSettings()
{
    applyTxAudioProcPrefs(prefs->txAudioProc);
    applyRxAudioProcPrefs(prefs->rxAudioProc);
}

void MainController::applyTxAudioProcPrefs(const txAudioProcessingPrefs& p)
{
    if (!txProc)
        return;

    txProc->setCompEnabled(p.compEnabled);
    txProc->setEqEnabled(p.eqEnabled);
    txProc->setEqFirst(p.eqFirst);
    txProc->setInputGainDB(p.inputGainDB);
    txProc->setOutputGainDB(p.outputGainDB);
    for (int i = 0; i < TxAudioProcessor::EQ_BANDS; ++i)
        txProc->setEqBand(i, p.eqBands[i]);
    txProc->setCompPeakLimit(p.compPeakLimit);
    txProc->setCompRelease(p.compRelease);
    txProc->setCompFastRatio(p.compFastRatio);
    txProc->setCompSlowRatio(p.compSlowRatio);
    txProc->setSidetoneEnabled(p.sidetoneEnabled);
    txProc->setSidetoneLevel(p.sidetoneLevel);
    // The RX mute only makes sense while self-monitoring; never leave RX
    // audio muted by a control that is no longer active.
    if (!p.sidetoneEnabled && m_rxMuted)
        setRxMuted(false);
    txProc->setSpectrumEnabled(p.spectrumEnabled);
    m_txSpectrumEnabled = p.spectrumEnabled;
    emit audioProcessingSpectrumStateChanged(m_txSpectrumEnabled, m_rxSpectrumEnabled);
    txProc->setSpectrumFps(p.spectrumFPS);
    txProc->setBypassed(p.bypass);
    txProc->setGateEnabled(p.gateEnabled);
    txProc->setGateThreshold(p.gateThreshold);
    txProc->setGateAttack(p.gateAttack);
    txProc->setGateHold(p.gateHold);
    txProc->setGateDecay(p.gateDecay);
    txProc->setGateRange(p.gateRange);
    txProc->setGateLfCutoff(p.gateLfCutoff);
    txProc->setGateHfCutoff(p.gateHfCutoff);
}

void MainController::applyRxAudioProcPrefs(const rxAudioProcessingPrefs& p)
{
    if (!rxProc)
        return;

    rxProc->setBypassed(p.bypass);
    rxProc->setNrEnabled(!p.bypass);
    rxProc->setNrMode(p.nrMode);
    rxProc->setChannelSelect(p.channelSelect);
    rxProc->setSpeexSuppression(p.speexSuppression);
    rxProc->setSpeexBandsPreset(p.speexBandsPreset);
    rxProc->setSpeexFrameMs(p.speexFrameMs);
    rxProc->setSpeexAgc(p.speexAgc);
    rxProc->setSpeexAgcLevel(p.speexAgcLevel);
    rxProc->setSpeexAgcMaxGain(p.speexAgcMaxGain);
    rxProc->setSpeexVad(p.speexVad);
    rxProc->setSpeexVadProbStart(p.speexVadProbStart);
    rxProc->setSpeexVadProbCont(p.speexVadProbCont);
    rxProc->setSpeexSnrDecay(p.speexSnrDecay);
    rxProc->setSpeexNoiseUpdateRate(p.speexNoiseUpdateRate);
    rxProc->setSpeexPriorBase(p.speexPriorBase);
    rxProc->setAnrNoiseReductionDb(p.anrNoiseReductionDb);
    rxProc->setAnrSensitivity(p.anrSensitivity);
    rxProc->setAnrFreqSmoothing(p.anrFreqSmoothing);
    rxProc->setEqEnabled(p.eqEnabled);
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        rxProc->setEqBandGain(i, p.eqGain[i]);
        rxProc->setEqBandFreq(i, p.eqFreq[i]);
        rxProc->setEqBandQ(i, p.eqQ[i]);
    }
    rxProc->setOutputGainDB(p.outputGainDB);
    rxProc->setSpectrumEnabled(p.spectrumEnabled);
    m_rxSpectrumEnabled = p.spectrumEnabled;
    emit audioProcessingSpectrumStateChanged(m_txSpectrumEnabled, m_rxSpectrumEnabled);
    rxProc->setSpectrumFps(p.spectrumFPS);
}

void MainController::startAnrNoiseProfile()
{
    if (!rxProc)
        return;
    // Starting again while collecting discards the partial sample and restarts
    // the countdown.
    rxProc->startAnrProfile();
    m_anrProfiling = true;
    m_anrProfileSecondsLeft = anrProfileSeconds;
    m_anrProfileTimer.start();
    emit anrProfileStateChanged();
}

void MainController::stopAnrNoiseProfile()
{
    if (!m_anrProfiling)
        return;
    m_anrProfileTimer.stop();
    m_anrProfiling = false;
    m_anrProfileSecondsLeft = 0;
    if (rxProc) {
        rxProc->stopAnrProfile();
        m_anrHasProfile = rxProc->anrHasProfile();
    }
    emit anrProfileStateChanged();
}

void MainController::setRxMuted(bool muted)
{
    if (rxProc)
        rxProc->setMuted(muted);
    if (m_rxMuted != muted) {
        m_rxMuted = muted;
        emit rxMutedChanged();
    }
}

void MainController::setReceiverDetached(int i, bool v)
{
    if (i < 0 || i >= detached.size())
        return;

    if (detached[i] != v) {
        detached[i] = v;
        emit receiverDetachedChanged(i, v);
    }

    if (m_settings)
        m_settings->saveReceiverSetting(i, QStringLiteral("Detached"), v);
}

QVariantMap MainController::restoredMainWindowGeometry() const
{
    return m_settings ? m_settings->restoredMainWindowGeometry() : QVariantMap{{"valid", false}};
}

void MainController::saveMainWindowGeometry(int x, int y, int width, int height, bool maximized)
{
    if (m_settings)
        m_settings->saveMainWindowGeometry(x, y, width, height, maximized);
}

void MainController::ifChanged(prefIfItems items)
{
    /*
     *     if_useFullScreen = 1 << 0,
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
     */
    for (int bit = 0; bit < 32; bit++) {
        prefIfItem item = static_cast<prefIfItem>(1 << bit);
        if (items & item) {
            switch (item)
            {
            case if_frequencyUnits:
                // Receiver frequency unit handling has not been ported to QML yet.
                break;
            case if_useSystemTheme:
                updateApplicationPalette();
                break;
            case if_wfAntiAlias:
            case if_wfInterpolate:
            case if_wftheme:
            case if_plotFloor:
            case if_plotCeiling:
            case if_wflength:
                for (int i = 0; prefs && i < receivers.size(); ++i) {
                    const QVariantMap receiverPrefs = m_settings->receiverSettings(i);
                    receivers[i]->setScopeDisplaySettings(receiverPrefs.value("PlotFloor").toInt(),
                                                          receiverPrefs.value("PlotCeiling").toInt(),
                                                          receiverPrefs.value("WFLength").toInt(),
                                                          static_cast<WaterfallItem::Theme>(receiverPrefs.value("WFTheme").toInt()),
                                                          prefs->wfInterpolate,
                                                          prefs->wfAntiAlias);
                }
                break;
            case if_meter2Type:
                configureOptionalMeter(2, prefs ? prefs->meter2Type : meterNone);
                emit optionalMetersChanged();
                break;
            case if_meter3Type:
                configureOptionalMeter(3, prefs ? prefs->meter3Type : meterNone);
                emit optionalMetersChanged();
                break;
            default:
                break;
            }
        }
    }
}

QVariantList MainController::optionalMeterOptions() const
{
    const QList<meter_t> types = {
        meterNone, meterS, meterCenter, meterSWR, meterALC, meterComp,
        meterVoltage, meterCurrent, meterAudio, meterRxAudio, meterTxMod,
        meterdBu, meterdBuEMF, meterdBm
    };

    QVariantList options;
    for (meter_t type : types) {
        QVariantMap option;
        // meterS in a meter slot is S+Power: S on receive, Po on transmit.
        option["text"] = (type == meterS) ? tr("S+Power") : meterString[int(type)];
        option["value"] = int(type);
        option["available"] = isOptionalMeterAvailable(int(type));
        options.push_back(option);
    }
    return options;
}

QVariantList MainController::shortcutCommandOptions() const
{
    QVariantList options = shortcutAppCommandOptions();
    const QVariantList radioOptions = shortcutRadioCommandOptions();
    for (const QVariant& option : radioOptions)
        options.push_back(option);
    return options;
}

QVariantList MainController::shortcutAppCommandOptions() const
{
    QVariantList options;

    QVariantMap debugOption;
    debugOption["text"] = tr("Debug Window");
    debugOption["value"] = QString::fromLatin1(shortcutDebugWindow);
    options.push_back(debugOption);

    const QList<QPair<QString, QString>> appCommands = {
        { tr("Raise Main Window"), QString::fromLatin1(shortcutRaiseMainWindow) },
        { tr("Open Settings"), QString::fromLatin1(shortcutOpenSettings) },
        { tr("Toggle Fullscreen"), QString::fromLatin1(shortcutToggleFullscreen) },
        { tr("Quit wfview"), QString::fromLatin1(shortcutQuitApplication) },
        { tr("Close Window"), QString::fromLatin1(shortcutCloseWindow) },
        { tr("Open Memories"), QString::fromLatin1(shortcutOpenMemories) },
        { tr("Open CW Sender"), QString::fromLatin1(shortcutOpenCwSender) },
        { tr("Pop Out Receiver"), QString::fromLatin1(shortcutPopoutReceiver) },
        { tr("Pop In Receiver"), QString::fromLatin1(shortcutPopinReceiver) },
        { tr("Enter Frequency"), QString::fromLatin1(shortcutEnterFrequency) }
    };
    for (const auto& item : appCommands) {
        QVariantMap option;
        option["text"] = item.first;
        option["value"] = item.second;
        options.push_back(option);
    }

    return options;
}

QVariantList MainController::shortcutRadioCommandOptions() const
{
    QVariantList options;

    for (int i = 0; i < int(funcLastFunc); ++i) {
        const QString text = funcString[i];
        if (text.startsWith(QLatin1String("+<")))
            continue;

        QVariantMap option;
        option["text"] = text;
        option["value"] = text;
        options.push_back(option);
    }

    return options;
}

void MainController::runShortcutAppAction(const QString& commandName)
{
    if (commandName.startsWith(QLatin1String(macroCommandPrefix))) {
        bool ok = false;
        const int macroId = commandName.mid(int(qstrlen(macroCommandPrefix))).toInt(&ok);
        if (ok)
            runMacro(macroId);
        return;
    }
    if (commandName == QLatin1String(shortcutDebugWindow))
        return;
    if (commandName == QLatin1String(shortcutRaiseMainWindow)
        || commandName == QLatin1String(shortcutOpenSettings)
        || commandName == QLatin1String(shortcutToggleFullscreen)
        || commandName == QLatin1String(shortcutCloseWindow)
        || commandName == QLatin1String(shortcutOpenMemories)
        || commandName == QLatin1String(shortcutPopoutReceiver)
        || commandName == QLatin1String(shortcutPopinReceiver)
        || commandName == QLatin1String(shortcutEnterFrequency))
        return;
    if (commandName == QLatin1String(shortcutQuitApplication)) {
        quitApplication();
        return;
    }
    if (commandName == QLatin1String(shortcutOpenCwSender)) {
        showCWSender();
        return;
    }
}

QString MainController::macroRawCommandHint() const
{
    if (rigCaps && rigCaps->manufacturer != manufIcom)
        return tr("CAT command, e.g. FA00007215000;");
    return tr("CI-V command bytes, e.g. 0x14 0x0A 0x01 0x28");
}

QVariantList MainController::macroCommandOptions() const
{
    QVariantList options;
    const auto add = [&options](const QString& text, const QString& value, const QString& category,
                                bool hasParameter, const QString& hint) {
        options.append(QVariantMap {
            { QStringLiteral("text"), text },
            { QStringLiteral("value"), value },
            { QStringLiteral("category"), category },
            { QStringLiteral("hasParameter"), hasParameter },
            { QStringLiteral("hint"), hint }
        });
    };

    add(tr("None"), QStringLiteral("None"), tr("General"), false, QString());
    add(tr("Raw radio command"), QString::fromLatin1(macroRawCommand), tr("General"), true, macroRawCommandHint());

    if (!rigCaps || (rigCaps->hasTransmit && rigCaps->commands.contains(funcTransceiverStatus))) {
        add(tr("Transmit (PTT on)"), QString::fromLatin1(macroPttOn), tr("Transmit"), false, QString());
        add(tr("Receive (PTT off)"), QString::fromLatin1(macroPttOff), tr("Transmit"), false, QString());
        add(tr("Toggle PTT"), QString::fromLatin1(macroPttToggle), tr("Transmit"), false, QString());
    }

    add(tr("Restore mode"), QString::fromLatin1(macroRestoreMode), tr("Restore"), false, QString());
    if (!rigCaps || rigCaps->commands.contains(funcRFPower))
        add(tr("Restore TX power"), QString::fromLatin1(macroRestoreTxPower), tr("Restore"), false, QString());

    // Macros cannot call other macros, so a macro can never start itself.
    const QVariantList appOptions = shortcutAppCommandOptions();
    for (const QVariant& value : appOptions) {
        const QVariantMap option = value.toMap();
        add(option.value(QStringLiteral("text")).toString(), option.value(QStringLiteral("value")).toString(),
            tr("wfview"), false, QString());
    }

    QStringList modeNames;
    if (rigCaps) {
        for (const modeInfo& mode : rigCaps->modes)
            modeNames.append(mode.name);
    }

    for (int i = int(funcNone) + 1; i < int(funcLastFunc); ++i) {
        const funcs func = funcs(i);
        const QString text = funcString[i];
        if (text.startsWith(QLatin1String("+<")))
            continue;
        // With a radio connected, offer only what its rig file can set.
        if (rigCaps && (!rigCaps->commands.contains(func) || !rigCaps->commands.value(func).setCmd))
            continue;

        const funcType type = rigCaps ? rigCaps->commands.value(func) : funcType();
        QString hint;
        if (isMacroFrequencyFunc(func))
            hint = tr("Frequency in MHz, e.g. 7.215");
        else if (isMacroModeFunc(func))
            hint = modeNames.isEmpty() ? tr("Mode, e.g. LSB") : tr("Mode: %1").arg(modeNames.join(QStringLiteral(", ")));
        else if (func == funcAntenna)
            hint = tr("Antenna number, e.g. 2");
        else if (func == funcTunerStatus)
            hint = tr("on, off, or tune");
        else if (func == funcTransceiverStatus)
            hint = tr("on (transmit) or off (receive)");
        else if (isMacroLevelFunc(func))
            hint = tr("Percent, e.g. 25%");
        else if (type.maxVal == 1 && type.minVal == 0)
            hint = tr("on or off");
        else if (type.maxVal > type.minVal)
            hint = tr("Value %1 to %2, or a percent such as 50%").arg(type.minVal).arg(type.maxVal);
        else
            hint = tr("Value, or blank to send the command alone");
        add(text, text, tr("Radio"), true, hint);
    }

    return options;
}

void MainController::runMacro(int macroId)
{
    if (!prefs)
        return;
    if (m_runningMacroId >= 0) {
        qInfo(logSystem()) << "Macro" << macroId << "not started; macro" << m_runningMacroId << "is still running";
        return;
    }

    for (const macroPreference& macro : std::as_const(prefs->macros)) {
        if (macro.id != macroId)
            continue;
        if (macro.steps.isEmpty())
            return;
        qInfo(logSystem()) << "Running macro" << macro.id << macro.name << "with" << macro.steps.size() << "steps";
        m_macroSteps = macro.steps;
        m_macroStepIndex = 0;
        m_macroStartReceiver = currentReceiver;
        m_macroStartMode = currentReceiver < receivers.size() && receivers[currentReceiver]
                               ? receivers[currentReceiver]->getModeInfo() : modeInfo();
        // Use the cache rather than m_txPower, which is 0 until the radio reports a value.
        m_macroStartTxPower = -1;
        if (queue && rigCaps && rigCaps->commands.contains(funcRFPower)) {
            const QVariant power = queue->getCache(funcRFPower, 0).value;
            if (power.isValid())
                m_macroStartTxPower = int(power.value<ushort>());
        }
        m_runningMacroId = macro.id;
        emit runningMacroChanged();
        runNextMacroStep();
        return;
    }
}

void MainController::stopMacro()
{
    m_macroTimer.stop();
    m_macroSteps.clear();
    m_macroStepIndex = 0;
    if (m_runningMacroId < 0)
        return;
    m_runningMacroId = -1;
    emit runningMacroChanged();
}

void MainController::runNextMacroStep()
{
    if (m_runningMacroId < 0 || m_macroStepIndex >= m_macroSteps.size()) {
        stopMacro();
        return;
    }

    const macroStep step = m_macroSteps.at(m_macroStepIndex++);
    executeMacroStep(step);

    // A step may have stopped the macro (for example by quitting or disconnecting).
    if (m_runningMacroId < 0 || m_macroStepIndex >= m_macroSteps.size()) {
        stopMacro();
        return;
    }
    m_macroTimer.start(qMax(step.delayMs, prefs ? prefs->macroSpacingMs : macroSpacingDefaultMs));
}

void MainController::executeMacroStep(const macroStep& step)
{
    if (step.command.isEmpty() || step.command == QLatin1String("None"))
        return;

    if (step.command.startsWith(QLatin1String("app."))) {
        if (step.command.startsWith(QLatin1String(macroCommandPrefix))) {
            qWarning(logSystem()) << "Macro step skipped: macros cannot run other macros";
            return;
        }
        if (m_disconnectPending && step.command == QLatin1String(shortcutQuitApplication)) {
            qWarning(logSystem()) << "Macro step skipped: cannot quit while disconnecting";
            return;
        }
        emit appShortcutActivated(step.command, int(currentReceiver));
        return;
    }

    // Same path as the main window's Transmit button, so the PTT setting is respected.
    if (step.command == QLatin1String(macroPttOn) || step.command == QLatin1String(macroPttOff)
        || step.command == QLatin1String(macroPttToggle)) {
        if (!rigCaps || !rigCaps->hasTransmit) {
            qWarning(logSystem()) << "Macro step" << step.command << "skipped: radio cannot transmit";
            return;
        }
        if (step.command == QLatin1String(macroPttToggle))
            toggleTransmit();
        else
            setTransmit(step.command == QLatin1String(macroPttOn));
        return;
    }

    if (step.command == QLatin1String(macroRestoreMode)) {
        if (m_macroStartReceiver >= receivers.size() || !receivers[m_macroStartReceiver]
            || m_macroStartMode.mk == modeUnknown) {
            qWarning(logSystem()) << "Macro step Restore mode skipped: mode was unknown when the macro started";
            return;
        }
        receivers[m_macroStartReceiver]->restoreModeInfo(m_macroStartMode);
        return;
    }

    if (step.command == QLatin1String(macroRestoreTxPower)) {
        if (m_macroStartTxPower < 0 || !rigCaps || !rigCaps->commands.contains(funcRFPower)) {
            qWarning(logSystem()) << "Macro step Restore TX power skipped: TX power was unknown when the macro started";
            return;
        }
        setTxPower(m_macroStartTxPower);
        return;
    }

    QString error;
    bool ok = false;
    if (step.command == QLatin1String(macroRawCommand)) {
        ok = sendMacroRawCommand(step.parameter, &error);
    } else {
        const funcs func = shortcutFuncFromName(step.command);
        if (func == funcNone)
            error = tr("unknown command");
        else
            ok = executeMacroRadioStep(func, step.parameter, &error);
    }

    if (!ok)
        qWarning(logSystem()) << "Macro step" << step.command << step.parameter << "skipped:" << error;
}

bool MainController::executeMacroRadioStep(funcs func, const QString& parameter, QString* error)
{
    if (!rigCaps || !queue || receivers.isEmpty()) {
        *error = tr("no radio connected");
        return false;
    }
    if (!rigCaps->commands.contains(func)) {
        *error = tr("the rig file has no such command");
        return false;
    }

    const uchar rx = uchar(qBound(0, int(currentReceiver), int(receivers.size()) - 1));
    const QString param = parameter.trimmed();

    if (isMacroFrequencyFunc(func)) {
        quint64 hz = 0;
        if (!parseMacroFrequencyHz(param, &hz)) {
            *error = tr("frequency must be in MHz, e.g. 7.215");
            return false;
        }
        if (freqLock) {
            *error = tr("frequency is locked");
            return false;
        }
        if (func == funcUnselectedFreq) {
            // The inactive VFO is "selected" on rigs with fixed VFO A/B freq commands when B is active
            const funcs otherFreq = queue->vfoFreqSwapped() ? funcSelectedFreq : funcUnselectedFreq;
            queue->add(priorityImmediate, queueItem(otherFreq, QVariant::fromValue(freqt(hz, double(hz) / 1E6, inactiveVFO)),
                                                    false, rx));
        } else {
            receivers[rx]->setExactFrequencyMHz(double(hz) / 1E6);
        }
        return true;
    }

    if (isMacroModeFunc(func)) {
        const QString wanted = QString(param).remove(QLatin1Char('-')).remove(QLatin1Char(' ')).toUpper();
        for (const modeInfo& mode : rigCaps->modes) {
            const QString name = QString(mode.name).remove(QLatin1Char('-')).remove(QLatin1Char(' ')).toUpper();
            if (wanted.isEmpty() || name != wanted)
                continue;
            if (func == funcUnselectedMode) {
                modeInfo requested = mode;
                requested.filter = 1;
                requested.data = 0;
                requested.VFO = inactiveVFO;
                queue->add(priorityImmediate, queueItem(func, QVariant::fromValue(requested), false, rx));
            } else {
                receivers[rx]->setMode(uchar(mode.mk), true);
            }
            return true;
        }
        *error = tr("the radio has no mode named \"%1\"").arg(param);
        return false;
    }

    if (func == funcAntenna) {
        antennaInfo antenna { 0, false };
        const QVariant current = queue->getCache(funcAntenna, rx).value;
        if (current.canConvert<antennaInfo>())
            antenna = current.value<antennaInfo>();

        bool ok = false;
        const int number = param.toInt(&ok);
        bool found = false;
        for (int i = 0; i < int(rigCaps->antennas.size()); ++i) {
            const genericType& item = rigCaps->antennas[size_t(i)];
            if ((ok && number == i + 1) || item.name.compare(param, Qt::CaseInsensitive) == 0) {
                antenna.antenna = item.num;
                found = true;
                break;
            }
        }
        if (!found && ok && number >= 1 && rigCaps->antennas.empty()) {
            antenna.antenna = quint8(number - 1);
            found = true;
        }
        if (!found) {
            *error = tr("unknown antenna \"%1\"").arg(param);
            return false;
        }
        const vfoCommandType t = queue->getVfoCommand(vfoA, rx, true);
        queue->add(priorityImmediate, queueItem(funcAntenna, QVariant::fromValue(antenna), false, t.receiver));
        return true;
    }

    if (func == funcTunerStatus) {
        bool enabled = false;
        if (param.compare(QLatin1String("tune"), Qt::CaseInsensitive) == 0 || param == QLatin1String("2"))
            tuneNow();
        else if (parseMacroBool(param, &enabled))
            setTunerEnabled(enabled);
        else {
            *error = tr("use on, off, or tune");
            return false;
        }
        return true;
    }

    if (func == funcTransceiverStatus) {
        bool transmit = false;
        if (!parseMacroBool(param, &transmit)) {
            *error = tr("use on or off");
            return false;
        }
        setTransmit(transmit);
        return true;
    }

    // Commands that take no value (or a read) are sent on their own.
    if (param.isEmpty()) {
        queue->add(priorityImmediate, func, false, rx);
        return true;
    }

    const funcType type = rigCaps->commands.value(func);
    const QVariant current = queue->getCache(func, rx).value;
    int valueType = current.isValid() ? current.userType()
                                      : ((isMacroLevelFunc(func) || type.maxVal > 255) ? int(QMetaType::UShort)
                                                                                         : int(QMetaType::UChar));

    if (valueType == QMetaType::QString) {
        queue->add(priorityImmediate, queueItem(func, QVariant::fromValue<QString>(param), false, rx));
        return true;
    }

    bool boolValue = false;
    const bool isBoolText = parseMacroBool(param, &boolValue);
    if (valueType == QMetaType::Bool) {
        if (!isBoolText) {
            *error = tr("use on or off");
            return false;
        }
        queue->add(priorityImmediate, queueItem(func, QVariant::fromValue<bool>(boolValue), false, rx));
        return true;
    }

    QString number = param;
    const bool percent = number.endsWith(QLatin1Char('%')) || isMacroLevelFunc(func);
    if (number.endsWith(QLatin1Char('%')))
        number.chop(1);
    bool ok = false;
    double v = isBoolText ? (boolValue ? 1 : 0) : number.trimmed().toDouble(&ok);
    if (!isBoolText && !ok) {
        *error = tr("value must be a number");
        return false;
    }
    if (percent && !isBoolText) {
        if (v < 0 || v > 100) {
            *error = tr("percent must be from 0 to 100");
            return false;
        }
        const int minimum = type.maxVal > type.minVal ? type.minVal : 0;
        const int maximum = type.maxVal > type.minVal ? type.maxVal : 255;
        v = minimum + (maximum - minimum) * v / 100.0;
    }
    v = std::round(v);

    QVariant value;
    switch (valueType) {
    case QMetaType::UChar:
        if (v < 0 || v > 255)
            break;
        value = QVariant::fromValue<uchar>(uchar(v));
        break;
    case QMetaType::UShort:
        if (v < 0 || v > 65535)
            break;
        value = QVariant::fromValue<ushort>(ushort(v));
        break;
    case QMetaType::Short:
        if (v < -32768 || v > 32767)
            break;
        value = QVariant::fromValue<short>(short(v));
        break;
    case QMetaType::Int:
        value = QVariant::fromValue<int>(int(v));
        break;
    case QMetaType::UInt:
        if (v < 0)
            break;
        value = QVariant::fromValue<uint>(uint(v));
        break;
    case QMetaType::Double:
        value = QVariant::fromValue<double>(v);
        break;
    default:
        *error = tr("this command uses %1 values, which macros cannot set; use a raw command")
                     .arg(QString::fromLatin1(current.typeName()));
        return false;
    }
    if (!value.isValid()) {
        *error = tr("value is out of range");
        return false;
    }

    queue->add(priorityImmediate, queueItem(func, value, false, rx));
    return true;
}

bool MainController::sendMacroRawCommand(const QString& text, QString* error)
{
    if (!rigCaps || !queue) {
        *error = tr("no radio connected");
        return false;
    }

    QByteArray data;
    if (rigCaps->manufacturer == manufIcom) {
        // "0x20 0x42 0x21" and "20 42 21" both become 20 42 21.
        QString hex = text;
        hex.remove(QRegularExpression(QStringLiteral("\\b0[xX]")));
        hex.remove(QRegularExpression(QStringLiteral("[\\s,:]")));
        static const QRegularExpression hexOnly(QStringLiteral("^([0-9A-Fa-f]{2})+$"));
        if (!hexOnly.match(hex).hasMatch()) {
            *error = tr("raw Icom commands must be pairs of hex digits");
            return false;
        }
        data = QByteArray::fromHex(hex.toLatin1());

        // Wrap bare command bytes in a CI-V frame addressed to this radio.
        if (!data.startsWith(QByteArray("\xFE\xFE", 2))) {
            const quint16 civ = prefs && prefs->radioCIVAddr ? prefs->radioCIVAddr : rigCaps->modelID;
            QByteArray frame("\xFE\xFE", 2);
            frame.append(char(civ & 0xff));
            frame.append(char(compCivAddr));
            data.prepend(frame);
        }
        if (!data.endsWith(char(0xFD)))
            data.append(char(0xFD));
    } else {
        data = text.trimmed().toLatin1();
        if (data.isEmpty()) {
            *error = tr("empty command");
            return false;
        }
        if (!data.endsWith(';'))
            data.append(';');
    }

    qInfo(logSystem()) << "Macro sending raw command" << data.toHex(' ');
    queue->add(priorityImmediate, queueItem(data, 0));
    return true;
}

bool MainController::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_unknown)
        return QObject::eventFilter(watched, event);

    if (focusedControlAcceptsTextInput())
        return QObject::eventFilter(watched, event);

    const int shortcutKey = int(keyEvent->modifiers() & (Qt::ShiftModifier
                                                         | Qt::ControlModifier
                                                         | Qt::AltModifier
                                                         | Qt::MetaModifier))
                            | keyEvent->key();
    const QString sequence = portableShortcutText(QKeySequence(shortcutKey));
    if (sequence.isEmpty())
        return QObject::eventFilter(watched, event);

    for (const shortcutPreference& shortcut : std::as_const(prefs->shortcuts)) {
        if (!shortcut.enabled || shortcut.sequence != sequence)
            continue;
        if (shortcut.command.startsWith(QLatin1String("app."))) {
            int receiver = shortcut.receiver;
            if ((shortcut.command == QLatin1String(shortcutPopoutReceiver)
                 || shortcut.command == QLatin1String(shortcutPopinReceiver)
                 || shortcut.command == QLatin1String(shortcutEnterFrequency))
                && receiver < 0) {
                receiver = int(currentReceiver);
            }
            emit appShortcutActivated(shortcut.command, receiver);
            keyEvent->accept();
            return true;
        }

        if (shortcut.command == funcString[funcFreq]) {
            const int hz = fixedFrequencyShortcutHz(shortcut.sequence, shortcut.value);
            if (hz > 0 && (shortcut.action == 2 || shortcut.action == 3)) {
                tuneFrequencyHz(shortcut.action == 2 ? hz : -hz, shortcut.receiver);
                keyEvent->accept();
                return true;
            }
        }

        runShortcutCommand(shortcut.command, shortcut.action, shortcut.value, shortcut.receiver);
        keyEvent->accept();
        return true;
    }

    return QObject::eventFilter(watched, event);
}

void MainController::runShortcutCommand(const QString& commandName, int action, int value, int receiver)
{
    const funcs command = shortcutFuncFromName(commandName);
    if (command <= int(funcNone) || command >= int(funcLastFunc))
        return;

    COMMAND shortcut;
    shortcut.text = funcString[command];
    shortcut.cmdType = commandAny;
    shortcut.command = command;
    shortcut.suffix = 0xff;

    const int amount = qMax(1, qAbs(value));
    switch (action) {
    case 1:
        shortcut.value = value;
        break;
    case 2:
        shortcut.value = amount;
        break;
    case 3:
        shortcut.value = -amount;
        break;
    default:
        shortcut.value = value;
        break;
    }

    if (command == funcMode || command == funcModeSet || command == funcUnselectedMode)
        shortcut.mode = rigMode_t(value);

    switch (command) {
    case funcMode:
    case funcModeSet:
    case funcUnselectedMode:
    case funcFreq:
    case funcSelectedFreq:
    case funcUnselectedFreq:
    case funcCwPitch:
    case funcKeySpeed:
    case funcAfGain:
    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcMicGain:
    case funcLANModLevel:
    case funcUSBModLevel:
    case funcACCAModLevel:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        shortcut.suffix = receiver >= 0 ? quint8(receiver) : currentReceiver;
        break;
    default:
        break;
    }

    buttonControl(&shortcut);
}

void MainController::tuneFrequencyHz(qint64 hzDelta, int receiver)
{
    if (hzDelta == 0 || freqLock || receivers.isEmpty() || !queue)
        return;

    const int defaultReceiver = qBound(0, int(currentReceiver), receivers.size() - 1);
    const int rx = receiver >= 0 ? qBound(0, receiver, receivers.size() - 1) : defaultReceiver;

    const quint64 current = receivers[rx]->getFrequencyA();
    if (current == 0)
        return;

    quint64 target = 0;
    if (hzDelta < 0) {
        const quint64 decrement = quint64(-hzDelta);
        if (decrement >= current)
            return;
        target = current - decrement;
    } else {
        target = current + quint64(hzDelta);
    }

    vfoCommandType command = queue->getVfoCommand(vfoA, uchar(rx), true);
    if (command.freqFunc == funcNone)
        return;

    freqt frequency;
    frequency.Hz = target;
    frequency.MHzDouble = target / 1E6;
    receivers[rx]->setFrequencyA(target, false);

    queueItem item(command.freqFunc, QVariant::fromValue<freqt>(frequency), false, command.receiver);
    queue->addUnique(priorityImmediate, item);
}

QString MainController::platformName() const
{
    return QGuiApplication::platformName();
}

bool MainController::isOptionalMeterAvailable(int meterType) const
{
    const meter_t type = meter_t(meterType);
    if (type == meterNone || type == meterAudio || type == meterRxAudio || type == meterTxMod)
        return true;

    const funcs command = meterCommandForType(type);
    if (command == funcNone)
        return false;

    return !rigCaps || rigCaps->commands.contains(command);
}

QVariantMap MainController::optionalMeterExtremities(int meterType) const
{
    const meter_t type = meter_t(meterType);
    QVariantMap ext;
    double low = 0.0;
    double high = 255.0;
    double red = 241.0;
    bool valid = false;
    bool calibrationMissing = false;

    switch (type) {
    case meterNone:
        break;
    case meterAudio:
    case meterRxAudio:
    case meterTxMod:
        valid = true;
        break;
    case meterComp:
        low = 0.0; high = 100.0; red = 75.0;
        valid = true;
        break;
    case meterdBu:
        low = 0.0; high = 80.0; red = 80.0;
        valid = true;
        break;
    case meterdBuEMF:
        low = 0.0; high = 85.0; red = 85.0;
        valid = true;
        break;
    case meterdBm:
        low = -100.0; high = -20.0; red = -20.0;
        valid = true;
        break;
    default:
        if (rigCaps && type > meterNone && type <= meterUnknown && rigCaps->meters[type].size()) {
            low = UINT16_MAX;
            high = -double(UINT16_MAX);
            red = rigCaps->meterLines[type];
            for (auto it = rigCaps->meters[type].keyValueBegin(); it != rigCaps->meters[type].keyValueEnd(); ++it) {
                low = qMin(low, it->second);
                high = qMax(high, it->second);
            }
            valid = high > low;
        } else if (rigCaps && type > meterNone && type < meterUnknown && isOptionalMeterAvailable(int(type))) {
            calibrationMissing = true;
        }
        break;
    }

    ext["valid"] = valid;
    ext["calibrationMissing"] = calibrationMissing;
    ext["low"] = low;
    ext["high"] = high;
    ext["red"] = red;
    return ext;
}

void MainController::setOptionalMeterType(int slot, int meterType)
{
    if (!m_settings || (slot != 2 && slot != 3))
        return;

    const QString key = slot == 2 ? QStringLiteral("Interface.Meter2Type")
                                  : QStringLiteral("Interface.Meter3Type");
    m_settings->setOption(key, meterType);
    configureOptionalMeter(slot, meter_t(meterType));
    emit optionalMetersChanged();
}

funcs MainController::modSourceCommand(int dataMode) const
{
    switch (dataMode) {
    case 0: return funcDATAOffMod;
    case 1: return funcDATA1Mod;
    case 2: return funcDATA2Mod;
    case 3: return funcDATA3Mod;
    default: return funcNone;
    }
}

funcType MainController::inputLevelCommand(inputTypes input) const
{
    funcs func = funcNone;
    switch (input) {
    case inputMICACCA:
    case inputMICACCB:
    case inputMICACCAACCB:
    case inputMICUSB:
    case inputMic:
    case inputUnknown:
        func = funcMicGain;
        break;
    case inputACCAACCB:
    case inputACCA:
    case inputACCUSB:
        func = funcACCAModLevel;
        break;
    case inputACCB:
        func = funcACCBModLevel;
        break;
    case inputUSB:
        func = funcUSBModLevel;
        break;
    case inputLAN:
        func = funcLANModLevel;
        break;
    case inputSPDIF:
        func = funcSPDIFModLevel;
        break;
    default:
        break;
    }

    return (rigCaps && rigCaps->commands.contains(func)) ? rigCaps->commands.value(func) : funcType();
}

uchar MainController::currentDataMode() const
{
    if (currentReceiver < receivers.size() && receivers[currentReceiver])
        return qMin(receivers[currentReceiver]->getDataMode(), uchar(3));

    return 0;
}

QVariantList MainController::modSourceOptions(int dataMode) const
{
    QVariantList values;
    if (!rigCaps || !modSourceSupported(dataMode))
        return values;

    for (const auto& input : rigCaps->inputs) {
        if (dataMode != 0 && input.reg < 0)
            continue;

        values.append(QVariantMap{
            {"text", input.name},
            {"value", int(input.reg)},
            {"type", int(input.type)}
        });
    }

    if (values.isEmpty())
        values.append(QVariantMap{{"text", tr("None")}, {"value", -1}, {"type", int(inputUnknown)}});

    return values;
}

int MainController::modSourceReg(int dataMode) const
{
    if (!m_settings || dataMode < 0 || dataMode > 3)
        return -1;

    return m_settings->getPrefs()->inputSource[dataMode].reg;
}

bool MainController::modSourceSupported(int dataMode) const
{
    const funcs func = modSourceCommand(dataMode);
    return rigCaps && func != funcNone && rigCaps->commands.contains(func);
}

// Returns the rig input with the given reg if the radio can select it as the
// source for this data mode, otherwise nullptr. Requires rigCaps.
const rigInput* MainController::findModSourceInput(int dataMode, int reg) const
{
    if (!modSourceSupported(dataMode) || (dataMode != 0 && reg < 0))
        return nullptr;

    for (const auto& input : rigCaps->inputs) {
        if (int(input.reg) == reg)
            return &input;
    }
    return nullptr;
}

void MainController::applyConnectModSources()
{
    if (!rigCaps || !prefs || !prefs->setModSrcOnConnect)
        return;

    qInfo(logSystem()) << "Setting transmit modulation sources on connect, data off:"
                       << prefs->connectDataOffModSrc << "data on:" << prefs->connectDataOnModSrc;
    setModSource(0, prefs->connectDataOffModSrc);
    setModSource(1, prefs->connectDataOnModSrc);
}

// Queues the on-disconnect modulation sources ahead of everything else.
// Returns true if anything was queued; the caller then delays the actual
// disconnect so the commands reach the radio.
bool MainController::applyDisconnectModSources()
{
    if (!rigCaps || !prefs || !queue || !rig || !prefs->setModSrcOnDisconnect ||
        connStatus != connectionStatus_t::connConnected)
        return false;

    const int regs[2] = { prefs->disconnectDataOffModSrc, prefs->disconnectDataOnModSrc };
    bool queued = false;
    for (int dataMode = 0; dataMode < 2; ++dataMode) {
        const rigInput* input = findModSourceInput(dataMode, regs[dataMode]);
        if (input == nullptr)
            continue;
        queue->add(priorityImmediate, queueItem(modSourceCommand(dataMode), QVariant::fromValue<rigInput>(*input),
                                                false, currentReceiver));
        queued = true;
    }

    if (queued)
        qInfo(logSystem()) << "Setting transmit modulation sources on disconnect, data off:"
                           << regs[0] << "data on:" << regs[1];
    return queued;
}

// Time from a macro's first step until its last step runs, or -1 if the
// macro does not exist or has no steps.
int MainController::macroDurationMs(int macroId) const
{
    if (!prefs)
        return -1;
    for (const macroPreference& macro : std::as_const(prefs->macros)) {
        if (macro.id != macroId)
            continue;
        if (macro.steps.isEmpty())
            return -1;
        int total = 0;
        for (int i = 0; i < macro.steps.size() - 1; ++i)
            total += qMax(macro.steps.at(i).delayMs, prefs->macroSpacingMs);
        return total;
    }
    return -1;
}

void MainController::scheduleConnectMacro()
{
    m_connectMacroTimer.stop();
    if (!prefs || !prefs->runMacroOnConnect)
        return;
    if (macroDurationMs(prefs->connectMacroId) < 0) {
        qWarning(logSystem()) << "On-connect macro" << prefs->connectMacroId << "not found or empty";
        return;
    }
    m_connectMacroTimer.start(connectMacroDelayMs);
}

// Queues the on-disconnect modulation sources and starts the on-disconnect
// macro. Returns how long the caller should wait before closing the
// connection so they can complete, or 0 if nothing was started.
int MainController::startDisconnectActions()
{
    if (connStatus != connectionStatus_t::connConnected)
        return 0;

    QStringList actions;
    int waitMs = 0;
    if (applyDisconnectModSources()) {
        waitMs = disconnectCommandWaitMs;
        actions << tr("Setting transmit mod input sources");
    }

    if (prefs && prefs->runMacroOnDisconnect && rig && queue) {
        const int macroMs = macroDurationMs(prefs->disconnectMacroId);
        if (macroMs < 0) {
            qWarning(logSystem()) << "On-disconnect macro" << prefs->disconnectMacroId << "not found or empty";
        } else {
            m_connectMacroTimer.stop();
            stopMacro();
            qInfo(logSystem()) << "Running on-disconnect macro" << prefs->disconnectMacroId
                               << "waiting" << macroMs + disconnectMacroMarginMs << "ms before closing";
            runMacro(prefs->disconnectMacroId);
            waitMs = qMax(waitMs, macroMs + disconnectMacroMarginMs);
            for (const macroPreference& macro : std::as_const(prefs->macros)) {
                if (macro.id == prefs->disconnectMacroId)
                    actions << tr("Running macro \"%1\"").arg(macro.name);
            }
        }
    }

    if (waitMs > 0) {
        m_disconnectDeadline.setRemainingTime(waitMs);
        setClosingActionsText(actions.join(QLatin1Char('\n')));
    }
    return waitMs;
}

void MainController::setClosingActionsText(const QString& text)
{
    if (m_closingActionsText == text)
        return;
    m_closingActionsText = text;
    emit closingActionsTextChanged();
}

void MainController::setModSource(int dataMode, int reg)
{
    if (!m_settings)
        return;

    const rigInput* it = findModSourceInput(dataMode, reg);
    if (it == nullptr)
        return;

    const funcs sourceFunc = modSourceCommand(dataMode);

    auto* pref = m_settings->getPrefs();
    pref->inputSource[dataMode] = *it;
    m_currentModSrc[dataMode] = *it;

    queue->add(priorityImmediate, queueItem(sourceFunc, QVariant::fromValue<rigInput>(*it), false, currentReceiver));

    const auto levelCommand = inputLevelCommand(it->type);
    if (levelCommand.cmd != funcNone)
        queue->addUnique(priorityHigh, queueItem(levelCommand.cmd, true, currentReceiver));

    if (currentDataMode() == dataMode)
        updateCurrentModSource(true);

    ++m_modSourceRevision;
    emit modSourcesChanged();
}

funcs MainController::meterCommandForType(meter_t meterType) const
{
    switch (meterType) {
    case meterS:
    case meterSubS:
        return funcSMeter;
    case meterCenter:
        return funcCenterMeter;
    case meterPower:
        return funcPowerMeter;
    case meterSWR:
        return funcSWRMeter;
    case meterALC:
        return funcALCMeter;
    case meterComp:
        return funcCompMeter;
    case meterCurrent:
        return funcIdMeter;
    case meterVoltage:
        return funcVdMeter;
    case meterdBu:
    case meterdBm:
    case meterdBuEMF:
        return funcAbsoluteMeter;
    default:
        return funcNone;
    }
}

void MainController::updatePrimaryMeterForTransmit(bool transmitting)
{
    if (!rigCaps || currentReceiver >= receivers.size() || !receivers[currentReceiver])
        return;

    // The primary meter shows TX power while transmitting. SWR and other
    // TX meters belong in the additional meter slots.
    meter_t rxMeter = meterS;
    meter_t txMeter = meterPower;
    funcs rxCommand = meterCommandForType(rxMeter);
    funcs txCommand = meterCommandForType(txMeter);

    if (!rigCaps->commands.contains(txCommand)) {
        if (rigCaps->manufacturer == manufKenwood) {
            // Kenwood has no separate power meter command; the S meter
            // command returns TX power while transmitting.
            txCommand = rxCommand;
        } else {
            txMeter = rxMeter;
            txCommand = rxCommand;
        }
    }

    for (int i = 0; i < receivers.size(); ++i) {
        if (!receivers[i])
            continue;

        const uchar receiver = uchar(i);
        const bool txMeterApplies = transmitting && receiver == currentReceiver;
        const meter_t newMeter = txMeterApplies ? txMeter : rxMeter;
        const funcs newCommand = txMeterApplies ? txCommand : rxCommand;
        const funcs oldCommand = txMeterApplies ? rxCommand : txCommand;

        // Don't stop polling a meter that another meter still needs.
        if (oldCommand != funcNone && oldCommand != newCommand && !meterRequested(oldCommand, receiver))
            queue->del(oldCommand, receiver);

        receivers[i]->setPrimaryMeterType(newMeter);

        if (newCommand != funcNone && rigCaps->commands.contains(newCommand))
            queue->addUnique(priorityHighest, queueItem(newCommand, true, receiver));
    }
}

void MainController::configureOptionalMeter(int slot, meter_t meterType)
{
    if (slot != 2 && slot != 3)
        return;

    if (slot == 2)
        m_optionalMeter2Level = 0.0;
    else
        m_optionalMeter3Level = 0.0;

    applyMeterRequest(slot == 2 ? QStringLiteral("slot2") : QStringLiteral("slot3"), meterType);
}

funcs MainController::meterPollCommand(meter_t meterType) const
{
    // S+Power (meterS) is the primary meter, which is always polled.
    if (meterType == meterS)
        return funcNone;
    return meterCommandForType(meterType);
}

uchar MainController::meterPollReceiver(meter_t meterType)
{
    return (meterType == meterSubS) ? uchar(1) : uchar(0);
}

bool MainController::meterRequested(funcs command, uchar receiver) const
{
    for (auto it = m_meterRequests.cbegin(); it != m_meterRequests.cend(); ++it) {
        if (meterPollCommand(it.value()) == command && meterPollReceiver(it.value()) == receiver)
            return true;
    }
    return false;
}

bool MainController::meterTypeRequested(meter_t meterType) const
{
    for (auto it = m_meterRequests.cbegin(); it != m_meterRequests.cend(); ++it) {
        const meter_t type = it.value();
        if (type == meterType)
            return true;
        if (type == meterAudio && (meterType == meterTxMod || meterType == meterRxAudio))
            return true;
    }
    return false;
}

bool MainController::primaryMeterUses(funcs command, uchar receiver) const
{
    if (!rigCaps)
        return false;
    // Mirrors updatePrimaryMeterForTransmit(): S on receive, Po (or S on
    // radios without a power meter command) on the transmitting receiver.
    const funcs rxCommand = funcSMeter;
    const funcs txCommand = rigCaps->commands.contains(funcPowerMeter) ? funcPowerMeter : funcSMeter;
    const bool txApplies = m_transmitting && receiver == currentReceiver;
    return command == (txApplies ? txCommand : rxCommand);
}

void MainController::applyMeterRequest(const QString& consumer, meter_t meterType)
{
    const meter_t oldType = m_meterRequests.value(consumer, meterNone);
    if (meterType == meterNone)
        m_meterRequests.remove(consumer);
    else
        m_meterRequests.insert(consumer, meterType);

    const funcs oldCommand = meterPollCommand(oldType);
    const uchar oldReceiver = meterPollReceiver(oldType);
    if (queue && oldCommand != funcNone && !meterRequested(oldCommand, oldReceiver)
        && !primaryMeterUses(oldCommand, oldReceiver)) {
        queue->del(oldCommand, oldReceiver);
    }

    const funcs newCommand = meterPollCommand(meterType);
    if (queue && newCommand != funcNone && rigCaps && rigCaps->commands.contains(newCommand))
        queue->addUnique(priorityHighest, queueItem(newCommand, true, meterPollReceiver(meterType)));
}

void MainController::refreshMeterRequests()
{
    if (!queue || !rigCaps)
        return;
    for (auto it = m_meterRequests.cbegin(); it != m_meterRequests.cend(); ++it) {
        const funcs command = meterPollCommand(it.value());
        if (command != funcNone && rigCaps->commands.contains(command))
            queue->addUnique(priorityHighest, queueItem(command, true, meterPollReceiver(it.value())));
    }
}

void MainController::requestMeter(const QString& consumer, int meterType)
{
    if (consumer.isEmpty() || consumer == QLatin1String("slot2") || consumer == QLatin1String("slot3"))
        return;
    applyMeterRequest(consumer, meter_t(meterType));
}

double MainController::meterLevel(int meterType) const
{
    if (meter_t(meterType) == meterAudio)
        return m_meterLevels.value(m_transmitting ? int(meterTxMod) : int(meterRxAudio), 0.0);
    return m_meterLevels.value(meterType, 0.0);
}

void MainController::receiveOptionalMeter(meter_t meterType, double level)
{
    m_meterLevels.insert(int(meterType), level);
    if (meterTypeRequested(meterType)) {
        ++m_meterLevelsRevision;
        emit meterLevelsChanged();
    }

    bool changed = false;
    if (prefs && (prefs->meter2Type == meterType ||
                  (prefs->meter2Type == meterAudio && ((meterType == meterTxMod && m_transmitting) || (meterType == meterRxAudio && !m_transmitting))))) {
        m_optionalMeter2Level = level;
        changed = true;
    }
    if (prefs && (prefs->meter3Type == meterType ||
                  (prefs->meter3Type == meterAudio && ((meterType == meterTxMod && m_transmitting) || (meterType == meterRxAudio && !m_transmitting))))) {
        m_optionalMeter3Level = level;
        changed = true;
    }
    if (changed)
        emit optionalMetersChanged();
}

void MainController::colChanged(prefColItems items)
{
    qDebug(logGui()) << "Color preferences changed" << items;

    // No need to step through each color, just update the preset.

    //We need to change the main palette color:
    /*
    colorPrefsType colors = m_settings->getCurrentColorPreset();

    qInfo() << "Received changed colors into MainController" << items;
    for (auto *r : std::as_const(receivers)) {
        if (r) r->setColors(colors);
    }
    */

    updateApplicationPalette();

    /*
    palette.setColor(QPalette::Window, colors.window);
    palette.setColor(QPalette::WindowText, colors.windowText);
    palette.setColor(QPalette::Base, colors.base);
    palette.setColor(QPalette::AlternateBase, colors.alternateBase);
    palette.setColor(QPalette::Text, colors.mainText);
    palette.setColor(QPalette::Button, colors.button);
    palette.setColor(QPalette::ButtonText, colors.buttonText);
    palette.setColor(QPalette::BrightText, colors.brightText);
    palette.setColor(QPalette::Light, colors.light);
    palette.setColor(QPalette::Midlight, colors.midLight);
    palette.setColor(QPalette::Dark, colors.dark);
    palette.setColor(QPalette::Mid, colors.mid);
    palette.setColor(QPalette::Shadow, colors.shadow);
    palette.setColor(QPalette::Highlight, colors.highlight);
    palette.setColor(QPalette::HighlightedText, colors.highlightedText);
    palette.setColor(QPalette::Link, colors.link);
    palette.setColor(QPalette::LinkVisited, colors.linkVisited);
    palette.setColor(QPalette::ToolTipBase, colors.toolTipBase);
    palette.setColor(QPalette::ToolTipText, colors.toolTipText);
    palette.setColor(QPalette::PlaceholderText, colors.placeholder);
    QGuiApplication::setPalette(palette);
    */

}

void MainController::buildUiSpecs()
{
    // Must only be called if we already have a valid rigCaps

    QVariantList values;

    values.clear();
    for (auto &sm : rigCaps->steps) {
        values.append(QVariantMap{
            {"text",  sm.name},
            {"value", sm.hz}
        });
    }
    uiSpecs["tuningSteps"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    auto rangeSpec = [this](funcs f, int fallbackMin, int fallbackMax, bool requiresTransmit = false) {
        QVariantMap spec;
        spec["available"] = rigCaps->commands.contains(f) && (!requiresTransmit || rigCaps->hasTransmit);
        spec["min"] = fallbackMin;
        spec["max"] = fallbackMax;
        if (spec["available"].toBool()) {
            const auto cmd = rigCaps->commands.find(f).value();
            spec["min"] = cmd.minVal;
            spec["max"] = cmd.maxVal;
        }
        return spec;
    };

    uiSpecs["mainControls"] = QVariantMap{
        {"canTransmit", rigCaps->hasTransmit && rigCaps->commands.contains(funcTransceiverStatus)},
        {"canTune", rigCaps->commands.contains(funcTunerStatus)},
        {"canRit", rigCaps->commands.contains(funcRitStatus) || rigCaps->commands.contains(funcRitFreq)},
        {"canDualScope", rigCaps->numReceiver > 1},
        {"canDualWatch", rigCaps->commands.contains(funcVFODualWatch)},
        {"canSplit", rigCaps->hasTransmit && rigCaps->commands.contains(funcSplitStatus)},
        {"canMainSub", rigCaps->commands.contains(funcVFOBandMS)},
        {"canSwapMainSub", rigCaps->commands.contains(funcVFOSwapMS)},
        {"canEqualMainSub", rigCaps->commands.contains(funcVFOEqualMS)},
        {"canCompressor", rigCaps->hasTransmit && rigCaps->commands.contains(funcCompressor)},
        {"canVox", rigCaps->hasTransmit && rigCaps->commands.contains(funcVox)},
        {"canSendCW", rigCaps->hasTransmit && rigCaps->commands.contains(funcSendCW)},
        {"canMemories", rigCaps->commands.contains(funcMemoryContents)},
        {"canRepeater", rigCaps->commands.contains(funcSplitStatus)
                        || rigCaps->commands.contains(funcToneSquelchType)
                        || rigCaps->commands.contains(funcRepeaterTone)
                        || rigCaps->commands.contains(funcRepeaterTSQL)
                        || rigCaps->commands.contains(funcRepeaterDTCS)
                        || rigCaps->commands.contains(funcToneFreq)
                        || rigCaps->commands.contains(funcTSQLFreq)
                        || rigCaps->commands.contains(funcDTCSCode)
                        || rigCaps->commands.contains(funcReadFreqOffset)
                        || rigCaps->commands.contains(funcSendFreqOffset)},
        {"canMonitor", rigCaps->hasTransmit && rigCaps->commands.contains(funcMonitor)},
        {"txPower", rangeSpec(funcRFPower, 0, 255, true)},
        {"monitorGain", rangeSpec(funcMonitorGain, 0, 255, true)},
        {"micGain", rangeSpec(funcMicGain, 0, 255, true)},
        {"ritFrequency", rangeSpec(funcRitFreq, -500, 500)}
    };
    emit uiSpecsChanged();
}

void MainController::powerOn()
{
    emit sendPowerOn();
}

void MainController::powerOff()
{
    emit sendPowerOff();
}

void MainController::toggleTransmit()
{
    setTransmit(!m_transmitting);
}

void MainController::setTransmit(bool enabled)
{
    if (!prefs->enablePTT) {
        qInfo(logSystem()) << "PTT is disabled, not sending transmit command.";
        return;
    }

    if (enabled) {
        const TxAudioInput source = txSourceForPttOrigin(PttOrigin::Wfview, currentAudioRoute());
        if (source != TxAudioInput::None && !requestTxAudioOwnership(PttOrigin::Wfview, source)) {
            qDebug(logAudio()) << "Ignoring wfview transmit request because another source owns TX audio";
            return;
        }
        if (source == TxAudioInput::LocalInput)
            setTxAudioSource(TxAudioInput::LocalInput);
    } else {
        if (releaseTxAudioOwnership(PttOrigin::Wfview) &&
            currentAudioRoute().rxSource == RadioRxAudioSource::WfShare)
            setTxAudioSource(TxAudioInput::None);
    }

    if (rigCaps && rigCaps->commands.contains(funcTransceiverStatus)) {
        queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(enabled), false, uchar(0)));
        if (enabled)
            armPttTimeout();
        else
            m_pttTimeoutTimer.stop();
    }
}

void MainController::armPttTimeout()
{
    // Repeated PTT-on requests (e.g. a held controller button) must not extend the timeout.
    if (!prefs || !prefs->pttTimeoutEnabled || m_pttTimeoutTimer.isActive())
        return;
    m_pttTimeoutTimer.start(qBound(30, prefs->pttTimeoutSeconds, 999) * 1000);
}

void MainController::handlePttTimeout()
{
    const int seconds = prefs ? prefs->pttTimeoutSeconds : 0;
    qWarning(logSystem()) << "PTT Timeout: transmission exceeded" << seconds << "seconds, sending PTT off.";

    if (rigCaps && rigCaps->commands.contains(funcTransceiverStatus))
        queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(false), false, uchar(0)));

    handleRigCtlTransmitRequested(false);
    if (releaseTxAudioOwnership(PttOrigin::Wfview) &&
        currentAudioRoute().rxSource == RadioRxAudioSource::WfShare)
        setTxAudioSource(TxAudioInput::None);

    emit pttTimeoutExpired(seconds);
}

void MainController::tuneNow()
{
    if (!prefs->enablePTT) {
        qInfo(logSystem()) << "PTT is disabled, not sending tuner command.";
        return;
    }

    if (rigCaps && rigCaps->commands.contains(funcTunerStatus)) {
        queue->addUnique(priorityImmediate, queueItem(funcTunerStatus, QVariant::fromValue<uchar>(2U)));
    }
}

void MainController::setTunerEnabled(bool enabled)
{
    if (m_tunerEnabled != enabled) {
        m_tunerEnabled = enabled;
        emit tunerEnabledChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcTunerStatus)) {
        queue->addUnique(priorityImmediate, queueItem(funcTunerStatus, QVariant::fromValue<uchar>(enabled ? 1U : 0U)));
    }
}

void MainController::setFrequencyLock(bool locked)
{
    if (freqLock == locked)
        return;

    freqLock = locked;
    for (const auto &rx : std::as_const(receivers)) {
        rx->setFreqLock(locked);
    }
}

void MainController::setRitFrequency(int value)
{
    if (rigCaps && rigCaps->commands.contains(funcRitFreq)) {
        const auto cmd = rigCaps->commands.find(funcRitFreq).value();
        value = qBound(cmd.minVal, value, cmd.maxVal);
    }

    m_ritUserChange.start();

    if (m_ritFrequency != value) {
        m_ritFrequency = value;
        emit ritFrequencyChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcRitFreq)) {
        queue->addUnique(priorityImmediate, queueItem(funcRitFreq, QVariant::fromValue(short(value))));
    }
}

void MainController::setRitEnabled(bool enabled)
{
    if (m_ritEnabled != enabled) {
        m_ritEnabled = enabled;
        emit ritEnabledChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcRitStatus)) {
        queue->add(priorityImmediate, queueItem(funcRitStatus, QVariant::fromValue(enabled)));
    }
}

void MainController::setTxPower(int value)
{
    if (rigCaps && rigCaps->commands.contains(funcRFPower)) {
        const auto cmd = rigCaps->commands.find(funcRFPower).value();
        value = qBound(cmd.minVal, value, cmd.maxVal);
    }

    if (m_txPower != value) {
        m_txPower = value;
        emit txPowerChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcRFPower)) {
        queue->addUnique(priorityImmediate, queueItem(funcRFPower, QVariant::fromValue<ushort>(ushort(value)), false));
    }
}

bool MainController::localAudioAvailable() const
{
    return currentAudioRoute().localOutputEnabled;
}

void MainController::setLocalAfGain(int value)
{
    const quint8 gain = static_cast<quint8>(qBound(0, value, 255));

    if (prefs && (prefs->localAFgain != gain || prefs->rxSetup.localAFgain != gain)) {
        prefs->localAFgain = gain;
        prefs->rxSetup.localAFgain = gain;
        emit localAfGainChanged();
        if (m_settings) {
            m_settings->saveLocalAFGain(gain);
        }
    }

    if (usbRxOutputAudio) {
        QMetaObject::invokeMethod(usbRxOutputAudio, "setVolume",
                                  Qt::QueuedConnection, Q_ARG(quint8, gain));
    }

    emit sendLocalAudioVolume(gain);
}

void MainController::setMonitorGain(int value)
{
    if (rigCaps && rigCaps->commands.contains(funcMonitorGain)) {
        const auto cmd = rigCaps->commands.find(funcMonitorGain).value();
        value = qBound(cmd.minVal, value, cmd.maxVal);
    }

    if (m_monitorGain != value) {
        m_monitorGain = value;
        emit monitorGainChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcMonitorGain)) {
        queue->addUnique(priorityImmediate, queueItem(funcMonitorGain, QVariant::fromValue<ushort>(ushort(value)), false, currentReceiver));
    }
}

void MainController::setMonitorEnabled(bool enabled)
{
    if (m_monitorEnabled != enabled) {
        m_monitorEnabled = enabled;
        emit monitorEnabledChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcMonitor)) {
        queue->add(priorityImmediate, queueItem(funcMonitor, QVariant::fromValue<bool>(enabled), false, currentReceiver));
    }
}

void MainController::setMicGain(int value)
{
    const auto source = m_settings ? m_settings->getPrefs()->inputSource[currentDataMode()] : rigInput(inputMic);
    const auto cmd = inputLevelCommand(source.type);
    value = qBound(cmd.minVal, value, cmd.maxVal);

    if (m_micGain != value) {
        m_micGain = value;
        emit micGainChanged();
    }

    if (cmd.cmd != funcNone) {
        queue->addUnique(priorityImmediate, queueItem(cmd.cmd, QVariant::fromValue<ushort>(ushort(value)), false, currentReceiver));
    }
}

void MainController::setDualScope(bool enabled)
{
    if (m_dualScope != enabled) {
        m_dualScope = enabled;
        emit dualScopeChanged();
    }

    if (!rigCaps || !rigCaps->commands.contains(funcScopeSingleDual))
        return;

    queue->add(priorityImmediate, queueItem(funcScopeSingleDual, QVariant::fromValue(enabled), false, uchar(0)));
    queue->add(priorityImmediate, funcScopeMainSub, false, uchar(0));
}

void MainController::setDualWatch(bool enabled)
{
    if (m_dualWatch != enabled) {
        m_dualWatch = enabled;
        emit dualWatchChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcVFODualWatch)) {
        queue->add(priorityImmediate, queueItem(funcVFODualWatch, QVariant::fromValue(enabled), false, uchar(0)));
        if (!rigCaps->hasCommand29)
            queue->add(priorityImmediate, funcScopeMainSub, false, uchar(0));
    }
}

void MainController::setSplitEnabled(bool enabled)
{
    if (m_splitEnabled != enabled) {
        m_splitEnabled = enabled;
        emit splitEnabledChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcSplitStatus)) {
        queue->add(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue(enabled), false));
    }
}

QVariantMap MainController::repeaterState() const
{
    QVariantMap caps;
    caps["duplex"] = rigCaps && rigCaps->commands.contains(funcSplitStatus);
    caps["repeaterDuplex"] = rigCaps && rigCaps->commands.contains(funcToneSquelchType);
    caps["toneMode"] = rigCaps && (rigCaps->commands.contains(funcToneSquelchType)
                                   || rigCaps->commands.contains(funcRepeaterTone)
                                   || rigCaps->commands.contains(funcRepeaterTSQL)
                                   || rigCaps->commands.contains(funcRepeaterDTCS));
    caps["tone"] = rigCaps && (rigCaps->commands.contains(funcToneFreq)
                               || rigCaps->commands.contains(funcRepeaterTone)
                               || rigCaps->commands.contains(funcToneSquelchType));
    caps["tsql"] = rigCaps && (rigCaps->commands.contains(funcTSQLFreq)
                               || rigCaps->commands.contains(funcRepeaterTSQL)
                               || rigCaps->commands.contains(funcToneSquelchType));
    caps["dtcs"] = rigCaps && (rigCaps->commands.contains(funcDTCSCode)
                               || rigCaps->commands.contains(funcRepeaterDTCS));
    caps["radioOffset"] = rigCaps && (rigCaps->commands.contains(funcReadFreqOffset)
                                      || rigCaps->commands.contains(funcSendFreqOffset));
    caps["quickSplit"] = rigCaps && rigCaps->commands.contains(funcQuickSplit);
    caps["secondaryVfo"] = rigCaps && rigCaps->commands.contains(funcVFOMainSelect);

    QVariantMap state;
    state["available"] = rigCaps != nullptr && caps.values().contains(true);
    state["capabilities"] = caps;
    state["duplexMode"] = int(m_repeaterDuplexMode);
    state["toneMode"] = int(m_repeaterToneMode);
    state["tone"] = m_repeaterTone;
    state["tsql"] = m_repeaterTsql;
    state["dtcs"] = m_repeaterDtcs;
    state["dtcsTxInvert"] = m_repeaterDtcsTxInvert;
    state["dtcsRxInvert"] = m_repeaterDtcsRxInvert;
    state["offsetMHz"] = QString::number(m_repeaterOffsetHz / double(1000000), 'f', 6);
    state["mainFrequencyMHz"] = m_repeaterHaveMainFrequency
                                    ? QString::number(m_repeaterMainFrequency.Hz / double(1000000), 'f', 6)
                                    : QString();
    state["quickSplit"] = m_repeaterQuickSplit;
    state["warnNotFm"] = m_repeaterWarnNotFm;

    state["dmSplitOff"] = int(dmSplitOff);
    state["dmSplitOn"] = int(dmSplitOn);
    state["dmSimplex"] = int(dmSimplex);
    state["dmDupMinus"] = int(dmDupMinus);
    state["dmDupPlus"] = int(dmDupPlus);
    state["dmDupAutoOn"] = int(dmDupAutoOn);
    state["ratrNN"] = int(ratrNN);
    state["ratrTN"] = int(ratrTN);
    state["ratrTT"] = int(ratrTT);
    state["ratrDD"] = int(ratrDD);
    return state;
}

QVariantList MainController::repeaterToneOptions() const
{
    QVariantList values;
    if (!rigCaps)
        return values;

    for (const auto& tone : rigCaps->ctcss) {
        values.append(QVariantMap{
            {"text", tone.name},
            {"value", int(tone.tone)}
        });
    }
    return values;
}

QVariantList MainController::repeaterDtcsOptions() const
{
    QVariantList values;
    if (!rigCaps)
        return values;

    for (const auto& tone : rigCaps->dtcs) {
        values.append(QVariantMap{
            {"text", tone.name},
            {"value", int(tone.tone)}
        });
    }
    return values;
}

void MainController::refreshRepeaterState()
{
    if (!rigCaps || !queue)
        return;

    if (rigCaps->commands.contains(funcSplitStatus))
        queue->add(priorityImmediate, funcSplitStatus, false);
    if (rigCaps->commands.contains(funcToneSquelchType))
        queue->add(priorityImmediate, funcToneSquelchType, false);
    else {
        if (rigCaps->commands.contains(funcRepeaterTone))
            queue->add(priorityImmediate, funcRepeaterTone, false);
        if (rigCaps->commands.contains(funcRepeaterTSQL))
            queue->add(priorityImmediate, funcRepeaterTSQL, false);
    }
    if (rigCaps->commands.contains(funcToneFreq))
        queue->add(priorityImmediate, funcToneFreq, false);
    if (rigCaps->commands.contains(funcTSQLFreq))
        queue->add(priorityImmediate, funcTSQLFreq, false);
    if (rigCaps->commands.contains(funcDTCSCode))
        queue->add(priorityImmediate, funcDTCSCode, false);
    if (rigCaps->commands.contains(funcReadFreqOffset))
        queue->add(priorityImmediate, funcReadFreqOffset, false);
    if (rigCaps->commands.contains(funcQuickSplit))
        queue->add(priorityImmediate, funcQuickSplit, false);
}

void MainController::setRepeaterDuplex(int mode)
{
    if (!rigCaps || !rigCaps->commands.contains(funcSplitStatus))
        return;

    const auto dm = duplexMode_t(mode);
    m_repeaterDuplexMode = dm;
    const bool split = dm != dmSplitOff;
    if (m_splitEnabled != split) {
        m_splitEnabled = split;
        emit splitEnabledChanged();
    }
    emit repeaterStateChanged();
    queue->add(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue(dm), false));
}

void MainController::setRepeaterAccessMode(rptAccessTxRx_t mode, bool secondaryVfo)
{
    if (!rigCaps)
        return;

    if (rigCaps->commands.contains(funcToneSquelchType)) {
        rptrAccessData data;
        data.accessMode = mode;
        data.useSecondaryVFO = secondaryVfo;
        queue->add(priorityImmediate, queueItem(funcToneSquelchType, QVariant::fromValue(data), false));
        return;
    }

    if (mode == ratrTN && rigCaps->commands.contains(funcRepeaterTone)) {
        queue->add(priorityImmediate, queueItem(funcRepeaterTone, QVariant::fromValue(true), false, uchar(secondaryVfo)));
    } else if (mode == ratrTT && rigCaps->commands.contains(funcRepeaterTSQL)) {
        queue->add(priorityImmediate, queueItem(funcRepeaterTSQL, QVariant::fromValue(true), false, uchar(secondaryVfo)));
    } else if (mode == ratrNN) {
        if (rigCaps->commands.contains(funcRepeaterTone))
            queue->add(priorityImmediate, queueItem(funcRepeaterTone, QVariant::fromValue(false), false, uchar(secondaryVfo)));
        if (rigCaps->commands.contains(funcRepeaterTSQL))
            queue->add(priorityImmediate, queueItem(funcRepeaterTSQL, QVariant::fromValue(false), false, uchar(secondaryVfo)));
    }
}

void MainController::setRepeaterToneMode(int mode, bool secondaryVfo)
{
    const auto accessMode = rptAccessTxRx_t(mode);
    updateRepeaterToneMode(accessMode);
    setRepeaterAccessMode(accessMode, secondaryVfo);
}

toneInfo MainController::toneFromList(const std::vector<toneInfo>& tones, int tone, bool secondaryVfo) const
{
    for (auto item : tones) {
        if (int(item.tone) == tone) {
            item.useSecondaryVFO = secondaryVfo;
            return item;
        }
    }

    toneInfo fallback{short(tone)};
    fallback.useSecondaryVFO = secondaryVfo;
    return fallback;
}

void MainController::setRepeaterToneFrequency(int tone, bool secondaryVfo)
{
    if (!rigCaps || !rigCaps->commands.contains(funcToneFreq))
        return;

    m_repeaterTone = tone;
    emit repeaterStateChanged();
    queue->add(priorityImmediate, queueItem(funcToneFreq, QVariant::fromValue(toneFromList(rigCaps->ctcss, tone, secondaryVfo)), false, uchar(secondaryVfo)));
}

void MainController::setRepeaterTsqlFrequency(int tone, bool secondaryVfo)
{
    if (!rigCaps || !rigCaps->commands.contains(funcTSQLFreq))
        return;

    m_repeaterTsql = tone;
    emit repeaterStateChanged();
    queue->add(priorityImmediate, queueItem(funcTSQLFreq, QVariant::fromValue(toneFromList(rigCaps->ctcss, tone, secondaryVfo)), false, uchar(secondaryVfo)));
}

void MainController::setRepeaterDtcsCode(int tone, bool txInvert, bool rxInvert)
{
    if (!rigCaps || !rigCaps->commands.contains(funcDTCSCode))
        return;

    toneInfo code = toneFromList(rigCaps->dtcs, tone);
    code.tinv = txInvert;
    code.rinv = rxInvert;
    m_repeaterDtcs = tone;
    m_repeaterDtcsTxInvert = txInvert;
    m_repeaterDtcsRxInvert = rxInvert;
    emit repeaterStateChanged();
    queue->add(priorityImmediate, queueItem(funcDTCSCode, QVariant::fromValue(code), false));
}

quint64 MainController::parseRepeaterFrequencyHz(const QString& text, double multiplier, const QString& fieldName) const
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || value <= 0.0) {
        qWarning(logRptr()) << "Invalid repeater" << fieldName << text;
        return 0;
    }

    return quint64(std::llround(value * multiplier));
}

void MainController::setRepeaterOffsetMHz(const QString& mhz)
{
    if (!rigCaps || !rigCaps->commands.contains(funcSendFreqOffset))
        return;

    freqt offset;
    offset.Hz = parseRepeaterFrequencyHz(mhz, 1000000.0, QStringLiteral("offset"));
    if (offset.Hz == 0) {
        setFooterMessageText(tr("Invalid repeater offset"));
        return;
    }

    offset.MHzDouble = offset.Hz / double(1000000);
    offset.VFO = activeVFO;
    updateRepeaterOffset(offset);
    queue->add(priorityImmediate, queueItem(funcSendFreqOffset, QVariant::fromValue(offset), false));
}

void MainController::setRepeaterSplitFromOffsetKHz(const QString& khz, bool plus, bool autoEnableSplit)
{
    if (!rigCaps || !queue)
        return;
    if (!m_repeaterHaveMainFrequency || m_repeaterMainFrequency.Hz == 0) {
        setFooterMessageText(tr("Cannot set split before a receive frequency is available"));
        return;
    }

    const quint64 offsetHz = parseRepeaterFrequencyHz(khz, 1000.0, QStringLiteral("split offset"));
    if (offsetHz == 0) {
        setFooterMessageText(tr("Invalid split offset"));
        return;
    }

    freqt tx;
    tx.Hz = plus ? m_repeaterMainFrequency.Hz + offsetHz : m_repeaterMainFrequency.Hz - offsetHz;
    tx.MHzDouble = tx.Hz / double(1000000);
    tx.VFO = inactiveVFO;
    m_repeaterLastSplitPlus = plus;

    if (autoEnableSplit)
        setRepeaterDuplex(int(dmSplitOn));

    const uchar targetReceiver = uchar(rigCaps->numReceiver > 1);
    const vfo_t txVfo = rigCaps->numVFO > 1 ? vfoB : vfoSub;
    const auto cmd = queue->getVfoCommand(txVfo, targetReceiver, true);
    if (cmd.freqFunc != funcNone)
        queue->add(priorityImmediate, queueItem(cmd.freqFunc, QVariant::fromValue(tx), false, cmd.receiver));
}

void MainController::setRepeaterSplitTxFrequencyMHz(const QString& mhz, bool autoEnableSplit)
{
    if (!rigCaps || !queue)
        return;

    freqt tx;
    tx.Hz = parseRepeaterFrequencyHz(mhz, 1000000.0, QStringLiteral("split transmit frequency"));
    if (tx.Hz == 0) {
        setFooterMessageText(tr("Invalid split transmit frequency"));
        return;
    }

    tx.MHzDouble = tx.Hz / double(1000000);
    tx.VFO = inactiveVFO;

    if (autoEnableSplit)
        setRepeaterDuplex(int(dmSplitOn));

    const uchar targetReceiver = uchar(rigCaps->numReceiver > 1);
    const vfo_t txVfo = rigCaps->numVFO > 1 ? vfoB : vfoSub;
    const auto cmd = queue->getVfoCommand(txVfo, targetReceiver, true);
    if (cmd.freqFunc != funcNone)
        queue->add(priorityImmediate, queueItem(cmd.freqFunc, QVariant::fromValue(tx), false, cmd.receiver));
}

void MainController::setRepeaterQuickSplit(bool enabled)
{
    if (!rigCaps || !rigCaps->commands.contains(funcQuickSplit))
        return;

    m_repeaterQuickSplit = enabled;
    emit repeaterStateChanged();
    queue->add(priorityImmediate, queueItem(funcQuickSplit, QVariant::fromValue(enabled), false));
}

void MainController::updateRepeaterToneMode(rptAccessTxRx_t mode)
{
    if (mode == ratrTONEon)
        mode = ratrTN;
    else if (mode == ratrTSQLon)
        mode = ratrTT;
    else if (mode == ratrTONEoff || mode == ratrTSQLoff)
        mode = ratrNN;

    if (m_repeaterToneMode != mode) {
        m_repeaterToneMode = mode;
        emit repeaterStateChanged();
    }
}

void MainController::updateRepeaterOffset(freqt offset)
{
    if (m_repeaterOffsetHz != offset.Hz) {
        m_repeaterOffsetHz = offset.Hz;
        emit repeaterStateChanged();
    }
}

void MainController::selectMainSub()
{
    if (rigCaps && rigCaps->commands.contains(funcVFOBandMS)) {
        queue->add(priorityImmediate, queueItem(funcVFOBandMS, QVariant::fromValue<uchar>(currentReceiver == 0), false, uchar(0)));
    }
}

void MainController::swapMainSub()
{
    if (!rigCaps || !rigCaps->commands.contains(funcVFOSwapMS))
        return;

    queue->add(priorityImmediate, funcVFOSwapMS);
    for (const auto &receiver : std::as_const(receivers)) {
        queue->add(priorityImmediate, queue->getVfoCommand(vfoA, receiver->getReceiver(), true).freqFunc, false, receiver->getReceiver());
        queue->add(priorityImmediate, queue->getVfoCommand(vfoA, receiver->getReceiver(), true).modeFunc, false, receiver->getReceiver());
        queue->add(priorityImmediate, queue->getVfoCommand(vfoB, receiver->getReceiver(), true).freqFunc, false, receiver->getReceiver());
        queue->add(priorityImmediate, queue->getVfoCommand(vfoB, receiver->getReceiver(), true).modeFunc, false, receiver->getReceiver());
    }
}

void MainController::equalizeMainSub()
{
    if (!rigCaps || !rigCaps->commands.contains(funcVFOEqualMS))
        return;

    queue->add(priorityImmediate, funcVFOEqualMS);
    for (const auto &receiver : std::as_const(receivers)) {
        queue->add(priorityImmediate, queue->getVfoCommand(vfoA, receiver->getReceiver(), true).modeFunc, false, receiver->getReceiver());
        queue->add(priorityImmediate, queue->getVfoCommand(vfoA, receiver->getReceiver(), true).freqFunc, false, receiver->getReceiver());
        queue->add(priorityImmediate, queue->getVfoCommand(vfoB, receiver->getReceiver(), true).modeFunc, false, receiver->getReceiver());
        queue->add(priorityImmediate, queue->getVfoCommand(vfoB, receiver->getReceiver(), true).freqFunc, false, receiver->getReceiver());
    }
}

void MainController::setCompressorEnabled(bool enabled)
{
    if (m_compressorEnabled != enabled) {
        m_compressorEnabled = enabled;
        emit compressorEnabledChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcCompressor)) {
        queue->addUnique(priorityImmediate, queueItem(funcCompressor, QVariant::fromValue<bool>(enabled), false, currentReceiver));
    }
}

void MainController::setVoxEnabled(bool enabled)
{
    if (m_voxEnabled != enabled) {
        m_voxEnabled = enabled;
        emit voxEnabledChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcVox)) {
        queue->addUnique(priorityImmediate, queueItem(funcVox, QVariant::fromValue<bool>(enabled), false, currentReceiver));
    }
}

AudioRouteState MainController::currentAudioRoute() const
{
    if (!prefs)
        return AudioRouteState();

    AudioRouteInputs inputs;
    inputs.connected = connStatus == connectionStatus_t::connConnected;
    inputs.rigCanTransmit = rigCaps && rigCaps->hasTransmit;
    inputs.enableNetwork = prefs->enableLAN;
    inputs.enableWfShare = prefs->wfShareEnabled;
    inputs.wfShareDirectMode = prefs->wfShareDirectMode;
    inputs.enableUsbAudio = prefs->enableUsbAudio;
    inputs.hasLocalRxOutput = audioSetupHasDevice(prefs->rxSetup);
    inputs.hasLocalTxInput = audioSetupHasDevice(prefs->txSetup);
    inputs.hasUsbRadioRxInput = audioSetupHasDevice(prefs->usbRxSetup);
    inputs.hasUsbRadioTxOutput = audioSetupHasDevice(prefs->usbTxSetup);
    inputs.hasTciServer = tci != nullptr;
    inputs.audioSystem = prefs->audioSystem;
    return computeAudioRoute(inputs);
}

void MainController::updateAudioRouteState()
{
    const AudioRouteState route = currentAudioRoute();
    if (route == m_audioRoute)
        return;

    m_audioRoute = route;
    qDebug(logAudio()) << "Audio route changed:" << audioRouteDebugString(m_audioRoute);
    if (m_cwSender)
        m_cwSender->setSidetoneAvailable(m_audioRoute.localOutputEnabled);
    emit localAudioAvailableChanged();
}

bool MainController::requestTxAudioOwnership(PttOrigin origin, TxAudioInput source)
{
    qDebug(logAudio()) << "TX audio ownership request origin"
                       << pttOriginName(origin) << "source" << txAudioInputName(source)
                       << "activeOrigin" << pttOriginName(m_txAudioArbiter.activeOrigin())
                       << "activeSource" << txAudioInputName(m_txAudioArbiter.activeSource());

    const bool accepted = m_txAudioArbiter.request(origin, source);
    qDebug(logAudio()) << "TX audio ownership" << (accepted ? "granted" : "rejected")
                       << "origin" << pttOriginName(origin)
                       << "source" << txAudioInputName(source);
    if (accepted) {
        clearTxAudioInterlockError();
    } else {
        setTxAudioInterlockError(origin, source);
    }
    if (accepted && m_selRad)
        m_selRad->setTxAudioState(pttOriginName(origin), txAudioInputName(source));
    return accepted;
}

bool MainController::releaseTxAudioOwnership(PttOrigin origin)
{
    const TxAudioInput activeSource = m_txAudioArbiter.activeSource();
    const bool released = m_txAudioArbiter.release(origin);
    qDebug(logAudio()) << "TX audio ownership release"
                       << (released ? "accepted" : "ignored")
                       << "origin" << pttOriginName(origin)
                       << "source" << txAudioInputName(activeSource);
    if (released) {
        clearTxAudioInterlockError();
        if (m_selRad)
            m_selRad->setTxAudioState("none", "none");
    }
    return released;
}

void MainController::resetTxAudioOwnership()
{
    if (m_txAudioArbiter.isActive()) {
        qDebug(logAudio()) << "TX audio ownership reset origin"
                           << pttOriginName(m_txAudioArbiter.activeOrigin())
                           << "source" << txAudioInputName(m_txAudioArbiter.activeSource());
    }
    m_txAudioArbiter.reset();
    setTxAudioSource(TxAudioInput::None);
    clearTxAudioInterlockError();
    if (m_selRad)
        m_selRad->setTxAudioState("none", "none");
}

void MainController::setTxAudioInterlockError(PttOrigin requestedOrigin, TxAudioInput requestedSource)
{
    const QString message = QStringLiteral("ERROR: TX audio interlock: %1/%2 blocked by active %3/%4")
                                .arg(pttOriginName(requestedOrigin),
                                     txAudioInputName(requestedSource),
                                     pttOriginName(m_txAudioArbiter.activeOrigin()),
                                     txAudioInputName(m_txAudioArbiter.activeSource()));
    qWarning(logAudio()) << message;
    setFooterMessageText(message);
}

void MainController::clearTxAudioInterlockError()
{
    if (m_footerMessageText.startsWith(QStringLiteral("ERROR: TX audio interlock:")))
        setFooterMessageText(QString());
}

void MainController::setRxAudioLevel(int level)
{
    const int bounded = qBound(0, level, 255);
    if (m_rxAudioLevel == bounded)
        return;

    m_rxAudioLevel = bounded;
    emit audioLevelsChanged();
    // Same level drives the optional "RX Audio" / "Audio" meters.
    receiveOptionalMeter(meterRxAudio, bounded);
}

void MainController::setTxAudioLevel(int level)
{
    const int bounded = qBound(0, level, 255);
    if (m_txAudioLevel == bounded)
        return;

    m_txAudioLevel = bounded;
    emit audioLevelsChanged();
    // Same level drives the optional "TX Mod" / "Audio" meters.
    receiveOptionalMeter(meterTxMod, bounded);
}

void MainController::publishTxAudioDiagnosticLevel(const audioPacket& packet, TxAudioInput source)
{
    if (m_txAudioArbiter.isActive() && m_txAudioArbiter.activeSource() != source)
        return;

    const int level = qBound(0, qRound(packet.amplitudePeak * 255.0f), 255);
    setTxAudioLevel(level);
    if (m_selRad)
        m_selRad->audioInputLevel(quint16(level));
}

void MainController::routeRxAudioToSinks(const audioPacket &packet)
{
    if (!tciRxOutputAudio)
        return;

    QMetaObject::invokeMethod(tciRxOutputAudio, "incomingAudio",
                              Qt::QueuedConnection,
                              Q_ARG(audioPacket, packet));
}

void MainController::routeLocalTxAudioToRadio(const audioPacket &packet)
{
    publishTxAudioDiagnosticLevel(packet, TxAudioInput::LocalInput);

    const AudioRouteState route = currentAudioRoute();
    if (!usbRadioTxAudio || !m_txAudioArbiter.accepts(TxAudioInput::LocalInput)) {
        if ((route.rxSource != RadioRxAudioSource::WfShare &&
             route.rxSource != RadioRxAudioSource::NativeNetwork) ||
            m_txAudioArbiter.activeSource() != TxAudioInput::LocalInput)
            return;
    }

    if (usbRadioTxAudio) {
        QMetaObject::invokeMethod(usbRadioTxAudio, "incomingAudio",
                                  Qt::QueuedConnection,
                                  Q_ARG(audioPacket, packet));
    }

    if ((route.rxSource == RadioRxAudioSource::WfShare ||
         route.rxSource == RadioRxAudioSource::NativeNetwork) &&
        m_txAudioArbiter.activeSource() == TxAudioInput::LocalInput) {
        emit sendTxAudioToRig(packet);
    }
}

void MainController::routeTciTxAudioToRadio(const audioPacket &packet)
{
    if (m_txAudioArbiter.activeSource() != TxAudioInput::Tci)
        return;

    publishTxAudioDiagnosticLevel(packet, TxAudioInput::Tci);

    if (usbRadioTxAudio) {
        QMetaObject::invokeMethod(usbRadioTxAudio, "incomingAudio",
                                  Qt::QueuedConnection,
                                  Q_ARG(audioPacket, packet));
    }

    const AudioRouteState route = currentAudioRoute();
    if (route.rxSource == RadioRxAudioSource::WfShare ||
        route.rxSource == RadioRxAudioSource::NativeNetwork) {
        emit sendTxAudioToRig(packet);
    }
}

void MainController::handleTciTransmitRequested(bool transmit)
{
    if (transmit) {
        if (!rigCaps || !rigCaps->hasTransmit) {
            qWarning(logAudio()) << "Ignoring TCI transmit request because the current rig has no TX capability";
            return;
        }

        if (!requestTxAudioOwnership(PttOrigin::Tci, TxAudioInput::Tci)) {
            qDebug(logAudio()) << "Ignoring TCI transmit audio because another source owns TX audio";
            return;
        }

        qDebug(logAudio()) << "TCI owns transmit audio source";
        setTxAudioSource(TxAudioInput::Tci);
        if (m_activeTxAudioSource != TxAudioInput::Tci) {
            qWarning(logAudio()) << "TCI transmit audio source could not be started";
            releaseTxAudioOwnership(PttOrigin::Tci);
        }
        return;
    }

    if (releaseTxAudioOwnership(PttOrigin::Tci)) {
        qDebug(logAudio()) << "TCI released transmit audio source";
        setTxAudioSource(currentAudioRoute().txInput == TxAudioInput::LocalInput
                         ? TxAudioInput::LocalInput
                         : TxAudioInput::None);
    }
}

void MainController::handleRigCtlTransmitRequested(bool transmit)
{
    if (transmit) {
        if (!rigCaps || !rigCaps->hasTransmit)
            return;

        // rigctld has already queued the PTT command itself.
        armPttTimeout();

        const TxAudioInput source = txSourceForPttOrigin(PttOrigin::RigCtl, currentAudioRoute());
        if (source == TxAudioInput::None)
            return;

        if (!requestTxAudioOwnership(PttOrigin::RigCtl, source)) {
            qDebug(logAudio()) << "Ignoring rigctld transmit audio because another source owns TX audio";
            return;
        }

        if (source == TxAudioInput::LocalInput)
            setTxAudioSource(TxAudioInput::LocalInput);
        return;
    }

    m_pttTimeoutTimer.stop();
    if (releaseTxAudioOwnership(PttOrigin::RigCtl) &&
        currentAudioRoute().rxSource == RadioRxAudioSource::WfShare)
        setTxAudioSource(TxAudioInput::None);
}

void MainController::handleExternalTransmitRequested(bool transmit)
{
    if (transmit) {
        if (!rigCaps || !rigCaps->hasTransmit)
            return;

        const TxAudioInput source = txSourceForPttOrigin(PttOrigin::ExternalCiv, currentAudioRoute());
        if (source == TxAudioInput::None)
            return;

        if (!requestTxAudioOwnership(PttOrigin::ExternalCiv, source)) {
            qDebug(logAudio()) << "External PTT did not take TX audio ownership because another origin is active";
            return;
        }

        if (source == TxAudioInput::LocalInput)
            setTxAudioSource(TxAudioInput::LocalInput);
        return;
    }

    if (releaseTxAudioOwnership(PttOrigin::ExternalCiv) &&
        currentAudioRoute().rxSource == RadioRxAudioSource::WfShare)
        setTxAudioSource(TxAudioInput::None);
}

audioHandlerBase* MainController::createAudioHandler(const audioSetup& setup)
{
    switch (setup.type) {
    case qtAudio:
        return setup.isinput ? static_cast<audioHandlerBase*>(new audioHandlerQtInput())
                             : static_cast<audioHandlerBase*>(new audioHandlerQtOutput());
    case portAudio:
        return setup.isinput ? static_cast<audioHandlerBase*>(new audioHandlerPaInput())
                             : static_cast<audioHandlerBase*>(new audioHandlerPaOutput());
    case rtAudio:
        return setup.isinput ? static_cast<audioHandlerBase*>(new audioHandlerRtInput())
                             : static_cast<audioHandlerBase*>(new audioHandlerRtOutput());
    default:
        qWarning(logAudio()) << "Unsupported audio system for USB audio bridge" << setup.type;
        return nullptr;
    }
}

void MainController::disposeAudioHandler(audioHandlerBase*& handler, QThread*& thread)
{
    if (handler) {
        QMetaObject::invokeMethod(handler, &audioHandlerBase::dispose,
                                  handler->thread() == QThread::currentThread()
                                      ? Qt::DirectConnection
                                      : Qt::BlockingQueuedConnection);
    }

    if (thread) {
        thread->quit();
        thread->wait();
    }

    handler = nullptr;
    thread = nullptr;
}

void MainController::setupUsbAudioBridge()
{
    stopUsbAudioBridge();

    const AudioRouteState route = currentAudioRoute();
    if (!prefs || route.rxSource != RadioRxAudioSource::LocalUsb)
        return;

    if (!route.usbRadioRxInputEnabled && !route.usbRadioTxOutputEnabled) {
        qWarning(logAudio()) << "USB audio enabled but no radio USB audio devices are configured";
        return;
    }

    auto startHandler = [this](audioHandlerBase*& handler, QThread*& thread, const audioSetup& setup, const QString& name) {
        handler = createAudioHandler(setup);
        if (!handler)
            return false;

        thread = new QThread(this);
        thread->setObjectName(name);
        handler->moveToThread(thread);
        connect(thread, &QThread::finished, handler, &QObject::deleteLater);
        thread->start(QThread::TimeCriticalPriority);
        QMetaObject::invokeMethod(handler, "init", Qt::QueuedConnection, Q_ARG(audioSetup, setup));
        return true;
    };

    const bool haveRx = route.usbRadioRxInputEnabled && route.localOutputEnabled;
    if (haveRx) {
        audioSetup radioRx = prefs->usbRxSetup;
        audioSetup localRx = prefs->rxSetup;
        radioRx.type = prefs->audioSystem;
        localRx.type = prefs->audioSystem;
        radioRx.isinput = true;
        localRx.isinput = false;
        radioRx.codec = localRx.codec;
        radioRx.sampleRate = localRx.sampleRate;
        radioRx.latency = localRx.latency;
        radioRx.blockSize = localRx.blockSize;
        radioRx.resampleQuality = localRx.resampleQuality;
        localRx.rxProc = prefs->rxSetup.rxProc;

        if (startHandler(usbRadioRxAudio, usbRadioRxThread, radioRx, QStringLiteral("usbRadioRxAudio()")) &&
            startHandler(usbRxOutputAudio, usbRxOutputThread, localRx, QStringLiteral("usbRxOutputAudio()"))) {
            connect(usbRadioRxAudio, SIGNAL(haveAudioData(audioPacket)),
                    usbRxOutputAudio, SLOT(incomingAudio(audioPacket)),
                    Qt::QueuedConnection);
            connect(usbRadioRxAudio, &audioHandlerBase::haveAudioData,
                    this, &MainController::routeRxAudioToSinks,
                    Qt::QueuedConnection);
            QMetaObject::invokeMethod(usbRxOutputAudio, "setVolume",
                                      Qt::QueuedConnection, Q_ARG(quint8, prefs->localAFgain));
            qDebug(logAudio()) << "USB radio RX audio bridge enabled:" << radioRx.name << "->" << localRx.name;
        } else {
            disposeAudioHandler(usbRadioRxAudio, usbRadioRxThread);
            disposeAudioHandler(usbRxOutputAudio, usbRxOutputThread);
        }
    }

    if (m_activeTxAudioSource == TxAudioInput::Tci)
        setupTciUsbTxBridge();
    else
        setupLocalTxBridge();
}

void MainController::stopUsbAudioBridge()
{
    disposeAudioHandler(usbRadioRxAudio, usbRadioRxThread);
    disposeAudioHandler(usbRxOutputAudio, usbRxOutputThread);
    stopLocalTxBridge();
    updateAudioRouteState();
}

void MainController::setupLocalTxBridge()
{
    stopLocalTxBridge();

    const AudioRouteState route = currentAudioRoute();
    const bool haveUsbTxOutput = route.usbRadioTxOutputEnabled;
    const bool haveNativeNetworkTxOutput = route.rxSource == RadioRxAudioSource::NativeNetwork &&
                                           route.txInput == TxAudioInput::LocalInput;
    const bool haveWfShareTxOutput = route.rxSource == RadioRxAudioSource::WfShare &&
                                     route.txInput == TxAudioInput::LocalInput;
    if (!prefs || route.txInput != TxAudioInput::LocalInput ||
        (!haveUsbTxOutput && !haveNativeNetworkTxOutput && !haveWfShareTxOutput))
        return;

    audioSetup localTx = prefs->txSetup;
    localTx.type = prefs->audioSystem;
    localTx.isinput = true;
    localTx.txProc = prefs->txSetup.txProc;

    usbTxInputAudio = createAudioHandler(localTx);
    if (!usbTxInputAudio) {
        stopLocalTxBridge();
        return;
    }

    audioSetup radioTx = prefs->usbTxSetup;
    if (haveUsbTxOutput) {
        radioTx.type = prefs->audioSystem;
        radioTx.isinput = false;
        radioTx.codec = localTx.codec;
        radioTx.sampleRate = localTx.sampleRate;
        radioTx.latency = localTx.latency;
        radioTx.blockSize = localTx.blockSize;
        radioTx.resampleQuality = localTx.resampleQuality;
        usbRadioTxAudio = createAudioHandler(radioTx);
        if (!usbRadioTxAudio) {
            stopLocalTxBridge();
            return;
        }
    }

    usbTxInputThread = new QThread(this);
    usbTxInputThread->setObjectName(QStringLiteral("usbTxInputAudio()"));
    usbTxInputAudio->moveToThread(usbTxInputThread);
    connect(usbTxInputThread, &QThread::finished, usbTxInputAudio, &QObject::deleteLater);
    connect(usbTxInputAudio, &audioHandlerBase::haveAudioData,
            this, &MainController::routeLocalTxAudioToRadio,
            Qt::QueuedConnection);
    usbTxInputThread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(usbTxInputAudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, localTx));

    if (usbRadioTxAudio) {
        usbRadioTxThread = new QThread(this);
        usbRadioTxThread->setObjectName(QStringLiteral("usbRadioTxAudio()"));
        usbRadioTxAudio->moveToThread(usbRadioTxThread);
        connect(usbRadioTxThread, &QThread::finished, usbRadioTxAudio, &QObject::deleteLater);
        usbRadioTxThread->start(QThread::TimeCriticalPriority);
        QMetaObject::invokeMethod(usbRadioTxAudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, radioTx));
    }

    m_activeTxAudioSource = TxAudioInput::LocalInput;
    if (haveUsbTxOutput)
        qDebug(logAudio()) << "USB radio TX audio bridge enabled:" << localTx.name << "->" << radioTx.name;
    else if (haveNativeNetworkTxOutput)
        qDebug(logAudio()) << "native network TX audio bridge enabled:" << localTx.name << "-> radio";
    else
        qDebug(logAudio()) << "wfshare TX audio bridge enabled:" << localTx.name << "-> wfshare";
}

void MainController::stopLocalTxBridge()
{
    disposeAudioHandler(tciTxInputAudio, tciTxInputThread);
    disposeAudioHandler(usbTxInputAudio, usbTxInputThread);
    disposeAudioHandler(usbRadioTxAudio, usbRadioTxThread);
    m_activeTxAudioSource = TxAudioInput::None;
}

void MainController::setupTciUsbTxBridge()
{
    stopLocalTxBridge();

    const AudioRouteState route = currentAudioRoute();
    if (!prefs || !tci)
        return;

    const bool haveUsbTxOutput = connStatus == connectionStatus_t::connConnected &&
                                 rigCaps && rigCaps->hasTransmit &&
                                 route.validLocalBackend &&
                                 route.rxSource == RadioRxAudioSource::LocalUsb &&
                                 audioSetupHasDevice(prefs->usbTxSetup);
    const bool haveNetworkTxOutput = connStatus == connectionStatus_t::connConnected &&
                                     rigCaps && rigCaps->hasTransmit &&
                                     (route.rxSource == RadioRxAudioSource::NativeNetwork ||
                                      route.rxSource == RadioRxAudioSource::WfShare);
    if (!haveUsbTxOutput && !haveNetworkTxOutput)
        return;

    audioSetup tciTx = prefs->txSetup;
    audioSetup radioTx = prefs->usbTxSetup;
    tciTx.type = prefs->audioSystem;
    tciTx.isinput = true;
    tciTx.tci = tci;
    tciTx.portInt = 0;
    tciTx.txProc = prefs->txSetup.txProc;
    radioTx.type = prefs->audioSystem;
    radioTx.isinput = false;
    radioTx.codec = tciTx.codec;
    radioTx.sampleRate = tciTx.sampleRate;
    radioTx.latency = tciTx.latency;
    radioTx.blockSize = tciTx.blockSize;
    radioTx.resampleQuality = tciTx.resampleQuality;

    tciTxInputAudio = new audioHandlerTciInput();
    if (haveUsbTxOutput)
        usbRadioTxAudio = createAudioHandler(radioTx);
    if (!tciTxInputAudio || (haveUsbTxOutput && !usbRadioTxAudio)) {
        stopLocalTxBridge();
        return;
    }

    tciTxInputThread = new QThread(this);
    tciTxInputThread->setObjectName(QStringLiteral("tciTxInputAudio()"));
    tciTxInputAudio->moveToThread(tciTxInputThread);
    connect(tciTxInputThread, &QThread::finished, tciTxInputAudio, &QObject::deleteLater);
    connect(tciTxInputAudio, &audioHandlerBase::haveAudioData,
            this, &MainController::routeTciTxAudioToRadio,
            Qt::QueuedConnection);
    tciTxInputThread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(tciTxInputAudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, tciTx));

    if (usbRadioTxAudio) {
        usbRadioTxThread = new QThread(this);
        usbRadioTxThread->setObjectName(QStringLiteral("usbRadioTxAudio()"));
        usbRadioTxAudio->moveToThread(usbRadioTxThread);
        connect(usbRadioTxThread, &QThread::finished, usbRadioTxAudio, &QObject::deleteLater);
        usbRadioTxThread->start(QThread::TimeCriticalPriority);
        QMetaObject::invokeMethod(usbRadioTxAudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, radioTx));
    }
    if (haveUsbTxOutput)
        qDebug(logAudio()) << "TCI TX audio bridge enabled: TCI ->" << radioTx.name;
    else
        qDebug(logAudio()) << "TCI TX audio bridge enabled: TCI -> radio transport";
}

void MainController::setTxAudioSource(TxAudioInput source)
{
    if (m_activeTxAudioSource == source)
        return;

    if (source == TxAudioInput::Tci) {
        if (!m_txAudioArbiter.accepts(TxAudioInput::Tci)) {
            qWarning(logAudio()) << "TCI TX audio bridge refused because another source owns TX audio";
            return;
        }
        setupTciUsbTxBridge();
        if (tciTxInputAudio)
            m_activeTxAudioSource = TxAudioInput::Tci;
        return;
    }

    stopLocalTxBridge();
    if (source == TxAudioInput::LocalInput)
        setupLocalTxBridge();
}



void MainController::shutdown()
{
    if (shuttingDown) return;
    shuttingDown = true;

    stopUsbAudioBridge();

#if defined(USB_CONTROLLER)
    stopUsbControllerDevice();
#endif
    stopClusterClient();
    stopRadioServer();

    if (connStatus != connectionStatus_t::connDisconnected)
    {
        connectionHandler(); // This will disconnect/delete if the radio is connected/connecting
    }

    // If startup connection failed or was cancelled while connStatus is disconnected, the
    // commander thread still needs explicit cleanup.
    if (rigThread)
    {
        if (rig) {
            stopUsbAudioBridge();
            QMetaObject::invokeMethod(rig, &rigCommander::closeComm,
                                      rig->thread() == QThread::currentThread()
                                          ? Qt::DirectConnection
                                          : Qt::BlockingQueuedConnection);
        }
        rigThread->quit();
        rigThread->wait();
        rig = nullptr;
        rigThread = nullptr;
    }

    stopRigCtlServer();
    stopTciServer();

    if (settings)
    {
        delete settings;
    }

}

void MainController::quitApplication()
{
    if (m_quitPending)
        return;

    // Give on-disconnect commands and macro time to complete before shutting
    // down. If a disconnect is already under way, wait for it instead.
    if (!shuttingDown) {
        m_quitPending = true;
        int waitMs = 0;
        if (m_disconnectPending)
            waitMs = int(qMax<qint64>(0, m_disconnectDeadline.remainingTime()));
        else
            waitMs = startDisconnectActions();
        if (waitMs > 0) {
            QTimer::singleShot(waitMs, this, [this]() {
                shutdown();
                QCoreApplication::quit();
            });
            return;
        }
    }

    shutdown();
    QCoreApplication::quit();
}

void MainController::ctChanged(SettingsController::prefCtItems items)
{
    if (items.testFlag(prefCtItem::ct_pttTimeout) && !prefs->pttTimeoutEnabled)
        m_pttTimeoutTimer.stop();
    if (items.testFlag(prefCtItem::ct_niceTS)) {
        for (auto *r : std::as_const(receivers)) {
            if (r) r->setTuningFloorZeros(prefs->niceTS);
        }
    }
#if defined(USB_CONTROLLER)
    if (items.testFlag(prefCtItem::ct_enableUSBControllers)) {
        if (!prefs->enableUSBControllers)
            stopUsbControllerDevice();
        else
            setupUsbControllerDevice();
    }
#endif
}

void MainController::lanChanged(SettingsController::prefLanItems items)
{
    if (items.testFlag(prefLanItem::l_enableLAN)) {
        updateAudioRouteState();
        if (connStatus == connectionStatus_t::connDisconnected) {
            if (rig) {
                QMetaObject::invokeMethod(rig, &rigCommander::closeComm,
                                          rig->thread() == QThread::currentThread()
                                              ? Qt::DirectConnection
                                              : Qt::BlockingQueuedConnection);
            }
            if (rigThread) {
                rigThread->quit();
                rigThread->wait();
            }
            rig = nullptr;
            rigThread = nullptr;
        } else {
            qInfo(logRig()) << "Radio connection type changed; reconnect to apply"
                            << (prefs && prefs->enableLAN ? "network" : "serial")
                            << "setup";
        }
    }

    if (items.testFlag(prefLanItem::l_tciPort)) {
        stopTciServer();
        setupTciServer();
    }

    if (items.testFlag(prefLanItem::l_enableRigCtlD) || items.testFlag(prefLanItem::l_rigCtlPort)) {
        stopRigCtlServer();
        setupRigCtlServer();
    }

    if (items.testFlag(prefLanItem::l_rigCtlIntegerFreqs) && rigCtl) {
        rigCtl->setIntegerFreqs(prefs->rigCtlIntegerFreqs);
    }
}

void MainController::raChanged(prefRaItems items)
{
    if (!prefs)
        return;

    if (items.testFlag(prefRaItem::ra_manufacturer)) {
        setManufacturer(prefs->manufacturer);

        if (connStatus == connectionStatus_t::connDisconnected) {
            // Match v2: changing manufacturer only rebuilds the rig definition list.
            // Any idle commander from a failed/cancelled connection must be discarded;
            // a new manufacturer-specific commander is created only when connecting.
            if (rig) {
                QMetaObject::invokeMethod(rig, &rigCommander::closeComm,
                                          rig->thread() == QThread::currentThread()
                                              ? Qt::DirectConnection
                                              : Qt::BlockingQueuedConnection);
            }
            if (rigThread) {
                rigThread->quit();
                rigThread->wait();
            }
            rig = nullptr;
            rigThread = nullptr;
        } else {
            qInfo(logRig()) << "Radio manufacturer changed; reconnect to apply commander type";
        }
    }

    if (items.testFlag(prefRaItem::ra_audioSystem)) {
        prefs->rxSetup.type = prefs->audioSystem;
        prefs->txSetup.type = prefs->audioSystem;
        prefs->usbRxSetup.type = prefs->audioSystem;
        prefs->usbTxSetup.type = prefs->audioSystem;

        prefs->rxSetup.tci = nullptr;
        prefs->txSetup.tci = nullptr;
    }

    if (items.testFlag(prefRaItem::ra_usbAudio) ||
        items.testFlag(prefRaItem::ra_usbAudioRx) ||
        items.testFlag(prefRaItem::ra_usbAudioTx) ||
        items.testFlag(prefRaItem::ra_audioSystem)) {
        updateAudioRouteState();
        if (connStatus == connectionStatus_t::connConnected &&
            currentAudioRoute().rxSource == RadioRxAudioSource::LocalUsb)
            setupUsbAudioBridge();
    }
}

void MainController::setupTciServer()
{
    if (!prefs)
        return;

    if (prefs->tciPort == 0) {
        stopTciServer();
        prefs->rxSetup.tci = nullptr;
        prefs->txSetup.tci = nullptr;
        updateAudioRouteState();
        return;
    }

    if (tci != nullptr) {
        setupTciRxBridge();
        updateAudioRouteState();
        return;
    }

    tci = new tciServer();
    tciThread = new QThread(this);
    tciThread->setObjectName("TCIServer()");
    tci->moveToThread(tciThread);
    connect(queue, &cachingQueue::rigCapsUpdated, tci, &tciServer::receiveRigCaps);
    connect(tci, &tciServer::transmitRequested, this, &MainController::handleTciTransmitRequested);
    connect(tciThread, &QThread::finished, tci, &QObject::deleteLater);
    tciThread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(tci, "init", Qt::QueuedConnection, Q_ARG(quint16, prefs->tciPort));

    setupTciRxBridge();
    updateAudioRouteState();
}

void MainController::stopTciServer()
{
    if (!tciThread)
        return;

    if (releaseTxAudioOwnership(PttOrigin::Tci) ||
        m_activeTxAudioSource == TxAudioInput::Tci)
        setTxAudioSource(TxAudioInput::LocalInput);

    stopTciRxBridge();

    tciThread->quit();
    tciThread->wait();
    tci = nullptr;
    tciThread = nullptr;
    updateAudioRouteState();
}

void MainController::setupRadioServer()
{
    if (server) {
        updateServerRadio();
        publishServerStatus();
        return;
    }

    SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    // The server shares the radio wfview is using, so it needs an identified
    // radio on a serial port.
    if (shuttingDown || !cfg || !cfg->enabled || cfg->rigs.isEmpty() || !rig || !rigCaps ||
        !usingSerialConnection()) {
        publishServerStatus();
        return;
    }

    switch (prefs->manufacturer) {
    case manufIcom:
        server = new icomServer(cfg);
        break;
    case manufKenwood:
        server = new kenwoodServer(cfg);
        break;
    case manufYaesu:
        server = new yaesuServer(cfg);
        break;
    default:
        qWarning(logRigServer()) << "No radio server for manufacturer" << prefs->manufacturer;
        publishServerStatus();
        return;
    }

    cfg->lan = false;
    updateServerRadio();

    RIGCONFIG* radio = cfg->rigs.first();
    if (prefs->enableUsbAudio && !radio->rxAudioSetup.name.isEmpty() &&
        radio->rxAudioSetup.name == prefs->usbRxSetup.name)
        qWarning(logRigServer()) << "The server RX audio input" << radio->rxAudioSetup.name
                                 << "is also wfview's radio USB audio input; some audio systems cannot share it";

    m_serverListening = false;
    m_serverError.clear();
    m_serverStartedAt = QDateTime::currentDateTime();
    m_serverSessions.clear();
    m_serverSeenUsers.clear();
    m_serverTotalLogins = 0;
    const int generation = ++m_serverGeneration;

    serverThread = new QThread(this);
    serverThread->setObjectName(QStringLiteral("radioServer()"));
    server->moveToThread(serverThread);
    connect(serverThread, &QThread::finished, server, &QObject::deleteLater);

    connect(rig, &rigCommander::haveDataForServer, server, &rigServer::dataForServer);
    connect(rig, &rigCommander::haveScopeDataForServer, server, &rigServer::receiveScopeData);
    connect(server, &rigServer::haveDataFromServer, rig, &rigCommander::dataFromServer);

    connect(server, &rigServer::listening, this, [this, generation](bool ok, QString error) {
        if (generation != m_serverGeneration)
            return;
        m_serverListening = ok;
        m_serverError = error;
        if (ok)
            qInfo(logRigServer()) << "Radio server running";
        else
            qWarning(logRigServer()) << "Radio server could not start:" << error;
        publishServerStatus();
    });
    connect(server, &rigServer::userLoggedIn, this, [this, generation](QString username, QString address) {
        if (generation == m_serverGeneration)
            handleServerLogin(username, address);
    });
    connect(server, &rigServer::userLoggedOut, this, [this, generation](QString username, QString address) {
        if (generation == m_serverGeneration)
            handleServerLogout(username, address);
    });

    if (prefs->manufacturer == manufYaesu) {
        connect(server, &rigServer::requestRxAudioForGuid, this,
                [this, generation](QByteArray guid, quint8 codec, quint32 sampleRate) {
            Q_UNUSED(guid)
            if (generation != m_serverGeneration)
                return;
            ensureServerRxAudio(codec, sampleRate);
            ensureServerTxAudio(codec, sampleRate);
        });
        connect(server, &rigServer::releaseRxAudioForGuid, this, [this, generation](QByteArray guid) {
            Q_UNUSED(guid)
            if (generation == m_serverGeneration)
                stopServerHostAudio();
        });
        // Transmit audio from the client, for the radio's USB audio output.
        connect(server, &rigServer::haveAudioData, this, [this, generation](audioPacket packet) {
            if (generation != m_serverGeneration)
                return;
            SERVERCONFIG* c = m_settings->getServerConfig();
            ensureServerTxAudio(packet.codec, packet.sampleRate);
            if (!c->rigs.isEmpty() && c->rigs.first()->txaudio)
                QMetaObject::invokeMethod(c->rigs.first()->txaudio, "incomingAudio",
                                          Qt::QueuedConnection, Q_ARG(audioPacket, packet));
        });
    }

    serverThread->start();
    QMetaObject::invokeMethod(server, &rigServer::init, Qt::QueuedConnection);
    qInfo(logRigServer()) << "Starting radio server for" << rigCaps->modelName
                          << "control port" << cfg->controlPort << "CI-V/CAT port" << cfg->civPort
                          << "audio port" << cfg->audioPort << "scope port" << cfg->scopePort;
    publishServerStatus();
}

void MainController::stopRadioServer()
{
    if (!serverThread) {
        server = nullptr;
        return;
    }

    // Anything the old server still has queued for us is ignored.
    ++m_serverGeneration;
    stopServerHostAudio();

    serverThread->quit();
    serverThread->wait();
    delete serverThread;
    serverThread = nullptr;
    server = nullptr;

    SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    if (cfg && !cfg->rigs.isEmpty()) {
        RIGCONFIG* radio = cfg->rigs.first();
        radio->rig = nullptr;
        radio->rigThread = nullptr;
        radio->rigCaps = nullptr;
        radio->rigAvailable = false;
    }

    m_serverListening = false;
    m_serverError.clear();
    // Everyone still logged in is now logged out.
    m_serverSessions.clear();
    qInfo(logRigServer()) << "Radio server stopped";
    publishServerStatus();
}

void MainController::updateServerRadio()
{
    SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    if (!cfg || cfg->rigs.isEmpty() || !rigCaps)
        return;

    // The server reads these when a client connects, as wfview's radio is
    // already identified and the server never sees the rig caps arrive.
    RIGCONFIG* radio = cfg->rigs.first();
    radio->rig = rig;
    radio->rigThread = rigThread;
    radio->rigCaps = rigCaps;
    radio->rigAvailable = true;
    radio->modelName = rigCaps->modelName;
    radio->rigName = rigCaps->modelName;
    radio->civAddr = rigCaps->modelID;
    radio->baudRate = prefs->serialPortBaud != 0 ? quint32(prefs->serialPortBaud) : rigCaps->baudRate;
    radio->pttType = prefs->pttType;
    radio->waterfallFormat = prefs->waterfallFormat;
    radio->rxAudioSetup.type = prefs->audioSystem;
    radio->txAudioSetup.type = prefs->audioSystem;
    cfg->baudRate = radio->baudRate;
}

void MainController::sendUsersToServer()
{
    SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    if (!server || !cfg)
        return;

    rigServer* s = server;
    const QList<SERVERUSER> users = cfg->users;
    QMetaObject::invokeMethod(server, [s, users]() { s->setUsers(users); }, Qt::QueuedConnection);
}

void MainController::serverChanged(SettingsController::prefServerItems items)
{
    SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    if (!cfg)
        return;

    if (items.testFlag(prefServerItem::s_users))
        sendUsersToServer();

    if (items.testFlag(prefServerItem::s_enabled)) {
        if (cfg->enabled)
            setupRadioServer();
        else
            stopRadioServer();
    } else if (server && (items.testFlag(prefServerItem::s_controlPort) ||
                          items.testFlag(prefServerItem::s_civPort) ||
                          items.testFlag(prefServerItem::s_audioPort) ||
                          items.testFlag(prefServerItem::s_scopePort) ||
                          items.testFlag(prefServerItem::s_listenAddress) ||
                          items.testFlag(prefServerItem::s_audioInput) ||
                          items.testFlag(prefServerItem::s_audioOutput))) {
        qInfo(logRigServer()) << "Radio server settings changed, restarting the server";
        stopRadioServer();
        setupRadioServer();
    }

    publishServerStatus();
}

void MainController::handleServerLogin(const QString& username, const QString& address)
{
    qInfo(logRigServer()) << "User" << username << "logged in from" << address;
    m_serverSessions.append({ username, address, QDateTime::currentDateTime() });
    m_serverSeenUsers.insert(username);
    ++m_serverTotalLogins;
    publishServerStatus();
}

void MainController::handleServerLogout(const QString& username, const QString& address)
{
    qInfo(logRigServer()) << "User" << username << "from" << address << "logged out";
    for (int i = 0; i < m_serverSessions.size(); ++i) {
        if (m_serverSessions.at(i).username == username && m_serverSessions.at(i).address == address) {
            m_serverSessions.removeAt(i);
            break;
        }
    }
    publishServerStatus();
}

int MainController::serverUsersLoggedIn() const
{
    QSet<QString> names;
    for (const serverSession& session : m_serverSessions)
        names.insert(session.username);
    return int(names.size());
}

QString MainController::serverStatusText() const
{
    const SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    if (!cfg || !cfg->enabled)
        return tr("Disabled");
    if (server) {
        if (m_serverListening)
            return tr("Running");
        if (!m_serverError.isEmpty())
            return tr("Not running: %1").arg(m_serverError);
        return tr("Starting");
    }
    if (!usingSerialConnection())
        return tr("Not running: the server is only available when the radio is connected by a serial (USB) port");
    if (connStatus == connectionStatus_t::connDisconnected)
        return tr("Not running: waiting for the radio to be connected");
    return tr("Not running: waiting for the radio to be identified");
}

void MainController::publishServerStatus()
{
    if (m_settings && m_settings->serverUsersModel()) {
        QHash<QString, ServerUsersModel::UserStatus> statuses;
        for (const QString& name : std::as_const(m_serverSeenUsers))
            statuses.insert(name, ServerUsersModel::LoggedOut);
        for (const serverSession& session : std::as_const(m_serverSessions))
            statuses.insert(session.username, ServerUsersModel::LoggedIn);
        m_settings->serverUsersModel()->setUserStatuses(statuses);
    }
    emit serverStatusChanged();
}

void MainController::ensureServerRxAudio(quint8 codec, quint32 sampleRate)
{
    SERVERCONFIG* cfg = m_settings->getServerConfig();
    if (!server || cfg->rigs.isEmpty())
        return;

    RIGCONFIG* radio = cfg->rigs.first();
    if (radio->rxaudio) {
        if (radio->rxAudioSetup.codec != codec || radio->rxAudioSetup.sampleRate != sampleRate)
            qWarning(logRigServer()) << "Ignoring server RX audio format change while audio is active";
        return;
    }

    radio->rxAudioSetup.codec = codec == 0 ? quint8(4) : codec;
    radio->rxAudioSetup.sampleRate = sampleRate == 0 ? quint32(16000) : sampleRate;
    radio->rxAudioSetup.latency = 150;
    radio->rxAudioSetup.blockSize = 10;
    radio->rxAudioSetup.isinput = true;
    radio->rxAudioSetup.type = prefs->audioSystem;
    memcpy(radio->rxAudioSetup.guid, radio->guid, GUIDLEN);

    radio->rxaudio = createAudioHandler(radio->rxAudioSetup);
    if (!radio->rxaudio)
        return;

    qInfo(logRigServer()) << "Starting server RX audio input" << radio->rxAudioSetup.name
                          << "codec" << radio->rxAudioSetup.codec << "sampleRate" << radio->rxAudioSetup.sampleRate;
    radio->rxAudioThread = new QThread(this);
    radio->rxAudioThread->setObjectName(QStringLiteral("serverRxAudio()"));
    radio->rxaudio->moveToThread(radio->rxAudioThread);
    connect(radio->rxAudioThread, &QThread::finished, radio->rxaudio, &QObject::deleteLater);
    connect(radio->rxaudio, &audioHandlerBase::haveAudioData, server, &rigServer::receiveAudioData);
    radio->rxAudioThread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(radio->rxaudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, radio->rxAudioSetup));
}

void MainController::ensureServerTxAudio(quint8 codec, quint32 sampleRate)
{
    SERVERCONFIG* cfg = m_settings->getServerConfig();
    if (!server || cfg->rigs.isEmpty())
        return;

    RIGCONFIG* radio = cfg->rigs.first();
    if (radio->txaudio) {
        if (radio->txAudioSetup.codec != codec || radio->txAudioSetup.sampleRate != sampleRate)
            qWarning(logRigServer()) << "Ignoring server TX audio format change while audio is active";
        return;
    }

    radio->txAudioSetup.codec = codec == 0 ? quint8(4) : codec;
    radio->txAudioSetup.sampleRate = sampleRate == 0 ? quint32(16000) : sampleRate;
    radio->txAudioSetup.latency = radio->txAudioSetup.latency == 0 ? quint16(150) : radio->txAudioSetup.latency;
    radio->txAudioSetup.isinput = false;
    radio->txAudioSetup.type = prefs->audioSystem;
    memcpy(radio->txAudioSetup.guid, radio->guid, GUIDLEN);

    radio->txaudio = createAudioHandler(radio->txAudioSetup);
    if (!radio->txaudio)
        return;

    qInfo(logRigServer()) << "Starting server TX audio output" << radio->txAudioSetup.name
                          << "codec" << radio->txAudioSetup.codec << "sampleRate" << radio->txAudioSetup.sampleRate;
    radio->txAudioThread = new QThread(this);
    radio->txAudioThread->setObjectName(QStringLiteral("serverTxAudio()"));
    radio->txaudio->moveToThread(radio->txAudioThread);
    connect(radio->txAudioThread, &QThread::finished, radio->txaudio, &QObject::deleteLater);
    radio->txAudioThread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(radio->txaudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, radio->txAudioSetup));
}

void MainController::stopServerHostAudio()
{
    // Only the Yaesu server's audio is opened here; the Icom and Kenwood
    // servers open and close their own.
    SERVERCONFIG* cfg = m_settings ? m_settings->getServerConfig() : nullptr;
    if (!prefs || prefs->manufacturer != manufYaesu || !cfg || cfg->rigs.isEmpty())
        return;

    RIGCONFIG* radio = cfg->rigs.first();
    if (radio->rxaudio || radio->rxAudioThread)
        qInfo(logRigServer()) << "Stopping server audio";
    disposeAudioHandler(radio->rxaudio, radio->rxAudioThread);
    disposeAudioHandler(radio->txaudio, radio->txAudioThread);
}

void MainController::setupTciRxBridge()
{
    stopTciRxBridge();

    if (!prefs || !tci)
        return;

    audioSetup tciRx = prefs->rxSetup;
    tciRx.isinput = false;
    tciRx.tci = tci;
    tciRx.portInt = 0;
    tciRx.rxProc = nullptr;

    tciRxOutputAudio = new audioHandlerTciOutput();
    tciRxOutputThread = new QThread(this);
    tciRxOutputThread->setObjectName(QStringLiteral("tciRxOutputAudio()"));
    tciRxOutputAudio->moveToThread(tciRxOutputThread);
    connect(tciRxOutputThread, &QThread::finished, tciRxOutputAudio, &QObject::deleteLater);
    tciRxOutputThread->start(QThread::TimeCriticalPriority);
    QMetaObject::invokeMethod(tciRxOutputAudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, tciRx));
    qInfo(logAudio()) << "TCI RX audio bridge enabled";
}

void MainController::stopTciRxBridge()
{
    disposeAudioHandler(tciRxOutputAudio, tciRxOutputThread);
}

void MainController::setupRigCtlServer()
{
    if (!prefs || !prefs->enableRigCtlD || prefs->rigCtlPort == 0) {
        stopRigCtlServer();
        return;
    }

    if (rigCtl)
        return;

    rigCtl = new rigCtlD(this);
    rigCtl->setIntegerFreqs(prefs->rigCtlIntegerFreqs);
    connect(rigCtl, &rigCtlD::setPTT, this, &MainController::handleRigCtlTransmitRequested);
    if (rigCtl->startServer(prefs->rigCtlPort) < 0) {
        rigCtl->deleteLater();
        rigCtl = nullptr;
    }
}

void MainController::stopRigCtlServer()
{
    if (!rigCtl)
        return;

    rigCtl->stopServer();
    rigCtl->deleteLater();
    rigCtl = nullptr;
}

void MainController::setupClusterClient()
{
    if (cluster || clusterThread)
        return;

    cluster = new dxClusterClient();
    clusterThread = new QThread(this);
    clusterThread->setObjectName("dxClusterClient()");
    cluster->moveToThread(clusterThread);
    connect(clusterThread, &QThread::finished, cluster, &QObject::deleteLater);
    connect(cluster, &dxClusterClient::sendOutput, this, &MainController::appendClusterOutput);
    connect(cluster, &dxClusterClient::sendSpots, this, &MainController::receiveClusterSpots);
    connect(cluster, &dxClusterClient::tcpStatusChanged, this, &MainController::receiveClusterStatus);
    clusterThread->start();

    if (clusterReconnectTimer == nullptr) {
        clusterReconnectTimer = new QTimer(this);
        clusterReconnectTimer->setSingleShot(true);
        clusterReconnectTimer->setInterval(1500);
        connect(clusterReconnectTimer, &QTimer::timeout, this, [this]() {
            if (cluster)
                QMetaObject::invokeMethod(cluster, "reconnectTcpIfChanged", Qt::QueuedConnection);
        });
    }

    applyClusterSettingsToClient(true);
}

void MainController::stopClusterClient()
{
    if (!clusterThread)
        return;

    if (clusterReconnectTimer)
        clusterReconnectTimer->stop();

    if (cluster) {
        QMetaObject::invokeMethod(cluster, "shutdown", Qt::BlockingQueuedConnection);
    }

    clusterThread->quit();
    clusterThread->wait();
    cluster = nullptr;
    clusterThread = nullptr;
}

void MainController::applyClusterSettingsToClient(bool includeTcpEnable)
{
    if (!cluster)
        return;

    QMetaObject::invokeMethod(cluster, "setUdpPort", Qt::QueuedConnection, Q_ARG(int, prefs->clusterUdpPort));
    QMetaObject::invokeMethod(cluster, "enableUdp", Qt::QueuedConnection, Q_ARG(bool, prefs->clusterUdpEnable));
    QMetaObject::invokeMethod(cluster, "setTcpServerName", Qt::QueuedConnection, Q_ARG(QString, prefs->clusterTcpServerName));
    QMetaObject::invokeMethod(cluster, "setTcpUserName", Qt::QueuedConnection, Q_ARG(QString, prefs->clusterTcpUserName));
    QMetaObject::invokeMethod(cluster, "setTcpPassword", Qt::QueuedConnection, Q_ARG(QString, prefs->clusterTcpPassword));
    QMetaObject::invokeMethod(cluster, "setTcpPort", Qt::QueuedConnection, Q_ARG(int, prefs->clusterTcpPort));
    QMetaObject::invokeMethod(cluster, "setTcpTimeout", Qt::QueuedConnection, Q_ARG(int, prefs->clusterTimeout));
    QMetaObject::invokeMethod(cluster, "enableSkimmerSpots", Qt::QueuedConnection, Q_ARG(bool, prefs->clusterSkimmerSpotsEnable));
    QMetaObject::invokeMethod(cluster, "setUdpLifetime", Qt::QueuedConnection,
                              Q_ARG(int, prefs->clusterUdpLifetimeEnable ? prefs->clusterUdpLifetime : 0));
    if (includeTcpEnable)
        QMetaObject::invokeMethod(cluster, "enableTcp", Qt::QueuedConnection, Q_ARG(bool, prefs->clusterTcpEnable));
}

void MainController::clusterChanged(SettingsController::prefClusterItems items)
{
    if (!cluster)
        return;

    if (items.testFlag(prefClusterItem::cl_clusterUdpPort))
        QMetaObject::invokeMethod(cluster, "setUdpPort", Qt::QueuedConnection, Q_ARG(int, prefs->clusterUdpPort));
    if (items.testFlag(prefClusterItem::cl_clusterUdpEnable))
        QMetaObject::invokeMethod(cluster, "enableUdp", Qt::QueuedConnection, Q_ARG(bool, prefs->clusterUdpEnable));
    if (items.testFlag(prefClusterItem::cl_clusterTcpServerName))
        QMetaObject::invokeMethod(cluster, "setTcpServerName", Qt::QueuedConnection, Q_ARG(QString, prefs->clusterTcpServerName));
    if (items.testFlag(prefClusterItem::cl_clusterTcpUserName))
        QMetaObject::invokeMethod(cluster, "setTcpUserName", Qt::QueuedConnection, Q_ARG(QString, prefs->clusterTcpUserName));
    if (items.testFlag(prefClusterItem::cl_clusterTcpPassword))
        QMetaObject::invokeMethod(cluster, "setTcpPassword", Qt::QueuedConnection, Q_ARG(QString, prefs->clusterTcpPassword));
    if (items.testFlag(prefClusterItem::cl_clusterTcpPort))
        QMetaObject::invokeMethod(cluster, "setTcpPort", Qt::QueuedConnection, Q_ARG(int, prefs->clusterTcpPort));
    if (items.testFlag(prefClusterItem::cl_clusterTimeout))
        QMetaObject::invokeMethod(cluster, "setTcpTimeout", Qt::QueuedConnection, Q_ARG(int, prefs->clusterTimeout));
    if (items.testFlag(prefClusterItem::cl_clusterSkimmerSpotsEnable))
        QMetaObject::invokeMethod(cluster, "enableSkimmerSpots", Qt::QueuedConnection, Q_ARG(bool, prefs->clusterSkimmerSpotsEnable));
    if (items.testFlag(prefClusterItem::cl_clusterUdpLifetimeEnable) || items.testFlag(prefClusterItem::cl_clusterUdpLifetime))
        QMetaObject::invokeMethod(cluster, "setUdpLifetime", Qt::QueuedConnection,
                                  Q_ARG(int, prefs->clusterUdpLifetimeEnable ? prefs->clusterUdpLifetime : 0));
    if (prefs->clusterTcpEnable && clusterReconnectTimer &&
        (items.testFlag(prefClusterItem::cl_clusterTcpServerName) || items.testFlag(prefClusterItem::cl_clusterTcpPort) ||
         items.testFlag(prefClusterItem::cl_clusterTcpUserName) || items.testFlag(prefClusterItem::cl_clusterTcpPassword)))
        clusterReconnectTimer->start();
    if (items.testFlag(prefClusterItem::cl_clusterTcpEnable)) {
        if (prefs->clusterTcpEnable)
            applyClusterSettingsToClient(false);
        QMetaObject::invokeMethod(cluster, "enableTcp", Qt::QueuedConnection, Q_ARG(bool, prefs->clusterTcpEnable));
    }
}

void MainController::connectCluster()
{
    if (!cluster)
        setupClusterClient();

    if (prefs->clusterTcpServerName.trimmed().isEmpty()) {
        appendClusterOutput(tr("\nNo TCP cluster server is selected.\n"));
        return;
    }

    applyClusterSettingsToClient(false);
    QMetaObject::invokeMethod(cluster, "enableTcp", Qt::QueuedConnection, Q_ARG(bool, false));
    QMetaObject::invokeMethod(cluster, "enableTcp", Qt::QueuedConnection, Q_ARG(bool, true));
}

void MainController::disconnectCluster()
{
    if (cluster)
        QMetaObject::invokeMethod(cluster, "enableTcp", Qt::QueuedConnection, Q_ARG(bool, false));
}

void MainController::clearClusterOutput()
{
    if (m_clusterOutputText.isEmpty())
        return;

    m_clusterOutputText.clear();
    m_clusterOutputLines = 0;
    emit clusterOutputTextChanged();
}

void MainController::testWfShareConnection()
{
    if (!prefs) {
        qWarning(logSystem()) << "wfshare test failed: settings are not loaded";
        return;
    }

    const QString host = prefs->wfShareHost.trimmed().isEmpty()
        ? (prefs->wfShareDirectMode ? QStringLiteral("127.0.0.1") : QStringLiteral("pbx.wfshare.org"))
        : prefs->wfShareHost.trimmed();
    const quint16 port = prefs->wfSharePort == 0 ? quint16(4569) : prefs->wfSharePort;
    const QString username = prefs->wfShareUsername.trimmed();
    const QString password = prefs->wfSharePassword;
    const QString calledNumber = prefs->wfShareDirectMode ? QString() : prefs->wfShareCalledNumber.trimmed();

    qInfo(logSystem()).noquote()
        << QStringLiteral("wfshare test: connecting %1 to %2:%3 as %4%5")
                .arg(prefs->wfShareDirectMode ? QStringLiteral("direct") : QStringLiteral("via relay"))
                .arg(host)
                .arg(port)
                .arg(username.isEmpty() ? QStringLiteral("<empty>") : username)
                .arg(calledNumber.isEmpty()
                         ? QString()
                         : QStringLiteral(" for station %1").arg(calledNumber));

    auto *session = new IaxClientSession(this);
    QPointer<IaxClientSession> guard(session);
    auto finished = std::make_shared<bool>(false);
    auto finishSession = [guard, finished]() {
        if (*finished || !guard)
            return;

        *finished = true;
        QTimer::singleShot(0, guard, [guard]() {
            if (!guard)
                return;

            guard->disconnectFromServer();
            guard->deleteLater();
        });
    };

    connect(session, &IaxClientSession::debugMessage, this, [](const QString &message) {
        qDebug(logSystem()).noquote() << QStringLiteral("wfshare test: %1").arg(message);
    });
    connect(session, &IaxClientSession::errorOccurred, this, [finishSession](const QString &message) {
        qWarning(logSystem()).noquote() << QStringLiteral("wfshare test failed: %1").arg(message);
        finishSession();
    });
    connect(session, &IaxClientSession::connectedChanged, this, [finishSession](bool connected) {
        if (!connected)
            return;

        qInfo(logSystem()) << "wfshare test: authentication accepted; holding call briefly for routing test";
        QTimer::singleShot(5000, qApp, [finishSession]() {
            qInfo(logSystem()) << "wfshare test: ending routing test call";
            finishSession();
        });
    });

    QTimer::singleShot(15000, this, [guard, finished, finishSession]() {
        if (*finished || !guard)
            return;

        qWarning(logSystem()) << "wfshare test failed: timed out waiting for accept/reject";
        finishSession();
    });

    session->connectToServer(host, port, username, password, calledNumber);
}

void MainController::resetUsbControllers()
{
#if defined(USB_CONTROLLER)
    if (!m_settings)
        return;

    const bool restart = prefs && prefs->enableUSBControllers;
    stopUsbControllerDevice();

    {
        QMutexLocker locker(m_settings->usbMutex());
        m_settings->usbDevices()->clear();
        m_settings->usbButtons()->clear();
        m_settings->usbKnobs()->clear();
    }

    if (auto *controllerUi = m_settings->controllerController())
        controllerUi->deviceModel()->reset();

    m_settings->markDirty();

    if (restart)
        setupUsbControllerDevice();
#endif
}

void MainController::revertSettingsToDefault()
{
    if (!m_settings)
        return;

    m_settings->resetToDefaults();
    qInfo(logSystem()) << "Closing wfview for full preference reset.";
    quitApplication();
}

void MainController::appendClusterOutput(const QString& text)
{
    if (text.isEmpty())
        return;

    m_clusterOutputText.append(text);
    m_clusterOutputLines += text.count(QLatin1Char('\n'));

    constexpr qsizetype maxLines = 300;
    if (m_clusterOutputLines > maxLines) {
        // Drop the oldest lines so maxLines remain.
        qsizetype cut = -1;
        for (qsizetype excess = m_clusterOutputLines - maxLines; excess > 0; --excess)
            cut = m_clusterOutputText.indexOf(QLatin1Char('\n'), cut + 1);
        m_clusterOutputText.remove(0, cut + 1);
        m_clusterOutputLines = maxLines;
    }
    emit clusterOutputTextChanged();
}

void MainController::receiveClusterStatus(int status)
{
    if (m_clusterStatus == status)
        return;
    m_clusterStatus = status;
    emit clusterStatusChanged();
}

QString MainController::clusterStatusText() const
{
    switch (m_clusterStatus) {
    case dxClusterClient::tcpConnecting:         return tr("Connecting...");
    case dxClusterClient::tcpConnectedNoTraffic: return tr("Connected, no traffic");
    case dxClusterClient::tcpConnectedTraffic:   return tr("Connected, traffic received");
    case dxClusterClient::tcpError:              return tr("NC, Error");
    default:                                     return tr("Not connected");
    }
}

void MainController::receiveClusterSpots(quint8 receiver, const QList<spotData> &spots)
{
    if (receiver >= receivers.size() || receivers[receiver] == nullptr)
        return;

    receivers[receiver]->setClusterSpots(spots);
}

#if defined(USB_CONTROLLER)
void MainController::setupUsbControllerDevice()
{
    if (usbControllerDev || usbControllerThread || !m_settings)
        return;

    usbControllerDev = new usbController();

    ControllerController *controllerUi = m_settings->controllerController();
    connect(usbControllerDev, &usbController::sendJog, this, &MainController::changeFrequency);
    connect(usbControllerDev, &usbController::doShuttle, this, &MainController::doShuttle);
    connect(usbControllerDev, &usbController::button, this, &MainController::buttonControl);
    connect(usbControllerDev, &usbController::initUI, controllerUi, &ControllerController::init);
    connect(usbControllerDev, &usbController::removeDevice, controllerUi, &ControllerController::removeDevice);
    connect(usbControllerDev, &usbController::changePage, controllerUi,
            [controllerUi](USBDEVICE *dev, int page) {
                if (dev)
                    controllerUi->setPage(dev->path, page);
            });
    connect(usbControllerDev, &usbController::setConnected, controllerUi, &ControllerController::setConnected);
    connect(usbControllerDev, &usbController::newDevice, controllerUi, &ControllerController::newDevice);
    connect(controllerUi, &ControllerController::sendRequest, usbControllerDev, &usbController::sendRequest);
    connect(controllerUi, &ControllerController::programPages, usbControllerDev, &usbController::programPages);
    connect(controllerUi, &ControllerController::programDisable, usbControllerDev, &usbController::programDisable);
    connect(controllerUi, &ControllerController::backup, usbControllerDev, &usbController::backupController);
    connect(controllerUi, &ControllerController::restore, usbControllerDev, &usbController::restoreController);
    connect(controllerUi, &ControllerController::commandTriggered, this, &MainController::buttonControl);

    QMutex *usbMutex = m_settings->usbMutex();
    usbDevMap *usbDevices = m_settings->usbDevices();
    QVector<BUTTON> *usbButtons = m_settings->usbButtons();
    QVector<KNOB> *usbKnobs = m_settings->usbKnobs();
    usbController *usbDev = usbControllerDev;

    usbControllerThread = new QThread(this);
    usbControllerThread->setObjectName("usb()");
    usbDev->moveToThread(usbControllerThread);
    connect(usbControllerThread, &QThread::started, usbDev,
            [usbDev, usbMutex, usbDevices, usbButtons, usbKnobs]() {
                usbDev->init(usbMutex, usbDevices, usbButtons, usbKnobs);
                usbDev->run();
            });
    connect(usbControllerThread, &QThread::finished, usbDev, &usbController::deleteLater);
    usbControllerThread->start(QThread::LowestPriority);
}

void MainController::stopUsbControllerDevice()
{
    if (!usbControllerThread)
        return;

    usbControllerThread->quit();
    usbControllerThread->wait();
    usbControllerThread = nullptr;
    usbControllerDev = nullptr;
}

void MainController::changeFrequency(int value)
{
    if (freqLock || receivers.isEmpty())
        return;

    const int rx = qBound(0, int(currentReceiver), receivers.size() - 1);
    receivers[rx]->tuneSteps(value, 0, true);
}

void MainController::doShuttle(bool up, int steps)
{
    // Steps already include the device's ring acceleration curve.
    changeFrequency(up ? steps : -steps);
}
#endif

void MainController::buttonControl(const COMMAND* cmd)
{
    if (!cmd || !rigCaps)
        return;

    qDebug(logUsbControl()) << QString("executing command: %0 (%1) suffix:%2 value:%3")
                                   .arg(cmd->text)
                                   .arg(funcString[cmd->command])
                                   .arg(cmd->suffix)
                                   .arg(cmd->value);

    uchar receiver = cmd->suffix < receivers.size() ? cmd->suffix : currentReceiver;
    switch (cmd->command) {
    case funcNone:
        return;
    case funcTransceiverStatus:
        if (cmd->value == -1)
            toggleTransmit();
        else if (cmd->suffix != 0xff)
            setTransmit(cmd->suffix != 0);
        else
            setTransmit(cmd->value != 0);
        return;
    case funcTunerStatus:
        if (cmd->value == 2)
            tuneNow();
        else
            setTunerEnabled(cmd->value != 0);
        return;
    case funcTuningStep:
        if (!rigCaps->steps.empty()) {
            int idx = 0;
            const int stepCount = static_cast<int>(rigCaps->steps.size());
            for (int i = 0; i < stepCount; ++i) {
                if (rigCaps->steps[i].hz == stepSize) {
                    idx = i;
                    break;
                }
            }
            idx = (idx + cmd->value + stepCount) % stepCount;
            setStepSize(rigCaps->steps[idx].hz);
        }
        return;
    case funcMode:
    case funcModeSet:
    case funcUnselectedMode:
        if (receiver < receivers.size()) {
            if (cmd->value == 1 || cmd->value == -1) {
                int idx = 0;
                const int modeCount = static_cast<int>(rigCaps->modes.size());
                for (int i = 0; i < modeCount; ++i) {
                    if (rigCaps->modes[i].mk == receivers[receiver]->getMode()) {
                        idx = i;
                        break;
                    }
                }
                idx = (idx + cmd->value + modeCount) % modeCount;
                receivers[receiver]->setMode(rigCaps->modes[idx].mk, true);
            } else {
                receivers[receiver]->setMode(cmd->mode, true);
            }
        }
        return;
    case funcFreq:
    case funcSelectedFreq:
    case funcUnselectedFreq:
        if (receiver < receivers.size())
            receivers[receiver]->tuneSteps(cmd->value, 0, true);
        return;
    case funcCwPitch:
    case funcKeySpeed:
    case funcAfGain:
    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcMicGain:
    case funcLANModLevel:
    case funcUSBModLevel:
    case funcACCAModLevel:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        queue->addUnique(priorityImmediate, queueItem(funcs(cmd->command), QVariant::fromValue<ushort>(ushort(cmd->value)), false, receiver));
        return;
    default:
        if (cmd->suffix == 0xff)
            queue->add(priorityImmediate, queueItem(funcs(cmd->command), QVariant::fromValue<uchar>(uchar(cmd->value)), false, cmd->suffix));
        else
            queue->add(priorityImmediate, queueItem(funcs(cmd->command), QVariant::fromValue<uchar>(uchar(cmd->suffix)), false, 0));
        return;
    }
}

void MainController::connectionHandler()
{
    if (connStatus == connectionStatus_t::connDisconnected)
    {
        openRigConnection();
    } else {
        if (m_disconnectPending && !shuttingDown)
            return;

        // Give on-disconnect commands and macro time to complete before closing.
        // m_disconnectPending is set first so macro steps see the disconnect.
        if (!shuttingDown) {
            m_disconnectPending = true;
            const int waitMs = startDisconnectActions();
            if (waitMs > 0) {
                QTimer::singleShot(waitMs, this, [this]() {
                    if (!m_disconnectPending)
                        return;
                    m_disconnectPending = false;
                    if (connStatus == connectionStatus_t::connDisconnected)
                        return;
                    disconnectRig();
                    emit connStatusChanged();
                });
                return;
            }
            m_disconnectPending = false;
        }

        disconnectRig();
    }

    emit connStatusChanged();
}

void MainController::disconnectRig()
{
    setClosingActionsText(QString());
    m_connectMacroTimer.stop();
    stopMacro();
    m_pttTimeoutTimer.stop();

    // disconnect and delete
    resetTxAudioOwnership();
    stopUsbAudioBridge();
    stopRadioServer();

    // Might as well empty both queue and cache, as both are now invalid.
    queue->clear();
    queue->clearCache();
    m_meterLevels.clear();
    for (auto *r : std::as_const(receivers)) {
        if (!r)
            continue;
        // Drop queued slot calls (e.g. setScopeData); they would run after the
        // rig, and the rigCaps it owns, have been destroyed below.
        QCoreApplication::removePostedEvents(r, QEvent::MetaCall);
        r->deleteLater();
    }
    receivers.clear();
    detached.clear();
    currentReceiver = 0;
    emit currentReceiverChanged();
    emit receiverCountChanged();
    emit detachedChanged();

    if (rig) {
        QMetaObject::invokeMethod(rig, &rigCommander::closeComm,
                                  rig->thread() == QThread::currentThread()
                                      ? Qt::DirectConnection
                                      : Qt::BlockingQueuedConnection);
    }
    if (rigThread) {
        rigThread->quit();
        rigThread->wait();
    }
    rig = nullptr;
    rigThread = nullptr;
    if (m_memoriesModel) {
        m_memoriesModel->deleteLater();
        m_memoriesModel = nullptr;
        emit memoriesModelChanged();
    }

    queue->interval(-1); // Disable queue
    setFooterMessageText(QString());
    // radioStatusText is kept so the last connection statistics stay visible
    setRigModelName(QString());
    connStatus = connectionStatus_t::connDisconnected;
    updateAudioRouteState();
}

void MainController::openRigConnection()
{
    if (!prefs)
        return;

    resetTxAudioOwnership();
    m_reportedAudioProblems.clear();
    m_serialPortProblemReported = false;
    setRadioStatusText(QString());
    connStatus = connectionStatus_t::connConnecting;
    emit connStatusChanged();
    updateAudioRouteState();

    startRigConnection();

    if (!rig)
        return;

    // Set a suitable queue interval to ensure polling happens quickly enough.
    queue->interval(prefs->wfShareEnabled ? wfShareCmdInterval_ms : cmdStartupInterval_ms);

    if (prefs->wfShareEnabled)
    {
        const QString host = prefs->wfShareHost.trimmed().isEmpty()
            ? (prefs->wfShareDirectMode ? QStringLiteral("127.0.0.1") : QStringLiteral("pbx.wfshare.org"))
            : prefs->wfShareHost.trimmed();
        const quint16 port = prefs->wfSharePort == 0 ? quint16(4569) : prefs->wfSharePort;
        emit sendWfShareCommSetup(rigList, prefs->radioCIVAddr, host, port,
                                  prefs->wfShareUsername.trimmed(),
                                  prefs->wfSharePassword,
                                  prefs->wfShareDirectMode ? QString() : prefs->wfShareCalledNumber.trimmed(),
                                  prefs->rxSetup,
                                  prefs->txSetup);
    }
    else if (prefs->enableLAN)
    {
        udpPrefs->waterfallFormat = prefs->waterfallFormat;
        emit sendNetworkCommSetup(rigList, prefs->radioCIVAddr, *udpPrefs, prefs->rxSetup, prefs->txSetup,
                                  prefs->virtualSerialPort, prefs->virtualSerialPortUseQueue, prefs->tcpPort);
    } else {
        const serialPortResolution serial = m_settings->resolveSerialPort();
        switch (serial.problem) {
        case serialPortProblem::None:
            break;
        case serialPortProblem::Blank:
            // Normal on first run, before a port has been chosen.
            qWarning(logSerial()) << "No radio serial port is configured";
            m_serialPortProblemReported = true;
            break;
        case serialPortProblem::AutoNoPorts:
            reportSerialPortProblem(serial.configured,
                tr("The port was set to auto, but no serial ports are available on this computer. "
                   "The radio may be off, unplugged, or its USB driver may not be installed."));
            break;
        case serialPortProblem::AutoAmbiguous:
            reportSerialPortProblem(serial.configured,
                tr("The port was set to auto, but %n serial ports were found and none could be "
                   "identified as the radio. Select the radio's port by name.", "", serial.portsFound));
            break;
        case serialPortProblem::NotPresent:
            reportSerialPortProblem(serial.configured,
                tr("That port isn't present on this computer. The radio may be off, unplugged, "
                   "or connected to a different port."));
            break;
        case serialPortProblem::NoPermission:
#if defined(Q_OS_LINUX)
            reportSerialPortProblem(serial.configured,
                tr("This user does not have permission to use the port. Add your user to the group "
                   "that owns the port (usually dialout or uucp), then log out and back in."));
#else
            reportSerialPortProblem(serial.configured,
                tr("This user does not have permission to use the port."));
#endif
            break;
        }
        // Connect even when a problem was found: the port handler keeps
        // retrying, so a radio that is switched on later is picked up.
        emit sendSerialCommSetup(rigList, prefs->radioCIVAddr, serial.port, prefs->serialPortBaud,
                                 prefs->virtualSerialPort, prefs->virtualSerialPortUseQueue, prefs->tcpPort,prefs->waterfallFormat);
    }
}

void MainController::startRigConnection()
{
    if (!rigThread)
    {
        // On a serial port the radio may be shared by the radio server, which
        // recognises it by the server's GUID.
        SERVERCONFIG* cfg = m_settings->getServerConfig();
        quint8* guid = (usingSerialConnection() && cfg && !cfg->rigs.isEmpty()) ? cfg->rigs.first()->guid : nullptr;
        switch (prefs->manufacturer){
        case manufIcom:
            rig = guid ? new icomCommander(guid) : new icomCommander();
            break;
        case manufKenwood:
            rig = guid ? new kenwoodCommander(guid) : new kenwoodCommander();
            break;
        case manufYaesu:
            rig = guid ? new yaesuCommander(guid) : new yaesuCommander();
            break;
        default:
            qCritical() << "Unknown Manufacturer, aborting...";
            break;
        }
        if (!rig) {
            setConnStatus(connectionStatus_t::connDisconnected);
            return;
        }

        rigThread = new QThread(this);
        rigThread->setObjectName("rigCommander()");

        // Thread:
        rig->moveToThread(rigThread);
        connect(rigThread, &QThread::started, rig, &rigCommander::process);
        connect(rigThread, &QThread::finished, rig, &rigCommander::deleteLater);
        rigThread->start();

        // Renamed commSetup to network/serial to make it easier to differentiate.
        connect(this, &MainController::sendNetworkCommSetup,rig,&rigCommander::networkCommSetup);
        connect(this, &MainController::sendSerialCommSetup,rig,&rigCommander::serialCommSetup);
        connect(this, &MainController::sendWfShareCommSetup, rig, &rigCommander::wfShareCommSetup);
        connect(this, &MainController::sendLocalAudioVolume, rig, &rigCommander::setLocalAudioVolume);
        connect(this, &MainController::sendTxAudioToRig, rig, &rigCommander::receiveTxAudioData);

        // This is the signal from rigCommander that we are connected and ready to start model detection.
        connect(rig, &rigCommander::commReady, this, &MainController::receiveCommReady);
        // This signal tells rigCommander that we want to disconnect cleanly.
        connect(this, &MainController::sendCloseComm, rig, &rigCommander::closeComm);

        connect(this, &MainController::setRigID, rig, &rigCommander::setRigID);
        connect(this, &MainController::setCIVAddr, rig, &rigCommander::setCIVAddr);
        connect(this, &MainController::setPTTType, rig, &rigCommander::setPTTType);
        connect(this, &MainController::sendPowerOn, rig, &rigCommander::powerOn);
        connect(this, &MainController::sendPowerOff, rig, &rigCommander::powerOff);

        connect(m_selRad.get(), &SelectRadioController::selectedRadio, rig, &rigCommander::setCurrentRadio);
        connect(rig, &rigCommander::setRadioUsage, m_selRad.get(), &SelectRadioController::setInUse);
        connect(rig, &rigCommander::requestRadioSelection, m_selRad.get(), &SelectRadioController::populate);
        connect(rig, &rigCommander::haveStatusUpdate, this, &MainController::receiveStatusUpdate);
        connect(rig, &rigCommander::haveNetworkAudioLevels, this, &MainController::receiveNetworkAudioLevels);
        connect(rig, &rigCommander::havePortError, this, &MainController::receivePortError);
        connect(rig, &rigCommander::connectionLost, this, &MainController::handleRigConnectionLost);
        connect(rig, &rigCommander::haveAudioData, this, &MainController::routeRxAudioToSinks);
        connect(rig, &rigCommander::externalPttChanged, this, &MainController::handleExternalTransmitRequested);
        connect(rig, &rigCommander::stopsidetone, m_cwSender.get(), &CWSenderController::handleStopSidetone);

        /*
        connect(this, SIGNAL(setPTTType(pttType_t)), rig, SLOT(setPTTType(pttType_t)));
        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));
        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(setRigID(quint16)), rig, SLOT(setRigID(quint16)));
        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));
        connect(this, SIGNAL(setCIVAddr(quint16)), rig, SLOT(setCIVAddr(quint16)));
        connect(this, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
        connect(this, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));
        connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));
        connect(rig, SIGNAL(haveRptOffsetFrequency(freqt)), rpt, SLOT(handleRptOffsetFrequency(freqt)));
        */

    }

}

void MainController::receiveStatusUpdate(networkStatus status)
{
    setRxAudioLevel(status.rxAudioLevel);
    if (m_activeTxAudioSource == TxAudioInput::None)
        setTxAudioLevel(status.txAudioLevel);

    if (!m_selRad)
        return;

    m_selRad->audioOutputLevel(status.rxAudioLevel);
    m_selRad->audioInputLevel(status.txAudioLevel);
    m_selRad->addTimeDifference(status.timeDifference);

    if (status.message.startsWith(QStringLiteral("IAX "))) {
        setRadioStatusText(status.message);
        return;
    }

    const QString txPrefix = status.message.contains(QStringLiteral("(no tx)")) ? QStringLiteral("(no tx) ") : QString();
    setRadioStatusText(QStringLiteral("%1rx latency: %2 ms / rtt: %3 ms / loss: %4/%5")
                           .arg(txPrefix)
                           .arg(status.rxCurrentLatency, 3)
                           .arg(status.networkLatency, 3)
                           .arg(status.packetsLost, 3)
                           .arg(status.packetsSent, 3));
}

void MainController::receiveNetworkAudioLevels(networkAudioLevels levels)
{
    if (levels.haveRxLevels) {
        setRxAudioLevel(levels.rxAudioPeak);
        if (m_selRad)
            m_selRad->audioOutputLevel(levels.rxAudioPeak);
    }

    if (levels.haveTxLevels && m_activeTxAudioSource == TxAudioInput::None) {
        setTxAudioLevel(levels.txAudioPeak);
        if (m_selRad)
            m_selRad->audioInputLevel(levels.txAudioPeak);
    }
}

void MainController::handleRigConnectionLost()
{
    if (shuttingDown || connStatus == connectionStatus_t::connDisconnected)
        return;

    // The radio dropped the connection, so tear down as if the user had
    // pressed Disconnect. The on-disconnect commands are skipped as there is
    // no radio to send them to, and the error that arrived just before this
    // is kept in the footer.
    qInfo(logSystem()) << "Radio connection lost, disconnecting";
    const QString footer = m_footerMessageText;
    m_disconnectPending = false;
    disconnectRig();
    setFooterMessageText(footer);
    emit connStatusChanged();
}

void MainController::checkAudioDeviceConfiguration()
{
    // A blank or missing device never reaches an audio handler, because the
    // audio route leaves it out, so check the configuration directly.
    if (!prefs)
        return;

    const AudioRouteState route = currentAudioRoute();
    const bool canTransmit = rigCaps && rigCaps->hasTransmit;

    auto check = [this](const audioSetup& setup) {
        if (audioSetupHasDevice(setup))
            return; // Present; any open failure is reported by the handler.
        const int problem = setup.name.trimmed().isEmpty()
                                ? audioDeviceNotifier::DeviceBlank
                                : audioDeviceNotifier::DeviceNotFound;
        handleAudioDeviceProblem(int(setup.role), setup.isinput, problem,
                                 setup.name, QString());
    };

    switch (route.rxSource) {
    case RadioRxAudioSource::NativeNetwork:
    case RadioRxAudioSource::WfShare:
        check(prefs->rxSetup);
        if (canTransmit)
            check(prefs->txSetup);
        break;
    case RadioRxAudioSource::LocalUsb:
        check(prefs->usbRxSetup);
        check(prefs->rxSetup);
        if (canTransmit) {
            check(prefs->usbTxSetup);
            check(prefs->txSetup);
        }
        break;
    case RadioRxAudioSource::None:
        break;
    }
}

void MainController::handleAudioDeviceProblem(int role, bool isInput, int problem,
                                               QString deviceName, QString detail)
{
    if (shuttingDown)
        return;

    QString deviceKind;
    switch (audioDeviceRole(role)) {
    case audioDeviceRole::LocalOutput:
        deviceKind = tr("audio output device (speaker/headphones)");
        break;
    case audioDeviceRole::LocalInput:
        deviceKind = tr("audio input device (microphone)");
        break;
    case audioDeviceRole::RadioUsbInput:
        deviceKind = tr("radio USB audio input device (receive audio from the radio)");
        break;
    case audioDeviceRole::RadioUsbOutput:
        deviceKind = tr("radio USB audio output device (transmit audio to the radio)");
        break;
    case audioDeviceRole::Unknown:
        deviceKind = isInput ? tr("audio input device") : tr("audio output device");
        break;
    }

    QString headline;
    switch (problem) {
    case audioDeviceNotifier::DeviceBlank:
        headline = tr("The selected %1 was blank. No device has been chosen.").arg(deviceKind);
        break;
    case audioDeviceNotifier::DeviceNotFound:
        headline = tr("The selected %1 could not be found.").arg(deviceKind);
        break;
    default:
        headline = tr("The selected %1 could not be used.").arg(deviceKind);
        break;
    }

    const QString key = QStringLiteral("%1:%2:%3").arg(role).arg(isInput).arg(problem);
    qWarning(logAudio()).noquote() << headline << "Device:" << deviceName << "Detail:" << detail;
    if (m_reportedAudioProblems.contains(key))
        return;
    m_reportedAudioProblems.insert(key);

    if (!prefs || !prefs->warnAudioDeviceProblems)
        return;

    QStringList lines;
    if (!deviceName.trimmed().isEmpty())
        lines << tr("Specified Device: \"%1\"").arg(deviceName.trimmed());
    if (!detail.trimmed().isEmpty())
        lines << QString() << tr("Reason: %1").arg(detail.trimmed());
    if (problem == audioDeviceNotifier::DeviceNotFound)
        lines << tr("The device may have been unplugged, renamed, or is in use by a different audio system.");
    else if (problem == audioDeviceNotifier::DeviceUnusable)
        lines << tr("The device may be in use by another program, or may not support the required audio format.");

    const QString detailText = lines.join(QLatin1Char('\n'));
    emitWhenWindowReady([this, headline, detailText]() { emit audioDeviceProblem(headline, detailText); });
}

bool MainController::usingSerialConnection() const
{
    return prefs && !prefs->wfShareEnabled && !prefs->enableLAN;
}

void MainController::reportSerialPortProblem(const QString& port, const QString& reason)
{
    if (shuttingDown || m_serialPortProblemReported)
        return;
    m_serialPortProblemReported = true;

    const SERVERCONFIG* server = m_settings ? m_settings->getServerConfig() : nullptr;
    const bool serverEnabled = server && server->enabled;

    qWarning(logSerial()).noquote() << "Serial port" << port << "cannot be used:" << reason;

    if (serverEnabled && server->suppressSerialWarnings) {
        qInfo(logSerial()) << "Serial port warning suppressed (radio server setting); will keep retrying";
        return;
    }

    const QString headline = serverEnabled
        ? tr("wfview cannot use the serial port for running the radio server.")
        : tr("wfview cannot use the serial port for connecting to the radio.");

    QStringList lines;
    lines << tr("Serial port: \"%1\"").arg(port.trimmed());
    lines << QString() << reason.trimmed();
    lines << QString()
          << tr("To use a different port, disconnect from the radio, go to Settings \u2192 Radio Access, "
                "and select a new serial device. Then connect again.");
    if (serverEnabled)
        lines << QString()
              << tr("For an unattended server, this warning can be turned off in "
                    "Settings \u2192 Radio Server; wfview will then keep retrying the port.");

    const QString detailText = lines.join(QLatin1Char('\n'));
    emitWhenWindowReady([this, headline, detailText]() { emit serialPortProblem(headline, detailText); });
}

void MainController::emitWhenWindowReady(std::function<void()> alert)
{
    if (m_windowReady)
        alert();
    else
        m_pendingWindowAlerts.append(std::move(alert));
}

void MainController::windowReady()
{
    if (m_windowReady)
        return;
    m_windowReady = true;
    const auto pending = std::move(m_pendingWindowAlerts);
    m_pendingWindowAlerts.clear();
    for (const auto& alert : pending)
        alert();
}

void MainController::receivePortError(errorType err)
{
    qWarning(logSystem()) << "Radio connection error from" << err.device << ":" << err.message;

    // A failure to open the radio's own serial port; virtual serial port
    // errors carry alert == false.
    if (err.alert && usingSerialConnection() && connStatus != connectionStatus_t::connDisconnected)
        reportSerialPortProblem(err.device, err.message);

    QString message = err.message.trimmed();
    message.replace(QLatin1Char('\r'), QLatin1Char(' '));
    message.replace(QLatin1Char('\n'), QLatin1Char(' '));

    const QString device = err.device.trimmed();
    if (!message.isEmpty()) {
        m_footerErrorText = device.isEmpty()
                                ? tr("ERROR: %1").arg(message)
                                : tr("ERROR: %1: %2").arg(device, message);
        setFooterMessageText(m_footerErrorText);
        m_footerErrorTimer.start(20000);
    }

    if (!prefs || !prefs->wfShareEnabled)
        return;

    if (shuttingDown || connStatus == connectionStatus_t::connDisconnected)
        return;

    queue->clear();
    queue->clearCache();
    resetTxAudioOwnership();
    stopRadioServer();

    if (rig) {
        QMetaObject::invokeMethod(rig, &rigCommander::closeComm,
                                  rig->thread() == QThread::currentThread()
                                      ? Qt::DirectConnection
                                      : Qt::BlockingQueuedConnection);
    }
    if (rigThread) {
        rigThread->quit();
        rigThread->wait();
    }
    rig = nullptr;
    rigThread = nullptr;

    for (auto *r : std::as_const(receivers)) {
        if (!r)
            continue;
        // The rig and its rigCaps are gone; queued slot calls must not run.
        QCoreApplication::removePostedEvents(r, QEvent::MetaCall);
        r->deleteLater();
    }
    receivers.clear();
    detached.clear();
    currentReceiver = 0;
    emit currentReceiverChanged();
    emit receiverCountChanged();
    emit detachedChanged();

    if (m_memoriesModel) {
        m_memoriesModel->deleteLater();
        m_memoriesModel = nullptr;
        emit memoriesModelChanged();
    }

    queue->interval(-1);
    setRigModelName(QString());
    connStatus = connectionStatus_t::connDisconnected;
    emit connStatusChanged();
}

void MainController::receiveModInput(const rigInput& input, int dataMode)
{
    if (!m_settings || dataMode < 0 || dataMode > 3)
        return;

    auto* pref = m_settings->getPrefs();
    if (m_currentModSrc[dataMode] == input)
        return;

    const auto oldLevelCommand = inputLevelCommand(pref->inputSource[dataMode].type);
    if (oldLevelCommand.cmd != funcNone)
        queue->del(oldLevelCommand.cmd, currentReceiver);

    pref->inputSource[dataMode] = input;
    m_currentModSrc[dataMode] = input;

    if (currentDataMode() == dataMode)
        updateCurrentModSource(true);

    ++m_modSourceRevision;
    emit modSourcesChanged();
}

void MainController::processModLevel(inputTypes input, int level)
{
    if (!m_settings)
        return;

    auto* pref = m_settings->getPrefs();
    const uchar dataMode = currentDataMode();
    if (pref->inputSource[dataMode].type != input)
        return;

    pref->inputSource[dataMode].level = uchar(level);
    if (m_micGain != level) {
        m_micGain = level;
        emit micGainChanged();
    }
}

void MainController::updateCurrentModSource(bool requestLevel)
{
    if (!m_settings)
        return;

    const auto input = m_settings->getPrefs()->inputSource[currentDataMode()];
    const auto levelCommand = inputLevelCommand(input.type);
    const QString label = input.name.isEmpty() ? tr("Mic") : input.name;

    bool changed = false;
    if (m_modGainLabel != label) {
        m_modGainLabel = label;
        changed = true;
    }
    if (m_modGainMin != levelCommand.minVal || m_modGainMax != levelCommand.maxVal) {
        m_modGainMin = levelCommand.minVal;
        m_modGainMax = levelCommand.maxVal;
        changed = true;
    }

    if (requestLevel && levelCommand.cmd != funcNone)
        queue->add(priorityMedium, levelCommand.cmd, true, currentReceiver);

    if (changed)
        emit modSourcesChanged();
}

void MainController::handleDataModeChanged(int receiver)
{
    if (receiver != currentReceiver || !m_settings)
        return;

    const uchar dataMode = currentDataMode();
    m_currentModSrc[dataMode] = rigInput();

    const funcs sourceFunc = modSourceCommand(dataMode);
    if (sourceFunc != funcNone && rigCaps && rigCaps->commands.contains(sourceFunc))
        queue->add(priorityImmediate, sourceFunc, false, currentReceiver);

    updateCurrentModSource(false);
}

void MainController::receiveCommReady()
{
    qInfo(logSystem()) << "Received CommReady() signal from rigCommander()";
    if(!prefs->enableLAN && !prefs->wfShareEnabled)
    {
        // If we're not using the LAN, then we're on serial, and
        // we already know the baud rate and can calculate the timing parameters.
        //calculateTimingParameters();
    }
    if(prefs->radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        // ui->statusBar->showMessage(QString("Searching CI-V bus for connected radios."), 1000);

        queue->addUnique(priorityHigh,funcTransceiverId,true);
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qInfo(logSystem()) << QString("Skipping automatic CIV, using user-supplied value of 0x%1").arg(prefs->radioCIVAddr,2,16) ;

        //showStatusBarText(QString("Using user-supplied radio CI-V address of 0x%1").arg(prefs->radioCIVAddr, 2, 16));
        if(prefs->CIVisRadioModel)
        {
            qInfo(logSystem()) << QString("Skipping Rig ID query, using user-supplied model from CI-V address: 0x%1").arg(prefs->radioCIVAddr,2,16) ;
            emit setCIVAddr(prefs->radioCIVAddr);
            emit setRigID(prefs->radioCIVAddr);
        } else {
            emit setCIVAddr(prefs->radioCIVAddr);
            queue->addUnique(priorityHigh,funcTransceiverId,true);
        }
    }
    if (prefs->autoPowerOn)
    {
        // After 2s send powerOn command
        QTimer::singleShot(2000, rig, SLOT(powerOn()));
    }
}

void MainController::getInitialRigState()
{

    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 200ms. Faster is possible but slower
    // computers will glitch occasionally.

    queue->clear();

    foreach (auto cap, rigCaps->periodic) {
        if (!cap.modes.isEmpty()) {
            qDebug(logSystem()) << "Deferring mode-specific periodic command" << funcString[cap.func] << "until mode is known";
            continue;
        }

        if (cap.receiver == char(-1)) {
            for (uchar v=0;v<rigCaps->numReceiver;v++)
            {
                qDebug(logSystem()) << "Inserting command" << funcString[cap.func] << "priority" << cap.priority << "on Receiver" << QString::number(v);
                queue->add(queuePriority(cap.prioVal),cap.func,true,v);
            }
        }
        else {
            qDebug(logSystem()) << "Inserting command" << funcString[cap.func] << "priority" << cap.priority << "on Receiver" << QString::number(cap.receiver);
            queue->add(queuePriority(cap.prioVal),cap.func,true,cap.receiver);
        }
    }

    if (rigCaps->commands.contains(funcAutoInformation) && (!rigCaps->hasSpectrum || prefs->enableLAN)) {
        queue->add(priorityImmediate,queueItem(funcAutoInformation,QVariant::fromValue(uchar(2)),false,0));
        // Enable metering data in the AutoInformation stream:
        queue->add(priorityImmediate,queueItem(funcALCMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcCompMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcIdMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcVdMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcSWRMeter,QVariant::fromValue(uchar(1)),false,0));
    }

    configureOptionalMeter(2, prefs->meter2Type);
    configureOptionalMeter(3, prefs->meter3Type);
    refreshMeterRequests();

    if (prefs->forceVfoMode) {
        queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(vfoA)));
        //if (rigCaps->commands.contains(funcSatelliteMode))
        //    queue->add(priorityImmediate,funcSatelliteMode,false,0); // are we in satellite mode?.

        //if (rigCaps->commands.contains(funcVFOModeSelect))
        //    queue->add(priorityImmediate,funcVFOModeSelect); // Make sure we are in VFO mode.

        //if (rigCaps->commands.contains(funcScopeMainSub))
        //    queue->add(priorityImmediate,queueItem(funcScopeMainSub,QVariant::fromValue(uchar(0)),false,0)); // Set main scope

    }
    //if (rigCaps->commands.contains(funcVFOBandMS))
    //    queue->add(priorityImmediate,queueItem(funcVFOBandMS,QVariant::fromValue(uchar(0)),false,0));

    if(rigCaps->hasSpectrum)
    {
        // Send commands to start scope immediately
        //if (receivers.size()>0 && receivers[0]->isScopeEnabled()) {
        queue->add(priorityHigh,queueItem(funcScopeOnOff,QVariant::fromValue(quint8(1)),true));
        queue->add(priorityHigh,queueItem(funcScopeDataOutput,QVariant::fromValue(quint8(1)),false));
        //}

        // Find the scope ref limits
        auto mr = rigCaps->commands.find(funcScopeRef);
        if (mr != rigCaps->commands.end())
        {
            for (uchar f = 0; f<receivers.size();f++) {
                queue->add(priorityMediumHigh,funcScopeRef,false,f);
            }
        }
    }

    quint64 start=UINT64_MAX;
    quint64 end=0;
    for (auto &band: rigCaps->bands)
    {
        if (band.region == "" || band.region == prefs->region) {
            if (start > band.lowFreq)
                start = band.lowFreq;
            if (end < band.highFreq)
                end = band.highFreq;
        }
    }

    frequencyDisplay["digits"] = 0;
    frequencyDisplay["min"] = start;
    frequencyDisplay["max"] = end;
    frequencyDisplay["minStep"] = 1;
    frequencyDisplay["unit"] = (FctlUnit)prefs->frequencyUnits;
    frequencyDisplay["gsep"] = prefs->groupSeparator;
    frequencyDisplay["dsep"] = prefs->decimalSeparator;

    for (const auto& receiver: std::as_const(receivers))
    {
        qInfo(logSystem()) << "Display Settings start:" << start << "end:" << end;
        qInfo(logSystem()) << "Frequency units:" << prefs->frequencyUnits;
        qInfo(logSystem()) << "Group Separator:" << prefs->groupSeparator;
        qInfo(logSystem()) << "Decimal Separator:" << prefs->decimalSeparator;
        receiver->setFreqDisplay(frequencyDisplay);
        receiver->setBandIndicators(prefs->showBands, prefs->region, &rigCaps->bands);
    }

    if (rigCaps->commands.contains(funcRitFreq))
    {
        funcType func = rigCaps->commands.find(funcRitFreq).value();
        //ui->ritTuneDial->setRange(func.minVal,func.maxVal);
    }

    if (rigCaps->commands.contains(funcTime) && prefs->setRadioTime)
        prepareRadioClockSync();

    applyConnectModSources();
    scheduleConnectMacro();

    /*
    if (cw != nullptr)
    {
        cw->receiveEnabled(rigCaps->commands.contains(funcCWDecode));
    }
    */

    if (rigCaps->commands.contains(funcVOIP))
    {
        queue->add(priorityHigh,queueItem(funcVOIP,QVariant::fromValue<uchar>(prefs->rxSetup.codec==0x80?2:1),false,0));
    }

}

void MainController::setAppTheme(bool isCustom)
{
    if(isCustom)
    {
#ifndef Q_OS_LINUX
        QFile f(":"+prefs->stylesheetPath); // built-in resource
#else
        QFile f(PREFIX "/share/wfview/" + prefs->stylesheetPath);
        if (!f.exists())
        {
            // Fallback to built-in resource for AppImage and non-root installs.
            f.setFileName(":" + prefs->stylesheetPath);
        }
#endif
        if (!f.exists())
        {
            printf("Unable to set stylesheet, file not found\n");
            printf("Tried to load: [%s]\n", f.fileName().toStdString().c_str() );
        }
        else
        {
            if (f.open(QFile::ReadOnly | QFile::Text)) {
                QTextStream ts(&f);
                Q_UNUSED(ts)
                qInfo() << "Ignoring QWidget stylesheet in QML UI:" << f.fileName();
            }
        }
    } else {
        qInfo() << "Ignoring QWidget stylesheet reset in QML UI";
    }
}
void MainController::receiveRigCaps(rigCapabilities* caps)
{
    this->rigCaps = caps;
    if (m_cwSender) {
        m_cwSender->receiveRigCaps(caps);
        m_cwSender->receiveEnabled(caps && caps->commands.contains(funcCWDecode));
    }

    for (auto *r : std::as_const(receivers)) {
        if (r) r->deleteLater();
    }
    receivers.clear();
    detached.clear();
    currentReceiver = 0;
    emit currentReceiverChanged();

    if (rigCaps) {
        if(prefs->polling_ms != 0)
        {
            queue->interval(prefs->polling_ms); // Set sensible frequency to poll the radio
        } else if (prefs->wfShareEnabled) {
            queue->interval(wfShareCmdInterval_ms);
        } else {
            if (prefs->serialPortBaud == 0)
            {
                prefs->serialPortBaud = 9600;
                qInfo(logSystem()) << "WARNING: baud rate received was zero. Assuming 9600 baud, performance may suffer.";
            }

            unsigned int usPerByte = 9600*1000 / prefs->serialPortBaud;
            unsigned int msMinTiming=usPerByte * 10*2/1000;
            if(msMinTiming < 25)
                msMinTiming = 25;

            if(rigCaps->hasFDcomms) {
                queue->interval(msMinTiming);
            } else {
                queue->interval(msMinTiming * 4);
            }

            // Normal:
            cmdInterval_ms = queue->interval();
        }

        receivers.reserve(rigCaps->numReceiver);
        for (int i = 0; i < rigCaps->numReceiver; ++i) {
            auto *rc = new ReceiverController(i, prefs->region, this);   // UI thread only
            rc->setColors(m_settings->getCurrentColorPreset());
            rc->setTuningFloorZeros(prefs->niceTS);
            rc->receiveStepSize(stepSize);
            m_settings->ensureReceiverSettings(i);
            const QVariantMap receiverPrefs = m_settings->receiverSettings(i);
            rc->setScopeDisplaySettings(receiverPrefs.value("PlotFloor").toInt(),
                                        receiverPrefs.value("PlotCeiling").toInt(),
                                        receiverPrefs.value("WFLength").toInt(),
                                        static_cast<WaterfallItem::Theme>(receiverPrefs.value("WFTheme").toInt()),
                                        prefs->wfInterpolate,
                                        prefs->wfAntiAlias);
            connect(rc, &ReceiverController::dataModeChanged, this, [this, i]() {
                handleDataModeChanged(i);
            });
            connect(rc, &ReceiverController::scopeUpdated, this, [this, i](const scopeData &scope) {
                if (!cluster || !scope.valid || scope.endFreq <= scope.startFreq)
                    return;

                QMetaObject::invokeMethod(cluster, "freqRange", Qt::QueuedConnection,
                                          Q_ARG(quint8, quint8(i)),
                                          Q_ARG(double, scope.startFreq),
                                          Q_ARG(double, scope.endFreq));
            });

            if (i == 0) {
                // Report scope redraw time to Select Radio window (only scope 0)
                //connect(rc,&ReceiverController::spectrumTime,m_selRad,&SelectRadioController::spectrumTime);
                //connect(rc,&ReceiverController::waterfallTime,m_selRad,&SelectRadioController::spectrumTime);
                //connect(rc,&ReceiverController::spectrumTime,rig,&rigCommander::spectrumTime);
                //connect(rc,&ReceiverController::waterfallTime,rig,&rigCommander::waterfallTime);
            }

            receivers.push_back(rc);
        }
        detached.fill(false,rigCaps->numReceiver);
        for (int i = 0; i < detached.size(); ++i)
            detached[i] = m_settings->receiverSetting(i, QStringLiteral("Detached"), false).toBool();
        connStatus = connectionStatus_t::connConnected;
        // A successful connection supersedes any earlier connection errors.
        m_footerErrorTimer.stop();
        m_footerErrorText.clear();
        setFooterMessageText(QString());
        updateAudioRouteState();
        checkAudioDeviceConfiguration();
        setRigModelName(rigCaps->modelName);
        setWindowTitle(rigCaps->modelName);
        updateCurrentModSource(false);
        ++m_modSourceRevision;
        emit modSourcesChanged();
        m_repeaterDuplexMode = dmSplitOff;
        m_repeaterToneMode = ratrNN;
        m_repeaterTone = rigCaps->ctcss.empty() ? 670 : int(rigCaps->ctcss.front().tone);
        m_repeaterTsql = m_repeaterTone;
        m_repeaterDtcs = rigCaps->dtcs.empty() ? 23 : int(rigCaps->dtcs.front().tone);
        m_repeaterDtcsTxInvert = false;
        m_repeaterDtcsRxInvert = false;
        m_repeaterOffsetHz = 0;
        m_repeaterQuickSplit = false;
        m_repeaterWarnNotFm = false;
        m_repeaterHaveMainFrequency = false;
        emit repeaterStateChanged();
        emit repeaterModelsChanged();

        getInitialRigState();
        buildUiSpecs();
        refreshRepeaterState();
        setupRadioServer();
    }
    else
    {
        stopRadioServer();
        resetTxAudioOwnership();
        setFooterMessageText(QString());
        setRigModelName(QString());
        connStatus = connectionStatus_t::connDisconnected;
        updateAudioRouteState();
        emit repeaterStateChanged();
        emit repeaterModelsChanged();
    }

    emit connStatusChanged();

    emit receiverCountChanged();
    emit detachedChanged();

    if (rigCaps && currentAudioRoute().rxSource == RadioRxAudioSource::LocalUsb) {
        QTimer::singleShot(500, this, [this]() {
            if (rigCaps && prefs && connStatus == connectionStatus_t::connConnected &&
                currentAudioRoute().rxSource == RadioRxAudioSource::LocalUsb) {
                setupUsbAudioBridge();
            }
        });
    } else if (rigCaps && currentAudioRoute().txInput == TxAudioInput::LocalInput) {
        setupLocalTxBridge();
    }


    /*
    // Convenient place to stop the connection timer as rigCaps has changed.
    ConnectionTimer.stop();

    //bandbtns->acceptRigCaps(rigCaps);

    if(caps == nullptr)
    {
        // Note: This line makes it difficult to accept a different radio connecting.
        return;
    } else {

        // Enable all controls
        enableControls(true);

        if (prefs->manufacturer == manufIcom)
            showStatusBarText(QString("Found radio of name %0 and model ID %1.").arg(rigCaps->modelName).arg(rigCaps->modelID,2,16,QChar('0')));
        else
            showStatusBarText(QString("Found radio of name %0 and model ID %1.").arg(rigCaps->modelName).arg(rigCaps->modelID));

        qDebug(logSystem()) << "Rig name: " << rigCaps->modelName;
        qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps->hasLan;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectLenMax: " << rigCaps->spectLenMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectAmpMax: " << rigCaps->spectAmpMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectDbPerDN: " << rigCaps->spectDbPerDN;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectSeqMax: " << rigCaps->spectSeqMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: hasSpectrum: " << rigCaps->hasSpectrum;

        configureVFOs(); // Now we have a rig connection, need to configure the VFOs

        rigName->setText(rigCaps->modelName);
        setRigModelName(rigCaps->modelName);
        if (serverConfig.enabled) {
            serverConfig.rigs.first()->modelName = rigCaps->modelName;
            serverConfig.rigs.first()->rigName = rigCaps->modelName;
            serverConfig.rigs.first()->civAddr = rigCaps->modelID;
            serverConfig.rigs.first()->baudRate = rigCaps->baudRate;
        }
        setWindowTitle(rigCaps->modelName);

        if(rigCaps->hasSpectrum)
        {
            for (const auto& receiver: receivers)
            {
                receiver->prepareScope(rigCaps->spectAmpMax, rigCaps->spectLenMax);
            }
        }

        ui->transmitBtn->setEnabled(rigCaps->hasTransmit);
        ui->micGainSlider->setEnabled(rigCaps->hasTransmit);
        ui->txPowerSlider->setEnabled(rigCaps->hasTransmit);

        if (rigCaps->commands.contains(funcSendCW)) {
            // We have a send CW function, so enable the window.
            cw = new cwSender(this,rig);
            cw->setCutNumbers(prefs->cwCutNumbers);
            cw->setSendImmediate(prefs->cwSendImmediate);
            cw->setSidetoneEnable(prefs->cwSidetoneEnabled);
            cw->setSidetoneLevel(prefs->cwSidetoneLevel);
            cw->setMacroText(prefs->cwMacroList);
        }

        ui->cwButton->setEnabled(rigCaps->commands.contains(funcSendCW));
        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));
        ui->monitorSlider->setEnabled(rigCaps->commands.contains(funcMonitorGain));
        ui->rfGainSlider->setEnabled(rigCaps->commands.contains(funcRfGain));

        // Only show settingsgroup if rig has sub
        ui->scopeSettingsGroup->setVisible(rigCaps->commands.contains(funcVFODualWatch)||rigCaps->commands.contains(funcVFOBandMS));

        ui->scopeDualBtn->setVisible(rigCaps->commands.contains(funcVFODualWatch));
        ui->mainEqualsSubBtn->setVisible(rigCaps->commands.contains(funcVFOEqualMS));
        ui->swapMainSubBtn->setVisible(rigCaps->commands.contains(funcVFOSwapMS));
        //ui->mainSubTrackingBtn->setVisible(rigCaps->commands.contains(funcMainSubTracking));
        // Only show this split button on IC7610/IC785x
        ui->splitBtn->setVisible(rigCaps->commands.contains(funcSplitStatus) && rigCaps->commands.contains(funcVFOEqualMS));
        ui->antennaGroup->setVisible(rigCaps->commands.contains(funcAntenna) || rigCaps->commands.contains(funcRXAntenna));
        ui->preampAttGroup->setVisible(rigCaps->commands.contains(funcPreamp));
        //ui->dualWatchBtn->setVisible(rigCaps->hasCommand29);

        ui->nbEnableChk->setTristate(false);
        ui->nrEnableChk->setTristate(false);
        ui->nbEnableChk->setChecked(false);
        ui->nrEnableChk->setChecked(false);

        if (rigCaps->commands.contains(funcNoiseBlanker)) {
            qInfo () << "nb max val" << rigCaps->commands.find(funcNoiseBlanker)->maxVal;
            if (rigCaps->commands.find(funcNoiseBlanker)->maxVal > 1)
                ui->nbEnableChk->setTristate(true);
        }
        ui->nbEnableChk->setEnabled(rigCaps->commands.contains(funcNoiseBlanker));

        if (rigCaps->commands.contains(funcNoiseReduction)) {
            qInfo () << "nr max val" << rigCaps->commands.find(funcNoiseReduction)->maxVal;
            if (rigCaps->commands.find(funcNoiseReduction)->maxVal > 1)
                ui->nrEnableChk->setTristate(true);
        }

        if (rigCaps->commands.contains(funcRFPower)) {
            auto f = rigCaps->commands.find(funcRFPower);
            ui->txPowerSlider->setRange(f->minVal,f->maxVal);
        }

        if (rigCaps->commands.contains(funcMonitorGain)) {
            auto f = rigCaps->commands.find(funcMonitorGain);
            ui->monitorSlider->setRange(f->minVal,f->maxVal);
        }

        if (rigCaps->commands.contains(funcSquelch)) {
            auto f = rigCaps->commands.find(funcSquelch);
            ui->sqlSlider->setRange(f->minVal,f->maxVal);
        }

        ui->nrEnableChk->setEnabled(rigCaps->commands.contains(funcNoiseReduction));

        ui->ipPlusEnableChk->setEnabled(rigCaps->commands.contains(funcIPPlus));
        ui->compEnableChk->setEnabled(rigCaps->commands.contains(funcCompressor));
        ui->voxEnableChk->setEnabled(rigCaps->commands.contains(funcVox));
        ui->digiselEnableChk->setEnabled(rigCaps->commands.contains(funcDigiSel));

        for (const auto& receiver: receivers) {
            if (receiver->getReceiver() == 0) {
                // Report scope redraw time to Select Radio window (only scope 0)
                connect(receiver,SIGNAL(spectrumTime(double)),selRad,SLOT(spectrumTime(double)));
                connect(receiver,SIGNAL(waterfallTime(double)),selRad,SLOT(waterfallTime(double)));
                connect(receiver,SIGNAL(spectrumTime(double)),rig,SLOT(spectrumTime(double)));
                connect(receiver,SIGNAL(waterfallTime(double)),rig,SLOT(waterfallTime(double)));
            }
            if (rigCaps->commands.contains(funcDATA1Mod))
            {
                setupui->updateModSourceList(1, rigCaps->inputs);
            }

            if (rigCaps->commands.contains(funcDATA2Mod))
            {
                setupui->updateModSourceList(2, rigCaps->inputs);
            }

            if (rigCaps->commands.contains(funcDATA3Mod))
            {
                setupui->updateModSourceList(3, rigCaps->inputs);
            }

            setupui->enableModSource(0,rigCaps->commands.contains(funcDATAOffMod));
            setupui->enableModSource(1,rigCaps->commands.contains(funcDATA1Mod));
            setupui->enableModSource(2,rigCaps->commands.contains(funcDATA2Mod));
            setupui->enableModSource(3,rigCaps->commands.contains(funcDATA3Mod));

            // Disable unsupported mod sources
            for (const auto &r: rigCaps->inputs)
            {
                setupui->enableModSourceItem(0,r,rigCaps->commands.contains(funcDATAOffMod));
                setupui->enableModSourceItem(1,r,(rigCaps->commands.contains(funcDATA1Mod) && r.reg > -1) ? true : false);
                setupui->enableModSourceItem(2,r,(rigCaps->commands.contains(funcDATA2Mod) && r.reg > -1) ? true : false);
                setupui->enableModSourceItem(3,r,(rigCaps->commands.contains(funcDATA3Mod) && r.reg > -1) ? true : false);
            }

            if(rigCaps->hasSpectrum)
            {
                receiver->setRange(prefs->mainPlotFloor, prefs->mainPlotCeiling);
            }
        }

        // Set the tuning step combo box up:
        ui->tuningStepCombo->blockSignals(true);
        ui->tuningStepCombo->clear();
        for (auto &s: rigCaps->steps)
        {
            ui->tuningStepCombo->addItem(s.name, s.hz);
        }

        ui->tuningStepCombo->setCurrentIndex(2);
        ui->tuningStepCombo->blockSignals(false);

        setupui->updateModSourceList(0, rigCaps->inputs);

        ui->attSelCombo->clear();
        if(rigCaps->commands.contains(funcAttenuator))
        {
            ui->attSelCombo->setDisabled(false);
            for (auto &att: rigCaps->attenuators)
            {
                ui->attSelCombo->addItem(att.name,att.num);
            }
        } else {
            ui->attSelCombo->setDisabled(true);
        }

        ui->preampSelCombo->clear();
        if(rigCaps->commands.contains(funcPreamp))
        {
            ui->preampSelCombo->setDisabled(false);
            for (auto &pre: rigCaps->preamps)
            {
                ui->preampSelCombo->addItem(pre.name, pre.num);
            }
        } else {
            ui->preampSelCombo->setDisabled(true);
        }

        ui->antennaSelCombo->clear();
        if(rigCaps->commands.contains(funcAntenna))
        {
            ui->antennaSelCombo->setDisabled(false);
            for (auto &ant: rigCaps->antennas)
            {
                ui->antennaSelCombo->addItem(ant.name,ant.num);
            }
        } else {
            ui->antennaSelCombo->setDisabled(true);
        }

        ui->rxAntennaCheck->setEnabled(rigCaps->commands.contains(funcAntenna) || rigCaps->commands.contains(funcRXAntenna));
        ui->rxAntennaCheck->setChecked(false);

        ui->tuneEnableChk->setEnabled(rigCaps->commands.contains(funcTunerStatus));
        ui->tuneNowBtn->setEnabled(rigCaps->commands.contains(funcTunerStatus));

        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));

        ui->connectBtn->setText("Disconnect from Radio"); // We must be connected now.
        connStatus = connConnected;

        // Keep rigctld listening according to current LAN settings.
        setupRigCtlServer();

        if(prefs->enableLAN || prefs->wfShareEnabled)
        {
            setLocalAfGain(prefs->localAFgain);
        } else {
            // If not network connected, select the requested PTT type.
            emit setPTTType(prefs->pttType);
        }
        // Adding these here because clearly at this point we have valid
        // rig comms. In the future, we should establish comms and then
        // do all the initial grabs. For now, this hack of adding them here and there:
        // recalculate command timing now that we know the rig better:
        if(prefs->polling_ms != 0)
        {
            changePollTiming(prefs->polling_ms, true);
        } else {
            calculateTimingParameters();
        }

    }

    initPeriodicCommands();
    getInitialRigState();
    */
}

void MainController::receiveValueFromQueue(cacheItem val)
{
    uchar vfo=0;

    // Sometimes we can receive data before the rigCaps have been determined, so return if this happens:

    if (rigCaps == nullptr)
    {
        qWarning(logSystem()) << "Data received before we have rigCaps(), aborting";
        return;
    }
    else if (val.receiver >= receivers.size())
    {
        qWarning(logSystem()) << "Data received for Radio/VFO that doesn't exist!" << val.receiver << "(" << funcString[val.command] << ")";
        return;
    }

    switch (val.command)
    {
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcUnselectedFreq:
        vfo = 1;
    case funcFreqGet:
    case funcFreqTR:
    case funcFreq:
    case funcSelectedFreq:
    {
        const freqt frequency = val.value.value<freqt>();
        // Selected/Unselected Freq are fixed to VFO A/B on some rigs, so route to the active VFO
        if ((val.command == funcSelectedFreq || val.command == funcUnselectedFreq) && queue->vfoFreqSwapped())
            vfo ^= 1;
        if (vfo == 0)
            receivers[val.receiver]->receiveFrequencyA(frequency.Hz);
        else
            receivers[val.receiver]->receiveFrequencyB(frequency.Hz);
        if (val.receiver == 0 && vfo == 0 && !m_transmitting) {
            m_repeaterMainFrequency = frequency;
            m_repeaterHaveMainFrequency = true;
            emit repeaterStateChanged();
        }
        break;
    }
    case funcModeGet:
    case funcModeTR:
        // These commands don't include filter, so queue an immediate request for filter
        queue->addUnique(priorityImmediate,funcDataModeWithFilter,false,0);
    case funcDataModeWithFilter:
    case funcUnselectedMode:
        if (val.command == funcUnselectedMode)
            vfo=1;
    case funcDataMode:
    case funcSelectedMode:
    case funcMode:
    {
        modeInfo m = val.value.value<modeInfo>();
        receivers[val.receiver]->receiveMode(m,vfo);
        if (val.receiver == currentReceiver && vfo == 0) {
            m_cwSender->handleCurrentModeUpdate(m.mk);
            // Notify the ANR engine so it can swap in the saved noise profile
            // for this mode (or save the current one before switching away).
            if (rxProc && !m.name.isEmpty())
                rxProc->setRxMode(m.name);
        }
        if (val.receiver == 0 && vfo == 0 && !m_transmitting) {
            const bool warn = m.mk != modeUnknown && m.mk != modeFM;
            if (m_repeaterWarnNotFm != warn) {
                m_repeaterWarnNotFm = warn;
                emit repeaterStateChanged();
            }
        }
        //qDebug() << funcString[val.command] << "receiver:" << val.receiver << "vfo:" << vfo << "mk:" << m.mk << "name:" << m.name << "data:" << m.data << "filter:" << m.filter;

        break;
    }
    case funcTXFreq:
        // Not sure if we want to do anything with this? M0VSE
        break;
    case funcVFODualWatch:
    {
        const bool enabled = val.value.value<bool>();
        if (m_dualWatch != enabled) {
            m_dualWatch = enabled;
            emit dualWatchChanged();
        }
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcVFOBandMS:
    {
        // This indicates whether main or sub is currently "active"
        // Swap recievers if necessary.
        uchar r = val.value.value<uchar>();
        if (currentReceiver != r)
        {
            // VFO has swapped, make sure scope follows if we only have a single scope.
            // Th radio doesn't do this, but I don't see why we would want it without a scope?
            if (!rigCaps->hasCommand29 && rigCaps->commands.contains(funcScopeMainSub))
                queue->add(priorityImmediate,queueItem(funcScopeMainSub,val.value,false,0));

            for (const auto& rx: receivers)
            {
                rx->setActive(rx->getReceiver() == r);
            }
            currentReceiver = r;
            emit currentReceiverChanged();
            updatePrimaryMeterForTransmit(m_transmitting);
        }
        break;
    }
    case funcSatelliteMode:
    {
        const bool enabled = val.value.value<bool>();
        for (const auto &receiver : std::as_const(receivers))
            receiver->setSatelliteMode(enabled, false);
        // If satellite mode is enabled, disable mode/freq query commands.
        /*
        for (auto r: receivers){
            if (val.value.value<bool>()) {
                queue->del(funcUnselectedMode,r->getReceiver());
                queue->del(funcUnselectedFreq,r->getReceiver());
            }
            //r->setSatMode(val.value.value<bool>());
        }
        */
        //qInfo(logRig()) << "Is radio currently in satellite mode?" << val.value.value<bool>();
        break;
    }
    case funcSatelliteMemory:
    case funcMemoryContents:
        emit memoryReceived(val.value.value<memoryType>());
        break;
    case funcMemoryTag:
    case funcMemoryTagB:
        emit memoryNameReceived(val.value.value<memoryTagType>());
        break;
    case funcSplitMemory:
        emit memorySplitReceived(val.value.value<memorySplitType>());
        break;
    case funcMemoryClear:
    case funcMemoryKeyer:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        updateRepeaterOffset(val.value.value<freqt>());
        break;
    case funcSplitStatus:
    {
        const auto mode = val.value.value<duplexMode_t>();
        m_repeaterDuplexMode = mode;
        const bool enabled = mode != dmSplitOff;
        if (m_splitEnabled != enabled) {
            m_splitEnabled = enabled;
            emit splitEnabledChanged();
        }
        emit repeaterStateChanged();
        for (const auto &receiver : std::as_const(receivers))
            receiver->setSplitEnabled(enabled, false);
        //receivers[val.receiver]->setSplit(val.value.value<duplexMode_t>()==dmSplitOn?true:false);
        break;
    }
    case funcQuickSplit:
    {
        const bool enabled = val.value.value<bool>();
        if (m_repeaterQuickSplit != enabled) {
            m_repeaterQuickSplit = enabled;
            emit repeaterStateChanged();
        }
        break;
    }
    case funcTuningStep:
    {
        auto it = std::find_if(rigCaps->steps.begin(), rigCaps->steps.end(),
                               [val](const stepType &s) {
                                   return s.num == val.value.value<uchar>();   // change condition
                               });
        if (it != rigCaps->steps.end() && it->hz != stepSize && it->num != 0) {
            stepSize = it->hz;
            for (auto *r : std::as_const(receivers)) {
                if (r) r->receiveStepSize(stepSize);
            }
            emit stepSizeChanged();
        }
        break;
    }
    case funcAttenuator:
        receivers[val.receiver]->setAttenuator(val.value.value<uchar>(),false);
        break;
    case funcAntenna:
        receivers[val.receiver]->setAntenna(val.value.value<antennaInfo>().antenna,false);
        receivers[val.receiver]->setRxAntenna(val.value.value<antennaInfo>().rx,false);
        break;
    case funcPBTOuter:
        receivers[val.receiver]->setPbtOuter(val.value.value<ushort>(),false);
        break;
    case funcPBTInner:
        receivers[val.receiver]->setPbtInner(val.value.value<ushort>(),false);
        break;
    case funcIFShift:
        receivers[val.receiver]->setIfShift(val.value.value<ushort>(),false);
        break;
        /*
    connect(this->rig, &rigCommander::haveDashRatio,
        [=](const quint8& ratio) { cw->handleDashRatio(ratio); });
    connect(this->rig, &rigCommander::haveCWBreakMode,
            [=](const quint8 &bm) { cw->handleBreakInMode(bm);});
*/
    case funcCwPitch:

        // There is only a single CW Pitch setting, so send to all scopes
        // Also send to CW window
        {
            const quint16 pitch = val.value.value<quint16>();
            for (auto *receiver : receivers) {
                if (receiver)
                    receiver->receiveCwPitch(pitch);
            }
            m_cwSender->handlePitch(pitch);
        }
        break;

    case funcMicGain:
        if (val.receiver == currentReceiver) {
            const int value = val.value.value<ushort>();
            if (m_micGain != value) {
                m_micGain = value;
                emit micGainChanged();
            }
        }
        break;
    case funcKeySpeed:
        // Only used by CW window
        m_cwSender->handleKeySpeed(val.value.value<uchar>());
        break;
    case funcNotchFilter:
        break;
    case funcAfGain:
        receivers[val.receiver]->setAfGain(val.value.value<ushort>(),false);
        break;
    case funcMonitorGain:
        if (val.receiver == currentReceiver) {
            const int value = val.value.value<ushort>();
            if (m_monitorGain != value) {
                m_monitorGain = value;
                emit monitorGainChanged();
            }
        }
        break;
    case funcRfGain:
        receivers[val.receiver]->setRfGain(val.value.value<ushort>(),false);
        break;
    case funcSquelch:
        receivers[val.receiver]->setSquelch(val.value.value<ushort>(),false);
        break;
    case funcRFPower:
    {
        const int value = val.value.value<ushort>();
        if (m_txPower != value) {
            m_txPower = value;
            emit txPowerChanged();
        }
        break;
    }
    case funcNBLevel:
        receivers[val.receiver]->setNbLevel(val.value.value<ushort>(),false);
        break;
    case funcNRLevel:
        receivers[val.receiver]->setNrLevel(val.value.value<ushort>(),false);
        break;
    case funcCompressorLevel:
    case funcAPFLevel:
    case funcDriveGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        break;
    case funcBreakInDelay:
        break;
    case funcDigiSelShift:
        break;
    // 0x15 Meters
    case funcSMeterSqlStatus:
        break;
    case funcSMeter:
        receivers[val.receiver]->receiveMeter(meter_t::meterS,val.value.value<double>());
        break;
    case funcAbsoluteMeter:
    {
        meterkind m = val.value.value<meterkind>();
        receiveOptionalMeter(m.type, m.value);
        break;
    }
    case funcMeterType:
    {
        /*
        meter_t m = val.value.value<meter_t>();
        if (ui->meterSPoWidget->getMeterType() != m)
            changeMeterType(m, 1);
        break;
        */
    }
    case funcVariousSql:
        break;
    case funcOverflowStatus:
        //receivers[val.receiver]->overflow(val.value.value<bool>());
        break;
    case funcCenterMeter:
        receiveOptionalMeter(meter_t::meterCenter, val.value.value<double>());
        break;
    case funcPowerMeter:
        if (val.receiver == currentReceiver)
            receivers[val.receiver]->receiveMeter(meter_t::meterPower, val.value.value<double>());
        receiveOptionalMeter(meter_t::meterPower, val.value.value<double>());
        break;
    case funcSWRMeter:
        receiveOptionalMeter(meter_t::meterSWR, val.value.value<double>());
        break;
    case funcALCMeter:
        receiveOptionalMeter(meter_t::meterALC, val.value.value<double>());
        break;
    case funcCompMeter:
        receiveOptionalMeter(meter_t::meterComp, val.value.value<double>());
        break;
    case funcVdMeter:
        receiveOptionalMeter(meter_t::meterVoltage, val.value.value<double>());
        break;
    case funcIdMeter:
        receiveOptionalMeter(meter_t::meterCurrent, val.value.value<double>());
        break;
    // 0x16 enable/disable functions:
    case funcPreamp:
        receivers[val.receiver]->setPreamp(val.value.value<uchar>(),false);
        break;
    case funcAGC:
        receivers[val.receiver]->setAgc(val.value.value<uchar>(),false);
        break;
    case funcAGCTimeConstant:
        break;
    case funcNoiseBlanker:
        receivers[val.receiver]->setNb(val.value.value<uchar>(),false);

        /*
        if (val.receiver == currentReceiver) {
            ui->nbEnableChk->blockSignals(true);
            if (ui->nbEnableChk->isTristate())
                ui->nbEnableChk->setCheckState(Qt::CheckState(val.value.value<uchar>()));
            else
                ui->nbEnableChk->setChecked(val.value.value<bool>());

            ui->nbEnableChk->blockSignals(false);
        }
        */
        break;
    case funcAudioPeakFilter:
        break;
    case funcFilterShape:
        //if (rigCaps->manufacturer == manufIcom || receivers[val.receiver]->getFilter() == val.value.value<uchar>() / 10)
        //{
            receivers[val.receiver]->setFilterShape(val.value.value<uchar>() % 10, false);
        //}
        break;
    case funcRoofingFilter:
        //if (rigCaps->manufacturer == manufIcom || receivers[val.receiver]->getFilter() == val.value.value<uchar>() / 10)
        //{
            receivers[val.receiver]->setRoofing(val.value.value<uchar>() % 10, false);
        //}
        break;
    case funcNoiseReduction:
        receivers[val.receiver]->setNr(val.value.value<uchar>(),false);
        /*
        if (val.receiver == currentReceiver) {
            ui->nrEnableChk->blockSignals(true);
            if (ui->nrEnableChk->isTristate())
                ui->nrEnableChk->setCheckState(Qt::CheckState(val.value.value<uchar>()));
            else
                ui->nrEnableChk->setChecked(val.value.value<bool>());

            ui->nrEnableChk->blockSignals(false);
        }
        */
        break;
    case funcAutoNotch:
        break;
    case funcRepeaterTone:
        updateRepeaterToneMode(val.value.value<bool>() ? ratrTONEon : ratrTONEoff);
        break;
    case funcRepeaterTSQL:
        updateRepeaterToneMode(val.value.value<bool>() ? ratrTSQLon : ratrTSQLoff);
        break;
    case funcRepeaterDTCS:
        break;
    case funcRepeaterCSQL:
        break;
    case funcCompressor:
        if (val.receiver == currentReceiver) {
            const bool enabled = val.value.value<bool>();
            if (m_compressorEnabled != enabled) {
                m_compressorEnabled = enabled;
                emit compressorEnabledChanged();
            }
        }
        break;
    case funcMonitor:
    {
        const bool enabled = val.value.value<bool>();
        if (m_monitorEnabled != enabled) {
            m_monitorEnabled = enabled;
            emit monitorEnabledChanged();
        }
        break;
    }
    case funcVox:
    {
        const bool enabled = val.value.value<bool>();
        if (m_voxEnabled != enabled) {
            m_voxEnabled = enabled;
            emit voxEnabledChanged();
        }
        break;
    }
    case funcManualNotch:
        receivers[val.receiver]->setMn(val.value.value<bool>(),false);
        break;
    case funcDigiSel:
        receivers[val.receiver]->setDs(val.value.value<bool>(),false);
        break;
    case funcTwinPeakFilter:
        break;
    case funcDialLock:
        break;
    case funcRXAntenna:
        receivers[val.receiver]->setRxAntenna(val.value.value<bool>(),false);
        break;
    case funcManualNotchWidth:
        break;
    case funcSSBTXBandwidth:
        break;
    case funcMainSubTracking:

        //ui->mainSubTrackingBtn->setChecked(val.value.value<bool>());
        //for (const auto& receiver:receivers)
        //{
        //    receiver->setTracking(val.value.value<bool>());
        //}
        break;
    case funcToneSquelchType:
        updateRepeaterToneMode(val.value.value<rptrAccessData>().accessMode);
        break;
    case funcIPPlus:
        receivers[val.receiver]->setIpPlus(val.value.value<bool>(),false);
        break;
    case funcBreakIn:
        m_cwSender->handleBreakInMode(val.value.value<uchar>());
        break;
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 it automatically added.
    case funcTransceiverId:
        break;
    // 0x1a
    case funcBandStackReg:
    {
        bandStackType bsr = val.value.value<bandStackType>();
        for (const auto& r: receivers)
        {
            r->receiveBSR(bsr);
        }
        /*

        queue->add(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,val.receiver,true).freqFunc,
                                                QVariant::fromValue<freqt>(bsr.freq),false,uchar(val.receiver)));

        for (auto &md: rigCaps->modes)
        {
            if (md.reg == bsr.mode) {
                modeInfo m(md);
                m.filter=bsr.filter;
                m.data=bsr.data;
                qDebug(logRig()) << __func__ << "Setting Mode/Data for new mode" << m.name << "data" << m.data << "filter" << m.filter << "reg" << m.reg;
                queue->add(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,val.receiver,true).modeFunc,
                                                        QVariant::fromValue<modeInfo>(m),false,uchar(val.receiver)));
                break;
            }
        }
        */
        break;
    }
    case funcFilterWidth:
        receivers[val.receiver]->setPassbandWidth(val.value.value<ushort>());
        break;

    case funcAFMute:
        break;
    // 0x1a 0x05 various registers!
    case funcREFAdjust:
        break;
    case funcREFAdjustFine:
        //break;
    case funcACCAModLevel:
        processModLevel(inputACCA, val.value.value<uchar>());
        break;
    case funcACCBModLevel:
        processModLevel(inputACCB, val.value.value<uchar>());
        break;
    case funcUSBModLevel:
        processModLevel(inputUSB, val.value.value<uchar>());
        break;
    case funcSPDIFModLevel:
        processModLevel(inputSPDIF, val.value.value<uchar>());
        break;
    case funcLANModLevel:
        processModLevel(inputLAN, val.value.value<uchar>());
        break;
    case funcDATAOffMod:
        receiveModInput(val.value.value<rigInput>(), 0);
        break;
    case funcDATA1Mod:
        receiveModInput(val.value.value<rigInput>(), 1);
        break;
    case funcDATA2Mod:
        receiveModInput(val.value.value<rigInput>(), 2);
        break;
    case funcDATA3Mod:
        receiveModInput(val.value.value<rigInput>(), 3);
        break;
    case funcDashRatio:
        m_cwSender->handleDashRatio(val.value.value<uchar>());
        break;
    case funcTXFreqMon:
        break;
    // 0x1b register
    case funcToneFreq:
        m_repeaterTone = val.value.value<toneInfo>().tone;
        emit repeaterStateChanged();
        break;
    case funcTSQLFreq:
        m_repeaterTsql = val.value.value<toneInfo>().tone;
        emit repeaterStateChanged();
        break;
    case funcDTCSCode:
    {
        toneInfo t = val.value.value<toneInfo>();
        m_repeaterDtcs = t.tone;
        m_repeaterDtcsTxInvert = t.tinv;
        m_repeaterDtcsRxInvert = t.rinv;
        emit repeaterStateChanged();
        break;
    }
    case funcCSQLCode:
        break;
    // 0x1c register
    case funcRitStatus:
    {
        const bool enabled = val.value.value<bool>();
        if (m_ritEnabled != enabled) {
            m_ritEnabled = enabled;
            emit ritEnabledChanged();
        }
        break;
    }
    case funcTransceiverStatus:
    {
        const bool enabled = val.value.value<bool>();
        if (m_transmitting != enabled) {
            m_transmitting = enabled;
            // The radio has returned to receive, however that happened.
            if (!enabled)
                m_pttTimeoutTimer.stop();
            emit transmittingChanged();
            updatePrimaryMeterForTransmit(enabled);
        }
        break;
    }
    case funcTunerStatus:
    {
        const bool enabled = val.value.value<uchar>() != 0;
        if (m_tunerEnabled != enabled) {
            m_tunerEnabled = enabled;
            emit tunerEnabledChanged();
        }
        break;
    }
    // 0x21 RIT:
    case funcRitFreq:
    {
        const int value = val.value.value<short>();
        if (m_ritUserChange.isValid() && m_ritUserChange.elapsed() < ritHoldoffMs)
            break;
        if (m_ritFrequency != value) {
            m_ritFrequency = value;
            emit ritFrequencyChanged();
        }
        break;
    }
    case funcRitTXStatus:
        // Not sure what this is used for?
        break;
    // 0x27
    case funcScopeWaveData:
        if (val.receiver >= receivers.size() || receivers[val.receiver] == nullptr) {
            qWarning(logSystem()) << "Ignoring scope data for invalid receiver" << val.receiver
                                  << "receiver count" << receivers.size();
            break;
        }
        QMetaObject::invokeMethod(receivers[val.receiver], "setScopeData",
                                  Qt::QueuedConnection,
                                  Q_ARG(scopeData, val.value.value<scopeData>()));

        break;
    case funcScopeOnOff:
        // confirming scope is on
        break;
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
        break;
    case funcScopeMainSub:
    {
        // Has the primary scope changed?
        break;
    }
    case funcScopeSingleDual:
    {
        const bool enabled = val.value.value<bool>();
        if (m_dualScope != enabled) {
            m_dualScope = enabled;
            emit dualScopeChanged();
        }
        break;
    }
    case funcScopeMode:
        receivers[val.receiver]->setScopeMode(val.value.value<uchar>(),false);
        break;
    case funcScopeSpan:
        receivers[val.receiver]->setScopeSpan(val.value.value<centerSpanData>().reg,false);
        break;
    case funcScopeEdge:
        receivers[val.receiver]->setScopeEdge(val.value.value<uchar>(),false);
        break;
    case funcScopeHold:
        receivers[val.receiver]->setHold(val.value.value<bool>(), false);
        break;
    case funcScopeRef:
        receivers[val.receiver]->setRef(val.value.value<int>(),false);
        break;
    case funcScopeSpeed:
        receivers[val.receiver]->setSpeed(val.value.value<uchar>(),false);
        break;
    case funcScopeDuringTX:
    case funcScopeCenterType:
    case funcScopeVBW:
    case funcScopeFixedEdgeFreq:
    case funcScopeRBW:
        break;
    // 0x28
    case funcVoiceTX:
        break;
    //0x29 - Prefix certain commands with this to get/set certain values without changing current VFO
    // If we use it for get commands, need to parse the \x29\x<VFO> first.
    case funcMainSubPrefix:
        break;
    case funcPowerControl:
        // We could indicate the rig being powered-on somehow?
        break;
    case funcCWDecode:
        if (m_cwSender)
            m_cwSender->receive(val.value.value<QString>());
        break;
    case funcCWDecodeEnable:
        if (m_cwSender)
            m_cwSender->handleCwDecodeEnable(val.value.value<bool>());
        break;
    case funcCWDecodeDisplay:
        if (m_cwSender)
            m_cwSender->handleCwDecodeDisplay(val.value.value<bool>());
        break;
    case funcCWDecodeThreshold:
        if (m_cwSender)
            m_cwSender->handleCwDecodeThreshold(val.value.value<uchar>());
        break;
    case funcCWDecodeFilter:
        if (m_cwSender)
            m_cwSender->handleCwDecodeFilter(val.value.value<uchar>());
        break;
    default:
        //qWarning(logSystem()) << "Unhandled command received from rigcommander()" << funcString[val.command] << "Contact support!";
        break;
    }
}

/*
void MainController::setDefPrefs()
{
    defprefs->hasRunSetup = false;
    defprefs->useFullScreen = false;
    defprefs->useSystemTheme = false;
    defprefs->drawPeaks = true;
    defprefs->currentColorPresetNumber = 0;
    defprefs->underlayMode = underlayAverage;
    defprefs->underlayBufferSize = 80;
    defprefs->peakDecay = 50;
    defprefs->wfEnable = 2;
    defprefs->wfAntiAlias = false;
    defprefs->wfInterpolate = true;
    defprefs->stylesheetPath = QString("qdarkstyle/style.qss");
    defprefs->radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defprefs->CIVisRadioModel = false;
    defprefs->pttType = pttCIV;
    defprefs->serialPortRadio = QString("auto");
    defprefs->serialPortBaud = 115200;
    defprefs->enableLAN = false;
    defprefs->polling_ms = 0; // 0 = Automatic
    defprefs->enablePTT = true;
    defprefs->pttTimeoutEnabled = true;
    defprefs->pttTimeoutSeconds = 300;
    defprefs->niceTS = true;
    defprefs->enableRigCtlD = false;
    defprefs->rigCtlPort = 4533;
    defprefs->rigCtlIntegerFreqs = false;
    defprefs->virtualSerialPort = QString("none");
    defprefs->virtualSerialPortUseQueue = false;
    defprefs->localAFgain = 255;
    defprefs->mainWflength = 160;
    defprefs->mainWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defprefs->mainPlotFloor = 0;
    defprefs->mainPlotCeiling = 160;
    defprefs->subWflength = 160;
    defprefs->subWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defprefs->subPlotFloor = 0;
    defprefs->subPlotCeiling = 160;
    defprefs->scopeScrollX = 120;
    defprefs->scopeScrollY = 120;
    defprefs->confirmExit = true;
    defprefs->confirmPowerOff = true;
    defprefs->confirmSettingsChanged = true;
    defprefs->confirmMemories = false;
    defprefs->meter1Type = meterS;
    defprefs->meter2Type = meterNone;
    defprefs->meter3Type = meterNone;
    defprefs->compMeterReverse = false;
    defprefs->region = "1";
    defprefs->showBands = true;
    defprefs->miniRxButtons = true;
    defprefs->showTuningDial = true;
    defprefs->wideRitSlider = false;
    defprefs->alwaysSaveOnExit = false;
    defprefs->warnAudioDeviceProblems = true;
    defprefs->reverseWheel = false;
    defprefs->sMeterOnTop = false;
    defprefs->meterStyle = 0;
    defprefs->popoutMeter = false;
    defprefs->popoutMeterStyle = 1;
    defprefs->popoutSecondaryVfo = false;
    defprefs->popoutMeterPrimary = 1;
    defprefs->popoutMeterSecondary = 0;
    defprefs->popoutMeter2Primary = 1;
    defprefs->popoutMeter2Secondary = 0;
    defprefs->popoutMeterWidth = 0;
    defprefs->popoutMeterHeight = 0;
    defprefs->largeFreqDisplay = false;
    defprefs->largeOptionalMeters = false;
    defprefs->freqFont = QStringLiteral(FCTL_DEFAULT_FONT);
    defprefs->manufacturer = manufIcom;
    defprefs->useUTC = false;
    defprefs->setRadioTime = false;
    defprefs->forceVfoMode = true;
    defprefs->autoPowerOn=true;

    defprefs->tcpPort = 0;
    defprefs->tciPort = 50001;
    defprefs->clusterUdpEnable = false;
    defprefs->clusterTcpEnable = false;
    defprefs->waterfallFormat = 0;
    defprefs->audioSystem = qtAudio;
    defprefs->enableUSBControllers = false;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    defprefs->groupSeparator = QLocale().groupSeparator();
    defprefs->decimalSeparator = QLocale().decimalPoint();
#else
    defprefs->groupSeparator = QLocale().groupSeparator().at(0);       // digit group separator
    defprefs->decimalSeparator = QLocale().decimalPoint().at(0);       // digit group separator
#endif

    // Audio
    defprefs->rxSetup.latency = 150;
    defprefs->txSetup.latency = 150;
    defprefs->rxSetup.isinput = false;
    defprefs->txSetup.isinput = true;
    defprefs->rxSetup.sampleRate = 48000;
    defprefs->rxSetup.codec = 4;
    defprefs->txSetup.codec = 4;
    defprefs->rxSetup.resampleQuality = 4;
    defprefs->rxSetup.type = qtAudio;
    defprefs->txSetup.type = qtAudio;

    // Cluster
    defprefs->clusterUdpEnable = false;
    defprefs->clusterTcpEnable = false;
    defprefs->clusterUdpPort = 12060;

    // CW
    defprefs->cwCutNumbers=false;
    defprefs->cwSendImmediate=false;
    defprefs->cwSidetoneEnabled=true;
    defprefs->cwSidetoneLevel=100;

    udpDefprefs->ipAddress = QString("");
    udpDefprefs->controlLANPort = 50001;
    udpDefprefs->serialLANPort = 50002;
    udpDefprefs->audioLANPort = 50003;
    udpDefprefs->scopeLANPort = 50004;
    udpDefprefs->username = QString("");
    udpDefprefs->password = QString("");
    udpDefprefs->clientName = QHostInfo::localHostName();
    udpDefprefs->connectionType = connectionLAN;
    udpDefprefs->adminLogin = false;

    // Create color presets, set default colors for preset 0 and empty struct for others
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        colorPrefsType *p = &colorPreset[pn];

        // Begin every parameter with these safe defaults first:
        if(p->presetName == nullptr)
        {
            p->presetName = new QString();
        }
        p->presetName->clear();
        p->presetName->append(QString("%1").arg(pn));
        p->presetNum = pn;
        p->gridColor = QColor(0,0,0,255);
        p->axisColor = QColor(Qt::white);
        p->textColor = QColor(Qt::white);
        p->spectrumLine = QColor(Qt::yellow);
        p->spectrumFill = QColor("transparent");
        p->underlayLine = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150).lighter(200);
        p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
        p->plotBackground = QColor(Qt::black);
        p->tuningLine = QColor(Qt::blue);
        p->passband = QColor(Qt::blue);
        p->pbt = QColor(0x32,0xff,0x00,0x00);
        p->meterLevel = QColor(0x14,0x8c,0xd2).darker();
        p->meterAverage = QColor(0x2f,0xb7,0xcd);
        p->meterPeakLevel = QColor(0x3c,0xa0,0xdb).lighter();
        p->meterPeakScale = QColor(Qt::red);
        p->meterLowerLine = QColor(0xed,0xf0,0xf1);
        p->meterLowText = QColor(0xef,0xf0,0xf1);

        p->wfBackground = QColor(Qt::black);
        p->wfAxis = QColor(Qt::white);
        p->wfGrid = QColor(Qt::white);
        p->wfText = QColor(Qt::white);

        p->clusterSpots = QColor(Qt::red);

        p->buttonOff = QColor("transparent");
        p->buttonOn = QColor(0x50,0x50,0x50);

        switch (pn)
        {
        case 0:
        {
            // Dark
            p->presetName->clear();
            p->presetName->append("Dark");
            p->plotBackground = QColor(0,0,0,255);
            p->axisColor = QColor(Qt::white);
            p->textColor = QColor(255,255,255,255);
            p->gridColor = QColor(0x5E,0x5C,0x64,0xFF);
            p->spectrumFill = QColor("transparent");
            p->spectrumLine = QColor(Qt::yellow);
            p->underlayLine = QColor(0x96,0x33,0xff,0xff);
            p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
            p->tuningLine = QColor(0xff,0x55,0xff,0xff);
            p->passband = QColor(0x32,0xff,0xff,0x80);
            p->pbt = QColor(0x32,0xff,0x00,0x00);

            p->meterLevel = QColor(0x14,0x8c,0xd2).darker();
            p->meterAverage = QColor(0x3f,0xb7,0xcd);
            p->meterPeakScale = QColor(Qt::red);
            p->meterPeakLevel = QColor(0x3c,0xa0,0xdb).lighter();
            p->meterLowerLine = QColor(0xef,0xf0,0xf1);
            p->meterLowText = QColor(0xef,0xf0,0xf1);

            p->wfBackground = QColor(Qt::black);
            p->wfAxis = QColor(Qt::white);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::white);
            p->clusterSpots = QColor(Qt::red);
            p->buttonOff = QColor("transparent");
            p->buttonOn = QColor(0x50,0x50,0x50);
            break;
        }
        case 1:
        {
            // Bright
            p->presetName->clear();
            p->presetName->append("Bright");
            p->plotBackground = QColor(Qt::white);
            p->axisColor = QColor(200,200,200,255);
            p->gridColor = QColor(255,255,255,0);
            p->textColor = QColor(Qt::black);
            p->spectrumFill = QColor("transparent");
            p->spectrumLine = QColor(Qt::black);
            p->underlayLine = QColor(Qt::blue);
            p->tuningLine = QColor(Qt::darkBlue);
            p->passband = QColor(0x64,0x00,0x00,0x80);
            p->pbt = QColor(0x32,0xff,0x00,0x00);

            p->meterAverage = QColor(0x3f,0xb7,0xcd);
            p->meterPeakLevel = QColor(0x3c,0xa0,0xdb);
            p->meterPeakScale = QColor(Qt::darkRed);
            p->meterLowerLine = QColor(Qt::black);
            p->meterLowText = QColor(Qt::black);

            p->wfBackground = QColor(Qt::white);
            p->wfAxis = QColor(200,200,200,255);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::black);
            p->clusterSpots = QColor(Qt::red);
            p->buttonOff = QColor("transparent");
            p->buttonOn = QColor(0x50,0x50,0x50);
            break;
        }

        case 2:
        case 3:
        case 4:
        default:
            break;

        }
    }
}

void MainController::loadSettings(QString settingsFile)
{
    if (settingsFile.isNull()) {
        // No settingsFile provided so use default QSettings for current platform
        settings = new QSettings();
    }
    else
    {
        QString file = settingsFile;
        QFile info(settingsFile);
        QString path="";
        if (!QFileInfo(info).isAbsolute())
        {
            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (path.isEmpty())
            {
                path = QDir::homePath();
            }
            path = path + "/";
            file = info.fileName();
        }
        settings = new QSettings(path + file, QSettings::Format::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        settings->setIniCodec("UTF-8");
#endif
    }

    qInfo(logSystem()) << "Loading settings from " << settings->fileName();

    QString currentVersionString = QString(WFVIEW_VERSION);
    float currentVersionFloat = currentVersionString.toFloat();

    settings->beginGroup("Program");
    QString priorVersionString = settings->value("version", "unset").toString();
    float priorVersionFloat = priorVersionString.toFloat();
    if (currentVersionString != priorVersionString)
    {
        qWarning(logSystem()) << "Settings previously saved under version " << priorVersionString << ", you should review your settings and press SaveSettings.";
    }
    if(priorVersionFloat > currentVersionFloat)
    {
        qWarning(logSystem()).noquote().nospace() << "It looks like the previous version of wfview (" << priorVersionString << ") was newer than this version (" << currentVersionString << ")";
    }
    prefs->version = priorVersionString;
    prefs->majorVersion = settings->value("majorVersion", defprefs->majorVersion).toInt();
    prefs->minorVersion = settings->value("minorVersion", defprefs->minorVersion).toInt();
    prefs->hasRunSetup = settings->value("hasRunSetup", defprefs->hasRunSetup).toBool();
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    prefs->useFullScreen = settings->value("UseFullScreen", defprefs->useFullScreen).toBool();
    prefs->useSystemTheme = settings->value("UseSystemTheme", defprefs->useSystemTheme).toBool();
    prefs->wfEnable = settings->value("WFEnable", defprefs->wfEnable).toInt();
    //ui->scopeEnableWFBtn->setCheckState(Qt::CheckState(prefs->wfEnable));
    prefs->mainWfTheme = settings->value("MainWFTheme", defprefs->mainWfTheme).toInt();
    prefs->subWfTheme = settings->value("SubWFTheme", defprefs->subWfTheme).toInt();
    prefs->mainPlotFloor = settings->value("MainPlotFloor", defprefs->mainPlotFloor).toInt();
    prefs->subPlotFloor = settings->value("SubPlotFloor", defprefs->subPlotFloor).toInt();
    prefs->mainPlotCeiling = settings->value("MainPlotCeiling", defprefs->mainPlotCeiling).toInt();
    prefs->subPlotCeiling = settings->value("SubPlotCeiling", defprefs->subPlotCeiling).toInt();
    prefs->scopeScrollX = settings->value("scopeScrollX", defprefs->scopeScrollX).toInt();
    prefs->scopeScrollY = settings->value("scopeScrollY", defprefs->scopeScrollY).toInt();
    prefs->decimalSeparator = settings->value("DecimalSeparator", defprefs->decimalSeparator).toChar();
    prefs->groupSeparator = settings->value("GroupSeparator", defprefs->groupSeparator).toChar();
    prefs->forceVfoMode =  settings->value("ForceVfoMode", defprefs->groupSeparator).toBool();
    prefs->autoPowerOn =  settings->value("AutoPowerOn", defprefs->autoPowerOn).toBool();

    prefs->drawPeaks = settings->value("DrawPeaks", defprefs->drawPeaks).toBool();
    prefs->underlayBufferSize = settings->value("underlayBufferSize", defprefs->underlayBufferSize).toInt();
    {
        int modeVal = settings->value("underlayMode", defprefs->underlayMode).toInt();
        if (modeVal < 0 || modeVal > 2) modeVal = underlayAverage;
        prefs->underlayMode = static_cast<underlay_t>(modeVal);
    }
    prefs->peakDecay = qBound(0, settings->value("peakDecay", defprefs->peakDecay).toInt(), 100);
    prefs->wfAntiAlias = settings->value("WFAntiAlias", defprefs->wfAntiAlias).toBool();
    prefs->wfInterpolate = settings->value("WFInterpolate", defprefs->wfInterpolate).toBool();
    prefs->mainWflength = (unsigned int)settings->value("MainWFLength", defprefs->mainWflength).toInt();
    prefs->subWflength = (unsigned int)settings->value("SubWFLength", defprefs->subWflength).toInt();
    prefs->stylesheetPath = settings->value("StylesheetPath", defprefs->stylesheetPath).toString();


    restoreGeometry(settings->value("windowGeometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.

    if (bandbtns != nullptr)
        bandbtns->setGeometry(settings->value("BandWindowGeometry").toByteArray());

    if (finputbtns != nullptr)
        finputbtns->setGeometry(settings->value("FreqWindowGeometry").toByteArray());

    prefs->confirmExit = settings->value("ConfirmExit", defprefs->confirmExit).toBool();
    prefs->confirmPowerOff = settings->value("ConfirmPowerOff", defprefs->confirmPowerOff).toBool();
    prefs->confirmSettingsChanged = settings->value("ConfirmSettingsChanged", defprefs->confirmSettingsChanged).toBool();
    prefs->confirmMemories = settings->value("ConfirmMemories", defprefs->confirmMemories).toBool();
    prefs->meter1Type = static_cast<meter_t>(settings->value("Meter1Type", defprefs->meter1Type).toInt());
    prefs->meter2Type = static_cast<meter_t>(settings->value("Meter2Type", defprefs->meter2Type).toInt());
    prefs->meter3Type = static_cast<meter_t>(settings->value("Meter3Type", defprefs->meter3Type).toInt());
    prefs->compMeterReverse = settings->value("compMeterReverse", defprefs->compMeterReverse).toBool();
    prefs->clickDragTuningEnable = settings->value("ClickDragTuning", false).toBool();

    prefs->useUTC = settings->value("UseUTC", defprefs->useUTC).toBool();
    prefs->setRadioTime = settings->value("SetRadioTime", defprefs->setRadioTime).toBool();

    prefs->rigCreatorEnable = settings->value("RigCreator",false).toBool();
    prefs->region = settings->value("Region",defprefs->region).toString();
    //bandbtns->setRegion(prefs->region);
    prefs->showBands = settings->value("ShowBands",defprefs->showBands).toBool();
    prefs->miniRxButtons = settings->value("MiniRxButtons",defprefs->miniRxButtons).toBool();
    prefs->showTuningDial = settings->value("ShowTuningDial",defprefs->showTuningDial).toBool();
    prefs->wideRitSlider = settings->value("WideRitSlider",defprefs->wideRitSlider).toBool();
    prefs->alwaysSaveOnExit = settings->value("AlwaysSaveOnExit",defprefs->alwaysSaveOnExit).toBool();
    prefs->warnAudioDeviceProblems = settings->value("WarnAudioDeviceProblems",defprefs->warnAudioDeviceProblems).toBool();
    prefs->reverseWheel = settings->value("ReverseWheel",defprefs->reverseWheel).toBool();
    prefs->sMeterOnTop = settings->value("SMeterOnTop",defprefs->sMeterOnTop).toBool();
    prefs->meterStyle = settings->value("MeterStyle", defprefs->meterStyle).toInt();
    prefs->popoutMeter = settings->value("PopoutMeter", defprefs->popoutMeter).toBool();
    prefs->popoutMeterStyle = settings->value("PopoutMeterStyle", defprefs->popoutMeterStyle).toInt();
    prefs->popoutSecondaryVfo = settings->value("PopoutSecondaryVfo", defprefs->popoutSecondaryVfo).toBool();
    prefs->popoutMeterPrimary = settings->value("PopoutMeterPrimary", defprefs->popoutMeterPrimary).toInt();
    prefs->popoutMeterSecondary = settings->value("PopoutMeterSecondary", defprefs->popoutMeterSecondary).toInt();
    prefs->popoutMeter2Primary = settings->value("PopoutMeter2Primary", defprefs->popoutMeter2Primary).toInt();
    prefs->popoutMeter2Secondary = settings->value("PopoutMeter2Secondary", defprefs->popoutMeter2Secondary).toInt();
    prefs->popoutMeterWidth = settings->value("PopoutMeterWidth", defprefs->popoutMeterWidth).toInt();
    prefs->popoutMeterHeight = settings->value("PopoutMeterHeight", defprefs->popoutMeterHeight).toInt();
    prefs->largeFreqDisplay = settings->value("LargeFreqDisplay",defprefs->largeFreqDisplay).toBool();
    prefs->largeOptionalMeters = settings->value("LargeOptionalMeters",defprefs->largeOptionalMeters).toBool();
    prefs->freqFont = settings->value("FreqFont",defprefs->freqFont).toString();

    //ui->rigCreatorBtn->setEnabled(prefs->rigCreatorEnable);

    prefs->frequencyUnits = settings->value("FrequencyUnits",3).toInt();

    settings->endGroup();

    // Load in the color presets. The default values are already loaded.

    settings->beginGroup("ColorPresets");
    prefs->currentColorPresetNumber = settings->value("currentColorPresetNumber", defprefs->currentColorPresetNumber).toInt();
    if(prefs->currentColorPresetNumber > numColorPresetsTotal-1)
        prefs->currentColorPresetNumber = 0;

    int numPresetsInFile = settings->beginReadArray("ColorPreset");
    // We will use the number of presets that the working copy of wfview
    // supports, as we must never exceed the available number.
    if (numPresetsInFile > 0)
    {
        colorPrefsType* p;
        QString tempName;
        for (int pn = 0; pn < numColorPresetsTotal; pn++)
        {
            settings->setArrayIndex(pn);
            p = &(colorPreset[pn]);
            p->presetNum = settings->value("presetNum", p->presetNum).toInt();
            tempName = settings->value("presetName", *p->presetName).toString();
            if ((!tempName.isEmpty()) && tempName.length() < 11)
            {
                p->presetName->clear();
                p->presetName->append(tempName);
            }
            p->gridColor = colorFromString(settings->value("gridColor", p->gridColor.name(QColor::HexArgb)).toString());
            p->axisColor = colorFromString(settings->value("axisColor", p->axisColor.name(QColor::HexArgb)).toString());
            p->textColor = colorFromString(settings->value("textColor", p->textColor.name(QColor::HexArgb)).toString());
            p->spectrumLine = colorFromString(settings->value("spectrumLine", p->spectrumLine.name(QColor::HexArgb)).toString());
            p->spectrumFill = colorFromString(settings->value("spectrumFill", p->spectrumFill.name(QColor::HexArgb)).toString());
            p->useSpectrumFillGradient = settings->value("useSpectrumFillGradient", p->useSpectrumFillGradient).toBool();
            p->spectrumFillTop = colorFromString(settings->value("spectrumFillTop", p->spectrumFillTop.name(QColor::HexArgb)).toString());
            p->spectrumFillBot = colorFromString(settings->value("spectrumFillBot", p->spectrumFillBot.name(QColor::HexArgb)).toString());
            p->underlayLine = colorFromString(settings->value("underlayLine", p->underlayLine.name(QColor::HexArgb)).toString());
            p->underlayFill = colorFromString(settings->value("underlayFill", p->underlayFill.name(QColor::HexArgb)).toString());
            p->useUnderlayFillGradient = settings->value("useUnderlayFillGradient", p->useUnderlayFillGradient).toBool();
            p->underlayFillTop = colorFromString(settings->value("underlayFillTop", p->underlayFillTop.name(QColor::HexArgb)).toString());
            p->underlayFillBot = colorFromString(settings->value("underlayFillBot", p->underlayFillBot.name(QColor::HexArgb)).toString());
            p->plotBackground = colorFromString(settings->value("plotBackground", p->plotBackground.name(QColor::HexArgb)).toString());
            p->tuningLine = colorFromString(settings->value("tuningLine", p->tuningLine.name(QColor::HexArgb)).toString());
            p->passband = colorFromString(settings->value("passband", p->passband.name(QColor::HexArgb)).toString());
            p->pbt = colorFromString(settings->value("pbt", p->pbt.name(QColor::HexArgb)).toString());
            p->wfBackground = colorFromString(settings->value("wfBackground", p->wfBackground.name(QColor::HexArgb)).toString());
            p->wfGrid = colorFromString(settings->value("wfGrid", p->wfGrid.name(QColor::HexArgb)).toString());
            p->wfAxis = colorFromString(settings->value("wfAxis", p->wfAxis.name(QColor::HexArgb)).toString());
            p->wfText = colorFromString(settings->value("wfText", p->wfText.name(QColor::HexArgb)).toString());
            p->meterLevel = colorFromString(settings->value("meterLevel", p->meterLevel.name(QColor::HexArgb)).toString());
            p->meterAverage = colorFromString(settings->value("meterAverage", p->meterAverage.name(QColor::HexArgb)).toString());
            p->meterPeakLevel = colorFromString(settings->value("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb)).toString());
            p->meterPeakScale = colorFromString(settings->value("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb)).toString());
            p->meterLowerLine = colorFromString(settings->value("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb)).toString());
            p->meterLowText = colorFromString(settings->value("meterLowText", p->meterLowText.name(QColor::HexArgb)).toString());
            p->clusterSpots = colorFromString(settings->value("clusterSpots", p->clusterSpots.name(QColor::HexArgb)).toString());
            p->buttonOff = colorFromString(settings->value("buttonOff", p->buttonOff.name(QColor::HexArgb)).toString());
            p->buttonOn = colorFromString(settings->value("buttonOn", p->buttonOn.name(QColor::HexArgb)).toString());
        }
    }
    settings->endArray();
    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    prefs->manufacturer = static_cast<manufacturersType_t>(settings->value("Manufacturer", defprefs->manufacturer).toInt());
    prefs->radioCIVAddr = (quint16)settings->value("RigCIVuInt", defprefs->radioCIVAddr).toInt();
    prefs->CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defprefs->CIVisRadioModel).toBool();
    prefs->pttType = (pttType_t)settings->value("PTTType", defprefs->pttType).toInt();
    prefs->serialPortRadio = settings->value("SerialPortRadio", defprefs->serialPortRadio).toString();
    prefs->serialPortBaud = (quint32)settings->value("SerialPortBaud", defprefs->serialPortBaud).toInt();


    if (prefs->serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs->serialPortBaud;
    }


    prefs->polling_ms = settings->value("polling_ms", defprefs->polling_ms).toInt();
    // Migrated
    if(prefs->polling_ms == 0)
    {
        // Automatic

    } else {
        // Manual

    }

    prefs->virtualSerialPort = settings->value("VirtualSerialPort", defprefs->virtualSerialPort).toString();
    prefs->virtualSerialPortUseQueue = settings->value("VirtualSerialPortUseQueue", defprefs->virtualSerialPortUseQueue).toBool();


    prefs->localAFgain = (quint8)settings->value("localAFgain", defprefs->localAFgain).toUInt();
    prefs->rxSetup.localAFgain = prefs->localAFgain;
    prefs->txSetup.localAFgain = 255;

    prefs->audioSystem = normalizeAudioType(settings->value("AudioSystem", defprefs->audioSystem).toInt());

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs->enablePTT = settings->value("EnablePTT", defprefs->enablePTT).toBool();
    prefs->pttTimeoutEnabled = settings->value("PTTTimeoutEnabled", defprefs->pttTimeoutEnabled).toBool();
    prefs->pttTimeoutSeconds = qBound(30, settings->value("PTTTimeoutSeconds", defprefs->pttTimeoutSeconds).toInt(), 999);

    prefs->niceTS = settings->value("NiceTS", defprefs->niceTS).toBool();

    prefs->automaticSidebandSwitching = settings->value("automaticSidebandSwitching", defprefs->automaticSidebandSwitching).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs->enableLAN = settings->value("EnableLAN", defprefs->enableLAN).toBool();

    // If LAN is enabled, server gets its audio straight from the LAN
    // migrated, remove these
    //ui->serverRXAudioInputCombo->setEnabled(!prefs->enableLAN);
    //ui->serverTXAudioOutputCombo->setEnabled(!prefs->enableLAN);

    // If LAN is not enabled, disable local audio input/output
    //ui->audioOutputCombo->setEnabled(prefs->enableLAN);
    //ui->audioInputCombo->setEnabled(prefs->enableLAN);

    //ui->connectBtn->setEnabled(true);

    prefs->enableRigCtlD = settings->value("EnableRigCtlD", defprefs->enableRigCtlD).toBool();
    prefs->rigCtlPort = settings->value("RigCtlPort", defprefs->rigCtlPort).toInt();

    prefs->tcpPort = settings->value("TCPServerPort", defprefs->tcpPort).toInt();
    prefs->tciPort = settings->value("TCIServerPort", defprefs->tciPort).toInt();

    prefs->waterfallFormat = settings->value("WaterfallFormat", defprefs->waterfallFormat).toInt();

    udpprefs->ipAddress = settings->value("IPAddress", udpDefprefs->ipAddress).toString();
    udpprefs->controlLANPort = settings->value("ControlLANPort", udpDefprefs->controlLANPort).toInt();
    udpprefs->serialLANPort = settings->value("SerialLANPort", udpDefprefs->serialLANPort).toInt();
    udpprefs->audioLANPort = settings->value("AudioLANPort", udpDefprefs->audioLANPort).toInt();
    udpprefs->scopeLANPort = settings->value("ScopeLANPort", udpDefprefs->scopeLANPort).toInt();
    udpprefs->adminLogin = settings->value("AdminLogin",udpDefprefs->adminLogin).toBool();
    udpprefs->username = settings->value("Username", udpDefprefs->username).toString();
    udpprefs->password = settings->value("Password", udpDefprefs->password).toString();
    prefs->rxSetup.isinput = defprefs->rxSetup.isinput;
    prefs->txSetup.isinput = defprefs->txSetup.isinput;

    prefs->rxSetup.latency = settings->value("AudioRXLatency", defprefs->rxSetup.latency).toInt();
    prefs->txSetup.latency = settings->value("AudioTXLatency", defprefs->txSetup.latency).toInt();

    prefs->rxSetup.sampleRate=settings->value("AudioRXSampleRate", defprefs->rxSetup.sampleRate).toInt();
    prefs->txSetup.sampleRate=prefs->rxSetup.sampleRate;

    prefs->rxSetup.codec = settings->value("AudioRXCodec", defprefs->rxSetup.codec).toInt();
    prefs->txSetup.codec = settings->value("AudioTXCodec", defprefs->txSetup.codec).toInt();
    prefs->rxSetup.name = settings->value("AudioOutput", "Default Output Device").toString();

    qInfo(logGui()) << "Got Audio Output from Settings: " << prefs->rxSetup.name;

    prefs->txSetup.name = settings->value("AudioInput", "Default Input Device").toString();
    qInfo(logGui()) << "Got Audio Input from Settings: " << prefs->txSetup.name;

    prefs->rxSetup.resampleQuality = settings->value("ResampleQuality", defprefs->rxSetup.resampleQuality).toInt();
    prefs->txSetup.resampleQuality = prefs->rxSetup.resampleQuality;

    prefs->rxSetup.type = prefs->audioSystem;
    prefs->txSetup.type = prefs->audioSystem;


    if (prefs->tciPort > 0 && tci == nullptr) {

        tci = new tciServer();

        tciThread = new QThread(this);
        tciThread->setObjectName("TCIServer()");
        tci->moveToThread(tciThread);
        connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),tci, SLOT(receiveRigCaps(rigCapabilities*)));
        connect(this,SIGNAL(tciInit(quint16)),tci, SLOT(init(quint16)));
        connect(tciThread, SIGNAL(finished()), tci, SLOT(deleteLater()));
        tciThread->start(QThread::TimeCriticalPriority);
        emit tciInit(prefs->tciPort);
    }

    udpprefs->connectionType = normalizeConnectionType(settings->value("ConnectionType", int(udpDefprefs->connectionType)).toInt());
    udpprefs->clientName = settings->value("ClientName", udpDefprefs->clientName).toString();

    udpprefs->halfDuplex = settings->value("HalfDuplex", udpDefprefs->halfDuplex).toBool();

    settings->endGroup();

    settings->beginGroup("Server");
    //setupui->acceptServerConfig(&serverConfig);

    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.disableUI = settings->value("DisableUI", false).toBool();
    // These defPrefs are actually for the client, but they are the same.
    serverConfig.controlPort = settings->value("ServerControlPort", udpDefprefs->controlLANPort).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", udpDefprefs->serialLANPort).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", udpDefprefs->audioLANPort).toInt();
    serverConfig.scopePort = settings->value("ServerScopePort", udpDefprefs->scopeLANPort).toInt();
    serverConfig.listenAddress = settings->value("ServerListenAddress", QString()).toString();

    serverConfig.users.clear();

    int numUsers = settings->beginReadArray("Users");
    if (numUsers > 0) {
        {
            for (int f = 0; f < numUsers; f++)
            {
                settings->setArrayIndex(f);
                SERVERUSER user;
                user.username = settings->value("Username", "").toString();
                user.password = settings->value("Password", "").toString();
                user.userType = settings->value("UserType", 0).toInt();
                serverConfig.users.append(user);
            }
        }
        settings->endArray();
    }
    else {
        // Support old way of storing users
        settings->endArray();
        numUsers = settings->value("ServerNumUsers", 2).toInt();
        for (int f = 0; f < numUsers; f++)
        {
            SERVERUSER user;
            user.username = settings->value("ServerUsername_" + QString::number(f), "").toString();
            user.password = settings->value("ServerPassword_" + QString::number(f), "").toString();
            user.userType = settings->value("ServerUserType_" + QString::number(f), 0).toInt();
            serverConfig.users.append(user);
        }
    }

    RIGCONFIG* rigTemp = new RIGCONFIG();
    rigTemp->rxAudioSetup.isinput = true;
    rigTemp->txAudioSetup.isinput = false;
    rigTemp->rxAudioSetup.localAFgain = 255;
    rigTemp->txAudioSetup.localAFgain = 255;
    rigTemp->rxAudioSetup.resampleQuality = 4;
    rigTemp->txAudioSetup.resampleQuality = 4;
    rigTemp->rxAudioSetup.type = prefs->audioSystem;
    rigTemp->txAudioSetup.type = prefs->audioSystem;
    rigTemp->pttType = prefs->pttType; // Use the global PTT type.

    rigTemp->baudRate = prefs->serialPortBaud;
    rigTemp->civAddr = 0;
    rigTemp->serialPort = prefs->serialPortRadio;
    QString guid = settings->value("GUID", "").toString();
    if (guid.isEmpty()) {
        guid = QUuid::createUuid().toString();
        settings->setValue("GUID", guid);
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    memcpy(rigTemp->guid, QUuid::fromString(guid).toRfc4122().constData(), GUIDLEN);
#endif

    rigTemp->rxAudioSetup.name = settings->value("ServerAudioInput", "").toString();
    rigTemp->txAudioSetup.name = settings->value("ServerAudioOutput", "").toString();

    serverConfig.rigs.append(rigTemp);

    // At this point, the users list has exactly one empty user.
    //setupui->updateServerConfig(s_users);

    settings->endGroup();

    // Memory channels


    settings->beginGroup("Memory");
    int size = settings->beginReadArray("Channel");
    int chan = 0;
    double freq;
    quint8 mode;
    bool isSet;

    // Annoying: QSettings will write the array to the
    // preference file starting the array at 1 and ending at 100.
    // Thus, we are writing the channel number each time.
    // It is also annoying that they get written with their array
    // numbers in alphabetical order without zero padding.
    // Also annoying that the preference groups are not written in
    // the order they are specified here.

    for (int i = 0; i < size; i++)
    {
        settings->setArrayIndex(i);
        chan = settings->value("chan", 0).toInt();
        freq = settings->value("freq", 12.345).toDouble();
        mode = settings->value("mode", 0).toInt();
        isSet = settings->value("isSet", false).toBool();

        if (isSet)
        {
            mem.setPreset(chan, freq, (rigMode_t)mode);
        }
    }

    settings->endArray();
    settings->endGroup();


    settings->beginGroup("Cluster");

    prefs->clusterUdpEnable = settings->value("UdpEnabled", defprefs->clusterUdpEnable).toBool();
    prefs->clusterTcpEnable = settings->value("TcpEnabled", defprefs->clusterTcpEnable).toBool();
    prefs->clusterUdpPort = settings->value("UdpPort", defprefs->clusterUdpPort).toInt();

    int numClusters = settings->beginReadArray("Servers");
    prefs->clusters.clear();
    if (numClusters > 0) {
        {
            for (int f = 0; f < numClusters; f++)
            {
                settings->setArrayIndex(f);
                clusterSettings c;
                c.server = settings->value("ServerName", "").toString();
                c.port = settings->value("Port", 7300).toInt();
                c.userName = settings->value("UserName", "").toString();
                c.password = settings->value("Password", "").toString();
                c.timeout = qBound(dxClusterClient::spotLifetimeMin, settings->value("SpotLifetime", 60).toInt(), dxClusterClient::spotLifetimeMax);
                c.isdefault = settings->value("Default", false).toBool();
                if (!c.server.isEmpty()) {
                    prefs->clusters.append(c);
                }
            }
        }
    }
    settings->endArray();
    settings->endGroup();

    // CW Memory Load:
    settings->beginGroup("Keyer");
    prefs->cwCutNumbers=settings->value("CutNumbers", defprefs->cwCutNumbers).toBool();
    prefs->cwSendImmediate=settings->value("SendImmediate", defprefs->cwSendImmediate).toBool();
    prefs->cwSidetoneEnabled=settings->value("SidetoneEnabled", defprefs->cwSidetoneEnabled).toBool();
    prefs->cwSidetoneLevel=settings->value("SidetoneLevel", defprefs->cwSidetoneLevel).toInt();

    int numMemories = settings->beginReadArray("macros");
    if(numMemories==10)
    {
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            prefs->cwMacroList << settings->value("macroText", "").toString();
        }
    }
    settings->endArray();
    settings->endGroup();

#if defined (USB_CONTROLLER)
    settings->beginGroup("USB");
    prefs->enableUSBControllers = settings->value("EnableUSBControllers", defprefs->enableUSBControllers).toBool();


    QMutexLocker locker(&usbMutex);

    int numControllers = settings->beginReadArray("Controllers");
    if (numControllers == 0) {
        settings->endArray();
    }
    else {
        usbDevices.clear();
        for (int nc = 0; nc < numControllers; nc++)
        {
            settings->setArrayIndex(nc);
            USBDEVICE tempPrefs;
            tempprefs->path = settings->value("Path", "").toString();
            tempprefs->disabled = settings->value("Disabled", false).toBool();
            tempprefs->sensitivity = qMax(1, settings->value("Sensitivity", 1).toInt());
            tempprefs->ringAccel = qBound(1, settings->value("RingAccel", 3).toInt(), maxRingAccel);
            tempprefs->pages = settings->value("Pages", 1).toInt();
            tempprefs->brightness = (quint8)settings->value("Brightness", 2).toInt();
            tempprefs->orientation = (quint8)settings->value("Orientation", 2).toInt();
            tempprefs->speed = (quint8)settings->value("Speed", 2).toInt();
            tempprefs->timeout = (quint8)settings->value("Timeout", 30).toInt();
            tempprefs->color = colorFromString(settings->value("Color", QColor(Qt::white).name(QColor::HexArgb)).toString());
            tempprefs->lcd = (funcs)settings->value("LCD",0).toInt();

            if (!tempprefs->path.isEmpty()) {
                usbDevices.insert(tempprefs->path,tempPrefs);
            }
        }
        settings->endArray();
    }

    int numButtons = settings->beginReadArray("Buttons");
    if (numButtons == 0) {
        settings->endArray();
    }
    else {
        usbButtons.clear();
        for (int nb = 0; nb < numButtons; nb++)
        {
            settings->setArrayIndex(nb);
            BUTTON butt;
            butt.path = settings->value("Path", "").toString();
            butt.page = settings->value("Page", 1).toInt();
            auto it = usbDevices.find(butt.path);
            if (it==usbDevices.end())
            {
                qWarning(logUsbControl) << "Cannot find existing device while creating button, aborting!";
                continue;
            }
            butt.parent = &it.value();
            butt.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            butt.num = settings->value("Num", 0).toInt();
            butt.name = settings->value("Name", "").toString();
            butt.pos = QRect(settings->value("Left", 0).toInt(),
                             settings->value("Top", 0).toInt(),
                             settings->value("Width", 0).toInt(),
                             settings->value("Height", 0).toInt());
            butt.textColour = colorFromString(settings->value("Colour", QColor(Qt::white).name(QColor::HexArgb)).toString());
            butt.backgroundOn = colorFromString(settings->value("BackgroundOn", QColor(Qt::lightGray).name(QColor::HexArgb)).toString());
            butt.backgroundOff = colorFromString(settings->value("BackgroundOff", QColor(Qt::blue).name(QColor::HexArgb)).toString());
            butt.toggle = settings->value("Toggle", false).toBool();
            // PET add Linux as it stops Qt6 building FIXME
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS))
            if (settings->value("Icon",NULL) != NULL) {
                butt.icon = new QImage(settings->value("Icon",NULL).value<QImage>());
                butt.iconName = settings->value("IconName", "").toString();
            }
#endif
            butt.on = settings->value("OnCommand", "None").toString();
            butt.off = settings->value("OffCommand", "None").toString();
            butt.graphics = settings->value("Graphics", false).toBool();
            butt.led = settings->value("Led", 0).toInt();
            if (!butt.path.isEmpty())
                usbButtons.append(butt);
        }
        settings->endArray();
    }

    int numKnobs = settings->beginReadArray("Knobs");
    if (numKnobs == 0) {
        settings->endArray();
    }
    else {
        usbKnobs.clear();
        for (int nk = 0; nk < numKnobs; nk++)
        {
            settings->setArrayIndex(nk);
            KNOB kb;
            kb.path = settings->value("Path", "").toString();
            auto it = usbDevices.find(kb.path);
            if (it==usbDevices.end())
            {
                qWarning(logUsbControl) << "Cannot find existing device while creating knob, aborting!";
                continue;
            }
            kb.parent = &it.value();
            kb.page = settings->value("Page", 1).toInt();
            kb.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            kb.num = settings->value("Num", 0).toInt();
            kb.name = settings->value("Name", "").toString();
            kb.pos = QRect(settings->value("Left", 0).toInt(),
                           settings->value("Top", 0).toInt(),
                           settings->value("Width", 0).toInt(),
                           settings->value("Height", 0).toInt());
            kb.textColour = QColor((settings->value("Colour", "Green").toString()));

            kb.cmd = settings->value("Command", "None").toString();
            if (!kb.path.isEmpty())
                usbKnobs.append(kb);
        }
        settings->endArray();
    }

    settings->endGroup();

    if (prefs->enableUSBControllers) {
        // Setup USB Controller
        //setupUsbControllerDevice();
        //emit initUsbController(&usbMutex,&usbDevices,&usbButtons,&usbKnobs);
    }


#endif

    //setupui->acceptPreferencesPtr(&prefs);
    //setupui->acceptColorPresetPtr(colorPreset);
    //setupui->acceptUdpPreferencesPtr(&udpPrefs);

    prefs->settingsChanged = false;
}
*/

QStringList MainController::systemRigLocationCandidates()
{
    QStringList candidates;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    candidates << QCoreApplication::applicationDirPath() + "/../Resources/rigs";
#elif !defined(Q_OS_LINUX)
    candidates << QCoreApplication::applicationDirPath() + "/rigs";
#else
    const QString appDir = qEnvironmentVariable("APPDIR");
    if (appDir.isEmpty()) {
        candidates << QString(PREFIX "/share/wfview/rigs");
    } else {
        // Running as an AppImage, so PREFIX is not useful. The installer puts rigs in
        // ~/.local/share (user install) or /usr/local/share (root install); fall back
        // to the copy bundled inside the AppImage.
        candidates << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/wfview/rigs";
        for (const QString &dataDir : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
            candidates << dataDir + "/wfview/rigs";
        candidates << QStringLiteral("/usr/local/share/wfview/rigs")
                   << QStringLiteral("/usr/share/wfview/rigs")
                   << appDir + "/usr/share/wfview/rigs";
    }
#endif
    candidates.removeDuplicates();
    return candidates;
}

QString MainController::systemRigLocation()
{
    // Prefer a folder that holds rig files, so an empty folder does not hide a populated one.
    const QStringList candidates = systemRigLocationCandidates();
    QString firstExisting;
    for (const QString &candidate : candidates) {
        const QDir dir(candidate);
        if (!dir.exists())
            continue;
        if (!dir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files).isEmpty())
            return candidate;
        if (firstExisting.isEmpty())
            firstExisting = candidate;
    }
    if (!firstExisting.isEmpty())
        return firstExisting;
    return candidates.isEmpty() ? QString() : candidates.first();
}

void MainController::setManufacturer(manufacturersType_t man)
{

    this->rigList.clear();
    qInfo() << "Searching for radios with Manufacturer =" << man;

    const QString systemRigLocation = MainController::systemRigLocation();
    QDir systemRigDir(systemRigLocation);
    // Rig description files found for any manufacturer.
    int rigFilesFound = 0;

    if (!systemRigDir.exists()) {
        qWarning() << "********* No rig directory found. Searched:" << systemRigLocationCandidates().join(", ") << "********";
    } else {
        qInfo(logRig()) << "Using system rig file directory" << systemRigLocation;
        QStringList rigs = systemRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        for (QString &rig: rigs) {
            QSettings* rigSettings = new QSettings(systemRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            rigSettings->setIniCodec("UTF-8");
#endif

            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }
            rigFilesFound++;

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");
            manufacturersType_t manuf = static_cast<manufacturersType_t>(rigSettings->value("Manufacturer", manufIcom).toInt());
            if (manuf == man) {
                quint16 civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = systemRigDir.absoluteFilePath(rig);

                qInfo() << QString("Found Rig %0 with CI-V address of %1 and version %2").arg(model).arg(civ,4,10,QChar('0')).arg(ver,0,'f',2);
                // Any user modified rig files will override system provided ones.
                this->rigList.insert(civ,rigInfo(civ,model,path,ver));
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

    QString userRigLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/rigs";
    QDir userRigDir(userRigLocation);
    if (userRigDir.exists()){
        QStringList rigs = userRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        for (QString& rig: rigs) {
            QSettings* rigSettings = new QSettings(userRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            rigSettings->setIniCodec("UTF-8");
#endif

            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }
            rigFilesFound++;

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");

            manufacturersType_t manuf = static_cast<manufacturersType_t>(rigSettings->value("Manufacturer", manufIcom).toInt());
            if (manuf == man) {
                quint16 civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = userRigDir.absoluteFilePath(rig);

                auto it = this->rigList.find(civ);

                if (it != this->rigList.end())
                {
                    if (ver >= it.value().version) {
                        qInfo() << QString("Found User Rig %0 with CI-V address of 0x%1 and newer or same version than system one (%2>=%3)").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2).arg(it.value().version,0,'f',2);
                        this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                    }
                } else {
                    qInfo() << QString("Found New User Rig %0 with CI-V address of 0x%1 version %2").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2);
                    this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                }
                // Any user modified rig files will override system provided ones.
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

    QString problem;
    if (rigFilesFound == 0) {
        const auto describe = [](const QDir& dir) {
            return dir.exists()
                ? tr("%1 (no rig files)").arg(QDir::toNativeSeparators(dir.absolutePath()))
                : tr("%1 (folder not found)").arg(QDir::toNativeSeparators(dir.absolutePath()));
        };
        const auto logDescribe = [](const QDir& dir) {
            return QDir::toNativeSeparators(dir.absolutePath())
                   + (dir.exists() ? " (no rig files)" : " (folder not found)");
        };
        QStringList searchedLog;
        QStringList searchedUi;
        for (const QString& candidate : systemRigLocationCandidates()) {
            searchedLog << logDescribe(QDir(candidate));
            searchedUi << describe(QDir(candidate));
        }
        searchedLog << logDescribe(userRigDir);
        searchedUi << describe(userRigDir);
        qCritical(logRig()).noquote() << "No rig files were found. Locations searched:"
                                      << searchedLog.join(", ");
        problem = tr("No rig definition files were found, so wfview cannot identify or control a radio. "
                     "Reinstalling wfview should restore them.\n\nLocations searched:\n%1")
                      .arg(searchedUi.join("\n"));
    }
    if (m_rigFilesProblem != problem) {
        m_rigFilesProblem = problem;
        emit rigFilesProblemChanged();
    }
}

void MainController::onRadioPacket(const QByteArray &packet)
{
    Q_UNUSED(packet)
    //const int rx = /* extract rx index from packet */;
    //if (rx < 0 || rx >= rxCtrls.size()) return;

    // Parse once here if it’s common work, or pass raw if per-rx parsing differs:
    //rxCtrls[rx]->ingestPacket(packet);
}

void MainController::setWindowTitle(const QString &t) {
    if (windowTitle == t) return;
    windowTitle = t;
    emit windowTitleChanged();
}

void MainController::setRadioStatusText(const QString& text)
{
    if (m_radioStatusText == text)
        return;

    m_radioStatusText = text;
    emit radioStatusTextChanged();
}

void MainController::setFooterMessageText(const QString& text)
{
    if (m_footerMessageText == text)
        return;

    m_footerMessageText = text;
    emit footerMessageTextChanged();
}

void MainController::setRigModelName(const QString& modelName)
{
    if (m_rigModelName == modelName)
        return;

    m_rigModelName = modelName;
    emit rigModelNameChanged();
}

void MainController::syncRadioClock()
{
    prepareRadioClockSync();
}

void MainController::prepareRadioClockSync()
{
    if (!queue || !rigCaps || !rigCaps->commands.contains(funcTime)) {
        setFooterMessageText(tr("Radio clock sync is not available for this connection."));
        return;
    }

    if (waitingToSetRadioClock)
        return;

    const QDateTime now = prefs->useUTC ? QDateTime::currentDateTimeUtc() : QDateTime::currentDateTime();
    const int msNow = now.time().second() * 1000 + now.time().msec();
    const int delayMs = 60000 - msNow;
    const QDateTime setpoint = now.addMSecs(delayMs);

    radioClockTime.hours = static_cast<quint8>(setpoint.time().hour());
    radioClockTime.minutes = static_cast<quint8>(setpoint.time().minute());

    radioClockDate.day = static_cast<quint8>(setpoint.date().day());
    radioClockDate.month = static_cast<quint8>(setpoint.date().month());
    radioClockDate.year = static_cast<uint16_t>(setpoint.date().year());

    const int utcOffsetSeconds = qAbs(setpoint.offsetFromUtc());
    radioClockUtcOffset.hours = static_cast<quint8>(utcOffsetSeconds / 3600);
    radioClockUtcOffset.minutes = static_cast<quint8>((utcOffsetSeconds % 3600) / 60);
    radioClockUtcOffset.isMinus = setpoint.offsetFromUtc() < 0;

    radioClockSyncTimer.start(delayMs);
    waitingToSetRadioClock = true;
    setFooterMessageText(tr("Setting radio clock at the next minute boundary."));
}

void MainController::sendRadioClockSync()
{
    if (!queue || !rigCaps) {
        waitingToSetRadioClock = false;
        return;
    }

    setFooterMessageText(tr("Setting radio clock now."));

    if (rigCaps->commands.contains(funcUTCOffset))
        queue->add(priorityImmediate, queueItem(funcUTCOffset, QVariant::fromValue<timekind>(radioClockUtcOffset)));
    if (rigCaps->commands.contains(funcTime))
        queue->add(priorityHighest, queueItem(funcTime, QVariant::fromValue<timekind>(radioClockTime)));
    if (rigCaps->commands.contains(funcDate))
        queue->add(priorityHighest, queueItem(funcDate, QVariant::fromValue<datekind>(radioClockDate)));

    waitingToSetRadioClock = false;
}

void MainController::setStepSize(quint64 st)
{
    if (st != stepSize) {
        stepSize = st;

        auto it = std::find_if(rigCaps->steps.begin(), rigCaps->steps.end(),
                               [st](const stepType &s) {
                                   return s.hz == st;   // change condition
                               });
        if (it != rigCaps->steps.end()) {
            queue->addUnique(priorityImmediate,queueItem(funcTuningStep,QVariant::fromValue(it->num),false,0));
        }

        for (auto *r : std::as_const(receivers)) {
            if (r) r->receiveStepSize(stepSize);
        }

    }
}
