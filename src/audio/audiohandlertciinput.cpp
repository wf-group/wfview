#include "audiohandlertciinput.h"
#include "tciserver.h"
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
        //connect(this,SIGNAL(setupTxPacket(int)), (tciServer*)setup.tci, SLOT(setupTxPacket(int)));
        //emit setupTxPacket((nativeFormat.bytesForDuration(setup.blockSize * 1000)*2)/sizeof(float));
    } else {
        qCritical(logAudio()) << "***** TCI NOT FOUND *****";
    }

    blockMs = setupData.blockSize > 0 ? int(setupData.blockSize) : 20;
    bytesPerFrame = qMax(1, nativeFormat.bytesPerFrame());
    bytesPerBlock = nativeFormat.bytesForDuration(qint64(blockMs) * 1000);
    prebufferBytes = nativeFormat.bytesForDuration(qint64(prebufferMs) * 1000);
    maxBufferBytes = nativeFormat.bytesForDuration(qint64(qMax(maxBufferMs, prebufferMs + 2 * blockMs)) * 1000);
    pending.clear();
    pending.reserve(maxBufferBytes + bytesPerBlock);
    priming = true;
    blocksSent = 0;
    statsLog.start();

    // This runs in the handler's own thread, so the timer and the queued
    // receiveTCIAudio() calls never run at the same time.
    paceTimer = new QTimer(this);
    paceTimer->setTimerType(Qt::PreciseTimer);
    paceTimer->setInterval(5);
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

    pending.append(packet);

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

    while (blocksSent < due) {
        if (pending.size() < bytesPerBlock) {
            // Ran dry: collect the prebuffer again before restarting.
            ++underruns;
            MCP_AUDIO_DIAG(McpAudioDiag::anomaly(McpAudioDiag::TciTxPaced,
                                                 "underrun, buffering again (value: frames left)",
                                                 double(pending.size() / bytesPerFrame)));
            priming = true;
            isUnderrun.store(true, std::memory_order_relaxed);
            logPacingStats(false);
            return;
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
    }
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
