// TxAudioProcessor — TX audio processing engine
// Lives on the main thread; processAudio() is called from the converter thread.

#include "txaudioprocessor.h"
#include <cmath>
#include <algorithm>
#include <cstring>

TxAudioProcessor::TxAudioProcessor(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<Eigen::VectorXf>("Eigen::VectorXf");
    qRegisterMetaType<QVector<float>>("QVector<float>");
}

void TxAudioProcessor::setSpectrumEnabled(bool en)
{
    m_specEnabled.store(en, std::memory_order_relaxed);
    // Accumulators are cleared from the converter thread on the next block
    // when it observes the flag as false (avoids cross-thread writes).
}

// ─── processAudio ─────────────────────────────────────────────────────────────
// Called from the audioConverter thread (TimeCriticalPriority).

Eigen::VectorXf TxAudioProcessor::processAudio(Eigen::VectorXf samples, float sampleRate)
{
    if (samples.size() == 0) return samples;

    // ── Snapshot parameters under lock (very brief) ──────────────────────────
    Params p;
    {
        QMutexLocker lk(&m_mutex);
        p = m_params;
    }

    // ── (Re)create plugins if sample rate changed ────────────────────────────
    if (sampleRate != m_activeSR) {
        m_activeSR = sampleRate;
        m_gate = std::make_unique<NoiseGate>(sampleRate);
        m_comp = std::make_unique<DysonCompressor>(sampleRate);
        m_eq   = std::make_unique<MbeqProcessor>(sampleRate);
        // Recalculate spectrum emit threshold and reset accumulators.
        m_specThresh = static_cast<int>(sampleRate / 10.0f);
        m_specInBuf.clear();
        m_specOutBuf.clear();
    }

    // ── Spectrum flag (read once per block) ──────────────────────────────────
    const bool specActive = m_specEnabled.load(std::memory_order_relaxed);
    // If spectrum was disabled, drain stale accumulators (converter thread only).
    if (!specActive && !m_specInBuf.isEmpty()) {
        m_specInBuf.clear();
        m_specOutBuf.clear();
    }

    // ── Master bypass — pass audio through, still meter and sidetone ─────────
    if (p.bypass) {
        const float peak = samples.array().abs().maxCoeff();
        emit txInputLevel(peak);
        emit txOutputLevel(peak);
        emit txGainReduction(1.0f);
        if (p.sidetoneEnabled && m_activeSR > 0.0f) {
            Eigen::VectorXf st = samples * p.sidetoneLevel;
            emit haveSidetoneFloat(std::move(st), static_cast<quint32>(m_activeSR));
        }
        if (specActive)
            appendSpectrumSamples(samples, samples); // input == output in bypass
        return samples;
    }

    // Push EQ band gains into plugin (cheap — just stores values)
    for (int i = 0; i < MbeqProcessor::BANDS; ++i)
        m_eq->setBand(i, p.eqBands[i]);

    // Push compressor parameters
    m_comp->setPeakLimit(p.compPeakLimit);
    m_comp->setReleaseTime(p.compRelease);
    m_comp->setFastRatio(p.compFastRatio);
    m_comp->setSlowRatio(p.compSlowRatio);

    // Push noise gate parameters
    m_gate->setThreshold(p.gateThreshold);
    m_gate->setAttack(p.gateAttack);
    m_gate->setHold(p.gateHold);
    m_gate->setDecay(p.gateDecay);
    m_gate->setRange(p.gateRange);
    m_gate->setLfCutoff(p.gateLfCutoff);
    m_gate->setHfCutoff(p.gateHfCutoff);

    // Snapshot for spectrum input (before input gain — raw microphone signal).
    Eigen::VectorXf specInCapture;
    if (specActive)
        specInCapture = samples;

    // ── Noise gate (before input gain, on raw microphone signal) ─────────────
    if (p.gateEnabled) {
        Eigen::VectorXf tmp(samples.size());
        m_gate->process(samples.data(), tmp.data(), static_cast<unsigned long>(samples.size()));
        samples = std::move(tmp);
    }

    // ── Input gain ───────────────────────────────────────────────────────────
    const float inputPeak = samples.array().abs().maxCoeff();
    emit txInputLevel(inputPeak);

    // ── Measure input peak ───────────────────────────────────────────────────
    applyGainDB(samples, p.inputGainDB);

    // ── DSP processing ───────────────────────────────────────────────────────
    if (p.eqFirst) {
        // EQ → Comp
        if (p.eqEnabled) {
            Eigen::VectorXf tmp(samples.size());
            m_eq->process(samples.data(), tmp.data(), static_cast<int>(samples.size()));
            samples = std::move(tmp);
        }
        if (p.compEnabled) {
            Eigen::VectorXf tmp(samples.size());
            m_comp->process(samples.data(), tmp.data(), static_cast<unsigned long>(samples.size()));
            samples = std::move(tmp);
        }
    } else {
        // Comp → EQ
        if (p.compEnabled) {
            Eigen::VectorXf tmp(samples.size());
            m_comp->process(samples.data(), tmp.data(), static_cast<unsigned long>(samples.size()));
            samples = std::move(tmp);
        }
        if (p.eqEnabled) {
            Eigen::VectorXf tmp(samples.size());
            m_eq->process(samples.data(), tmp.data(), static_cast<int>(samples.size()));
            samples = std::move(tmp);
        }
    }

    // ── Gain reduction meter ─────────────────────────────────────────────────
    if (p.compEnabled)
        emit txGainReduction(m_comp->getLastTotalGain());
    else
        emit txGainReduction(1.0f);

    // ── Output gain ──────────────────────────────────────────────────────────
    applyGainDB(samples, p.outputGainDB);

    // Clip to valid float range
    samples = samples.array().max(-1.0f).min(1.0f);

    // ── Measure output peak ──────────────────────────────────────────────────
    const float outputPeak = samples.array().abs().maxCoeff();
    emit txOutputLevel(outputPeak);

    // ── Sidetone ─────────────────────────────────────────────────────────────
    if (p.sidetoneEnabled && m_activeSR > 0.0f) {
        Eigen::VectorXf st = samples * p.sidetoneLevel;
        emit haveSidetoneFloat(std::move(st), static_cast<quint32>(m_activeSR));
    }

    // ── Spectrum emission ────────────────────────────────────────────────────
    if (specActive)
        appendSpectrumSamples(specInCapture, samples);

    return samples;
}

// ─── appendSpectrumSamples ───────────────────────────────────────────────────
// Converter thread only.  Accumulates samples and emits txSpectrumSamples when
// enough have been collected for one ~30 Hz display frame.

void TxAudioProcessor::appendSpectrumSamples(const Eigen::VectorXf& in,
                                              const Eigen::VectorXf& out)
{
    const int n      = static_cast<int>(in.size());
    const int oldSz  = m_specInBuf.size();

    m_specInBuf.resize(oldSz + n);
    std::memcpy(m_specInBuf.data()  + oldSz, in.data(),  n * sizeof(float));

    m_specOutBuf.resize(oldSz + n);
    std::memcpy(m_specOutBuf.data() + oldSz, out.data(), n * sizeof(float));

    if (m_specThresh > 0 && m_specInBuf.size() >= m_specThresh) {
        emit txSpectrumSamples(m_specInBuf, m_specOutBuf, m_activeSR);
        m_specInBuf.clear();
        m_specOutBuf.clear();
    }
}

// ─── Parameter setters ────────────────────────────────────────────────────────

void TxAudioProcessor::setBypassed(bool bypass)         { QMutexLocker lk(&m_mutex); m_params.bypass        = bypass; }
void TxAudioProcessor::setCompEnabled(bool en)          { QMutexLocker lk(&m_mutex); m_params.compEnabled   = en; }
void TxAudioProcessor::setEqEnabled(bool en)            { QMutexLocker lk(&m_mutex); m_params.eqEnabled     = en; }
void TxAudioProcessor::setEqFirst(bool f)               { QMutexLocker lk(&m_mutex); m_params.eqFirst       = f;  }
void TxAudioProcessor::setInputGainDB(float dB)         { QMutexLocker lk(&m_mutex); m_params.inputGainDB   = dB; }
void TxAudioProcessor::setOutputGainDB(float dB)        { QMutexLocker lk(&m_mutex); m_params.outputGainDB  = dB; }
void TxAudioProcessor::setCompPeakLimit(float dB)       { QMutexLocker lk(&m_mutex); m_params.compPeakLimit = dB; }
void TxAudioProcessor::setCompRelease(float s)          { QMutexLocker lk(&m_mutex); m_params.compRelease   = s;  }
void TxAudioProcessor::setCompFastRatio(float r)        { QMutexLocker lk(&m_mutex); m_params.compFastRatio = r;  }
void TxAudioProcessor::setCompSlowRatio(float r)        { QMutexLocker lk(&m_mutex); m_params.compSlowRatio = r;  }
void TxAudioProcessor::setSidetoneEnabled(bool en)      { QMutexLocker lk(&m_mutex); m_params.sidetoneEnabled = en; }
void TxAudioProcessor::setSidetoneLevel(float lv)       { QMutexLocker lk(&m_mutex); m_params.sidetoneLevel   = lv; }
void TxAudioProcessor::setMuteRx(bool muted)
{
    { QMutexLocker lk(&m_mutex); m_params.muteRx = muted; }
    emit haveRxMuted(muted);
}

void TxAudioProcessor::setEqBand(int idx, float gainDB)
{
    if (idx < 0 || idx >= MbeqProcessor::BANDS) return;
    QMutexLocker lk(&m_mutex);
    m_params.eqBands[idx] = gainDB;
}

void TxAudioProcessor::setGateEnabled(bool en)      { QMutexLocker lk(&m_mutex); m_params.gateEnabled   = en;  }
void TxAudioProcessor::setGateThreshold(float dB)   { QMutexLocker lk(&m_mutex); m_params.gateThreshold = dB;  }
void TxAudioProcessor::setGateAttack(float ms)      { QMutexLocker lk(&m_mutex); m_params.gateAttack    = ms;  }
void TxAudioProcessor::setGateHold(float ms)        { QMutexLocker lk(&m_mutex); m_params.gateHold      = ms;  }
void TxAudioProcessor::setGateDecay(float ms)       { QMutexLocker lk(&m_mutex); m_params.gateDecay     = ms;  }
void TxAudioProcessor::setGateRange(float dB)       { QMutexLocker lk(&m_mutex); m_params.gateRange     = dB;  }
void TxAudioProcessor::setGateLfCutoff(float hz)    { QMutexLocker lk(&m_mutex); m_params.gateLfCutoff  = hz;  }
void TxAudioProcessor::setGateHfCutoff(float hz)    { QMutexLocker lk(&m_mutex); m_params.gateHfCutoff  = hz;  }

// ─── Getters ──────────────────────────────────────────────────────────────────

bool  TxAudioProcessor::bypassed()       const { QMutexLocker lk(&m_mutex); return m_params.bypass;        }
bool  TxAudioProcessor::compEnabled()    const { QMutexLocker lk(&m_mutex); return m_params.compEnabled;   }
bool  TxAudioProcessor::eqEnabled()      const { QMutexLocker lk(&m_mutex); return m_params.eqEnabled;     }
bool  TxAudioProcessor::eqFirst()        const { QMutexLocker lk(&m_mutex); return m_params.eqFirst;       }
float TxAudioProcessor::inputGainDB()    const { QMutexLocker lk(&m_mutex); return m_params.inputGainDB;   }
float TxAudioProcessor::outputGainDB()   const { QMutexLocker lk(&m_mutex); return m_params.outputGainDB;  }
float TxAudioProcessor::compPeakLimit()  const { QMutexLocker lk(&m_mutex); return m_params.compPeakLimit; }
float TxAudioProcessor::compRelease()    const { QMutexLocker lk(&m_mutex); return m_params.compRelease;   }
float TxAudioProcessor::compFastRatio()  const { QMutexLocker lk(&m_mutex); return m_params.compFastRatio; }
float TxAudioProcessor::compSlowRatio()  const { QMutexLocker lk(&m_mutex); return m_params.compSlowRatio; }
bool  TxAudioProcessor::sidetoneEnabled()const { QMutexLocker lk(&m_mutex); return m_params.sidetoneEnabled; }
float TxAudioProcessor::sidetoneLevel()  const { QMutexLocker lk(&m_mutex); return m_params.sidetoneLevel;   }
bool  TxAudioProcessor::muteRx()         const { QMutexLocker lk(&m_mutex); return m_params.muteRx;          }

float TxAudioProcessor::eqBand(int idx) const
{
    if (idx < 0 || idx >= MbeqProcessor::BANDS) return 0.0f;
    QMutexLocker lk(&m_mutex);
    return m_params.eqBands[idx];
}

bool  TxAudioProcessor::gateEnabled()   const { QMutexLocker lk(&m_mutex); return m_params.gateEnabled;   }
float TxAudioProcessor::gateThreshold() const { QMutexLocker lk(&m_mutex); return m_params.gateThreshold; }
float TxAudioProcessor::gateAttack()    const { QMutexLocker lk(&m_mutex); return m_params.gateAttack;    }
float TxAudioProcessor::gateHold()      const { QMutexLocker lk(&m_mutex); return m_params.gateHold;      }
float TxAudioProcessor::gateDecay()     const { QMutexLocker lk(&m_mutex); return m_params.gateDecay;     }
float TxAudioProcessor::gateRange()     const { QMutexLocker lk(&m_mutex); return m_params.gateRange;     }
float TxAudioProcessor::gateLfCutoff()  const { QMutexLocker lk(&m_mutex); return m_params.gateLfCutoff;  }
float TxAudioProcessor::gateHfCutoff()  const { QMutexLocker lk(&m_mutex); return m_params.gateHfCutoff;  }

// ─── Helper ───────────────────────────────────────────────────────────────────

void TxAudioProcessor::applyGainDB(Eigen::VectorXf& s, float dB)
{
    if (std::fabs(dB) < 0.01f) return;  // ≈ 0 dB — skip multiply
    const float linear = std::pow(10.0f, dB / 20.0f);
    s *= linear;
}
