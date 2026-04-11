// =============================
// audiohandleroutput.cpp
// =============================
#include "audiohandlerqtoutput.h"

bool audioHandlerQtOutput::openDevice() noexcept
{
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    audioOutput = new QAudioOutput(deviceInfo, nativeFormat, this);
    connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), this, SLOT(stateChanged(QAudio::State)));
#else
    audioOutput = new QAudioSink(deviceInfo, nativeFormat, this);
    connect(audioOutput, &QAudioSink::stateChanged, this, &audioHandlerQtOutput::stateChanged);
#endif

    emit setupConverter(radioFormat,  codec,
                        nativeFormat, codecType::LPCM,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 100));
#else
    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));
#endif

    audioDevice = audioOutput->start();
    if (!audioDevice) return false;

    // Pre-fill half the buffer with silence so ALSA has data to pull
    // before the first real audio packet arrives from the network.
    {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        QByteArray silence(prefillBytes, '\0');
        audioDevice->write(silence.constData(), silence.size());
    }

    connect(audioOutput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    qInfo(logAudio()) << "Connected to Qt audio output device" << deviceInfo.deviceName();
#else
    qInfo(logAudio()) << "Connected to Qt audio output device" << deviceInfo.description();
#endif
    return true;
}

void audioHandlerQtOutput::closeDevice() noexcept
{
    if (audioOutput) {
        if (audioOutput->state() != QAudio::StoppedState) audioOutput->stop();
        audioOutput->deleteLater();
        audioOutput = nullptr;
    }
    audioDevice = nullptr;
}

void audioHandlerQtOutput::incomingAudio(audioPacket packet)
{
    if (!audioDevice || packet.data.isEmpty()) return;
    packet.volume = volume;
    emit sendToConverter(packet);
}

void audioHandlerQtOutput::onConverted(audioPacket pkt)
{
    if (!audioOutput || !audioDevice || pkt.data.isEmpty()) return;
    const int nowToPktMs = pkt.time.msecsTo(QTime::currentTime());
    if (nowToPktMs > setupData.latency * 1.5) {
        return; // late frame ( more than latency * 1.5), drop
    }
    writeToOutputDevice(pkt.data, pkt.seq, pkt.amplitudePeak, pkt.amplitudeRMS);
}

void audioHandlerQtOutput::writeToOutputDevice(QByteArray data, quint32 seq, float ampPeak, float ampRms)
{
    Q_UNUSED(seq);
    if (!audioOutput || !audioDevice) return;

    // Recover from underrun: re-prime the buffer with silence so the device
    // has a cushion before real audio resumes, preventing click cascades.
    if (isUnderrun.load(std::memory_order_relaxed)) {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        const int freeBytes    = static_cast<int>(audioOutput->bytesFree());
        const int silenceBytes = qMin(prefillBytes, freeBytes);
        if (silenceBytes > 0) {
            QByteArray silence(silenceBytes, '\0');
            audioDevice->write(silence.constData(), silence.size());
        }
        isUnderrun.store(false, std::memory_order_relaxed);
        if (underTimer && underTimer->isActive()) underTimer->stop();
    }

    qint64 buffered = audioOutput->bufferSize() - audioOutput->bytesFree();
    int devLatencyMs = static_cast<int>(nativeFormat.durationForBytes(buffered) / 1000);
    int pipelineMs   = lastReceived.isValid() ? static_cast<int>(lastReceived.elapsed()) : 0;
    int newLatency   = pipelineMs + devLatencyMs;
    int prev = currentLatency.load(std::memory_order_relaxed);
    currentLatency.store(static_cast<int>(prev * 0.8 + newLatency * 0.2), std::memory_order_relaxed);

    qint64 toWrite = data.size();
    const char* p  = data.constData();
    while (toWrite > 0) {
        qint64 written = audioDevice->write(p, toWrite);
        if (written <= 0) break;
        p       += written;
        toWrite -= written;
    }

    lastReceived.restart();
    amplitude.store(ampPeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), static_cast<quint16>(ampRms * 255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

QAudioFormat audioHandlerQtOutput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool audioHandlerQtOutput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
