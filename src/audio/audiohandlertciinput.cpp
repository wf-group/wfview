#include "audiohandlertciinput.h"
#include "tciserver.h"
#include "txaudiopacer.h"
#include "mcphooks.h"

bool audioHandlerTciInput::openDevice() noexcept
{

    emit setupConverter(nativeFormat, codecType::LPCM,
                        radioFormat,  codec,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));
    if (setupData.tci != Q_NULLPTR) {
        connect((tciServer*)setupData.tci, SIGNAL(sendTCIAudio(QByteArray)), this, SLOT(receiveTCIAudio(QByteArray)));
        // Same sender and thread as the audio, so it arrives after the last of it.
        connect((tciServer*)setupData.tci, SIGNAL(txAudioEnded()), this, SLOT(endOfInput()));
        //connect(this,SIGNAL(setupTxPacket(int)), (tciServer*)setup.tci, SLOT(setupTxPacket(int)));
        //emit setupTxPacket((nativeFormat.bytesForDuration(setup.blockSize * 1000)*2)/sizeof(float));
    } else {
        qCritical(logAudio()) << "***** TCI NOT FOUND *****";
    }

    blockMs = setupData.blockSize > 0 ? int(setupData.blockSize) : 20;
    bytesPerFrame = qMax(1, nativeFormat.bytesPerFrame());
    channels = qMax(1, nativeFormat.channelCount());
    fadeFrames = nativeFormat.sampleRate() * txAudioFade::fadeMs / 1000;
    bytesPerBlock = nativeFormat.bytesForDuration(qint64(blockMs) * 1000);
    prebufferBytes = nativeFormat.bytesForDuration(qint64(prebufferMs) * 1000);
    maxBufferBytes = nativeFormat.bytesForDuration(qint64(qMax(maxBufferMs, prebufferMs + 2 * blockMs)) * 1000);
    pending.clear();
    pending.reserve(maxBufferBytes + bytesPerBlock);
    priming = true;
    fadeInNext = true;
    inputEnded = false;
    blocksSent = 0;
    statsLog.start();

    // This runs in the handler's own thread, so the timer and the queued
    // receiveTCIAudio() calls never run at the same time.
    // A 1 ms tick sends each block within about 1 ms of its deadline; a 5 ms
    // tick sent about 40% of blocks 15 or 25 ms apart. The handler only exists
    // while TCI transmits, so the extra wakeups cost little.
    paceTimer = new QTimer(this);
    paceTimer->setTimerType(Qt::PreciseTimer);
    paceTimer->setInterval(1);
    connect(paceTimer, &QTimer::timeout, this, &audioHandlerTciInput::pace);
    paceTimer->start();

    qDebug(logAudio()) << "TCI TX audio pacing:" << blockMs << "ms blocks, prebuffer" << prebufferMs
                       << "ms, max" << maxBufferMs << "ms";
    return true;
}

void audioHandlerTciInput::closeDevice() noexcept
{
    if (paceTimer)
        paceTimer->stop();
    logPacingStats(true);
    pending.clear();
}

void audioHandlerTciInput::receiveTCIAudio(const QByteArray packet)
{
    if (packet.isEmpty() || bytesPerBlock <= 0)
        return;

    const int start = int(pending.size());
    pending.append(packet);

    // Audio that follows silence starts wherever its waveform happens to be,
    // most of all at the start of a transmission, when wfview starts taking
    // the client's audio part way through: fade it in.
    if (fadeInNext) {
        const int frames = qMin(fadeFrames, int(packet.size()) / bytesPerFrame);
        txAudioFade::floatFrames(reinterpret_cast<float *>(pending.data() + start), frames, channels, true);
        fadeInNext = false;
    }
    inputEnded = false;

    // Keep the delay bounded: if the client got far ahead, drop the oldest audio.
    if (pending.size() > maxBufferBytes) {
        int drop = pending.size() - maxBufferBytes;
        drop += (bytesPerFrame - drop % bytesPerFrame) % bytesPerFrame; // whole frames only
        pending.remove(0, drop);
        droppedBytes += quint64(drop);
        isOverrun.store(true, std::memory_order_relaxed);
        MCP_AUDIO_DIAG(McpAudioDiag::anomaly(McpAudioDiag::TciTxPaced,
                                             "buffer over the limit, oldest audio dropped (value: frames)",
                                             double(drop / bytesPerFrame)));
        logPacingStats(false);
    }
    reportBusy();
}

void audioHandlerTciInput::endOfInput()
{
    if (bytesPerBlock <= 0 || inputEnded)
        return;
    inputEnded = true;
    if (pending.isEmpty()) {
        fadeInNext = true;
        return;
    }
    // Send everything the client sent, ending in a fade rather than a cut,
    // even if the transmission was too short to fill the prebuffer.
    finishTail();
    if (priming) {
        priming = false;
        isUnderrun.store(false, std::memory_order_relaxed);
        paceClock.start();
        blocksSent = 0;
    }
    reportBusy();
}

void audioHandlerTciInput::finishTail()
{
    // Fade out the last of the audio and pad it with silence to whole blocks.
    const int frames = int(pending.size()) / bytesPerFrame;
    pending.truncate(frames * bytesPerFrame);
    const int n = qMin(fadeFrames, frames);
    txAudioFade::floatFrames(reinterpret_cast<float *>(pending.data()) + qsizetype(frames - n) * channels,
                             n, channels, false);
    const int partial = int(pending.size()) % bytesPerBlock;
    if (partial)
        pending.append(QByteArray(bytesPerBlock - partial, char(0)));
    fadeInNext = true;
}

void audioHandlerTciInput::reportBusy()
{
    // When the last audio held here will have been passed on (see txAudioDrain).
    const qint64 now = txAudioDrain::nowMs();
    qint64 last = now;
    if (!pending.isEmpty()) {
        const qint64 blocks = (pending.size() + bytesPerBlock - 1) / bytesPerBlock;
        const qint64 first = priming
            ? now + blockMs // waiting for more audio, or for the end of it
            : now + qMax<qint64>(0, (qint64(blocksSent) * blockMs * 1000000ll - paceClock.nsecsElapsed()) / 1000000);
        last = first + (blocks - 1) * blockMs;
    }
    txAudioDrain::report(txAudioDrain::TciInput, last);
}

void audioHandlerTciInput::pace()
{
    if (bytesPerBlock <= 0)
        return;

    if (priming) {
        if (pending.size() < prebufferBytes)
            return;
        priming = false;
        isUnderrun.store(false, std::memory_order_relaxed);
        paceClock.start();
        blocksSent = 0;
    }

    // Block deadlines come from the elapsed time, so the output rate does not
    // drift when a timer tick is late. The first block goes out straight away.
    const quint64 due = quint64(paceClock.nsecsElapsed()) / (quint64(blockMs) * 1000000ull) + 1;
    if (due > blocksSent + maxCatchUpBlocks) {
        // This thread was held up. Send a few blocks now, not the whole backlog.
        ++lateResyncs;
        MCP_AUDIO_DIAG(McpAudioDiag::anomaly(McpAudioDiag::TciTxPaced,
                                             "pacer timer late, skipped ahead (value: blocks)",
                                             double(due - blocksSent - maxCatchUpBlocks)));
        blocksSent = due - maxCatchUpBlocks;
        logPacingStats(false);
    }

    bool sent = false;
    while (blocksSent < due) {
        if (pending.size() < bytesPerBlock) {
            if (!pending.isEmpty()) {
                // Ran out part way through a block: send the rest, faded out,
                // on time. Audio that comes later is faded in.
                if (!inputEnded)
                    MCP_AUDIO_DIAG(McpAudioDiag::anomaly(McpAudioDiag::TciTxPaced,
                                                         "underrun, partial block faded out (value: frames left)",
                                                         double(pending.size() / bytesPerFrame)));
                finishTail();
                continue;
            }
            // Ran dry: collect the prebuffer again before restarting.
            if (!inputEnded) {
                ++underruns;
                MCP_AUDIO_DIAG(McpAudioDiag::anomaly(McpAudioDiag::TciTxPaced,
                                                     "underrun, buffering again", 0.0));
                isUnderrun.store(true, std::memory_order_relaxed);
                logPacingStats(false);
            }
            priming = true;
            fadeInNext = true;
            break;
        }

        audioPacket pkt;
        pkt.time   = QTime::currentTime();
        pkt.sent   = 0;
        pkt.volume = volume;
        std::memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data   = pending.left(bytesPerBlock);
        pending.remove(0, bytesPerBlock);
        MCP_AUDIO_DIAG({
            McpAudioDiag::event(McpAudioDiag::TciTxPaced, qint64(pending.size() / bytesPerFrame));
            McpAudioDiag::continuityFloat(McpAudioDiag::TciTxPaced,
                                          reinterpret_cast<const float *>(pkt.data.constData()),
                                          int(pkt.data.size() / int(sizeof(float))),
                                          nativeFormat.channelCount());
        });
        emit sendToConverter(pkt);
        ++blocksSent;
        sent = true;
    }
    if (sent)
        reportBusy();
}

void audioHandlerTciInput::logPacingStats(bool force)
{
    if (!force && statsLog.isValid() && statsLog.elapsed() < 5000)
        return;
    if (underruns || droppedBytes || lateResyncs) {
        qDebug(logAudio()) << "TCI TX audio pacing: underruns" << underruns
                           << "dropped" << (bytesPerFrame > 0 ? droppedBytes / quint64(bytesPerFrame) : 0) << "frames"
                           << "late resyncs" << lateResyncs
                           << "buffered" << (bytesPerFrame > 0 ? pending.size() / bytesPerFrame : 0) << "frames";
        underruns = 0;
        droppedBytes = 0;
        lateResyncs = 0;
    }
    statsLog.restart();
}

void audioHandlerTciInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;

    MCP_AUDIO_DIAG({
        McpAudioDiag::event(McpAudioDiag::TciTxConverted, audio.data.size());
        // Converted to the radio's codec; only plain 16-bit PCM can be checked.
        if (codec == codecType::LPCM && radioFormat.bytesPerFrame() == 2 * radioFormat.channelCount())
            McpAudioDiag::continuityInt16(McpAudioDiag::TciTxConverted, audio.data.constData(),
                                          int(audio.data.size()), radioFormat.channelCount());
    });

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


QAudioFormat audioHandlerTciInput::getNativeFormat()
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

bool audioHandlerTciInput::isFormatSupported(QAudioFormat f)
{
    Q_UNUSED(f)
    return true;
}
