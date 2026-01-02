#include "SettingsController.h"
#include "logcategories.h"

SettingsController::SettingsController(QString file, QObject *p) : QObject(p)
{
    qInfo(logSystem) << "Creating SettingsConntroller() to load settings";

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
    } else {
        settings = std::make_unique<QSettings>();
    }
    setDefPrefs();
    load();    
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
    prefs.manufacturer = (manufacturersType_t)settings->value("Manufacturer", defPrefs.manufacturer).value<manufacturersType_t>();
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

    settings->endGroup();

    // Now we have audioSystem, get the audio devices
    // Will need to work out a way to get the font size?, this will also need reloading when audioSystem changes.
    audioDev = std::make_unique<audioDevices>(prefs.audioSystem, QFontMetrics(QFont()));
    audioDev->enumerate();

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
    }
    settings->endArray();
    settings->endGroup();

#if defined (USB_CONTROLLER)
    settings->beginGroup("USB");
    prefs.enableUSBControllers = settings->value("EnableUSBControllers", defPrefs.enableUSBControllers).toBool();

    /*
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
                usbDevices.insert(tempPrefs.path,tempPrefs);
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
    */
    settings->endGroup();

    if (prefs.enableUSBControllers) {
        // Setup USB Controller
        //setupUsbControllerDevice();
        //emit initUsbController(&usbMutex,&usbDevices,&usbButtons,&usbKnobs);
    }


#endif
    /*
    setupui->acceptPreferencesPtr(&prefs);
    setupui->acceptColorPresetPtr(colorPreset);
    setupui->acceptUdpPreferencesPtr(&udpPrefs);
    */

    prefs.settingsChanged = false;

}

void SettingsController::save() const
{
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
    defPrefs.mainWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.mainPlotFloor = 0;
    defPrefs.mainPlotCeiling = 160;
    defPrefs.subWflength = 160;
    defPrefs.subWfTheme = static_cast<int>(QCPColorGradient::gpJet);
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
    defPrefs.rxSetup.isinput = false;
    defPrefs.txSetup.isinput = true;
    defPrefs.rxSetup.sampleRate = 48000;
    defPrefs.rxSetup.codec = 4;
    defPrefs.txSetup.codec = 4;
    defPrefs.rxSetup.resampleQuality = 4;

    // Cluster
    defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.clusterUdpPort = 12060;

    // CW
    defPrefs.cwCutNumbers=false;
    defPrefs.cwSendImmediate=false;
    defPrefs.cwSidetoneEnabled=true;
    defPrefs.cwSidetoneLevel=100;

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
