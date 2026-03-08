// RxAudioProcessor — RX audio noise-reduction engine.
//
// NOTE: speexnrprocessor.h must be included BEFORE rxaudioprocessor.h.
// rxaudioprocessor.h pulls in (via prefs.h → audioconverter.h) the wfview
// speex_resampler.h which defines spx_int32_t etc. as preprocessor macros
// under OUTSIDE_SPEEX.  speexnrprocessor.h → speex_preprocess.h →
// speexdsp_config_types.h tries to typedef those same names.  If the macro
// is already defined the typedef text-substitutes to "typedef int32_t int;"
// which is invalid C++.  Including speexnrprocessor.h first lets the typedef
// succeed; the subsequent macro definition from speex_resampler.h then merely
// shadows the typedef (harmless on LP64 where int32_t == int).

#include "speexnrprocessor.h"   // ← must precede any wfview header
#include "spacnrprocessor.h"
#include "rxaudioprocessor.h"
#include <cmath>
#include <algorithm>

RxAudioProcessor::RxAudioProcessor(QObject* parent)
    : QObject(parent)
    , m_speex(std::make_unique<SpeexNrProcessor>())
    , m_spac(std::make_unique<SpacNrProcessor>())
{
    qRegisterMetaType<Eigen::VectorXf>("Eigen::VectorXf");
}

RxAudioProcessor::~RxAudioProcessor() = default;

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

    // ── Emit raw input level ─────────────────────────────────────────────────
    const float inputPeak = samples.array().abs().maxCoeff();
    emit rxInputLevel(inputPeak);

    // ── Master bypass ────────────────────────────────────────────────────────
    // Channel select still operates during bypass (stereo routing unchanged).
    if (p.bypass) {
        mixSidetone(samples, channels, p);
        emit rxOutputLevel(samples.array().abs().maxCoeff());
        return samples;
    }

    // ── Ensure NR processors exist and parameters are up to date ─────────────
    if (sampleRate != m_activeSR || channels != m_activeChannels) {
        m_activeSR       = sampleRate;
        m_activeChannels = channels;
        m_speex->reset();
        m_spac->reset();
    }
    pushSpeexParams(p);
    pushSpacParams(p);

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

    // ── Output gain ──────────────────────────────────────────────────────────
    applyGainDB(samples, p.outputGainDB);
    samples = samples.array().max(-1.0f).min(1.0f);

    // ── Mix sidetone (AFTER NR so user's own voice is not processed) ─────────
    mixSidetone(samples, channels, p);

    emit rxOutputLevel(samples.array().abs().maxCoeff());
    return samples;
}

// ─── applyNr ─────────────────────────────────────────────────────────────────
std::vector<float> RxAudioProcessor::applyNr(const float* in, int n,
                                              float sr, const Params& p)
{
    if (p.nrMode == RxNrMode::Speex) {
        return m_speex->process(in, n, sr);
    } else {
        return m_spac->process(in, n, sr);
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
        p.speexSuppression   != m_cachedSpeexSuppress ||
        p.speexBandsPreset   != m_cachedSpeexBands    ||
        p.speexFrameMs       != m_cachedSpeexFrameMs  ||
        p.speexDereverb      != m_cachedSpeexDereverb ||
        p.speexDereverbLevel != m_cachedDRLevel       ||
        p.speexDereverbDecay != m_cachedDRDecay       ||
        p.speexAgc           != m_cachedAgc           ||
        p.speexAgcLevel      != m_cachedAgcLevel      ||
        p.speexAgcMaxGain    != m_cachedAgcMax;

    if (!changed) return;

    m_cachedSpeexSuppress = p.speexSuppression;
    m_cachedSpeexBands    = p.speexBandsPreset;
    m_cachedSpeexFrameMs  = p.speexFrameMs;
    m_cachedSpeexDereverb = p.speexDereverb;
    m_cachedDRLevel       = p.speexDereverbLevel;
    m_cachedDRDecay       = p.speexDereverbDecay;
    m_cachedAgc           = p.speexAgc;
    m_cachedAgcLevel      = p.speexAgcLevel;
    m_cachedAgcMax        = p.speexAgcMaxGain;

    m_speex->setSuppression(p.speexSuppression);
    m_speex->setBandsPreset(p.speexBandsPreset);
    m_speex->setFrameMs(p.speexFrameMs);
    m_speex->setDereverb(p.speexDereverb);
    m_speex->setDereverbLevel(p.speexDereverbLevel);
    m_speex->setDereverbDecay(p.speexDereverbDecay);
    m_speex->setAgc(p.speexAgc);
    m_speex->setAgcLevel(p.speexAgcLevel);
    m_speex->setAgcMaxGain(p.speexAgcMaxGain);
    // Changing key structural params (frame size, bands) requires state reset
    m_speex->reset();
}

void RxAudioProcessor::pushSpacParams(const Params& p)
{
    m_spac->setVoicingThr(p.spacVoicingThr);
    m_spac->setVoicingFull(p.spacVoicingFull);
    m_spac->setAttenDb(p.spacAttenDb);
    // setFrameMs calls reset() internally if it changes
    m_spac->setFrameMs(p.spacFrameMs);
}

// ─── Parameter setters ────────────────────────────────────────────────────────

void RxAudioProcessor::setBypassed(bool v)               { QMutexLocker lk(&m_mutex); m_params.bypass         = v; }
void RxAudioProcessor::setNrEnabled(bool v)              { QMutexLocker lk(&m_mutex); m_params.nrEnabled      = v; }
void RxAudioProcessor::setNrMode(RxNrMode v)             { QMutexLocker lk(&m_mutex); m_params.nrMode         = v; }
void RxAudioProcessor::setChannelSelect(int v)           { QMutexLocker lk(&m_mutex); m_params.channelSelect  = v; }
void RxAudioProcessor::setSpeexSuppression(int v)        { QMutexLocker lk(&m_mutex); m_params.speexSuppression= v; }
void RxAudioProcessor::setSpeexBandsPreset(int v)        { QMutexLocker lk(&m_mutex); m_params.speexBandsPreset= v; }
void RxAudioProcessor::setSpeexFrameMs(int v)            { QMutexLocker lk(&m_mutex); m_params.speexFrameMs   = v; }
void RxAudioProcessor::setSpeexDereverb(bool v)          { QMutexLocker lk(&m_mutex); m_params.speexDereverb  = v; }
void RxAudioProcessor::setSpeexDereverbLevel(float v)    { QMutexLocker lk(&m_mutex); m_params.speexDereverbLevel = v; }
void RxAudioProcessor::setSpeexDereverbDecay(float v)    { QMutexLocker lk(&m_mutex); m_params.speexDereverbDecay = v; }
void RxAudioProcessor::setSpeexAgc(bool v)               { QMutexLocker lk(&m_mutex); m_params.speexAgc       = v; }
void RxAudioProcessor::setSpeexAgcLevel(float v)         { QMutexLocker lk(&m_mutex); m_params.speexAgcLevel  = v; }
void RxAudioProcessor::setSpeexAgcMaxGain(int v)         { QMutexLocker lk(&m_mutex); m_params.speexAgcMaxGain= v; }
void RxAudioProcessor::setSpacFrameMs(float v)           { QMutexLocker lk(&m_mutex); m_params.spacFrameMs    = v; }
void RxAudioProcessor::setSpacVoicingThr(float v)        { QMutexLocker lk(&m_mutex); m_params.spacVoicingThr = v; }
void RxAudioProcessor::setSpacVoicingFull(float v)       { QMutexLocker lk(&m_mutex); m_params.spacVoicingFull= v; }
void RxAudioProcessor::setSpacAttenDb(float v)           { QMutexLocker lk(&m_mutex); m_params.spacAttenDb    = v; }
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
    return m_spac->latencyMs();
}

int RxAudioProcessor::speexPresetCount()             { return SpeexNrProcessor::presetCount();        }
int RxAudioProcessor::speexBandsForPreset(int p)     { return SpeexNrProcessor::bandsForPreset(p);    }

// ─── Helper ───────────────────────────────────────────────────────────────────

void RxAudioProcessor::applyGainDB(Eigen::VectorXf& s, float dB)
{
    if (std::fabs(dB) < 0.01f) return;
    s *= std::pow(10.0f, dB / 20.0f);
}
