#include "SettingsController.h"
#include "logcategories.h"
#include "waterfallitem.h" // To get the theme
#include <QGuiApplication>
#include <QScreen>
#include <utility>


static inline QVariant qColorToVariant(const QColor& c) {
    return QVariant::fromValue(c);
}

static inline QColor variantToQColor(const QVariant& v) {
    if (v.canConvert<QColor>()) return v.value<QColor>();
    // allow "#AARRGGBB" or "#RRGGBB"
    const QString s = v.toString().trimmed();
    QColor c(s);
    return c;
}


SettingsController::SettingsController(QString file, QObject *p) :
    QObject(p),
    m_controllerController(new ControllerController(this))
{
    qInfo(logSystem) << "Creating SettingsController() to load settings";

    setObjectName("MainSettingsController");

    if (!file.isNull())
    {
        QFile info(file);
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

        settings = std::make_unique<QSettings>(file, QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        settings->setIniCodec("UTF-8");
#endif
    } else {
        settings = std::make_unique<QSettings>();
    }

    m_options = new QQmlPropertyMap(this);
    m_clusterModel = std::make_unique<ClusterSettingsModel>();
    m_serverUsersModel = std::make_unique<ServerUsersModel>();


    setDefPrefs();
    load();
    buildUiSpecs();

    // buildBindings() will attach all preferences items to the QML lookup for easy updating.
    buildBindings();
    seedOptionsFromBindings();

    m_clusterModel->setFromList(prefs.clusters);
    prefs.clusters = m_clusterModel->toList();
    updateDefaultClusterPrefs();
    m_serverUsersModel->setFromList(serverConfig.users);
    connect(m_clusterModel.get(), &ClusterSettingsModel::modified, this, [this]() {
        prefs.clusters = m_clusterModel->toList();
        updateDefaultClusterPrefs();
        markDirty();
        emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpServerName) |
                            prefClusterItems(prefClusterItem::cl_clusterTcpUserName) |
                            prefClusterItems(prefClusterItem::cl_clusterTcpPassword) |
                            prefClusterItems(prefClusterItem::cl_clusterTcpPort) |
                            prefClusterItems(prefClusterItem::cl_clusterTimeout));
    });
    connect(m_serverUsersModel.get(), &ServerUsersModel::modified, this, &SettingsController::markDirty);
}

QVariantMap SettingsController::restoredMainWindowGeometry() const
{
    QVariantMap result;
    result["valid"] = false;
    result["maximized"] = false;

    if (!settings)
        return result;

    settings->beginGroup("Interface");
    const bool haveGeometry = settings->contains("MainWindowX") &&
                              settings->contains("MainWindowY") &&
                              settings->contains("MainWindowWidth") &&
                              settings->contains("MainWindowHeight");
    QRect rect(settings->value("MainWindowX").toInt(),
               settings->value("MainWindowY").toInt(),
               settings->value("MainWindowWidth").toInt(),
               settings->value("MainWindowHeight").toInt());
    const bool maximized = settings->value("MainWindowMaximized", false).toBool();
    settings->endGroup();

    if (!haveGeometry || rect.width() < 200 || rect.height() < 200)
        return result;

    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return result;

    QScreen *primary = QGuiApplication::primaryScreen();
    QRect available = primary ? primary->availableGeometry() : screens.first()->availableGeometry();
    bool intersectsScreen = false;
    for (QScreen *screen : screens) {
        if (!screen)
            continue;

        const QRect screenAvailable = screen->availableGeometry();
        if (screenAvailable.intersects(rect)) {
            available = screenAvailable;
            intersectsScreen = true;
            break;
        }
    }

    QSize size(qMin(rect.width(), available.width()), qMin(rect.height(), available.height()));
    size.setWidth(qMax(size.width(), 400));
    size.setHeight(qMax(size.height(), 300));

    QPoint topLeft = rect.topLeft();
    if (!intersectsScreen) {
        topLeft = QPoint(available.x() + (available.width() - size.width()) / 2,
                         available.y() + (available.height() - size.height()) / 2);
    }

    topLeft.setX(qBound(available.left(), topLeft.x(), available.right() - size.width() + 1));
    topLeft.setY(qBound(available.top(), topLeft.y(), available.bottom() - size.height() + 1));

    result["valid"] = true;
    result["x"] = topLeft.x();
    result["y"] = topLeft.y();
    result["width"] = size.width();
    result["height"] = size.height();
    result["maximized"] = maximized;
    return result;
}

void SettingsController::saveMainWindowGeometry(int x, int y, int width, int height, bool maximized)
{
    if (!settings || width < 200 || height < 200)
        return;

    settings->beginGroup("Interface");
    settings->setValue("MainWindowX", x);
    settings->setValue("MainWindowY", y);
    settings->setValue("MainWindowWidth", width);
    settings->setValue("MainWindowHeight", height);
    settings->setValue("MainWindowMaximized", maximized);
    settings->endGroup();
    settings->sync();
}

void SettingsController::saveLocalAFGain(int gain)
{
    if (!settings)
        return;

    const int clampedGain = qBound(0, gain, 255);
    prefs.localAFgain = static_cast<quint8>(clampedGain);
    prefs.rxSetup.localAFgain = prefs.localAFgain;

    settings->beginGroup("Radio");
    settings->setValue("localAFgain", clampedGain);
    settings->endGroup();
    settings->sync();
}

QVariantMap SettingsController::receiverSettings(int index) const
{
    if (index < 0)
        return QVariantMap();

    QVariantMap result = defaultReceiverSettings(index);
    if (index < m_receiverSettings.size()) {
        const QVariantMap stored = m_receiverSettings.at(index);
        for (auto it = stored.constBegin(); it != stored.constEnd(); ++it)
            result[it.key()] = it.value();
    }
    return result;
}

QVariant SettingsController::receiverSetting(int index, const QString& key, const QVariant& fallback) const
{
    const QVariantMap values = receiverSettings(index);
    return values.value(key, fallback);
}

void SettingsController::saveReceiverSetting(int index, const QString& key, const QVariant& value)
{
    if (index < 0 || key.isEmpty())
        return;

    ensureReceiverSettings(index);
    if (m_receiverSettings[index].value(key) == value)
        return;

    m_receiverSettings[index].insert(key, value);
    markDirty();
}

QVariantMap SettingsController::defaultReceiverSettings(int index) const
{
    QVariantMap result;
    result["Detached"] = false;
    result["BandDrawerLocked"] = false;
    result["BandDrawerOpen"] = false;
    result["ControlDrawerLocked"] = false;
    result["ControlDrawerOpen"] = false;
    result["DetachedFullScreen"] = false;
    result["DetachedX"] = 0;
    result["DetachedY"] = 0;
    result["DetachedWidth"] = 900;
    result["DetachedHeight"] = 500;
    result["PlotFloor"] = index == 0 ? prefs.mainPlotFloor : (index == 1 ? prefs.subPlotFloor : defPrefs.mainPlotFloor);
    result["PlotCeiling"] = index == 0 ? prefs.mainPlotCeiling : (index == 1 ? prefs.subPlotCeiling : defPrefs.mainPlotCeiling);
    result["WFLength"] = index == 0 ? int(prefs.mainWflength) : (index == 1 ? int(prefs.subWflength) : int(defPrefs.mainWflength));
    result["WFTheme"] = index == 0 ? prefs.mainWfTheme : (index == 1 ? prefs.subWfTheme : defPrefs.mainWfTheme);
    return result;
}

void SettingsController::ensureReceiverSettings(int index)
{
    if (index < 0)
        return;

    while (m_receiverSettings.size() <= index)
        m_receiverSettings.append(defaultReceiverSettings(m_receiverSettings.size()));
}

void SettingsController::buildUiSpecs()
{
    // First create comboBox models
    QVariantList values;
    uiSpecs.clear();

    // Audio input devices
    for (auto &a: audioDev->getInputList()) {
        qDebug(logAudio()) << "Audio input device:" << a->name;
        values.append(QVariantMap{
            {"text",  a->name},
            {"value", a->name}
        });
    }

    uiSpecs["audioInputs"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    for (auto &a: audioDev->getOutputList()) {
        values.append(QVariantMap{
            {"text",  a->name},
            {"value", a->name}
        });
    }

    uiSpecs["audioOutputs"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    for (auto &a: QSerialPortInfo::availablePorts()) {
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
        values.append(QVariantMap{
            {"text",  QString("%0 (%1)").arg(a.portName(),a.serialNumber())},
            {"value", QString("/dev/%0").arg(a.portName())}
        });
#else
        values.append(QVariantMap{
            {"text",  QString("%0 (%1)").arg(a.portName(),a.serialNumber())},
            {"value", a.portName()}
        });
#endif
    }

#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
    values.append(QVariantMap{
        {"text",  "Manual ..."},
        {"value", 256}
    });
#endif

    uiSpecs["SerialPorts"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();

    emit uiSpecsChanged();
}

void SettingsController::load()
{

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

    prefs.version = priorVersionString;
    prefs.majorVersion = settings->value("majorVersion", defPrefs.majorVersion).toInt();
    prefs.minorVersion = settings->value("minorVersion", defPrefs.minorVersion).toInt();
    prefs.hasRunSetup = settings->value("hasRunSetup", defPrefs.hasRunSetup).toBool();
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    prefs.useFullScreen = settings->value("UseFullScreen", defPrefs.useFullScreen).toBool();
    prefs.useSystemTheme = settings->value("UseSystemTheme", defPrefs.useSystemTheme).toBool();
    prefs.wfEnable = settings->value("WFEnable", defPrefs.wfEnable).toInt();
    //ui->scopeEnableWFBtn->setCheckState(Qt::CheckState(prefs.wfEnable));
    prefs.mainWfTheme = settings->value("MainWFTheme", defPrefs.mainWfTheme).toInt();
    prefs.subWfTheme = settings->value("SubWFTheme", defPrefs.subWfTheme).toInt();
    prefs.mainPlotFloor = settings->value("MainPlotFloor", defPrefs.mainPlotFloor).toInt();
    prefs.subPlotFloor = settings->value("SubPlotFloor", defPrefs.subPlotFloor).toInt();
    prefs.mainPlotCeiling = settings->value("MainPlotCeiling", defPrefs.mainPlotCeiling).toInt();
    prefs.subPlotCeiling = settings->value("SubPlotCeiling", defPrefs.subPlotCeiling).toInt();
    prefs.scopeScrollX = settings->value("scopeScrollX", defPrefs.scopeScrollX).toInt();
    prefs.scopeScrollY = settings->value("scopeScrollY", defPrefs.scopeScrollY).toInt();
    prefs.decimalSeparator = settings->value("DecimalSeparator", defPrefs.decimalSeparator).toChar();
    prefs.groupSeparator = settings->value("GroupSeparator", defPrefs.groupSeparator).toChar();
    prefs.forceVfoMode =  settings->value("ForceVfoMode", defPrefs.groupSeparator).toBool();
    prefs.autoPowerOn =  settings->value("AutoPowerOn", defPrefs.autoPowerOn).toBool();

    prefs.drawPeaks = settings->value("DrawPeaks", defPrefs.drawPeaks).toBool();
    prefs.underlayBufferSize = settings->value("underlayBufferSize", defPrefs.underlayBufferSize).toInt();
    prefs.underlayMode = static_cast<underlay_t>(settings->value("underlayMode", defPrefs.underlayMode).toInt());
    prefs.wfAntiAlias = settings->value("WFAntiAlias", defPrefs.wfAntiAlias).toBool();
    prefs.wfInterpolate = settings->value("WFInterpolate", defPrefs.wfInterpolate).toBool();
    prefs.mainWflength = (unsigned int)settings->value("MainWFLength", defPrefs.mainWflength).toInt();
    prefs.subWflength = (unsigned int)settings->value("SubWFLength", defPrefs.subWflength).toInt();
    prefs.stylesheetPath = settings->value("StylesheetPath", defPrefs.stylesheetPath).toString();

    m_receiverSettings.clear();
    const int receiverCount = settings->beginReadArray("Receiver");
    for (int i = 0; i < receiverCount; ++i) {
        settings->setArrayIndex(i);
        QVariantMap values = defaultReceiverSettings(i);
        const QStringList keys = settings->allKeys();
        for (const QString& key : keys)
            values.insert(key, settings->value(key, values.value(key)));
        m_receiverSettings.append(values);
    }
    settings->endArray();

    /*
    restoreGeometry(settings->value("windowGeometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.

    if (bandbtns != nullptr)
        bandbtns->setGeometry(settings->value("BandWindowGeometry").toByteArray());

    if (finputbtns != nullptr)
        finputbtns->setGeometry(settings->value("FreqWindowGeometry").toByteArray());
    */
    prefs.confirmExit = settings->value("ConfirmExit", defPrefs.confirmExit).toBool();
    prefs.confirmPowerOff = settings->value("ConfirmPowerOff", defPrefs.confirmPowerOff).toBool();
    prefs.confirmSettingsChanged = settings->value("ConfirmSettingsChanged", defPrefs.confirmSettingsChanged).toBool();
    prefs.confirmMemories = settings->value("ConfirmMemories", defPrefs.confirmMemories).toBool();
    prefs.meter1Type = static_cast<meter_t>(settings->value("Meter1Type", defPrefs.meter1Type).toInt());
    prefs.meter2Type = static_cast<meter_t>(settings->value("Meter2Type", defPrefs.meter2Type).toInt());
    prefs.meter3Type = static_cast<meter_t>(settings->value("Meter3Type", defPrefs.meter3Type).toInt());
    prefs.compMeterReverse = settings->value("compMeterReverse", defPrefs.compMeterReverse).toBool();
    prefs.clickDragTuningEnable = settings->value("ClickDragTuning", false).toBool();

    prefs.useUTC = settings->value("UseUTC", defPrefs.useUTC).toBool();
    prefs.setRadioTime = settings->value("SetRadioTime", defPrefs.setRadioTime).toBool();

    prefs.rigCreatorEnable = settings->value("RigCreator",false).toBool();
    prefs.region = settings->value("Region",defPrefs.region).toString();
    //bandbtns->setRegion(prefs.region);
    prefs.showBands = settings->value("ShowBands",defPrefs.showBands).toBool();

    //ui->rigCreatorBtn->setEnabled(prefs.rigCreatorEnable);

    prefs.frequencyUnits = settings->value("FrequencyUnits",3).toInt();

    settings->endGroup();

    // Load in the color presets. The default values are already loaded.

    settings->beginGroup("ColorPresets");
    prefs.currentColorPresetNumber = settings->value("currentColorPresetNumber", defPrefs.currentColorPresetNumber).toInt();
    if(prefs.currentColorPresetNumber > numColorPresetsTotal-1)
        prefs.currentColorPresetNumber = 0;

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


            p->window = colorFromString(settings->value("window", p->window.name(QColor::HexArgb)).toString());
            p->windowText = colorFromString(settings->value("windowText", p->windowText.name(QColor::HexArgb)).toString());
            p->base = colorFromString(settings->value("base", p->base.name(QColor::HexArgb)).toString());
            p->alternateBase = colorFromString(settings->value("alternateBase", p->alternateBase.name(QColor::HexArgb)).toString());
            p->mainText = colorFromString(settings->value("mainText", p->mainText.name(QColor::HexArgb)).toString());
            p->button = colorFromString(settings->value("button", p->button.name(QColor::HexArgb)).toString());
            p->buttonText = colorFromString(settings->value("buttonText", p->buttonText.name(QColor::HexArgb)).toString());
            p->brightText = colorFromString(settings->value("brightText", p->brightText.name(QColor::HexArgb)).toString());
            p->light = colorFromString(settings->value("light", p->light.name(QColor::HexArgb)).toString());
            p->midLight = colorFromString(settings->value("midLight", p->midLight.name(QColor::HexArgb)).toString());
            p->dark = colorFromString(settings->value("dark", p->dark.name(QColor::HexArgb)).toString());
            p->mid = colorFromString(settings->value("mid", p->mid.name(QColor::HexArgb)).toString());
            p->shadow = colorFromString(settings->value("shadow", p->shadow.name(QColor::HexArgb)).toString());
            p->highlight = colorFromString(settings->value("highlight", p->highlight.name(QColor::HexArgb)).toString());
            p->highlightedText = colorFromString(settings->value("highlightedText", p->highlightedText.name(QColor::HexArgb)).toString());
            p->link = colorFromString(settings->value("link", p->link.name(QColor::HexArgb)).toString());
            p->linkVisited = colorFromString(settings->value("linkVisited", p->linkVisited.name(QColor::HexArgb)).toString());
            p->toolTipBase = colorFromString(settings->value("toolTipBase", p->toolTipBase.name(QColor::HexArgb)).toString());
            p->toolTipText = colorFromString(settings->value("toolTipText", p->toolTipText.name(QColor::HexArgb)).toString());
            p->placeholder = colorFromString(settings->value("placeholder", p->placeholder.name(QColor::HexArgb)).toString());

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
    prefs.manufacturer = static_cast<manufacturersType_t>(settings->value("Manufacturer", defPrefs.manufacturer).toInt());
    prefs.radioCIVAddr = (quint16)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defPrefs.CIVisRadioModel).toBool();
    prefs.pttType = (pttType_t)settings->value("PTTType", defPrefs.pttType).toInt();
    prefs.serialPortRadio = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    prefs.serialPortBaud = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();

    /*
    if (prefs.serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs.serialPortBaud;
    }
    */

    prefs.polling_ms = settings->value("polling_ms", defPrefs.polling_ms).toInt();
    // Migrated
    if(prefs.polling_ms == 0)
    {
        // Automatic

    } else {
        // Manual

    }

    prefs.virtualSerialPort = settings->value("VirtualSerialPort", defPrefs.virtualSerialPort).toString();


    prefs.localAFgain = (quint8)settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    prefs.rxSetup.localAFgain = prefs.localAFgain;
    prefs.txSetup.localAFgain = 255;

    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());
    prefs.enableUsbAudio = settings->value("EnableUSBAudio", defPrefs.enableUsbAudio).toBool();
    prefs.usbRxSetup.name = settings->value("USBAudioRXInput", defPrefs.usbRxSetup.name).toString();
    prefs.usbTxSetup.name = settings->value("USBAudioTXOutput", defPrefs.usbTxSetup.name).toString();

    settings->endGroup();

    // Now we have audioSystem, get the audio devices
    // Will need to work out a way to get the font size?, this will also need reloading when audioSystem changes.
    audioDev = std::make_unique<audioDevices>(prefs.audioSystem, QFontMetrics(QFont()));
    audioDev->enumerate();

    const QString configuredUsbRxInput = prefs.usbRxSetup.name;
    const QString configuredUsbTxOutput = prefs.usbTxSetup.name;
    prefs.usbRxSetup = defPrefs.usbRxSetup;
    prefs.usbTxSetup = defPrefs.usbTxSetup;
    prefs.usbRxSetup.type = prefs.audioSystem;
    prefs.usbTxSetup.type = prefs.audioSystem;
    prefs.usbRxSetup.isinput = true;
    prefs.usbTxSetup.isinput = false;
    prefs.usbRxSetup.sampleRate = prefs.rxSetup.sampleRate;
    prefs.usbTxSetup.sampleRate = prefs.txSetup.sampleRate;
    prefs.usbRxSetup.codec = prefs.rxSetup.codec;
    prefs.usbTxSetup.codec = prefs.txSetup.codec;
    prefs.usbRxSetup.latency = prefs.rxSetup.latency;
    prefs.usbTxSetup.latency = prefs.txSetup.latency;
    prefs.usbRxSetup.resampleQuality = prefs.rxSetup.resampleQuality;
    prefs.usbTxSetup.resampleQuality = prefs.txSetup.resampleQuality;
    prefs.usbRxSetup.name = configuredUsbRxInput;
    prefs.usbTxSetup.name = configuredUsbTxOutput;

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();

    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();

    prefs.automaticSidebandSwitching = settings->value("automaticSidebandSwitching", defPrefs.automaticSidebandSwitching).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();

    // If LAN is enabled, server gets its audio straight from the LAN
    // migrated, remove these
    //ui->serverRXAudioInputCombo->setEnabled(!prefs.enableLAN);
    //ui->serverTXAudioOutputCombo->setEnabled(!prefs.enableLAN);

    // If LAN is not enabled, disable local audio input/output
    //ui->audioOutputCombo->setEnabled(prefs.enableLAN);
    //ui->audioInputCombo->setEnabled(prefs.enableLAN);

    //ui->connectBtn->setEnabled(true);

    prefs.enableRigCtlD = settings->value("EnableRigCtlD", defPrefs.enableRigCtlD).toBool();
    prefs.rigCtlPort = settings->value("RigCtlPort", defPrefs.rigCtlPort).toInt();

    prefs.tcpPort = settings->value("TCPServerPort", defPrefs.tcpPort).toInt();
    prefs.tciPort = settings->value("TCIServerPort", defPrefs.tciPort).toInt();

    prefs.waterfallFormat = settings->value("WaterfallFormat", defPrefs.waterfallFormat).toInt();

    udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
    udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
    udpPrefs.serialLANPort = settings->value("SerialLANPort", udpDefPrefs.serialLANPort).toInt();
    udpPrefs.audioLANPort = settings->value("AudioLANPort", udpDefPrefs.audioLANPort).toInt();
    udpPrefs.scopeLANPort = settings->value("ScopeLANPort", udpDefPrefs.scopeLANPort).toInt();
    udpPrefs.adminLogin = settings->value("AdminLogin",udpDefPrefs.adminLogin).toBool();
    udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
    udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();

    int input = audioDev->findInput("Client",settings->value("AudioInput", "Default Input Device").toString(),true);

    if (input != -1) {
        if (prefs.audioSystem == qtAudio) {
            prefs.txSetup.port = audioDev->getInputDeviceInfo(input);
        }
        else {
            prefs.txSetup.portInt = audioDev->getInputDeviceInt(input);
        }
        prefs.txSetup.name = audioDev->getInputName(input);
    } else {
        prefs.txSetup.port = {};
        prefs.txSetup.portInt = -1;
        prefs.txSetup.name.clear();
        qWarning(logAudio) << "No valid audio input device found, please configure one in settings";
    }

    int output = audioDev->findOutput("Client",settings->value("AudioOutput", "Default Output Device").toString(),true);

    if (output != -1) {
        if (prefs.audioSystem == qtAudio) {
            prefs.rxSetup.port = audioDev->getOutputDeviceInfo(output);
        }
        else {
            prefs.rxSetup.portInt = audioDev->getOutputDeviceInt(output);
        }
        prefs.rxSetup.name = audioDev->getOutputName(output);
    } else {
        prefs.rxSetup.port = {};
        prefs.rxSetup.portInt = -1;
        prefs.rxSetup.name.clear();
        qWarning(logAudio) << "No valid audio output device found, please configure one in settings";
    }

    // Set these anyway even if we don't have a valid audio device.
    prefs.rxSetup.isinput = defPrefs.rxSetup.isinput;
    prefs.rxSetup.latency = settings->value("AudioRXLatency", defPrefs.rxSetup.latency).toInt();
    prefs.rxSetup.sampleRate=settings->value("AudioRXSampleRate", defPrefs.rxSetup.sampleRate).toInt();
    prefs.rxSetup.codec = settings->value("AudioRXCodec", defPrefs.rxSetup.codec).toInt();
    prefs.rxSetup.resampleQuality = settings->value("ResampleQuality", defPrefs.rxSetup.resampleQuality).toInt();
    prefs.rxSetup.type = prefs.audioSystem;
    qInfo(logGui()) << "Got Audio Output from Settings: " << prefs.rxSetup.name;

    prefs.txSetup.latency = settings->value("AudioTXLatency", defPrefs.txSetup.latency).toInt();
    prefs.txSetup.isinput = defPrefs.txSetup.isinput;
    prefs.txSetup.sampleRate=prefs.rxSetup.sampleRate;
    prefs.txSetup.codec = settings->value("AudioTXCodec", defPrefs.txSetup.codec).toInt();
    prefs.txSetup.resampleQuality = prefs.rxSetup.resampleQuality;
    prefs.txSetup.type = prefs.audioSystem;
    qInfo(logGui()) << "Got Audio Input from Settings: " << prefs.txSetup.name;

    prefs.usbRxSetup.isinput = true;
    prefs.usbRxSetup.type = prefs.audioSystem;
    prefs.usbRxSetup.sampleRate = prefs.rxSetup.sampleRate;
    prefs.usbRxSetup.codec = prefs.rxSetup.codec;
    prefs.usbRxSetup.latency = prefs.rxSetup.latency;
    prefs.usbRxSetup.resampleQuality = prefs.rxSetup.resampleQuality;
    prefs.usbRxSetup.localAFgain = 255;
    if (!prefs.usbRxSetup.name.isEmpty()) {
        const int radioInput = audioDev->findInput(QStringLiteral("Radio"), prefs.usbRxSetup.name, false);
        if (radioInput >= 0) {
            if (prefs.audioSystem == qtAudio)
                prefs.usbRxSetup.port = audioDev->getInputDeviceInfo(radioInput);
            else
                prefs.usbRxSetup.portInt = audioDev->getInputDeviceInt(radioInput);
            prefs.usbRxSetup.name = audioDev->getInputName(radioInput);
        } else {
            prefs.usbRxSetup.port = {};
            prefs.usbRxSetup.portInt = -1;
        }
    }

    prefs.usbTxSetup.isinput = false;
    prefs.usbTxSetup.type = prefs.audioSystem;
    prefs.usbTxSetup.sampleRate = prefs.txSetup.sampleRate;
    prefs.usbTxSetup.codec = prefs.txSetup.codec;
    prefs.usbTxSetup.latency = prefs.txSetup.latency;
    prefs.usbTxSetup.resampleQuality = prefs.txSetup.resampleQuality;
    prefs.usbTxSetup.localAFgain = 255;
    if (!prefs.usbTxSetup.name.isEmpty()) {
        const int radioOutput = audioDev->findOutput(QStringLiteral("Radio"), prefs.usbTxSetup.name, false);
        if (radioOutput >= 0) {
            if (prefs.audioSystem == qtAudio)
                prefs.usbTxSetup.port = audioDev->getOutputDeviceInfo(radioOutput);
            else
                prefs.usbTxSetup.portInt = audioDev->getOutputDeviceInt(radioOutput);
            prefs.usbTxSetup.name = audioDev->getOutputName(radioOutput);
        } else {
            prefs.usbTxSetup.port = {};
            prefs.usbTxSetup.portInt = -1;
        }
    }

    prefs.txAudioProc.bypass = settings->value("TxProcBypass", defPrefs.txAudioProc.bypass).toBool();
    prefs.txAudioProc.compEnabled = settings->value("TxProcCompEnabled", defPrefs.txAudioProc.compEnabled).toBool();
    prefs.txAudioProc.eqEnabled = settings->value("TxProcEqEnabled", defPrefs.txAudioProc.eqEnabled).toBool();
    prefs.txAudioProc.eqFirst = settings->value("TxProcEqFirst", defPrefs.txAudioProc.eqFirst).toBool();
    prefs.txAudioProc.inputGainDB = settings->value("TxProcInputGain", defPrefs.txAudioProc.inputGainDB).toFloat();
    prefs.txAudioProc.outputGainDB = settings->value("TxProcOutputGain", defPrefs.txAudioProc.outputGainDB).toFloat();
    prefs.txAudioProc.compPeakLimit = settings->value("TxProcCompPeak", defPrefs.txAudioProc.compPeakLimit).toFloat();
    prefs.txAudioProc.compRelease = settings->value("TxProcCompRelease", defPrefs.txAudioProc.compRelease).toFloat();
    prefs.txAudioProc.compFastRatio = settings->value("TxProcCompFast", defPrefs.txAudioProc.compFastRatio).toFloat();
    prefs.txAudioProc.compSlowRatio = settings->value("TxProcCompSlow", defPrefs.txAudioProc.compSlowRatio).toFloat();
    prefs.txAudioProc.sidetoneEnabled = settings->value("TxProcSidetone", defPrefs.txAudioProc.sidetoneEnabled).toBool();
    prefs.txAudioProc.sidetoneLevel = settings->value("TxProcSidetoneLevel", defPrefs.txAudioProc.sidetoneLevel).toFloat();
    prefs.txAudioProc.muteRx = settings->value("TxProcMuteRx", defPrefs.txAudioProc.muteRx).toBool();
    prefs.txAudioProc.spectrumEnabled = settings->value("TxProcSpectrumEnabled", defPrefs.txAudioProc.spectrumEnabled).toBool();
    prefs.txAudioProc.spectrumFPS = settings->value("TxProcSpectrumFps", defPrefs.txAudioProc.spectrumFPS).toInt();
    prefs.txAudioProc.specInhibitDuringRx = settings->value("TxProcSpecInhibitDuringRx", defPrefs.txAudioProc.specInhibitDuringRx).toBool();
    for (int i = 0; i < TX_AUDIO_EQ_BANDS; ++i)
        prefs.txAudioProc.eqBands[i] = settings->value(QString("TxProcEqBand%1").arg(i), defPrefs.txAudioProc.eqBands[i]).toFloat();
    prefs.txAudioProc.gateEnabled = settings->value("TxProcGateEnabled", defPrefs.txAudioProc.gateEnabled).toBool();
    prefs.txAudioProc.gateThreshold = settings->value("TxProcGateThreshold", defPrefs.txAudioProc.gateThreshold).toFloat();
    prefs.txAudioProc.gateAttack = settings->value("TxProcGateAttack", defPrefs.txAudioProc.gateAttack).toFloat();
    prefs.txAudioProc.gateHold = settings->value("TxProcGateHold", defPrefs.txAudioProc.gateHold).toFloat();
    prefs.txAudioProc.gateDecay = settings->value("TxProcGateDecay", defPrefs.txAudioProc.gateDecay).toFloat();
    prefs.txAudioProc.gateRange = settings->value("TxProcGateRange", defPrefs.txAudioProc.gateRange).toFloat();
    prefs.txAudioProc.gateLfCutoff = settings->value("TxProcGateLfCutoff", defPrefs.txAudioProc.gateLfCutoff).toFloat();
    prefs.txAudioProc.gateHfCutoff = settings->value("TxProcGateHfCutoff", defPrefs.txAudioProc.gateHfCutoff).toFloat();

    prefs.rxAudioProc.bypass = settings->value("RxProcBypass", defPrefs.rxAudioProc.bypass).toBool();
    prefs.rxAudioProc.nrEnabled = settings->value("RxProcNrEnabled", defPrefs.rxAudioProc.nrEnabled).toBool();
    prefs.rxAudioProc.nrMode = static_cast<RxNrMode>(settings->value("RxProcNrMode", static_cast<int>(defPrefs.rxAudioProc.nrMode)).toInt());
    prefs.rxAudioProc.channelSelect = settings->value("RxProcChannelSelect", defPrefs.rxAudioProc.channelSelect).toInt();
    prefs.rxAudioProc.speexSuppression = settings->value("RxProcSpeexSuppression", defPrefs.rxAudioProc.speexSuppression).toInt();
    prefs.rxAudioProc.speexBandsPreset = settings->value("RxProcSpeexBandsPreset", defPrefs.rxAudioProc.speexBandsPreset).toInt();
    prefs.rxAudioProc.speexFrameMs = settings->value("RxProcSpeexFrameMs", defPrefs.rxAudioProc.speexFrameMs).toInt();
    prefs.rxAudioProc.speexAgc = settings->value("RxProcSpeexAgc", defPrefs.rxAudioProc.speexAgc).toBool();
    prefs.rxAudioProc.speexAgcLevel = settings->value("RxProcSpeexAgcLevel", defPrefs.rxAudioProc.speexAgcLevel).toFloat();
    prefs.rxAudioProc.speexAgcMaxGain = settings->value("RxProcSpeexAgcMaxGain", defPrefs.rxAudioProc.speexAgcMaxGain).toInt();
    prefs.rxAudioProc.speexVad = settings->value("RxProcSpeexVad", defPrefs.rxAudioProc.speexVad).toBool();
    prefs.rxAudioProc.speexVadProbStart = settings->value("RxProcSpeexVadProbStart", defPrefs.rxAudioProc.speexVadProbStart).toInt();
    prefs.rxAudioProc.speexVadProbCont = settings->value("RxProcSpeexVadProbCont", defPrefs.rxAudioProc.speexVadProbCont).toInt();
    prefs.rxAudioProc.speexSnrDecay = settings->value("RxProcSpeexSnrDecay", defPrefs.rxAudioProc.speexSnrDecay).toFloat();
    prefs.rxAudioProc.speexNoiseUpdateRate = settings->value("RxProcSpeexNoiseUpdateRate", defPrefs.rxAudioProc.speexNoiseUpdateRate).toFloat();
    prefs.rxAudioProc.speexPriorBase = settings->value("RxProcSpeexPriorBase", defPrefs.rxAudioProc.speexPriorBase).toFloat();
    prefs.rxAudioProc.anrNoiseReductionDb = settings->value("RxProcAnrNoiseReductionDb", defPrefs.rxAudioProc.anrNoiseReductionDb).toDouble();
    prefs.rxAudioProc.anrSensitivity = settings->value("RxProcAnrSensitivity", defPrefs.rxAudioProc.anrSensitivity).toDouble();
    prefs.rxAudioProc.anrFreqSmoothing = settings->value("RxProcAnrFreqSmoothing", defPrefs.rxAudioProc.anrFreqSmoothing).toInt();
    prefs.rxAudioProc.eqEnabled = settings->value("RxProcEqEnabled", defPrefs.rxAudioProc.eqEnabled).toBool();
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        prefs.rxAudioProc.eqGain[i] = settings->value(QString("RxProcEqGain%1").arg(i), defPrefs.rxAudioProc.eqGain[i]).toFloat();
        prefs.rxAudioProc.eqFreq[i] = settings->value(QString("RxProcEqFreq%1").arg(i), defPrefs.rxAudioProc.eqFreq[i]).toFloat();
        prefs.rxAudioProc.eqQ[i] = settings->value(QString("RxProcEqQ%1").arg(i), defPrefs.rxAudioProc.eqQ[i]).toFloat();
    }
    prefs.rxAudioProc.outputGainDB = settings->value("RxProcOutputGainDB", defPrefs.rxAudioProc.outputGainDB).toFloat();
    prefs.rxAudioProc.spectrumEnabled = settings->value("RxProcSpectrumEnabled", defPrefs.rxAudioProc.spectrumEnabled).toBool();
    prefs.rxAudioProc.spectrumFPS = settings->value("RxProcSpectrumFps", defPrefs.rxAudioProc.spectrumFPS).toInt();
    prefs.rxAudioProc.specInhibitDuringTx = settings->value("RxProcSpecInhibitDuringTx", defPrefs.rxAudioProc.specInhibitDuringTx).toBool();



    /*
    if (prefs.tciPort > 0 && tci == nullptr) {

        tci = new tciServer();

        tciThread = new QThread(this);
        tciThread->setObjectName("TCIServer()");
        tci->moveToThread(tciThread);
        connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),tci, SLOT(receiveRigCaps(rigCapabilities*)));
        connect(this,SIGNAL(tciInit(quint16)),tci, SLOT(init(quint16)));
        connect(tciThread, SIGNAL(finished()), tci, SLOT(deleteLater()));
        tciThread->start(QThread::TimeCriticalPriority);
        emit tciInit(prefs.tciPort);
    }

    if (prefs.audioSystem == tciAudio)
    {
        prefs.rxSetup.tci = tci;
        prefs.txSetup.tci = tci;
    }
    */

    udpPrefs.connectionType = settings->value("ConnectionType", udpDefPrefs.connectionType).value<connectionType_t>();
    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    udpPrefs.halfDuplex = settings->value("HalfDuplex", udpDefPrefs.halfDuplex).toBool();

    settings->endGroup();

    settings->beginGroup("Experimental");
    prefs.wfShareEnabled = settings->value("WfShareEnabled",
                                           settings->value("IaxEnabled", defPrefs.wfShareEnabled)).toBool();
    prefs.wfShareDirectMode = settings->value("WfShareDirectMode", defPrefs.wfShareDirectMode).toBool();
    prefs.wfShareHost = settings->value("WfShareHost",
                                        settings->value("IaxHost", defPrefs.wfShareHost)).toString();
    prefs.wfSharePort = settings->value("WfSharePort",
                                        settings->value("IaxPort", defPrefs.wfSharePort)).toUInt();
    prefs.wfShareUsername = settings->value("WfShareUsername",
                                            settings->value("IaxUsername", defPrefs.wfShareUsername)).toString();
    prefs.wfSharePassword = settings->value("WfSharePassword",
                                            settings->value("IaxPassword", defPrefs.wfSharePassword)).toString();
    prefs.wfShareCalledNumber = settings->value("WfShareCalledNumber",
                                                settings->value("IaxCalledNumber", defPrefs.wfShareCalledNumber)).toString();
    settings->endGroup();

    settings->beginGroup("Server");
    //setupui->acceptServerConfig(&serverConfig);

    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.disableUI = settings->value("DisableUI", false).toBool();
    // These defPrefs are actually for the client, but they are the same.
    serverConfig.controlPort = settings->value("ServerControlPort", udpDefPrefs.controlLANPort).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", udpDefPrefs.serialLANPort).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", udpDefPrefs.audioLANPort).toInt();

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
    rigTemp->rxAudioSetup.type = prefs.audioSystem;
    rigTemp->txAudioSetup.type = prefs.audioSystem;
    rigTemp->pttType = prefs.pttType; // Use the global PTT type.

    rigTemp->baudRate = prefs.serialPortBaud;
    rigTemp->civAddr = 0;
    rigTemp->serialPort = prefs.serialPortRadio;
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

    /*
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
    */

    settings->beginGroup("Cluster");

    prefs.clusterUdpEnable = settings->value("UdpEnabled", defPrefs.clusterUdpEnable).toBool();
    prefs.clusterTcpEnable = settings->value("TcpEnabled", defPrefs.clusterTcpEnable).toBool();
    prefs.clusterUdpPort = settings->value("UdpPort", defPrefs.clusterUdpPort).toInt();

    int numClusters = settings->beginReadArray("Servers");
    prefs.clusters.clear();
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
                    prefs.clusters.append(c);
                }
            }
        }
    }
    settings->endArray();
    settings->endGroup();

    // CW Memory Load:
    settings->beginGroup("Keyer");
    prefs.cwCutNumbers=settings->value("CutNumbers", defPrefs.cwCutNumbers).toBool();
    prefs.cwSendImmediate=settings->value("SendImmediate", defPrefs.cwSendImmediate).toBool();
    prefs.cwSidetoneEnabled=settings->value("SidetoneEnabled", defPrefs.cwSidetoneEnabled).toBool();
    prefs.cwSidetoneLevel=settings->value("SidetoneLevel", defPrefs.cwSidetoneLevel).toInt();

    int numMemories = settings->beginReadArray("macros");
    if(numMemories==10)
    {
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            prefs.cwMacroList << settings->value("macroText", "").toString();
        }
    } else {
        prefs.cwMacroList = defPrefs.cwMacroList;
    }
    settings->endArray();
    settings->endGroup();

#if defined (USB_CONTROLLER)
    settings->beginGroup("USB");
    prefs.enableUSBControllers = settings->value("EnableUSBControllers", defPrefs.enableUSBControllers).toBool();

    QMutexLocker locker(&m_usbMutex);

    int numControllers = settings->beginReadArray("Controllers");
    if (numControllers == 0) {
        settings->endArray();
    }
    else {
        m_usbDevices.clear();
        for (int nc = 0; nc < numControllers; nc++)
        {
            settings->setArrayIndex(nc);
            USBDEVICE tempPrefs;
            tempPrefs.path = settings->value("Path", "").toString();
            tempPrefs.disabled = settings->value("Disabled", false).toBool();
            tempPrefs.sensitivity = settings->value("Sensitivity", 1).toInt();
            tempPrefs.pages = settings->value("Pages", 1).toInt();
            tempPrefs.brightness = (quint8)settings->value("Brightness", 2).toInt();
            tempPrefs.orientation = (quint8)settings->value("Orientation", 2).toInt();
            tempPrefs.speed = (quint8)settings->value("Speed", 2).toInt();
            tempPrefs.timeout = (quint8)settings->value("Timeout", 30).toInt();
            tempPrefs.color = colorFromString(settings->value("Color", QColor(Qt::white).name(QColor::HexArgb)).toString());
            tempPrefs.lcd = (funcs)settings->value("LCD",0).toInt();

            if (!tempPrefs.path.isEmpty()) {
                m_usbDevices.insert(tempPrefs.path,tempPrefs);
            }
        }
        settings->endArray();
    }

    int numButtons = settings->beginReadArray("Buttons");
    if (numButtons == 0) {
        settings->endArray();
    }
    else {
        m_usbButtons.clear();
        for (int nb = 0; nb < numButtons; nb++)
        {
            settings->setArrayIndex(nb);
            BUTTON butt;
            butt.path = settings->value("Path", "").toString();
            butt.page = settings->value("Page", 1).toInt();
            auto it = m_usbDevices.find(butt.path);
            if (it==m_usbDevices.end())
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
                m_usbButtons.append(butt);
        }
        settings->endArray();
    }

    int numKnobs = settings->beginReadArray("Knobs");
    if (numKnobs == 0) {
        settings->endArray();
    }
    else {
        m_usbKnobs.clear();
        for (int nk = 0; nk < numKnobs; nk++)
        {
            settings->setArrayIndex(nk);
            KNOB kb;
            kb.path = settings->value("Path", "").toString();
            auto it = m_usbDevices.find(kb.path);
            if (it==m_usbDevices.end())
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
                m_usbKnobs.append(kb);
        }
        settings->endArray();
    }
    settings->endGroup();
#endif
    /*
    setupui->acceptPreferencesPtr(&prefs);
    setupui->acceptColorPresetPtr(colorPreset);
    setupui->acceptUdpPreferencesPtr(&udpPrefs);
    */

    prefs.settingsChanged = false;
    m_dirty = false;
    emit dirtyChanged();
}

void SettingsController::save()
{
    qInfo(logSystem()) << "Saving settings to " << settings->fileName();
    // Basic things to load:

    prefs.clusters = m_clusterModel->toList();
    updateDefaultClusterPrefs();
    serverConfig.users = m_serverUsersModel->toList();

    QString versionstr = QString(WFVIEW_VERSION);
    QString majorVersion = "-1";
    QString minorVersion = "-1";
    if(versionstr.contains(".") && (versionstr.split(".").length() == 2))
    {
        majorVersion = versionstr.split(".").at(0);
        minorVersion = versionstr.split(".").at(1);
    }

    settings->beginGroup("Program");
    settings->setValue("version", versionstr);
    settings->setValue("majorVersion", int(majorVersion.toInt()));
    settings->setValue("minorVersion", int(minorVersion.toInt()));
    settings->setValue("gitShort", QString(GITSHORT));
    settings->setValue("hasRunSetup", prefs.hasRunSetup);
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    settings->setValue("UseFullScreen", prefs.useFullScreen);
    settings->setValue("UseSystemTheme", prefs.useSystemTheme);
    settings->setValue("WFEnable", prefs.wfEnable);
    settings->setValue("DrawPeaks", prefs.drawPeaks);
    settings->setValue("underlayMode", prefs.underlayMode);
    settings->setValue("underlayBufferSize", prefs.underlayBufferSize);
    settings->setValue("WFAntiAlias", prefs.wfAntiAlias);
    settings->setValue("WFInterpolate", prefs.wfInterpolate);
    settings->setValue("MainWFTheme", prefs.mainWfTheme);
    settings->setValue("SubWFTheme", prefs.subWfTheme);
    settings->setValue("MainPlotFloor", prefs.mainPlotFloor);
    settings->setValue("SubPlotFloor", prefs.subPlotFloor);
    settings->setValue("MainPlotCeiling", prefs.mainPlotCeiling);
    settings->setValue("SubPlotCeiling", prefs.subPlotCeiling);
    settings->setValue("StylesheetPath", prefs.stylesheetPath);
    //settings->setValue("splitter", ui->splitter->saveState());
    /*
    settings->setValue("windowGeometry", saveGeometry());

    if (bandbtns != nullptr)
        settings->setValue("BandWindowGeometry", bandbtns->getGeometry());
    if (finputbtns != nullptr)
        settings->setValue("FreqWindowGeometry", finputbtns->getGeometry());
    settings->setValue("windowState", saveState());
    */
    settings->setValue("MainWFLength", prefs.mainWflength);
    settings->setValue("SubWFLength", prefs.subWflength);

    settings->beginWriteArray("Receiver", m_receiverSettings.size());
    for (int i = 0; i < m_receiverSettings.size(); ++i) {
        settings->setArrayIndex(i);
        QVariantMap values = defaultReceiverSettings(i);
        const QVariantMap stored = m_receiverSettings.at(i);
        for (auto it = stored.constBegin(); it != stored.constEnd(); ++it)
            values.insert(it.key(), it.value());
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
            settings->setValue(it.key(), it.value());
    }
    settings->endArray();

    settings->setValue("ConfirmExit", prefs.confirmExit);
    settings->setValue("ConfirmPowerOff", prefs.confirmPowerOff);
    settings->setValue("ConfirmSettingsChanged", prefs.confirmSettingsChanged);
    settings->setValue("ConfirmMmories", prefs.confirmMemories);
    settings->setValue("Meter1Type", (int)prefs.meter1Type);
    settings->setValue("Meter2Type", (int)prefs.meter2Type);
    settings->setValue("Meter3Type", (int)prefs.meter3Type);
    settings->setValue("compMeterReverse", prefs.compMeterReverse);
    settings->setValue("ClickDragTuning", prefs.clickDragTuningEnable);

    settings->setValue("UseUTC",prefs.useUTC);
    settings->setValue("SetRadioTime",prefs.setRadioTime);

    settings->setValue("RigCreator",prefs.rigCreatorEnable);
    settings->setValue("FrequencyUnits",prefs.frequencyUnits);
    settings->setValue("Region",prefs.region);
    settings->setValue("ShowBands",prefs.showBands);
    settings->setValue("GroupSeparator",prefs.groupSeparator);
    settings->setValue("DecimalSeparator",prefs.decimalSeparator);
    settings->setValue("ForceVfoMode",prefs.forceVfoMode);
    settings->setValue("AutoPowerOn",prefs.autoPowerOn);

    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    settings->setValue("Manufacturer", prefs.manufacturer);

    settings->setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings->setValue("CIVisRadioModel", prefs.CIVisRadioModel);
    settings->setValue("PTTType", prefs.pttType);
    settings->setValue("polling_ms", prefs.polling_ms); // 0 = automatic
    if (!prefs.serialPortRadio.isEmpty())
        settings->setValue("SerialPortRadio", prefs.serialPortRadio);
    if (prefs.serialPortBaud != 0)
        settings->setValue("SerialPortBaud", prefs.serialPortBaud);
    settings->setValue("VirtualSerialPort", prefs.virtualSerialPort);
    settings->setValue("localAFgain", prefs.localAFgain);
    settings->setValue("AudioSystem", prefs.audioSystem);
    settings->setValue("EnableUSBAudio", prefs.enableUsbAudio);
    if (!prefs.usbRxSetup.name.isEmpty())
        settings->setValue("USBAudioRXInput", prefs.usbRxSetup.name);
    if (!prefs.usbTxSetup.name.isEmpty())
        settings->setValue("USBAudioTXOutput", prefs.usbTxSetup.name);

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    settings->setValue("EnablePTT", prefs.enablePTT);
    settings->setValue("NiceTS", prefs.niceTS);
    settings->setValue("automaticSidebandSwitching", prefs.automaticSidebandSwitching);
    settings->endGroup();

    settings->beginGroup("LAN");
    settings->setValue("EnableLAN", prefs.enableLAN);
    settings->setValue("EnableRigCtlD", prefs.enableRigCtlD);
    settings->setValue("TcpServerPort", prefs.tcpPort);
    settings->setValue("RigCtlPort", prefs.rigCtlPort);
    settings->setValue("TCPServerPort", prefs.tcpPort);
    settings->setValue("TCIServerPort", prefs.tciPort);
    settings->setValue("IPAddress", udpPrefs.ipAddress);
    settings->setValue("ControlLANPort", udpPrefs.controlLANPort);
    settings->setValue("SerialLANPort", udpPrefs.serialLANPort);
    settings->setValue("AudioLANPort", udpPrefs.audioLANPort);
    settings->setValue("ScopeLANPort", udpPrefs.scopeLANPort);
    settings->setValue("AdminLogin",udpPrefs.adminLogin);
    settings->setValue("Username", udpPrefs.username);
    settings->setValue("Password", udpPrefs.password);
    settings->setValue("AudioRXLatency", prefs.rxSetup.latency);
    settings->setValue("AudioTXLatency", prefs.txSetup.latency);
    settings->setValue("AudioRXSampleRate", prefs.rxSetup.sampleRate);
    settings->setValue("AudioRXCodec", prefs.rxSetup.codec);
    settings->setValue("AudioTXSampleRate", prefs.txSetup.sampleRate);
    settings->setValue("AudioTXCodec", prefs.txSetup.codec);
    if (!prefs.rxSetup.name.isEmpty())
        settings->setValue("AudioOutput", prefs.rxSetup.name);
    if (!prefs.txSetup.name.isEmpty())
        settings->setValue("AudioInput", prefs.txSetup.name);
    settings->setValue("ResampleQuality", prefs.rxSetup.resampleQuality);
    settings->setValue("ClientName", udpPrefs.clientName);
    settings->setValue("WaterfallFormat", prefs.waterfallFormat);
    settings->setValue("HalfDuplex", udpPrefs.halfDuplex);
    settings->setValue("ConnectionType", udpPrefs.connectionType);

    settings->setValue("TxProcBypass", prefs.txAudioProc.bypass);
    settings->setValue("TxProcCompEnabled", prefs.txAudioProc.compEnabled);
    settings->setValue("TxProcEqEnabled", prefs.txAudioProc.eqEnabled);
    settings->setValue("TxProcEqFirst", prefs.txAudioProc.eqFirst);
    settings->setValue("TxProcInputGain", prefs.txAudioProc.inputGainDB);
    settings->setValue("TxProcOutputGain", prefs.txAudioProc.outputGainDB);
    settings->setValue("TxProcCompPeak", prefs.txAudioProc.compPeakLimit);
    settings->setValue("TxProcCompRelease", prefs.txAudioProc.compRelease);
    settings->setValue("TxProcCompFast", prefs.txAudioProc.compFastRatio);
    settings->setValue("TxProcCompSlow", prefs.txAudioProc.compSlowRatio);
    settings->setValue("TxProcSidetone", prefs.txAudioProc.sidetoneEnabled);
    settings->setValue("TxProcSidetoneLevel", prefs.txAudioProc.sidetoneLevel);
    settings->setValue("TxProcMuteRx", prefs.txAudioProc.muteRx);
    settings->setValue("TxProcSpectrumEnabled", prefs.txAudioProc.spectrumEnabled);
    settings->setValue("TxProcSpectrumFps", prefs.txAudioProc.spectrumFPS);
    settings->setValue("TxProcSpecInhibitDuringRx", prefs.txAudioProc.specInhibitDuringRx);
    for (int i = 0; i < TX_AUDIO_EQ_BANDS; ++i)
        settings->setValue(QString("TxProcEqBand%1").arg(i), prefs.txAudioProc.eqBands[i]);
    settings->setValue("TxProcGateEnabled", prefs.txAudioProc.gateEnabled);
    settings->setValue("TxProcGateThreshold", prefs.txAudioProc.gateThreshold);
    settings->setValue("TxProcGateAttack", prefs.txAudioProc.gateAttack);
    settings->setValue("TxProcGateHold", prefs.txAudioProc.gateHold);
    settings->setValue("TxProcGateDecay", prefs.txAudioProc.gateDecay);
    settings->setValue("TxProcGateRange", prefs.txAudioProc.gateRange);
    settings->setValue("TxProcGateLfCutoff", prefs.txAudioProc.gateLfCutoff);
    settings->setValue("TxProcGateHfCutoff", prefs.txAudioProc.gateHfCutoff);

    settings->setValue("RxProcBypass", prefs.rxAudioProc.bypass);
    settings->setValue("RxProcNrEnabled", prefs.rxAudioProc.nrEnabled);
    settings->setValue("RxProcNrMode", static_cast<int>(prefs.rxAudioProc.nrMode));
    settings->setValue("RxProcChannelSelect", prefs.rxAudioProc.channelSelect);
    settings->setValue("RxProcSpeexSuppression", prefs.rxAudioProc.speexSuppression);
    settings->setValue("RxProcSpeexBandsPreset", prefs.rxAudioProc.speexBandsPreset);
    settings->setValue("RxProcSpeexFrameMs", prefs.rxAudioProc.speexFrameMs);
    settings->setValue("RxProcSpeexAgc", prefs.rxAudioProc.speexAgc);
    settings->setValue("RxProcSpeexAgcLevel", prefs.rxAudioProc.speexAgcLevel);
    settings->setValue("RxProcSpeexAgcMaxGain", prefs.rxAudioProc.speexAgcMaxGain);
    settings->setValue("RxProcSpeexVad", prefs.rxAudioProc.speexVad);
    settings->setValue("RxProcSpeexVadProbStart", prefs.rxAudioProc.speexVadProbStart);
    settings->setValue("RxProcSpeexVadProbCont", prefs.rxAudioProc.speexVadProbCont);
    settings->setValue("RxProcSpeexSnrDecay", prefs.rxAudioProc.speexSnrDecay);
    settings->setValue("RxProcSpeexNoiseUpdateRate", prefs.rxAudioProc.speexNoiseUpdateRate);
    settings->setValue("RxProcSpeexPriorBase", prefs.rxAudioProc.speexPriorBase);
    settings->setValue("RxProcAnrNoiseReductionDb", prefs.rxAudioProc.anrNoiseReductionDb);
    settings->setValue("RxProcAnrSensitivity", prefs.rxAudioProc.anrSensitivity);
    settings->setValue("RxProcAnrFreqSmoothing", prefs.rxAudioProc.anrFreqSmoothing);
    settings->setValue("RxProcEqEnabled", prefs.rxAudioProc.eqEnabled);
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        settings->setValue(QString("RxProcEqGain%1").arg(i), prefs.rxAudioProc.eqGain[i]);
        settings->setValue(QString("RxProcEqFreq%1").arg(i), prefs.rxAudioProc.eqFreq[i]);
        settings->setValue(QString("RxProcEqQ%1").arg(i), prefs.rxAudioProc.eqQ[i]);
    }
    settings->setValue("RxProcOutputGainDB", prefs.rxAudioProc.outputGainDB);
    settings->setValue("RxProcSpectrumEnabled", prefs.rxAudioProc.spectrumEnabled);
    settings->setValue("RxProcSpectrumFps", prefs.rxAudioProc.spectrumFPS);
    settings->setValue("RxProcSpecInhibitDuringTx", prefs.rxAudioProc.specInhibitDuringTx);

    settings->endGroup();

    settings->beginGroup("Experimental");
    settings->setValue("WfShareEnabled", prefs.wfShareEnabled);
    settings->setValue("WfShareDirectMode", prefs.wfShareDirectMode);
    settings->setValue("WfShareHost", prefs.wfShareHost);
    settings->setValue("WfSharePort", prefs.wfSharePort);
    settings->setValue("WfShareUsername", prefs.wfShareUsername);
    settings->setValue("WfSharePassword", prefs.wfSharePassword);
    settings->setValue("WfShareCalledNumber", prefs.wfShareCalledNumber);
    settings->endGroup();

    // Memory channels
    /*
    settings->beginGroup("Memory");
    settings->beginWriteArray("Channel", (int)mem.getNumPresets());

    preset_kind temp;
    for (int i = 0; i < (int)mem.getNumPresets(); i++)
    {
        temp = mem.getPreset((int)i);
        settings->setArrayIndex(i);
        settings->setValue("chan", i);
        settings->setValue("freq", temp.frequency);
        settings->setValue("mode", temp.mode);
        settings->setValue("isSet", temp.isSet);
    }

    settings->endArray();
    settings->endGroup();
    */
    // Color presets:
    settings->beginGroup("ColorPresets");
    settings->setValue("currentColorPresetNumber", prefs.currentColorPresetNumber);
    settings->beginWriteArray("ColorPreset", numColorPresetsTotal);
    const colorPrefsType *p;
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        p = &(colorPreset[pn]);

        settings->setArrayIndex(pn);
        settings->setValue("presetNum", p->presetNum);
        settings->setValue("presetName", *(p->presetName));     

        settings->setValue("window", p->window.name(QColor::HexArgb));
        settings->setValue("windowText", p->windowText.name(QColor::HexArgb));
        settings->setValue("base", p->base.name(QColor::HexArgb));
        settings->setValue("alternateBase", p->alternateBase.name(QColor::HexArgb));
        settings->setValue("mainText", p->mainText.name(QColor::HexArgb));
        settings->setValue("button", p->button.name(QColor::HexArgb));
        settings->setValue("buttonText", p->buttonText.name(QColor::HexArgb));
        settings->setValue("brightText", p->brightText.name(QColor::HexArgb));
        settings->setValue("light", p->light.name(QColor::HexArgb));
        settings->setValue("midLight", p->midLight.name(QColor::HexArgb));
        settings->setValue("dark", p->dark.name(QColor::HexArgb));
        settings->setValue("mid", p->mid.name(QColor::HexArgb));
        settings->setValue("shadow", p->shadow.name(QColor::HexArgb));
        settings->setValue("highlight", p->highlight.name(QColor::HexArgb));
        settings->setValue("highlightedText", p->highlightedText.name(QColor::HexArgb));
        settings->setValue("link", p->link.name(QColor::HexArgb));
        settings->setValue("linkVisited", p->linkVisited.name(QColor::HexArgb));
        settings->setValue("toolTipBase", p->toolTipBase.name(QColor::HexArgb));
        settings->setValue("toolTipText", p->toolTipText.name(QColor::HexArgb));
        settings->setValue("placeholder", p->placeholder.name(QColor::HexArgb));

        settings->setValue("gridColor", p->gridColor.name(QColor::HexArgb));
        settings->setValue("axisColor", p->axisColor.name(QColor::HexArgb));
        settings->setValue("textColor", p->textColor.name(QColor::HexArgb));
        settings->setValue("spectrumLine", p->spectrumLine.name(QColor::HexArgb));
        settings->setValue("spectrumFill", p->spectrumFill.name(QColor::HexArgb));
        settings->setValue("useSpectrumFillGradient", p->useSpectrumFillGradient);
        settings->setValue("spectrumFillTop", p->spectrumFillTop.name(QColor::HexArgb));
        settings->setValue("spectrumFillBot", p->spectrumFillBot.name(QColor::HexArgb));
        settings->setValue("underlayLine", p->underlayLine.name(QColor::HexArgb));
        settings->setValue("underlayFill", p->underlayFill.name(QColor::HexArgb));
        settings->setValue("useUnderlayFillGradient", p->useUnderlayFillGradient);
        settings->setValue("underlayFillTop", p->underlayFillTop.name(QColor::HexArgb));
        settings->setValue("underlayFillBot", p->underlayFillBot.name(QColor::HexArgb));
        settings->setValue("plotBackground", p->plotBackground.name(QColor::HexArgb));
        settings->setValue("tuningLine", p->tuningLine.name(QColor::HexArgb));
        settings->setValue("passband", p->passband.name(QColor::HexArgb));
        settings->setValue("pbt", p->pbt.name(QColor::HexArgb));
        settings->setValue("wfBackground", p->wfBackground.name(QColor::HexArgb));
        settings->setValue("wfGrid", p->wfGrid.name(QColor::HexArgb));
        settings->setValue("wfAxis", p->wfAxis.name(QColor::HexArgb));
        settings->setValue("wfText", p->wfText.name(QColor::HexArgb));
        settings->setValue("meterLevel", p->meterLevel.name(QColor::HexArgb));
        settings->setValue("meterAverage", p->meterAverage.name(QColor::HexArgb));
        settings->setValue("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb));
        settings->setValue("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb));
        settings->setValue("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb));
        settings->setValue("meterLowText", p->meterLowText.name(QColor::HexArgb));
        settings->setValue("clusterSpots", p->clusterSpots.name(QColor::HexArgb));
        settings->setValue("buttonOff", p->buttonOff.name(QColor::HexArgb));
        settings->setValue("buttonOn", p->buttonOn.name(QColor::HexArgb));
    }
    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Server");

    settings->setValue("ServerEnabled", serverConfig.enabled);
    settings->setValue("DisableUI", serverConfig.disableUI);
    settings->setValue("ServerControlPort", serverConfig.controlPort);
    settings->setValue("ServerCivPort", serverConfig.civPort);
    settings->setValue("ServerAudioPort", serverConfig.audioPort);
    if (!serverConfig.rigs.first()->txAudioSetup.name.isEmpty())
        settings->setValue("ServerAudioOutput", serverConfig.rigs.first()->txAudioSetup.name);
    if (!serverConfig.rigs.first()->rxAudioSetup.name.isEmpty())
        settings->setValue("ServerAudioInput", serverConfig.rigs.first()->rxAudioSetup.name);

    /* Remove old format users*/
    int numUsers = settings->value("ServerNumUsers", 0).toInt();
    if (numUsers > 0) {
        settings->remove("ServerNumUsers");
        for (int f = 0; f < numUsers; f++)
        {
            settings->remove("ServerUsername_" + QString::number(f));
            settings->remove("ServerPassword_" + QString::number(f));
            settings->remove("ServerUserType_" + QString::number(f));
        }
    }

    settings->beginWriteArray("Users");

    for (int f = 0; f < serverConfig.users.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("Username", serverConfig.users[f].username);
        settings->setValue("Password", serverConfig.users[f].password);
        settings->setValue("UserType", serverConfig.users[f].userType);
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Cluster");
    settings->setValue("UdpEnabled", prefs.clusterUdpEnable);
    settings->setValue("TcpEnabled", prefs.clusterTcpEnable);
    settings->setValue("UdpPort", prefs.clusterUdpPort);

    settings->beginWriteArray("Servers");

    for (int f = 0; f < prefs.clusters.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("ServerName", prefs.clusters[f].server);
        settings->setValue("UserName", prefs.clusters[f].userName);
        settings->setValue("Port", prefs.clusters[f].port);
        settings->setValue("Password", prefs.clusters[f].password);
        settings->setValue("Timeout", prefs.clusters[f].timeout);
        settings->setValue("Default", prefs.clusters[f].isdefault);
        /*
        if (prefs.clusters[f].isdefault  == true) {
            prefs.clusterTcpServerName = prefs.clusters[f].server;
            prefs.clusterTcpUserName = prefs.clusters[f].userName;
            prefs.clusterTcpPassword = prefs.clusters[f].password;
            prefs.clusterTcpPort = prefs.clusters[f].port;
            prefs.clusterTimeout = prefs.clusters[f].timeout;
        }
        */
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Keyer");
    /*
    if (cw != nullptr) {
        prefs.cwCutNumbers = cw->getCutNumbers();
        prefs.cwSendImmediate = cw->getSendImmediate();
        prefs.cwSidetoneEnabled = cw->getSidetoneEnable();
        prefs.cwSidetoneLevel = cw->getSidetoneLevel();
        prefs.cwMacroList = cw->getMacroText();
    }
    */
    settings->setValue("CutNumbers", prefs.cwCutNumbers);
    settings->setValue("SendImmediate", prefs.cwSendImmediate);
    settings->setValue("SidetoneEnabled", prefs.cwSidetoneEnabled);
    settings->setValue("SidetoneLevel", prefs.cwSidetoneLevel);

    if(prefs.cwMacroList.length() == 10)
    {
        settings->beginWriteArray("macros");
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            settings->setValue("macroText", prefs.cwMacroList.at(m));
        }
        settings->endArray();
    } else {
        qDebug(logSystem()) << "Error, CW macro list is wrong length: " << prefs.cwMacroList.length();
    }
    settings->endGroup();

#if defined(USB_CONTROLLER)
    settings->beginGroup("USB");

    settings->setValue("EnableUSBControllers", prefs.enableUSBControllers);

    QMutexLocker locker(&m_usbMutex);

    // Store USB Controller

    settings->remove("Controllers");
    settings->beginWriteArray("Controllers");
    int nc=0;

    for (auto it = m_usbDevices.begin(); it != m_usbDevices.end(); it++)
    {
        auto dev = &it.value();
        settings->setArrayIndex(nc);

        settings->setValue("Model", dev->product);
        settings->setValue("Path", dev->path);
        settings->setValue("Disabled", dev->disabled);
        settings->setValue("Sensitivity", dev->sensitivity);
        settings->setValue("Brightness", dev->brightness);
        settings->setValue("Orientation", dev->orientation);
        settings->setValue("Speed", dev->speed);
        settings->setValue("Timeout", dev->timeout);
        settings->setValue("Pages", dev->pages);
        settings->setValue("Color", dev->color.name(QColor::HexArgb));
        settings->setValue("LCD", dev->lcd);

        ++nc;
    }
    settings->endArray();


    settings->remove("Buttons");
    settings->beginWriteArray("Buttons");
    for (int nb = 0; nb < m_usbButtons.count(); nb++)
    {
        settings->setArrayIndex(nb);
        settings->setValue("Page", m_usbButtons[nb].page);
        settings->setValue("Dev", m_usbButtons[nb].dev);
        settings->setValue("Num", m_usbButtons[nb].num);
        settings->setValue("Path", m_usbButtons[nb].path);
        settings->setValue("Name", m_usbButtons[nb].name);
        settings->setValue("Left", m_usbButtons[nb].pos.left());
        settings->setValue("Top", m_usbButtons[nb].pos.top());
        settings->setValue("Width", m_usbButtons[nb].pos.width());
        settings->setValue("Height", m_usbButtons[nb].pos.height());
        settings->setValue("Colour", m_usbButtons[nb].textColour.name(QColor::HexArgb));
        settings->setValue("BackgroundOn", m_usbButtons[nb].backgroundOn.name(QColor::HexArgb));
        settings->setValue("BackgroundOff", m_usbButtons[nb].backgroundOff.name(QColor::HexArgb));
        if (m_usbButtons[nb].icon != nullptr) {
            settings->setValue("Icon", *m_usbButtons[nb].icon);
            settings->setValue("IconName", m_usbButtons[nb].iconName);
        }
        settings->setValue("Toggle", m_usbButtons[nb].toggle);

        if (m_usbButtons[nb].onCommand != nullptr)
            settings->setValue("OnCommand", m_usbButtons[nb].onCommand->text);
        if (m_usbButtons[nb].offCommand != nullptr)
            settings->setValue("OffCommand", m_usbButtons[nb].offCommand->text);
        settings->setValue("Graphics",m_usbButtons[nb].graphics);
        if (m_usbButtons[nb].led) {
            settings->setValue("Led", m_usbButtons[nb].led);
        }
    }

    settings->endArray();

    settings->remove("Knobs");
    settings->beginWriteArray("Knobs");
    for (int nk = 0; nk < m_usbKnobs.count(); nk++)
    {
        settings->setArrayIndex(nk);
        settings->setValue("Page", m_usbKnobs[nk].page);
        settings->setValue("Dev", m_usbKnobs[nk].dev);
        settings->setValue("Num", m_usbKnobs[nk].num);
        settings->setValue("Path", m_usbKnobs[nk].path);
        settings->setValue("Left", m_usbKnobs[nk].pos.left());
        settings->setValue("Top", m_usbKnobs[nk].pos.top());
        settings->setValue("Width", m_usbKnobs[nk].pos.width());
        settings->setValue("Height", m_usbKnobs[nk].pos.height());
        settings->setValue("Colour", m_usbKnobs[nk].textColour.name());
        if (m_usbKnobs[nk].command != nullptr)
            settings->setValue("Command", m_usbKnobs[nk].command->text);
    }

    settings->endArray();

    settings->endGroup();
#endif

    settings->sync(); // Automatic, not needed (supposedly)
    prefs.settingsChanged = false;
    if (m_dirty) {
        m_dirty = false;
        emit dirtyChanged();
    }
}

void SettingsController::resetToDefaults()
{
    qInfo(logSystem()) << "Per user request, resetting preferences.";

    prefs = defPrefs;
    udpPrefs = udpDefPrefs;
    serverConfig.enabled = false;
    serverConfig.users.clear();

    if (m_clusterModel) {
        m_clusterModel->setFromList(prefs.clusters);
        prefs.clusters = m_clusterModel->toList();
        updateDefaultClusterPrefs();
    }

    if (m_serverUsersModel)
        m_serverUsersModel->setFromList(serverConfig.users);

    seedOptionsFromBindings();
    save();
}

void SettingsController::setDefPrefs()
{
    defPrefs.hasRunSetup = false;
    defPrefs.useFullScreen = false;
    defPrefs.useSystemTheme = false;
    defPrefs.drawPeaks = true;
    defPrefs.currentColorPresetNumber = 0;
    defPrefs.underlayMode = underlayNone;
    defPrefs.underlayBufferSize = 64;
    defPrefs.wfEnable = 2;
    defPrefs.wfAntiAlias = false;
    defPrefs.wfInterpolate = true;
    defPrefs.stylesheetPath = QString("qdarkstyle/style.qss");
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.pttType = pttCIV;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.enableLAN = false;
    defPrefs.polling_ms = 0; // 0 = Automatic
    defPrefs.enablePTT = true;
    defPrefs.niceTS = true;
    defPrefs.enableRigCtlD = false;
    defPrefs.rigCtlPort = 4533;
    defPrefs.virtualSerialPort = QString("none");
    defPrefs.localAFgain = 255;
    defPrefs.mainWflength = 160;
    defPrefs.mainWfTheme = static_cast<int>(WaterfallItem::Theme::Jet);
    defPrefs.mainPlotFloor = 0;
    defPrefs.mainPlotCeiling = 160;
    defPrefs.subWflength = 160;
    defPrefs.subWfTheme = static_cast<int>(WaterfallItem::Theme::Jet);
    defPrefs.subPlotFloor = 0;
    defPrefs.subPlotCeiling = 160;
    defPrefs.scopeScrollX = 120;
    defPrefs.scopeScrollY = 120;
    defPrefs.confirmExit = true;
    defPrefs.confirmPowerOff = true;
    defPrefs.confirmSettingsChanged = true;
    defPrefs.confirmMemories = false;
    defPrefs.meter1Type = meterS;
    defPrefs.meter2Type = meterNone;
    defPrefs.meter3Type = meterNone;
    defPrefs.compMeterReverse = false;
    defPrefs.region = "1";
    defPrefs.showBands = true;
    defPrefs.manufacturer = manufIcom;
    defPrefs.useUTC = false;
    defPrefs.setRadioTime = false;
    defPrefs.forceVfoMode = true;
    defPrefs.autoPowerOn=true;

    defPrefs.tcpPort = 0;
    defPrefs.tciPort = 50001;
    defPrefs.wfShareEnabled = false;
    defPrefs.wfShareDirectMode = false;
    defPrefs.wfShareHost = QStringLiteral("pbx.wfshare.org");
    defPrefs.wfSharePort = 4569;
    defPrefs.wfShareUsername.clear();
    defPrefs.wfSharePassword.clear();
    defPrefs.wfShareCalledNumber.clear();
    defPrefs.enableUsbAudio = false;
    defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.waterfallFormat = 0;
    defPrefs.audioSystem = qtAudio;
    defPrefs.enableUSBControllers = false;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    defPrefs.groupSeparator = QLocale().groupSeparator();
    defPrefs.decimalSeparator = QLocale().decimalPoint();
#else
    defPrefs.groupSeparator = QLocale().groupSeparator().at(0);       // digit group separator
    defPrefs.decimalSeparator = QLocale().decimalPoint().at(0);       // digit group separator
#endif

    // Audio
    defPrefs.rxSetup.latency = 150;
    defPrefs.txSetup.latency = 150;
    defPrefs.rxSetup.portInt = -1;
    defPrefs.txSetup.portInt = -1;
    defPrefs.rxSetup.isinput = false;
    defPrefs.txSetup.isinput = true;
    defPrefs.rxSetup.sampleRate = 48000;
    defPrefs.txSetup.sampleRate = defPrefs.rxSetup.sampleRate;
    defPrefs.rxSetup.codec = 4;
    defPrefs.txSetup.codec = 4;
    defPrefs.rxSetup.resampleQuality = 4;
    defPrefs.usbRxSetup = defPrefs.txSetup;
    defPrefs.usbRxSetup.isinput = true;
    defPrefs.usbTxSetup = defPrefs.rxSetup;
    defPrefs.usbTxSetup.isinput = false;

    // Cluster
    defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.clusterUdpPort = 12060;

    // CW
    defPrefs.cwCutNumbers=false;
    defPrefs.cwSendImmediate=false;
    defPrefs.cwSidetoneEnabled=true;
    defPrefs.cwSidetoneLevel=100;
    defPrefs.cwMacroList = QStringList({ "", "", "", "", "", "", "", "", "", "" });

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.scopeLANPort = 50004;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();
    udpDefPrefs.connectionType = connectionLAN;
    udpDefPrefs.adminLogin = false;


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
        p->presetNum = pn;

        /* Window WindowText Base AlternateBase MainText Button ButtonText BrightText
         * Light MidLight Dark Mid Shadow Highlight HighlightedText Link LinkVisited ToolTipBase ToolTipText Placeholder
        */

        switch (pn)
        {
        case 0:
        {
            // Dark
            p->presetName->append("Dark");
            // Dark scheme - improved with better 3D effects and link colors
            p->window = QColor(0x31, 0x31, 0x31, 255);           // Dark gray background
            p->windowText = QColor(0xef, 0xf0, 0xf1, 255);       // Off-white text
            p->base = QColor(0x2d, 0x2d, 0x2d, 255);
            //p->base = QColor(0x23, 0x26, 0x29, 255);             // Darker gray for input fields
            p->alternateBase = QColor(0x2d, 0x2d, 0x2d, 255);    // Slightly different for alternating rows
            p->mainText = QColor(0xef, 0xf0, 0xf1, 255);         // Off-white text
            p->button = QColor(0x3d, 0x3d, 0x3d, 255);           // Slightly lighter for buttons
            p->buttonText = QColor(0xef, 0xf0, 0xf1, 255);       // Off-white button text
            p->brightText = QColor(0xff, 0xff, 0xff, 255);       // Pure white for high contrast
            p->light = QColor(0x4d, 0x4d, 0x4d, 255);            // Lighter edge (for 3D)
            p->midLight = QColor(0x42, 0x42, 0x42, 255);         // Mid-light edge
            p->dark = QColor(0x1f, 0x1f, 0x1f, 255);             // Darker edge (for 3D)
            p->mid = QColor(0x28, 0x28, 0x28, 255);              // Mid-dark edge
            p->shadow = QColor(0x0, 0x0, 0x0, 255);              // Pure black shadow
            p->highlight = QColor(0x3d, 0xae, 0xe9, 255);        // Blue highlight (good)
            p->highlightedText = QColor(0xff, 0xff, 0xff, 255);  // Pure white on highlight
            p->link = QColor(0x41, 0xcd, 0x52, 255);             // Green links (more visible)
            p->linkVisited = QColor(0x9b, 0x59, 0xb6, 255);      // Purple visited links
            p->toolTipBase = QColor(0x5a, 0x75, 0x66, 255);      // Teal tooltip background (good)
            p->toolTipText = QColor(0xff, 0xff, 0xff, 255);      // White tooltip text
            p->placeholder = QColor(0x88, 0x88, 0x88, 255);      // Gray placeholder (good)
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
            p->presetName->append("Bright");
            // Light scheme - clean and readable
            p->window = QColor(0xef, 0xef, 0xef, 255);           // Light gray background
            p->windowText = QColor(0x23, 0x23, 0x23, 255);       // Dark gray text
            p->base = QColor(0xff, 0xff, 0xff, 255);             // White for input fields
            p->alternateBase = QColor(0xf5, 0xf5, 0xf5, 255);    // Very light gray for alternating rows
            p->mainText = QColor(0x23, 0x23, 0x23, 255);         // Dark gray text
            p->button = QColor(0xe0, 0xe0, 0xe0, 255);           // Light gray button
            p->buttonText = QColor(0x23, 0x23, 0x23, 255);       // Dark gray button text
            p->brightText = QColor(0xff, 0xff, 0xff, 255);       // Pure white for high contrast on dark
            p->light = QColor(0xff, 0xff, 0xff, 255);            // White edge (for 3D)
            p->midLight = QColor(0xf5, 0xf5, 0xf5, 255);         // Very light gray edge
            p->dark = QColor(0xa0, 0xa0, 0xa0, 255);             // Medium gray edge (for 3D)
            p->mid = QColor(0xc8, 0xc8, 0xc8, 255);              // Light-medium gray edge
            p->shadow = QColor(0x50, 0x50, 0x50, 255);           // Medium-dark gray shadow
            p->highlight = QColor(0x30, 0x7f, 0xc1, 255);        // Blue highlight
            p->highlightedText = QColor(0xff, 0xff, 0xff, 255);  // White text on highlight
            p->link = QColor(0x00, 0x7a, 0xcc, 255);             // Blue links
            p->linkVisited = QColor(0x85, 0x37, 0x88, 255);      // Purple visited links
            p->toolTipBase = QColor(0xff, 0xff, 0xdc, 255);      // Light yellow tooltip background
            p->toolTipText = QColor(0x23, 0x23, 0x23, 255);      // Dark gray tooltip text
            p->placeholder = QColor(0xa0, 0xa0, 0xa0, 255);      // Medium gray placeholder
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
        default:
            p->presetName->append(QString("%1").arg(pn));
            p->window = QColor(0xef, 0xef, 0xef, 255);           // Light gray background
            p->windowText = QColor(0x23, 0x23, 0x23, 255);       // Dark gray text
            p->base = QColor(0xff, 0xff, 0xff, 255);             // White for input fields
            p->alternateBase = QColor(0xf5, 0xf5, 0xf5, 255);    // Very light gray for alternating rows
            p->mainText = QColor(0x23, 0x23, 0x23, 255);         // Dark gray text
            p->button = QColor(0xe0, 0xe0, 0xe0, 255);           // Light gray button
            p->buttonText = QColor(0x23, 0x23, 0x23, 255);       // Dark gray button text
            p->brightText = QColor(0xff, 0xff, 0xff, 255);       // Pure white for high contrast on dark
            p->light = QColor(0xff, 0xff, 0xff, 255);            // White edge (for 3D)
            p->midLight = QColor(0xf5, 0xf5, 0xf5, 255);         // Very light gray edge
            p->dark = QColor(0xa0, 0xa0, 0xa0, 255);             // Medium gray edge (for 3D)
            p->mid = QColor(0xc8, 0xc8, 0xc8, 255);              // Light-medium gray edge
            p->shadow = QColor(0x50, 0x50, 0x50, 255);           // Medium-dark gray shadow
            p->highlight = QColor(0x30, 0x7f, 0xc1, 255);        // Blue highlight
            p->highlightedText = QColor(0xff, 0xff, 0xff, 255);  // White text on highlight
            p->link = QColor(0x00, 0x7a, 0xcc, 255);             // Blue links
            p->linkVisited = QColor(0x85, 0x37, 0x88, 255);      // Purple visited links
            p->toolTipBase = QColor(0xff, 0xff, 0xdc, 255);      // Light yellow tooltip background
            p->toolTipText = QColor(0x23, 0x23, 0x23, 255);      // Dark gray tooltip text
            p->placeholder = QColor(0xa0, 0xa0, 0xa0, 255);      // Medium gray placeholder
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
            break;

        }
    }

    qDebug(logGui()) << "Color preset buttons"
                     << "dark" << colorPreset[0].button
                     << "bright" << colorPreset[1].button
                     << "custom" << colorPreset[2].button;
}

void SettingsController::markDirty()
{
    prefs.settingsChanged = true;
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
}

void SettingsController::updateDefaultClusterPrefs()
{
    for (const auto &cluster : std::as_const(prefs.clusters)) {
        if (!cluster.isdefault)
            continue;

        prefs.clusterTcpServerName = cluster.server;
        prefs.clusterTcpUserName = cluster.userName;
        prefs.clusterTcpPassword = cluster.password;
        prefs.clusterTcpPort = cluster.port;
        prefs.clusterTimeout = cluster.timeout;
        updateOptionInMap("Cluster.TcpServerName", prefs.clusterTcpServerName);
        updateOptionInMap("Cluster.TcpUserName", prefs.clusterTcpUserName);
        updateOptionInMap("Cluster.TcpPassword", prefs.clusterTcpPassword);
        updateOptionInMap("Cluster.TcpPort", prefs.clusterTcpPort);
        updateOptionInMap("Cluster.Timeout", prefs.clusterTimeout);
        return;
    }
}

void SettingsController::emitGroupChange(const Binding& b)
{
    if (b.notify) b.notify();
}

void SettingsController::refreshAudioDevices()
{
    audioDev = std::make_unique<audioDevices>(prefs.audioSystem, QFontMetrics(QFont()));
    audioDev->enumerate();

    prefs.rxSetup.type = prefs.audioSystem;
    prefs.txSetup.type = prefs.audioSystem;
    prefs.usbRxSetup.type = prefs.audioSystem;
    prefs.usbTxSetup.type = prefs.audioSystem;

    const int input = audioDev->findInput(QStringLiteral("Client"), prefs.txSetup.name, true);
    if (input >= 0) {
        if (prefs.audioSystem == qtAudio)
            prefs.txSetup.port = audioDev->getInputDeviceInfo(input);
        else
            prefs.txSetup.portInt = audioDev->getInputDeviceInt(input);
        prefs.txSetup.name = audioDev->getInputName(input);
        updateOptionInMap(QStringLiteral("UDP.TxAudio"), prefs.txSetup.name);
    } else {
        prefs.txSetup.port = {};
        prefs.txSetup.portInt = -1;
        prefs.txSetup.name.clear();
        updateOptionInMap(QStringLiteral("UDP.TxAudio"), prefs.txSetup.name);
        qWarning(logAudio()) << "No valid audio input device found after audio system change";
    }

    const int output = audioDev->findOutput(QStringLiteral("Client"), prefs.rxSetup.name, true);
    if (output >= 0) {
        if (prefs.audioSystem == qtAudio)
            prefs.rxSetup.port = audioDev->getOutputDeviceInfo(output);
        else
            prefs.rxSetup.portInt = audioDev->getOutputDeviceInt(output);
        prefs.rxSetup.name = audioDev->getOutputName(output);
        updateOptionInMap(QStringLiteral("UDP.RxAudio"), prefs.rxSetup.name);
    } else {
        prefs.rxSetup.port = {};
        prefs.rxSetup.portInt = -1;
        prefs.rxSetup.name.clear();
        updateOptionInMap(QStringLiteral("UDP.RxAudio"), prefs.rxSetup.name);
        qWarning(logAudio()) << "No valid audio output device found after audio system change";
    }

    const int radioInput = audioDev->findInput(QStringLiteral("Radio"), prefs.usbRxSetup.name, false);
    if (radioInput >= 0) {
        if (prefs.audioSystem == qtAudio)
            prefs.usbRxSetup.port = audioDev->getInputDeviceInfo(radioInput);
        else
            prefs.usbRxSetup.portInt = audioDev->getInputDeviceInt(radioInput);
        prefs.usbRxSetup.name = audioDev->getInputName(radioInput);
    } else {
        prefs.usbRxSetup.port = {};
        prefs.usbRxSetup.portInt = -1;
    }
    updateOptionInMap(QStringLiteral("Radio.USBAudioRXInput"), prefs.usbRxSetup.name);

    const int radioOutput = audioDev->findOutput(QStringLiteral("Radio"), prefs.usbTxSetup.name, false);
    if (radioOutput >= 0) {
        if (prefs.audioSystem == qtAudio)
            prefs.usbTxSetup.port = audioDev->getOutputDeviceInfo(radioOutput);
        else
            prefs.usbTxSetup.portInt = audioDev->getOutputDeviceInt(radioOutput);
        prefs.usbTxSetup.name = audioDev->getOutputName(radioOutput);
    } else {
        prefs.usbTxSetup.port = {};
        prefs.usbTxSetup.portInt = -1;
    }
    updateOptionInMap(QStringLiteral("Radio.USBAudioTXOutput"), prefs.usbTxSetup.name);

    buildUiSpecs();
}

void SettingsController::applyManufacturerDefaults()
{
    prefUDPItems udpItems;

    auto updatePort = [this, &udpItems](const QString &key, auto &field, int value, prefUDPItem item) {
        if (field == value)
            return;
        field = value;
        updateOptionInMap(key, field);
        udpItems |= item;
    };

    auto updateAudioInt = [this, &udpItems](const QString &key, auto &field, int value, prefUDPItem item) {
        if (int(field) == value)
            return;
        field = value;
        updateOptionInMap(key, int(field));
        udpItems |= item;
    };

    switch (prefs.manufacturer) {
    case manufIcom:
        updatePort(QStringLiteral("UDP.ControlLANPort"), udpPrefs.controlLANPort, 50001, prefUDPItem::u_controlLANPort);
        updatePort(QStringLiteral("UDP.SerialLANPort"), udpPrefs.serialLANPort, 50002, prefUDPItem::u_serialLANPort);
        updatePort(QStringLiteral("UDP.AudioLANPort"), udpPrefs.audioLANPort, 50003, prefUDPItem::u_audioLANPort);
        break;
    case manufKenwood:
        updatePort(QStringLiteral("UDP.ControlLANPort"), udpPrefs.controlLANPort, 60000, prefUDPItem::u_controlLANPort);
        updatePort(QStringLiteral("UDP.AudioLANPort"), udpPrefs.audioLANPort, 60001, prefUDPItem::u_audioLANPort);
        updateAudioInt(QStringLiteral("UDP.SampleRate"), prefs.rxSetup.sampleRate, 16000, prefUDPItem::u_sampleRate);
        if (prefs.txSetup.sampleRate != 16000)
            prefs.txSetup.sampleRate = 16000;
        updateAudioInt(QStringLiteral("UDP.RxCodec"), prefs.rxSetup.codec, 4, prefUDPItem::u_rxCodec);
        updateAudioInt(QStringLiteral("UDP.TxCodec"), prefs.txSetup.codec, 4, prefUDPItem::u_txCodec);
        break;
    case manufYaesu:
        updatePort(QStringLiteral("UDP.ControlLANPort"), udpPrefs.controlLANPort, 50000, prefUDPItem::u_controlLANPort);
        updatePort(QStringLiteral("UDP.SerialLANPort"), udpPrefs.serialLANPort, 50001, prefUDPItem::u_serialLANPort);
        updatePort(QStringLiteral("UDP.AudioLANPort"), udpPrefs.audioLANPort, 50002, prefUDPItem::u_audioLANPort);
        updatePort(QStringLiteral("UDP.ScopeLANPort"), udpPrefs.scopeLANPort, 50003, prefUDPItem::u_scopeLANPort);
        break;
    default:
        break;
    }

    if (udpItems)
        emit udpChanged(udpItems);
}


void SettingsController::updateOptionInMap(const QString& iniKey, const QVariant& v)
{
    if (!m_options) {
        qWarning() << "updateOptionInMap: NO m_options (iniKey=" << iniKey << ")";
        return;
    }

    const QString qmlKey = iniKey;

    // IMPORTANT: insert() emits QQmlPropertyMap::valueChanged -> QML updates
    m_options->insert(qmlKey, v);
}


void SettingsController::setOption(const QString& key, const QVariant& value)
{

    auto it = m_bindings.find(key);
    if (it == m_bindings.end()) {
        qWarning() << "setOption: unknown key"
                   << "key=" << key;
        return;
    }

    const QVariant oldVal = it.value().get();

    // Apply change into prefs
    const bool changed = it.value().set(value);
    if (!changed)
        return;

    const QVariant newVal = it.value().get();
    qDebug(logSystem) << "setting changed" << key << oldVal << "->" << newVal;

    // Mark dirty + update map (QML reacts to the map)
    markDirty();
    updateOptionInMap(key, newVal);

    if (key == QStringLiteral("Radio.Manufacturer"))
        applyManufacturerDefaults();

    // Notify other subsystems (your notify lambda)
    emitGroupChange(it.value());
}

QVariant SettingsController::option(const QString& key) const
{
    const QString realKey = key; // same helper as setOption()

    const auto it = m_bindings.constFind(realKey);
    if (it == m_bindings.constEnd()) {
        qWarning() << "option: unknown key"
                   << "key=" << key
                   << "realKey=" << realKey;
        return {};
    }

    const QVariant v = it.value().get();
    return v;
}

bool SettingsController::selectClusterRow(int row)
{
    if (!m_clusterModel || row < 0 || row >= m_clusterModel->count())
        return false;

    m_clusterModel->setDefaultRow(row, true);
    prefs.clusters = m_clusterModel->toList();
    updateDefaultClusterPrefs();
    markDirty();
    emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpServerName) |
                        prefClusterItems(prefClusterItem::cl_clusterTcpUserName) |
                        prefClusterItems(prefClusterItem::cl_clusterTcpPassword) |
                        prefClusterItems(prefClusterItem::cl_clusterTcpPort) |
                        prefClusterItems(prefClusterItem::cl_clusterTimeout));
    return true;
}

void SettingsController::seedOptionsFromBindings()
{
    if (!m_options) return;

    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        m_options->insert(it.key(), it.value().get());
    }
}


// ---- Binding helpers / macros ----
// NOTE: These macros capture "this" and use prefs directly.

//
// ---- Binding table (ONE LINE PER SETTING) ----
// This is how you avoid Q_PROPERTY spam while still keeping QML binding easy.
//
// Base binder
#define WF_BIND(KEY, GETEXPR, SETBODY, NOTIFY)                           \
do {                                                                     \
        Binding b;                                                           \
        b.get = [this]() -> QVariant { return (GETEXPR); };                  \
        b.set = [this](const QVariant& _v) -> bool { SETBODY };              \
        b.notify = (NOTIFY);                                                 \
        m_bindings.insert(QStringLiteral(KEY), std::move(b));                \
} while(false)

// Typed helpers
#define WF_BOOL(KEY, FIELD, NOTIFY)                                      \
    WF_BIND(KEY, QVariant(bool(FIELD)), {                                \
            const bool _nv = _v.toBool();                                    \
            if ((FIELD) == _nv) return false;                                \
            (FIELD) = _nv;                                                   \
            return true;                                                     \
    }, (NOTIFY))

#define WF_I32(KEY, FIELD, NOTIFY)                                       \
    WF_BIND(KEY, QVariant(int(FIELD)), {                                 \
            const int _nv = _v.toInt();                                      \
            if (int(FIELD) == _nv) return false;                             \
            (FIELD) = _nv;                                                   \
            return true;                                                     \
    }, (NOTIFY))

#define WF_F32(KEY, FIELD, NOTIFY)                                       \
    WF_BIND(KEY, QVariant(double(FIELD)), {                              \
            const float _nv = _v.toFloat();                                  \
            if (qFuzzyCompare(float(FIELD), _nv)) return false;              \
            (FIELD) = _nv;                                                   \
            return true;                                                     \
    }, (NOTIFY))

#define WF_F64(KEY, FIELD, NOTIFY)                                       \
    WF_BIND(KEY, QVariant(double(FIELD)), {                              \
            const double _nv = _v.toDouble();                                \
            if (qFuzzyCompare(double(FIELD), _nv)) return false;             \
            (FIELD) = _nv;                                                   \
            return true;                                                     \
    }, (NOTIFY))

#define WF_U8(KEY, FIELD, NOTIFY)                                        \
    WF_BIND(KEY, QVariant(uint(FIELD)), {                                \
            const uint _nv = _v.toUInt();                                    \
            if (uint(FIELD) == _nv) return false;                            \
            (FIELD) = quint8(_nv);                                           \
            return true;                                                     \
    }, (NOTIFY))

#define WF_U16(KEY, FIELD, NOTIFY)                                       \
    WF_BIND(KEY, QVariant(uint(FIELD)), {                                \
            const uint _nv = _v.toUInt();                                    \
            if (uint(FIELD) == _nv) return false;                            \
            (FIELD) = quint16(_nv);                                          \
            return true;                                                     \
    }, (NOTIFY))

#define WF_U32(KEY, FIELD, NOTIFY)                                       \
    WF_BIND(KEY, QVariant(uint(FIELD)), {                                \
            const uint _nv = _v.toUInt();                                    \
            if (uint(FIELD) == _nv) return false;                            \
            (FIELD) = quint32(_nv);                                          \
            return true;                                                     \
    }, (NOTIFY))

#define WF_STR(KEY, FIELD, NOTIFY)                                       \
    WF_BIND(KEY, QVariant(FIELD), {                                      \
            const QString _nv = _v.toString();                               \
            if ((FIELD) == _nv) return false;                                \
            (FIELD) = _nv;                                                   \
            return true;                                                     \
    }, (NOTIFY))

#define WF_CHAR(KEY, FIELD, NOTIFY)                                      \
    WF_BIND(KEY, QVariant(QString(FIELD)), {                             \
            const QString _s = _v.toString();                                \
            const QChar _nv = _s.isEmpty() ? QChar() : _s.at(0);             \
            if ((FIELD) == _nv) return false;                                \
            (FIELD) = _nv;                                                   \
            return true;                                                     \
    }, (NOTIFY))

// Enum stored as int in QML
#define WF_ENUM_I32(KEY, FIELD, ENUMTYPE, NOTIFY)                        \
    WF_BIND(KEY, QVariant(int(FIELD)), {                                 \
            const int _nv = _v.toInt();                                      \
            if (int(FIELD) == _nv) return false;                             \
            (FIELD) = ENUMTYPE(_nv);                                         \
            return true;                                                     \
    }, (NOTIFY))


#define WF_COLOR(KEY, FIELD_EXPR, NOTIFY)                                  \
WF_BIND(KEY, qColorToVariant((FIELD_EXPR)), {                          \
        const QColor _nv = variantToQColor(_v);                            \
        if (!_nv.isValid()) return false;                                  \
        if ((FIELD_EXPR) == _nv) return false;                             \
        (FIELD_EXPR) = _nv;                                                \
        return true;                                                       \
}, (NOTIFY))

#define WF_PRESETNAME(KEY, NOTIFY)                                         \
WF_BIND(KEY, QVariant(curColor().presetName ? *curColor().presetName : QString()), { \
        const QString _nv = _v.toString();                                 \
        auto &cp = curColor();                                             \
        if (!cp.presetName) cp.presetName = new QString();                 \
        if (*cp.presetName == _nv) return false;                           \
        *cp.presetName = _nv;                                              \
        return true;                                                       \
}, (NOTIFY))


/* This function will build all bindings between the currently loaded settings and
 * QML. It will also emit the correct signal on changes */
void SettingsController::buildBindings()
{
    m_bindings.clear();

    // -------------------------
    // LAN group
    // -------------------------
    WF_BOOL("LAN.EnableLAN", prefs.enableLAN,
            [this](){ emit lanChanged(prefLanItems(prefLanItem::l_enableLAN)); });

    WF_BOOL("LAN.EnableRigCtlD", prefs.enableRigCtlD,
            [this](){ emit lanChanged(prefLanItems(prefLanItem::l_enableRigCtlD)); });

    WF_U16("LAN.RigCtlPort", prefs.rigCtlPort,
           [this](){ emit lanChanged(prefLanItems(prefLanItem::l_rigCtlPort)); });

    WF_U16("LAN.TCPPort", prefs.tcpPort,
           [this](){ emit lanChanged(prefLanItems(prefLanItem::l_tcpPort)); });

    WF_U16("LAN.TCIPort", prefs.tciPort,
           [this](){ emit lanChanged(prefLanItems(prefLanItem::l_tciPort)); });

    WF_U8("LAN.WaterfallFormat", prefs.waterfallFormat,
          [this](){ emit lanChanged(prefLanItems(prefLanItem::l_waterfallFormat)); });

    // -------------------------
    // Experimental group
    // -------------------------
    WF_BOOL("Experimental.WfShareEnabled", prefs.wfShareEnabled, [](){});
    WF_BOOL("Experimental.WfShareDirectMode", prefs.wfShareDirectMode, [](){});
    WF_STR("Experimental.WfShareHost", prefs.wfShareHost, [](){});
    WF_U16("Experimental.WfSharePort", prefs.wfSharePort, [](){});
    WF_STR("Experimental.WfShareUsername", prefs.wfShareUsername, [](){});
    WF_STR("Experimental.WfSharePassword", prefs.wfSharePassword, [](){});
    WF_STR("Experimental.WfShareCalledNumber", prefs.wfShareCalledNumber, [](){});

    // -------------------------
    // Radio Access group (RA)
    // -------------------------
    WF_ENUM_I32("Radio.Manufacturer", prefs.manufacturer, manufacturersType_t,
                [this](){ emit raChanged(prefRaItems(prefRaItem::ra_manufacturer)); });

    WF_U8("Radio.CIVAddr", prefs.radioCIVAddr,
          [this](){ emit raChanged(prefRaItems(prefRaItem::ra_radioCIVAddr)); });

    WF_BOOL("Radio.CIVisRadioModel", prefs.CIVisRadioModel,
            [this](){ emit raChanged(prefRaItems(prefRaItem::ra_CIVisRadioModel)); });

    WF_ENUM_I32("Radio.PTTType", prefs.pttType, pttType_t,
                [this](){ emit raChanged(prefRaItems(prefRaItem::ra_pttType)); });

    WF_I32("Radio.PollingMS", prefs.polling_ms,
           [this](){ emit raChanged(prefRaItems(prefRaItem::ra_polling_ms)); });

    WF_BOOL("Radio.UseUTC", prefs.useUTC,
            [this](){ emit rsChanged(prefRsItems(prefRsItem::rs_clockUseUtc)); });

    WF_BOOL("Radio.SetRadioTime", prefs.setRadioTime,
            [this](){ emit rsChanged(prefRsItems(prefRsItem::rs_setRadioTime)); });

    WF_STR("Radio.SerialPortRadio", prefs.serialPortRadio,
           [this](){ emit raChanged(prefRaItems(prefRaItem::ra_serialPortRadio)); });

    WF_U32("Radio.SerialPortBaud", prefs.serialPortBaud,
           [this](){ emit raChanged(prefRaItems(prefRaItem::ra_serialPortBaud)); });

    WF_STR("Radio.VirtualSerialPort", prefs.virtualSerialPort,
           [this](){ emit raChanged(prefRaItems(prefRaItem::ra_virtualSerialPort)); });

    WF_U8("Radio.LocalAFGain", prefs.localAFgain,
          [this](){ emit raChanged(prefRaItems(prefRaItem::ra_localAFgain)); });

    WF_ENUM_I32("Radio.AudioSystem", prefs.audioSystem, audioType,
                [this](){
                    refreshAudioDevices();
                    emit raChanged(prefRaItems(prefRaItem::ra_audioSystem));
                });

    WF_BOOL("Radio.EnableUSBAudio", prefs.enableUsbAudio,
            [this](){ emit raChanged(prefRaItems(prefRaItem::ra_usbAudio)); });

    WF_BIND("Radio.USBAudioRXInput", QVariant(prefs.usbRxSetup.name), {
        const QString name = _v.toString();
        if (prefs.usbRxSetup.name == name)
            return false;
        prefs.usbRxSetup.name = name;
        const int index = audioDev ? audioDev->findInput(QStringLiteral("Radio"), name, true) : -1;
        if (index >= 0) {
            if (prefs.audioSystem == qtAudio)
                prefs.usbRxSetup.port = audioDev->getInputDeviceInfo(index);
            else
                prefs.usbRxSetup.portInt = audioDev->getInputDeviceInt(index);
            prefs.usbRxSetup.name = audioDev->getInputName(index);
        } else {
            prefs.usbRxSetup.port = {};
            prefs.usbRxSetup.portInt = -1;
        }
        return true;
    }, [this](){ emit raChanged(prefRaItems(prefRaItem::ra_usbAudioRx)); });

    WF_BIND("Radio.USBAudioTXOutput", QVariant(prefs.usbTxSetup.name), {
        const QString name = _v.toString();
        if (prefs.usbTxSetup.name == name)
            return false;
        prefs.usbTxSetup.name = name;
        const int index = audioDev ? audioDev->findOutput(QStringLiteral("Radio"), name, true) : -1;
        if (index >= 0) {
            if (prefs.audioSystem == qtAudio)
                prefs.usbTxSetup.port = audioDev->getOutputDeviceInfo(index);
            else
                prefs.usbTxSetup.portInt = audioDev->getOutputDeviceInt(index);
            prefs.usbTxSetup.name = audioDev->getOutputName(index);
        } else {
            prefs.usbTxSetup.port = {};
            prefs.usbTxSetup.portInt = -1;
        }
        return true;
    }, [this](){ emit raChanged(prefRaItems(prefRaItem::ra_usbAudioTx)); });

    // -------------------------
    // Interface group (IF) - common ones
    // -------------------------
    WF_BOOL("Interface.UseFullScreen", prefs.useFullScreen,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_useFullScreen)); });

    WF_BOOL("Interface.UseSystemTheme", prefs.useSystemTheme,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_useSystemTheme)); });

    WF_BOOL("Interface.DrawPeaks", prefs.drawPeaks,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_drawPeaks)); });

    WF_ENUM_I32("Interface.UnderlayMode", prefs.underlayMode, underlay_t,
                [this](){ emit ifChanged(prefIfItems(prefIfItem::if_underlayMode)); });

    WF_I32("Interface.UnderlayBufferSize", prefs.underlayBufferSize,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_underlayBufferSize)); });

    WF_BOOL("Interface.WFAntiAlias", prefs.wfAntiAlias,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_wfAntiAlias)); });

    WF_BOOL("Interface.WFInterpolate", prefs.wfInterpolate,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_wfInterpolate)); });

    WF_I32("Interface.MainPlotFloor", prefs.mainPlotFloor,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_plotFloor)); });

    WF_I32("Interface.SubPlotFloor", prefs.subPlotFloor,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_plotFloor)); });

    WF_I32("Interface.MainPlotCeiling", prefs.mainPlotCeiling,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_plotCeiling)); });

    WF_I32("Interface.SubPlotCeiling", prefs.subPlotCeiling,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_plotCeiling)); });

    WF_I32("Interface.MainWFLength", prefs.mainWflength,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_wflength)); });

    WF_I32("Interface.SubWFLength", prefs.subWflength,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_wflength)); });

    WF_I32("Interface.MainWFTheme", prefs.mainWfTheme,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_wftheme)); });

    WF_I32("Interface.SubWFTheme", prefs.subWfTheme,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_wftheme)); });

    WF_STR("Interface.StylesheetPath", prefs.stylesheetPath,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_stylesheetPath)); });

    WF_BOOL("Interface.RigCreatorEnable", prefs.rigCreatorEnable,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_rigCreatorEnable)); });

    WF_BOOL("Interface.ClickDragTuningEnable", prefs.clickDragTuningEnable,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_clickDragTuningEnable)); });

    WF_I32("Interface.FrequencyUnits", prefs.frequencyUnits,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_frequencyUnits)); });

    WF_STR("Interface.Region", prefs.region,
           [this](){ emit ifChanged(prefIfItems(prefIfItem::if_region)); });

    WF_BOOL("Interface.ShowBands", prefs.showBands,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_showBands)); });

    WF_ENUM_I32("Interface.Meter2Type", prefs.meter2Type, meter_t,
                [this](){ emit ifChanged(prefIfItems(prefIfItem::if_meter2Type)); });

    WF_ENUM_I32("Interface.Meter3Type", prefs.meter3Type, meter_t,
                [this](){ emit ifChanged(prefIfItems(prefIfItem::if_meter3Type)); });

    WF_BOOL("Interface.CompMeterReverse", prefs.compMeterReverse,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_compMeterReverse)); });

    WF_CHAR("Interface.DecimalSeparator", prefs.decimalSeparator,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_separators)); });

    WF_CHAR("Interface.GroupSeparator", prefs.groupSeparator,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_separators)); });

    WF_BOOL("Interface.ForceVfoMode", prefs.forceVfoMode,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_forceVfoMode)); });

    WF_BOOL("Interface.AutoPowerOn", prefs.autoPowerOn,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_autoPowerOn)); });

    WF_BOOL("Interface.ConfirmSettingsChanged", prefs.confirmSettingsChanged,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_confirmExit)); });

    WF_BOOL("Interface.ConfirmExit", prefs.confirmExit,
            [this](){ emit ifChanged(prefIfItems(prefIfItem::if_confirmExit)); });

    // -------------------------
    // Controls group (CT)
    // -------------------------
    WF_BOOL("Controls.EnablePTT", prefs.enablePTT,
            [this](){ emit ctChanged(prefCtItems(prefCtItem::ct_enablePTT)); });

    WF_BOOL("Controls.NiceTS", prefs.niceTS,
            [this](){ emit ctChanged(prefCtItems(prefCtItem::ct_niceTS)); });

    WF_BOOL("Controls.AutomaticSidebandSwitching", prefs.automaticSidebandSwitching,
            [this](){ emit ctChanged(prefCtItems(prefCtItem::ct_automaticSidebandSwitching)); });

    WF_BOOL("Controls.EnableUSBControllers", prefs.enableUSBControllers,
            [this](){ emit ctChanged(prefCtItems(prefCtItem::ct_enableUSBControllers)); });

    // -------------------------
    // Cluster group (basic)
    // -------------------------
    WF_BOOL("Cluster.UdpEnabled", prefs.clusterUdpEnable,
            [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterUdpEnable)); });

    WF_BOOL("Cluster.TcpEnabled", prefs.clusterTcpEnable,
            [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpEnable)); });

    WF_I32("Cluster.UdpPort", prefs.clusterUdpPort,
           [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterUdpPort)); });

    WF_I32("Cluster.TcpPort", prefs.clusterTcpPort,
           [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpPort)); });

    WF_I32("Cluster.Timeout", prefs.clusterTimeout,
           [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTimeout)); });

    WF_BOOL("Cluster.SkimmerSpotsEnable", prefs.clusterSkimmerSpotsEnable,
            [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterSkimmerSpotsEnable)); });

    WF_STR("Cluster.TcpServerName", prefs.clusterTcpServerName,
           [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpServerName)); });

    WF_STR("Cluster.TcpUserName", prefs.clusterTcpUserName,
           [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpUserName)); });

    WF_STR("Cluster.TcpPassword", prefs.clusterTcpPassword,
           [this](){ emit clusterChanged(prefClusterItems(prefClusterItem::cl_clusterTcpPassword)); });

    // -------------------------
    // UDP group (u_*)
    // -------------------------
    WF_STR("UDP.IPAddress", udpPrefs.ipAddress,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_ipAddress)); });

    WF_I32("UDP.ControlLANPort", udpPrefs.controlLANPort,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_controlLANPort)); });

    WF_I32("UDP.SerialLANPort", udpPrefs.serialLANPort,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_serialLANPort)); });

    WF_I32("UDP.AudioLANPort", udpPrefs.audioLANPort,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_audioLANPort)); });

    WF_I32("UDP.ScopeLANPort", udpPrefs.scopeLANPort,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_scopeLANPort)); });

    WF_STR("UDP.Username", udpPrefs.username,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_username)); });

    WF_STR("UDP.Password", udpPrefs.password,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_password)); });

    WF_STR("UDP.ClientName", udpPrefs.clientName,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_clientName)); });

    WF_BOOL("UDP.HalfDuplex", udpPrefs.halfDuplex,
            [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_halfDuplex)); });

    // If connectionType is an enum, expose as int in QML
    WF_ENUM_I32("UDP.ConnectionType", udpPrefs.connectionType, connectionType_t,
                [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_connectionType)); });

    WF_BOOL("UDP.AdminLogin", udpPrefs.adminLogin,
            [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_adminLogin)); });

    WF_I32("UDP.SampleRate", prefs.rxSetup.sampleRate,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_sampleRate)); });

    WF_U16("UDP.RxLatency", prefs.rxSetup.latency,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_rxLatency)); });

    WF_U8("UDP.RxCodec", prefs.rxSetup.codec,
          [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_rxCodec)); });

    WF_U16("UDP.TxLatency", prefs.txSetup.latency,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_txLatency)); });

    WF_U8("UDP.TxCodec", prefs.txSetup.codec,
          [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_txCodec)); });

    WF_STR("UDP.TxAudio", prefs.txSetup.name,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_audioInput)); });

    WF_STR("UDP.RxAudio", prefs.rxSetup.name,
           [this](){ emit udpChanged(prefUDPItems(prefUDPItem::u_audioOutput)); });

    // -------------------------
    // Server group
    // -------------------------
    WF_BOOL("Server.Enabled", serverConfig.enabled,
            [this](){ emit serverChanged(prefServerItems(prefServerItem::s_enabled)); });

    WF_BOOL("Server.DisableUI", serverConfig.disableUI,
            [this](){ emit serverChanged(prefServerItems(prefServerItem::s_disableui)); });

    WF_I32("Server.ControlPort", serverConfig.controlPort,
           [this](){ emit serverChanged(prefServerItems(prefServerItem::s_controlPort)); });

    WF_I32("Server.CivPort", serverConfig.civPort,
           [this](){ emit serverChanged(prefServerItems(prefServerItem::s_civPort)); });

    WF_I32("Server.AudioPort", serverConfig.audioPort,
           [this](){ emit serverChanged(prefServerItems(prefServerItem::s_audioPort)); });

    WF_BIND("Server.AudioInput", QVariant(serverConfig.rigs.isEmpty() ? QString() : serverConfig.rigs.first()->rxAudioSetup.name), {
        if (serverConfig.rigs.isEmpty())
            return false;
        const QString name = _v.toString();
        auto *rig = serverConfig.rigs.first();
        if (rig->rxAudioSetup.name == name)
            return false;

        rig->rxAudioSetup.name = name;
        const int index = audioDev ? audioDev->findInput("Server", name, true) : -1;
        if (index >= 0) {
            if (prefs.audioSystem == qtAudio)
                rig->rxAudioSetup.port = audioDev->getInputDeviceInfo(index);
            else
                rig->rxAudioSetup.portInt = audioDev->getInputDeviceInt(index);
        }
        return true;
    }, [this](){ emit serverChanged(prefServerItems(prefServerItem::s_audioInput)); });

    WF_BIND("Server.AudioOutput", QVariant(serverConfig.rigs.isEmpty() ? QString() : serverConfig.rigs.first()->txAudioSetup.name), {
        if (serverConfig.rigs.isEmpty())
            return false;
        const QString name = _v.toString();
        auto *rig = serverConfig.rigs.first();
        if (rig->txAudioSetup.name == name)
            return false;

        rig->txAudioSetup.name = name;
        const int index = audioDev ? audioDev->findOutput("Server", name, true) : -1;
        if (index >= 0) {
            if (prefs.audioSystem == qtAudio)
                rig->txAudioSetup.port = audioDev->getOutputDeviceInfo(index);
            else
                rig->txAudioSetup.portInt = audioDev->getOutputDeviceInt(index);
        }
        return true;
    }, [this](){ emit serverChanged(prefServerItems(prefServerItem::s_audioOutput)); });

    auto audioNotify = [this](){ emit audioProcChanged(); };

    WF_BOOL("AudioProc.Tx.Bypass", prefs.txAudioProc.bypass, audioNotify);
    WF_BOOL("AudioProc.Tx.CompEnabled", prefs.txAudioProc.compEnabled, audioNotify);
    WF_BOOL("AudioProc.Tx.EqEnabled", prefs.txAudioProc.eqEnabled, audioNotify);
    WF_BOOL("AudioProc.Tx.EqFirst", prefs.txAudioProc.eqFirst, audioNotify);
    WF_F32("AudioProc.Tx.InputGainDB", prefs.txAudioProc.inputGainDB, audioNotify);
    WF_F32("AudioProc.Tx.OutputGainDB", prefs.txAudioProc.outputGainDB, audioNotify);
    WF_F32("AudioProc.Tx.CompPeakLimit", prefs.txAudioProc.compPeakLimit, audioNotify);
    WF_F32("AudioProc.Tx.CompRelease", prefs.txAudioProc.compRelease, audioNotify);
    WF_F32("AudioProc.Tx.CompFastRatio", prefs.txAudioProc.compFastRatio, audioNotify);
    WF_F32("AudioProc.Tx.CompSlowRatio", prefs.txAudioProc.compSlowRatio, audioNotify);
    WF_BOOL("AudioProc.Tx.SidetoneEnabled", prefs.txAudioProc.sidetoneEnabled, audioNotify);
    WF_F32("AudioProc.Tx.SidetoneLevel", prefs.txAudioProc.sidetoneLevel, audioNotify);
    WF_BOOL("AudioProc.Tx.MuteRx", prefs.txAudioProc.muteRx, audioNotify);
    WF_BOOL("AudioProc.Tx.SpectrumEnabled", prefs.txAudioProc.spectrumEnabled, audioNotify);
    WF_I32("AudioProc.Tx.SpectrumFPS", prefs.txAudioProc.spectrumFPS, audioNotify);
    WF_BOOL("AudioProc.Tx.SpecInhibitDuringRx", prefs.txAudioProc.specInhibitDuringRx, audioNotify);
    WF_BOOL("AudioProc.Tx.GateEnabled", prefs.txAudioProc.gateEnabled, audioNotify);
    WF_F32("AudioProc.Tx.GateThreshold", prefs.txAudioProc.gateThreshold, audioNotify);
    WF_F32("AudioProc.Tx.GateAttack", prefs.txAudioProc.gateAttack, audioNotify);
    WF_F32("AudioProc.Tx.GateHold", prefs.txAudioProc.gateHold, audioNotify);
    WF_F32("AudioProc.Tx.GateDecay", prefs.txAudioProc.gateDecay, audioNotify);
    WF_F32("AudioProc.Tx.GateRange", prefs.txAudioProc.gateRange, audioNotify);
    WF_F32("AudioProc.Tx.GateLfCutoff", prefs.txAudioProc.gateLfCutoff, audioNotify);
    WF_F32("AudioProc.Tx.GateHfCutoff", prefs.txAudioProc.gateHfCutoff, audioNotify);
    for (int i = 0; i < TX_AUDIO_EQ_BANDS; ++i) {
        const QString key = QStringLiteral("AudioProc.Tx.EqBand%1").arg(i);
        Binding b;
        b.get = [this, i]() -> QVariant { return double(prefs.txAudioProc.eqBands[i]); };
        b.set = [this, i](const QVariant& _v) -> bool {
            const float nv = _v.toFloat();
            if (qFuzzyCompare(prefs.txAudioProc.eqBands[i], nv)) return false;
            prefs.txAudioProc.eqBands[i] = nv;
            return true;
        };
        b.notify = audioNotify;
        m_bindings.insert(key, std::move(b));
    }

    WF_BOOL("AudioProc.Rx.Bypass", prefs.rxAudioProc.bypass, audioNotify);
    WF_BOOL("AudioProc.Rx.NrEnabled", prefs.rxAudioProc.nrEnabled, audioNotify);
    WF_ENUM_I32("AudioProc.Rx.NrMode", prefs.rxAudioProc.nrMode, RxNrMode, audioNotify);
    WF_I32("AudioProc.Rx.ChannelSelect", prefs.rxAudioProc.channelSelect, audioNotify);
    WF_I32("AudioProc.Rx.SpeexSuppression", prefs.rxAudioProc.speexSuppression, audioNotify);
    WF_I32("AudioProc.Rx.SpeexBandsPreset", prefs.rxAudioProc.speexBandsPreset, audioNotify);
    WF_I32("AudioProc.Rx.SpeexFrameMs", prefs.rxAudioProc.speexFrameMs, audioNotify);
    WF_BOOL("AudioProc.Rx.SpeexAgc", prefs.rxAudioProc.speexAgc, audioNotify);
    WF_F32("AudioProc.Rx.SpeexAgcLevel", prefs.rxAudioProc.speexAgcLevel, audioNotify);
    WF_I32("AudioProc.Rx.SpeexAgcMaxGain", prefs.rxAudioProc.speexAgcMaxGain, audioNotify);
    WF_BOOL("AudioProc.Rx.SpeexVad", prefs.rxAudioProc.speexVad, audioNotify);
    WF_I32("AudioProc.Rx.SpeexVadProbStart", prefs.rxAudioProc.speexVadProbStart, audioNotify);
    WF_I32("AudioProc.Rx.SpeexVadProbCont", prefs.rxAudioProc.speexVadProbCont, audioNotify);
    WF_F32("AudioProc.Rx.SpeexSnrDecay", prefs.rxAudioProc.speexSnrDecay, audioNotify);
    WF_F32("AudioProc.Rx.SpeexNoiseUpdateRate", prefs.rxAudioProc.speexNoiseUpdateRate, audioNotify);
    WF_F32("AudioProc.Rx.SpeexPriorBase", prefs.rxAudioProc.speexPriorBase, audioNotify);
    WF_F64("AudioProc.Rx.AnrNoiseReductionDb", prefs.rxAudioProc.anrNoiseReductionDb, audioNotify);
    WF_F64("AudioProc.Rx.AnrSensitivity", prefs.rxAudioProc.anrSensitivity, audioNotify);
    WF_I32("AudioProc.Rx.AnrFreqSmoothing", prefs.rxAudioProc.anrFreqSmoothing, audioNotify);
    WF_BOOL("AudioProc.Rx.EqEnabled", prefs.rxAudioProc.eqEnabled, audioNotify);
    WF_F32("AudioProc.Rx.OutputGainDB", prefs.rxAudioProc.outputGainDB, audioNotify);
    WF_BOOL("AudioProc.Rx.SpectrumEnabled", prefs.rxAudioProc.spectrumEnabled, audioNotify);
    WF_I32("AudioProc.Rx.SpectrumFPS", prefs.rxAudioProc.spectrumFPS, audioNotify);
    WF_BOOL("AudioProc.Rx.SpecInhibitDuringTx", prefs.rxAudioProc.specInhibitDuringTx, audioNotify);
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        const QString gainKey = QStringLiteral("AudioProc.Rx.EqGain%1").arg(i);
        Binding gain;
        gain.get = [this, i]() -> QVariant { return double(prefs.rxAudioProc.eqGain[i]); };
        gain.set = [this, i](const QVariant& _v) -> bool {
            const float nv = _v.toFloat();
            if (qFuzzyCompare(prefs.rxAudioProc.eqGain[i], nv)) return false;
            prefs.rxAudioProc.eqGain[i] = nv;
            return true;
        };
        gain.notify = audioNotify;
        m_bindings.insert(gainKey, std::move(gain));

        const QString freqKey = QStringLiteral("AudioProc.Rx.EqFreq%1").arg(i);
        Binding freq;
        freq.get = [this, i]() -> QVariant { return double(prefs.rxAudioProc.eqFreq[i]); };
        freq.set = [this, i](const QVariant& _v) -> bool {
            const float nv = _v.toFloat();
            if (qFuzzyCompare(prefs.rxAudioProc.eqFreq[i], nv)) return false;
            prefs.rxAudioProc.eqFreq[i] = nv;
            return true;
        };
        freq.notify = audioNotify;
        m_bindings.insert(freqKey, std::move(freq));

        const QString qKey = QStringLiteral("AudioProc.Rx.EqQ%1").arg(i);
        Binding q;
        q.get = [this, i]() -> QVariant { return double(prefs.rxAudioProc.eqQ[i]); };
        q.set = [this, i](const QVariant& _v) -> bool {
            const float nv = _v.toFloat();
            if (qFuzzyCompare(prefs.rxAudioProc.eqQ[i], nv)) return false;
            prefs.rxAudioProc.eqQ[i] = nv;
            return true;
        };
        q.notify = audioNotify;
        m_bindings.insert(qKey, std::move(q));
    }

   /*
     * prefs.rxSetup.isinput = defPrefs.rxSetup.isinput;
    prefs.rxSetup.latency = settings->value("AudioRXLatency", defPrefs.rxSetup.latency).toInt();
    prefs.rxSetup.sampleRate=settings->value("AudioRXSampleRate", defPrefs.rxSetup.sampleRate).toInt();
    prefs.rxSetup.codec = settings->value("AudioRXCodec", defPrefs.rxSetup.codec).toInt();
    prefs.rxSetup.resampleQuality = settings->value("ResampleQuality", defPrefs.rxSetup.resampleQuality).toInt();
    prefs.rxSetup.type = prefs.audioSystem;
    qInfo(logGui()) << "Got Audio Output from Settings: " << prefs.rxSetup.name;

    prefs.txSetup.latency = settings->value("AudioTXLatency", defPrefs.txSetup.latency).toInt();
    prefs.txSetup.isinput = defPrefs.txSetup.isinput;
    prefs.txSetup.sampleRate=prefs.rxSetup.sampleRate;
    prefs.txSetup.codec = settings->value("AudioTXCodec", defPrefs.txSetup.codec).toInt();
    prefs.txSetup.resampleQuality = prefs.rxSetup.resampleQuality;
    prefs.txSetup.type = prefs.audioSystem;
    qInfo(logGui()) << "Got Audio Input from Settings: " << prefs.txSetup.name;
     *
     * */


    /* ----- Current color preset bindings -----
     * If any more colors are added, they must be added here and to the refreshCurrentColorPresetOptions() function below
     * refreshCurrentColorPresetOptions() must also be called whenever currentColorPresetNumber is changed.
     */

    WF_I32("Interface.CurrentColorPresetNumber", prefs.currentColorPresetNumber,
           [this](){
               emit ifChanged(prefIfItems(prefIfItem::if_currentColorPresetNumber));
               refreshCurrentColorPresetOptions(false); // IMPORTANT
               emit colChanged(prefColItems(prefColItem::col_all));
           });


    WF_PRESETNAME("Color.PresetName",
                  [this](){ emit colChanged(prefColItems(prefColItem::col_all)); });

    WF_I32("Color.PresetNum", curColor().presetNum,
           [this](){ emit colChanged(prefColItems(prefColItem::col_all)); });

    // Window default colors


    WF_COLOR("Color.Window",              curColor().window,
             [this](){ emit colChanged(prefColItems(prefColItem::col_window)); });
    WF_COLOR("Color.WindowText",              curColor().windowText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_windowText)); });
    WF_COLOR("Color.Base",              curColor().base,
             [this](){ emit colChanged(prefColItems(prefColItem::col_base)); });
    WF_COLOR("Color.AlternateBase",              curColor().alternateBase,
             [this](){ emit colChanged(prefColItems(prefColItem::col_alternateBase)); });
    WF_COLOR("Color.MainText",              curColor().mainText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_mainText)); });
    WF_COLOR("Color.Button",              curColor().button,
             [this](){ emit colChanged(prefColItems(prefColItem::col_button)); });
    WF_COLOR("Color.ButtonText",              curColor().buttonText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_buttonText)); });
    WF_COLOR("Color.BrightText",              curColor().brightText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_brightText)); });
    WF_COLOR("Color.Light",              curColor().light,
             [this](){ emit colChanged(prefColItems(prefColItem::col_light)); });
    WF_COLOR("Color.MidLight",              curColor().midLight,
             [this](){ emit colChanged(prefColItems(prefColItem::col_midLight)); });
    WF_COLOR("Color.Dark",              curColor().dark,
             [this](){ emit colChanged(prefColItems(prefColItem::col_dark)); });
    WF_COLOR("Color.Mid",              curColor().mid,
             [this](){ emit colChanged(prefColItems(prefColItem::col_mid)); });
    WF_COLOR("Color.Shadow",              curColor().shadow,
             [this](){ emit colChanged(prefColItems(prefColItem::col_shadow)); });
    WF_COLOR("Color.Highlight",              curColor().highlight,
             [this](){ emit colChanged(prefColItems(prefColItem::col_highlight)); });
    WF_COLOR("Color.HighlightedText",              curColor().highlightedText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_highlightedText)); });
    WF_COLOR("Color.Link",              curColor().link,
             [this](){ emit colChanged(prefColItems(prefColItem::col_link)); });
    WF_COLOR("Color.LinkVisited",              curColor().linkVisited,
             [this](){ emit colChanged(prefColItems(prefColItem::col_linkVisited)); });
    WF_COLOR("Color.ToolTipBase",              curColor().toolTipBase,
             [this](){ emit colChanged(prefColItems(prefColItem::col_toolTipBase)); });
    WF_COLOR("Color.ToolTipText",              curColor().toolTipText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_toolTipText)); });
    WF_COLOR("Color.Placeholder",              curColor().placeholder,
             [this](){ emit colChanged(prefColItems(prefColItem::col_placeholder)); });

    // Spectrum plot
    WF_COLOR("Color.Grid",              curColor().gridColor,
             [this](){ emit colChanged(prefColItems(prefColItem::col_grid)); });
    WF_COLOR("Color.Axis",              curColor().axisColor,
             [this](){ emit colChanged(prefColItems(prefColItem::col_axis)); });
    WF_COLOR("Color.Text",              curColor().textColor,
             [this](){ emit colChanged(prefColItems(prefColItem::col_text)); });
    WF_COLOR("Color.PlotBackground",    curColor().plotBackground,
             [this](){ emit colChanged(prefColItems(prefColItem::col_plotBackground)); });

    WF_COLOR("Color.SpectrumLine",      curColor().spectrumLine,
             [this](){ emit colChanged(prefColItems(prefColItem::col_spectrumLine)); });
    WF_COLOR("Color.SpectrumFill",      curColor().spectrumFill,
             [this](){ emit colChanged(prefColItems(prefColItem::col_spectrumFill)); });
    WF_BOOL ("Color.UseSpectrumFillGradient", curColor().useSpectrumFillGradient,
            [this](){ emit colChanged(prefColItems(prefColItem::col_useSpectrumFillGradient)); });
    WF_COLOR("Color.SpectrumFillTop",   curColor().spectrumFillTop,
             [this](){ emit colChanged(prefColItems(prefColItem::col_spectrumFillTop)); });
    WF_COLOR("Color.SpectrumFillBot",   curColor().spectrumFillBot,
             [this](){ emit colChanged(prefColItems(prefColItem::col_spectrumFillBot)); });

    WF_COLOR("Color.UnderlayLine",      curColor().underlayLine,
             [this](){ emit colChanged(prefColItems(prefColItem::col_underlayLine)); });
    WF_COLOR("Color.UnderlayFill",      curColor().underlayFill,
             [this](){ emit colChanged(prefColItems(prefColItem::col_underlayFill)); });
    WF_BOOL ("Color.UseUnderlayFillGradient", curColor().useUnderlayFillGradient,
            [this](){ emit colChanged(prefColItems(prefColItem::col_useUnderlayFillGradient)); });
    WF_COLOR("Color.UnderlayFillTop",   curColor().underlayFillTop,
             [this](){ emit colChanged(prefColItems(prefColItem::col_underlayFillTop)); });
    WF_COLOR("Color.UnderlayFillBot",   curColor().underlayFillBot,
             [this](){ emit colChanged(prefColItems(prefColItem::col_underlayFillBot)); });

    WF_COLOR("Color.TuningLine",        curColor().tuningLine,
             [this](){ emit colChanged(prefColItems(prefColItem::col_tuningLine)); });
    WF_COLOR("Color.Passband",          curColor().passband,
             [this](){ emit colChanged(prefColItems(prefColItem::col_passband)); });
    WF_COLOR("Color.PbtIndicator",      curColor().pbt,
             [this](){ emit colChanged(prefColItems(prefColItem::col_pbtIndicator)); });

    // Waterfall
    WF_COLOR("Color.WaterfallBack",     curColor().wfBackground,
             [this](){ emit colChanged(prefColItems(prefColItem::col_waterfallBack)); });
    WF_COLOR("Color.WaterfallGrid",     curColor().wfGrid,
             [this](){ emit colChanged(prefColItems(prefColItem::col_waterfallGrid)); });
    WF_COLOR("Color.WaterfallAxis",     curColor().wfAxis,
             [this](){ emit colChanged(prefColItems(prefColItem::col_waterfallAxis)); });
    WF_COLOR("Color.WaterfallText",     curColor().wfText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_waterfallText)); });

    // Meters
    WF_COLOR("Color.MeterLevel",        curColor().meterLevel,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterLevel)); });
    WF_COLOR("Color.MeterAverage",      curColor().meterAverage,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterAverage)); });
    WF_COLOR("Color.MeterPeakLevel",    curColor().meterPeakLevel,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterPeakLevel)); });
    WF_COLOR("Color.MeterHighScale",    curColor().meterPeakScale,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterHighScale)); });
    WF_COLOR("Color.MeterScale",        curColor().meterScale,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterScale)); });
    // if we add a separate meterText field, bind that instead
    WF_COLOR("Color.MeterText",         curColor().textColor,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterText)); });
    WF_COLOR("Color.MeterLowerLine",    curColor().meterLowerLine,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterText)); });
    WF_COLOR("Color.MeterLowText",      curColor().meterLowText,
             [this](){ emit colChanged(prefColItems(prefColItem::col_meterText)); });

    // Assorted
    WF_COLOR("Color.ClusterSpots",      curColor().clusterSpots,
             [this](){ emit colChanged(prefColItems(prefColItem::col_clusterSpots)); });
    WF_COLOR("Color.ButtonOff",         curColor().buttonOff,
             [this](){ emit colChanged(prefColItems(prefColItem::col_buttonOff)); });
    WF_COLOR("Color.ButtonOn",          curColor().buttonOn,
             [this](){ emit colChanged(prefColItems(prefColItem::col_buttonOn)); });
}


void SettingsController::refreshCurrentColorPresetOptions(bool loading)
{
    Q_UNUSED(loading)

    // Update ONLY Color.* keys (fast enough, and avoids having to rebuild bindings)
    auto push = [this](const char* key) {
        const auto it = m_bindings.constFind(QString::fromLatin1(key));
        if (it != m_bindings.constEnd())
            updateOptionInMap(QString::fromLatin1(key), it.value().get());
    };

    push("Color.PresetName");
    push("Color.PresetNum");

    push("Color.Window");
    push("Color.WindowText");
    push("Color.Base");
    push("Color.AlternateBase");
    push("Color.MainText");
    push("Color.Button");
    push("Color.ButtonText");
    push("Color.BrightText");
    push("Color.Light");
    push("Color.MidLight");
    push("Color.Dark");
    push("Color.Mid");
    push("Color.Shadow");
    push("Color.Highlight");
    push("Color.HighlightedText");
    push("Color.Link");
    push("Color.LinkVisited");
    push("Color.ToolTipBase");
    push("Color.ToolTipText");
    push("Color.Placeholder");

    push("Color.Grid");
    push("Color.Axis");
    push("Color.Text");
    push("Color.PlotBackground");

    push("Color.SpectrumLine");
    push("Color.SpectrumFill");
    push("Color.UseSpectrumFillGradient");
    push("Color.SpectrumFillTop");
    push("Color.SpectrumFillBot");

    push("Color.UnderlayLine");
    push("Color.UnderlayFill");
    push("Color.UseUnderlayFillGradient");
    push("Color.UnderlayFillTop");
    push("Color.UnderlayFillBot");

    push("Color.TuningLine");
    push("Color.Passband");
    push("Color.PbtIndicator");

    push("Color.WaterfallBack");
    push("Color.WaterfallGrid");
    push("Color.WaterfallAxis");
    push("Color.WaterfallText");

    push("Color.MeterLevel");
    push("Color.MeterAverage");
    push("Color.MeterPeakLevel");
    push("Color.MeterHighScale");
    push("Color.MeterScale");
    push("Color.MeterLowerLine");
    push("Color.MeterLowText");

    push("Color.ClusterSpots");
    push("Color.ButtonOff");
    push("Color.ButtonOn");
}


colorPrefsType& SettingsController::curColor()
{
    int idx = prefs.currentColorPresetNumber;
    if (idx < 0) idx = 0;
    if (idx >= numColorPresetsTotal) idx = 0;
    return colorPreset[idx];
}

const colorPrefsType& SettingsController::curColor() const
{
    int idx = prefs.currentColorPresetNumber;
    if (idx < 0) idx = 0;
    if (idx >= numColorPresetsTotal) idx = 0;
    return colorPreset[idx];
}
