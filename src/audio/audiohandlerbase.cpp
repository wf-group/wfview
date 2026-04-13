// =============================
// audiohandler_base.cpp (fixed names)
// =============================
#include "audiohandlerbase.h"

audioHandlerBase::audioHandlerBase(QObject* parent)
    : QObject(parent)
{
}

audioHandlerBase::~audioHandlerBase()
{
    // Safety net: if dispose() was never called, stop the converter thread
    // now so QThread::~QThread() won't abort.
    if (converterThread) {
        converterThread->quit();
        converterThread->wait();
        delete converterThread;
        converterThread = nullptr;
    }
}


void audioHandlerBase::dispose()
{
    qDebug(logAudio()) << "[SHUTDOWN] dispose() enter, role=" << role()
                       << "onCorrectThread=" << (QThread::currentThread() == thread());
    // Ensure we run on this object's thread (prevent races with audio callbacks)
    if (QThread::currentThread() != thread()) {
        qDebug(logAudio()) << "[SHUTDOWN] dispose() marshaling via BlockingQueuedConnection ...";
        QMetaObject::invokeMethod(this, [this]{ dispose(); },
                                  Qt::BlockingQueuedConnection);
        qDebug(logAudio()) << "[SHUTDOWN] dispose() BlockingQueuedConnection returned";
        return;
    }

    bool expected = false;
    if (!disposed.compare_exchange_strong(expected, true)) {
        qDebug(logAudio()) << "[SHUTDOWN] dispose() already disposed, returning";
        return;
    }

    qDebug(logAudio()) << "[SHUTDOWN] dispose() locking devMutex, role=" << role();
    QMutexLocker lock(&devMutex);
    qDebug(logAudio()) << "[SHUTDOWN] dispose() calling closeDevice(), role=" << role();
    closeDevice();
    qDebug(logAudio()) << "[SHUTDOWN] dispose() closeDevice() done, role=" << role();

    if (underTimer) {
        underTimer->stop();
        underTimer->deleteLater();
        underTimer=nullptr;
    }

    if (converterThread) {
        disconnect(this, nullptr, nullptr, nullptr);
        disconnect(converterThread, nullptr, nullptr, nullptr);
        qDebug(logAudio()) << "[SHUTDOWN] dispose() converterThread->quit(), role=" << role();
        converterThread->quit();
        converterThread->wait();
        qDebug(logAudio()) << "[SHUTDOWN] dispose() converterThread done, role=" << role();
        delete converterThread;
        converterThread = nullptr;
    }
    qDebug(logAudio()) << "[SHUTDOWN] dispose() complete, role=" << role();
}

void audioHandlerBase::reportError(const QString& msg)
{
    qCritical(logAudio()).noquote() << role() << msg;
    emit audioMessage(QString("%1: %2").arg(role(), msg));
}

bool audioHandlerBase::negotiateFormat(int minSampleRate)
{
    nativeFormat = getNativeFormat();

    if (nativeFormat.channelCount() < 1) {
        reportError("Cannot initialize audio device, no channels found");
        return false;
    }

    if (nativeFormat.channelCount() > 2) nativeFormat.setChannelCount(2);

    if (nativeFormat.channelCount() == 1 && radioFormat.channelCount() == 2) {
        nativeFormat.setChannelCount(2);


#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!isFormatSupported(nativeFormat)) nativeFormat.setChannelCount(1);
#else
        if (!isFormatSupported(nativeFormat)) nativeFormat.setChannelCount(1);
#endif
    }

    if (nativeFormat.sampleRate() < minSampleRate) {
        const int prev = nativeFormat.sampleRate();
        nativeFormat.setSampleRate(minSampleRate);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleRate(prev);
#else
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleRate(prev);
#endif
    }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    if (nativeFormat.sampleType() == QAudioFormat::UnSignedInt && nativeFormat.sampleSize() == 8) {
        nativeFormat.setSampleType(QAudioFormat::SignedInt);
        nativeFormat.setSampleSize(16);
        if (!isFormatSupported(nativeFormat)) {
            nativeFormat.setSampleType(QAudioFormat::UnSignedInt);
            nativeFormat.setSampleSize(8);
        }
    }
    if (nativeFormat.sampleSize() == 24) {
        nativeFormat.setSampleSize(32);
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleSize(16);
    }
#else
    if (nativeFormat.sampleFormat() == QAudioFormat::UInt8) {
        nativeFormat.setSampleFormat(QAudioFormat::Int16);
        if (!isFormatSupported(nativeFormat)) nativeFormat.setSampleFormat(QAudioFormat::UInt8);
    }
#endif

    qDebug(logAudio()) << role() << "Selected format: ch=" << nativeFormat.channelCount()
                       << " rate=" << nativeFormat.sampleRate();
    return true;
}

bool audioHandlerBase::init(const audioSetup& setup)
{
    QMutexLocker lock(&devMutex);
    if (initialized) return true;

    setupData = setup;
    radioFormat = toQAudioFormat(setup.codec, setup.sampleRate);

    codec = codecType::LPCM;
    if (setup.codec == 0x01 || setup.codec == 0x20)      codec = codecType::PCMU;
    else if (setup.codec == 0x40 || setup.codec == 0x41) codec = codecType::OPUS;
    else if (setup.codec == 0x80)                        codec = codecType::ADPCM;

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    if (setup.port.isNull() && setup.portInt == -1) {
        reportError("No audio device was found (install Qt Multimedia plugins?)");
        emit initFailed();
        return false;
    }
    deviceInfo = setup.port;
#else
    if (setup.port.isNull() && setup.port.description().isEmpty() && setup.portInt == -1) {
        reportError("Audio device is NULL, check device selection in settings");
        emit initFailed();
        return false;
    } else {
        deviceInfo = setup.port;
    }
#endif

    if (!negotiateFormat(48000)) {
        emit initFailed();
        return false;
    }

    converter   = new audioConverter();
    converterThread  = new QThread(this);
    converterThread->setObjectName(role() == "Input" ? "audioConvIn()" : "audioConvOut()");
    converter->moveToThread(converterThread);

    connect(this, &audioHandlerBase::setupConverter, converter, &audioConverter::init);
    connect(converterThread, &QThread::finished, converter, &QObject::deleteLater);

    converterThread->start(QThread::TimeCriticalPriority);

    underTimer = new QTimer(this);
    underTimer->setSingleShot(true);
    connect(underTimer, &QTimer::timeout, this, &audioHandlerBase::clearUnderrun);

    setVolume(setup.localAFgain);

    if (!openDevice()) {
        reportError("Failed to open device");
        emit initFailed();
        return false;
    }

#ifndef BUILD_WFSERVER
    // Install TX processing hook (input converters only).
    // The hook is invoked after resampling mic→codec rate.
    if (setup.isinput && setup.txProc) {
        TxAudioProcessor* proc = setup.txProc;
        const float sr = static_cast<float>(radioFormat.sampleRate());
        QMetaObject::invokeMethod(converter, [converter=this->converter, proc, sr]{
            converter->setProcessingHook([proc, sr](Eigen::VectorXf s){
                return proc->processAudio(std::move(s), sr);
            });
        }, Qt::QueuedConnection);
    }
#endif

    // Install RX processing hook (output converters only).
    // Uses setPreResampleHook so the hook runs BEFORE channel conversion and
    // resampling.  The samples therefore arrive at the radio's codec rate and
    // channel count — matching the TX sidetone rate — which avoids the 2×-pitch
    // bug that would occur if we mixed 8 kHz sidetone into 48 kHz (post-resample)
    // audio.  Sidetone is mixed inside RxAudioProcessor AFTER noise reduction,
    // ensuring the user's voice is not NR-processed.
#ifndef BUILD_WFSERVER
    if (!setup.isinput && setup.rxProc) {
        RxAudioProcessor* proc = setup.rxProc;
        const float sr = static_cast<float>(radioFormat.sampleRate());
        const int   ch = radioFormat.channelCount();
        QMetaObject::invokeMethod(converter, [converter=this->converter, proc, sr, ch]{
            converter->setPreResampleHook([proc, sr, ch](Eigen::VectorXf s){
                return proc->processAudio(std::move(s), sr, ch);
            });
        }, Qt::QueuedConnection);
    }
#endif

    initialized = true;
    qInfo(logAudio()) << role() << "thread id" << QThread::currentThreadId();
    return true;
}

void audioHandlerBase::start() {
    QMutexLocker lock(&devMutex);
}

void audioHandlerBase::stop()  {
    QMutexLocker lock(&devMutex);
    closeDevice();
}

void audioHandlerBase::setVolume(quint8 volumeIdx) {
    volume = audiopot[volumeIdx];
    //qInfo(logAudio) << "Setting audio volume:" << volume;
}

void audioHandlerBase::changeLatency(quint16 newLatencyMs) {
    QMutexLocker lock(&devMutex);
    setupData.latency = newLatencyMs;
}

void audioHandlerBase::stateChanged(QAudio::State state)
{
    switch (state) {
    case QAudio::IdleState:
        isUnderrun.store(true, std::memory_order_relaxed);
        if (underTimer->isActive()) underTimer->stop();
        break;
    case QAudio::ActiveState:
        if (!underTimer->isActive()) underTimer->start(500);
        break;
    default: break;
    }
}

void audioHandlerBase::clearUnderrun() { isUnderrun.store(false, std::memory_order_relaxed); }


