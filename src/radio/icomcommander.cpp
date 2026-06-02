#include "icomcommander.h"
#include <QDebug>

#include "audiohandler.h"
#include "audiopacketcodec.h"
#include "icomscopeframeassembler.h"
#include "iaxradiotransport.h"
#include "rigidentities.h"
#include "logcategories.h"
#include "printhex.h"

// Copyright 2017-2024 Elliott H. Liggett W6EL and Phil E. Taylor M0VSE

// This file parses data from the radio and also forms commands to the radio.
// The radio physical interface is handled by the commHandler() instance "comm"

//
// See here for a wonderful CI-V overview:
// http://www.plicht.de/ekki/civ/civ-p0a.html
//
// The IC-7300 "full" manual also contains a command reference.

// How to make spectrum display stop using rigctl:
//  echo "w \0xFE\0xFE\0x94\0xE0\0x27\0x11\0x00\0xFD" | rigctl -m 3073 -r /dev/ttyUSB0 -s 115200 -vvvvv

// Note: When sending \x00, must use QByteArray.setRawData()

namespace {

void applyIaxStatsToNetworkStatus(const IaxStats &stats, networkStatus *status)
{
    if (status == nullptr)
        return;

    status->networkLatency = stats.lagMs;
    status->packetsSent = stats.fullFramesTx + stats.miniFramesTx + stats.fullFramesRx + stats.miniFramesRx;
    status->packetsLost = stats.retries + stats.oSeqnoGaps + stats.invalidIaxFrames;
    status->message = QStringLiteral("IAX rx: %1 ms / lag: %2 ms / retry: %3 / OSeq gap: %4 / invalid: %5")
                          .arg(status->rxCurrentLatency, 3)
                          .arg(stats.lagMs, 3)
                          .arg(stats.retries, 3)
                          .arg(stats.oSeqnoGaps, 3)
                          .arg(stats.invalidIaxFrames, 3);
}

}

icomCommander::icomCommander(rigCommander* parent) : rigCommander(parent)
{

    qInfo(logRig()) << "creating instance of icomCommander()";
}

icomCommander::icomCommander(quint8 guid[GUIDLEN], rigCommander* parent) : rigCommander(parent)
{
    qInfo(logRig()) << "creating instance of icomCommander() with GUID";
    memcpy(this->guid, guid, GUIDLEN);
    // Add some commands that is a minimum for rig detection
}

icomCommander::~icomCommander()
{
    qInfo(logRig()) << "closing instance of icomCommander()";

    emit requestRadioSelection(QList<radio_cap_packet>()); // Remove radio list.

    queue->setRigCaps(nullptr); // Remove access to rigCaps

    qDebug(logRig()) << "Closing rig comms";
    if (comm != nullptr) {
        delete comm;
        comm = nullptr;
    }

    if (udpHandlerThread != nullptr) {
        if (udp != nullptr) {
            QMetaObject::invokeMethod(udp, &icomUdpHandler::shutdown,
                                      !udpHandlerThread->isRunning() || udp->thread() == QThread::currentThread()
                                          ? Qt::DirectConnection
                                          : Qt::BlockingQueuedConnection);
        }
        udpHandlerThread->quit();
        udpHandlerThread->wait();
        udpHandlerThread = nullptr;
    }
    udp = nullptr;

    if (vspPort != nullptr) {
        delete vspPort;
        vspPort = nullptr;
    }

    closeWfShare();
}


void icomCommander::serialCommSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, QString rigSerialPort, quint32 rigBaudRate, QString vsp, bool vspUseQueue, quint16 tcpPort, quint8 wf)
{
    // construct

    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.
    this->rigList = rigList;
    civAddr = rigCivAddr; // address of the radio.
    usingNativeLAN = false;

    this->rigSerialPort = rigSerialPort;
    this->rigBaudRate = rigBaudRate;
    vspQueueEnabled = vspUseQueue;
    rigCaps.baudRate = rigBaudRate;


    comm = new commHandler(rigSerialPort, rigBaudRate,wf,this);
    // data from the comm port to the program:
    connect(comm, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));
    // data from the program to the comm port:
    connect(this, SIGNAL(dataForComm(QByteArray)), comm, SLOT(receiveDataFromUserToRig(QByteArray)));
    // Whether radio is half duplex only
    connect(this, SIGNAL(setHalfDuplex(bool)), comm, SLOT(setHalfDuplex(bool)));
    connect(comm, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
    connect(this, SIGNAL(getMoreDebug()), comm, SLOT(debugThis()));


    if (vsp.toLower() != "none") {
        qInfo(logRig()) << "Attempting to connect to VSP:" << vsp;
        vspPort = new vspHandler(vsp,this);
        // data from the VSP to the rig:
        connect(vspPort, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(dataFromVspClient(QByteArray)));
        // data from the rig to the VSP:
        connect(comm, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(dataFromRigToExternalClient(QByteArray)));
        connect(this, SIGNAL(haveDataForVsp(QByteArray)), vspPort, SLOT(receiveDataFromRigToVsp(QByteArray)));
        connect(vspPort, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
        connect(this, SIGNAL(getMoreDebug()), vspPort, SLOT(debugThis()));
    }

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), this, SLOT(dataFromExternalClient(QByteArray)));
        connect(comm, SIGNAL(haveDataFromPort(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }

    commonSetup();
}

void icomCommander::networkCommSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, udpPreferences prefs, audioSetup rxSetup, audioSetup txSetup, QString vsp, bool vspUseQueue, quint16 tcpPort)
{
    // construct
    // TODO: Bring this parameter and the comm port from the UI.
    // Keep in hex in the UI as is done with other CIV apps.

    this->rigList = rigList;
    this->prefs = prefs;
    civAddr = rigCivAddr; // address of the radio
    usingNativeLAN = true;
    vspQueueEnabled = vspUseQueue;

    if (udp != nullptr) {
        closeComm();
    }

    udp = new icomUdpHandler(prefs,rxSetup,txSetup);

    udpHandlerThread = new QThread(this);
    udpHandlerThread->setObjectName("icomUdpHandler()");

    udp->moveToThread(udpHandlerThread);

    connect(this, SIGNAL(initUdpHandler()), udp, SLOT(init()));
    connect(udpHandlerThread, SIGNAL(finished()), udp, SLOT(deleteLater()));
    udpHandlerThread->start();

    emit initUdpHandler();

    // Data from UDP to the program
    connect(udp, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(handleNewData(QByteArray)));
    // data from the program to the rig:
    connect(this, SIGNAL(dataForComm(QByteArray)), udp, SLOT(receiveDataFromUserToRig(QByteArray)));
    // Audio from UDP
    connect(udp, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));

    connect(this, SIGNAL(haveChangeLatency(quint16)), udp, SLOT(changeLatency(quint16)));
    connect(this, SIGNAL(haveSetVolume(quint8)), udp, SLOT(setVolume(quint8)));
    connect(udp, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));
    // Connect for errors/alerts
    connect(udp, SIGNAL(haveNetworkError(errorType)), this, SLOT(handlePortError(errorType)));
    connect(udp, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(handleStatusUpdate(networkStatus)));
    connect(udp, SIGNAL(haveNetworkAudioLevels(networkAudioLevels)), this, SLOT(handleNetworkAudioLevels(networkAudioLevels)));
    // Other assorted UDP connections
    connect(udp, SIGNAL(requestRadioSelection(QList<radio_cap_packet>)), this, SLOT(radioSelection(QList<radio_cap_packet>)));
    connect(udp, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), this, SLOT(radioUsage(quint8, bool, quint8, QString, QString)));
    connect(this, SIGNAL(selectedRadio(quint8)), udp, SLOT(setCurrentRadio(quint8)));

    if (vsp != "None") {
        vspPort = new vspHandler(vsp,this);
        // data from the VSP to the rig:
        connect(vspPort, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(dataFromVspClient(QByteArray)));
        // data from the rig to the VSP:
        connect(udp, SIGNAL(haveDataFromPort(QByteArray)), this, SLOT(dataFromRigToExternalClient(QByteArray)));
        connect(this, SIGNAL(haveDataForVsp(QByteArray)), vspPort, SLOT(receiveDataFromRigToVsp(QByteArray)));

        connect(vspPort, SIGNAL(havePortError(errorType)), this, SLOT(handlePortError(errorType)));
        connect(this, SIGNAL(getMoreDebug()), vspPort, SLOT(debugThis()));
    }

    if (tcpPort > 0) {
        tcp = new tcpServer(this);
        tcp->startServer(tcpPort);
        // data from the tcp port to the rig:
        connect(tcp, SIGNAL(receiveData(QByteArray)), this, SLOT(dataFromExternalClient(QByteArray)));
        // data from the rig to the tcp port:
        connect(udp, SIGNAL(haveDataFromPort(QByteArray)), tcp, SLOT(sendData(QByteArray)));
    }

    commonSetup();

}

void icomCommander::wfShareCommSetup(QHash<quint16,rigInfo> rigList, quint16 rigCivAddr, QString host,
                                     quint16 port, QString username, QString password, QString calledNumber,
                                     audioSetup rxSetup, audioSetup txSetup)
{
    this->rigList = rigList;
    civAddr = rigCivAddr;
    usingNativeLAN = false;
    wfShareRxSetup = rxSetup;
    wfShareRxSetup.codec = 0x40;
    wfShareRxSetup.sampleRate = 16000;
    wfShareRxSetup.isinput = false;
    wfShareTxSetup = txSetup;
    wfShareTxSetup.codec = 0x40;
    wfShareTxSetup.sampleRate = 16000;
    wfShareTxSetup.isinput = true;
    wfShareTxSetup.latency = txSetup.latency == 0 ? quint16(150) : txSetup.latency;
    memcpy(wfShareTxSetup.guid, guid, GUIDLEN);
    wfShareStats = IaxStats();
    wfShareStatus = networkStatus();

    if (wfShareTransport != nullptr) {
        wfShareTransport->disconnectTransport();
        delete wfShareTransport;
        wfShareTransport = nullptr;
    }

    auto *iaxTransport = new IaxRadioTransport(this);
    wfShareTransport = iaxTransport;
    connect(iaxTransport, &IaxRadioTransport::iaxStatsChanged, this, [this](IaxStats stats) {
        wfShareStats = stats;
        applyIaxStatsToNetworkStatus(wfShareStats, &wfShareStatus);
        emit haveStatusUpdate(wfShareStatus);
    });
    connect(wfShareTransport, &RadioTransport::streamReceived, this, [this](RadioTransportChannel channel, QByteArray data) {
        if (channel == RadioTransportChannel::Civ || channel == RadioTransportChannel::Scope) {
            handleNewData(data);
        } else if (channel == RadioTransportChannel::AudioRx) {
            if (!haveRigCaps) {
                return;
            }

            audioPacket packet;
            if (audioPacketCodec::decode(data, &packet)) {
                wfShareAudioRxFrames++;
                if (wfShareAudioRxFrames <= 10 || wfShareAudioRxFrames % 250 == 0) {
                    qDebug(logRig()).noquote()
                        << QStringLiteral("wfshare received RX audio frame %1 bytes %2")
                               .arg(wfShareAudioRxFrames)
                               .arg(packet.data.size());
                }
                if (wfShareRxAudio == nullptr) {
                    if (wfShareRxSetup.type == qtAudio) {
                        wfShareRxAudio = new audioHandlerQtOutput();
                    } else if (wfShareRxSetup.type == portAudio) {
                        wfShareRxAudio = new audioHandlerPaOutput();
                    } else if (wfShareRxSetup.type == rtAudio) {
                        wfShareRxAudio = new audioHandlerRtOutput();
                    } else {
                        qCritical(logAudio()) << "Unsupported wfshare receive audio handler selected" << wfShareRxSetup.type;
                        return;
                    }

                    wfShareRxAudioThread = new QThread(this);
                    wfShareRxAudioThread->setObjectName(QStringLiteral("wfshareRxAudio()"));
                    wfShareRxAudio->moveToThread(wfShareRxAudioThread);
                    wfShareRxAudioThread->start(QThread::TimeCriticalPriority);
                    connect(wfShareRxAudioThread, SIGNAL(finished()), wfShareRxAudio, SLOT(deleteLater()));
                    connect(wfShareRxAudio, &audioHandlerBase::haveLevels, this,
                            [this](quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current,
                                   bool under, bool over) {
                                Q_UNUSED(amplitudeRMS)
                                wfShareStatus.rxAudioLevel = quint8(qMin<quint16>(amplitudePeak, 255));
                                wfShareStatus.rxLatency = latency;
                                wfShareStatus.rxCurrentLatency = qint16(current);
                                wfShareStatus.rxUnderrun = under;
                                wfShareStatus.rxOverrun = over;
                                applyIaxStatsToNetworkStatus(wfShareStats, &wfShareStatus);
                                emit haveStatusUpdate(wfShareStatus);
                            });

#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
                    QMetaObject::invokeMethod(wfShareRxAudio, [this]() {
                        wfShareRxAudio->init(wfShareRxSetup);
                    }, Qt::QueuedConnection);
#else
                    QMetaObject::invokeMethod(wfShareRxAudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, wfShareRxSetup));
#endif
                }

                QMetaObject::invokeMethod(wfShareRxAudio, "incomingAudio", Qt::QueuedConnection, Q_ARG(audioPacket, packet));
                receiveAudioData(packet);
            } else {
                qWarning(logRig()) << "wfshare audio packet decode failed";
            }
        }
    });
    connect(wfShareTransport, &RadioTransport::connectedChanged, this, [this](bool connected) {
        if (connected) {
            qInfo(logRig()) << "wfshare CI-V transport connected";
            QMetaObject::invokeMethod(this, [this]() {
                commonSetup();
            }, Qt::QueuedConnection);
        }
    });
    connect(wfShareTransport, &RadioTransport::errorOccurred, this, [this](const QString &message) {
        handlePortError(errorType(true, QStringLiteral("wfshare"), message));
    });
    connect(this, &icomCommander::dataForComm, wfShareTransport, [this](const QByteArray &data) {
        if (wfShareTransport != nullptr) {
            wfShareTransport->sendStream(RadioTransportChannel::Civ, data);
        }
    });

    RadioTransportEndpoint endpoint;
    endpoint.host = host;
    endpoint.port = port;
    endpoint.username = username;
    endpoint.password = password;
    endpoint.options.insert(QStringLiteral("calledNumber"), calledNumber);
    wfShareTransport->connectTransport(endpoint);
}

void icomCommander::setLocalAudioVolume(quint8 level)
{
    if (udp != nullptr) {
        emit haveSetVolume(level);
    }

    if (wfShareTransport != nullptr) {
        wfShareRxSetup.localAFgain = level;
        if (wfShareRxAudio != nullptr) {
            QMetaObject::invokeMethod(wfShareRxAudio, "setVolume",
                                      Qt::QueuedConnection, Q_ARG(quint8, level));
        }
    }
}

void icomCommander::receiveTxAudioData(const audioPacket &packet)
{
    if (udp != nullptr) {
        QMetaObject::invokeMethod(udp, "sendTxAudioData",
                                  Qt::QueuedConnection,
                                  Q_ARG(audioPacket, packet));
        return;
    }

    if (wfShareTransport == nullptr || !wfShareTransport->isConnected() ||
        !haveRigCaps || !rigCaps.hasTransmit) {
        return;
    }

    wfShareAudioTxFrames++;
    if (wfShareAudioTxFrames <= 10 || wfShareAudioTxFrames % 250 == 0) {
        qDebug(logRig()).noquote()
            << QStringLiteral("wfshare sent TX audio frame %1 bytes %2")
                   .arg(wfShareAudioTxFrames)
                   .arg(packet.data.size());
    }
    wfShareTransport->sendStream(RadioTransportChannel::AudioTx, audioPacketCodec::encode(packet));
}

void icomCommander::closeComm()
{
    qDebug(logRig()) << "Closing rig comms";
    if (comm != nullptr) {
        delete comm;
    }
    comm = nullptr;

    if (udpHandlerThread != nullptr) {
        if (udp != nullptr) {
            QMetaObject::invokeMethod(udp, &icomUdpHandler::shutdown,
                                      !udpHandlerThread->isRunning() || udp->thread() == QThread::currentThread()
                                          ? Qt::DirectConnection
                                          : Qt::BlockingQueuedConnection);
        }
        udpHandlerThread->quit();
        udpHandlerThread->wait();
    }
    udp = nullptr;

    if (vspPort != nullptr) {
        delete vspPort;
    }
    vspPort = nullptr;

    closeWfShare();
}

void icomCommander::closeWfShare()
{
    if (wfShareTransport != nullptr) {
        disconnect(wfShareTransport, nullptr, this, nullptr);
        disconnect(this, nullptr, wfShareTransport, nullptr);
        wfShareTransport->disconnectTransport();
        delete wfShareTransport;
        wfShareTransport = nullptr;
    }

    if (wfShareRxAudio != nullptr) {
        disconnect(wfShareRxAudio, nullptr, this, nullptr);
        if (wfShareRxAudioThread == nullptr ||
            wfShareRxAudio->thread() == QThread::currentThread() ||
            !wfShareRxAudioThread->isRunning()) {
            wfShareRxAudio->dispose();
        } else {
            QMetaObject::invokeMethod(wfShareRxAudio, &audioHandlerBase::dispose,
                                      Qt::BlockingQueuedConnection);
        }
    }

    if (wfShareRxAudioThread != nullptr) {
        wfShareRxAudioThread->quit();
        wfShareRxAudioThread->wait();
        delete wfShareRxAudioThread;
        wfShareRxAudioThread = nullptr;
    }
    wfShareRxAudio = nullptr;

    wfShareAudioRxFrames = 0;
    wfShareAudioTxFrames = 0;
    wfShareStats = IaxStats();
    wfShareStatus = networkStatus();
}

void icomCommander::commonSetup()
{

    // common elements between the two constructors go here:
    setCIVAddr(civAddr);
    spectSeqMax = 0; // this is now set after rig ID determined

    payloadSuffix = QByteArray("\xFD");

    lookingForRig = true;
    foundRig = false;

    // Add the below commands so we can get a response until we have received rigCaps
    rigCaps.commands.clear();
    rigCaps.commandsReverse.clear();
    rigCaps.commands.insert(funcTransceiverId,funcType(funcTransceiverId, QString("Transceiver ID"),QByteArrayLiteral("\x19\x00"),0,0,false,false,true,false,1,false));
    rigCaps.commandsReverse.insert(QByteArrayLiteral("\x19\x00"),funcTransceiverId);

    rigCaps.commands.insert(funcPowerControl,funcType(funcPowerControl, QString("Power Control"),QByteArrayLiteral("\x18"),0,0,false,false,true,false,1,false));
    rigCaps.commandsReverse.insert(QByteArrayLiteral("\x18"),funcPowerControl);

    connect(queue,SIGNAL(haveCommand(funcs,QVariant,uchar)),this,SLOT(receiveCommand(funcs,QVariant,uchar)));
    connect(queue,SIGNAL(haveRawCommand(QByteArray,uchar)),this,SLOT(receiveRawExternalCommand(QByteArray,uchar)));
    oldScopeMode = 0xff;

    pttAllowed = true; // This is for developing, set to false for "safe" debugging. Set to true for deployment.

    emit commReady();
}



void icomCommander::process()
{
    // new thread enters here. Do nothing but do check for errors.
    if(comm!=nullptr && comm->serialError)
    {
        emit havePortError(errorType(rigSerialPort, QString("Error from commhandler. Check serial port.")));
    }
}


void icomCommander::receiveBaudRate(quint32 baudrate) {
    rigCaps.baudRate = baudrate;
    emit haveBaudRate(baudrate);
}

void icomCommander::setPTTType(pttType_t ptt)
{
    qDebug(logRig()) << "Received request to set PTT Type to:" << ptt;
    if(!usingNativeLAN)
    {
        if(comm != nullptr)
        {
            comm->setPTTType(ptt);
        }
    }
}

void icomCommander::prepDataAndSend(QByteArray data)
{    
    data.prepend(payloadPrefix);
    data.append(payloadSuffix);

    if(data[4] != '\x15')
    {
        // We don't print out requests for meter levels
        qDebug(logRigTraffic()) << "Final payload in rig commander to be sent to rig: ";
        printHexNow(data, logRigTraffic());
    }
    emit dataForComm(data);
}

void icomCommander::dataFromServer(QByteArray data)
{
    recordLastExternalCommand(data);
    rigCommander::dataFromServer(data);
}

void icomCommander::dataFromVspClient(QByteArray data)
{
    QByteArray commandData;
    const funcs externalFunc = lookupExternalCommand(data, &commandData);
    if (externalFunc == funcCIVTransceive) {
        QByteArray reply = QByteArrayLiteral("\xfe\xfe\x00\x00\xfb\xfd");
        if (data.size() > 3) {
            reply[2] = data[3];
            reply[3] = data[2];
        }
        emit haveDataForVsp(data);
        emit haveDataForVsp(reply);
        if (!vspTransceiveDisabled) {
            qInfo(logSerial()) << "pty requested CI-V Transceive disable";
            vspTransceiveDisabled = true;
        }
        return;
    }

    dataFromExternalClient(data);
}

void icomCommander::dataFromExternalClient(QByteArray data)
{
    bool transmit = false;
    if (detectExternalTransmitStatus(data, &transmit))
        emit externalPttChanged(transmit);

    recordLastExternalCommand(data);
    if (vspQueueEnabled && queueExternalCommand(data))
        return;

    emit dataForComm(data);
}

void icomCommander::dataFromRigToExternalClient(QByteArray data)
{
    int fePos = data.lastIndexOf(char(0xfe));
    if (fePos > 0 && data.length() > fePos + 3)
        fePos--;

    if (fePos < 0 || data.length() <= fePos + 3) {
        qDebug(logSerial()) << "Invalid CI-V command for VSP";
        return;
    }

    if (vspTransceiveDisabled) {
        if (quint8(data.at(fePos + 2)) == 0x00 || quint8(data.at(fePos + 3)) == 0x00) {
            qDebug(logSerial()) << "Transceive command filtered";
            return;
        }
    }

    if (quint8(data.at(fePos + 2)) == quint8(0xE1) || quint8(data.at(fePos + 3)) == quint8(0xE1))
        return;

    emit haveDataForVsp(data);
}

void icomCommander::receiveRawExternalCommand(QByteArray data, uchar receiver)
{
    Q_UNUSED(receiver)
    emit dataForComm(data);
}

bool icomCommander::queueExternalCommand(QByteArray data)
{
    if (queue == nullptr || data.isEmpty())
        return false;

    QVector<QByteArray> frames;
    int searchFrom = 0;
    while (searchFrom < data.size()) {
        int frameStart = -1;
        for (int i = searchFrom; i + 1 < data.size(); ++i) {
            if (quint8(data.at(i)) == 0xfe && quint8(data.at(i + 1)) == 0xfe) {
                frameStart = i;
                break;
            }
        }

        if (frameStart < 0)
            break;

        const int frameEnd = data.indexOf(char(0xfd), frameStart + 2);
        if (frameEnd < 0)
            break;

        frames.append(data.mid(frameStart, frameEnd - frameStart + 1));
        searchFrom = frameEnd + 1;
    }

    if (frames.isEmpty()) {
        queue->add(priorityImmediate, queueItem(data, 0));
        return true;
    }

    for (int frameIndex = frames.size() - 1; frameIndex >= 0; --frameIndex) {
        const QByteArray frame = frames.at(frameIndex);
        QByteArray commandData;
        QByteArray lookupData;
        int matchedLength = 0;
        const funcs func = lookupExternalCommand(frame, &commandData, &lookupData, &matchedLength);
        uchar receiver = 0;
        if (rigCaps.hasCommand29 && commandData.size() >= 2 && quint8(commandData.at(0)) == 0x29)
            receiver = quint8(commandData.at(1));

        if (func != funcNone && matchedLength > 0 && lookupData.size() == matchedLength)
            queue->add(externalCommandPriority(func), func, false, receiver);
        else
            queue->add(externalCommandPriority(func), queueItem(frame, receiver));
    }

    return true;
}

funcs icomCommander::lookupExternalCommand(const QByteArray &data, QByteArray *commandData,
                                           QByteArray *lookupData, int *matchedLength) const
{
    if (commandData)
        commandData->clear();
    if (lookupData)
        lookupData->clear();
    if (matchedLength)
        *matchedLength = 0;

    int frameStart = -1;
    for (int i = 0; i + 3 < data.size(); ++i) {
        if (quint8(data.at(i)) == 0xfe && quint8(data.at(i + 1)) == 0xfe) {
            frameStart = i;
            break;
        }
    }

    if (frameStart < 0 || data.size() <= frameStart + 4) {
        return funcNone;
    }

    QByteArray command = data.mid(frameStart + 4);
    const int terminator = command.indexOf(char(0xfd));
    if (terminator >= 0) {
        command.truncate(terminator);
    }

    if (command.isEmpty()) {
        return funcNone;
    }

    QByteArray lookup = command;
    if (rigCaps.hasCommand29 && lookup.size() >= 3 && quint8(lookup.at(0)) == 0x29) {
        lookup.remove(0, 2);
    }

    if (commandData)
        *commandData = command;
    if (lookupData)
        *lookupData = lookup;

    for (int len = qMin(lookup.size(), 10); len > 0; --len) {
        auto it = rigCaps.commandsReverse.constFind(lookup.left(len));
        if (it == rigCaps.commandsReverse.constEnd()) {
            continue;
        }

        if (matchedLength)
            *matchedLength = len;
        return it.value();
    }

    return funcNone;
}

void icomCommander::recordLastExternalCommand(const QByteArray &data)
{
    QByteArray commandData;
    const funcs func = lookupExternalCommand(data, &commandData);
    const auto cmdIt = rigCaps.commands.find(func);
    if (func != funcNone && cmdIt != rigCaps.commands.end()) {
        const funcType cmd = cmdIt.value();
        lastCommand.func = cmd.cmd;
        lastCommand.data = commandData;
        lastCommand.minValue = cmd.minVal;
        lastCommand.maxValue = cmd.maxVal;
        lastCommand.bytes = cmd.bytes;
        return;
    }

    lastCommand.func = funcNone;
    lastCommand.data = commandData;
    lastCommand.minValue = 0;
    lastCommand.maxValue = 0;
    lastCommand.bytes = 0;
}

bool icomCommander::detectExternalTransmitStatus(const QByteArray &data, bool *transmit) const
{
    QByteArray lookupData;
    int matchedLength = 0;
    const funcs func = lookupExternalCommand(data, nullptr, &lookupData, &matchedLength);
    if (func != funcTransceiverStatus || matchedLength <= 0 || lookupData.size() <= matchedLength)
        return false;

    if (transmit)
        *transmit = quint8(lookupData.at(matchedLength)) != 0;
    return true;
}

funcType icomCommander::getCommand(funcs func, QByteArray &payload, int value, uchar receiver)
{
    funcType cmd;
    // Value is set to INT_MIN by default as this should be outside any "real" values
    auto it = rigCaps.commands.find(func);
    if (it != rigCaps.commands.end())
    {
        if (value == INT_MIN || (value>=it.value().minVal && value <= it.value().maxVal))
        {
            /*
            if (value == INT_MIN)
                qDebug(logRig()) << QString("%0 with no value (get)").arg(funcString[func]);
            else
                qDebug(logRig()) << QString("%0 with value %1 (Range: %2-%3)").arg(funcString[func]).arg(value).arg(it.value().minVal).arg(it.value().maxVal);
            */
            if (rigCaps.hasCommand29 && it.value().cmd29)
            {
                // This can use cmd29 so add sub/main to the command
                payload.append('\x29');
                payload.append(static_cast<uchar>(receiver));
            } else if (!rigCaps.hasCommand29 && receiver)
            {
                // We don't have command29 so can't select sub, but if this is a scope command, let it through.
                switch (func)
                {
                case funcScopeMode: case funcScopeSpan: case funcScopeRef: case funcScopeHold:
                case funcScopeSpeed: case funcScopeRBW: case funcScopeVBW: case funcScopeCenterType: case funcScopeEdge:
                    break;
                default:
                    qDebug(logRig()) << "Rig has no Command29, removing command:" << funcString[func] << "VFO" << receiver;
                    queue->del(func,receiver);
                    break;
                }
            }
            payload.append(it.value().data);
            cmd = it.value();
        }
        else if (value != INT_MIN)
        {
            qDebug(logRig()) << QString("Value %0 for %1 is outside of allowed range (%2-%3)").arg(value).arg(funcString[func]).arg(it.value().minVal).arg(it.value().maxVal);
        }
    } else {
        // Don't try this command again as the rig doesn't support it!
        qDebug(logRig()) << "Removing unsupported command from queue" << funcString[func] << "VFO" << receiver;
        queue->del(func,receiver);
    }
    return cmd;
}

void icomCommander::powerOn()
{
    QByteArray payload;

    int numFE=150;

    switch (this->rigBaudRate) {
    case 57600:
        numFE = 75;
        break;
    case 38400:
        numFE = 50;
        break;
    case 19200:
        numFE = 25;
        break;
    case 9600:
        numFE = 13;
        break;
    case 4800:
        numFE = 7;
        break;
    }

    if (!usingNativeLAN || !rigCaps.hasLan) {
        for(int i=0; i < numFE; i++)
        {
            payload.append("\xFE");
        }
    }

    quint8 cmd = 0x01;
    payload.append(payloadPrefix);
    if (getCommand(funcPowerControl,payload,cmd).cmd != funcNone)
    {
        payload.append(cmd);
        payload.append(payloadSuffix); // FD
    }
    else
    {
        // We may not know the command to turn the radio on so here it is:
        payload.append("\x18\x01");
        payload.append(payloadSuffix); // FD
    }

    qDebug(logRig()) << "Power ON command in icomCommander to be sent to rig: ";
    printHex(payload);

    emit dataForComm(payload);

}

void icomCommander::powerOff()
{
    QByteArray payload;
    quint8 cmd = '\x00';
    if (getCommand(funcPowerControl,payload,cmd).cmd != funcNone)
    {
        payload.append(cmd);
        prepDataAndSend(payload);
    }
}


QByteArray icomCommander::makeFreqPayload(freqt freq,uchar numchars)
{
    QByteArray result;
    quint64 freqInt = freq.Hz;

    quint8 a;

    if (numchars == 5 && freq.Hz >= 1E10)
    {
        // Quick fix for IC905, will need to do something better eventually M0VSE
        numchars = 6;
    }

    for (int i = 0; i < numchars; i++) {
        a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10)<<4;

        freqInt /= 10;

        result.append(a);
        //printHex(result, false, true);
    }

    return result;
}

QByteArray icomCommander::makeFreqPayload(double freq)
{
    quint64 freqInt = (quint64) (freq * 1E6);

    QByteArray result;
    quint8 a;
    int numchars = 5;
    if (freqInt >= 1E10)
        numchars = 6;

    for (int i = 0; i < numchars; i++) {
        a = 0;
        a |= (freqInt) % 10;
        freqInt /= 10;
        a |= ((freqInt) % 10)<<4;

        freqInt /= 10;

        result.append(a);
        //printHex(result, false, true);
    }
    //qInfo(logRig()) << "encoded frequency for " << freq << " as int " << freqInt;
    //printHex(result, false, true);
    return result;
}


QByteArray icomCommander::encodeTone(quint16 tone)
{
    return encodeTone(tone, false, false);
}

QByteArray icomCommander::encodeTone(quint16 tone, bool tinv, bool rinv)
{
    // This function is fine to use for DTCS and TONE
    QByteArray enct;

    quint8 inv=0;
    inv = inv | (quint8)rinv;
    inv = inv | ((quint8)tinv) << 4;

    enct.append(inv);

    quint8 hundreds = tone / 1000;
    quint8 tens = (tone-(hundreds*1000)) / 100;
    quint8 ones = (tone -(hundreds*1000)-(tens*100)) / 10;
    quint8 dec =  (tone -(hundreds*1000)-(tens*100)-(ones*10));

    enct.append(tens | (hundreds<<4));
    enct.append(dec | (ones <<4));

    return enct;
}


toneInfo icomCommander::decodeTone(QByteArray eTone)
{
    // index:  00 01  02 03 04
    // CTCSS:  1B 01  00 12 73 = PL 127.3, decode as 1273
    // D(T)CS: 1B 01  TR 01 23 = T/R Invert bits + DCS code 123

    toneInfo t;
    if (eTone.length() < 3) {
        return t;
    }

    ushort tone = (eTone.at(2) & 0x0f);
    tone += ((eTone.at(2) & 0xf0) >> 4) *   10;
    tone += (eTone.at(1) & 0x0f) *  100;
    tone += ((eTone.at(1) & 0xf0) >> 4) * 1000;

    for (const auto &ti: rigCaps.ctcss)
    {
        if (ti.tone == tone) {
            t = ti;
            break;
        }
    }

    if((eTone.at(0) & 0x01) == 0x01)
        t.tinv = true;
    if((eTone.at(0) & 0x10) == 0x10)
        t.rinv = true;

    return t;
}

void icomCommander::setCIVAddr(quint16 civAddr)
{
    // Note: This sets the radio's CIV address
    // the computer's CIV address is defined in the header file.

    this->civAddr = civAddr;
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append((char)civAddr);
    payloadPrefix.append((char)compCivAddr);
}

void icomCommander::handleNewData(const QByteArray& data)
{
    emit haveDataForServer(data);
    parseData(data);
}

void icomCommander::parseData(QByteArray dataInput)
{
    // TODO: Clean this up.
    // It finally works very nicely, needs to be streamlined.
    //

    int index = 0;
    volatile int count = 0; // debug purposes

    // use this:
    QList <QByteArray> dataList = dataInput.split('\xFD');
    QByteArray data;
    // qInfo(logRig()) << "data list has this many elements: " << dataList.size();
    if (dataList.last().isEmpty())
    {
        dataList.removeLast(); // if the original ended in FD, then there is a blank entry at the end.
    }
    // Only thing is, each frame is missing '\xFD' at the end. So append! Keeps the frames intact.
    for(index = 0; index < dataList.count(); index++)
    {
        data = dataList[index];
        data.append('\xFD'); // because we expect it to be there.

        count++;
        // Data echo'd back from the rig start with this:
        // fe fe 94 e0 ...... fd

        // Data from the rig that is not an echo start with this:
        // fe fe e0 94 ...... fd (for example, a reply to a query)

        // Data from the rig that was not asked for is sent to controller 0x00:
        // fe fe 00 94 ...... fd (for example, user rotates the tune control or changes the mode)

        //qInfo(logRig()) << "Data received: ";
        //printHex(data, false, true);
        if(data.length() < 4)
        {
            if(data.length())
            {
                // Finally this almost never happens
                // qInfo(logRig()) << "Data length too short: " << data.length() << " bytes. Data:";
                //printHex(data, false, true);
            }
            // no
            //return;
            // maybe:
            // continue;
        }

        if(!data.startsWith("\xFE\xFE"))
        {
            // qInfo(logRig()) << "Warning: Invalid data received, did not start with FE FE.";
            // find 94 e0 and shift over,
            // or look inside for a second FE FE
            // Often a local echo will miss a few bytes at the beginning.
            if(data.startsWith('\xFE'))
            {
                data.prepend('\xFE');
                // qInfo(logRig()) << "Warning: Working with prepended data stream.";
                parseData(payloadIn);
                return;
            } else {
                //qInfo(logRig()) << "Error: Could not reconstruct corrupted data: ";
                //printHex(data, false, true);
                // data.right(data.length() - data.find('\xFE\xFE'));
                // if found do not return and keep going.
                return;
            }
        }

        if((quint8)data[02] == civAddr)
        {
            // data is or begins with an echoback from what we sent
            // find the first 'fd' and cut it. Then continue.
            //payloadIn = data.right(data.length() - data.indexOf('\xfd')-1);
            // qInfo(logRig()) << "[FOUND] Trimmed off echo:";
            //printHex(payloadIn, false, true);
            //parseData(payloadIn);
            //return;
        }

        incomingCIVAddr = data[03] & 0xff; // track the CIV of the sender.

        switch(data[02])
        {
            //    case civAddr: // can't have a variable here :-(
            //        // data is or begins with an echoback from what we sent
            //        // find the first 'fd' and cut it. Then continue.
            //        payloadIn = data.right(data.length() - data.indexOf('\xfd')-1);
            //        //qInfo(logRig()) << "Trimmed off echo:";
            //        //printHex(payloadIn, false, true);
            //        parseData(payloadIn);
            //        break;
            // case '\xE0':

            case (char)0xE0:
            case (char)compCivAddr:
                // data is a reply to some query we sent
                // extract the payload out and parse.
                // payload = getpayload(data); // or something
                // parse (payload); // recursive ok?
                payloadIn = data.right(data.length() - 4);
                if(payloadIn.contains("\xFE"))
                {
                    //qDebug(logRig()) << "Corrupted data contains FE within message body: ";
                    //printHex(payloadIn);
                    break;
                }
                parseCommand();
                // We can use this to indicate power status I think
                if (!rigPoweredOn) {
                    queue->receiveValue(funcPowerControl,QVariant::fromValue<bool>(true),0);
                    rigPoweredOn = true;
                }

                break;
            case '\x00':
                // data send initiated by the rig due to user control
                // extract the payload out and parse.
                if((quint8)data[03]==compCivAddr)
                {
                    // This is an echo of our own broadcast request.
                    // The data are "to 00" and "from E1"
                    // Don't use it!
                    // We can use this to indicate power status I think.
                    if (rigPoweredOn) {
                        qDebug(logRig()) << "Echo caught:" << data.toHex(' ');
                        queue->message("Radio is available but may be powered-off");
                        queue->receiveValue(funcPowerControl,QVariant::fromValue<bool>(false),0);
                        rigPoweredOn = false;
                    }
                } else {
                    payloadIn = data.right(data.length() - 4); // Removes FE FE E0 94 part
                    if(payloadIn.contains("\xFE"))
                    {
                        //qDebug(logRig()) << "Corrupted data contains FE within message body: ";
                        //printHex(payloadIn);
                        break;
                    }
                    parseCommand();
                }
                break;
            default:
                // could be for other equipment on the CIV network.
                // just drop for now.
                // relaySendOutData(data);
                break;
        }
    }
    /*
    if(dataList.length() > 1)
    {
        qInfo(logRig()) << "Recovered " << count << " frames from single data with size" << dataList.count();
    }
    */
}

void icomCommander::parseCommand()
{

#ifdef DEBUG_PARSE
    QElapsedTimer performanceTimer;
    performanceTimer.start();
#endif

    funcs func = funcNone;
    uchar receiver = 0; // Used for Dual/RX
    uchar vfo=0; // Used for second VFO

    if (payloadIn.endsWith((char)0xfd))
    {
        payloadIn.chop(1);
    }

    if (rigCaps.hasCommand29 && payloadIn.at(0) == '\x29')
    {
        receiver = static_cast<uchar>(payloadIn.at(1));
        payloadIn.remove(0,2);
    }

    // As some commands bave both single and multi-byte options, start at 4 characters and work down to 1.
    // This is quite wasteful as many commands are single-byte, but I can't think of an easier way?
    int count = 0;
    for (int i=4;i>0;i--)
    {
        auto it = rigCaps.commandsReverse.find(payloadIn.left(i));
        if (it != rigCaps.commandsReverse.end())
        {
            func = it.value();
            count = i;
            break;
        }
    }

    // Remove the command so all we are left with is the data.
    payloadIn.remove(0,count);

#ifdef DEBUG_PARSE
    int currentParse=performanceTimer.nsecsElapsed();
#endif

    if (!rigCaps.commands.contains(func)) {
        // Don't warn if we haven't received rigCaps yet
        if (haveRigCaps)
            qInfo(logRig()) << "Unsupported command received from rig" <<  payloadIn.toHex().mid(0,10) << "Check rig file";
        return;
    }

    // If this is a non-command29 radio, and command is not selected/unselected, then get the current receiver from cache.
    // non-command29 radios only provide selected/unselected on the main band.
    if (!rigCaps.hasCommand29 && func != funcSelectedFreq && func != funcSelectedMode && func != funcUnselectedFreq && func != funcUnselectedMode )
    {
        receiver = queue->getState().receiver;
    }


    freqt test;
    QVector<memParserFormat> memParser;
    QVariant value;
    switch (func)
    {
    case funcVFODualWatch:
        value.setValue(static_cast<bool>(bool(payloadIn.at(0))));
        break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcFreq:
        receiver = payloadIn.at(0);
        payloadIn.remove(0,1);
    case funcSelectedFreq:
    case funcUnselectedFreq:
    case funcFreqGet:
    case funcFreqTR:
    case funcTXFreq:
    {
        if (func == funcFreqTR || func == funcFreqGet)
        {
            if (rigCaps.commands.contains(funcFreq))
                func = funcFreq;
            else if (rigCaps.commands.contains(funcSelectedFreq))
                func = funcSelectedFreq;
            else
                func = funcFreqGet;
        }
        else if (func == funcUnselectedFreq)
        {
            vfo = 1;
        }

        value.setValue(parseFreqData(payloadIn,vfo));
        //qDebug(logRig()) << funcString[func] << "len:" << payloadIn.size() << "receiver=" << receiver << "vfo=" << vfo <<
        //    "value:" << value.value<freqt>().Hz << "data:" << payloadIn.toHex(' ');

        break;
    }
    case funcMode:
        receiver = payloadIn.at(0);
        payloadIn.remove(0,1);
    case funcModeGet:
    case funcModeTR:
    case funcSelectedMode:
    case funcUnselectedMode:
    case funcDataModeWithFilter:
    {
        funcs origFunc = func;
        // First we need to work out what command we want to actually use
        if (func == funcModeTR || func == funcModeGet || func == funcDataModeWithFilter)
        {
            if (rigCaps.commands.contains(funcMode))
                func = funcMode;
            else if (rigCaps.commands.contains(funcSelectedMode))
                func = funcSelectedMode;
            else
                func = funcModeGet;
        } else if (func == funcUnselectedMode)
        {
            vfo = 1;
        }

        modeInfo mi;

        // then get the current cached value
        cacheItem ci = queue->getCache(func,receiver);
        if (ci.value.isValid()) {
            mi = queue->getCache(func,receiver).value.value<modeInfo>();
        }

        // then parse the data
        if (origFunc == funcDataModeWithFilter)
        {
            // Old format payload with datamode+filter
            mi.filter = bcdHexToUChar(payloadIn.at(1));
            mi.data = bcdHexToUChar(payloadIn.at(0));
        }
        else
        {
            if (payloadIn.size())
                mi.reg = bcdHexToUChar(payloadIn.at(0));
            if (payloadIn.size()==2)
                mi.filter = payloadIn.at(1);
            if (payloadIn.size()==3) {
                mi.data = payloadIn.at(1);
                mi.filter = payloadIn.at(2);
            }
        }

        mi = parseMode(mi.reg, mi.data,mi.filter,receiver,vfo);
        mi.VFO = selVFO_t(receiver);
        value.setValue(mi);
        //qInfo() << funcString[func] << "rx:" << receiver << "vfo:" << vfo << "name:"<<  mi.name << "data:" << mi.data << "filter:" << mi.filter << payloadIn.toHex(' ');
        break;
    }

    case funcVFOBandMS:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcMemoryMode:
        qInfo(logRig) << "Memory Mode command!";
        break;
    case funcSatelliteMemory:
        memParser=rigCaps.satParser;
    case funcMemoryContents:
    {
        qDebug() << "Received mem:" << payloadIn.toHex(' ');
        memoryType mem;
        if (memParser.isEmpty()) {
             memParser=rigCaps.memParser;
             mem.sat=false;
        } else {
             mem.sat=true;
        }

        if (parseMemory(&memParser,&mem)) {
            value.setValue(mem);
        }
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcMemoryClear:
    case funcMemoryKeyer:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        value.setValue(parseFreqData(payloadIn,receiver));
        break;
    // These return a single byte that we convert to a uchar (0-99)
    case funcTuningStep:
    case funcAttenuator:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // Return a duplexMode_t for split or duplex (same function)
    case funcSplitStatus:
        value.setValue(static_cast<duplexMode_t>(uchar(payloadIn.at(0))));
        break;
    case funcQuickSplit:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    case funcAntenna:
    {
        antennaInfo ant;
        ant.rx=false;
        ant.antenna = bcdHexToUChar(payloadIn.at(0));
        if (payloadIn.size()>1)
            ant.rx = static_cast<bool>(payloadIn.at(1));
        value.setValue(ant);
        //qInfo(logRig()) << "Got antenna" << ant.antenna << "rx" << ant.rx;
        break;

    // Register 13 (speech) has no get values
    // Register 14 (levels) starts here:
    }
    case funcAfGain:
        value.setValue(bcdHexToUChar(payloadIn.at(0),payloadIn.at(1)));
        break;
    // The following group are 2 bytes converted to uchar (0-255) but require special processing
    case funcKeySpeed: {
        uchar level = bcdHexToUChar(payloadIn.at(0),payloadIn.at(1));
        value.setValue<ushort>(round((level / 6.071) + 6));
        break;
    }
    case funcCwPitch: {
        uchar level = bcdHexToUChar(payloadIn.at(0),payloadIn.at(1));
        value.setValue<ushort>(round((((600.0 / 255.0) * level) + 300) / 5.0) * 5.0);
        break;
    }
    // 0x15 Meters
    case funcSMeter:
        value.setValue(getMeterCal(meterS,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcCenterMeter:
        value.setValue(getMeterCal(meterCenter,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcPowerMeter:
        value.setValue(getMeterCal(meterPower,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcSWRMeter:
        value.setValue(getMeterCal(meterSWR,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcALCMeter:
        value.setValue(getMeterCal(meterALC,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcCompMeter:
        value.setValue(getMeterCal(meterComp,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcVdMeter:
        value.setValue(getMeterCal(meterVoltage,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;
    case funcIdMeter:
        value.setValue(getMeterCal(meterCurrent,bcdHexToUChar(payloadIn.at(0),payloadIn.at(1))));
        break;

    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcMicGain:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
    case funcBackLightLevel:

	case funcBeepLevel:
	case funcBeepMain:
	case funcBeepSub:
	
	case funcRFSQLControl:
	case funcTXDelayHF:
	case funcTXDelay50m:
	case funcTimeOutTimer:
	case funcTimeOutCIV:   
	
        value.setValue(bcdHexToUChar(payloadIn.at(0),payloadIn.at(1)));
        break;
    case funcAGC:
    case funcAGCTimeConstant:
    case funcBreakIn:   // This is 0,1 or 2
    case funcPreamp:
    case funcManualNotchWidth:
    case funcSSBTXBandwidth:
    case funcRoofingFilter:
    case funcFilterShape:
    // Bass treble (A105)
    case funcSSBRXBass:
    case funcSSBRXTreble:
    case funcAMRXBass:
    case funcAMRXTreble:
    case funcFMRXBass:
    case funcFMRXTreble:
    case funcSSBTXBass:
    case funcSSBTXTreble:
    case funcAMTXBass:
    case funcAMTXTreble:
    case funcFMTXBass:
    case funcFMTXTreble:
    case funcBandEdgeBeep:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;

    // LPF/HPF
    case funcSSBRXHPFLPF:
    case FuncAMRXHPFLPF:
    case funcFMRXHPFLPF:
    case FuncCWRXHPFLPF:
    case funcRTTYRXHPFLPF:
        value.setValue(lpfhpf(ushort(payloadIn.at(0))*100,ushort(payloadIn.at(1))*100));
        break;
    case funcAbsoluteMeter:
    {
        meterkind m;
        m.value = double(bcdHexToUInt(payloadIn.at(0),payloadIn.at(1)))/10.0;
        if (bool(payloadIn.at(2)) == true) {
            m.value=-m.value;
        }
        if (payloadIn.at(3) == 0)
            m.type=meterdBu;
        else if (payloadIn.at(3) == 1)
            m.type=meterdBuEMF;
        else if (payloadIn.at(3) == 2)
            m.type=meterdBm;
        else {
            qWarning(logRig()) << "Unknown meter type received!";
            m.type = meterNone;
        }
        value.setValue(m);
        break;
    }
    case funcMeterType:
    {
        meter_t m;
        if (payloadIn.at(0) == 0)
            m = meterS;
        else if (payloadIn.at(0) == 1)
            m = meterdBu;
        else if (payloadIn.at(0) == 2)
            m = meterdBuEMF;
        else if (payloadIn.at(0) == 3)
            m = meterdBm;
        else {
            qWarning(logRig()) << "Unknown meterType received!";
            m = meterNone;
        }
        value.setValue(m);
        break;
    }
    // The following group ALL return bool
    case funcMainSubTracking:
    case funcSatelliteMode:
    case funcNoiseBlanker:
    case funcAudioPeakFilter:
    case funcNoiseReduction:
    case funcAutoNotch:
    case funcRepeaterTone:
    case funcRepeaterTSQL:
    case funcRepeaterDTCS:
    case funcRepeaterCSQL:
    case funcCompressor:
    case funcMonitor:
    case funcVox:
    case funcManualNotch:
    case funcDigiSel:
    case funcTwinPeakFilter:
    case funcDialLock:
    case funcOverflowStatus:
    case funcSMeterSqlStatus:
    case funcVariousSql:
    case funcRXAntenna:
    case funcIPPlus:
    case funcBeepLevelLimit:
    case funcBeepConfirmation:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcToneSquelchType:
    {
        rptrAccessData r;
        r.accessMode = static_cast<rptAccessTxRx_t>(bcdHexToUChar(payloadIn.at(0)));
        r.useSecondaryVFO = static_cast<bool>(vfo);
        value.setValue(r);
        break;
    }
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 it automatically added.
    case funcTransceiverId:
        if (!rigCaps.modelID)
        {
            rigCaps.modelID = static_cast<quint16>(payloadIn.at(0)) & 0xff;
            if (rigList.contains(rigCaps.modelID))
            {
                this->model = rigList.find(rigCaps.modelID).key();
            }
            qInfo(logRig()) << QString("Have new rig ID: 0x%0").arg(rigCaps.modelID,2,16);
            determineRigCaps();
            this->civAddr = incomingCIVAddr & 0xff; // Override and use immediately.
            payloadPrefix = QByteArray("\xFE\xFE");
            payloadPrefix.append((char)civAddr);
            payloadPrefix.append((char)compCivAddr);
        }
        value.setValue(rigCaps.modelID);
        break;
    // 0x1a
    case funcBandStackReg:
    {
        bandStackType bsr;
        bsr.band = bcdHexToUChar(payloadIn.at(0));
        bsr.reg = bcdHexToUChar(payloadIn.at(1));
        for (const auto &b: rigCaps.bands)
        {
            if (b.bsr == bsr.band)
            {
                bsr.freq = parseFreqData(payloadIn.mid(2,b.bytes),receiver);
                // The Band Stacking command returns the reg in the position that VFO is expected.
                // As BSR is always on the active VFO, just set that.
                bsr.freq.VFO = selVFO_t::activeVFO;
                bsr.mode = bcdHexToUChar(payloadIn.at(b.bytes+2));
                bsr.filter = bcdHexToUChar(payloadIn.at(b.bytes+3));
                bsr.data = (payloadIn.at(b.bytes+4) & 0xf0) >> 4;
                bsr.sql = (payloadIn.at(b.bytes+4) & 0x0f);
                if (payloadIn.length()>b.bytes+10) {
                    bsr.tone = decodeTone(payloadIn.mid(b.bytes+5,3));
                    bsr.tsql = decodeTone(payloadIn.mid(b.bytes+8,3));
                }
                qDebug(logRig()) << QString("BSR received, band:%0 code:%1 freq:%2 data:%3 mode:%4 filter:%5")
                                        .arg(bsr.band).arg(bsr.reg).arg(bsr.freq.Hz).arg(bsr.data).arg(bsr.mode).arg(bsr.filter);
                value.setValue(bsr);
                break;
            }

        }
        if (!value.isValid()) {
            qWarning(logRig()) << "Unknown BSR received, check rig file:" << payloadIn.toHex(' ');
        } else {
            qInfo(logRig()) << "BSR received:" << payloadIn.toHex(' ');
        }
        break;
    }
    case funcFilterWidth:
    {
        quint16 calc;
        quint8 pass = bcdHexToUChar((quint8)payloadIn.at(0));
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);
        modeInfo m = queue->getCache(t.modeFunc,t.receiver).value.value<modeInfo>();

        if (m.mk == modeAM)
        {
             calc = 200 + (pass * 200);
        }
        else if (pass <= 10)
        {
             calc = 50 + (pass * 50);
        }
        else {
             calc = 600 + ((pass - 10) * 100);
        }
        value.setValue(calc);
        //qInfo() << "Got filter width" << calc << "VFO" << receiver;
        break;
    }
    case funcAFMute:
        // TODO add AF Mute
        break;
    // 0x1a 0x05 various registers below are 2 byte (0-255) as uchar
    case funcREFAdjust:
    case funcREFAdjustFine:
    case funcACCAModLevel:
    case funcACCBModLevel:
    case funcUSBModLevel:
    case funcLANModLevel:
    case funcSPDIFModLevel:
    case funcNBWidth:
        value.setValue(bcdHexToUChar(payloadIn.at(0),payloadIn.at(1)));
        break;
    // Singla byte returned as uchar (0-99)
    case funcDATAOffMod:
    case funcDATA1Mod:
    case funcDATA2Mod:
    case funcDATA3Mod:
    {
        for (auto &r: rigCaps.inputs)
        {
            if (r.reg == bcdHexToUChar(payloadIn.at(0)))
            {
                value.setValue(r);
                break;
            }
        }
        break;
    }
    case funcDashRatio:
    case funcNBDepth:
    case funcVOXDelay:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // Time & date functions
    case funcUTCOffset:
    case funcDate:
    case funcTime:
        break;
    // Fixed Freq Scope Edges
    case funcScopeEdge1a:
    case funcScopeEdge2a:
    case funcScopeEdge3a:
    case funcScopeEdge4a:
    case funcScopeEdge1b:
    case funcScopeEdge2b:
    case funcScopeEdge3b:
    case funcScopeEdge4b:
    case funcScopeEdge1c:
    case funcScopeEdge2c:
    case funcScopeEdge3c:
    case funcScopeEdge4c:
    case funcScopeEdge1d:
    case funcScopeEdge2d:
    case funcScopeEdge3d:
    case funcScopeEdge4d:
    case funcScopeEdge1e:
    case funcScopeEdge2e:
    case funcScopeEdge3e:
    case funcScopeEdge4e:
    case funcScopeEdge1f:
    case funcScopeEdge2f:
    case funcScopeEdge3f:
    case funcScopeEdge4f:
    case funcScopeEdge1g:
    case funcScopeEdge2g:
    case funcScopeEdge3g:
    case funcScopeEdge4g:
    case funcScopeEdge1h:
    case funcScopeEdge2h:
    case funcScopeEdge3h:
    case funcScopeEdge4h:
    case funcScopeEdge1i:
    case funcScopeEdge2i:
    case funcScopeEdge3i:
    case funcScopeEdge4i:
    case funcScopeEdge1j:
    case funcScopeEdge2j:
    case funcScopeEdge3j:
    case funcScopeEdge4j:
    case funcScopeEdge1k:
    case funcScopeEdge2k:
    case funcScopeEdge3k:
    case funcScopeEdge4k:
    case funcScopeEdge1l:
    case funcScopeEdge2l:
    case funcScopeEdge3l:
    case funcScopeEdge4l:
    case funcScopeEdge1m:
    case funcScopeEdge2m:
    case funcScopeEdge3m:
    case funcScopeEdge4m:
    case funcScopeEdge1n:
    case funcScopeEdge2n:
    case funcScopeEdge3n:
    case funcScopeEdge4n:
    case funcScopeEdge1o:
    case funcScopeEdge2o:
    case funcScopeEdge3o:
    case funcScopeEdge4o:
    case funcScopeEdge1p:
    case funcScopeEdge2p:
    case funcScopeEdge3p:
    case funcScopeEdge4p:
    case funcScopeEdge1q:
    case funcScopeEdge2q:
    case funcScopeEdge3q:
    case funcScopeEdge4q:
    case funcScopeEdge1r:
    case funcScopeEdge2r:
    case funcScopeEdge3r:
    case funcScopeEdge4r:
    case funcScopeEdge1s:
    case funcScopeEdge2s:
    case funcScopeEdge3s:
    case funcScopeEdge4s:
        break;
    // 0x1b register (tones)
    case funcToneFreq:
    case funcTSQLFreq:
    case funcDTCSCode:
    case funcCSQLCode:
        value.setValue(decodeTone(payloadIn));
        break;
    // 0x1c register (bool)
    case funcRitStatus:
    case funcXitStatus:
    case funcTransceiverStatus:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    // tuner is 0-2
    case funcTunerStatus:
        value.setValue(bcdHexToUChar(payloadIn.at(0)));
        break;
    // 0x21 RIT:
    case funcRitFreq:
    case funcXitFreq:
    {
        /* M0VSE DOES THIS NEEED FIXING? */
        short ritHz = 0;
        freqt f;
        QByteArray longfreq;
        longfreq = payloadIn.mid(2,2);
        longfreq.append(QByteArray(3,'\x00'));
        f = parseFrequency(longfreq, 3);
        if(payloadIn.length() < 5)
             break;
        ritHz = f.Hz*((payloadIn.at(4)=='\x01')?-1:1);
        value.setValue(ritHz);
        break;
    }
    case funcRitTXStatus:
    case funcXitTXStatus:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    case funcTXFreqMon:
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
    // 0x27
    case funcScopeWaveData:
    {
        receiver=payloadIn.at(0);
        payloadIn.remove(0,1);
        scopeData d;
        if (parseSpectrum(d,receiver))
            value.setValue(d);
        break;
    }
    case funcScopeOnOff:
        // confirming scope is on
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
    case funcScopeMainSub:
        // This tells us whether we are receiving main or sub data
    case funcScopeSingleDual:
        // This tells us whether we are receiving single or dual scopes
        //qInfo(logRig()) << "funcScopeSingleDual (" << receiver <<") " << static_cast<bool>(payloadIn.at(0));
        value.setValue(static_cast<bool>(payloadIn.at(0)));
        break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcScopeMode:
        // fixed or center
        // [1] 0x14
        // [2] 0x00
        // [3] 0x00 (center), 0x01 (fixed), 0x02, 0x03
        receiver = payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        break;
    case funcScopeSpan:
    {
        receiver = payloadIn.at(0);
        payloadIn.remove(0,1);
        freqt f = parseFrequency(payloadIn, 3);
        for (auto &s: rigCaps.scopeCenterSpans)
        {
            if (s.freq == f.Hz)
            {
                value.setValue(s);
            }
        }
        break;
    }
    case funcScopeEdge:
        // read edge mode center in edge mode
        // [1] 0x16
        // [2] 0x01, 0x02, 0x03: Edge 1,2,3
        receiver=payloadIn.at(0);
        value.setValue(bcdHexToUChar(payloadIn.at(1)));
        break;
    case funcBandEdgeFreq:
        // M0VSE add this
        break;
    case funcScopeHold:
        receiver=payloadIn.at(0);
        value.setValue(static_cast<bool>(payloadIn.at(1)));
        break;
    case funcScopeRef:
    {
        receiver=payloadIn.at(0);
        // scope reference level
        // [1] 0x19
        // [2] 0x00
        // [3] 10dB digit, 1dB digit
        // [4] 0.1dB digit, 0
        // [5] 0x00 = +, 0x01 = -
        quint8 negative = payloadIn.at(3);
        short ref = bcdHexToUInt(payloadIn.at(1), payloadIn.at(2));
        ref = ref / 10;
        if(negative){
             ref *= (-1*negative);
        }
        value.setValue(ref);
        break;
    }
    case funcScopeSpeed:
        receiver=payloadIn.at(0);
        value.setValue(static_cast<uchar>(payloadIn.at(1)));
        break;
    case funcScopeVBW:
        receiver=payloadIn.at(0);
        break;
    case funcScopeRBW:
        receiver=payloadIn.at(0);
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcScopeFixedEdgeFreq:
    case funcScopeDuringTX:
    case funcScopeCenterType:
        break;
    // 0x28
    case funcVoiceTX:
        break;
    //0x29 - Prefix certain commands with this to get/set certain values without changing current VFO
    // If we use it for get commands, need to parse the \x29\x<VFO> first.
    case funcMainSubPrefix:
        break;
    case funcPowerControl:
        qWarning(logRig()) << "Received power control command from radio" << payloadIn;
        break;
    case funcFB:
        break;
    case funcFA:
    {
        if (!lastCommand.data.isEmpty()) {
            if (!warnedAboutFA) {
                qInfo(logRig()) << "Occasional error response (FA) from rig can safely be ignored";
                 warnedAboutFA=true;
            }
            qWarning(logRig()) << "Rig (FA) error, last command sent:" << funcString[lastCommand.func] << "(min:" << lastCommand.minValue << "max:" <<
                lastCommand.maxValue << "bytes:" << lastCommand.bytes <<  ") data:" << lastCommand.data.toHex(' ');
        }
        break;
    }
    default:
        qWarning(logRig()).noquote() << "Unhandled command received from rig:" << funcString[func] << "value:" << payloadIn.toHex().mid(0,10);
        break;
    }
    if(func != funcScopeWaveData
        && func != funcSMeter
        && func != funcAbsoluteMeter
        && func != funcCenterMeter
        && func != funcPowerMeter
        && func != funcSWRMeter
        && func != funcALCMeter
        && func != funcCompMeter
        && func != funcVdMeter
        && func != funcIdMeter)
    {
        // We do not log spectrum and meter data,
        // as they tend to clog up any useful logging.
        qDebug(logRigTraffic()) << QString("Received from radio: %0").arg(funcString[func]);
        printHexNow(payloadIn, logRigTraffic());
    }


#ifdef DEBUG_PARSE
    averageParseTime += currentParse;
    if (lowParse > currentParse)
        lowParse = currentParse;
    else if (highParse < currentParse)
        highParse = currentParse;

    numParseSamples++;
    if (lastParseReport.msecsTo(QTime::currentTime()) >= 10000)
    {
        qInfo(logRig()) << QString("10 second average command parse time %0 ns (low=%1, high=%2, num=%3:").arg(averageParseTime/numParseSamples).arg(lowParse).arg(highParse).arg(numParseSamples);
        averageParseTime = 0;
        numParseSamples = 0;
        lowParse=9999;
        highParse=0;
        lastParseReport = QTime::currentTime();
    }
#endif

    if (value.isValid() && queue != nullptr) {
        queue->receiveValue(func,value,receiver);
    }

}

bool icomCommander::parseSpectrum(scopeData& d, uchar receiver)
{
    bool ret = false;

    if(!haveRigCaps)
    {
        qDebug(logRig()) << "Spectrum received in icomCommander, but rigID is incomplete.";
        return ret;
    }
    if(rigCaps.spectSeqMax == 0)
    {
        // there is a chance this will happen with rigs that support spectrum. Once our RigID query returns, we will parse correctly.
        qInfo(logRig()) << "Warning: Spectrum sequence max was zero, yet spectrum was received.";
        return ret;
    }

    d = receiver ? subScopeData : mainScopeData;
    quint8 &nextSequence = receiver ? subScopeNextSequence : mainScopeNextSequence;

    ret = IcomScopeFrameAssembler::parsePayload(d,
                                                payloadIn,
                                                receiver,
                                                rigCaps.modelID,
                                                rigCaps.spectLenMax,
                                                oldScopeMode,
                                                nextSequence);

    if (!ret && (nextSequence != 0 || !d.data.isEmpty())) {
        if (receiver)
            subScopeData = d;
        else
            mainScopeData = d;
    }
    return ret;
}

quint8 icomCommander::bcdHexToUChar(quint8 in)
{
    quint8 out = 0;
    out = in & 0x0f;
    out += ((in & 0xf0) >> 4)*10;
    return out;
}

unsigned int icomCommander::bcdHexToUInt(quint8 hundreds, quint8 tensunits)
{
    // convert:
    // hex data: 0x41 0x23
    // convert to uint:
    // uchar: 4123
    quint8 thousands = ((hundreds & 0xf0)>>4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;

    return rtnVal;
}

unsigned int icomCommander::bcdHexToUInt(quint8 tenthou, quint8 hundreds, quint8 tensunits)
{
    // convert:
    // hex data: 0x41 0x23
    // convert to uint:
    // uchar: 4123
    quint8 thousands = ((hundreds & 0xf0)>>4);
    unsigned int rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);
    rtnVal += thousands * 1000;
    rtnVal += (tenthou & 0x0f) * 10000;
    return rtnVal;
}
quint8 icomCommander::bcdHexToUChar(quint8 hundreds, quint8 tensunits)
{
    // convert:
    // hex data: 0x01 0x23
    // convert to uchar:
    // uchar: 123

    quint8 rtnVal;
    rtnVal = (hundreds & 0x0f)*100;
    rtnVal += ((tensunits & 0xf0)>>4)*10;
    rtnVal += (tensunits & 0x0f);

    return rtnVal;
}

QByteArray icomCommander::bcdEncodeInt(quint16 num)
{
    if(num > 9999)
    {
        qInfo(logRig()) << __FUNCTION__ << "Error, number is too big for four-digit conversion: " << num;
        return QByteArray();
    }

    char thousands = num / 1000;
    char hundreds = (num - (1000*thousands)) / 100;
    char tens = (num - (1000*thousands) - (100*hundreds)) / 10;
    char units = (num - (1000*thousands) - (100*hundreds) - (10*tens));

    char b0 = hundreds | (thousands << 4);
    char b1 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1);
    return result;
}



QByteArray icomCommander::bcdEncodeInt(unsigned int num)
{
    if(num > 999999)
    {
        qInfo(logRig()) << __FUNCTION__ << "Error, number is too big for six-digit conversion: " << num;
        return QByteArray();
    }

    char tenthou = num / 10000;
    char thousands = (num - (10000*tenthou)) / 1000;
    char hundreds = (num - (10000*tenthou) - (1000*thousands)) / 100;
    char tens = (num - (10000*tenthou) - (1000*thousands) - (100*hundreds)) / 10;
    char units = (num - (10000*tenthou) - (1000*thousands) - (100*hundreds) - (10*tens));

    char b0 = tenthou;
    char b1 = hundreds | (thousands << 4);
    char b2 = units | (tens << 4);

    QByteArray result;
    result.append(b0).append(b1).append(b2);
    qInfo(logRig()) << __FUNCTION__ << " encoding value " << num << " as hex:" << result.toHex(' ');

    return result;
}
QByteArray icomCommander::bcdEncodeChar(quint8 num)
{
    if(num > 99)
    {
        qInfo(logRig()) << __FUNCTION__ << "Error, number is too big for two-digit conversion: " << num;
        return QByteArray();
    }

    uchar tens = num / 10;
    uchar units = num - (10*tens);

    uchar b0 = units | (tens << 4);

    //qInfo(logRig()) << __FUNCTION__ << " encoding value " << num << " as hex:";
    //printHex(QByteArray(b0), false, true);
    //printHex(QByteArray(b1), false, true);

    QByteArray result;
    result.append(b0);
    return result;
}



freqt icomCommander::parseFrequency()
{
    freqt freq;
    freq.Hz = 0;
    freq.MHzDouble = 0;

    // process payloadIn, which is stripped.
    // float frequencyMhz
    //    payloadIn[04] = ; // XX MHz
    //    payloadIn[03] = ; //   XX0     KHz
    //    payloadIn[02] = ; //     X.X   KHz
    //    payloadIn[01] = ; //      . XX KHz

    // printHex(payloadIn, false, true);

    frequencyMhz = 0.0;
    if (payloadIn.length() == 7)
    {
        // 7300 has these digits too, as zeros.
        // IC-705 or IC-9700 with higher frequency data available.
        frequencyMhz += 100 * (payloadIn[05] & 0x0f);
        frequencyMhz += (1000 * ((payloadIn[05] & 0xf0) >> 4));

        freq.Hz += (payloadIn[05] & 0x0f) * 1E6 * 100;
        freq.Hz += ((payloadIn[05] & 0xf0) >> 4) * 1E6 * 1000;

    }

    freq.Hz += (payloadIn[04] & 0x0f) * 1E6;
    freq.Hz += ((payloadIn[04] & 0xf0) >> 4) * 1E6 * 10;

    frequencyMhz += payloadIn[04] & 0x0f;
    frequencyMhz += 10 * ((payloadIn[04] & 0xf0) >> 4);

    // KHz land:
    frequencyMhz += ((payloadIn[03] & 0xf0) >> 4) / 10.0;
    frequencyMhz += (payloadIn[03] & 0x0f) / 100.0;

    frequencyMhz += ((payloadIn[02] & 0xf0) >> 4) / 1000.0;
    frequencyMhz += (payloadIn[02] & 0x0f) / 10000.0;

    frequencyMhz += ((payloadIn[01] & 0xf0) >> 4) / 100000.0;
    frequencyMhz += (payloadIn[01] & 0x0f) / 1000000.0;

    freq.Hz += payloadIn[01] & 0x0f;
    freq.Hz += ((payloadIn[01] & 0xf0) >> 4) * 10;

    freq.Hz += (payloadIn[02] & 0x0f) * 100;
    freq.Hz += ((payloadIn[02] & 0xf0) >> 4) * 1000;

    freq.Hz += (payloadIn[03] & 0x0f) * 10000;
    freq.Hz += ((payloadIn[03] & 0xf0) >> 4) * 100000;

    freq.MHzDouble = frequencyMhz;

    return freq;
}

freqt icomCommander::parseFrequencyRptOffset(QByteArray data)
{
    // VHF 600 KHz:
    // DATA:  0c 00 60 00 fd
    // INDEX: 00 01 02 03 04

    // UHF 5 MHz:
    // DATA:  0c 00 00 05 fd
    // INDEX: 00 01 02 03 04

    freqt f;
    f.Hz = 0;

    f.Hz += (data[3] & 0x0f)        *    1E6; // 1 MHz
    f.Hz += ((data[3] & 0xf0) >> 4) *    1E6 * 10; //   10 MHz
    f.Hz += (data[2] & 0x0f) *          10E3; // 10 KHz
    f.Hz += ((data[2] & 0xf0) >> 4) *  100E3; // 100 KHz
    f.Hz += (data[1] & 0x0f) *           100; // 100 Hz
    f.Hz += ((data[1] & 0xf0) >> 4) *   1000; // 1 KHz

    f.MHzDouble=f.Hz/1E6;
    f.VFO = activeVFO;
    return f;
}

freqt icomCommander::parseFrequency(QByteArray data, quint8 lastPosition)
{
    // process payloadIn, which is stripped.
    // float frequencyMhz
    //    payloadIn[04] = ; // XX MHz
    //    payloadIn[03] = ; //   XX0     KHz
    //    payloadIn[02] = ; //     X.X   KHz
    //    payloadIn[01] = ; //      . XX KHz

    //printHex(data, false, true);

    // TODO: Check length of data array prior to reading +/- position

    // NOTE: This function was written on the IC-7300, which has no need for 100 MHz and 1 GHz.
    //       Therefore, this function has to go to position +1 to retrieve those numbers for the IC-9700.

    freqt freqs;
    freqs.MHzDouble = 0;
    freqs.Hz = 0;

    if (data.length() <= lastPosition)
    {
        // Something bad has happened!
        qWarning(logRig()) << "parseFrequency() given last position:" << lastPosition << "but data is only" << data.length() << "bytes";
        return freqs;
    }
    // Does Frequency contain 100 MHz/1 GHz data?
    if(data.length() > lastPosition+2)
    {
        freqs.Hz += (data[lastPosition+2] & 0x0f) * 1E9; //  1 GHz
        freqs.Hz += ((data[lastPosition+2] & 0xf0) >> 4) * 1E9 * 10; // 10 GHz
    }
    if(data.length() > lastPosition+1)
    {
        freqs.Hz += (data[lastPosition+1] & 0x0f) * 1E6 *         100; //  100 MHz
        freqs.Hz += ((data[lastPosition+1] & 0xf0) >> 4) * 1E6 * 1000; // 1000 MHz
    }

    // Does Frequency contain VFO data? (\x25 command)
    if (lastPosition-4 >= 0 && (quint8)data[lastPosition-4] < 0x02)
    {
        freqs.VFO=(selVFO_t)(quint8)data[lastPosition-4];
    }

    freqs.Hz += (data[lastPosition] & 0x0f) * 1E6;
    freqs.Hz += ((data[lastPosition] & 0xf0) >> 4) * 1E6 *     10; //   10 MHz

    freqs.Hz += (data[lastPosition-1] & 0x0f) *          10E3; // 10 KHz
    freqs.Hz += ((data[lastPosition-1] & 0xf0) >> 4) *  100E3; // 100 KHz

    freqs.Hz += (data[lastPosition-2] & 0x0f) *           100; // 100 Hz
    freqs.Hz += ((data[lastPosition-2] & 0xf0) >> 4) *   1000; // 1 KHz

    freqs.Hz += (data[lastPosition-3] & 0x0f) *             1; // 1 Hz
    freqs.Hz += ((data[lastPosition-3] & 0xf0) >> 4) *     10; // 10 Hz

    freqs.MHzDouble = (double)(freqs.Hz / 1000000.0);
    return freqs;
}


freqt icomCommander::parseFreqData(QByteArray data, uchar receiver)
{
    freqt freq;
    freq.Hz = parseFreqDataToInt(data);
    freq.MHzDouble = freq.Hz/1000000.0;
    freq.VFO = selVFO_t(receiver);
    //qInfo(logRig()) << "Received Frequency" << freq.Hz << "vfo" << receiver;
    return freq;
}

quint64 icomCommander::parseFreqDataToInt(QByteArray data)
{
    // Allow raw data to be parsed. Use a lookup table (pow10) for speed
    // Should support VERY large or VERY small numbers!
    quint64 val=0;

    for (int i=0;i<data.size()*2;i=i+2)
    {
        val += (data[i/2] & 0x0f) * pow10[i];
        val += ((data[i/2] & 0xf0) >> 4) * (pow10[i+1]);
    }

    return val;
}


modeInfo icomCommander::parseMode(uchar mode, uchar data, uchar filter, uchar receiver,uchar vfo)
{
    modeInfo mi;
    bool found=false;
    if (mode == 0xff)
    {
        // Get cached mode
        mi.reg=mode;
        mi.mk=modeUnknown;
        mi.filter=filter;
        mi.data=data;
        found = true;
    }
    else
    {
        for (auto& m: rigCaps.modes)
        {
            if (m.reg == mode)
            {
                mi = m;
                mi.filter = filter;
                mi.data = data;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        qInfo(logRig()) << QString("parseMode() No such mode %0 with filter %1").arg(mode).arg(filter) << payloadIn.toHex(' ') ;
    }

    // We cannot query sub VFO width without command29.
    if (!rigCaps.hasCommand29)
        receiver = 0;


    cacheItem item;

    // Does the current mode support filterwidth?
    // Cannot get the filterwidth of the second VFO, so use the default values.
    if (vfo == 0 && mi.bwMin >0  && mi.bwMax > 0) {
        item = queue->getCache(funcFilterWidth,receiver);
    }

    if (item.value.isValid()) {
        mi.pass = item.value.toInt();
    }
    else
    {

        /*  We haven't got a valid passband from the rig yet so we
            need to create a 'fake' one from default values
            This will be replaced with a valid one if we get it

            Also use default widths for modes that have no control*/

        if (mi.mk == modeCW || mi.mk == modeCW_R || mi.mk == modePSK || mi.mk == modePSK_R) {
            switch (filter) {
            case 1:
                mi.pass=1200;
                break;
            case 2:
                mi.pass=500;
                break;
            case 3:
                mi.pass=250;
                break;
            }
        }
        else if (mi.mk == modeRTTY || mi.mk == modeRTTY_R)
        {
            switch (filter) {
            case 1:
                mi.pass=2400;
                break;
            case 2:
                mi.pass=500;
                break;
            case 3:
                mi.pass=250;
                break;
            }
        }
        else if (mi.mk == modeAM)
        {
            switch (filter) {
            case 1:
                mi.pass=9000;
                break;
            case 2:
                mi.pass=6000;
                break;
            case 3:
                mi.pass=3000;
                break;
            }
        }
        else if (mi.mk == modeFM)
        {
            switch (filter) {
            case 1:
                mi.pass=15000;
                break;
            case 2:
                mi.pass=10000;
                break;
            case 3:
                mi.pass=7000;
                break;
            }
        }
        else if (mi.mk == modeWFM)
        {
            mi.pass=200000;
        }
        else { // SSB or unknown mode
            switch (filter) {
            case 1:
                mi.pass=3000;
                break;
            case 2:
                mi.pass=2400;
                break;
            case 3:
                mi.pass=1800;
                break;
            }
        }
    }

    //qInfo() << "Mode" << mi.name << "Passband" << mi.pass;
    return mi;
}

bool icomCommander::parseMemory(QVector<memParserFormat>* memParser, memoryType* mem)
{
    // Parse the memory entry into a memoryType, set some defaults so we don't get an unitialized warning.
    mem->frequency.Hz=0;
    mem->frequency.VFO=activeVFO;
    mem->frequency.MHzDouble=0.0;
    mem->frequencyB = mem->frequency;
    mem->duplexOffset = mem->frequency;
    mem->duplexOffsetB = mem->frequency;
    mem->scan = 0xfe;
    memset(mem->UR, 0x0, sizeof(mem->UR));
    memset(mem->URB, 0x0, sizeof(mem->UR));
    memset(mem->R1, 0x0, sizeof(mem->R1));
    memset(mem->R1B, 0x0, sizeof(mem->R1B));
    memset(mem->R2, 0x0, sizeof(mem->R2));
    memset(mem->R2B, 0x0, sizeof(mem->R2B));
    memset(mem->name, 0x0, sizeof(mem->name));
    // We need to add 2 characters so that the parser works!
    payloadIn.insert(0,"**");
    for (auto &parse: *memParser) {
        // non-existant memory is short so send what we have so far.
        if (payloadIn.size() < (parse.pos+1+parse.len) && parse.spec != 'Z') {
            return true;
        }
        QByteArray data = payloadIn.mid(parse.pos+1,parse.len);
        //qInfo(logRig()) << "Parse:" << data.toHex() << "pos" << parse.pos;
        switch (parse.spec)
        {
        case 'a':
            if (parse.len == 1) {
                mem->group = bcdHexToUChar(data[0]);
            }
            else
            {
                mem->group = bcdHexToUChar(data[0],data[1]);
            }
            break;
        case 'b':
            mem->channel = bcdHexToUChar(data[0],data[1]);
            break;
        case 'c':
            mem->scan = data[0];
            break;
        case 'C':
            mem->skip = data[0] >> 4 & 0xf;
            mem->scan = data[0] & 0xf;
            break;
        case 'd': // combined split and scan
            mem->split = quint8(data[0] >> 4 & 0x0f);
            mem->scan = quint8(data[0] & 0x0f);
            break;
        case 'D': // duplex only (used for IC-R8600)
            mem->duplex = quint8(data[0] & 0x0f);
            break;
        case 'e':
            mem->vfo=data[0];
            break;
        case 'E':
            mem->vfoB=data[0];
            break;
        case 'f':
            mem->frequency.Hz = parseFreqDataToInt(data);
            break;
        case 'F':
            mem->frequencyB.Hz = parseFreqDataToInt(data);
            break;
        case 'g':
            mem->mode=bcdHexToUChar(data[0]);
            break;
        case 'G':
            mem->modeB=bcdHexToUChar(data[0]);
            break;
        case 'h':
            mem->filter=bcdHexToUChar(data[0]);
            break;
        case 'H':
            mem->filterB=bcdHexToUChar(data[0]);
            break;
        case 'i': // single datamode
            mem->datamode=bcdHexToUChar(data[0]);
            break;
        case 'I': // single datamode
            mem->datamodeB=bcdHexToUChar(data[0]);
            break;
        case 'j': // combined duplex and tonemode
            mem->duplex=duplexMode_t(quint8(data[0] >> 4 & 0x0f));
            mem->tonemode=quint8(quint8(data[0] & 0x0f));
            break;
        case 'J': // combined duplex and tonemodeB
            mem->duplexB=duplexMode_t((data[0] >> 4 & 0x0f));
            mem->tonemodeB=data[0] & 0x0f;
            break;
        case 'k': // combined datamode and tonemode
            mem->datamode=(quint8(data[0] >> 4 & 0x0f));
            mem->tonemode=data[0] & 0x0f;
            break;
        case 'K': // combined datamode and tonemode
            mem->datamodeB=(quint8(data[0] >> 4 & 0x0f));
            mem->tonemodeB=data[0] & 0x0f;
            break;
        case 'l': // tonemode
            mem->tonemode=data[0] & 0x0f;
            break;
        case 'L': // tonemode
            mem->tonemodeB=data[0] & 0x0f;
            break;
        case 'm':
            mem->dsql = (quint8(data[0] >> 4 & 0x0f));
            break;
        case 'M':
            mem->dsqlB = (quint8(data[0] >> 4 & 0x0f));
            break;
        case 'n':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone ==  bcdHexToUInt(data[1],data[2]))
                    mem->tone = tn.name;
            break;
        case 'N':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone ==  bcdHexToUInt(data[1],data[2]))
                    mem->toneB = tn.name;
            break;
        case 'o':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone ==  bcdHexToUInt(data[1],data[2]))
                    mem->tsql = tn.name;
            break;
        case 'O':
            for (const auto &tn: rigCaps.ctcss)
                if (tn.tone ==  bcdHexToUInt(data[1],data[2]))
                    mem->tsqlB = tn.name;
            break;
        case 'p':
            mem->dtcsp = (quint8(data[0] >> 3 & 0x02) | quint8(data[0] & 0x01));
            break;
        case 'P':
            mem->dtcspB = (quint8(data[0] >> 3 & 0x10) | quint8(data[0] & 0x01));
            break;
        case 'q':
            mem->dtcs = bcdHexToUInt(data[0],data[1]);
            break;
        case 'Q':
            mem->dtcsB = bcdHexToUInt(data[0],data[1]);
            break;
        case 'r':
            mem->dvsql = bcdHexToUChar(data[0]);
            break;
        case 'R':
            mem->dvsqlB = bcdHexToUChar(data[0]);
            break;
        case 's':
            mem->duplexOffset.Hz = parseFreqDataToInt(data);
            break;
        case 'S':
            mem->duplexOffsetB.Hz = parseFreqDataToInt(data);
            break;
        case 't':
            memcpy(mem->UR,data.data(),qMin(int(sizeof mem->UR),data.size()));
            break;
        case 'T':
            memcpy(mem->URB,data.data(),qMin(int(sizeof mem->URB),data.size()));
            break;
        case 'u':
            memcpy(mem->R1,data.data(),qMin(int(sizeof mem->R1),data.size()));
            break;
        case 'U':
            memcpy(mem->R1B,data.data(),qMin(int(sizeof mem->R1B),data.size()));
            break;
        case 'v':
            memcpy(mem->R2,data.data(),qMin(int(sizeof mem->R2),data.size()));
            break;            
        case 'V':
            memcpy(mem->R2B,data.data(),qMin(int(sizeof mem->R2B),data.size()));
            break;
        case 'w': // Tuning step
            if (bool(data[0])) { // Only set if enabled.
                mem->tuningStep = bcdHexToUChar(data[1]);
                mem->progTs = bcdHexToUInt(data[2],data[3]);
            } else {
                mem->tuningStep = 0;
                mem->progTs = 5;
            }
            break;
        case 'x': // Attenuator & Preamp
            mem->atten = bcdHexToUChar(data[0]);
            mem->preamp = bcdHexToUChar(data[1]);
            break;
        case 'y': // Antenna
            mem->antenna = bcdHexToUChar(data[0]);
            break;
        case '+': // IP Plus
            mem->ipplus=bool(data[0] & 0x0f);
            break;
        case 'z':
            if (mem->scan == 0xfe)
                mem->scan = 0;
            memcpy(mem->name,data.data(),qMin(int(sizeof mem->name),data.size()));
            break;
        case 'Z': // Special mode dependant features (I have no idea how to make these work!)
            for (const auto &m: rigCaps.modes)
            {
                if (m.reg == mem->mode)
                {
                    // This mode is the one we are interested in!
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
                    switch (m.mk)
                    {
                    case modeFM:
                        mem->tonemode=data[0] & 0x0f;
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.tone ==  bcdHexToUInt(data[2],data[3]))
                                mem->tsql = tn.name;
                        mem->dtcsp = quint8(data[4] & 0x0f);
                        mem->dtcs = bcdHexToUInt(data[5],data[6]);
                        break;
                    case modeDV:
                        mem->dsql = (quint8(data[0] & 0x0f));
                        mem->dvsql = bcdHexToUChar(data[1]);
                        break;
                    case modeP25:
                        mem->p25Sql = bool(data[0]&0x01);
                        mem->p25Nac = quint16((quint16(data[1] & 0x0f) << 8) | (quint16(data[2]&0x0f) << 4) | quint16(data[3]&0x0f));
                        break;
                    case modedPMR:
                        mem->dPmrSql = bcdHexToUChar(data[0]);
                        mem->dPmrComid = bcdHexToUInt(data[1],data[2]);
                        mem->dPmrCc = bcdHexToUChar(data[3]);
                        mem->dPmrSCRM = bool(data[4]&0x01);
                        mem->dPmrKey = bcdHexToUInt(data[5],data[6],data[7]);
                        break;
                    case modeNXDN_N:
                    case modeNXDN_VN:
                        mem->nxdnSql = bool(data[0]&0x01);
                        mem->nxdnRan = bcdHexToUChar(data[1]);
                        mem->nxdnEnc = bool(data[2]&0x01);
                        mem->nxdnKey = bcdHexToUInt(data[3],data[4],data[5]);
                        break;
                    case modeDCR:
                        mem->dcrSql = bool(data[0]&0x01);
                        mem->dcrUc = bcdHexToUInt(data[1],data[2]);
                        mem->dcrEnc = bool(data[3]&0x01);
                        mem->dcrKey = bcdHexToUInt(data[4],data[5],data[6]);
                        break;
                    default:
                        break;
                    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif


                    break;
                }
            }

            break;
        default:
            qInfo() << "Parser didn't match!" << "spec:" << parse.spec << "pos:" << parse.pos << "len" << parse.len;
            break;
        }
    }

    return true;
}

void icomCommander::setRigID(quint16 rigID)
{
    // This function overrides radio model detection.
    // It can be used for radios without Rig ID commands,
    // or to force a specific radio model

    qInfo(logRig()).noquote() << QString("Setting rig ID to: 0x%0").arg(rigID & 0xff,1,16);

    lookingForRig = true;
    foundRig = false;

    // needed because this is a fake message and thus the value is uninitialized
    // this->civAddr comes from how icomCommander is setup and should be accurate.
    this->incomingCIVAddr = this->civAddr & 0xff;

    if (rigList.contains(rigID)) this->model = rigID & 0xff;
    rigCaps.modelID = rigID & 0xff;
    rigCaps.model = this->model;
    determineRigCaps();
    this->civAddr = incomingCIVAddr & 0xff; // Override and use immediately.
    payloadPrefix = QByteArray("\xFE\xFE");
    payloadPrefix.append((char)civAddr);
    payloadPrefix.append((char)compCivAddr);
}


uchar icomCommander::makeFilterWidth(ushort pass,uchar receiver)
{
    quint8 calc;
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);
    modeInfo mi = queue->getCache(t.modeFunc,receiver).value.value<modeInfo>();

    if (mi.mk == modeAM) { // AM 0-49

        calc = quint16((pass / 200) - 1);
        if (calc > 49)
            calc = 49;
    }
    else if (pass >= 600) // SSB/CW/PSK 10-40 (10-31 for RTTY)
    {
        calc = quint16((pass / 100) + 4);
        if (((calc > 31) && (mi.mk == modeRTTY || mi.mk == modeRTTY_R)))
        {
            calc = 31;
        }
        else if (calc > 40) {
            calc = 40;
        }
    }
    else {  // SSB etc 0-9
        calc = quint16((pass / 50) - 1);
    }

    char tens = (calc / 10);
    char units = (calc - (10 * tens));

    char b1 = (units) | (tens << 4);

    //qDebug(logRig()) << "Requested filter width" << pass << "sending value" << uchar(b1);
    return b1;
}

unsigned char icomCommander::convertNumberToHex(unsigned char num)
{
    // Two digit only
    if(num > 99)
    {
        qInfo(logRig()) << "Invalid numeric conversion from num " << num << " to hex.";
        return 0xFA;
    }
    unsigned char result = 0;
    result =  (num/10) << 4;
    result |= (num - 10*(num/10));
    return result;
}

void icomCommander::receiveCommand(funcs func, QVariant value, uchar receiver)
{
    //qInfo() << "Got command:" << funcString[func];
    int val=INT_MIN;
    if (value.isValid() && value.canConvert<int>() && func != funcSendCW) {
        // Used to validate payload, otherwise ignore.
        val = value.value<int>();
        //qInfo(logRig()) << "Got value" << QString(value.typeName());
        if (func == funcMemoryContents || func == funcMemoryClear || func == funcMemoryWrite || func == funcMemoryMode)
        {
            // Strip out group number from memory for validation purposes.
            qDebug(logRig()) << "Memory Command" << funcString[func] << "with valuetype "  << QString(value.typeName());
            val = val & 0xffff;
        }
    }

    // Need to work out what to do with older dual-VFO rigs.
    /*
    if ((func == funcMainFreq || func == funcSubFreq) && !rigCaps.commands.contains(func))
    {
        if (value.isValid())
            func = funcFreqSet;
        else
            func = funcFreqGet;
    } else if ((func == funcMainMode || func == funcSubMode) && !rigCaps.commands.contains(func))
    {
        if (value.isValid())
            func = funcModeSet;
        else
            func = funcModeGet;
    } else
    */
    if (func == funcSelectVFO) {
        // Special command
        vfo_t v = value.value<vfo_t>();
        func = (v == vfoA)?funcVFOASelect:(v == vfoB)?funcVFOBSelect:(v == vfoMain)?funcVFOMainSelect:(v == vfoSub)?funcVFOSubSelect:(v == vfoMem)?funcMemoryMode:funcNone;
        value.clear();
        val = INT_MIN;
    }

    QByteArray payload;
    funcType cmd;
    cmd = getCommand(func,payload,val,receiver);
    if (cmd.cmd != funcNone)
    {
        // Certain commands need the receiver number adding first!
        switch (cmd.cmd) {
        case funcFreq: case funcMode: case funcScopeMode: case funcScopeSpan: case funcScopeRef: case funcScopeHold:
        case funcScopeSpeed: case funcScopeRBW: case funcScopeVBW: case funcScopeCenterType: case funcScopeEdge:
            payload.append(receiver);
            break;
        default:
            break;
        }

        if (value.isValid())
        {

            if (!cmd.setCmd) {
                qDebug(logRig()) << "Removing unsupported set command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            }

            if (!isRadioAdmin && cmd.admin) {
                qWarning(logRig()) << "Admin permission required for set command" << funcString[func] << "access denied";
                return;
            }

            if (func == funcFreqSet) {
                queue->addUnique(priorityImmediate,funcFreqGet,false,receiver);
            } else if (func == funcModeSet) {
                queue->addUnique(priorityImmediate,funcModeGet,false,receiver);
            } else if (cmd.getCmd && func != funcScopeFixedEdgeFreq && func != funcSpeech &&
                       func != funcBandStackReg && func != funcMemoryContents && func != funcSatelliteMemory && func != funcSendCW) {
                // This was a set command, so queue a get to retrieve the updated value
                queue->addUnique(priorityImmediate,func,false,receiver);
            }


            if (!strcmp(value.typeName(),"bool"))
            {
                payload.append(value.value<bool>());
            }
            else if (!strcmp(value.typeName(),"QString"))
            {
                QString text = value.value<QString>();
                if (func == funcSendCW)
                {
                    QByteArray textData = text.toLocal8Bit();
                    quint8 p=0;
                    qDebug(logRig()) << "CW input:" << textData ;
                    for(int c=0; c < textData.length(); c++)
                    {
                        p = textData.at(c);
                        if( ( (p >= 0x30) && (p <= 0x39) ) ||
                            ( (p >= 0x41) && (p <= 0x5A) ) ||
                            ( (p >= 0x61) && (p <= 0x7A) ) ||
                            (p==0x2F) || (p==0x3F) || (p==0x2E) ||
                            (p==0x2D) || (p==0x2C) || (p==0x3A) ||
                            (p==0x27) || (p==0x28) || (p==0x29) ||
                            (p==0x3D) || (p==0x2B) || (p==0x22) ||
                            (p==0x40) || (p==0x20))
                        {
                            // Allowed character, continue
                        } else {
                            qWarning(logRig()) << "Invalid character detected in CW message at position " << c << ", the character is " << text.at(c);
                            textData[c] = 0x3F; // "?"
                        }
                    }
                    if (textData.isEmpty())
                    {
                        emit stopsidetone();
                        payload.append(uchar(0xff));
                    } else {
                        emit sidetone(QString(textData));
                        payload.append(textData);
                        qDebug(logRig()) << "CW output::" << textData ;
                    }
                    qDebug(logRig()) << "Sending CW: payload:" << payload.toHex(' ');
                }
            }
            else if (!strcmp(value.typeName(),"uchar"))
            {
                if (func == funcRoofingFilter || func == funcFilterShape)
                {
                    // Remove the filter number as Icom doesn't support setting anything other than the current filter.
                    payload.append(bcdEncodeChar(value.value<uchar>() % 10));
                }
                else
                {
                    payload.append(bcdEncodeChar(value.value<uchar>()));
                }
           }
            else if (!strcmp(value.typeName(),"ushort"))
            {
                 if (func == funcFilterWidth) {
                    payload.append(makeFilterWidth(value.value<ushort>(),receiver));
                    //qInfo() << "Setting filter width" << value.value<ushort>() << "VFO" << receiver << "hex" << payload.toHex();

                }
                else if (func == funcKeySpeed){
                    ushort wpm = round((value.value<ushort>()-6) * (6.071));
                    qDebug(logRig()) << "Sending key speed orig:" << value.value<ushort>() << "sent:" << wpm;
                    payload.append(bcdEncodeInt(wpm));
                }
                else if (func == funcCwPitch) {
                    ushort pitch = 0;
                    pitch = ceil((value.value<ushort>() - 300) * (255.0 / 600.0));
                    payload.append(bcdEncodeInt(pitch));
                }
                else {
                    payload.append(bcdEncodeInt(value.value<ushort>()));
                }
            }
            else if (!strcmp(value.typeName(),"short") && (func == funcRitFreq || func == funcXitFreq))
            {
                // Currently only used for RIT (I think)
                bool isNegative = false;
                short val = value.value<short>();
                qDebug() << "Setting RIT to " << val;
                if(val < 0)
                {
                    isNegative = true;
                    val *= -1;
                }
                freqt f;
                QByteArray freqBytes;
                f.Hz = val;
                freqBytes = makeFreqPayload(f);
                freqBytes.truncate(2);
                payload.append(freqBytes);
                payload.append(QByteArray(1,(char)isNegative));
            }
            else if (!strcmp(value.typeName(),"uint") && (func == funcMemoryContents || func == funcMemoryMode))
            {
                qDebug(logRig()) << "Get Memory Contents" << (value.value<uint>() & 0xffff);
                qDebug(logRig()) << "Get Memory Group (if exists)" << (value.value<uint>() >> 16 & 0xffff);
                // Format is different for all radios!
                if (func == funcMemoryContents) {
                    for (auto &parse: rigCaps.memParser) {
                        // If "a" exists, break out of the loop as soon as we have the value.
                        if (parse.spec == 'a') {
                            if (parse.len == 1) {
                                payload.append(bcdEncodeChar(quint16(value.value<uint>() >> 16 & 0xff)));
                            }
                            else if (parse.len == 2)
                            {
                                payload.append(bcdEncodeInt(quint16(value.value<uint>() >> 16 & 0xffff)));
                            }
                            break;
                        }
                    }
                }
                payload.append(bcdEncodeInt(quint16(value.value<uint>() & 0xffff)));
            }
            else if (!strcmp(value.typeName(),"memoryType")) {
                // We need to iterate through memParser to build the correct format
                bool finished=false;
                char nul = 0x0;
                uchar ffchar = 0xff;
                QVector<memParserFormat> parser;
                memoryType mem = value.value<memoryType>();
                if (mem.sat)
                {
                    parser = rigCaps.satParser;
                }
                else
                {
                    parser = rigCaps.memParser;
                }

                // Format is different for all radios!
                for (auto &parse: parser) {
                    switch (parse.spec)
                    {
                    case 'a':
                        if (parse.len == 1) {
                            payload.append(mem.group);
                        }
                        else if (parse.len == 2)
                        {
                            payload.append(bcdEncodeInt(mem.group));
                        }
                        break;
                    case 'b':
                        payload.append(bcdEncodeInt(mem.channel));
                        break;
                    case 'c':
                        // Are we deleting the memory?
                        if (mem.del) {
                            payload.append(ffchar);
                            finished=true;
                            break;
                        } else {
                            payload.append(mem.scan);
                        }
                        break;
                    case 'C':
                        // Are we deleting the memory?
                        if (mem.del) {
                            payload.append(ffchar);
                            finished=true;
                            break;
                        } else {
                            payload.append(mem.scan);
                        }
                        break;
                    case 'd': // combined split and scan
                        if (mem.del) {
                            payload.append(ffchar);
                            finished=true;
                            break;
                        } else {
                            payload.append(quint8((mem.split << 4 & 0xf0) | (mem.scan & 0x0f)));
                        }
                        break;
                    case 'D': // Duplex only
                        payload.append(mem.duplex);
                        break;
                    case 'e':
                        payload.append(mem.vfo);
                        break;
                    case 'E':
                        payload.append(mem.vfoB);
                        break;
                    case 'f':
                        if (mem.del) {
                            qDebug() << "Pre deleting f" << payload.toHex(' ');
                            payload.append(ffchar);
                            qDebug() << "Deleting f" << payload.toHex(' ');
                            finished=true;
                            break;
                        } else {
                            // IC905 memories use 6 byte freq (despite the docs saying 5)
                            // This workaround will add any missing bytes.
                            QByteArray f = makeFreqPayload(mem.frequency);
                            for (int i=f.size(); i<parse.len;i++) {
                                f.append(nul);
                            }
                            payload.append(f);
                        }
                        break;
                    case 'F':
                    {
                        QByteArray f = makeFreqPayload(mem.frequencyB);
                        for (int i=f.size(); i<parse.len;i++) {
                            f.append(nul);
                        }
                        payload.append(f);
                        break;
                    }
                    case 'g':
                        payload.append(bcdEncodeChar(mem.mode));
                        break;
                    case 'G':
                        payload.append(bcdEncodeChar(mem.modeB));
                        break;
                    case 'h':
                        payload.append(bcdEncodeChar(mem.filter));
                        break;
                    case 'H':
                        payload.append(bcdEncodeChar(mem.filterB));
                        break;
                    case 'i': // single datamode
                        payload.append(bcdEncodeChar(mem.datamode));
                        break;
                    case 'I':
                        payload.append(bcdEncodeChar(mem.datamodeB));
                        break;
                    case 'j': // combined duplex and tonemode
                        payload.append((mem.duplex << 4) | mem.tonemode);
                        break;
                    case 'J': // combined duplex and tonemode
                        payload.append((mem.duplexB << 4) | mem.tonemodeB);
                        break;
                    case 'k': // combined datamode and tonemode
                        payload.append((mem.datamode << 4 & 0xf0) | (mem.tonemode & 0x0f));
                        break;
                    case 'K': // combined datamode and tonemode
                        payload.append((mem.datamodeB << 4 & 0xf0) | (mem.tonemodeB & 0x0f));
                        break;
                    case 'l': // tonemode
                        payload.append(mem.tonemode);
                        break;
                    case 'L':
                        payload.append(mem.tonemodeB);
                        break;
                    case 'm':
                        payload.append(mem.dsql << 4);
                        break;
                    case 'M':
                        payload.append(mem.dsqlB << 4);
                        break;
                    case 'n':
                        payload.append(nul);
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.tone)
                                payload.append(bcdEncodeInt(tn.tone));
                        break;
                        break;
                    case 'N':
                        payload.append(nul);
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.toneB)
                                payload.append(bcdEncodeInt(tn.tone));
                        break;
                    case 'o':
                        payload.append(nul);
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.tsql)
                                payload.append(bcdEncodeInt(tn.tone));
                        break;
                    case 'O':
                        payload.append(nul);
                        for (const auto &tn: rigCaps.ctcss)
                            if (tn.name == mem.tsqlB)
                                payload.append(bcdEncodeInt(tn.tone));
                        break;
                    case 'p':
                        payload.append((mem.dtcsp << 3 & 0x10) |  (mem.dtcsp & 0x01));
                        break;
                    case 'P':
                        payload.append((mem.dtcspB << 3 & 0x10) |  (mem.dtcspB & 0x01));
                        break;
                    case 'q':
                        payload.append(bcdEncodeInt(mem.dtcs));
                        break;
                    case 'Q':
                        payload.append(bcdEncodeInt(mem.dtcsB));
                        break;
                    case 'r':
                        payload.append(bcdEncodeChar(mem.dvsql));
                        break;
                    case 'R':
                        payload.append(bcdEncodeChar(mem.dvsqlB));
                        break;
                    case 's':
                        payload.append(makeFreqPayload(mem.duplexOffset).mid(1,parse.len));
                        break;
                    case 'S':
                        payload.append(makeFreqPayload(mem.duplexOffsetB).mid(1,parse.len));
                        break;
                    case 't':
                        payload.append(QByteArray(mem.UR).leftJustified(parse.len,' ',true));
                        break;
                    case 'T':
                        payload.append(QByteArray(mem.URB).leftJustified(parse.len,' ',true));
                        break;
                    case 'u':
                        payload.append(QByteArray(mem.R1).leftJustified(parse.len,' ',true));
                        break;
                    case 'U':
                        payload.append(QByteArray(mem.R1B).leftJustified(parse.len,' ',true));
                        break;
                    case 'v':
                        payload.append(QByteArray(mem.R2).leftJustified(parse.len,' ',true));
                        break;
                    case 'V':
                        payload.append(QByteArray(mem.R2B).leftJustified(parse.len,' ',true));
                        break;
                    case 'w': // Tuning step
                        payload.append(quint8(mem.tuningStep!=0?1:0));
                        payload.append(bcdEncodeChar(qMax(uchar(1),mem.tuningStep))); // 0 is invalid.
                        payload.append(bcdEncodeInt(mem.progTs));
                        break;
                    case 'x': // Attenuator & Preamp
                        payload.append(bcdEncodeChar(mem.atten));
                        payload.append(bcdEncodeChar(mem.preamp));
                        break;
                    case 'y': // Antenna
                        payload.append(bcdEncodeChar(mem.antenna));
                        break;
                    case '+': // IP Plus
                        payload.append(bcdEncodeChar(mem.ipplus));
                        break;
                    case 'z':
                        payload.append(QByteArray(mem.name).leftJustified(parse.len,' ',true));
                        break;
                    case 'Z': // Special mode dependant features (I have no idea how to make these work!)
                        //qInfo() << "Looking for mode:" << mem.mode;
                        for (const auto &m: rigCaps.modes)
                        {
                            if (m.reg == mem.mode)
                            {
                                // This mode is the one we are interested in!
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
                                switch (m.mk)
                                {
                                case modeFM:
                                    if (mem.tonemode) {
                                        payload.append(bcdEncodeChar(mem.tonemode));
                                        payload.append(nul);
                                        for (const auto &tn: rigCaps.ctcss)
                                            if (tn.name == mem.tsql)
                                                payload.append(bcdEncodeInt(tn.tone));
                                        payload.append(bcdEncodeChar(mem.dtcsp));
                                        payload.append(bcdEncodeInt(mem.dtcs));
                                    }
                                    break;
                                case modeDV:
                                    if (mem.dsql) {
                                        payload.append(bcdEncodeChar(mem.dsql?2:0)); // Set must be 2 but radio returns 1?
                                        payload.append(bcdEncodeChar(mem.dvsql));
                                    }
                                    break;
                                case modeP25:
                                    if (mem.p25Sql) {
                                        payload.append(bcdEncodeChar(mem.p25Sql));
                                        payload.append(uchar((mem.p25Nac & 0xf00) >> 8));
                                        payload.append(uchar((mem.p25Nac & 0x0f0) >> 4));
                                        payload.append(uchar(mem.p25Nac & 0x00f));
                                    }
                                    break;
                                case modedPMR:
                                    qInfo() << "Sending dPMR sq:" << mem.dPmrSql << "Com ID:" << mem.dPmrComid << "CC:" << mem.dPmrCc << "SCRM:" << mem.dPmrSCRM << "Key:" << mem.dPmrKey;;
                                    if (mem.dPmrSql || mem.dPmrSCRM) {
                                        payload.append(bcdEncodeChar(mem.dPmrSql));
                                        payload.append(bcdEncodeInt(mem.dPmrComid));
                                        payload.append(bcdEncodeChar(mem.dPmrCc));
                                        payload.append(bcdEncodeChar(mem.dPmrSCRM));
                                        payload.append(bcdEncodeInt(mem.dPmrKey));
                                    }
                                    break;
                                case modeNXDN_N:
                                case modeNXDN_VN:
                                    if (mem.nxdnSql || mem.nxdnEnc) {
                                    payload.append(bcdEncodeChar(mem.nxdnSql));
                                    payload.append(bcdEncodeChar(mem.nxdnRan));
                                    payload.append(bcdEncodeChar(mem.nxdnEnc));
                                    payload.append(bcdEncodeInt(mem.nxdnKey));
                                    }
                                    break;
                                case modeDCR:
                                    if (mem.dcrSql || mem.dcrEnc) {
                                        payload.append(bcdEncodeChar(mem.dcrSql));
                                        payload.append(bcdEncodeInt(mem.dcrUc));
                                        payload.append(bcdEncodeChar(mem.dcrEnc));
                                        payload.append(bcdEncodeInt(mem.dcrKey));
                                    }
                                    break;
                                default:
                                    break;
                                }
                                break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

                            }
                        }
                        break;
                    default:
                        break;
                    }
                    if (finished)
                        break;
                }
                qDebug(logRig()) << "Writing memory location:" << payload.toHex(' ');

            }
            else if (!strcmp(value.typeName(),"int") && (func == funcScopeRef))
            {
                bool isNegative = false;
                int level = value.value<int>();
                if(level < 0)
                {
                    isNegative = true;
                    level *= -1;
                }
                payload.append(bcdEncodeInt(quint16(level*10)));
                payload.append(static_cast<quint8>(isNegative));
            }
            else if (!strcmp(value.typeName(),"modeInfo"))
            {
                {
                    modeInfo m = value.value<modeInfo>();
                    if (func == funcDataModeWithFilter)
                    {
                        payload.append(bcdEncodeChar(m.data));
                        if (m.data != 0)
                           payload.append(m.filter);
                    } else {
                        payload.append(bcdEncodeChar(m.reg));
                        if (func == funcMode|| func == funcSelectedMode || func == funcUnselectedMode)
                           payload.append(m.data);
                        if (!rigCaps.filters.empty() && m.mk != modeWFM)
                            payload.append(m.filter);
                        qDebug(logRig()) << "Sending mode command" << funcString[func] << " mode:" << m.name<< "data:" << m.data << "filter" << m.filter;
                    }
                }
            }
            else if(!strcmp(value.typeName(),"freqt"))
            {
                if (func == funcSendFreqOffset) {
                    payload.append(makeFreqPayload(value.value<freqt>()).mid(1,3));
                } else {
                    payload.append(makeFreqPayload(value.value<freqt>()));
                }
            }
            else if(!strcmp(value.typeName(),"antennaInfo"))
            {
                payload.append(bcdEncodeChar(value.value<antennaInfo>().antenna));
                if (rigCaps.commands.contains(funcRXAntenna))
                    payload.append(value.value<antennaInfo>().rx);
                //qInfo(logRig) << "Sending antenna info" << payload.toHex(' ');
            }
            else if(!strcmp(value.typeName(),"rigInput"))
            {
                payload.append(bcdEncodeChar(value.value<rigInput>().reg));
            }
            else if (!strcmp(value.typeName(),"spectrumBounds"))
            {
                spectrumBounds s = value.value<spectrumBounds>();
                uchar range=1;
                double lastRange=-1.0;
                auto band = rigCaps.bands.cend();
                while (band != rigCaps.bands.cbegin())
                {
                    band--;
                    //qInfo() << "Band" << band->name << "range" << band->range << "start" << s.start;
                    if (band->range > s.start)
                    {
                        break;
                    }
                    else if (lastRange != band->range && band->range != 0.0 && band->range <= s.start)
                    {
                        range++;
                        //qInfo() << "range=" <<range;
                        lastRange=band->range;
                    }
                }
                payload.append(bcdEncodeChar(range));
                payload.append(bcdEncodeChar(s.edge));
                payload.append(makeFreqPayload(s.start));
                payload.append(makeFreqPayload(s.end));
                //qInfo() << "Fixed Edge Bounds" << range << s.edge << s.start << s.end << payload.toHex();

            }
            else if (!strcmp(value.typeName(),"duplexMode_t"))
            {
                payload.append(static_cast<uchar>(value.value<duplexMode_t>()));
            }
            else if (!strcmp(value.typeName(),"centerSpanData"))
            {
                centerSpanData span = value.value<centerSpanData>();
                double freq = double(span.freq/1000000.0);
                payload.append(makeFreqPayload(freq));
            }
            else if (!strcmp(value.typeName(),"toneInfo"))
            {
                toneInfo t = value.value<toneInfo>();
                payload.append(encodeTone(t.tone, t.tinv, t.rinv));
            }
            else if (!strcmp(value.typeName(),"bandStackType"))
            {

                bandStackType bsr = value.value<bandStackType>();
                payload.append(bcdEncodeChar(bsr.band));        //byte 0
                payload.append(bcdEncodeChar(bsr.reg));     //byte 1
                if (bsr.freq.Hz != 0) {
                    // We are setting the bsr so send freq/mode data.
                    // First find which band we are working on.
                    for (const auto &b: rigCaps.bands)
                    {
                        if (b.bsr == bsr.band)
                        {
                            payload.append(makeFreqPayload(bsr.freq,b.bytes));
                            payload.append(bcdEncodeChar(bsr.mode));
                            payload.append(bcdEncodeChar(bsr.filter));
                            payload.append((bsr.data << 4 & 0xf0) + (bsr.sql & 0x0f));
                            if (bsr.tone.tone != 0) {
                                payload.append(encodeTone(bsr.tone.tone));
                                payload.append(encodeTone(bsr.tsql.tone));
                            }
                            break;
                        }

                    }
                }
                qInfo(logRig()) << "Sending BSR, Band Code:" << bsr.band << "Register Code:" << bsr.reg << "(Sent:" << payload.toHex(' ') << ")";
            }
            else if (!strcmp(value.typeName(),"datekind"))
            {
                datekind d = value.value<datekind>();
                qInfo(logRig()) << QString("Sending new date: (MM-DD-YYYY) %0-%1-%2").arg(d.month).arg(d.day).arg(d.year);
                // YYYYMMDD
                payload.append(convertNumberToHex(d.year/100)); // 20
                payload.append(convertNumberToHex(d.year - 100*(d.year/100))); // 21
                payload.append(convertNumberToHex(d.month));
                payload.append(convertNumberToHex(d.day));

            }
            else if (!strcmp(value.typeName(),"timekind"))
            {
                timekind t = value.value<timekind>();
                if (cmd.cmd == funcTime) {
                    qInfo(logRig()) << QString("Sending new time: (HH:MM) %0:%1").arg(t.hours).arg(t.minutes);
                    payload.append(convertNumberToHex(t.hours));
                    payload.append(convertNumberToHex(t.minutes));

                } else if (cmd.cmd == funcUTCOffset) {
                    qInfo(logRig()) << QString("Sending new UTC offset: %0%1:%2").arg(t.isMinus?"-":"+").arg(t.hours).arg(t.minutes);
                    payload.append(convertNumberToHex(t.hours));
                    payload.append(convertNumberToHex(t.minutes));
                    payload.append((uchar)t.isMinus);

                }
            }
            else if (!strcmp(value.typeName(),"rptrAccessData"))
            {
                rptrAccessData r = value.value<rptrAccessData>();
                qDebug(logRig()) << "Sending rptrAccessData Mode" << r.accessMode;
                payload.append(bcdEncodeChar(static_cast<uchar>(r.accessMode)));
            }
            else
            {
                qInfo(logRig()) << funcString[func] << "Got unknown value type" << QString(value.typeName());
                return;
            }              
        } else {
            // This is a get command
            if (!cmd.getCmd)
            {
                // Get command not supported
                qDebug(logRig()) << "Removing unsupported get command from queue" << funcString[func] << "VFO" << receiver;
                queue->del(func,receiver);
                return;
            }
            //else if (cmd.cmd == funcVFOModeSelect) {
                //qInfo(logRig()) << "Attempting to select VFO mode:" << payload.toHex(' ');
            //}
        }
        prepDataAndSend(payload);
        lastCommand.func = func;
        lastCommand.data = payload;
        lastCommand.minValue = cmd.minVal;
        lastCommand.maxValue = cmd.maxVal;
        lastCommand.bytes = cmd.bytes;
    }
    else
    {
        qDebug(logRig()) << "cachingQueue(): unimplemented command" << funcString[func];
        queue->del(func,receiver);
    }
}
