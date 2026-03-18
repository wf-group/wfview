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
#include "logcategories.h"
#include <cmath>
#include <algorithm>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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

    // ── Ensure spectrum ring-buffer is initialised before any use ────────────
    // This must happen before the bypass early-return so that the spectrum
    // display works even when the processor has never been un-bypassed.
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
// Mixes buffered sidetone into the RX output, with linear-interpolation
// resampling when the sidetone sample rate differs from the RX stream rate.
// Handles mono and stereo output (sidetone is always mono → duplicated to
// both channels for stereo).

void RxAudioProcessor::mixSidetone(Eigen::VectorXf& samples, int channels,
                                   const Params& /*p*/)
{
    QMutexLocker lk(&m_sidetoneMutex);
    if (m_sidetoneBuf.empty()) return;

    const int n      = static_cast<int>(samples.size());
    const int frames = (channels == 2) ? n / 2 : n;
    const int bufLen = static_cast<int>(m_sidetoneBuf.size());

    // Compute resample ratio: how many sidetone samples per output frame.
    // ratio = 1.0 when rates match (common case — no interpolation needed).
    const double ratio = (m_sidetoneSR > 0 && m_activeSR > 0.0f)
                       ? static_cast<double>(m_sidetoneSR) / static_cast<double>(m_activeSR)
                       : 1.0;

    double srcPos = m_sidetoneResampleFrac;  // carry-over from previous call

    for (int f = 0; f < frames; ++f) {
        const int idx = static_cast<int>(srcPos);
        if (idx >= bufLen) break;  // ran out of sidetone data

        // Linear interpolation between adjacent sidetone samples.
        const double frac = srcPos - idx;
        float st;
        if (idx + 1 < bufLen) {
            st = static_cast<float>(m_sidetoneBuf[idx] * (1.0 - frac)
                                  + m_sidetoneBuf[idx + 1] * frac);
        } else {
            st = m_sidetoneBuf[idx];
        }

        // Mix into output — mono sidetone added to all channels.
        if (channels == 2) {
            const int pair = f * 2;
            samples[pair]     = std::max(-1.0f, std::min(1.0f, samples[pair]     + st));
            samples[pair + 1] = std::max(-1.0f, std::min(1.0f, samples[pair + 1] + st));
        } else {
            samples[f] = std::max(-1.0f, std::min(1.0f, samples[f] + st));
        }

        srcPos += ratio;
    }

    // Consume whole sidetone samples that were fully traversed; carry the
    // fractional remainder for the next call to preserve phase continuity.
    const int consumed = qMin(static_cast<int>(srcPos), bufLen);
    if (consumed > 0)
        m_sidetoneBuf.erase(m_sidetoneBuf.begin(),
                            m_sidetoneBuf.begin() + consumed);
    m_sidetoneResampleFrac = srcPos - consumed;
}

// ─── injectSidetone ──────────────────────────────────────────────────────────
// Called on the TX converter thread via DirectConnection.

void RxAudioProcessor::injectSidetone(Eigen::VectorXf samples, quint32 sampleRate,
                                       int channels, QString format)
{
    if (samples.size() == 0) return;

    QMutexLocker lk(&m_sidetoneMutex);

    // Detect format changes and log diagnostics.
    const bool formatChanged = (sampleRate != m_sidetoneSR)
                            || (channels   != m_sidetoneChannels)
                            || (format     != m_sidetoneFormat);

    if (formatChanged || !m_sidetoneLoggedOnce) {
        const quint32 rxSR = static_cast<quint32>(m_activeSR);
        const double ratio = (m_activeSR > 0.0f) ? static_cast<double>(sampleRate) / m_activeSR : 0.0;
        qDebug(logAudio()) << "RxAudioProcessor::injectSidetone: sidetone="
                           << sampleRate << "Hz" << channels << "ch" << format
                           << "| RX stream=" << rxSR << "Hz" << m_activeChannels << "ch float32"
                           << "| ratio=" << ratio
                           << ((sampleRate != rxSR) ? "** RATE MISMATCH — will resample **" : "");
        m_sidetoneLoggedOnce = true;
    }

    m_sidetoneSR       = sampleRate;
    m_sidetoneChannels = channels;
    m_sidetoneFormat   = format;

    // If format changed, reset resampler state to avoid interpolation across
    // discontinuities.
    if (formatChanged)
        m_sidetoneResampleFrac = 0.0;

    // Cap buffer at ~0.5 s to prevent unbounded growth.
    const int cap = static_cast<int>(sampleRate / 2);
    const int nSamples = static_cast<int>(samples.size());
    const int bufSize  = static_cast<int>(m_sidetoneBuf.size());
    const int room     = qMax(0, cap - bufSize);
    const int toAppend = qMin(nSamples, room);
    if (toAppend > 0)
        m_sidetoneBuf.insert(m_sidetoneBuf.end(),
                             samples.data(), samples.data() + toAppend);
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

// ─── ANR noise profile persistence ───────────────────────────────────────────
// All methods here are main-thread only.  No mutex is needed because
// m_noiseStorePath and m_currentModeName are never touched from the converter
// thread, and AnrNrProcessor's public main-thread API is separately protected.

namespace {
    // Hard limits applied when reading a .noise file.
    constexpr int    kNoiseFileVersion = 1;
    constexpr int    kMaxProfiles      = 64;
    constexpr int    kMaxModeNameLen   = 32;
    constexpr size_t kMaxMeansSize     = 4097u;  // windowSize 8192 → 4097 bins
    constexpr double kMinSampleRate    = 100.0;
    constexpr double kMaxSampleRate    = 384000.0;
    constexpr int    kMinWindowSize    = 256;
    constexpr int    kMaxWindowSize    = 8192;
    constexpr qint64 kMaxFileBytes     = 2 * 1024 * 1024; // 2 MB cap
}

void RxAudioProcessor::setNoiseStorePath(const QString& path)
{
    m_noiseStorePath = path;
    qDebug(logAudio()) << "RxAudioProcessor: ANR noise profile store:" << path;
}

void RxAudioProcessor::setRxMode(const QString& modeName)
{
    if (modeName.isEmpty() || modeName == m_currentModeName)
        return;

    // Save the current profile (if any) before the mode switch.
    if (!m_currentModeName.isEmpty() && m_anr->hasProfile())
        saveCurrentProfile();

    m_currentModeName = modeName;

    // Try to load a saved profile for the incoming mode.
    const bool loaded = loadProfileForMode(modeName);
    if (loaded) {
        auto pb = getAnrNoiseProfileBins();
        if (!pb.bins.isEmpty())
            emit anrNoiseProfileBins(pb.bins, pb.sampleRate, pb.windowSize);
    }
    emit anrModeProfileStatus(modeName, loaded || m_anr->hasProfile());
}

bool RxAudioProcessor::saveCurrentProfile()
{
    if (m_noiseStorePath.isEmpty()) return false;
    if (m_currentModeName.isEmpty()) return false;
    if (!m_anr->hasProfile()) return false;

    const auto profile = m_anr->getNoiseProfile();
    if (profile.means.empty()) {
        qWarning(logAudio()) << "RxAudioProcessor: ANR profile has no means, skipping save:"
                             << m_noiseStorePath;
        return false;
    }

    // Load the existing file so we preserve profiles for other modes.
    QJsonObject root;
    {
        QFile f(m_noiseStorePath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray raw = f.read(kMaxFileBytes);
            f.close();
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            if (doc.isObject())
                root = doc.object();
            // Ignore parse errors — we'll overwrite with a fresh document.
        }
    }

    // Enforce the profile count cap so the file can't grow unboundedly.
    QJsonObject profiles = root["profiles"].toObject();
    if (!profiles.contains(m_currentModeName) && profiles.size() >= kMaxProfiles) {
        qWarning(logAudio()) << "RxAudioProcessor: noise profile store is full ("
                             << kMaxProfiles << " modes), skipping save for mode"
                             << m_currentModeName << m_noiseStorePath;
        return false;
    }

    // Serialise the means array.
    QJsonArray meansArr;
    for (float v : profile.means)
        meansArr.append(static_cast<double>(v));

    QJsonObject entry;
    entry["sampleRate"]   = profile.sampleRate;
    entry["windowSize"]   = static_cast<int>(profile.windowSize);
    entry["totalWindows"] = 1;
    entry["means"]        = meansArr;

    profiles[m_currentModeName] = entry;
    root["version"]  = kNoiseFileVersion;
    root["profiles"] = profiles;

    // Write atomically — QSaveFile writes to a temp file then renames.
    QSaveFile sf(m_noiseStorePath);
    if (!sf.open(QIODevice::WriteOnly)) {
        qWarning(logAudio()) << "RxAudioProcessor: cannot open noise profile file for writing:"
                             << m_noiseStorePath;
        return false;
    }
    sf.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!sf.commit()) {
        qWarning(logAudio()) << "RxAudioProcessor: failed to commit noise profile file:"
                             << m_noiseStorePath;
        return false;
    }

    qDebug(logAudio()) << "RxAudioProcessor: saved ANR noise profile for mode"
                       << m_currentModeName << "to" << m_noiseStorePath;
    return true;
}

bool RxAudioProcessor::loadProfileForMode(const QString& modeName)
{
    if (m_noiseStorePath.isEmpty() || modeName.isEmpty())
        return false;

    // Reject suspicious mode name strings before touching the filesystem.
    if (modeName.size() > kMaxModeNameLen) {
        qWarning(logAudio()) << "RxAudioProcessor: mode name too long, skipping load:"
                             << modeName << m_noiseStorePath;
        return false;
    }
    for (const QChar c : modeName) {
        if (!c.isPrint() || c.unicode() > 127) {
            qWarning(logAudio()) << "RxAudioProcessor: non-ASCII mode name, skipping load:"
                                 << modeName << m_noiseStorePath;
            return false;
        }
    }

    QFile f(m_noiseStorePath);
    if (!f.exists())
        return false;  // No profiles saved yet — normal, not an error.

    if (!f.open(QIODevice::ReadOnly)) {
        qWarning(logAudio()) << "RxAudioProcessor: cannot open noise profile file for reading:"
                             << m_noiseStorePath;
        return false;
    }
    const QByteArray raw = f.read(kMaxFileBytes);
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (!doc.isObject()) {
        qWarning(logAudio()) << "RxAudioProcessor: invalid JSON in noise profile file:"
                             << m_noiseStorePath << "-" << err.errorString();
        return false;
    }

    const QJsonObject root = doc.object();

    if (root["version"].toInt(-1) != kNoiseFileVersion) {
        qWarning(logAudio()) << "RxAudioProcessor: unsupported noise profile file version"
                             << root["version"].toInt() << "in" << m_noiseStorePath;
        return false;
    }

    const QJsonObject allProfiles = root["profiles"].toObject();
    if (!allProfiles.contains(modeName))
        return false;  // No profile for this mode yet — normal.

    const QJsonObject entry = allProfiles[modeName].toObject();

    // Validate each field strictly before touching the ANR engine.
    const double sampleRate   = entry["sampleRate"].toDouble(-1.0);
    const int    windowSize   = entry["windowSize"].toInt(-1);
    const int    totalWindows = entry["totalWindows"].toInt(0);

    if (sampleRate < kMinSampleRate || sampleRate > kMaxSampleRate) {
        qWarning(logAudio()) << "RxAudioProcessor: invalid sampleRate" << sampleRate
                             << "in noise profile for mode" << modeName << m_noiseStorePath;
        return false;
    }
    // windowSize must be in range and a power of two.
    if (windowSize < kMinWindowSize || windowSize > kMaxWindowSize
            || (windowSize & (windowSize - 1)) != 0) {
        qWarning(logAudio()) << "RxAudioProcessor: invalid windowSize" << windowSize
                             << "in noise profile for mode" << modeName << m_noiseStorePath;
        return false;
    }
    if (totalWindows <= 0) {
        qWarning(logAudio()) << "RxAudioProcessor: totalWindows must be > 0 in noise profile for mode"
                             << modeName << m_noiseStorePath;
        return false;
    }

    const QJsonArray meansArr    = entry["means"].toArray();
    const size_t     expectedBins = static_cast<size_t>(windowSize / 2 + 1);

    if (meansArr.isEmpty() || static_cast<size_t>(meansArr.size()) != expectedBins) {
        qWarning(logAudio()) << "RxAudioProcessor: means size mismatch (got"
                             << meansArr.size() << "expected" << expectedBins
                             << ") for mode" << modeName << m_noiseStorePath;
        return false;
    }
    if (expectedBins > kMaxMeansSize) {
        qWarning(logAudio()) << "RxAudioProcessor: means array too large (" << expectedBins
                             << ") for mode" << modeName << m_noiseStorePath;
        return false;
    }

    // Parse and range-check every value before injecting anything.
    std::vector<float> means;
    means.reserve(expectedBins);
    for (const QJsonValue& v : meansArr) {
        if (!v.isDouble()) {
            qWarning(logAudio()) << "RxAudioProcessor: non-numeric value in means array for mode"
                                 << modeName << m_noiseStorePath;
            return false;
        }
        const double d = v.toDouble();
        if (!std::isfinite(d) || d < 0.0 || d > 1e10) {
            qWarning(logAudio()) << "RxAudioProcessor: out-of-range mean value" << d
                                 << "in noise profile for mode" << modeName << m_noiseStorePath;
            return false;
        }
        means.push_back(static_cast<float>(d));
    }

    // All checks passed — restore the profile.
    NoiseReduction::NoiseProfile profile;
    profile.means      = std::move(means);
    profile.sampleRate = sampleRate;
    profile.windowSize = static_cast<size_t>(windowSize);
    m_anr->restoreFromNoiseProfile(profile);

    qDebug(logAudio()) << "RxAudioProcessor: loaded ANR noise profile for mode" << modeName
                       << "from" << m_noiseStorePath
                       << "(SR=" << sampleRate << "windowSize=" << windowSize << ")";
    return true;
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
        // Persist the freshly-built profile for the current mode.
        saveCurrentProfile();
        emit anrModeProfileStatus(m_currentModeName, true);
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
