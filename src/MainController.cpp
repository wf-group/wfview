#include "MainController.h"

#include "logcategories.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

static QVariantList spectrumToVariantList(const QVector<double>& bins)
{
    QVariantList out;
    out.reserve(bins.size());
    for (double v : bins)
        out.append(v);
    return out;
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
    setManufacturer(prefs->manufacturer);
    setAppTheme(prefs->useSystemTheme);
    qInfo() << "******** Theme colors:" << m_theme.text().name();

    m_theme.syncFromAppPalette();

    // We should be the first thing to request the queue so need to delete when finished
    queue = cachingQueue::getInstance(this);
    connect(queue,&cachingQueue::sendValue,this,&MainController::receiveValueFromQueue);
    //connect(queue,SIGNAL(sendMessage(QString)),this,SLOT(showStatusBarText(QString)));
    //connect(queue,SIGNAL(intervalUpdated(qint64)),this,SLOT(updatedQueueInterval(qint64)));

    connect(queue, &cachingQueue::finished, queue, &cachingQueue::deleteLater);
    // Make sure we know about any changes to rigCaps
    connect(queue, &cachingQueue::rigCapsUpdated, this, &MainController::receiveRigCaps);
    radioClockSyncTimer.setSingleShot(true);
    connect(&radioClockSyncTimer, &QTimer::timeout, this, &MainController::sendRadioClockSync);

    connect(m_settings.get(), &SettingsController::colChanged, this, &MainController::colChanged);
    connect(m_settings.get(), &SettingsController::ifChanged, this, &MainController::ifChanged);
    connect(m_settings.get(), &SettingsController::ctChanged, this, &MainController::ctChanged);
    connect(m_settings.get(), &SettingsController::clusterChanged, this, &MainController::clusterChanged);
    connect(m_settings.get(), &SettingsController::audioProcChanged, this, &MainController::applyAudioProcessingSettings);
    queue->interval(cmdStartupInterval_ms);

#if defined(USB_CONTROLLER)
    if (prefs->enableUSBControllers)
        QTimer::singleShot(0, this, &MainController::setupUsbControllerDevice);
#endif

    ensureAudioProcessors();
    setupClusterClient();
    startRigConnection();

}

void MainController::updateApplicationPalette()
{
    colorPrefsType colors = m_settings->getCurrentColorPreset();


    // DEBUG: Check what we actually got
    qInfo() << "=== Color Preset Values ===";
    qInfo() << "Window:" << colors.window;
    qInfo() << "Button:" << colors.button;
    qInfo() << "ButtonText:" << colors.buttonText;

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
    qInfo() << "Normal WindowText:" << palette.color(QPalette::Active, QPalette::WindowText);
    qInfo() << "Disabled WindowText:" << palette.color(QPalette::Disabled, QPalette::WindowText);
    qInfo() << "Normal ButtonText:" << palette.color(QPalette::Active, QPalette::ButtonText);
    qInfo() << "Disabled ButtonText:" << palette.color(QPalette::Disabled, QPalette::ButtonText);
}

void MainController::ensureAudioProcessors()
{
    if (!txProc) {
        txProc = new TxAudioProcessor(this);
    }

    if (!rxProc) {
        rxProc = new RxAudioProcessor(this);
        QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (appData.isEmpty())
            appData = QDir::homePath();
        QDir().mkpath(appData);
        rxProc->setNoiseStorePath(appData + "/wfview.noise");
    }

    if (!m_audioProcessorSignalsConnected) {
        connect(txProc, &TxAudioProcessor::haveSidetoneFloat,
                rxProc, &RxAudioProcessor::injectSidetone,
                Qt::DirectConnection);
        connect(txProc, &TxAudioProcessor::txSpectrumBins,
                this, [this](QVector<double> inBins, QVector<double> outBins, float rawSR) {
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
    txProc->setMuteRx(p.muteRx);
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
    if (rxProc)
        rxProc->startAnrProfile();
}

void MainController::stopAnrNoiseProfile()
{
    if (rxProc)
        rxProc->stopAnrProfile();
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
    qInfo() << "Received if item in MainController" << items;

    for (int bit = 0; bit < 32; bit++) {
        prefIfItem item = static_cast<prefIfItem>(1 << bit);
        if (items & item) {
            switch (item)
            {
            case if_frequencyUnits:
                qInfo() << "Got new frequencyUnit";
                // Receiver frequency unit handling has not been ported to QML yet.
                break;
            case if_useSystemTheme:
                updateApplicationPalette();
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
        meterNone, meterCenter, meterSWR, meterALC, meterComp,
        meterVoltage, meterCurrent, meterAudio, meterRxAudio, meterTxMod,
        meterdBu, meterdBuEMF, meterdBm
    };

    QVariantList options;
    for (meter_t type : types) {
        QVariantMap option;
        option["text"] = meterString[int(type)];
        option["value"] = int(type);
        option["available"] = isOptionalMeterAvailable(int(type));
        options.push_back(option);
    }
    return options;
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

    switch (type) {
    case meterComp:
        low = 0.0; high = 100.0; red = 75.0;
        break;
    case meterdBu:
        low = 0.0; high = 80.0; red = 80.0;
        break;
    case meterdBuEMF:
        low = 0.0; high = 85.0; red = 85.0;
        break;
    case meterdBm:
        low = -100.0; high = -20.0; red = -20.0;
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
        }
        break;
    }

    ext["low"] = low;
    ext["high"] = high;
    ext["red"] = red;
    return ext;
}

void MainController::setOptionalMeterType(int slot, int meterType)
{
    if (!m_settings || (slot != 2 && slot != 3))
        return;

    const meter_t oldType = slot == 2 ? (prefs ? prefs->meter2Type : meterNone)
                                      : (prefs ? prefs->meter3Type : meterNone);
    const meter_t otherType = slot == 2 ? (prefs ? prefs->meter3Type : meterNone)
                                        : (prefs ? prefs->meter2Type : meterNone);
    const funcs oldCommand = meterCommandForType(oldType);
    const funcs newCommand = meterCommandForType(meter_t(meterType));
    if (oldCommand != funcNone && oldCommand != newCommand && oldCommand != meterCommandForType(otherType)) {
        queue->del(oldCommand, (oldType == meterSubS) ? uchar(1) : uchar(0));
    }

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

void MainController::setModSource(int dataMode, int reg)
{
    if (!rigCaps || !m_settings || dataMode < 0 || dataMode > 3)
        return;

    auto it = std::find_if(rigCaps->inputs.cbegin(), rigCaps->inputs.cend(), [reg](const rigInput& input) {
        return int(input.reg) == reg;
    });

    if (it == rigCaps->inputs.cend())
        return;

    const funcs sourceFunc = modSourceCommand(dataMode);
    if (sourceFunc == funcNone || !rigCaps->commands.contains(sourceFunc))
        return;

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

void MainController::configureOptionalMeter(int slot, meter_t meterType)
{
    if (slot != 2 && slot != 3)
        return;

    const funcs newCommand = meterCommandForType(meterType);

    if (slot == 2)
        m_optionalMeter2Level = 0.0;
    else
        m_optionalMeter3Level = 0.0;

    if (meterType != meterNone && newCommand != funcNone && rigCaps && rigCaps->commands.contains(newCommand)) {
        queue->addUnique(priorityHighest, queueItem(newCommand, true, (meterType == meterSubS) ? uchar(1) : uchar(0)));
    }
}

void MainController::receiveOptionalMeter(meter_t meterType, double level)
{
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
    qInfo() << "Received changed colors into MainController" << items;

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

    auto rangeSpec = [this](funcs f, int fallbackMin, int fallbackMax) {
        QVariantMap spec;
        spec["available"] = rigCaps->commands.contains(f);
        spec["min"] = fallbackMin;
        spec["max"] = fallbackMax;
        if (rigCaps->commands.contains(f)) {
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
        {"canSplit", rigCaps->commands.contains(funcSplitStatus)},
        {"canMainSub", rigCaps->commands.contains(funcVFOBandMS)},
        {"canSwapMainSub", rigCaps->commands.contains(funcVFOSwapMS)},
        {"canEqualMainSub", rigCaps->commands.contains(funcVFOEqualMS)},
        {"canCompressor", rigCaps->commands.contains(funcCompressor)},
        {"canVox", rigCaps->commands.contains(funcVox)},
        {"txPower", rangeSpec(funcRFPower, 0, 255)},
        {"monitorGain", rangeSpec(funcMonitorGain, 0, 255)},
        {"micGain", rangeSpec(funcMicGain, 0, 255)},
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

    if (m_transmitting != enabled) {
        m_transmitting = enabled;
        emit transmittingChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcTransceiverStatus)) {
        queue->add(priorityImmediate, queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(enabled), false, uchar(0)));
    }
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

    if (m_ritFrequency != value) {
        m_ritFrequency = value;
        emit ritFrequencyChanged();
    }

    if (rigCaps && rigCaps->commands.contains(funcRitFreq)) {
        queue->add(priorityImmediate, queueItem(funcRitFreq, QVariant::fromValue(short(value))));
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



void MainController::shutdown()
{
    if (shuttingDown) return;
    shuttingDown = true;

#if defined(USB_CONTROLLER)
    stopUsbControllerDevice();
#endif
    stopClusterClient();

    if (connStatus != connectionStatus_t::connDisconnected)
    {
        connectionHandler(); // This will disconnect/delete if the radio is connected/connecting
    }

    // rigThread is started unconditionally in startRigConnection() (called from constructor),
    // but connectionHandler() only runs above when connStatus != connDisconnected.
    // If we started without ever connecting (connDisconnected throughout), rigThread is still
    // running and must be explicitly stopped here to avoid "QThread destroyed while running".
    if (rigThread)
    {
        if (rig) {
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

    if (settings)
    {
        delete settings;
    }

}

void MainController::ctChanged(SettingsController::prefCtItems items)
{
#if defined(USB_CONTROLLER)
    if (items.testFlag(prefCtItem::ct_enableUSBControllers)) {
        if (!prefs->enableUSBControllers)
            stopUsbControllerDevice();
        else
            setupUsbControllerDevice();
    }
#else
    Q_UNUSED(items)
#endif
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
    clusterThread->start();

    applyClusterSettingsToClient(true);
}

void MainController::stopClusterClient()
{
    if (!clusterThread)
        return;

    if (cluster) {
        QMetaObject::invokeMethod(cluster, "enableTcp", Qt::BlockingQueuedConnection, Q_ARG(bool, false));
        QMetaObject::invokeMethod(cluster, "enableUdp", Qt::BlockingQueuedConnection, Q_ARG(bool, false));
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
    emit clusterOutputTextChanged();
}

void MainController::appendClusterOutput(const QString& text)
{
    if (text.isEmpty())
        return;

    m_clusterOutputText.append(text);
    constexpr qsizetype maxChars = 12000;
    if (m_clusterOutputText.size() > maxChars) {
        m_clusterOutputText = m_clusterOutputText.right(maxChars);
        const qsizetype firstNewline = m_clusterOutputText.indexOf(QLatin1Char('\n'));
        if (firstNewline > 0 && firstNewline + 1 < m_clusterOutputText.size())
            m_clusterOutputText.remove(0, firstNewline + 1);
    }
    emit clusterOutputTextChanged();
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

    usbControllerThread = new QThread(this);
    usbControllerThread->setObjectName("usb()");
    usbControllerDev->moveToThread(usbControllerThread);
    connect(usbControllerThread, &QThread::started, usbControllerDev,
            [this, usbMutex, usbDevices, usbButtons, usbKnobs]() {
                usbControllerDev->init(usbMutex, usbDevices, usbButtons, usbKnobs);
                usbControllerDev->run();
            });
    connect(usbControllerThread, &QThread::finished, usbControllerDev, &usbController::deleteLater);
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

void MainController::doShuttle(bool up, quint8 level)
{
    int steps = 1;
    int modifiers = 0;
    if (level == 1) {
        modifiers = Qt::ShiftModifier;
    } else if (level > 4) {
        modifiers = Qt::ControlModifier;
        steps = qMax(1, int(level) - 3);
    } else if (level > 1) {
        steps = qMax(1, int(level) - 1);
    }
    changeFrequency(up ? steps : -steps);
}

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
#endif

void MainController::connectionHandler()
{
    if (connStatus == connectionStatus_t::connDisconnected)
    {
        // Start the connection.
        connStatus = connectionStatus_t::connConnecting;
        startRigConnection();
    } else {
        // disconnect and delete
        // Might as well empty both queue and cache, as both are now invalid.
        queue->clear();
        queue->clearCache();
        for (auto *r : std::as_const(receivers)) {
            if (r) r->deleteLater();
        }
        receivers.clear();
        detached.clear();
        currentReceiver = 0;
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
        setRadioStatusText(QString());
        setRigModelName(QString());
        connStatus = connectionStatus_t::connDisconnected;
    }

    emit connStatusChanged();
}

void MainController::startRigConnection()
{
    if (!rigThread)
    {
        switch (prefs->manufacturer){
        case manufIcom:
            rig = new icomCommander();
            break;
        case manufKenwood:
            rig = new kenwoodCommander();
            break;
        case manufYaesu:
            rig = new yaesuCommander();
            break;
        default:
            qCritical() << "Unknown Manufacturer, aborting...";
            break;
        }
        if (!rig) {
            setConnStatus(connectionStatus_t::connDisconnected);
            return;
        }

        // Set a suitable queue interval to ensure polling happens quickly enough.
        queue->interval(cmdStartupInterval_ms); // Currently 250ms but could be configurable

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




        if (prefs->enableLAN)
        {
            // "We need to setup the tx/rx audio:
            udpPrefs->waterfallFormat = prefs->waterfallFormat;
            // 60 second connection timeout.
            //ConnectionTimer.start(60000);
            emit sendNetworkCommSetup(rigList, prefs->radioCIVAddr, *udpPrefs, prefs->rxSetup, prefs->txSetup, prefs->virtualSerialPort, prefs->tcpPort);
        } else {
            if( (prefs->serialPortRadio.toLower() == QString("auto")))
            {
                //findSerialPort();
            } else {
                //serialPortRig = prefs->serialPortRadio;
            }
            emit sendSerialCommSetup(rigList, prefs->radioCIVAddr, prefs->serialPortRadio, prefs->serialPortBaud,prefs->virtualSerialPort, prefs->tcpPort,prefs->waterfallFormat);
            //ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);
        }


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
    if (!m_selRad)
        return;

    m_selRad->audioOutputLevel(status.rxAudioLevel);
    m_selRad->audioInputLevel(status.txAudioLevel);
    m_selRad->addTimeDifference(status.timeDifference);

    const QString txPrefix = status.message.contains(QStringLiteral("(no tx)")) ? QStringLiteral("(no tx) ") : QString();
    setRadioStatusText(QStringLiteral("%1rx latency: %2 ms / rtt: %3 ms / loss: %4/%5")
                           .arg(txPrefix)
                           .arg(status.rxCurrentLatency, 3)
                           .arg(status.networkLatency, 3)
                           .arg(status.packetsLost, 3)
                           .arg(status.packetsSent, 3));
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
    if(!prefs->enableLAN)
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

    // Put Kenwood into Shift&Width mode for filter control.
    queue->add(priorityHigh,queueItem(funcFilterControlSSB,QVariant::fromValue<bool>(true),false,0));
    queue->add(priorityHigh,queueItem(funcFilterControlData,QVariant::fromValue<bool>(true),false,0));


    // temporary, set AFGain.
    queue->addUnique(priorityImmediate,queueItem(funcAfGain,QVariant::fromValue<ushort>(prefs->localAFgain),false,0));

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

    for (auto *r : std::as_const(receivers)) {
        if (r) r->deleteLater();
    }
    receivers.clear();
    detached.clear();
    currentReceiver = 0;

    if (rigCaps) {
        if(prefs->polling_ms != 0)
        {
            queue->interval(prefs->polling_ms); // Set sensible frequency to poll the radio
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
        connStatus = connectionStatus_t::connConnected;
        setRigModelName(rigCaps->modelName);
        setWindowTitle(rigCaps->modelName);
        updateCurrentModSource(false);
        ++m_modSourceRevision;
        emit modSourcesChanged();

        getInitialRigState();
        buildUiSpecs();
    }
    else
    {
        setRadioStatusText(QString());
        setRigModelName(QString());
        connStatus = connectionStatus_t::connDisconnected;
    }

    emit connStatusChanged();

    emit receiverCountChanged();
    emit detachedChanged();


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
        ui->antennaGroup->setVisible(rigCaps->commands.contains(funcAntenna));
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

        ui->rxAntennaCheck->setEnabled(rigCaps->commands.contains(funcRXAntenna));
        ui->rxAntennaCheck->setChecked(false);

        ui->tuneEnableChk->setEnabled(rigCaps->commands.contains(funcTunerStatus));
        ui->tuneNowBtn->setEnabled(rigCaps->commands.contains(funcTunerStatus));

        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));

        ui->connectBtn->setText("Disconnect from Radio"); // We must be connected now.
        connStatus = connConnected;

        // Now we know that we are connected, enable rigctld
        enableRigCtl(prefs->enableRigCtlD);

        if(prefs->enableLAN)
        {
            ui->afGainSlider->setValue(prefs->localAFgain);
            queue->receiveValue(funcAfGain,quint8(prefs->localAFgain),currentReceiver);
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
        if (vfo == 0)
            receivers[val.receiver]->setFrequencyA(val.value.value<freqt>().Hz,false);
        else
            receivers[val.receiver]->setFrequencyB(val.value.value<freqt>().Hz,false);
        //if (val.receiver==0 || vfo == 0)
        //    rpt->handleUpdateCurrentMainFrequency(val.value.value<freqt>());
        break;
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
        // We are ONLY interested in VFOA
        /*
        if (val.receiver == currentReceiver && vfo == 0) {
            finputbtns->updateCurrentMode(m.mk);
            finputbtns->updateFilterSelection(m.filter);
            rpt->handleUpdateCurrentMainMode(m);
            if (cw != nullptr) {
                cw->handleCurrentModeUpdate(m.mk);
            }
        }
        */
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
        break;
    case funcSplitStatus:
    {
        const bool enabled = val.value.value<duplexMode_t>() != dmSplitOff;
        if (m_splitEnabled != enabled) {
            m_splitEnabled = enabled;
            emit splitEnabledChanged();
        }
        for (const auto &receiver : std::as_const(receivers))
            receiver->setSplitEnabled(enabled, false);
        //rpt->receiveDuplexMode(val.value.value<duplexMode_t>());
        //receivers[val.receiver]->setSplit(val.value.value<duplexMode_t>()==dmSplitOn?true:false);
        break;
    }
    case funcQuickSplit:
        //rpt->receiveQuickSplit(val.value.value<bool>());
        break;
    case funcTuningStep:
    {
        auto it = std::find_if(rigCaps->steps.begin(), rigCaps->steps.end(),
                               [val](const stepType &s) {
                                   return s.num == val.value.value<uchar>();   // change condition
                               });
        if (it != rigCaps->steps.end() && it->hz != stepSize && it->num != 0) {
            stepSize = it->hz;
            receivers[val.receiver]->receiveStepSize(it->hz);
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
        // Receiver-side CW pitch display has not been ported to QML yet.
        // Also send to CW window
        m_cwSender->setPitch(val.value.value<quint16>());
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
        m_cwSender->setWpm(val.value.value<uchar>());
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
        //receivers[val.receiver]->receiveMeter(meter_t::meterS,val.value.value<double>());
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
        //rpt->handleRptAccessMode(rptAccessTxRx_t((val.value.value<bool>())?ratrTONEon:ratrTONEoff));
        break;
    case funcRepeaterTSQL:
        //rpt->handleRptAccessMode(rptAccessTxRx_t((val.value.value<bool>())?ratrTSQLon:ratrTSQLoff));
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
        //receiveMonitor(val.value.value<bool>());
        break;
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
        //ui->preampSelCombo->setEnabled(!val.value.value<bool>());
        break;
    case funcTwinPeakFilter:
        break;
    case funcDialLock:
        break;
    case funcRXAntenna:
        //ui->rxAntennaCheck->setChecked(val.value.value<bool>());
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
        break;
    case funcIPPlus:
        receivers[val.receiver]->setIpPlus(val.value.value<bool>(),false);
        break;
    case funcBreakIn:
        //if (cw != nullptr) {
        //    cw->handleBreakInMode(val.value.value<uchar>());
        //}
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
        m_cwSender->setDashRatio(val.value.value<uchar>());
        break;
    case funcTXFreqMon:
        break;
    // 0x1b register
    case funcToneFreq:
        //rpt->handleTone(val.value.value<toneInfo>().tone);
        break;
    case funcTSQLFreq:
        //rpt->handleTSQL(val.value.value<toneInfo>().tone);
        break;
    case funcDTCSCode:
    {
        //toneInfo t = val.value.value<toneInfo>();
        //rpt->handleDTCS(t.tone,t.rinv,t.tinv);
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
            emit transmittingChanged();
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
        //cw->receive(val.value.value<QString>());
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
    defprefs->underlayMode = underlayNone;
    defprefs->underlayBufferSize = 64;
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
    defprefs->niceTS = true;
    defprefs->enableRigCtlD = false;
    defprefs->rigCtlPort = 4533;
    defprefs->virtualSerialPort = QString("none");
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
            p->gridColor = QColor(0,0,0,255);
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
    prefs->underlayMode = static_cast<underlay_t>(settings->value("underlayMode", defprefs->underlayMode).toInt());
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
    prefs->manufacturer = (manufacturersType_t)settings->value("Manufacturer", defprefs->manufacturer).value<manufacturersType_t>();
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


    prefs->localAFgain = (quint8)settings->value("localAFgain", defprefs->localAFgain).toUInt();
    prefs->rxSetup.localAFgain = prefs->localAFgain;
    prefs->txSetup.localAFgain = 255;

    prefs->audioSystem = static_cast<audioType>(settings->value("AudioSystem", defprefs->audioSystem).toInt());

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs->enablePTT = settings->value("EnablePTT", defprefs->enablePTT).toBool();

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

    if (prefs->audioSystem == tciAudio)
    {
        prefs->rxSetup.tci = tci;
        prefs->txSetup.tci = tci;
    }


    udpprefs->connectionType = settings->value("ConnectionType", udpDefprefs->connectionType).value<connectionType_t>();
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
                c.timeout = settings->value("Timeout", 0).toInt();
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
            tempprefs->sensitivity = settings->value("Sensitivity", 1).toInt();
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

void MainController::setManufacturer(manufacturersType_t man)
{

    this->rigList.clear();
    qInfo() << "Searching for radios with Manufacturer =" << man;

#ifndef Q_OS_LINUX
    QString systemRigLocation = QCoreApplication::applicationDirPath() + "/rigs";
#else
    // When running as an AppImage, APPDIR is set and PREFIX is not useful.
    QString systemRigLocation = qEnvironmentVariable("APPDIR").isEmpty()
        ? QString(PREFIX "/share/wfview/rigs")
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/wfview/rigs";
#endif

    QDir systemRigDir(systemRigLocation);

    if (!systemRigDir.exists()) {
        qWarning() << "********* Rig directory " << systemRigLocation << "does not exist ********";
    } else {
        qDebug(logRig()) << "Loaded rigs from directory " << systemRigLocation;
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

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");
            manufacturersType_t manuf = rigSettings->value("Manufacturer",manufIcom).value<manufacturersType_t>();
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

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");

            manufacturersType_t manuf = rigSettings->value("Manufacturer",manufIcom).value<manufacturersType_t>();
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
        setRadioStatusText(tr("Radio clock sync is not available for this connection."));
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
    setRadioStatusText(tr("Setting radio clock at the next minute boundary."));
}

void MainController::sendRadioClockSync()
{
    if (!queue || !rigCaps) {
        waitingToSetRadioClock = false;
        return;
    }

    setRadioStatusText(tr("Setting radio clock now."));

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
