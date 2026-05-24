#include "audiohandlerqtinput.h"

bool audioHandlerQtInput::openDevice() noexcept
{
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    audioInput = new QAudioInput(deviceInfo, nativeFormat, this);
    connect(audioInput, SIGNAL(stateChanged(QAudio::State)), this, SLOT(stateChanged(QAudio::State)));
#else
    audioInput = new QAudioSource(deviceInfo, nativeFormat, this);
    connect(audioInput, &QAudioSource::stateChanged, this, &audioHandlerQtInput::stateChanged);
#endif

    emit setupConverter(nativeFormat, codecType::LPCM,
                        radioFormat,  codec,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
    audioInput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 100));
#else
    audioInput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));
#endif

    audioDevice = audioInput->start();
    if (!audioDevice) return false;

    connect(audioInput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
    connect(audioDevice, SIGNAL(readyRead()), this, SLOT(onReadyRead()), Qt::UniqueConnection);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    qInfo(logAudio()) << "Connected to Qt audio input device" << deviceInfo.deviceName();
#else
    qInfo(logAudio()) << "Connected to Qt audio input device" << deviceInfo.description();
#endif
    return true;
}

void audioHandlerQtInput::closeDevice() noexcept
{
    if (audioInput) {
        if (audioInput->state() != QAudio::StoppedState) audioInput->stop();
        audioInput->deleteLater();
        audioInput = nullptr;
    }
    audioDevice = nullptr;
}

void audioHandlerQtInput::onReadyRead()
{
    if (!audioDevice) return;
    tempBuf.data.append(audioDevice->readAll());

    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    while (tempBuf.data.size() >= bytesPerBlock) {
        audioPacket pkt;
        pkt.time   = QTime::currentTime();
        pkt.sent   = 0;
        pkt.volume = volume;
        memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data   = tempBuf.data.left(bytesPerBlock);
        tempBuf.data.remove(0, bytesPerBlock);
        emit sendToConverter(pkt);
    }
}

void audioHandlerQtInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;

    emit haveAudioData(audio);

    if (lastReceived.isValid() && lastReceived.elapsed() > 100) {
        qDebug(logAudio()) << role() << "Time since last audio packet" << lastReceived.elapsed()
        << "Expected around" << setupData.blockSize;
    }
    if (!lastReceived.isValid()) lastReceived.start(); else lastReceived.restart();

    amplitude.store(audio.amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), audio.amplitudeRMS, setupData.latency,
                    static_cast<quint16>(latencyMs()), isUnderrun.load(), isOverrun.load());
}


QAudioFormat audioHandlerQtInput::getNativeFormat()
{
    QAudioFormat format = setupData.port.preferredFormat();
#if (QT_VERSION >= QT_VERSION_CHECK(6,0,0))
    if (format.channelCount() < 1 && setupData.port.maximumChannelCount() > 0) {
        qWarning(logAudio()) << "Qt audio input preferred format has no channels; deriving fallback for"
                             << setupData.port.description();
        format.setChannelCount(qMin(2, setupData.port.maximumChannelCount()));
        format.setSampleRate(qMax(48000, setupData.port.minimumSampleRate()));
        if (setupData.port.maximumSampleRate() > 0)
            format.setSampleRate(qMin(format.sampleRate(), setupData.port.maximumSampleRate()));

        const auto sampleFormats = setupData.port.supportedSampleFormats();
        if (sampleFormats.contains(QAudioFormat::Float))
            format.setSampleFormat(QAudioFormat::Float);
        else if (sampleFormats.contains(QAudioFormat::Int16))
            format.setSampleFormat(QAudioFormat::Int16);
        else if (!sampleFormats.isEmpty())
            format.setSampleFormat(sampleFormats.first());
    }
#endif
    return format;
}

bool audioHandlerQtInput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
