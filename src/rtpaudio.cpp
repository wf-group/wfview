#include "rtpaudio.h"
#include "logcategories.h"

// Audio stream
rtpAudio::rtpAudio(QString ip, quint16 port, audioSetup outSetup, audioSetup inSetup, QObject* parent)
    : QObject{parent}, outSetup(outSetup), inSetup(inSetup), port(port)
{
    qInfo(logUdp()) << "Starting rtpAudio";

    if (inSetup.sampleRate == 0) {
        enableIn = false;
    } else {
        this->ip = QHostAddress(ip);
    }

    if (outSetup.sampleRate == 0) {
        enableOut = false;
    }
}

void rtpAudio::shutdown()
{
    qDebug(logUdp()) << "[SHUTDOWN] rtpAudio::shutdown() enter";

    // Close the UDP socket first — this stops readyRead signals and unblocks
    // the event loop so that quit() can be processed after we return.
    if (udp != Q_NULLPTR) {
        qDebug(logUdp()) << "[SHUTDOWN] Closing RTP connection";
        udp->close();
        delete udp;
        udp = Q_NULLPTR;
    }

    // dispose() audio handlers while the audio threads' event loops are still
    // running (needed for BlockingQueuedConnection in dispose()).
    if (outaudio) {
        qDebug(logUdp()) << "[SHUTDOWN] outaudio->dispose() ...";
        outaudio->dispose();
        qDebug(logUdp()) << "[SHUTDOWN] outaudio->dispose() done";
    }
    if (inaudio) {
        qDebug(logUdp()) << "[SHUTDOWN] inaudio->dispose() ...";
        inaudio->dispose();
        qDebug(logUdp()) << "[SHUTDOWN] inaudio->dispose() done";
    }

    if (outAudioThread != Q_NULLPTR) {
        qDebug(logUdp()) << "[SHUTDOWN] outAudioThread->quit()";
        outAudioThread->quit();
        qDebug(logUdp()) << "[SHUTDOWN] outAudioThread->wait() ...";
        outAudioThread->wait();
        qDebug(logUdp()) << "[SHUTDOWN] outAudioThread done";
    }

    if (inAudioThread != Q_NULLPTR) {
        qDebug(logUdp()) << "[SHUTDOWN] inAudioThread->quit()";
        inAudioThread->quit();
        qDebug(logUdp()) << "[SHUTDOWN] inAudioThread->wait() ...";
        inAudioThread->wait();
        qDebug(logUdp()) << "[SHUTDOWN] inAudioThread done";
    }

    qDebug(logUdp()) << "[SHUTDOWN] rtpAudio::shutdown() complete";
}

rtpAudio::~rtpAudio()
{
    qDebug(logUdp()) << "[SHUTDOWN] ~rtpAudio enter";
    // shutdown() should have been called already; safety net if not:
    if (udp != Q_NULLPTR) {
        udp->close();
        delete udp;
        udp = Q_NULLPTR;
    }
    debugFile.close();
    qDebug(logUdp()) << "[SHUTDOWN] ~rtpAudio complete";
}

void rtpAudio::init()
{
    qDebug(logUdp()) << "[DIAG] rtpAudio::init() enter, thread=" << QThread::currentThread();

    udp = new QUdpSocket(this);

    if (!udp->bind(port))
    {
        // We couldn't bind to the selected port.
        qCritical(logUdp()) << "**** Unable to bind to RTP port" << port << "Cannot continue! ****";
        return;
    }
    else
    {
        QUdpSocket::connect(udp, &QUdpSocket::readyRead, this, &rtpAudio::dataReceived);
        qInfo(logUdp()) << "RTP Stream bound to local port:" << udp->localPort() << " remote port:" << port;
    }

    if (enableOut)
    {
        qDebug(logUdp()) << "[DIAG] creating output audio handler, type=" << outSetup.type;
        if (outSetup.type == qtAudio) {
            outaudio = new audioHandlerQtOutput();
        }
        else if (outSetup.type == portAudio) {
            outaudio = new audioHandlerPaOutput();
        }
        else if (outSetup.type == rtAudio) {
            outaudio = new audioHandlerRtOutput();
        }
#ifndef BUILD_WFSERVER
        else if (outSetup.type == tciAudio) {
            outaudio = new audioHandlerTciOutput();
        }
#endif
        else
        {
            qCritical(logAudio()) << "Unsupported Receive Audio Handler selected!";
        }
        qDebug(logUdp()) << "[DIAG] output audio handler created";

        outAudioThread = new QThread(this);
        outAudioThread->setObjectName("outAudio()");

        outaudio->moveToThread(outAudioThread);

        connect(this, SIGNAL(setupOutAudio(audioSetup)), outaudio, SLOT(init(audioSetup)));

        // signal/slot not currently used.
        connect(this, SIGNAL(haveAudioData(audioPacket)), outaudio, SLOT(incomingAudio(audioPacket)));
        connect(this, SIGNAL(haveChangeLatency(quint16)), outaudio, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(haveSetVolume(quint8)), outaudio, SLOT(setVolume(quint8)));
        connect(outaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getOutLevels(quint16, quint16, quint16, quint16, bool, bool)));
        connect(outAudioThread, SIGNAL(finished()), outaudio, SLOT(deleteLater()));
        connect(outaudio, &audioHandlerBase::initFailed, this, &rtpAudio::onOutAudioInitFailed);

        outAudioThread->start(QThread::TimeCriticalPriority);

        emit setupOutAudio(outSetup);
        qDebug(logUdp()) << "[DIAG] output audio thread started and setupOutAudio emitted";
    }

    if (enableIn) {
        qDebug(logUdp()) << "[DIAG] creating input audio handler, type=" << inSetup.type;
        if (inSetup.type == qtAudio) {
            inaudio = new audioHandlerQtInput();
        }
        else if (inSetup.type == portAudio) {
            inaudio = new audioHandlerPaInput();
        }
        else if (inSetup.type == rtAudio) {
            inaudio = new audioHandlerRtInput();
        }
#ifndef BUILD_WFSERVER
        else if (inSetup.type == tciAudio) {
            inaudio = new audioHandlerTciInput();
        }
#endif
        else
        {
            qCritical(logAudio()) << "Unsupported Transmit Audio Handler selected!";
        }
        qDebug(logUdp()) << "[DIAG] input audio handler created";

        inAudioThread = new QThread(this);
        inAudioThread->setObjectName("inAudio()");

        inaudio->moveToThread(inAudioThread);

        connect(this, SIGNAL(setupInAudio(audioSetup)), inaudio, SLOT(init(audioSetup)));
        connect(inaudio, SIGNAL(haveAudioData(audioPacket)), this, SLOT(receiveAudioData(audioPacket)));
        connect(inaudio, SIGNAL(haveLevels(quint16, quint16, quint16, quint16, bool, bool)), this, SLOT(getInLevels(quint16, quint16, quint16, quint16, bool, bool)));

        connect(inAudioThread, SIGNAL(finished()), inaudio, SLOT(deleteLater()));
        connect(inaudio, &audioHandlerBase::initFailed, this, &rtpAudio::onInAudioInitFailed);

        inAudioThread->start(QThread::TimeCriticalPriority);

        emit setupInAudio(inSetup);
        qDebug(logUdp()) << "[DIAG] input audio thread started and setupInAudio emitted";
    }

    // Heartbeat timer — confirms the event loop is responsive after init returns
    auto* heartbeat = new QTimer(this);
    connect(heartbeat, &QTimer::timeout, this, [this]{
        static int hbCount = 0;
        if (hbCount < 5) { // Log first 5 heartbeats then stop to reduce noise
            qDebug(logUdp()) << "[DIAG] rtpThread heartbeat" << hbCount
                             << "dataPackets=" << packetCount;
            hbCount++;
        }
    });
    heartbeat->start(1000);

    qDebug(logUdp()) << "[DIAG] rtpAudio::init() complete, event loop should now be free";
}

void rtpAudio::dataReceived()
{
    QHostAddress sender;
    quint16 port;
    QByteArray d(udp->pendingDatagramSize(),0x0);
    udp->readDatagram(d.data(), d.size(), &sender, &port);
    rtp_header in;
    memcpy(in.packet,d.mid(0,12).constData(),sizeof(rtp_header));
    //qInfo(logUdp()) << "Audio out: version:" << in.version << "type:" << in.payloadType << "len" << d.size() << "seq" << qFromBigEndian(in.seq) << "header" << sizeof(rtp_header) << "hex" << d.mid(0,12).toHex(' ');
    if (in.payloadType == 96) {
        // We have audio data
        audioPacket tempAudio;
        tempAudio.seq = qFromBigEndian(in.seq);
        tempAudio.time = QTime::currentTime().addMSecs(0);
        tempAudio.sent = 0;
        tempAudio.data = d.mid(12);

        // When RX is muted (self-monitor mode), silence the radio audio.
        if (m_rxMuted)
            tempAudio.data.fill(0);

        emit haveAudioData(tempAudio);
        packetCount++;
        size = size + tempAudio.data.size();
    }
}


void rtpAudio::setRxMuted(bool muted)
{
    m_rxMuted = muted;
}

void rtpAudio::changeLatency(quint16 value)
{
    emit haveChangeLatency(value);
}

void rtpAudio::setVolume(quint8 value)
{
    emit haveSetVolume(value);
}

void rtpAudio::getOutLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    emit haveOutLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void rtpAudio::getInLevels(quint16 amplitudePeak, quint16 amplitudeRMS, quint16 latency, quint16 current, bool under, bool over)
{
    emit haveInLevels(amplitudePeak, amplitudeRMS, latency, current, under, over);
}

void rtpAudio::receiveAudioData(audioPacket audio)
{
    // I really can't see how this could be possible but a quick sanity check!
    if (inaudio == Q_NULLPTR) {
        return;
    }
    if (audio.data.length() > 0) {
        int len = 0;

        while (len < audio.data.length() && udp != NULL


               ) {
            QByteArray partial = audio.data.mid(len, 640);
            rtp_header p;
            memset(p.packet, 0x0, sizeof(p)); // We can't be sure it is initialized with 0x00!
            p.version = 2;
            p.padding = 0;
            p.extension = 0;
            p.csrc = 0;
            p.marker = 0;
            p.payloadType=96;
            p.seq = qToBigEndian(seq++);
            p.timestamp = 0;
            p.ssrc = quint32(0x00) << 24 | quint32(0x30) << 16 | quint32(0x39) << 8 | (quint32(0x38) & 0xff);
            QByteArray in = QByteArray::fromRawData((const char*)p.packet, sizeof(p));
            in.append(partial);
            len = len + partial.length();
            //qInfo(logUdp()) << "Audio In: " << in.length() << "to" << ip.toString() << "port" << port << "ver" << p.version << "type" << p.payloadType << "seq" << p.seq << "ssrc" << QString::number(p.ssrc,16);
            udp->writeDatagram(in, ip, port);
        }
    }
}

void rtpAudio::onOutAudioInitFailed()
{
    qWarning(logAudio()) << "Output Audio Initialization failed. Cleaning up.";
    if (outAudioThread) {
        outAudioThread->quit();
    }
    outaudio = nullptr;
}

void rtpAudio::onInAudioInitFailed()
{
    qWarning(logAudio()) << "Input Audio Initialization failed. Cleaning up.";
    if (inAudioThread) {
        inAudioThread->quit();
    }
    inaudio = nullptr;
}
