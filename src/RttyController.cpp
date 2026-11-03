#include "RttyController.h"

#include "rttytxsource.h"
#include "rxaudioprocessor.h"
#include "txaudioprocessor.h"

#include <QMetaType>
#include <QSettings>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr int statsIntervalMs = 150;
constexpr double historySeconds = 6.0; // audio kept for auto-tune
constexpr double minTuneSeconds = 1.0;
constexpr int txPollMs = 40;
constexpr double prerollSeconds = 0.30; // mark before the first character (PTT settling)
constexpr double tailSeconds = 0.15; // mark after the last character
constexpr qint64 txStartTimeoutMs = 3000;
constexpr int scopeIntervalMs = 66; // ~15 frames per second
constexpr double scopePointsPerSecond = 4500.0;
constexpr size_t scopeMaxPoints = 400; // per frame

double levelDb(double level, double noise)
{
    return 20.0 * std::log10(std::max(level, 1e-12) / std::max(noise, 1e-12));
}

const char *const kDefaultMacroNames[RttyController::kMacroCount]
    = {"CQ", "Answer", "599", "TU 73", "QRZ?", "AGN?", "BTU", "RYRY"};
const char *const kDefaultMacroTexts[RttyController::kMacroCount] = {
    "CQ CQ CQ DE {MYCALL} {MYCALL} {MYCALL} PSE K\n",
    "{CALL} {CALL} DE {MYCALL} {MYCALL} {MYCALL} K\n",
    "{CALL} UR RST 599 599 DE {MYCALL} K\n",
    "{CALL} TU 73 DE {MYCALL} SK\n",
    "QRZ? DE {MYCALL} K\n",
    "AGN? AGN? K\n",
    "BTU {CALL} DE {MYCALL} K\n",
    "RYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRYRY\n",
};
} // namespace

// ---------------------------------------------------------------------------
// Worker (own QThread)
// ---------------------------------------------------------------------------

RttyDecoderWorker::RttyDecoderWorker(QObject *parent)
    : QObject(parent)
{
    m_cfg.sampleRate = 0.0; // set by the first audio block
    m_statsTimer.start();
    m_scopeTimer.start();
}

void RttyDecoderWorker::applyConfig()
{
    if (m_cfg.sampleRate <= 0.0)
        return;
    m_demod.setConfig(m_cfg);
    m_demod.setSquelch(m_squelch);
    m_scope.setConfig(m_cfg.sampleRate, m_cfg.markHz, m_cfg.spaceHz(), m_cfg.baud, m_cfg.shiftHz);
}

void RttyDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty() || sampleRate <= 0.0f)
        return;
    if (m_cfg.sampleRate != sampleRate)
    {
        m_cfg.sampleRate = sampleRate;
        m_history.clear();
        applyConfig();
    }

    const std::string out = m_demod.process(mono.constData(), static_cast<size_t>(mono.size()));
    if (!out.empty())
        emit textDecoded(QString::fromLatin1(out.data(), static_cast<int>(out.size())));

    m_history.insert(m_history.end(), mono.constBegin(), mono.constEnd());
    const size_t keep = static_cast<size_t>(historySeconds * sampleRate);
    if (m_history.size() > 2 * keep)
        m_history.erase(m_history.begin(), m_history.end() - static_cast<std::ptrdiff_t>(keep));

    const size_t step = static_cast<size_t>(std::max(1.0, sampleRate / scopePointsPerSecond));
    m_scope.process(mono.constData(), static_cast<size_t>(mono.size()), step, m_scopePts);
    if (m_scopeTimer.elapsed() >= scopeIntervalMs)
    {
        m_scopeTimer.restart();
        const size_t first = m_scopePts.size() > scopeMaxPoints ? m_scopePts.size() - scopeMaxPoints : 0;
        QVariantList xy;
        xy.reserve(static_cast<int>(2 * (m_scopePts.size() - first)));
        for (size_t i = first; i < m_scopePts.size(); ++i)
        {
            xy.append(static_cast<double>(m_scopePts[i].x));
            xy.append(static_cast<double>(m_scopePts[i].y));
        }
        m_scopePts.clear();
        emit scopeUpdated(xy);
    }

    if (m_statsTimer.elapsed() >= statsIntervalMs)
    {
        m_statsTimer.restart();
        const RttyDemodulator::Stats s = m_demod.stats();
        emit statsUpdated(s.quality,
                          levelDb(s.markLevel, s.noiseLevel),
                          levelDb(s.spaceLevel, s.noiseLevel),
                          s.squelchOpen,
                          m_cfg.sampleRate);
    }
}

void RttyDecoderWorker::setParams(
    double baud, double shiftHz, double markHz, bool reverse, bool usos, bool ita2, double squelch)
{
    m_cfg.baud = baud;
    m_cfg.shiftHz = shiftHz;
    m_cfg.markHz = markHz;
    m_cfg.reverse = reverse;
    m_cfg.usos = usos;
    m_cfg.ita2 = ita2;
    m_squelch = squelch;
    applyConfig();
}

void RttyDecoderWorker::resetDecoder()
{
    if (m_cfg.sampleRate > 0.0)
        m_demod.reset();
}

void RttyDecoderWorker::findTones(double shiftHz)
{
    const size_t keep = static_cast<size_t>(historySeconds * m_cfg.sampleRate);
    const size_t n = std::min(m_history.size(), keep);
    if (m_cfg.sampleRate <= 0.0 || n < static_cast<size_t>(minTuneSeconds * m_cfg.sampleRate))
    {
        emit tonesFound(false, 0.0, 0.0, 0.0);
        return;
    }
    const float *start = m_history.data() + (m_history.size() - n);
    const RttyTonePair p = rttyFindTonePair(start, n, m_cfg.sampleRate, shiftHz);
    emit tonesFound(p.found && p.levelDb >= 6.0, p.lowHz, p.highHz, p.levelDb);
}

// ---------------------------------------------------------------------------
// Controller (GUI thread)
// ---------------------------------------------------------------------------

RttyController::RttyController(QObject *parent)
    : QObject(parent)
    , m_tx(std::make_shared<RttyTxSource>())
{
    qRegisterMetaType<QVector<float>>("QVector<float>");

    for (int i = 0; i < kMacroCount; ++i)
    {
        m_macroNames << QString::fromLatin1(kDefaultMacroNames[i]);
        m_macroTexts << QString::fromLatin1(kDefaultMacroTexts[i]);
    }

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("rttyDecoder"));
    m_worker = new RttyDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RttyDecoderWorker::textDecoded, this, &RttyController::onTextDecoded);
    connect(m_worker, &RttyDecoderWorker::statsUpdated, this, &RttyController::onStatsUpdated);
    connect(m_worker, &RttyDecoderWorker::tonesFound, this, &RttyController::onTonesFound);
    connect(m_worker, &RttyDecoderWorker::scopeUpdated, this, &RttyController::onScopeUpdated);
    m_thread->start(QThread::LowPriority);

    m_txPoll.setInterval(txPollMs);
    connect(&m_txPoll, &QTimer::timeout, this, &RttyController::pollTx);

    m_status = tr("Off");
    applyConfig();
}

RttyController::~RttyController()
{
    disconnectTap();
    if (m_rxProc)
        m_rxProc->setRttyTapEnabled(false);
    m_tx->stop();
    if (m_txProc)
        m_txProc->setAfskSource(nullptr);
    m_thread->quit();
    m_thread->wait();
}

void RttyController::setRxAudioProcessor(RxAudioProcessor *rxProc)
{
    if (rxProc == m_rxProc.data())
        return;
    disconnectTap();
    if (m_rxProc)
        m_rxProc->setRttyTapEnabled(false);
    m_rxProc = rxProc;
    if (m_decoding)
        connectTap();
}

void RttyController::setTxAudioProcessor(TxAudioProcessor *txProc)
{
    if (txProc == m_txProc.data())
        return;
    if (m_txProc)
        m_txProc->setAfskSource(nullptr);
    m_txProc = txProc;
    if (m_txProc)
        m_txProc->setAfskSource(m_tx);
}

// ---- settings --------------------------------------------------------------

void RttyController::loadSettings(QSettings *settings)
{
    m_settings = settings;
    if (!m_settings)
        return;
    m_settings->beginGroup(QStringLiteral("RTTY"));
    m_baud = m_settings->value(QStringLiteral("Baud"), m_baud).toDouble();
    m_shift = m_settings->value(QStringLiteral("Shift"), m_shift).toInt();
    m_markHz = m_settings->value(QStringLiteral("Mark"), m_markHz).toInt();
    m_usb = m_settings->value(QStringLiteral("USB"), m_usb).toBool();
    m_reverse = m_settings->value(QStringLiteral("Reverse"), m_reverse).toBool();
    m_stopBits = m_settings->value(QStringLiteral("StopBits"), m_stopBits).toDouble();
    m_usos = m_settings->value(QStringLiteral("USOS"), m_usos).toBool();
    m_ita2 = m_settings->value(QStringLiteral("ITA2"), m_ita2).toBool();
    m_squelch = m_settings->value(QStringLiteral("Squelch"), m_squelch).toDouble();
    m_diddle = m_settings->value(QStringLiteral("Diddle"), m_diddle).toBool();
    m_txLevel = m_settings->value(QStringLiteral("TxLevel"), m_txLevel).toDouble();
    m_myCall = m_settings->value(QStringLiteral("MyCall"), m_myCall).toString();
    const QStringList names = m_settings->value(QStringLiteral("MacroNames")).toStringList();
    const QStringList texts = m_settings->value(QStringLiteral("MacroTexts")).toStringList();
    if (names.size() == kMacroCount && texts.size() == kMacroCount)
    {
        m_macroNames = names;
        m_macroTexts = texts;
    }
    m_settings->endGroup();

    m_baud = std::clamp(m_baud, 20.0, 300.0);
    m_shift = std::clamp(m_shift, 20, 1000);
    m_markHz = std::clamp(m_markHz, 200, 3500);
    m_squelch = std::clamp(m_squelch, 0.0, 0.95);
    m_txLevel = std::clamp(m_txLevel, 0.0, 1.0);
    applyConfig();
    emit settingsChanged();
    emit macrosChanged();
}

void RttyController::save(const char *key, const QVariant &value)
{
    if (!m_settings)
        return;
    m_settings->beginGroup(QStringLiteral("RTTY"));
    m_settings->setValue(QString::fromLatin1(key), value);
    m_settings->endGroup();
}

void RttyController::saveMacros()
{
    save("MacroNames", m_macroNames);
    save("MacroTexts", m_macroTexts);
}

RttyConfig RttyController::config() const
{
    RttyConfig c;
    c.baud = m_baud;
    c.shiftHz = m_shift;
    c.markHz = m_markHz;
    // USB: mark is the higher audio tone (it is the higher RF frequency), so the
    // space tone lies below it. LSB: the other way round. Reverse flips it.
    c.reverse = (m_usb != m_reverse);
    c.stopBits = m_stopBits;
    c.usos = m_usos;
    c.ita2 = m_ita2;
    return c;
}

int RttyController::spaceHz() const
{
    return static_cast<int>(std::lround(config().spaceHz()));
}

void RttyController::applyConfig()
{
    const RttyConfig c = config();
    QMetaObject::invokeMethod(m_worker,
                              "setParams",
                              Qt::QueuedConnection,
                              Q_ARG(double, c.baud),
                              Q_ARG(double, c.shiftHz),
                              Q_ARG(double, c.markHz),
                              Q_ARG(bool, c.reverse),
                              Q_ARG(bool, c.usos),
                              Q_ARG(bool, c.ita2),
                              Q_ARG(double, m_squelch));
    m_tx->setConfig(c);
    m_tx->setLevel(static_cast<float>(m_txLevel));
    m_tx->setDiddle(m_diddle);
}

#define RTTY_SETTER(Name, member, key, Type)                                                                           \
    void RttyController::set##Name(Type v)                                                                             \
    {                                                                                                                  \
        if (member == v)                                                                                               \
            return;                                                                                                    \
        member = v;                                                                                                    \
        save(key, member);                                                                                             \
        applyConfig();                                                                                                 \
        emit settingsChanged();                                                                                        \
    }

RTTY_SETTER(Usb, m_usb, "USB", bool)
RTTY_SETTER(Reverse, m_reverse, "Reverse", bool)
RTTY_SETTER(Usos, m_usos, "USOS", bool)
RTTY_SETTER(Ita2, m_ita2, "ITA2", bool)
RTTY_SETTER(Diddle, m_diddle, "Diddle", bool)
#undef RTTY_SETTER

void RttyController::setBaud(double baud)
{
    baud = std::clamp(baud, 20.0, 300.0);
    if (std::fabs(baud - m_baud) < 1e-6)
        return;
    m_baud = baud;
    save("Baud", m_baud);
    applyConfig();
    emit settingsChanged();
}

void RttyController::setShift(int hz)
{
    hz = std::clamp(hz, 20, 1000);
    if (hz == m_shift)
        return;
    m_shift = hz;
    save("Shift", m_shift);
    applyConfig();
    emit settingsChanged();
}

void RttyController::setMarkHz(int hz)
{
    hz = std::clamp(hz, 200, 3500);
    if (hz == m_markHz)
        return;
    m_markHz = hz;
    save("Mark", m_markHz);
    applyConfig();
    emit settingsChanged();
}

void RttyController::setStopBits(double bits)
{
    bits = bits < 1.25 ? 1.0 : (bits < 1.75 ? 1.5 : 2.0);
    if (bits == m_stopBits)
        return;
    m_stopBits = bits;
    save("StopBits", m_stopBits);
    applyConfig();
    emit settingsChanged();
}

void RttyController::setSquelch(double q)
{
    q = std::clamp(q, 0.0, 0.95);
    if (std::fabs(q - m_squelch) < 1e-6)
        return;
    m_squelch = q;
    save("Squelch", m_squelch);
    applyConfig();
    emit settingsChanged();
}

void RttyController::setTxLevel(double level)
{
    level = std::clamp(level, 0.0, 1.0);
    if (std::fabs(level - m_txLevel) < 1e-6)
        return;
    m_txLevel = level;
    save("TxLevel", m_txLevel);
    applyConfig();
    emit settingsChanged();
}

void RttyController::setMyCall(const QString &c)
{
    const QString v = c.trimmed().toUpper();
    if (v == m_myCall)
        return;
    m_myCall = v;
    save("MyCall", m_myCall);
    emit settingsChanged();
}

void RttyController::setCall(const QString &c)
{
    const QString v = c.trimmed().toUpper();
    if (v == m_call)
        return;
    m_call = v;
    emit callChanged();
}

void RttyController::setMacro(int index, const QString &name, const QString &text)
{
    if (index < 0 || index >= kMacroCount)
        return;
    m_macroNames[index] = name.trimmed().isEmpty() ? QStringLiteral("M%1").arg(index + 1) : name.trimmed();
    m_macroTexts[index] = text;
    saveMacros();
    emit macrosChanged();
}

// ---- receive ---------------------------------------------------------------

void RttyController::setVisible(bool on)
{
    if (m_visible == on)
        return;
    m_visible = on;
    emit visibleChanged();
    if (!on)
    {
        if (m_keyed)
            release(tr("Window closed"));
        setDecoding(false);
    }
}

void RttyController::setDecoding(bool on)
{
    if (m_decoding == on)
        return;
    m_decoding = on;
    emit decodingChanged();
    if (on)
    {
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        connectTap();
        if (m_rxProc)
            setStatus(tr("Listening..."));
        return;
    }
    if (m_rxProc)
        m_rxProc->setRttyTapEnabled(false);
    disconnectTap();
    m_quality = m_markDb = m_spaceDb = 0.0;
    m_squelchOpen = false;
    emit statsChanged();
    setStatus(tr("Off"));
}

void RttyController::connectTap()
{
    if (!m_rxProc)
    {
        setStatus(tr("No RX audio - connect to the radio first."));
        return;
    }
    if (!m_tapConn)
        m_tapConn = connect(m_rxProc.data(),
                            &RxAudioProcessor::rttyTapAudio,
                            m_worker,
                            &RttyDecoderWorker::pushAudio,
                            Qt::QueuedConnection);
    m_rxProc->setRttyTapEnabled(true);
}

void RttyController::disconnectTap()
{
    if (!m_tapConn)
        return;
    QObject::disconnect(m_tapConn);
    m_tapConn = QMetaObject::Connection();
}

void RttyController::resync()
{
    QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
}

void RttyController::onTextDecoded(const QString &text)
{
    if (m_keyed || !m_decoding || text.isEmpty())
        return; // our own transmission is shown from the TX side
    emit textAppended(text);
    m_atLineStart = text.endsWith(QLatin1Char('\n'));
}

void RttyController::onStatsUpdated(double quality, double markDb, double spaceDb, bool squelchOpen, double sampleRate)
{
    if (!m_decoding)
        return;
    m_quality = quality;
    m_markDb = markDb;
    m_spaceDb = spaceDb;
    m_squelchOpen = squelchOpen;
    m_rxRate = sampleRate;
    emit statsChanged();
    if (!m_keyed && m_status == tr("Listening..."))
        setStatus(tr("Decoding at %1 Hz audio").arg(static_cast<int>(sampleRate)));
}

void RttyController::autoTune()
{
    if (!m_decoding || !m_rxProc)
    {
        setStatus(tr("Auto-tune needs Decode on (it looks at the last few seconds of receive audio)."));
        return;
    }
    setStatus(tr("Looking for a %1 Hz tone pair...").arg(m_shift));
    QMetaObject::invokeMethod(m_worker, "findTones", Qt::QueuedConnection, Q_ARG(double, double(m_shift)));
}

void RttyController::onScopeUpdated(const QVariantList &xy)
{
    if (m_visible && m_decoding && !m_keyed)
        emit scopePoints(xy);
}

void RttyController::onTonesFound(bool found, double lowHz, double highHz, double levelDb)
{
    if (!found)
    {
        setStatus(
            tr("No RTTY tone pair %1 Hz apart found. Tune the signal into the passband and try again.").arg(m_shift));
        return;
    }
    // config().reverse == true means the space tone is below mark, i.e. mark is the higher tone.
    const double mark = config().reverse ? highHz : lowHz;
    setMarkHz(static_cast<int>(std::lround(mark)));
    resync();
    setStatus(tr("Tuned: mark %1 Hz, space %2 Hz (measured shift %3 Hz, %4 dB over noise)")
                  .arg(m_markHz)
                  .arg(spaceHz())
                  .arg(static_cast<int>(std::lround(highHz - lowHz)))
                  .arg(static_cast<int>(std::lround(levelDb))));
}

// ---- transmit --------------------------------------------------------------

void RttyController::setCanTransmit(bool on)
{
    if (m_canTransmit == on)
        return;
    m_canTransmit = on;
    emit canTransmitChanged();
    if (!on && m_keyed)
        release(tr("Transmit not available"));
}

void RttyController::setRadioTransmitting(bool on)
{
    m_radioTx = on;
    if (on && m_keyed)
        m_radioWasTx = true;
}

void RttyController::setHoldTx(bool on)
{
    if (m_holdTx == on)
        return;
    m_holdTx = on;
    emit holdTxChanged();
    if (!on && m_keyed)
        m_autoRelease = true;
}

QString RttyController::expand(const QString &text) const
{
    QString t = text;
    t.replace(QStringLiteral("{MYCALL}"), m_myCall, Qt::CaseInsensitive);
    t.replace(QStringLiteral("{CALL}"), m_call, Qt::CaseInsensitive);
    return t.toUpper();
}

void RttyController::keyUp()
{
    if (m_keyed)
        return;
    if (!m_canTransmit)
    {
        setStatus(tr("This radio cannot transmit from wfview."));
        return;
    }
    if (!m_txProc)
    {
        setStatus(tr("No TX audio - connect to the radio first."));
        return;
    }
    m_tx->start(prerollSeconds);
    m_keyed = true;
    m_tailQueued = false;
    m_radioWasTx = m_radioTx;
    m_keyedTimer.start();
    emit transmittingChanged();
    emit transmitRequested(true);
    m_txPoll.start();
    emit textAppended((m_atLineStart ? QString() : QStringLiteral("\n")) + QStringLiteral("[TX] "));
    m_atLineStart = false;
    setStatus(tr("Transmitting"));
}

void RttyController::release(const QString &why)
{
    if (!m_keyed)
        return;
    const std::string sent = m_tx->takeSent();
    m_tx->stop();
    m_txPoll.stop();
    m_keyed = false;
    m_autoRelease = false;
    emit transmitRequested(false);
    emit transmittingChanged();
    QString tail = QString::fromLatin1(sent.data(), static_cast<int>(sent.size()));
    if (!tail.endsWith(QLatin1Char('\n')))
        tail += QLatin1Char('\n');
    emit textAppended(tail);
    m_atLineStart = true;
    resync();
    setStatus(why.isEmpty() ? (m_decoding ? tr("Listening...") : tr("Off")) : why);
}

void RttyController::send(const QString &text)
{
    const QString t = expand(text);
    if (t.isEmpty())
        return;
    if (!m_keyed)
    {
        keyUp();
        if (!m_keyed)
            return;
        m_autoRelease = !m_holdTx;
    }
    m_tx->queueText(t.toLatin1().toStdString());
    m_tailQueued = false;
}

void RttyController::sendMacro(int index)
{
    if (index < 0 || index >= kMacroCount)
        return;
    QString t = m_macroTexts.at(index);
    if (t.contains(QStringLiteral("{CALL}"), Qt::CaseInsensitive) && m_call.isEmpty())
    {
        setStatus(tr("Enter the other station's call first (the macro uses {CALL})."));
        return;
    }
    if (t.contains(QStringLiteral("{MYCALL}"), Qt::CaseInsensitive) && m_myCall.isEmpty())
    {
        setStatus(tr("Enter your own call in the My call field first (the macro uses {MYCALL})."));
        return;
    }
    send(t);
}

void RttyController::startTx()
{
    if (m_keyed)
    {
        m_autoRelease = false;
        return;
    }
    keyUp();
    m_autoRelease = false;
}

void RttyController::finishTx()
{
    if (m_keyed)
        m_autoRelease = true;
}

void RttyController::abortTx()
{
    release(tr("Transmit aborted"));
}

void RttyController::pollTx()
{
    if (!m_keyed)
    {
        m_txPoll.stop();
        return;
    }
    const std::string sent = m_tx->takeSent();
    if (!sent.empty())
        emit textAppended(QString::fromLatin1(sent.data(), static_cast<int>(sent.size())));

    const qint64 ms = m_keyedTimer.elapsed();
    if (ms > txStartTimeoutMs && m_tx->renderedFrames() == 0)
    {
        release(tr("No TX audio: choose a TX audio input in Settings (any device - RTTY replaces its sound)."));
        return;
    }
    if (ms > txStartTimeoutMs && !m_radioTx)
    {
        release(tr("The radio did not go to transmit (is PTT enabled in Settings?)."));
        return;
    }
    if (m_radioWasTx && !m_radioTx)
    {
        release(tr("Transmit ended by the radio or the PTT timeout."));
        return;
    }
    if (m_autoRelease && m_tx->idle())
    {
        if (!m_tailQueued)
        {
            m_tx->queueTail(tailSeconds);
            m_tailQueued = true;
        }
        else
        {
            release(QString());
        }
    }
}

void RttyController::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}
