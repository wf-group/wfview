#include "rigcommander.h"
#include <QDebug>

#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

#include <utility>

// Copyright 2017-2024 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

// This is the master class for rigCommander, subclassed by each manufacturer

static QByteArray rigCommandString(const QVariant &value)
{
    const QString text = value.toString();
    QByteArray command;
    command.reserve(text.size());

    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') &&
            i + 3 < text.size() &&
            text.at(i + 1).toLower() == QLatin1Char('x')) {
            bool ok = false;
            const uchar byte = static_cast<uchar>(text.mid(i + 2, 2).toUInt(&ok, 16));
            if (ok) {
                command.append(char(byte));
                i += 3;
                continue;
            }
        }

        command.append(text.at(i).toLatin1());
    }

    return command;
}

static quint8 rigModeRegister(const QVariant &value, manufacturersType_t manufacturer)
{
    const QString text = value.toString();
    if (manufacturer == manufYaesu && !text.isEmpty()) {
        return static_cast<quint8>(text.back().toLatin1());
    }

    return static_cast<quint8>(text.toUInt());
}

rigCommander::rigCommander(QObject* parent) : QObject(parent)
{
    qInfo(logRig()) << "creating instance of rigCommander()";
    queue = cachingQueue::getInstance();

}

rigCommander::rigCommander(quint8 guid[GUIDLEN], QObject* parent) : QObject(parent)
{

    qInfo(logRig()) << "creating instance of rigCommander(guid)";
    memcpy(this->guid, guid, GUIDLEN);
    // Add some commands that is a minimum for rig detection
    queue = cachingQueue::getInstance();
}

rigCommander::~rigCommander()
{
    qInfo(logRig()) << "closing instance of rigCommander()";
}


void rigCommander::serialCommSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp,quint16 tcpPort, quint8 wf)
{
    Q_UNUSED(rigList)
    Q_UNUSED(rigCivAddr)
    Q_UNUSED(rigSerialPort)
    Q_UNUSED(rigBaudRate)
    Q_UNUSED(vsp)
    Q_UNUSED(tcpPort)
    Q_UNUSED(wf)
    qWarning(logRig()) << "commSetup() (serial) not implemented by rig type";
}

void rigCommander::networkCommSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, quint16 tcpPort)
{
    Q_UNUSED(rigList)
    Q_UNUSED(rigCivAddr)
    Q_UNUSED(prefs)
    Q_UNUSED(rxSetup)
    Q_UNUSED(txSetup)
    Q_UNUSED(vsp)
    Q_UNUSED(tcpPort)
    qWarning(logRig()) << "commSetup() (network) not implemented by rig type";
}

void rigCommander::wfShareCommSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, QString host, quint16 port,
                                    QString username, QString password, QString calledNumber,
                                    audioSetup rxSetup, audioSetup txSetup)
{
    Q_UNUSED(rigList)
    Q_UNUSED(rigCivAddr)
    Q_UNUSED(host)
    Q_UNUSED(port)
    Q_UNUSED(username)
    Q_UNUSED(password)
    Q_UNUSED(calledNumber)
    Q_UNUSED(rxSetup)
    Q_UNUSED(txSetup)
    qWarning(logRig()) << "commSetup() (wfshare) not implemented by rig type";
}

void rigCommander::closeComm()
{
    qWarning(logRig()) << "closeComm() not implemented by rig type";
}

void rigCommander::process()
{
    qWarning(logRig()) << "process() not implemented by rig type";
}

void rigCommander::setRigID(quint16 rigID)
{
    Q_UNUSED(rigID)

    qWarning(logRig()) << "setRigID() not implemented by rig type";
}

void rigCommander::receiveBaudRate(quint32 baudrate)
{
    Q_UNUSED(baudrate)

    qWarning(logRig()) << "receiveBaudRate() not implemented by rig type";
}

void rigCommander::setPTTType(pttType_t ptt)
{
    Q_UNUSED(ptt)

    qWarning(logRig()) << "setPTTType() not implemented by rig type";
}

void rigCommander::powerOn()
{
    qWarning(logRig()) << "powerOn() not implemented by rig type";
}

void rigCommander::powerOff()
{
    qWarning(logRig()) << "powerOff() not implemented by rig type";
}

void rigCommander::setCIVAddr(quint16 civAddr)
{
    Q_UNUSED(civAddr)

    qWarning(logRig()) << "setCIVAddr() not implemented by rig type";
}

void rigCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    Q_UNUSED(func)
    Q_UNUSED(value)
    Q_UNUSED(receiver)

    qWarning(logRig()) << "receiveCommand() not implemented by rig type";
}

/*
 * The following slots/functions are only implemented
 * in the parent rigCommander()
 *
*/

void rigCommander::handlePortError(errorType err)
{
    qInfo(logRig()) << "Error using port " << err.device << " message: " << err.message;
    emit havePortError(err);
}

void rigCommander::handleStatusUpdate(const networkStatus status)
{
    emit haveStatusUpdate(status);
}

void rigCommander::handleNetworkAudioLevels(networkAudioLevels l)
{
    emit haveNetworkAudioLevels(l);
}

void rigCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
}

void rigCommander::receiveAudioData(const audioPacket& data)
{
    emit haveAudioData(data);
}


void rigCommander::changeLatency(const quint16 value)
{
    emit haveChangeLatency(value);
}

void rigCommander::radioSelection(QList<radio_cap_packet> radios)
{
    emit requestRadioSelection(radios);
}

void rigCommander::radioUsage(quint8 radio, bool admin, quint8 busy, QString user, QString ip) {
    emit setRadioUsage(radio, admin, busy, user, ip);
}

void rigCommander::setCurrentRadio(quint8 radio) {
    emit selectedRadio(radio);
}

void rigCommander::getDebug()
{
    // generic debug function for development.
    emit getMoreDebug();
}

void rigCommander::dataFromServer(QByteArray data)
{
    emit dataForComm(data);
}

bool rigCommander::usingLAN()
{
    return usingNativeLAN;
}

quint8* rigCommander::getGUID() {
    return guid;
}


/* This function will load all of the rigCaps and inform the queue that it is loaded */
void rigCommander::determineRigCaps()
{
    // First clear all of the current settings
    rigCaps.preamps.clear();
    rigCaps.attenuators.clear();
    rigCaps.inputs.clear();
    rigCaps.scopeCenterSpans.clear();
    rigCaps.bands.clear();
    rigCaps.modes.clear();
    rigCaps.commands.clear();
    rigCaps.commandsReverse.clear();
    rigCaps.antennas.clear();
    rigCaps.filters.clear();
    rigCaps.steps.clear();
    rigCaps.memParser.clear();
    rigCaps.satParser.clear();
    rigCaps.periodic.clear();
    rigCaps.roofing.clear();
    rigCaps.scopeModes.clear();
    rigCaps.widths.clear();

    for (int i = meterNone; i < meterUnknown; i++)
    {
        rigCaps.meters[i].clear();
        rigCaps.meterLines[i] = 0.0;
    }

    // modelID should already be set!
    while (!rigList.contains(rigCaps.modelID))
    {
        if (!rigCaps.modelID) {
            for (const auto &r: rigList)
            {
                qInfo(logRig()) << "Rig:" << r.model << "ID:" << r.civ;
            }

            qWarning(logRig()) << "No default rig definition found, cannot continue (sorry!)";
            return;
        }
        // Unknown rig, load default
        qInfo(logRig()) << QString("No rig definition found for CI-V address: %0, using defaults (some functions may not be available)").arg(rigCaps.modelID,4,10);
        rigCaps.modelID=0;
    }
    rigCaps.filename = rigList.find(rigCaps.modelID).value().path;
    QSettings* settings = new QSettings(rigCaps.filename, QSettings::Format::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    settings->setIniCodec("UTF-8");
#endif
    if (!settings->childGroups().contains("Rig"))
    {
        qWarning(logRig()) << rigCaps.filename << "Cannot be loaded!";
        return;
    }
    settings->beginGroup("Rig");
    // Populate rigcaps

    rigCaps.modelName = settings->value("Model", "").toString();
    rigCaps.rigctlModel = settings->value("RigCtlDModel", 0).toInt();
    rigCaps.manufacturer = static_cast<manufacturersType_t>(settings->value("Manufacturer", manufIcom).toInt());

    qInfo(logRig()) << QString("Loading Rig: %0 from %1").arg(rigCaps.modelName,rigCaps.filename);

    rigCaps.numReceiver = settings->value("NumberOfReceivers",1).toUInt();
    rigCaps.numVFO = settings->value("NumberOfVFOs",1).toUInt();
    rigCaps.spectSeqMax = settings->value("SpectrumSeqMax",0).toUInt();
    rigCaps.spectAmpMax = settings->value("SpectrumAmpMax",0).toUInt();
    rigCaps.spectLenMax = settings->value("SpectrumLenMax",0).toUInt();

    rigCaps.hasSpectrum = settings->value("HasSpectrum",false).toBool();
    rigCaps.hasLan = settings->value("HasLAN",false).toBool();
    rigCaps.hasEthernet = settings->value("HasEthernet",false).toBool();
    rigCaps.hasWiFi = settings->value("HasWiFi",false).toBool();
    rigCaps.hasQuickSplitCommand = settings->value("HasQuickSplit",false).toBool();
    rigCaps.hasDD = settings->value("HasDD",false).toBool();
    rigCaps.hasDV = settings->value("HasDV",false).toBool();
    rigCaps.hasTransmit = settings->value("HasTransmit",false).toBool();
    rigCaps.hasFDcomms = settings->value("HasFDComms",false).toBool();
    rigCaps.hasCommand29 = settings->value("HasCommand29",false).toBool();

    rigCaps.memGroups = settings->value("MemGroups",0).toUInt();
    rigCaps.memories = settings->value("Memories",0).toUInt();
    rigCaps.memStart = settings->value("MemStart",1).toUInt();
    rigCaps.memFormat = settings->value("MemFormat","").toString();
    rigCaps.satMemories = settings->value("SatMemories",0).toUInt();
    rigCaps.satFormat = settings->value("SatFormat","").toString();

    // If rig doesn't have FD comms, tell the commhandler early.
    emit setHalfDuplex(!rigCaps.hasFDcomms);

    // Temporary QHash to hold the function string lookup // I would still like to find a better way of doing this!
    QHash<QString, funcs> funcsLookup;
    for (int i=0;i<funcLastFunc;i++)
    {
        if (!funcString[i].startsWith("+")) {
            funcsLookup.insert(funcString[i].toUpper(), funcs(i));
        }
    }

    int numCommands = settings->beginReadArray("Commands");
    if (numCommands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numCommands; c++)
        {
            settings->setArrayIndex(c);

            if (funcsLookup.contains(settings->value("Type", "****").toString().toUpper()))
            {
                funcs func = funcsLookup.find(settings->value("Type", "").toString().toUpper()).value();
                const QByteArray commandString = rigCommandString(settings->value("String", ""));
                rigCaps.commands.insert(func, funcType(func, funcString[int(func)],
                                                       commandString,
                                                       settings->value("Min", 0).toInt(NULL), settings->value("Max", 0).toInt(NULL),
                                                       settings->value("PadRight",false).toBool(),
                                                       settings->value("Command29",false).toBool(),
                                                       settings->value("GetCommand",true).toBool(),
                                                       settings->value("SetCommand",true).toBool(),
                                                       settings->value("Bytes",0).toInt(),
                                                       settings->value("Admin",false).toBool())
                                        );

                rigCaps.commandsReverse.insert(commandString,func);
            } else {
                qWarning(logRig()) << "**** Function" << settings->value("Type", "").toString() << "Not Found, rig file may be out of date?";
            }
        }
        settings->endArray();
    }

    int numPeriodic = settings->beginReadArray("Periodic");
    if (numPeriodic == 0){
        qWarning(logRig())<< "No periodic commands defined, please check rigcaps file";
        settings->endArray();
    } else {
        for (int c=0; c < numPeriodic; c++)
        {
            settings->setArrayIndex(c);

            if(funcsLookup.contains(settings->value("Command", "****").toString().toUpper())) {
                funcs func = funcsLookup.find(settings->value("Command", "").toString().toUpper()).value();
                if (!rigCaps.commands.contains(func)) {
                    qWarning(logRig()) << "Cannot find periodic command" << settings->value("Command", "").toString() << "in rigcaps, ignoring";
                } else {
                    QStringList modes;
                    const QVariant modesVariant = settings->value("Modes");
                    QStringList modeValues = modesVariant.toStringList();
                    if (modeValues.isEmpty() && modesVariant.isValid())
                        modeValues = QStringList{modesVariant.toString()};

                    for (const QString &modeValue : std::as_const(modeValues)) {
                        for (const QString &mode : modeValue.split(',', Qt::SkipEmptyParts))
                            modes.append(mode.trimmed().toUpper());
                    }

                    rigCaps.periodic.append(periodicType(func,
                                                         settings->value("Priority","").toString(),priorityMap[settings->value("Priority","").toString()],
                                                         settings->value("VFO",-1).toInt(), modes));
                    if (!modes.isEmpty()) {
                        qInfo(logRig()) << "Loaded mode-specific periodic command" << funcString[func]
                                        << "modes" << modes << "receiver" << settings->value("VFO",-1).toInt();
                    }
                }
            }
        }
        // Sort into a meaningful order
        std::sort(rigCaps.periodic.begin(), rigCaps.periodic.end(),
                  [](const periodicType &a, const periodicType &b) {
                      return a.func < b.func;
                  });
        settings->endArray();
    }

    int numModes = settings->beginReadArray("Modes");
    if (numModes == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numModes; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.modes.push_back(modeInfo(rigMode_t(settings->value("Num", 0).toUInt()),
                                             rigModeRegister(settings->value("Reg", 0), rigCaps.manufacturer), settings->value("Name", "").toString(), settings->value("Min", 0).toInt(), settings->value("Max", 0).toInt()));
        }
        // Sort into a meaningful order
        std::sort(rigCaps.modes.begin(), rigCaps.modes.end(),
                  [](const modeInfo &a, const modeInfo &b) {
                      return a.reg < b.reg;
                  });
        settings->endArray();
    }

    int numCTCSS = settings->beginReadArray("CTCSS");
    if (numCTCSS == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numCTCSS; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.ctcss.push_back(toneInfo(settings->value("Reg", 0).toUInt(), QString::number(settings->value("Tone", 0.0).toFloat(),'f',1)));
        }
        // Sort into a meaningful order
        std::sort(rigCaps.ctcss.begin(), rigCaps.ctcss.end(),
                  [](const toneInfo &a, const toneInfo &b) {
                      return a.tone < b.tone;
                  });

        settings->endArray();
    }

    int numDTCS = settings->beginReadArray("DTCS");
    if (numDTCS == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numDTCS; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.dtcs.push_back(toneInfo(settings->value("Reg", 0).toInt(), QString::number(settings->value("Reg", 0).toInt()).rightJustified(4,'0')));
        }
        // Sort into a meaningful order
        std::sort(rigCaps.dtcs.begin(), rigCaps.dtcs.end(),
                  [](const toneInfo &a, const toneInfo &b) {
                      return a.tone < b.tone;
                  });
        settings->endArray();
    }

    int numSpans = settings->beginReadArray("Spans");
    if (numSpans == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSpans; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.scopeCenterSpans.push_back(centerSpanData(uchar(settings->value("Num", 0).toUInt()),
                                                              settings->value("Name", "").toString(), settings->value("Freq", 0).toUInt()));
        }
        std::sort(rigCaps.scopeCenterSpans.begin(), rigCaps.scopeCenterSpans.end(),
                  [](const centerSpanData &a, const centerSpanData &b) {
                      return a.freq < b.freq;
                  });

        settings->endArray();
    }

    int numInputs = settings->beginReadArray("Inputs");
    if (numInputs == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numInputs; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.inputs.append(rigInput(inputTypes(settings->value("Num", 0).toUInt()),
                                           settings->value("Reg", 0).toString().toInt(),settings->value("Name", "").toString()));
        }
        std::sort(rigCaps.inputs.begin(), rigCaps.inputs.end(),
                  [](const rigInput &a, const rigInput &b) {
                      return a.type < b.type;
                  });

        settings->endArray();
    }

    int numSteps = settings->beginReadArray("Tuning Steps");
    if (numSteps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numSteps; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.steps.push_back(stepType(settings->value("Num", 0).toString().toUInt(),
                                             settings->value("Name", "").toString(),settings->value("Hz", 0ULL).toULongLong()));
        }
        std::sort(rigCaps.steps.begin(), rigCaps.steps.end(),
                  [](const stepType &a, const stepType &b) {
                      return a.hz < b.hz;
                  });

        settings->endArray();
    }

    int numPreamps = settings->beginReadArray("Preamps");
    if (numPreamps == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numPreamps; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.preamps.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        std::sort(rigCaps.preamps.begin(), rigCaps.preamps.end(),
                  [](const genericType &a, const genericType &b) {
                      return a.num < b.num;
                  });

        settings->endArray();
    }

    int numAntennas = settings->beginReadArray("Antennas");
    if (numAntennas == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAntennas; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.antennas.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        std::sort(rigCaps.antennas.begin(), rigCaps.antennas.end(),
                  [](const genericType &a, const genericType &b) {
                      return a.num < b.num;
                  });

        settings->endArray();
    }

    int numAttenuators = settings->beginReadArray("Attenuators");
    if (numAttenuators == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numAttenuators; c++)
        {
            settings->setArrayIndex(c);
            if (settings->value("Num", -1).toString().toInt() == -1) {
                rigCaps.attenuators.push_back(genericType(settings->value("dB", 0).toString().toUInt(),QString("%0 dB").arg(settings->value("dB", 0).toString().toUInt())));
            } else {
                rigCaps.attenuators.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
            }
        }
        std::sort(rigCaps.attenuators.begin(), rigCaps.attenuators.end(),
                  [](const genericType &a, const genericType &b) {
                      return a.num < b.num;
                  });

        settings->endArray();
    }

    int numFilters = settings->beginReadArray("Filters");
    if (numFilters == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numFilters; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.filters.push_back(filterType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", "").toString(), settings->value("Modes", 0).toUInt()));
        }
        std::sort(rigCaps.filters.begin(), rigCaps.filters.end(),
                  [](const filterType &a, const filterType &b) {
                      return a.num < b.num;
                  });

        settings->endArray();
    }

    int numBands = settings->beginReadArray("Bands");
    if (numBands == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numBands; c++)
        {
            settings->setArrayIndex(c);
            QString region = settings->value("Region","").toString();
            availableBands band =  availableBands(settings->value("Num", 0).toInt());
            quint64 start = settings->value("Start", 0ULL).toULongLong();
            quint64 end = settings->value("End", 0ULL).toULongLong();
            uchar bsr = static_cast<uchar>(settings->value("BSR", 0).toInt());
            double range = settings->value("Range", 0.0).toDouble();
            int memGroup = settings->value("MemoryGroup", -1).toInt();
            char bytes = settings->value("Bytes", 5).toInt();
            bool ants = settings->value("Antennas",true).toBool();
            QColor color(settings->value("Color", "#00000000").toString()); // Default color should be none!
            QString name(settings->value("Name", "None").toString());
            float power = settings->value("Power", 0.0f).toFloat();
            quint64 offset = settings->value("Offset", 0).toLongLong();
            rigCaps.bands.push_back(bandType(region,band,bsr,start,end,range,memGroup,bytes,ants,power,color,name,offset));
            rigCaps.bsr[band] = bsr;
            qDebug(logRig()) << "Adding Band " << band << "Start" << start << "End" << end << "BSR" << QString::number(bsr,16);
        }
        std::sort(rigCaps.bands.begin(), rigCaps.bands.end(),
                   [](const bandType &a, const bandType &b) {
                       return a.band < b.band;
                   });
        settings->endArray();


    }

    int numMeters = settings->beginReadArray("Meters");
    if (numMeters == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numMeters; c++)
        {
            settings->setArrayIndex(c);
            QString meterName = settings->value("Meter", "None").toString();
            if (meterName != "None" && meterName != "")
                for (int i = meterNone; i < meterUnknown; i++)
                {
                    if (meterName == meterString[i] && i != meterNone)
                    {
                        if (settings->value("RedLine", false).toBool())
                        {
                            rigCaps.meterLines[i] = settings->value("ActualVal", 0.0).toDouble();
                        }
                        rigCaps.meters[i].insert(settings->value("RigVal", 0).toInt(),settings->value("ActualVal", 0.0).toDouble());
                        break;
                    }
                }
        }

        settings->endArray();
    }

    int numRoofing = settings->beginReadArray("Roofing");
    if (numRoofing == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numRoofing; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.roofing.push_back(genericType(settings->value("Num", 0).toString().toUInt(), settings->value("Name", 0).toString()));
        }
        std::sort(rigCaps.roofing.begin(), rigCaps.roofing.end(),
                  [](const genericType &a, const genericType &b) {
                      return a.num < b.num;
                  });
        settings->endArray();
    }


    int numScopeModes = settings->beginReadArray("ScopeModes");
    if (numScopeModes == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numScopeModes; c++)
        {
            settings->setArrayIndex(c);
            const QString num = settings->value("Num", 0).toString();
            quint8 value = static_cast<quint8>(num.toUInt());
            if (rigCaps.manufacturer == manufYaesu && !num.isEmpty()) {
                value = static_cast<quint8>(num.back().toLatin1());
            }
            rigCaps.scopeModes.push_back(genericType(value, settings->value("Name", 0).toString()));
        }
        std::sort(rigCaps.scopeModes.begin(), rigCaps.scopeModes.end(),
                  [](const genericType &a, const genericType &b) {
                      return a.num < b.num;
                  });
        settings->endArray();
    }

    int numWidths = settings->beginReadArray("Widths");
    if (numWidths == 0) {
        settings->endArray();
    }
    else {
        for (int c = 0; c < numWidths; c++)
        {
            settings->setArrayIndex(c);
            rigCaps.widths.push_back(widthsType(settings->value("Bands", 0).toString().toUShort(nullptr,16),
                                                static_cast<uchar>(settings->value("Num", 0).toString().toUShort()),
                                                settings->value("Hz", 0).toString().toUShort()));
        }
        std::sort(rigCaps.widths.begin(), rigCaps.widths.end(),
                  [](const widthsType &a, const widthsType &b) {
                      if (a.bands == b.bands)
                          return a.num < b.num;
                      return a.bands < b.bands;
                  });
        settings->endArray();
    }

    settings->endGroup();

    delete settings;

    // Setup memory formats.
    static QRegularExpression memFmtEx("%(?<flags>[-+#0])?(?<pos>\\d+|\\*)?(?:\\.(?<width>\\d+|\\*))?(?<spec>[abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ+])");
    QRegularExpressionMatchIterator i = memFmtEx.globalMatch(rigCaps.memFormat);
    while (i.hasNext()) {
        QRegularExpressionMatch qmatch = i.next();

#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width")) {
#endif
            rigCaps.memParser.append(memParserFormat(qmatch.captured("spec").at(0).toLatin1(),qmatch.captured("pos").toInt(),qmatch.captured("width").toInt()));
        }
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
        }
#endif

    QRegularExpressionMatchIterator i2 = memFmtEx.globalMatch(rigCaps.satFormat);

    while (i2.hasNext()) {
        QRegularExpressionMatch qmatch = i2.next();
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
        if (qmatch.hasCaptured("spec") && qmatch.hasCaptured("pos") && qmatch.hasCaptured("width")) {
#endif
            rigCaps.satParser.append(memParserFormat(qmatch.captured("spec").at(0).toLatin1(),qmatch.captured("pos").toInt(),qmatch.captured("width").toInt()));
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
            }
#endif
    }

    // Copy received guid so we can recognise this radio.
    memcpy(rigCaps.guid, this->guid, GUIDLEN);

    haveRigCaps = true;
    queue->setRigCaps(&rigCaps);

    // Also signal that a radio is connected to the radio status window
    if (!usingNativeLAN) {
        QList<radio_cap_packet>radios;
        radio_cap_packet r;
        r.civ = char(rigCaps.modelID & 0xff);
        r.baudrate = qToBigEndian(rigCaps.baudRate);
#ifdef Q_OS_WINDOWS
        strncpy_s(r.name,rigCaps.modelName.toLocal8Bit(),sizeof(r.name)-1);
#else
        strncpy(r.name,rigCaps.modelName.toLocal8Bit(),sizeof(r.name)-1);
#endif
        radios.append(r);
        emit requestRadioSelection(radios);
        emit setRadioUsage(0, true, true, QString("<Local>"), QString("127.0.0.1"));
    }

    qDebug(logRig()) << "---Rig FOUND" << rigCaps.modelName;
    emit discoveredRigID(rigCaps);
    emit haveRigID(rigCaps);

    /*
    if(lookingForRig)
    {
        lookingForRig = false;
        foundRig = true;

        qDebug(logRig()) << "---Rig FOUND from broadcast query:";
        this->civAddr = incomingCIVAddr & 0xff; // Override and use immediately.
        payloadPrefix = QByteArray("\xFE\xFE");
        payloadPrefix.append((char)civAddr);
        payloadPrefix.append((char)compCivAddr);
        // if there is a compile-time error, remove the following line, the "hex" part is the issue:
        qInfo(logRig()) << "Using incomingCIVAddr: (int): " << this->civAddr << " hex: " << QString("0x%1").arg(this->civAddr,0,16);
        emit discoveredRigID(rigCaps);
    } else {
        if(!foundRig)
        {
            emit discoveredRigID(rigCaps);
            foundRig = true;
        }
        emit haveRigID(rigCaps);
    }
    */
}

double rigCommander::getMeterCal(meter_t meter,int value)
{
    double ret = static_cast<double>(value);

    if (rigCaps.meters[meter].size()) {
        auto it = rigCaps.meters[meter].lowerBound(value);
        if (value <= it.key())
        {
            if (it == rigCaps.meters[meter].begin())
            {
                // Value matches the cal exactly
                ret = it.value();
            }
            else if (it == rigCaps.meters[meter].end())
            {
                // Val is larger than the max cal value
                ret = (--it).value();
            }
            else
            {
                int key = it.key();
                double val = it.value();
                double prevVal = (--it).value();
                int prevKey = it.key();
                double interp = ((key - value) * (val - prevVal)) / (key - prevKey);
                ret = val - interp;
            }
        }
    }

    // qDebug() << meterString[meter] << "val:" << value << "=" << ret;

    return ret;
}

void rigCommander::printHex(const QByteArray &pdata)
{
    printHex(pdata, false, true);
}

void rigCommander::printHex(const QByteArray &pdata, bool printVert, bool printHoriz)
{
    qDebug(logRig()) << "---- Begin hex dump -----:";
    QString sdata("DATA:  ");
    QString index("INDEX: ");
    QStringList strings;

    for(int i=0; i < pdata.length(); i++)
    {
        strings << QString("[%1]: %2").arg(i,8,10,QChar('0')).arg((quint8)pdata[i], 2, 16, QChar('0'));
        sdata.append(QString("%1 ").arg((quint8)pdata[i], 2, 16, QChar('0')) );
        index.append(QString("%1 ").arg(i, 2, 10, QChar('0')));
    }

    if(printVert)
    {
        for(int i=0; i < strings.length(); i++)
        {
            //sdata = QString(strings.at(i));
            qDebug(logRig()) << strings.at(i);
        }
    }

    if(printHoriz)
    {
        qDebug(logRig()) << index;
        qDebug(logRig()) << sdata;
    }
    qDebug(logRig()) << "----- End hex dump -----";
}
