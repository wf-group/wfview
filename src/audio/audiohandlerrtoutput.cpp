#include "audiohandlerrtoutput.h"
#include <cstring>

audioHandlerRtOutput::audioHandlerRtOutput(QObject* parent)
    : audioHandlerBase(parent) {}

static constexpr int kUnderrunCooldownMs = 200;  // min interval between recovery primes

int audioHandlerRtOutput::rtCallback(void* out, void* in, unsigned nFrames, double, RtAudioStreamStatus st, void* u)
{ Q_UNUSED(in); static_cast<audioHandlerRtOutput*>(u)->handleCallbackOutput(out, nFrames, st); return 0; }


void audioHandlerRtOutput::handleCallbackOutput(void* out, unsigned nFrames, RtAudioStreamStatus st)
{
    if (st & RTAUDIO_OUTPUT_UNDERFLOW) {
        isUnderrun.store(true, std::memory_order_relaxed);
        underrunCount.fetch_add(1, std::memory_order_relaxed);
    }
    const size_t nBytes = nFrames * bytesPerFrame;
    if (!outRB) { memset(out, 0, nBytes); return; }
    size_t got = outRB->pop(static_cast<char*>(out), nBytes);
    if (got < nBytes) memset(static_cast<char*>(out)+got, 0, nBytes-got);
    int ringMs = int((outRB->size()/bytesPerFrame) * 1000.0 / nativeFormat.sampleRate());
    int prev=currentLatency.load(); currentLatency.store(int(prev*0.8 + ringMs*0.2));
}

bool audioHandlerRtOutput::openDevice() noexcept
{
    qDebug(logAudio()) << "Creating output RT Audio device:" << setupData.name;

#if RTAUDIO_VERSION_MAJOR < 6
    try {
        if (!rtaudio.getDeviceCount()) return false;
        RtAudio::StreamParameters outParams;
        outParams.deviceId=setupData.portInt;
        outParams.nChannels=nativeFormat.channelCount();
        outParams.firstChannel=0;
        RtAudio::StreamOptions opts; opts.streamName = "audioHandlerRtOutput";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        fmt = (nativeFormat.sampleSize() == 16) ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
#else
        fmt = (nativeFormat.sampleFormat()==QAudioFormat::Int16) ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
#endif
        bytesPerSample = (fmt==RTAUDIO_SINT16?2:4);
        bytesPerFrame = bytesPerSample * nativeFormat.channelCount();
        const unsigned sr = nativeFormat.sampleRate();
        unsigned frames = qMax(64u, static_cast<unsigned>((setupData.latency * sr)/1000));
        outRB = std::make_unique<ByteRing>(nativeFormat.bytesForDuration(setupData.latency*2000));


        rtaudio.openStream(&outParams, nullptr, fmt, sr, &frames, &audioHandlerRtOutput::rtCallback, this, &opts);
        if (rtaudio.isStreamOpen()) rtaudio.startStream();
        if (!rtaudio.isStreamRunning()) { reportError("RtAudio output stream failed to start"); return false; }

        // Pre-fill ring buffer with silence so ALSA has data to pull
        // before the first real audio packet arrives from the network.
        prefillRingBuffer();

        emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setupData.resampleQuality);
        connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
        connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
        return true;
    } catch (RtAudioError& e) { reportError(QString::fromStdString(e.getMessage())); return false; }
#else
    if (!rtaudio.getDeviceCount()) return false;

    rtaudio.setErrorCallback([](RtAudioErrorType t, const std::string& m){
        qWarning() << "RtAudio error" << int(t) << ":" << QString::fromStdString(m);
    });

    RtAudio::StreamParameters outParams; outParams.deviceId=setupData.portInt; outParams.nChannels=nativeFormat.channelCount(); outParams.firstChannel=0;
    RtAudio::StreamOptions opts; opts.streamName = "audioHandlerRtOutput";
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    bool useInt16 = (nativeFormat.sampleSize() == 16);
#else
    bool useInt16 = (nativeFormat.sampleFormat() == QAudioFormat::Int16);
#endif
    fmt = useInt16 ? RTAUDIO_SINT16 : RTAUDIO_FLOAT32;
    bytesPerSample = useInt16 ? 2 : 4;
    bytesPerFrame  = bytesPerSample * nativeFormat.channelCount();
    const unsigned sr = nativeFormat.sampleRate();
    unsigned frames = qMax(64u, static_cast<unsigned>((setupData.latency * sr)/1000));
    outRB = std::make_unique<ByteRing>(nativeFormat.bytesForDuration(setupData.latency*2000));

    rtaudio.openStream(&outParams, nullptr, fmt, sr, &frames, &audioHandlerRtOutput::rtCallback, this, &opts);
    if (rtaudio.isStreamOpen()) rtaudio.startStream();
    if (!rtaudio.isStreamRunning()) { reportError("RtAudio output stream failed to start"); return false; }

    // Pre-fill ring buffer with silence so ALSA has data to pull
    // before the first real audio packet arrives from the network.
    prefillRingBuffer();

    emit setupConverter(radioFormat, codec, nativeFormat, codecType::LPCM, 7, setupData.resampleQuality);
    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
    return true;
#endif
}



void audioHandlerRtOutput::closeDevice() noexcept
{
    qDebug(logAudio()) << "[SHUTDOWN] RtOutput::closeDevice() enter, streamOpen=" << rtaudio.isStreamOpen();
#ifdef RT_EXCEPTION
    try {
        if (rtaudio.isStreamOpen())
        {
            if (rtaudio.isStreamRunning())
            {
                qDebug(logAudio()) << "[SHUTDOWN] RtOutput::abortStream() ...";
                rtaudio.abortStream();
                qDebug(logAudio()) << "[SHUTDOWN] RtOutput::abortStream() done";
            }
            qDebug(logAudio()) << "[SHUTDOWN] RtOutput::closeStream() ...";
            rtaudio.closeStream();
            qDebug(logAudio()) << "[SHUTDOWN] RtOutput::closeStream() done";
        }
    }
    catch(...) {}
#else
    if (rtaudio.isStreamOpen()) {
        if (rtaudio.isStreamRunning())
        {
            qDebug(logAudio()) << "[SHUTDOWN] RtOutput::abortStream() ...";
            rtaudio.abortStream();
            qDebug(logAudio()) << "[SHUTDOWN] RtOutput::abortStream() done";
        }
        qDebug(logAudio()) << "[SHUTDOWN] RtOutput::closeStream() ...";
        rtaudio.closeStream();
        qDebug(logAudio()) << "[SHUTDOWN] RtOutput::closeStream() done";
    }
#endif
    outRB.reset();
    qDebug(logAudio()) << "[SHUTDOWN] RtOutput::closeDevice() complete";
}


void audioHandlerRtOutput::incomingAudio(audioPacket packet)
{
    if (packet.data.isEmpty()) return;
    packet.volume = volume;
    emit sendToConverter(packet);
}


void audioHandlerRtOutput::onConverted(audioPacket pkt)
{
    if (pkt.data.isEmpty() || !outRB) return;
    const int age = pkt.time.msecsTo(QTime::currentTime()); if (age > setupData.latency * 1.5) return;

    // Recover from underrun: re-prime the ring buffer with silence so the
    // callback has a cushion before real audio resumes, preventing click cascades.
    if (isUnderrun.load(std::memory_order_relaxed)) {
        if (!lastRecovery.isValid() || lastRecovery.elapsed() > kUnderrunCooldownMs) {
            prefillRingBuffer();
            lastRecovery.restart();
            qDebug(logAudio()) << "RtAudio output underrun recovery: re-primed ring buffer (total underruns:" << underrunCount.load(std::memory_order_relaxed) << ")";
        }
        isUnderrun.store(false, std::memory_order_relaxed);
    }

    outRB->push(pkt.data.constData(), size_t(pkt.data.size()));
    lastReceived.restart(); amplitude.store(pkt.amplitudePeak);
    emit haveLevels(amplitudePeak(), quint16(pkt.amplitudeRMS*255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

void audioHandlerRtOutput::prefillRingBuffer()
{
    if (!outRB) return;
    // Fill half the ring buffer capacity with silence
    const size_t prefillBytes = outRB->capacity() / 2;
    QByteArray silence(static_cast<int>(prefillBytes), '\0');
    outRB->push(silence.constData(), prefillBytes);
}

bool audioHandlerRtOutput::isFormatSupported(QAudioFormat f)
{
    bool ret=true;
#if RTAUDIO_VERSION_MAJOR < 6
    RtAudio::DeviceInfo info;
    try {
        info = rtaudio.getDeviceInfo(setupData.portInt);
    }
    catch (RtAudioError e) {
        qInfo(logAudio()) << "Device exception:" << setupData.portInt << ":" << QString::fromStdString(e.getMessage());
        return false;
    }
    if (info.probed)
    {
#else
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
#endif

        // Is this format supported?
        // First look through supported channel numbers
        if (int(info.outputChannels) < f.channelCount())
            ret = false;

        // Now search sample rates:
        if (std::find(info.sampleRates.begin(), info.sampleRates.end(), f.sampleRate()) == info.sampleRates.end())
            ret = false;

        // Now search output format:

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!(f.sampleType() == QAudioFormat::Float && info.nativeFormats & RTAUDIO_FLOAT32) &&
            !(f.sampleType() == QAudioFormat::SignedInt && f.sampleSize() == 32 && info.nativeFormats & RTAUDIO_SINT32) &&
            !(f.sampleType() == QAudioFormat::SignedInt && f.sampleSize() == 16 && info.nativeFormats & RTAUDIO_SINT16))
#else
    if (!(f.sampleFormat() == QAudioFormat::Float && info.nativeFormats & RTAUDIO_FLOAT32) &&
        !(f.sampleFormat() == QAudioFormat::Int32 && info.nativeFormats & RTAUDIO_SINT32) &&
        !(f.sampleFormat() == QAudioFormat::Int16 && info.nativeFormats & RTAUDIO_SINT16))
#endif
        {
            ret = false;
        }
#if RTAUDIO_VERSION_MAJOR < 6
    }
#endif
    return ret;
}


QAudioFormat audioHandlerRtOutput::getNativeFormat()
    {

        QAudioFormat native;
#if RTAUDIO_VERSION_MAJOR < 6
        RtAudio::DeviceInfo info;
        try {
            info = rtaudio.getDeviceInfo(setupData.portInt);
        }
        catch (RtAudioError e) {
            qCritical(logAudio()) << "Device exception:" << setupData.portInt << ":" << QString::fromStdString(e.getMessage());
            return native;
        }
        if (info.probed)
        {
#else
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(setupData.portInt);
#endif

            qDebug(logAudio()) << QString::fromStdString(info.name) << "(" << setupData.portInt << ") successfully probed";

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            native.setByteOrder(QAudioFormat::LittleEndian);
            native.setCodec("audio/pcm");
#endif

            if (info.nativeFormats == 0)
            {
                qCritical(logAudio()) << "rtaudio output, no natively supported data formats!";
                return QAudioFormat();
            }

            qDebug(logAudio()) << "rtaudio output channels:" << info.outputChannels << " preferred sample rate:" << info.preferredSampleRate;
            native.setChannelCount(info.outputChannels);
            native.setSampleRate(info.preferredSampleRate);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            if (info.nativeFormats & RTAUDIO_FLOAT32) {
                native.setSampleType(QAudioFormat::Float);
                native.setSampleSize(32);
            }
            else if (info.nativeFormats & RTAUDIO_SINT32)
            {
                native.setSampleType(QAudioFormat::SignedInt);
                native.setSampleSize(32);
            }
            else if (info.nativeFormats & RTAUDIO_SINT16)
            {
                native.setSampleType(QAudioFormat::SignedInt);
                native.setSampleSize(16);
            }
            else if (info.nativeFormats & RTAUDIO_SINT24)
            {
                native.setSampleType(QAudioFormat::SignedInt);
                native.setSampleSize(24);
            }
#else
        if (info.nativeFormats & RTAUDIO_FLOAT32) {
            native.setSampleFormat(QAudioFormat::Float);
        }
        else if (info.nativeFormats & RTAUDIO_SINT32)
        {
            native.setSampleFormat(QAudioFormat::Int32);
        }
        else if (info.nativeFormats & RTAUDIO_SINT16)
        {
            native.setSampleFormat(QAudioFormat::Int16);
        }
        else if (info.nativeFormats & RTAUDIO_SINT24)
        {
            native.setSampleFormat(QAudioFormat::Unknown);
        }
#endif
        else {
            qCritical(logAudio()) << "Cannot find default sample format!";
            return QAudioFormat();
        }

#if RTAUDIO_VERSION_MAJOR < 6
        }
#endif

    return native;
}
