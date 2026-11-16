#include "audiohandlertcioutput.h"
#include "tciserver.h"

bool audioHandlerTciOutput::openDevice() noexcept
{

    emit setupConverter(radioFormat,  codec,
                        nativeFormat, codecType::LPCM,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

    if (setupData.tci != Q_NULLPTR) {
        connect(this,SIGNAL(sendTCIAudio(audioPacket)), (tciServer*)setupData.tci, SLOT(receiveTCIAudio(audioPacket)));
    } else {
        qCritical(logAudio()) << "***** TCI NOT FOUND *****";
    }
    return true;
}

void audioHandlerTciOutput::closeDevice() noexcept
{
}

void audioHandlerTciOutput::incomingAudio(audioPacket packet)
{
    packet.volume = volume;
    emit sendToConverter(packet);
}

void audioHandlerTciOutput::onConverted(audioPacket pkt)
{
    // TCI is a network stream and the client keeps its own buffer, so a
    // packet that is a little late is still useful. This path also goes
    // through the rig and main threads, which adds queueing delay that the
    // local sound card path does not have. Dropping at the sound card
    // latency (about 50 ms) discarded every packet for seconds at a time,
    // so only drop audio that is far too old to be useful.
    const int nowToPktMs = pkt.time.msecsTo(QTime::currentTime());
    const int maxAgeMs = qMax(1000, int(setupData.latency) * 10);
    if (nowToPktMs > maxAgeMs) {
        ++lateDrops;
        if (!lateDropLog.isValid() || lateDropLog.elapsed() > 5000) {
            qDebug(logAudio()) << "TCI RX audio: dropped" << lateDrops
                               << "late packet(s), last age" << nowToPktMs << "ms, limit" << maxAgeMs << "ms";
            lateDrops = 0;
            lateDropLog.restart();
        }
        return;
    }

    arrayBuffer.append(pkt.data);
    amplitude = pkt.amplitudePeak;
    currentLatency = 0;
    const qsizetype packetBytes = qsizetype(TCI_AUDIO_SAMPLES * nativeFormat.channelCount() * sizeof(float));
    if (arrayBuffer.length() >= packetBytes) {
        pkt.data.clear();
        pkt.data = arrayBuffer.mid(0, packetBytes);
        arrayBuffer.remove(0, packetBytes);
        pkt.sampleRate = quint32(nativeFormat.sampleRate());
        pkt.channels = quint8(nativeFormat.channelCount());
        pkt.codec = 0;
        emit sendTCIAudio(pkt);
        emit haveLevels(pkt.amplitudePeak, quint16(pkt.amplitudeRMS*255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());

    }
}



QAudioFormat audioHandlerTciOutput::getNativeFormat()
{    
    QAudioFormat native;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    native.setByteOrder(QAudioFormat::LittleEndian);
    native.setCodec("audio/pcm");
    native.setSampleType(QAudioFormat::Float);
    native.setSampleSize(32);
#else
    native.setSampleFormat(QAudioFormat::Float);
#endif

    native.setSampleRate(48000);
    native.setChannelCount(2);
    return native;
}

bool audioHandlerTciOutput::isFormatSupported(QAudioFormat f)
{
    Q_UNUSED(f)
    return true;
}
