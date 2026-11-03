#include "CwAudioDecoder.h"

#include "cwdecodercore.h"
#include "cwmultidecoder.h"
#include "rxaudioprocessor.h"

#include <QMetaType>
#include <QThread>
#include <string>

namespace
{
constexpr float followSpanHz = 150.0f; // Follow radio: search the radio's pitch +/- this
constexpr float autoPitchLoHz = 200.0f;
constexpr float autoPitchHiHz = 1200.0f;
constexpr float multiLoHz = 200.0f;
// Above ~2 kHz the band beyond the radio's CW filter is near-silent, which
// lowers the noise floor estimate and lets noise through as stations.
constexpr float multiHiHz = 2000.0f;
constexpr int statsIntervalMs = 250;
constexpr int multiIntervalMs = 300;
constexpr int pitchPollMs = 3000;
constexpr int txFlushMs = 1500; // sent text is shown once sending pauses this long
} // namespace

// ---------------------------------------------------------------------------
// Worker (own QThread)
// ---------------------------------------------------------------------------

CwAudioDecoderWorker::CwAudioDecoderWorker(QObject *parent)
    : QObject(parent)
    , m_core(new CwDecoderCore())
{
    m_statsTimer.start();
    m_multiTimer.start();
}

CwAudioDecoderWorker::~CwAudioDecoderWorker() = default;

void CwAudioDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty())
        return;

    std::string out;
    m_core->feed(mono.constData(), static_cast<int>(mono.size()), sampleRate, out);
    if (!out.empty())
    {
        emit textDecoded(QString::fromLatin1(out.data(), static_cast<int>(out.size())));
    }

    // ---- multi-signal ------------------------------------------------------
    if (m_multi)
    {
        m_multi->feed(mono.constData(), static_cast<int>(mono.size()), sampleRate);
        if (m_multiTimer.elapsed() >= multiIntervalMs)
        {
            m_multiTimer.restart();
            QVariantList list;
            for (const auto &c : m_multi->channels())
            {
                QVariantMap m;
                m.insert(QStringLiteral("id"), c.id);
                m.insert(QStringLiteral("pitch"), static_cast<double>(c.pitchHz));
                m.insert(QStringLiteral("wpm"), static_cast<double>(c.wpm));
                m.insert(QStringLiteral("snr"), static_cast<double>(c.snrDb));
                m.insert(QStringLiteral("active"), c.active);
                m.insert(QStringLiteral("text"), QString::fromLatin1(c.text.data(), static_cast<int>(c.text.size())));
                list.append(m);
            }
            emit multiUpdated(list);
        }
    }

    if (m_statsTimer.elapsed() >= statsIntervalMs)
    {
        m_statsTimer.restart();
        const CwDecoderCore::Stats s = m_core->stats();
        emit statsUpdated(s.pitchHz, s.speedWpm, s.snrDb, s.locked, s.open, m_core->sampleRate());
    }
}

void CwAudioDecoderWorker::setPitchMode(int mode, float fixedHz, float radioHz)
{
    switch (mode)
    {
    case CwAudioDecoder::PitchFixed:
        m_core->setFixedPitch(fixedHz);
        break;
    case CwAudioDecoder::PitchFollowRadio:
        if (radioHz > 0.0f)
        {
            m_core->setTrackRange(radioHz - followSpanHz, radioHz + followSpanHz, radioHz);
        }
        else
        {
            m_core->setTrackRange(autoPitchLoHz, autoPitchHiHz, -1.0f); // radio pitch not known yet
        }
        break;
    default:
        m_core->setTrackRange(autoPitchLoHz, autoPitchHiHz, -1.0f);
        break;
    }
}

void CwAudioDecoderWorker::setSpeedWpm(float wpm)
{
    m_core->setSpeedWpm(wpm);
}
void CwAudioDecoderWorker::setSquelch(bool on)
{
    m_core->setSquelch(on);
}
void CwAudioDecoderWorker::setEngine(int engine)
{
    m_core->setEngine(engine == CwAudioDecoder::EngineHandKey ? CwDecoderCore::Engine::Hand
                                                              : CwDecoderCore::Engine::Machine);
}

void CwAudioDecoderWorker::resetDecoder()
{
    m_core->reset();
}

void CwAudioDecoderWorker::setMulti(bool on)
{
    if (on && !m_multi)
    {
        m_multi.reset(new CwMultiDecoder());
        m_multi->setRange(multiLoHz, multiHiHz);
    }
    else if (!on && m_multi)
    {
        m_multi.reset();
        emit multiUpdated(QVariantList());
    }
}

// ---------------------------------------------------------------------------
// Controller (GUI thread)
// ---------------------------------------------------------------------------

CwAudioDecoder::CwAudioDecoder(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<QVariantList>("QVariantList");

    m_status = tr("Off");

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("cwAudioDecoder"));
    m_worker = new CwAudioDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CwAudioDecoderWorker::textDecoded, this, &CwAudioDecoder::onTextDecoded);
    connect(m_worker, &CwAudioDecoderWorker::statsUpdated, this, &CwAudioDecoder::onStatsUpdated);
    connect(m_worker, &CwAudioDecoderWorker::multiUpdated, this, &CwAudioDecoder::onMultiUpdated);
    m_thread->start(QThread::LowPriority);

    // The radio's CW pitch is not polled, so re-read it while following it.
    m_pitchPoll.setInterval(pitchPollMs);
    connect(&m_pitchPoll,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (m_enabled && m_pitchMode == PitchFollowRadio)
                    emit radioPitchRequested();
            });

    // "Send immediate" sends one character at a time; show one [TX] line once sending pauses.
    m_txTimer.setSingleShot(true);
    m_txTimer.setInterval(txFlushMs);
    connect(&m_txTimer,
            &QTimer::timeout,
            this,
            [this]()
            {
                const QString sent = m_txPending.simplified().toUpper();
                m_txPending.clear();
                if (sent.isEmpty())
                    return;
                appendText((m_atLineStart ? QString() : QStringLiteral("\n")) + QStringLiteral("[TX] ") + sent
                           + QStringLiteral("\n"));
            });

    applyDecodeSettings();
}

CwAudioDecoder::~CwAudioDecoder()
{
    disconnectTap();
    if (m_rxProc)
        m_rxProc->setCwTapEnabled(false);
    m_thread->quit();
    m_thread->wait();
}

void CwAudioDecoder::setRxAudioProcessor(RxAudioProcessor *rxProc)
{
    if (rxProc == m_rxProc.data())
        return;
    disconnectTap();
    if (m_rxProc)
        m_rxProc->setCwTapEnabled(false);
    m_rxProc = rxProc;
    if (m_enabled)
        connectTap();
}

int CwAudioDecoder::pitchMin() const
{
    return static_cast<int>(autoPitchLoHz);
}

int CwAudioDecoder::pitchMax() const
{
    // Covers the multi-signal list and the wider passband of SSB-width filters.
    return static_cast<int>(CwDecoderCore::kMaxFixedPitchHz);
}

void CwAudioDecoder::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit enabledChanged();

    if (on)
    {
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        m_appliedEngine = -1; // resend the engine
        applyDecodeSettings();
        QMetaObject::invokeMethod(m_worker, "setSquelch", Qt::QueuedConnection, Q_ARG(bool, m_squelch));
        QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, m_multiSignal));
        connectTap();
        if (m_pitchMode == PitchFollowRadio)
            emit radioPitchRequested();
        m_pitchPoll.start();
        if (m_rxProc)
            setStatus(tr("Listening..."));
        return;
    }

    m_pitchPoll.stop();
    if (m_rxProc)
        m_rxProc->setCwTapEnabled(false);
    disconnectTap();
    setStatus(tr("Off"));
    if (!m_channels.isEmpty())
    {
        m_channels.clear();
        emit channelsChanged();
    }
}

void CwAudioDecoder::setSquelch(bool on)
{
    if (m_squelch == on)
        return;
    m_squelch = on;
    QMetaObject::invokeMethod(m_worker, "setSquelch", Qt::QueuedConnection, Q_ARG(bool, on));
    emit squelchChanged();
}

void CwAudioDecoder::setEngine(int engine)
{
    if (engine != EngineMachine && engine != EngineHandKey)
        return;
    if (m_engine == engine)
        return;
    m_engine = engine;
    applyDecodeSettings();
    emit engineChanged();
}

void CwAudioDecoder::setMultiSignal(bool on)
{
    if (m_multiSignal == on)
        return;
    m_multiSignal = on;
    QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, on));
    if (!on && !m_channels.isEmpty())
    {
        m_channels.clear();
        emit channelsChanged();
    }
    emit multiSignalChanged();
}

void CwAudioDecoder::setPitchMode(int mode)
{
    if (mode != PitchFollowRadio && mode != PitchAuto && mode != PitchFixed)
        return;
    if (m_pitchMode == mode)
        return;
    m_pitchMode = mode;
    emit pitchModeChanged();
    if (m_pitchMode == PitchFollowRadio)
        emit radioPitchRequested();
    applyDecodeSettings();
}

void CwAudioDecoder::setFixedPitch(int hz)
{
    hz = qBound(pitchMin(), hz, pitchMax());
    if (m_fixedPitch == hz)
        return;
    m_fixedPitch = hz;
    emit fixedPitchChanged();
    applyDecodeSettings();
}

void CwAudioDecoder::setFixedSpeed(bool on)
{
    if (m_fixedSpeed == on)
        return;
    m_fixedSpeed = on;
    emit fixedSpeedChanged();
    applyDecodeSettings();
}

void CwAudioDecoder::setSpeedWpm(int wpm)
{
    wpm = qBound(5, wpm, 60);
    if (m_speedWpm == wpm)
        return;
    m_speedWpm = wpm;
    emit speedWpmChanged();
    applyDecodeSettings();
}

void CwAudioDecoder::resync()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
    applyDecodeSettings();
}

void CwAudioDecoder::decodeChannel(int pitchHz)
{
    if (pitchHz < pitchMin() || pitchHz > pitchMax())
        return;
    if (m_fixedPitch != pitchHz)
    {
        m_fixedPitch = pitchHz;
        emit fixedPitchChanged();
    }
    if (m_pitchMode != PitchFixed)
    {
        m_pitchMode = PitchFixed;
        emit pitchModeChanged();
    }
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
    applyDecodeSettings();
    appendText(tr("\n[now decoding %1 Hz]\n").arg(pitchHz));
}

void CwAudioDecoder::setRadioPitch(int hz)
{
    if (hz < pitchMin() || hz > pitchMax() || hz == m_radioPitchHz)
        return;
    m_radioPitchHz = hz;
    if (m_pitchMode == PitchFollowRadio)
        applyDecodeSettings();
}

void CwAudioDecoder::appendSent(const QString &text)
{
    if (text.isEmpty())
        return;
    m_txPending += text;
    m_txTimer.start();
}

void CwAudioDecoder::connectTap()
{
    if (!m_rxProc)
    {
        setStatus(tr("No RX audio - connect to the radio first."));
        return;
    }
    if (!m_tapConn)
        m_tapConn = connect(m_rxProc.data(),
                            &RxAudioProcessor::cwTapAudio,
                            m_worker,
                            &CwAudioDecoderWorker::pushAudio,
                            Qt::QueuedConnection);
    m_rxProc->setCwTapEnabled(true);
}

void CwAudioDecoder::disconnectTap()
{
    if (!m_tapConn)
        return;
    QObject::disconnect(m_tapConn);
    m_tapConn = QMetaObject::Connection();
}

void CwAudioDecoder::applyDecodeSettings()
{
    // In follow-radio mode the pitch field shows the radio's pitch.
    if (m_pitchMode == PitchFollowRadio && m_radioPitchHz > 0 && m_fixedPitch != m_radioPitchHz)
    {
        m_fixedPitch = m_radioPitchHz;
        emit fixedPitchChanged();
    }
    const int engine = effectiveEngine();
    if (engine != m_appliedEngine)
    {
        m_appliedEngine = engine;
        QMetaObject::invokeMethod(m_worker, "setEngine", Qt::QueuedConnection, Q_ARG(int, engine));
    }
    const float wpm = m_fixedSpeed ? static_cast<float>(m_speedWpm) : -1.0f;
    QMetaObject::invokeMethod(m_worker,
                              "setPitchMode",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_pitchMode),
                              Q_ARG(float, static_cast<float>(m_fixedPitch)),
                              Q_ARG(float, static_cast<float>(m_radioPitchHz)));
    QMetaObject::invokeMethod(m_worker, "setSpeedWpm", Qt::QueuedConnection, Q_ARG(float, wpm));
}

int CwAudioDecoder::effectiveEngine() const
{
    // The Machine engine cannot tell a high pitch from the station it folds onto.
    if (m_engine == EngineMachine && m_pitchMode == PitchFixed && m_fixedPitch > CwDecoderCore::kMaxMachinePitchHz)
        return EngineHandKey;
    return m_engine;
}

void CwAudioDecoder::appendText(const QString &text)
{
    if (text.isEmpty())
        return;
    m_atLineStart = text.endsWith(QLatin1Char('\n'));
    emit textAppended(text);
}

void CwAudioDecoder::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void CwAudioDecoder::onTextDecoded(const QString &text)
{
    if (m_enabled)
        appendText(text);
}

void CwAudioDecoder::onStatsUpdated(
    float pitchHz, float speedWpm, float snrDb, bool locked, bool squelchOpen, float sampleRate)
{
    if (!m_enabled)
        return;

    QString pitchSource;
    switch (m_pitchMode)
    {
    case PitchFollowRadio:
        if (m_radioPitchHz <= 0)
            pitchSource = tr("radio pitch not read yet - searching");
        else if (locked)
            pitchSource = tr("locked, radio %1 Hz").arg(m_radioPitchHz);
        else
            pitchSource = tr("searching near radio %1 Hz").arg(m_radioPitchHz);
        break;
    case PitchFixed:
        pitchSource = tr("fixed");
        break;
    default:
        pitchSource = locked ? tr("locked") : tr("searching");
        break;
    }

    // Shown when Machine is selected but the pitch is above what it can decode.
    const QString engineNote = effectiveEngine() != m_engine
        ? tr("   (Hand key used above %1 Hz)").arg(static_cast<int>(CwDecoderCore::kMaxMachinePitchHz))
        : QString();
    const QString pitchText = pitchHz > 0.0f ? QString::number(qRound(pitchHz)) : QStringLiteral("--");
    setStatus(tr("Pitch %1 Hz (%2)   Speed %3 WPM   Signal %4 dB%5   Audio %6 kHz")
                  .arg(pitchText)
                  .arg(pitchSource)
                  .arg(speedWpm, 0, 'f', 1)
                  .arg(snrDb, 0, 'f', 0)
                  .arg(squelchOpen ? QString() : tr(" (squelched)"))
                  .arg(sampleRate / 1000.0f, 0, 'f', 1)
              + engineNote);
}

void CwAudioDecoder::onMultiUpdated(const QVariantList &channels)
{
    const QVariantList next = (m_multiSignal && m_enabled) ? channels : QVariantList();
    if (next.isEmpty() && m_channels.isEmpty())
        return;
    m_channels = next;
    emit channelsChanged();
}
