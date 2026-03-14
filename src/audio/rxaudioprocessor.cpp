// RxAudioProcessor — RX audio noise-reduction engine.
//
// NOTE: speexnrprocessor.h must be included BEFORE rxaudioprocessor.h.
// rxaudioprocessor.h pulls in (via prefs.h → audioconverter.h) the wfview
// speex_resampler.h which defines spx_int32_t etc. as preprocessor macros
// under OUTSIDE_SPEEX.  speexnrprocessor.h → speex_preprocess.h →
// speexdsp_config_types.h tries to typedef those same names.  Including
// speexnrprocessor.h first lets the typedef succeed; the subsequent macro
// definition from speex_resampler.h then merely shadows the typedef
// (harmless on LP64 where int32_t == int).

#include "speexnrprocessor.h"   // ← must precede any wfview header
#include "anrnrprocessor.h"
#include "triple_para.h"
#include "rxaudioprocessor.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

extern "C" {
#  include "pocketfft.h"
}

RxAudioProcessor::RxAudioProcessor(QObject* parent)
    : QObject(parent)
    , m_speex(std::make_unique<SpeexNrProcessor>())
    , m_anr(std::make_unique<AnrNrProcessor>())
    , m_eq(std::make_unique<TriplePara>())
{
    qRegisterMetaType<Eigen::VectorXf>("Eigen::VectorXf");
    qRegisterMetaType<QVector<double>>("QVector<double>");
    m_specFftPlan = make_rfft_plan(SPEC_FFT_LEN);
    m_specFftBuf.resize(SPEC_FFT_LEN, 0.0);
}

RxAudioProcessor::~RxAudioProcessor()
{
    if (m_specFftPlan)
        destroy_rfft_plan(m_specFftPlan);
}

void RxAudioProcessor::setSpectrumEnabled(bool en)
{
    m_specEnabled.store(en, std::memory_order_relaxed);
}

void RxAudioProcessor::setSpectrumFps(int fps)
{
    m_specTargetFps.store(qBound(1, fps, 60), std::memory_order_relaxed);
}

// ─── processAudio ────────────────────────────────────────────────────────────
// Called from the converter thread (TimeCriticalPriority).

Eigen::VectorXf RxAudioProcessor::processAudio(Eigen::VectorXf samples,
                                                float sampleRate,
                                                int   channels)
{
    if (samples.size() == 0) return samples;

    // Snapshot parameters
    Params p;
    {
        QMutexLocker lk(&m_mutex);
        p = m_params;
    }

    // Clamp channel count
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;

    // ── Spectrum flag (read once per block) ──────────────────────────────────
    const bool specActive = m_specEnabled.load(std::memory_order_relaxed);
    // Silence ring buffers when spectrum is disabled so stale data won't show
    // on re-enable (converter-thread-only — no locking required).
    if (!specActive && !m_specInRing.empty()) {
        std::fill(m_specInRing.begin(),  m_specInRing.end(),  0.0f);
        std::fill(m_specOutRing.begin(), m_specOutRing.end(), 0.0f);
        m_specFftTrigger = 0;
    }

    // ── Emit raw input level ─────────────────────────────────────────────────
    const float inputPeak = samples.array().abs().maxCoeff();
    emit rxInputLevel(inputPeak);

    // ── Master bypass ────────────────────────────────────────────────────────
    // Channel select still operates during bypass (stereo routing unchanged).
    if (p.bypass) {
        mixSidetone(samples, channels, p);
        emit rxOutputLevel(samples.array().abs().maxCoeff());
        if (specActive)
            appendSpectrumSamples(samples, samples);  // input == output in bypass
        return samples;
    }

    // ── ANR profile collection — feed raw input regardless of active mode ─────
    // This runs on the converter thread as required by AnrNrProcessor::addProfileSamples().
    if (m_anr->isProfiling()) {
        // Use de-interleaved mono for profiling regardless of channel count.
        if (channels == 1) {
            m_anr->addProfileSamples(samples.data(), static_cast<int>(samples.size()));
        } else {
            const int frames = static_cast<int>(samples.size()) / 2;
            for (int i = 0; i < frames; ++i)
                m_anrProfileMono.push_back((samples[i * 2] + samples[i * 2 + 1]) * 0.5f);
            m_anr->addProfileSamples(m_anrProfileMono.data(),
                                     static_cast<int>(m_anrProfileMono.size()));
            m_anrProfileMono.clear();
        }
    }

    // ── Ensure NR processors exist and parameters are up to date ─────────────
    if (sampleRate != m_activeSR || channels != m_activeChannels) {
        m_activeSR       = sampleRate;
        m_activeChannels = channels;
        m_speex->reset();
        m_anr->reset();
        m_eq->setSampleRate(sampleRate);

        // Spectrum ring-buffer + FFT state reset (decimate to ~16 kHz).
        const int K = qMax(1, static_cast<int>(std::round(sampleRate / 16000.0f)));
        m_specDecimFactor = K;
        m_specDecimCount  = 0;
        m_specFftTrigger  = 0;
        m_specRingPos     = 0;
        m_specInRing.assign(SPEC_FFT_LEN, 0.0f);
        m_specOutRing.assign(SPEC_FFT_LEN, 0.0f);
        m_specWindow.resize(SPEC_FFT_LEN);
        for (int i = 0; i < SPEC_FFT_LEN; ++i)
            m_specWindow[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (SPEC_FFT_LEN - 1));

        // Precompute log-bin → linear-bin mapping.
        const double decimSR = static_cast<double>(sampleRate) / K;
        const double binRes  = decimSR / SPEC_FFT_LEN;
        const double decades = std::log10(static_cast<double>(SPEC_FREQ_MAX) / SPEC_FREQ_MIN);
        m_specNumLogBins = static_cast<int>(std::round(decades * SPEC_BINS_PER_DECADE));
        m_specLogBins.resize(m_specNumLogBins);
        m_specLinMag.resize(SPEC_FFT_LEN / 2);
        for (int j = 0; j < m_specNumLogBins; ++j) {
            const double fLo = SPEC_FREQ_MIN * std::pow(10.0, (j - 0.5) / SPEC_BINS_PER_DECADE);
            const double fHi = SPEC_FREQ_MIN * std::pow(10.0, (j + 0.5) / SPEC_BINS_PER_DECADE);
            m_specLogBins[j].linBinLo = static_cast<float>(fLo / binRes);
            m_specLogBins[j].linBinHi = static_cast<float>(fHi / binRes);
        }
    }
    pushSpeexParams(p);
    pushAnrParams(p);
    pushEqParams(p);

    // Capture input snapshot for spectrum BEFORE NR.
    Eigen::VectorXf specInCapture;
    if (specActive)
        specInCapture = samples;

    // ── Channel routing → NR → reassemble ───────────────────────────────────
    const int n = static_cast<int>(samples.size());

    if (channels == 1 || p.channelSelect == 0) {
        // Mono or auto: process entire buffer as mono
        if (p.nrEnabled) {
            const float* in = samples.data();
            std::vector<float> nr = applyNr(in, n, sampleRate, p);
            for (int i = 0; i < n; ++i) samples[i] = nr[i];
        }
    } else {
        // Stereo: de-interleave, process selected channel(s)
        const int frames = n / 2;  // sample pairs
        std::vector<float> left(frames), right(frames);
        for (int i = 0; i < frames; ++i) {
            left[i]  = samples[i * 2];
            right[i] = samples[i * 2 + 1];
        }

        if (p.nrEnabled) {
            if (p.channelSelect == 1) {
                // Process L only
                auto nr = applyNr(left.data(), frames, sampleRate, p);
                left = nr;
            } else if (p.channelSelect == 2) {
                // Process R only
                auto nr = applyNr(right.data(), frames, sampleRate, p);
                right = nr;
            } else {
                // channelSelect == 3: sum to mono, process, write to both
                std::vector<float> mono(frames);
                for (int i = 0; i < frames; ++i)
                    mono[i] = (left[i] + right[i]) * 0.5f;
                auto nr = applyNr(mono.data(), frames, sampleRate, p);
                left = nr; right = nr;
            }
        }

        // Re-interleave
        for (int i = 0; i < frames; ++i) {
            samples[i * 2]     = left[i];
            samples[i * 2 + 1] = right[i];
        }
    }

    // ── Equalizer (after NR, before output gain) ─────────────────────────────
    if (p.eqEnabled) {
        if (channels == 1 || p.channelSelect == 0) {
            // Mono or auto: EQ the whole buffer
            m_eq->process(samples.data(), samples.data(), static_cast<int>(samples.size()));
        } else {
            // Stereo: de-interleave, EQ, re-interleave
            const int frames = static_cast<int>(samples.size()) / 2;
            // Re-use stack vectors (small enough for typical block sizes)
            std::vector<float> left(frames), right(frames);
            for (int i = 0; i < frames; ++i) {
                left[i]  = samples[i * 2];
                right[i] = samples[i * 2 + 1];
            }
            // EQ both channels (same settings)
            m_eq->process(left.data(), left.data(), frames);
            m_eq->process(right.data(), right.data(), frames);
            for (int i = 0; i < frames; ++i) {
                samples[i * 2]     = left[i];
                samples[i * 2 + 1] = right[i];
            }
        }
    }

    // ── Output gain ──────────────────────────────────────────────────────────
    applyGainDB(samples, p.outputGainDB);
    samples = samples.array().max(-1.0f).min(1.0f);

    // ── Mix sidetone (AFTER NR so user's own voice is not processed) ─────────
    mixSidetone(samples, channels, p);

    emit rxOutputLevel(samples.array().abs().maxCoeff());

    // ── Spectrum emission ─────────────────────────────────────────────────────
    if (specActive)
        appendSpectrumSamples(specInCapture, samples);

    return samples;
}

// ─── applyNr ─────────────────────────────────────────────────────────────────
std::vector<float> RxAudioProcessor::applyNr(const float* in, int n,
                                              float sr, const Params& p)
{
    if (p.nrMode == RxNrMode::Speex) {
        return m_speex->process(in, n, sr);
    } else if (p.nrMode == RxNrMode::Anr) {
        return m_anr->process(in, n, sr);
    }
    // RxNrMode::None — pass through (shouldn't reach here if nrEnabled is false)
    return std::vector<float>(in, in + n);
}

// ─── appendSpectrumSamples ───────────────────────────────────────────────────
// Converter thread only.  Mirrors TxAudioProcessor::appendSpectrumSamples.
// Decimates to ~16 kHz, fires Hanning-windowed FFT, then rebins linear
// magnitudes into log-spaced output bins (SPEC_BINS_PER_DECADE per decade).

void RxAudioProcessor::appendSpectrumSamples(const Eigen::VectorXf& in,
                                              const Eigen::VectorXf& out)
{
    if (m_specInRing.empty()) return;  // ring not yet initialised

    static constexpr int    N     = SPEC_FFT_LEN;
    static constexpr int    halfN = N / 2;
    static constexpr double kNorm = N / 4.0;

    const int fps         = m_specTargetFps.load(std::memory_order_relaxed);
    const double decimSR  = static_cast<double>(m_activeSR) / m_specDecimFactor;
    const int triggerEvery = qMax(1, static_cast<int>(std::round(decimSR / fps)));

    // Lambda: window + FFT on one ring buffer → rebin into log-spaced output.
    auto runFFT = [&](const std::vector<float>& ring, QVector<double>& bins) {
        // 1. Window and FFT
        for (int j = 0; j < N; ++j)
            m_specFftBuf[j] = m_specWindow[j]
                              * static_cast<double>(ring[(m_specRingPos + j) % N]);
        rfft_forward(m_specFftPlan, m_specFftBuf.data(), 1.0);

        // 2. Extract normalised linear magnitudes
        m_specLinMag[0] = std::abs(m_specFftBuf[0]) / kNorm;
        for (int k = 1; k < halfN; ++k) {
            const double re = m_specFftBuf[2*k - 1];
            const double im = m_specFftBuf[2*k];
            m_specLinMag[k] = std::sqrt(re*re + im*im) / kNorm;
        }

        // 3. Rebin linear magnitudes into log-spaced bins
        bins.resize(m_specNumLogBins);
        for (int j = 0; j < m_specNumLogBins; ++j) {
            const float lo = m_specLogBins[j].linBinLo;
            const float hi = m_specLogBins[j].linBinHi;
            int iLo = static_cast<int>(std::floor(lo));
            int iHi = static_cast<int>(std::floor(hi));
            iLo = qBound(0, iLo, halfN - 1);
            iHi = qBound(0, iHi, halfN - 1);

            double mag;
            if (iLo == iHi) {
                const float frac = lo - std::floor(lo);
                const int next = qMin(iLo + 1, halfN - 1);
                mag = m_specLinMag[iLo] * (1.0 - frac) + m_specLinMag[next] * frac;
            } else {
                const double wFirst = 1.0 - (lo - std::floor(lo));
                double sum = m_specLinMag[iLo] * wFirst;
                double totalWeight = wFirst;
                for (int k = iLo + 1; k < iHi; ++k) {
                    sum += m_specLinMag[k];
                    totalWeight += 1.0;
                }
                const double wLast = hi - std::floor(hi);
                if (wLast > 0.0) {
                    sum += m_specLinMag[iHi] * wLast;
                    totalWeight += wLast;
                }
                mag = sum / totalWeight;
            }
            bins[j] = 20.0 * std::log10(mag + 1e-10);
        }
    };

    const int n = static_cast<int>(in.size());
    for (int i = 0; i < n; ++i) {
        if (++m_specDecimCount >= m_specDecimFactor) {
            m_specDecimCount = 0;
            m_specInRing [m_specRingPos] = in [i];
            m_specOutRing[m_specRingPos] = out[i];
            m_specRingPos = (m_specRingPos + 1) % N;

            if (++m_specFftTrigger >= triggerEvery) {
                m_specFftTrigger = 0;
                runFFT(m_specInRing,  m_specInBins);
                runFFT(m_specOutRing, m_specOutBins);
                emit rxSpectrumBins(m_specInBins, m_specOutBins, m_activeSR);
            }
        }
    }
}

// ─── mixSidetone ─────────────────────────────────────────────────────────────
void RxAudioProcessor::mixSidetone(Eigen::VectorXf& samples, int channels,
                                   const Params& /*p*/)
{
    QMutexLocker lk(&m_sidetoneMutex);
    if (m_sidetoneBuf.empty()) return;

    const int n   = static_cast<int>(samples.size());
    int toCopy = std::min(n, static_cast<int>(m_sidetoneBuf.size()));

    for (int i = 0; i < toCopy; ++i) {
        float st = m_sidetoneBuf[i];
        if (channels == 2) {
            // Sidetone is mono; add to both channels
            int pair = i * 2;
            if (pair + 1 < n) {
                float v0 = samples[pair]     + st;
                float v1 = samples[pair + 1] + st;
                samples[pair]     = std::max(-1.0f, std::min(1.0f, v0));
                samples[pair + 1] = std::max(-1.0f, std::min(1.0f, v1));
            }
        } else {
            if (i < n) {
                float v = samples[i] + st;
                samples[i] = std::max(-1.0f, std::min(1.0f, v));
            }
        }
    }

    // Consume the used sidetone samples
    const int consumed = (channels == 2) ? toCopy : toCopy;
    if (consumed > 0 && consumed <= static_cast<int>(m_sidetoneBuf.size()))
        m_sidetoneBuf.erase(m_sidetoneBuf.begin(),
                            m_sidetoneBuf.begin() + consumed);
}

// ─── injectSidetone ──────────────────────────────────────────────────────────
// Called on the main thread via QueuedConnection.

void RxAudioProcessor::injectSidetone(Eigen::VectorXf samples, quint32 sampleRate)
{
    if (samples.size() == 0) return;
    QMutexLocker lk(&m_sidetoneMutex);
    m_sidetoneSR = sampleRate;
    // Cap buffer at ~0.5 s to prevent unbounded growth
    const int cap = static_cast<int>(sampleRate / 2);
    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        if (static_cast<int>(m_sidetoneBuf.size()) < cap)
            m_sidetoneBuf.push_back(samples[i]);
    }
}

// ─── ensureProcessors / pushParams ───────────────────────────────────────────

void RxAudioProcessor::pushSpeexParams(const Params& p)
{
    bool changed =
        p.speexSuppression   != m_cachedSpeexSuppress  ||
        p.speexBandsPreset   != m_cachedSpeexBands     ||
        p.speexFrameMs       != m_cachedSpeexFrameMs   ||
        p.speexAgc           != m_cachedAgc            ||
        p.speexAgcLevel      != m_cachedAgcLevel       ||
        p.speexAgcMaxGain    != m_cachedAgcMax         ||
        p.speexVad           != m_cachedVad            ||
        p.speexVadProbStart  != m_cachedVadProbStart   ||
        p.speexVadProbCont   != m_cachedVadProbCont    ||
        p.speexSnrDecay        != m_cachedSnrDecay        ||
        p.speexNoiseUpdateRate != m_cachedNoiseUpdateRate ||
        p.speexPriorBase       != m_cachedPriorBase;

    if (!changed) return;

    m_cachedSpeexSuppress  = p.speexSuppression;
    m_cachedSpeexBands     = p.speexBandsPreset;
    m_cachedSpeexFrameMs   = p.speexFrameMs;
    m_cachedAgc            = p.speexAgc;
    m_cachedAgcLevel       = p.speexAgcLevel;
    m_cachedAgcMax         = p.speexAgcMaxGain;
    m_cachedVad            = p.speexVad;
    m_cachedVadProbStart   = p.speexVadProbStart;
    m_cachedVadProbCont    = p.speexVadProbCont;
    m_cachedSnrDecay       = p.speexSnrDecay;
    m_cachedNoiseUpdateRate= p.speexNoiseUpdateRate;
    m_cachedPriorBase      = p.speexPriorBase;

    m_speex->setSuppression(p.speexSuppression);
    m_speex->setBandsPreset(p.speexBandsPreset);
    m_speex->setFrameMs(p.speexFrameMs);
    m_speex->setAgc(p.speexAgc);
    m_speex->setAgcLevel(p.speexAgcLevel);
    m_speex->setAgcMaxGain(p.speexAgcMaxGain);
    m_speex->setVad(p.speexVad);
    m_speex->setVadProbStart(p.speexVadProbStart);
    m_speex->setVadProbCont(p.speexVadProbCont);
    m_speex->setSnrDecay(p.speexSnrDecay);
    m_speex->setNoiseUpdateRate(p.speexNoiseUpdateRate);
    m_speex->setPriorBase(p.speexPriorBase);
    // Changing key structural params (frame size, bands) requires state reset
    m_speex->reset();
}

void RxAudioProcessor::pushAnrParams(const Params& p)
{
    m_anr->setNoiseReductionDb(p.anrNoiseReductionDb);
    m_anr->setSensitivity(p.anrSensitivity);
    m_anr->setFreqSmoothing(p.anrFreqSmoothing);
}

void RxAudioProcessor::pushEqParams(const Params& p)
{
    for (int i = 0; i < 4; ++i) {
        m_eq->setBandGain(i, p.eqGain[i]);
        m_eq->setBandFreq(i, p.eqFreq[i]);
        if (i == 1 || i == 2)
            m_eq->setBandQ(i, p.eqQ[i]);
        else
            m_eq->setShelfSlope(i, p.eqQ[i]);
    }
}

// ─── ANR profile control ─────────────────────────────────────────────────────

void RxAudioProcessor::startAnrProfile()
{
    m_anr->startProfiling();
}

void RxAudioProcessor::stopAnrProfile()
{
    const bool ok = m_anr->finishProfiling();
    emit anrProfileReady(ok);

    if (ok) {
        auto pb = getAnrNoiseProfileBins();
        if (!pb.bins.isEmpty())
            emit anrNoiseProfileBins(pb.bins, pb.sampleRate, pb.windowSize);
    }
}

RxAudioProcessor::AnrProfileBins RxAudioProcessor::getAnrNoiseProfileBins() const
{
    AnrProfileBins result;
    // m_anr is created once in the constructor — safe to call from main thread.
    auto profile = const_cast<AnrNrProcessor*>(m_anr.get())->getNoiseProfile();
    if (profile.means.empty())
        return result;

    result.sampleRate = profile.sampleRate;
    result.windowSize = static_cast<int>(profile.windowSize);
    result.bins.resize(static_cast<int>(profile.means.size()));
    for (int i = 0; i < result.bins.size(); ++i) {
        double val = static_cast<double>(profile.means[static_cast<size_t>(i)]);
        result.bins[i] = 10.0 * std::log10(val + 1e-20);
    }
    return result;
}

bool RxAudioProcessor::anrIsProfiling() const { return m_anr->isProfiling(); }
bool RxAudioProcessor::anrHasProfile()  const { return m_anr->hasProfile();  }

// ─── Parameter setters ────────────────────────────────────────────────────────

void RxAudioProcessor::setBypassed(bool v)               { QMutexLocker lk(&m_mutex); m_params.bypass         = v; }
void RxAudioProcessor::setNrEnabled(bool v)              { QMutexLocker lk(&m_mutex); m_params.nrEnabled      = v; }
void RxAudioProcessor::setNrMode(RxNrMode v)             { QMutexLocker lk(&m_mutex); m_params.nrMode         = v; }
void RxAudioProcessor::setChannelSelect(int v)           { QMutexLocker lk(&m_mutex); m_params.channelSelect  = v; }
void RxAudioProcessor::setSpeexSuppression(int v)        { QMutexLocker lk(&m_mutex); m_params.speexSuppression= v; }
void RxAudioProcessor::setSpeexBandsPreset(int v)        { QMutexLocker lk(&m_mutex); m_params.speexBandsPreset= v; }
void RxAudioProcessor::setSpeexFrameMs(int v)            { QMutexLocker lk(&m_mutex); m_params.speexFrameMs   = v; }
void RxAudioProcessor::setSpeexAgc(bool v)               { QMutexLocker lk(&m_mutex); m_params.speexAgc       = v; }
void RxAudioProcessor::setSpeexAgcLevel(float v)         { QMutexLocker lk(&m_mutex); m_params.speexAgcLevel  = v; }
void RxAudioProcessor::setSpeexAgcMaxGain(int v)         { QMutexLocker lk(&m_mutex); m_params.speexAgcMaxGain= v; }
void RxAudioProcessor::setSpeexVad(bool v)               { QMutexLocker lk(&m_mutex); m_params.speexVad       = v; }
void RxAudioProcessor::setSpeexVadProbStart(int v)       { QMutexLocker lk(&m_mutex); m_params.speexVadProbStart = v; }
void RxAudioProcessor::setSpeexVadProbCont(int v)        { QMutexLocker lk(&m_mutex); m_params.speexVadProbCont  = v; }
void RxAudioProcessor::setSpeexSnrDecay(float v)         { QMutexLocker lk(&m_mutex); m_params.speexSnrDecay       = v; }
void RxAudioProcessor::setSpeexNoiseUpdateRate(float v)  { QMutexLocker lk(&m_mutex); m_params.speexNoiseUpdateRate= v; }
void RxAudioProcessor::setSpeexPriorBase(float v)        { QMutexLocker lk(&m_mutex); m_params.speexPriorBase      = v; }
void RxAudioProcessor::setAnrNoiseReductionDb(double v)  { QMutexLocker lk(&m_mutex); m_params.anrNoiseReductionDb = v; }
void RxAudioProcessor::setAnrSensitivity(double v)       { QMutexLocker lk(&m_mutex); m_params.anrSensitivity      = v; }
void RxAudioProcessor::setAnrFreqSmoothing(int v)        { QMutexLocker lk(&m_mutex); m_params.anrFreqSmoothing    = v; }
void RxAudioProcessor::setEqEnabled(bool v)              { QMutexLocker lk(&m_mutex); m_params.eqEnabled      = v; }
void RxAudioProcessor::setEqBandGain(int idx, float dB)  { if (idx < 0 || idx >= 4) return; QMutexLocker lk(&m_mutex); m_params.eqGain[idx] = dB; }
void RxAudioProcessor::setEqBandFreq(int idx, float hz)  { if (idx < 0 || idx >= 4) return; QMutexLocker lk(&m_mutex); m_params.eqFreq[idx] = hz; }
void RxAudioProcessor::setEqBandQ(int idx, float q)      { if (idx < 0 || idx >= 4) return; QMutexLocker lk(&m_mutex); m_params.eqQ[idx]    = q; }
void RxAudioProcessor::setOutputGainDB(float v)          { QMutexLocker lk(&m_mutex); m_params.outputGainDB   = v; }

// ─── Getters ─────────────────────────────────────────────────────────────────

bool     RxAudioProcessor::bypassed()      const { QMutexLocker lk(&m_mutex); return m_params.bypass;        }
bool     RxAudioProcessor::nrEnabled()     const { QMutexLocker lk(&m_mutex); return m_params.nrEnabled;     }
RxNrMode RxAudioProcessor::nrMode()        const { QMutexLocker lk(&m_mutex); return m_params.nrMode;        }
int      RxAudioProcessor::channelSelect() const { QMutexLocker lk(&m_mutex); return m_params.channelSelect; }
float    RxAudioProcessor::outputGainDB()  const { QMutexLocker lk(&m_mutex); return m_params.outputGainDB;  }

float RxAudioProcessor::estimatedLatencyMs() const
{
    Params p;
    { QMutexLocker lk(&m_mutex); p = m_params; }
    if (p.bypass || !p.nrEnabled) return 0.0f;
    if (p.nrMode == RxNrMode::Speex) return m_speex->latencyMs();
    return m_anr->latencyMs();
}

int RxAudioProcessor::speexPresetCount()             { return SpeexNrProcessor::presetCount();        }
int RxAudioProcessor::speexBandsForPreset(int p)     { return SpeexNrProcessor::bandsForPreset(p);    }

// ─── Helper ───────────────────────────────────────────────────────────────────

void RxAudioProcessor::applyGainDB(Eigen::VectorXf& s, float dB)
{
    if (std::fabs(dB) < 0.01f) return;
    s *= std::pow(10.0f, dB / 20.0f);
}
