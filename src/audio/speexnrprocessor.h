// SpeexNrProcessor — streaming noise-reduction wrapper around speexdspmini.
//
// Operates entirely on the converter thread (NOT thread-safe).
// Accepts arbitrary-length float buffers; internally reframes to the
// Speex frame size and converts float ↔ int16.
//
// Latency: one Speex frame (frameMs ms).

#ifndef SPEEXNRPROCESSOR_H
#define SPEEXNRPROCESSOR_H

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

// speexdspmini public API
#include "speex_preprocess.h"   // found via -Isrc/audio/speexdspmini/include
// Internal filterbank header for the preset table
#include "filterbank.h"         // found via -Isrc/audio/speexdspmini/src

class SpeexNrProcessor
{
public:
    // ── Construction / destruction ───────────────────────────────────────────

    SpeexNrProcessor() = default;

    ~SpeexNrProcessor()
    {
        destroyState();
    }

    // Non-copyable
    SpeexNrProcessor(const SpeexNrProcessor&)            = delete;
    SpeexNrProcessor& operator=(const SpeexNrProcessor&) = delete;

    // ── Parameter setters (call before or between process() calls) ───────────

    void setSuppression(int dB)         { m_suppression  = dB;    }
    void setBandsPreset(int p)          { m_bandsPreset  = p;     }
    void setFrameMs(int ms)             { m_frameMs      = ms;    }
    void setAgc(bool en)                { m_agc          = en;    }
    void setAgcLevel(float v)           { m_agcLevel     = v;     }
    void setAgcMaxGain(int dB)          { m_agcMaxGain   = dB;    }
    void setVad(bool en)                { m_vad          = en;    }
    void setVadProbStart(int pct)       { m_vadProbStart = pct;   }  // 0–100
    void setVadProbCont(int pct)        { m_vadProbCont  = pct;   }  // 0–100

    // Tunable time-constants (float, clamped in speex_preprocess_ctl)
    void setSnrDecay(float v)           { m_snrDecay        = v;  }  // 0.0–0.95
    void setNoiseUpdateRate(float v)    { m_noiseUpdateRate = v;  }  // 0.01–0.5
    void setPriorBase(float v)          { m_priorBase       = v;  }  // 0.05–0.5

    // Returns the number of Bark bands active for the current preset.
    // Returns 0 if the state has never been initialised.
    int activeBandCount() const         { return m_activeBands;   }

    // ── Reset (force state recreation on next process call) ──────────────────
    void reset()
    {
        destroyState();
        m_inBuf.clear();
        m_outBuf.clear();
    }

    // ── Latency estimation (samples at current SR) ───────────────────────────
    int latencySamples() const { return m_frameSize; }
    float latencyMs() const
    {
        return (m_sampleRate > 0) ? 1000.0f * m_frameSize / m_sampleRate : 0.0f;
    }

    // ── Processing ───────────────────────────────────────────────────────────
    // Called from the converter thread.
    // Accepts float samples in the range [-1, 1].
    // Returns processed float samples (same count as input).
    std::vector<float> process(const float* in, int nSamples, float sampleRate)
    {
        std::vector<float> out(nSamples, 0.0f);

        // (Re)create state if sample-rate or frame-size changes
        const int newFrameSize = frameSize(sampleRate);
        if (!m_state || sampleRate != m_sampleRate || newFrameSize != m_frameSize) {
            destroyState();
            m_sampleRate = sampleRate;
            m_frameSize  = newFrameSize;
            createState();
        }

        if (!m_state) {
            // Couldn't create state — pass through
            std::copy(in, in + nSamples, out.begin());
            return out;
        }

        // Convert float input to int16 and append to input ring
        for (int i = 0; i < nSamples; ++i) {
            float v = in[i];
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            m_inBuf.push_back(static_cast<int16_t>(v * 32767.0f));
        }

        // Process complete frames
        while (static_cast<int>(m_inBuf.size()) >= m_frameSize) {
            // Copy frame, process in-place
            m_workBuf.assign(m_inBuf.begin(), m_inBuf.begin() + m_frameSize);
            m_inBuf.erase(m_inBuf.begin(), m_inBuf.begin() + m_frameSize);

            speex_preprocess_run(m_state, m_workBuf.data());

            // Append processed int16 to output queue
            for (int16_t s : m_workBuf)
                m_outBuf.push_back(s);
        }

        // Drain output — fill as many of the 'out' samples as we have
        int toDrain = std::min(nSamples, static_cast<int>(m_outBuf.size()));
        for (int i = 0; i < toDrain; ++i)
            out[i] = m_outBuf[i] / 32767.0f;
        if (toDrain > 0)
            m_outBuf.erase(m_outBuf.begin(), m_outBuf.begin() + toDrain);
        // Any remaining out[] samples stay 0 (startup latency fill)

        return out;
    }

    // ── Number of presets available in filterbank.h ──────────────────────────
    static int presetCount()          { return FILTERBANK_NUM_PRESETS; }
    static int bandsForPreset(int p)  { return (p >= 0 && p < FILTERBANK_NUM_PRESETS) ? band_presets[p] : -1; }

private:
    // ── Helpers ──────────────────────────────────────────────────────────────

    int frameSize(float sr) const
    {
        // Round to nearest whole samples; keep even
        int fs = static_cast<int>(sr * m_frameMs / 1000.0f + 0.5f);
        if (fs < 2)  fs = 2;
        if (fs & 1)  ++fs;
        return fs;
    }

    void createState()
    {
        m_state = speex_preprocess_state_init(m_frameSize, static_cast<int>(m_sampleRate));
        if (!m_state) return;

        m_workBuf.resize(m_frameSize);

        int denoise = 1;
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_DENOISE,        &denoise);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &m_suppression);

        int agc = m_agc ? 1 : 0;
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_AGC,            &agc);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_AGC_LEVEL,      &m_agcLevel);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN,   &m_agcMaxGain);

        // Band preset — bounds-checked; out-of-range preset is silently ignored.
        if (m_bandsPreset >= 0 && m_bandsPreset < FILTERBANK_NUM_PRESETS) {
            speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_NB_BANDS, &m_bandsPreset);
        }
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_GET_NB_BANDS, &m_activeBands);

        int vad = m_vad ? 1 : 0;
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_VAD,          &vad);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_PROB_START,    &m_vadProbStart);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_PROB_CONTINUE, &m_vadProbCont);

        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_SNR_DECAY,         &m_snrDecay);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_NOISE_UPDATE_RATE, &m_noiseUpdateRate);
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_PRIOR_BASE,        &m_priorBase);
    }

    void destroyState()
    {
        if (m_state) {
            speex_preprocess_state_destroy(m_state);
            m_state = nullptr;
        }
        m_activeBands = 0;
    }

    // ── State ────────────────────────────────────────────────────────────────
    SpeexPreprocessState* m_state      = nullptr;
    float  m_sampleRate  = 0.0f;
    int    m_frameSize   = 0;
    int    m_activeBands = 0;

    // Configurable parameters
    int    m_suppression    = -30;
    int    m_bandsPreset    =  3;
    int    m_frameMs        = 20;
    bool   m_agc            = false;
    float  m_agcLevel       = 8000.0f;
    int    m_agcMaxGain     = 30;
    bool   m_vad            = false;
    int    m_vadProbStart   = 85;   // Speex default
    int    m_vadProbCont    = 65;   // Speex default
    float  m_snrDecay       = 0.7f;
    float  m_noiseUpdateRate= 0.03f;
    float  m_priorBase      = 0.1f;

    // Sample buffers (int16 domain)
    std::vector<int16_t> m_inBuf;
    std::vector<int16_t> m_outBuf;
    std::vector<int16_t> m_workBuf;
};

#endif // SPEEXNRPROCESSOR_H
