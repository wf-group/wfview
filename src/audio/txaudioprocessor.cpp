// TxAudioProcessor — TX audio processing engine
// Lives on the main thread; processAudio() is called from the converter thread.

#include "txaudioprocessor.h"
#include <cmath>
#include <algorithm>

TxAudioProcessor::TxAudioProcessor(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<Eigen::VectorXf>("Eigen::VectorXf");
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
        m_comp = std::make_unique<DysonCompressor>(sampleRate);
        m_eq   = std::make_unique<MbeqProcessor>(sampleRate);
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

    // ── Input gain ───────────────────────────────────────────────────────────
    applyGainDB(samples, p.inputGainDB);

    // ── Measure input peak ───────────────────────────────────────────────────
    const float inputPeak = samples.array().abs().maxCoeff();
    emit txInputLevel(inputPeak);

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

    return samples;
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

// ─── Helper ───────────────────────────────────────────────────────────────────

void TxAudioProcessor::applyGainDB(Eigen::VectorXf& s, float dB)
{
    if (std::fabs(dB) < 0.01f) return;  // ≈ 0 dB — skip multiply
    const float linear = std::pow(10.0f, dB / 20.0f);
    s *= linear;
}
