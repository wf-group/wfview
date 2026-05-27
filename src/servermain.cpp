#include "servermain.h"

#include "audiopacketcodec.h"
#include "commhandler.h"
#include "rigidentities.h"
#include "logcategories.h"
#include <iostream>

namespace {
QString decodedServerPassword(const QString &stored)
{
    static const quint8 sequence[] =
    {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
        0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
        0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
        0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
        0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };

    QByteArray out;
    const QByteArray encoded = stored.toLocal8Bit();
    for (int i = 0; i < encoded.size() && i < 16; ++i) {
        const int target = quint8(encoded.at(i));
        bool found = false;
        for (int p = 32; p <= 126; ++p) {
            if (sequence[p] == target) {
                int ascii = p - i;
                if (ascii < 32)
                    ascii += 95;
                if (ascii >= 32 && ascii <= 126) {
                    out.append(char(ascii));
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return stored;
    }
    return QString::fromLocal8Bit(out);
}

QList<QByteArray> extractCompleteCivFrames(QByteArray *pending, const QByteArray &data)
{
    pending->append(data);

    QList<QByteArray> complete;
    while (!pending->isEmpty()) {
        int start = -1;
        for (int i = 0; i + 1 < pending->size(); ++i) {
            if (quint8(pending->at(i)) == 0xfe && quint8(pending->at(i + 1)) == 0xfe) {
                start = i;
                break;
            }
        }

        if (start < 0) {
            pending->clear();
            break;
        }

        if (start > 0) {
            pending->remove(0, start);
        }

        const int end = pending->indexOf(char(0xfd), 2);
        if (end < 0) {
            break;
        }

        complete.append(pending->left(end + 1));
        pending->remove(0, end + 1);
    }

    return complete;
}

}

// This code is copyright 2017-2020 Elliott H. Liggett
// All rights reserved

servermain::servermain(const QString settingsFile)
{

    qRegisterMetaType <udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType <rigCapabilities>();
    qRegisterMetaType <rigInput>();
    qRegisterMetaType <freqt>();
    qRegisterMetaType <audioPacket>();
    qRegisterMetaType <audioSetup>();
    qRegisterMetaType <SERVERCONFIG>();
    qRegisterMetaType <timekind>();
    qRegisterMetaType <datekind>();
    qRegisterMetaType<rigstate*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<networkStatus>();
    qRegisterMetaType<IaxStats>();
    qRegisterMetaType<codecType>();
    qRegisterMetaType<errorType>();

    this->setObjectName("wfserver");
    queue = cachingQueue::getInstance(this);
    // wfserver doesn't use the queue, it will send commands direct to the rig interface
    queue->interval(-1);
    // We need to open rig files

    // Make sure we know about any changes to rigCaps
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));

    setDefPrefs();

    getSettingsFilePath(settingsFile);

    loadSettings(); // Look for saved preferences

    setManufacturer(prefs.manufacturer);

    audioDev = new audioDevices(prefs.audioSystem, QFontMetrics(QFont()));
    connect(audioDev, SIGNAL(updated()), this, SLOT(updateAudioDevices()));
    audioDev->enumerate();

    setInitialTiming();

    openRig();

    setServerToPrefs();
    startWfShareStation();

    amTransmitting = false;

}

servermain::~servermain()
{
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {
            radio->rigThread->quit();
            radio->rigThread->wait();
        }
        delete radio; // This has been created by new in loadSettings();
    }
    serverConfig.rigs.clear();
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
    }

    if (wfShareStation != Q_NULLPTR) {
        wfShareStation->disconnectFromServer();
        delete wfShareStation;
        wfShareStation = Q_NULLPTR;
    }
    if (wfShareDirect != Q_NULLPTR) {
        wfShareDirect->disconnectFromServer();
        delete wfShareDirect;
        wfShareDirect = Q_NULLPTR;
    }
    if (wfShareTxFlushTimer != Q_NULLPTR) {
        wfShareTxFlushTimer->stop();
        delete wfShareTxFlushTimer;
        wfShareTxFlushTimer = Q_NULLPTR;
    }
    wfShareTxBuffer.clear();
    wfSharePendingCivToClient.clear();
    wfShareClientConnected = false;
    wfShareClientHasSentCiv = false;
    wfShareRxFrames = 0;
    wfShareTxFrames = 0;

    if (audioDev != Q_NULLPTR) {
        delete audioDev;
    }

    delete settings;

#if defined(PORTAUDIO)
    Pa_Terminate();
#endif

}

void servermain::openRig()
{
    // This function is intended to handle opening a connection to the rig.
    // the connection can be either serial or network,
    // and this function is also responsible for initiating the search for a rig model and capabilities.
    // Any errors, such as unable to open connection or unable to open port, are to be reported to the user.


    makeRig();

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        //qInfo(logSystem()) << "Opening rig";
        if (radio->rigThread != Q_NULLPTR)
        {
            //qInfo(logSystem()) << "Got rig";
            QMetaObject::invokeMethod(radio->rig, [=]() {
                const quint8 waterfallFormat = prefs.manufacturer == manufIcom ? quint8(1) : radio->waterfallFormat;
                radio->rig->serialCommSetup(rigList, radio->civAddr, radio->serialPort, radio->baudRate, QString("none"), 0, waterfallFormat);
            }, Qt::QueuedConnection);
        }
    }
}

void servermain::makeRig()
{
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread == Q_NULLPTR)
        {
            qInfo(logSystem()) << "Creating new rigThread()";
            // We can't currently mix different manufacturers
            switch (prefs.manufacturer){
            case manufIcom:
                radio->rig = new icomCommander(radio->guid);
                break;
            case manufKenwood:
                radio->rig = new kenwoodCommander(radio->guid);
                break;
            case manufYaesu:
                radio->rig = new yaesuCommander(radio->guid);
                break;
            default:
                qCritical() << "Unknown Manufacturer, aborting...";
                break;
            }

            radio->rigThread = new QThread(this);
            radio->rigThread->setObjectName("rigCommander()");

            // Thread:
            radio->rig->moveToThread(radio->rigThread);
            connect(radio->rigThread, SIGNAL(started()), radio->rig, SLOT(process()));
            connect(radio->rigThread, SIGNAL(finished()), radio->rig, SLOT(deleteLater()));
            radio->rigThread->start();
            // Rig status and Errors:
            connect(radio->rig, SIGNAL(havePortError(errorType)), this, SLOT(receivePortError(errorType)));
            connect(radio->rig, SIGNAL(haveStatusUpdate(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));

            // Rig comm setup:
            connect(this, SIGNAL(setPTTType(pttType_t)), radio->rig, SLOT(setPTTType(pttType_t)));

            connect(radio->rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

            connect(this, SIGNAL(sendCloseComm()), radio->rig, SLOT(closeComm()));
            connect(this, SIGNAL(sendChangeLatency(quint16)), radio->rig, SLOT(changeLatency(quint16)));
            //connect(this, SIGNAL(getRigCIV()), radio->rig, SLOT(findRigs()));
            //connect(this, SIGNAL(setRigID(unsigned char)), radio->rig, SLOT(setRigID(unsigned char)));
            connect(radio->rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

            //connect(this, SIGNAL(requestRigState()), radio->rig, SLOT(sendState()));
            //connect(this, SIGNAL(stateUpdated()), radio->rig, SLOT(stateUpdated()));
            //connect(radio->rig, SIGNAL(stateInfo(rigstate*)), this, SLOT(receiveStateInfo(rigstate*)));

            //Other connections
            connect(this, SIGNAL(setCIVAddr(unsigned char)), radio->rig, SLOT(setCIVAddr(unsigned char)));

            //connect(radio->rig, SIGNAL(havePTTStatus(bool)), this, SLOT(receivePTTstatus(bool)));
            //connect(this, SIGNAL(setPTT(bool)), radio->rig, SLOT(setPTT(bool)));
            //connect(this, SIGNAL(getPTT()), radio->rig, SLOT(getPTT()));
            connect(this, SIGNAL(getDebug()), radio->rig, SLOT(getDebug()));
            if (radio->rigThread->isRunning()) {
                qInfo(logSystem()) << "Rig thread is running";
            }
            else {
                qInfo(logSystem()) << "Rig thread is not running";
            }

        }

    }

}

void servermain::removeRig()
{
    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {
            radio->rigThread->disconnect();
            radio->rig->disconnect();
            delete radio->rigThread;
            delete radio->rig;
            radio->rig = Q_NULLPTR;
        }
    }
}

void servermain::receiveStatusUpdate(networkStatus status)
{
    if (status.message != lastMessage) {
        std::cout << status.message.toLocal8Bit().toStdString() << "\n";
        lastMessage = status.message;
    }
}

void servermain::receiveCommReady()
{
    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    // Use the GUID to determine which radio the response is from

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (sender != Q_NULLPTR && radio->rig != Q_NULLPTR && !memcmp(sender->getGUID(), radio->guid, GUIDLEN))
        {

            qInfo(logSystem()) << "Received CommReady!! ";
            if (radio->civAddr == 0)
            {
                // tell rigCommander to broadcast a request for all rig IDs.
                // qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
                if (!radio->rigAvailable) {
                    if (radio->connectTimer == Q_NULLPTR) {
                        radio->connectTimer = new QTimer();
                        connect(radio->connectTimer, &QTimer::timeout, this, std::bind(&servermain::connectToRig, this, radio));
                    }
                    radio->connectTimer->start(500);
                }
            }
            else {
                // don't bother, they told us the CIV they want, stick with it.
                // We still query the rigID to find the model, but at least we know the CIV.
                qInfo(logSystem()) << "Skipping automatic CIV, using user-supplied value of " << radio->civAddr;
                QMetaObject::invokeMethod(radio->rig, [=]() {
                    radio->rig->setRigID(radio->civAddr);
                    }, Qt::QueuedConnection);
            }
        }
    }
}

void servermain::connectToRig(RIGCONFIG* rig)
{
    if (!rig->rigAvailable) {
        qDebug(logSystem()) << "Searching for rig on" << rig->serialPort;
        QMetaObject::invokeMethod(rig->rig, [=]() {
            rig->rig->receiveCommand(funcTransceiverId,QVariant(),0);
        }, Qt::QueuedConnection);

    }
    else {
        rig->connectTimer->stop();
    }
}

void servermain::receiveRigCaps(rigCapabilities* rigCaps)
{

    // Entry point for unknown rig being identified at the start of the program.
    //now we know what the rig ID is:

    rigCommander* sender = qobject_cast<rigCommander*>(QObject::sender());

    // Use the GUID to determine which radio the response is from
    for (RIGCONFIG* radio : serverConfig.rigs)
    {

        if (sender != Q_NULLPTR && radio->rig != Q_NULLPTR && !radio->rigAvailable && !memcmp(sender->getGUID(), radio->guid, GUIDLEN))
        {

            qDebug(logSystem()) << "Rig name: " << rigCaps->modelName;
            qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps->hasLan;
            qDebug(logSystem()) << "Rig ID received into servermain: spectLenMax: " << rigCaps->spectLenMax;
            qDebug(logSystem()) << "Rig ID received into servermain: spectAmpMax: " << rigCaps->spectAmpMax;
            qDebug(logSystem()) << "Rig ID received into servermain: spectSeqMax: " << rigCaps->spectSeqMax;
            qDebug(logSystem()) << "Rig ID received into servermain: hasSpectrum: " << rigCaps->hasSpectrum;
            qDebug(logSystem()).noquote() << QString("Rig ID received into servermain: GUID: {%1%2%3%4-%5%6-%7%8-%9%10-%11%12%13%14%15%16}")
                                                 .arg(rigCaps->guid[0], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[1], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[2], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[3], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[4], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[5], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[6], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[7], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[8], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[9], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[10], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[11], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[12], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[13], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[14], 2, 16, QLatin1Char('0'))
                .arg(rigCaps->guid[15], 2, 16, QLatin1Char('0'))
                ;
            radio->rigCaps = rigCaps;
            // Added so that server receives rig capabilities.
            //emit sendRigCaps(rigCaps);
            ensureWfShareRxAudio(radio);
        }
    }
    return;
}

void servermain::receivePortError(errorType err)
{
    qInfo(logSystem()) << "servermain: received error for device: " << err.device << " with message: " << err.message;
}


void servermain::getSettingsFilePath(QString settingsFile)
{
    if (settingsFile.isNull()) {
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

        qInfo(logSystem()) << "Loading settings from:" << path + file;
        settings = new QSettings(path + file, QSettings::Format::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        settings->setIniCodec("UTF-8");
#endif
    }
}


void servermain::setInitialTiming()
{
    loopTickCounter = 0;
    pttTimer = new QTimer(this);
    pttTimer->setInterval(180*1000); // 3 minute max transmit time in ms
    pttTimer->setSingleShot(true);
    connect(pttTimer, SIGNAL(timeout()), this, SLOT(handlePttLimit()));
}

void servermain::setServerToPrefs()
{
	
    // Start server if enabled in config
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
        serverThread = Q_NULLPTR;
        server = Q_NULLPTR;
    }

    serverThread = new QThread(this);

    switch (prefs.manufacturer)
    {
    case manufIcom:
        server = new icomServer(&serverConfig);
        serverThread->setObjectName("icomServer()");
        break;
    case manufKenwood:
        server = new kenwoodServer(&serverConfig);
        serverThread->setObjectName("kenwoodServer()");
        break;
    case manufYaesu:
        server = new yaesuServer(&serverConfig);
        serverThread->setObjectName("yaesuServer()");
        break;
    default:
        return;

    }


    server->moveToThread(serverThread);


    connect(this, SIGNAL(initServer()), server, SLOT(init()));
    connect(serverThread, SIGNAL(finished()), server, SLOT(deleteLater()));

    // Step through all radios and connect them to the server, 
    // server will then use GUID to determine which actual radio it belongs to.

    for (RIGCONFIG* radio : serverConfig.rigs)
    {
        if (radio->rigThread != Q_NULLPTR)
        {

            if (radio->rig != Q_NULLPTR) {
                connect(radio->rig, SIGNAL(haveAudioData(audioPacket)), server, SLOT(receiveAudioData(audioPacket)));
                connect(radio->rig, SIGNAL(haveDataForServer(QByteArray)), server, SLOT(dataForServer(QByteArray)));
                //connect(server, SIGNAL(haveDataFromServer(QByteArray)), radio->rig, SLOT(dataFromServer(QByteArray)));
                //connect(this, SIGNAL(sendRigCaps(rigCapabilities)), server, SLOT(receiveRigCaps(rigCapabilities)));
            }
        }
    }

    connect(server, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));

    serverThread->start();

    emit initServer();
}

void servermain::ensureWfShareRxAudio(RIGCONFIG *radio)
{
    if ((!prefs.wfShareEnabled && !prefs.wfShareDirectEnabled) || radio == Q_NULLPTR) {
        return;
    }

    if (radio->rxaudio == Q_NULLPTR) {
        radio->rxAudioSetup.codec = 0x40;
        radio->rxAudioSetup.sampleRate = 16000;
        radio->rxAudioSetup.latency = 150;
        radio->rxAudioSetup.isinput = true;
        memcpy(radio->rxAudioSetup.guid, radio->guid, GUIDLEN);

        qInfo(logAudio()) << "Starting wfshare RX audio input"
                          << "device" << radio->rxAudioSetup.name
                          << "codec" << radio->rxAudioSetup.codec
                          << "sampleRate" << radio->rxAudioSetup.sampleRate;

        if (radio->rxAudioSetup.type == qtAudio) {
            radio->rxaudio = new audioHandlerQtInput();
        } else if (radio->rxAudioSetup.type == portAudio) {
            radio->rxaudio = new audioHandlerPaInput();
        } else if (radio->rxAudioSetup.type == rtAudio) {
            radio->rxaudio = new audioHandlerRtInput();
        } else {
            qCritical(logAudio()) << "Unsupported wfshare receive audio handler selected" << radio->rxAudioSetup.type;
            return;
        }

        radio->rxAudioThread = new QThread(this);
        radio->rxAudioThread->setObjectName(QStringLiteral("wfshareRxAudio()"));
        radio->rxaudio->moveToThread(radio->rxAudioThread);
        radio->rxAudioThread->start(QThread::TimeCriticalPriority);
        connect(radio->rxAudioThread, SIGNAL(finished()), radio->rxaudio, SLOT(deleteLater()));

#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
        QMetaObject::invokeMethod(radio->rxaudio, [radio]() {
            radio->rxaudio->init(radio->rxAudioSetup);
        }, Qt::QueuedConnection);
#else
        QMetaObject::invokeMethod(radio->rxaudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, radio->rxAudioSetup));
#endif
    }

    if (server != Q_NULLPTR) {
        connect(radio->rxaudio, SIGNAL(haveAudioData(audioPacket)), server, SLOT(receiveAudioData(audioPacket)), Qt::UniqueConnection);
    }
    connect(radio->rxaudio, &audioHandlerBase::haveAudioData, this, &servermain::sendWfShareAudio, Qt::UniqueConnection);
}

void servermain::sendWfShareAudio(const audioPacket &packet)
{
    if (!wfShareClientConnected || !wfShareClientHasSentCiv || wfShareStation == Q_NULLPTR) {
        return;
    }

    radioTransportFrame::Frame frame;
    frame.channel = RadioTransportChannel::AudioRx;
    frame.payload = audioPacketCodec::encode(packet);
    wfShareTxFrames++;
    if (wfShareTxFrames <= 10 || wfShareTxFrames % 250 == 0) {
        qDebug(logSystem()).noquote()
            << QStringLiteral("wfshare station sent RX audio frame %1 bytes %2")
                   .arg(wfShareTxFrames)
                   .arg(frame.payload.size());
    }
    wfShareStation->sendPayload(radioTransportFrame::encode(frame), false);
}

void servermain::ensureWfShareTxAudio(RIGCONFIG *radio)
{
    if ((!prefs.wfShareEnabled && !prefs.wfShareDirectEnabled) || radio == Q_NULLPTR) {
        return;
    }

    if (radio->rigCaps == Q_NULLPTR || !radio->rigCaps->hasTransmit) {
        return;
    }

    if (radio->txaudio != Q_NULLPTR) {
        return;
    }

    radio->txAudioSetup.codec = 0x40;
    radio->txAudioSetup.sampleRate = 16000;
    radio->txAudioSetup.latency = radio->txAudioSetup.latency == 0 ? quint16(150) : radio->txAudioSetup.latency;
    radio->txAudioSetup.isinput = false;
    memcpy(radio->txAudioSetup.guid, radio->guid, GUIDLEN);

    qInfo(logAudio()) << "Starting wfshare TX audio output"
                      << "device" << radio->txAudioSetup.name
                      << "codec" << radio->txAudioSetup.codec
                      << "sampleRate" << radio->txAudioSetup.sampleRate;

    if (radio->txAudioSetup.type == qtAudio) {
        radio->txaudio = new audioHandlerQtOutput();
    } else if (radio->txAudioSetup.type == portAudio) {
        radio->txaudio = new audioHandlerPaOutput();
    } else if (radio->txAudioSetup.type == rtAudio) {
        radio->txaudio = new audioHandlerRtOutput();
    } else {
        qCritical(logAudio()) << "Unsupported wfshare transmit audio handler selected" << radio->txAudioSetup.type;
        return;
    }

    radio->txAudioThread = new QThread(this);
    radio->txAudioThread->setObjectName(QStringLiteral("wfshareTxAudio()"));
    radio->txaudio->moveToThread(radio->txAudioThread);
    radio->txAudioThread->start(QThread::TimeCriticalPriority);
    connect(radio->txAudioThread, SIGNAL(finished()), radio->txaudio, SLOT(deleteLater()));

#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    QMetaObject::invokeMethod(radio->txaudio, [radio]() {
        radio->txaudio->init(radio->txAudioSetup);
    }, Qt::QueuedConnection);
#else
    QMetaObject::invokeMethod(radio->txaudio, "init", Qt::QueuedConnection, Q_ARG(audioSetup, radio->txAudioSetup));
#endif
}

void servermain::startWfShareStation()
{
    if (wfShareStation != Q_NULLPTR) {
        wfShareStation->disconnectFromServer();
        delete wfShareStation;
        wfShareStation = Q_NULLPTR;
    }
    if (wfShareTxFlushTimer != Q_NULLPTR) {
        wfShareTxFlushTimer->stop();
        delete wfShareTxFlushTimer;
        wfShareTxFlushTimer = Q_NULLPTR;
    }
    wfShareTxBuffer.clear();
    wfSharePendingCivToClient.clear();
    wfShareClientHasSentCiv = false;

    if (!prefs.wfShareEnabled && !prefs.wfShareDirectEnabled) {
        return;
    }

    wfShareStation = new IaxClientSession(this);
    wfShareTxFlushTimer = new QTimer(this);
    wfShareTxFlushTimer->setSingleShot(true);
    wfShareTxFlushTimer->setInterval(10);
    connect(wfShareTxFlushTimer, &QTimer::timeout, this, &servermain::flushWfShareTxBuffer);

    connect(wfShareStation, &IaxClientSession::debugMessage, this, [](const QString &message) {
        if (message.contains(QStringLiteral("incoming wfshare call accepted")) ||
            message.contains(QStringLiteral("direct listener bound"))) {
            qInfo(logSystem()).noquote() << QStringLiteral("wfshare station: %1").arg(message);
        } else {
            qDebug(logSystem()).noquote() << QStringLiteral("wfshare station: %1").arg(message);
        }
    });
    connect(wfShareStation, &IaxClientSession::errorOccurred, this, [](const QString &message) {
        qWarning(logSystem()).noquote() << QStringLiteral("wfshare station failed: %1").arg(message);
    });
    connect(wfShareStation, &IaxClientSession::registrationChanged, this, [](bool registered) {
        qInfo(logSystem()) << (registered ? "wfshare station registered" : "wfshare station unregistered");
    });
    connect(wfShareStation, &IaxClientSession::connectedChanged, this, [this](bool connected) {
        wfShareClientConnected = connected;
        if (!connected) {
            wfShareClientHasSentCiv = false;
            wfShareTxBuffer.clear();
            wfSharePendingCivToClient.clear();
        }
        qInfo(logSystem()) << (connected ? "wfshare station call connected" : "wfshare station call disconnected");
    });
    connect(wfShareStation, &IaxClientSession::payloadReceived, this, [this](const QByteArray &payload) {
        radioTransportFrame::Frame frame;
        if (radioTransportFrame::decode(payload, &frame) != radioTransportFrame::DecodeOk) {
            qWarning(logSystem()) << "wfshare station received invalid transport frame";
            return;
        }

        if (frame.channel == RadioTransportChannel::AudioTx) {
            audioPacket packet;
            if (!audioPacketCodec::decode(frame.payload, &packet)) {
                qWarning(logSystem()) << "wfshare station received invalid TX audio packet";
                return;
            }

            if (serverConfig.rigs.isEmpty()) {
                qWarning(logSystem()) << "wfshare station received TX audio but no rig is available";
                return;
            }

            RIGCONFIG *radio = serverConfig.rigs.first();
            for (RIGCONFIG *candidate : serverConfig.rigs) {
                if (candidate != Q_NULLPTR && !memcmp(candidate->guid, packet.guid, GUIDLEN)) {
                    radio = candidate;
                    break;
                }
            }

            ensureWfShareTxAudio(radio);
            if (radio->txaudio != Q_NULLPTR) {
                QMetaObject::invokeMethod(radio->txaudio, "incomingAudio",
                                          Qt::QueuedConnection, Q_ARG(audioPacket, packet));
            }
            return;
        }

        if (frame.channel != RadioTransportChannel::Civ) {
            qDebug(logSystem()) << "wfshare station ignored unsupported channel" << int(frame.channel);
            return;
        }

        wfShareRxFrames++;
        wfShareClientHasSentCiv = true;
        if (wfShareRxFrames <= 10 || wfShareRxFrames % 100 == 0) {
            qDebug(logSystem()).noquote()
                << QStringLiteral("wfshare station received CI-V frame %1 bytes %2")
                       .arg(wfShareRxFrames)
                       .arg(frame.payload.size());
        }

        if (serverConfig.rigs.isEmpty() || serverConfig.rigs.first()->rig == Q_NULLPTR) {
            qWarning(logSystem()) << "wfshare station received CI-V data but no rig is available";
            return;
        }

        RIGCONFIG *radio = serverConfig.rigs.first();
        QMetaObject::invokeMethod(radio->rig, [radio, data = frame.payload]() {
            radio->rig->dataFromServer(data);
        }, Qt::QueuedConnection);
    });

    for (RIGCONFIG *radio : serverConfig.rigs) {
        if (radio->rig == Q_NULLPTR) {
            continue;
        }

        connect(radio->rig, &rigCommander::haveDataForServer, wfShareStation, [this](QByteArray data) {
            if (!wfShareClientConnected || !wfShareClientHasSentCiv || wfShareStation == Q_NULLPTR) {
                return;
            }

            const QList<QByteArray> completeFrames = extractCompleteCivFrames(&wfSharePendingCivToClient, data);
            if (completeFrames.isEmpty()) {
                return;
            }

            for (const QByteArray &frameData : completeFrames) {
                radioTransportFrame::Frame frame;
                frame.channel = frameData.contains(QByteArrayLiteral("\x27\x00"))
                                    ? RadioTransportChannel::Scope
                                    : RadioTransportChannel::Civ;
                frame.payload = frameData;
                wfShareTxFrames++;
                if (wfShareTxFrames <= 10 || wfShareTxFrames % 100 == 0) {
                    qDebug(logSystem()).noquote()
                        << QStringLiteral("wfshare station sent %1 frame %2 bytes %3")
                               .arg(frame.channel == RadioTransportChannel::Scope ? QStringLiteral("scope") : QStringLiteral("CI-V"))
                               .arg(wfShareTxFrames)
                               .arg(frame.payload.size());
                }
                wfShareStation->sendPayload(radioTransportFrame::encode(frame),
                                            frame.channel == RadioTransportChannel::Civ);
            }
        });

        ensureWfShareRxAudio(radio);
    }

    if (prefs.wfShareDirectEnabled) {
        if (prefs.wfShareEnabled) {
            qInfo(logSystem()) << "wfshare direct enabled; using direct listener instead of PBX registration for this initial implementation";
        }
        const quint16 directPort = prefs.wfShareDirectPort == 0 ? quint16(4569) : prefs.wfShareDirectPort;
        qInfo(logSystem()).noquote()
            << QStringLiteral("wfshare direct: listening on UDP %1").arg(directPort);
        wfShareStation->listenForDirectCalls(directPort, [this](const QString &username) -> QStringList {
            for (const SERVERUSER &user : serverConfig.users) {
                if (user.username == username && !user.password.trimmed().isEmpty()) {
                    return { user.password, decodedServerPassword(user.password) };
                }
            }
            return {};
        });
        return;
    }

    const QString host = prefs.wfShareHost.trimmed().isEmpty()
                             ? QStringLiteral("pbx.wfshare.org")
                             : prefs.wfShareHost.trimmed();
    const QString username = prefs.wfShareUsername.trimmed();
    if (username.isEmpty()) {
        qWarning(logSystem()) << "wfshare station disabled: username is empty";
        return;
    }

    const quint16 port = prefs.wfSharePort == 0 ? quint16(4569) : prefs.wfSharePort;
    qInfo(logSystem()).noquote()
        << QStringLiteral("wfshare station: registering %1 at %2:%3")
               .arg(username, host)
               .arg(port);

    wfShareStation->registerWithServer(host, port, username, prefs.wfSharePassword, 60);
}

void servermain::flushWfShareTxBuffer()
{
    if (!wfShareClientConnected || wfShareStation == Q_NULLPTR || wfShareTxBuffer.isEmpty()) {
        wfShareTxBuffer.clear();
        wfSharePendingCivToClient.clear();
        return;
    }

    radioTransportFrame::Frame frame;
    frame.channel = RadioTransportChannel::Civ;
    frame.payload = wfShareTxBuffer;
    wfShareTxBuffer.clear();

    wfShareTxFrames++;
    if (wfShareTxFrames <= 10 || wfShareTxFrames % 100 == 0) {
        qDebug(logSystem()).noquote()
            << QStringLiteral("wfshare station sent CI-V frame %1 bytes %2")
                   .arg(wfShareTxFrames)
                   .arg(frame.payload.size());
    }
    wfShareStation->sendPayload(radioTransportFrame::encode(frame), true);
}


void servermain::setDefPrefs()
{
    defPrefs.manufacturer = manufIcom;
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.pttType = pttCIV;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.localAFgain = 255;
    defPrefs.tcpPort = 0;
    defPrefs.audioSystem = qtAudio;
    defPrefs.rxAudio.name = QString("default");
    defPrefs.txAudio.name = QString("default");
    defPrefs.wfShareEnabled = false;
    defPrefs.wfShareHost = QStringLiteral("pbx.wfshare.org");
    defPrefs.wfSharePort = 4569;
    defPrefs.wfShareUsername.clear();
    defPrefs.wfSharePassword.clear();
    defPrefs.wfShareDirectEnabled = false;
    defPrefs.wfShareDirectPort = 4569;

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();

}

void servermain::loadSettings()
{
    qInfo(logSystem()) << "Loading settings from " << settings->fileName();
    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());
    prefs.manufacturer = static_cast<manufacturersType_t>(settings->value("Manufacturer",defPrefs.manufacturer).toInt());

    int numRadios = settings->beginReadArray("Radios");
    if (numRadios == 0) {
        settings->endArray();

        // We assume that QSettings is empty as there are no radios configured, create new:
        qInfo(logSystem()) << "Creating new settings file " << settings->fileName();
        settings->setValue("AudioSystem", defPrefs.audioSystem);
        settings->setValue("Manufacturer", defPrefs.manufacturer);

        numRadios = 1;
        settings->beginWriteArray("Radios");
        for (int i = 0; i < numRadios; i++)
        {
            settings->setArrayIndex(i);
            settings->setValue("RigCIVuInt", defPrefs.radioCIVAddr);
            settings->setValue("PTTType", defPrefs.pttType);
            settings->setValue("SerialPortRadio", defPrefs.serialPortRadio);
            settings->setValue("RigName", "<NONE>");
            settings->setValue("SerialPortBaud", defPrefs.serialPortBaud);
            settings->setValue("AudioInput", "default");
            settings->setValue("AudioOutput", "default");
            settings->setValue("WaterfallFormat", 0);
        }
        settings->endArray();

        settings->beginGroup("Server");
        settings->setValue("ServerEnabled", true);
        settings->setValue("ServerControlPort", udpDefPrefs.controlLANPort);
        settings->setValue("ServerCivPort", udpDefPrefs.serialLANPort);
        settings->setValue("ServerAudioPort", udpDefPrefs.audioLANPort);

        settings->beginWriteArray("Users");
        settings->setArrayIndex(0);
        settings->setValue("Username", "user");
        QByteArray pass;
        passcode("password", pass);
        settings->setValue("Password", QString(pass));
        settings->setValue("UserType", 0);

        settings->endArray();
    
        settings->endGroup();
        settings->sync();

    } else {
        settings->endArray();
    }

    numRadios = settings->beginReadArray("Radios");
    int tempNum = numRadios;
    
    for (int i = 0; i < numRadios; i++) {
        settings->setArrayIndex(i);
        RIGCONFIG* tempPrefs = new RIGCONFIG();
        tempPrefs->civAddr = (unsigned char)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();

        tempPrefs->pttType = (pttType_t)settings->value("PTTType", defPrefs.pttType).toInt();
        // Workaround for old config option
        if ((bool)settings->value("ForceRTSasPTT", false).toBool())
            tempPrefs->pttType = pttRTS;
        tempPrefs->serialPort = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
        tempPrefs->rigName = settings->value("RigName", "<NONE>").toString();
        tempPrefs->baudRate = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();
        tempPrefs->rxAudioSetup.name = settings->value("AudioInput", "default").toString();
        tempPrefs->txAudioSetup.name = settings->value("AudioOutput", "default").toString();
        tempPrefs->waterfallFormat = settings->value("WaterfallFormat", 0).toInt();
        tempPrefs->rxAudioSetup.type = prefs.audioSystem;
        tempPrefs->txAudioSetup.type = prefs.audioSystem;

        if (tempPrefs->serialPort == "auto") {
            // Find the ICOM radio connected, or, if none, fall back to OS default.
            // qInfo(logSystem()) << "Searching for serial port...";
            QString serialPortRig="";
            bool found = false;
            // First try to find first Icom port:
            for(const QSerialPortInfo & serialPortInfo: QSerialPortInfo::availablePorts())
            {
                if (serialPortInfo.serialNumber().left(3) == "IC-") {
                    qInfo(logSystem()) << "Icom Serial Port found: " << serialPortInfo.portName() << "S/N" << serialPortInfo.serialNumber();
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
                    tempPrefs->serialPort = (QString("/dev/") + serialPortInfo.portName());
#else
                    tempPrefs->serialPort = serialPortInfo.portName();
#endif
                    tempPrefs->rigName = serialPortInfo.serialNumber();
                    found = true;
                    break;
                }
            }

            if (!found) {
                QDirIterator it73("/dev/serial/by-id", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it97("/dev/serial", QStringList() << "*IC-9700*A*", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it785x("/dev/serial", QStringList() << "*IC-785*A*", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it705("/dev/serial", QStringList() << "*IC-705*A", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator it7610("/dev/serial", QStringList() << "*IC-7610*A", QDir::Files, QDirIterator::Subdirectories);
                QDirIterator itR8600("/dev/serial", QStringList() << "*IC-R8600*A", QDir::Files, QDirIterator::Subdirectories);

                if(!it73.filePath().isEmpty())
                {
                    // IC-7300
                    tempPrefs->serialPort = it73.filePath(); // first
                } else if(!it97.filePath().isEmpty())
                {
                    // IC-9700
                    tempPrefs->serialPort = it97.filePath();
                } else if(!it785x.filePath().isEmpty())
                {
                    // IC-785x
                    tempPrefs->serialPort = it785x.filePath();
                } else if(!it705.filePath().isEmpty())
                {
                    // IC-705
                    tempPrefs->serialPort = it705.filePath();
                } else if(!it7610.filePath().isEmpty())
                {
                    // IC-7610
                    tempPrefs->serialPort = it7610.filePath();
                } else if(!itR8600.filePath().isEmpty())
                {
                    // IC-R8600
                    tempPrefs->serialPort = itR8600.filePath();
                }
                else {
                    //fall back:
                    qInfo(logSystem()) << "Could not find an Icom serial port. Falling back to OS default. Use --port to specify, or modify preferences.";
                    qInfo(logSystem()) << "Found serial ports:";
                    for(const QSerialPortInfo & serialPortInfo: QSerialPortInfo::availablePorts())
                    {
                            qInfo(logSystem()) << serialPortInfo.portName() << "Manufacturer:" << serialPortInfo.manufacturer() << "S/N:" << serialPortInfo.serialNumber();
                    }

#ifdef Q_OS_MAC
                    tempPrefs->serialPort = QString("/dev/tty.SLAB_USBtoUART");
#endif
#ifdef Q_OS_LINUX
                    tempPrefs->serialPort = QString("/dev/ttyUSB0");
#endif
#ifdef Q_OS_WIN
                    tempPrefs->serialPort = QString("COM1");
#endif
                }
            }
        }

        QString guid = settings->value("GUID", "").toString();
        if (guid.isEmpty()) {
            guid = QUuid::createUuid().toString();
            settings->setValue("GUID", guid);
        }
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
        memcpy(tempPrefs->guid, QUuid::fromString(guid).toRfc4122().constData(), GUIDLEN);
#endif
        tempPrefs->rxAudioSetup.isinput = true;
        tempPrefs->txAudioSetup.isinput = false;
        tempPrefs->rxAudioSetup.localAFgain = 255;
        tempPrefs->txAudioSetup.localAFgain = 255;
        tempPrefs->rxAudioSetup.resampleQuality = 4;
        tempPrefs->txAudioSetup.resampleQuality = 4;

        tempPrefs->rig = Q_NULLPTR;
        tempPrefs->rigThread = Q_NULLPTR;

        serverConfig.rigs.append(tempPrefs);
        if (tempNum == 0) {
            settings->endGroup();
        }
    }
    if (tempNum > 0) {
        settings->endArray();
    }


    settings->beginGroup("Server");
    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.controlPort = settings->value("ServerControlPort", 50001).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", 50002).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", 50003).toInt();
    prefs.wfShareEnabled = settings->value("WfShareEnabled", defPrefs.wfShareEnabled).toBool();
    prefs.wfShareHost = settings->value("WfShareHost", defPrefs.wfShareHost).toString();
    prefs.wfSharePort = settings->value("WfSharePort", defPrefs.wfSharePort).toUInt();
    prefs.wfShareUsername = settings->value("WfShareUsername", defPrefs.wfShareUsername).toString();
    prefs.wfSharePassword = settings->value("WfSharePassword", defPrefs.wfSharePassword).toString();
    prefs.wfShareDirectEnabled = settings->value("WfShareDirectEnabled", defPrefs.wfShareDirectEnabled).toBool();
    prefs.wfShareDirectPort = settings->value("WfShareDirectPort", defPrefs.wfShareDirectPort).toUInt();

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

    settings->endGroup();
    settings->sync();

#if defined(RTAUDIO)
    delete audio;
#endif

}

void servermain::updateAudioDevices()
{

    for (RIGCONFIG* rig : serverConfig.rigs)
    {
        qDebug(logAudio()) << "Rig" << rig->rigName << "configured rxAudio device:" << rig->rxAudioSetup.name;
        qDebug(logAudio()) << "Rig" << rig->rigName << "configured txAudio device:" << rig->txAudioSetup.name;

        int inputNum = audioDev->findInput(rig->rigName, rig->rxAudioSetup.name);
        int outputNum = audioDev->findOutput(rig->rigName, rig->txAudioSetup.name);

        if (prefs.audioSystem == qtAudio) {
            rig->rxAudioSetup.port = audioDev->getInputDeviceInfo(inputNum);
            rig->txAudioSetup.port = audioDev->getOutputDeviceInfo(outputNum);
        }
        else {
            rig->rxAudioSetup.portInt = audioDev->getInputDeviceInt(inputNum);
            rig->txAudioSetup.portInt = audioDev->getOutputDeviceInt(outputNum);
        }
        rig->rxAudioSetup.name = audioDev->getInputName(inputNum);
        rig->txAudioSetup.name = audioDev->getOutputName(outputNum);
    }

}

void servermain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
    //qInfo(logSystem()) << "PTT status: " << pttOn;
    amTransmitting = pttOn;
}

void servermain::handlePttLimit()
{
    // ptt time exceeded!
    std::cout << "Transmit timeout at 3 minutes. Sending PTT OFF command now.\n";
    emit setPTT(false);
    emit getPTT();
}

void servermain::receiveBaudRate(quint32 baud)
{
    qInfo() << "Received serial port baud rate from remote server:" << baud;
    prefs.serialPortBaud = baud;
}

void servermain::powerRigOn()
{
    emit sendPowerOn();
}

void servermain::powerRigOff()
{
    emit sendPowerOff();
}

void servermain::receiveStateInfo(rigstate* state)
{
    qInfo("Received rig state for wfmain");
    Q_UNUSED(state);
    //rigState = state;
}

void servermain::setManufacturer(manufacturersType_t man)
{

    this->rigList.clear();
    qInfo() << "Searching for radios with Manufacturer =" << man;

#ifndef Q_OS_LINUX
    QString systemRigLocation = QCoreApplication::applicationDirPath();
#else
    QString systemRigLocation = PREFIX;
#endif

#ifdef Q_OS_LINUX
    systemRigLocation += "/share/wfview/rigs";
#else
    systemRigLocation +="/rigs";
#endif

    QDir systemRigDir(systemRigLocation);

    if (!systemRigDir.exists()) {
        qWarning() << "********* Rig directory does not exist ********";
    } else {
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
            manufacturersType_t manuf = static_cast<manufacturersType_t>(rigSettings->value("Manufacturer", manufIcom).toInt());
            if (manuf == man) {
                uchar civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = systemRigDir.absoluteFilePath(rig);

                qDebug() << QString("Found Rig %0 with CI-V address of 0x%1 and version %2").arg(model).arg(civ,2,16,QChar('0')).arg(ver,0,'f',2);
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

            manufacturersType_t manuf = static_cast<manufacturersType_t>(rigSettings->value("Manufacturer", manufIcom).toInt());
            if (manuf == man) {
                uchar civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = userRigDir.absoluteFilePath(rig);

                auto it = this->rigList.find(civ);

                if (it != this->rigList.end())
                {
                    if (ver >= it.value().version) {
                        qInfo() << QString("Found User Rig %0 with CI-V address of 0x%1 and newer or same version than system one (%2>=%3)").arg(model).arg(civ,2,16,QChar('0')).arg(ver,0,'f',2).arg(it.value().version,0,'f',2);
                        this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                    }
                } else {
                    qInfo() << QString("Found New User Rig %0 with CI-V address of 0x%1 version %2").arg(model).arg(civ,2,16,QChar('0')).arg(ver,0,'f',2);
                    this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                }
                // Any user modified rig files will override system provided ones.
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

}
