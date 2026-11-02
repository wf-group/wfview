// cwdecodercore.h
//
// Qt-free wrapper around ggmorse (MIT, Georgi Gerganov) used by the wfview
// CW decoder. Feed it mono float audio at any sample rate; it
// buffers the samples, runs ggmorse, and returns decoded characters.
//
// Why a separate pitch tracker: ggmorse's own auto-pitch re-estimates the
// tone every frame and restarts the current letter whenever its estimate
// jumps by more than 50 Hz, which garbles copy on QSB/QRM. This tracker
// averages a spectrum over ~1 s, needs a new peak to persist before it
// moves, and hands ggmorse a steady pitch. It also gives a signal-to-noise
// figure used for the squelch (ggmorse prints random E/T/I on noise).
//
// Not thread-safe: use from a single thread (the decoder worker thread).

#pragma once

#include "cwhandkeydecoder.h"
#include "ggmorse/ggmorse.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

class CwDecoderCore
{
public:
    struct Stats
    {
        float pitchHz = 0.0f; // pitch ggmorse is decoding at
        float speedWpm = 0.0f;
        float threshold = 0.0f;
        float snrDb = 0.0f; // tracker peak / median, dB
        bool locked = false; // tracker has a steady carrier
        bool open = false; // squelch open
    };

    // ---- pitch control -----------------------------------------------------
    // Fixed pitch: no tracking.
    void setFixedPitch(float hz)
    {
        m_mode = Mode::Fixed;
        m_fixedHz = hz;
        m_paramsDirty = true;
    }

    // Track the strongest steady tone in [lo, hi]. 'fallbackHz' is used until
    // a tone is found (<= 0: let ggmorse search the whole range itself).
    void setTrackRange(float lo, float hi, float fallbackHz)
    {
        lo = std::max(150.0f, lo);
        hi = std::min(1500.0f, hi);
        const bool rangeChanged
            = (m_mode != Mode::Track) || std::fabs(lo - m_trackLo) > 1.0f || std::fabs(hi - m_trackHi) > 1.0f;
        m_mode = Mode::Track;
        m_trackLo = lo;
        m_trackHi = hi;
        m_fallbackHz = fallbackHz;
        if (rangeChanged)
            resetTracker();
        m_paramsDirty = true;
    }

    // Decoding engine: Machine = ggmorse (best on keyer/computer CW),
    // Hand = CwHandKeyDecoder (adapts to a sender's own timing).
    enum class Engine
    {
        Machine,
        Hand
    };
    void setEngine(Engine e)
    {
        if (e == m_engine)
            return;
        m_engine = e;
        m_hand.reset();
        m_handDelay.clear();
        m_paramsDirty = true;
    }
    Engine engine() const
    {
        return m_engine;
    }

    // <= 0 means "auto detect".
    void setSpeedWpm(float wpm)
    {
        if (wpm != m_speedWpm)
        {
            m_speedWpm = wpm;
            m_paramsDirty = true;
        }
    }

    // Suppress decoded text while no steady tone is present.
    void setSquelch(bool on)
    {
        m_squelch = on;
    }

    void reset()
    {
        m_gg.reset();
        m_sampleRate = 0.0f;
        m_buf.clear();
        m_readPos = 0;
        m_paramsDirty = true;
        resetTracker();
        m_hand.reset();
        m_handDelay.clear();
    }

    // Append samples and decode whatever full frames are available.
    // Decoded characters are appended to 'out'. Returns number appended.
    int feed(const float *samples, int n, float sampleRate, std::string &out)
    {
        if (n <= 0 || sampleRate < GGMorse::kBaseSampleRate)
            return 0;

        if (!m_gg || sampleRate != m_sampleRate)
        {
            create(sampleRate);
        }

        trackPitch(samples, n);

        const float want = decodePitch();

        if (m_engine == Engine::Hand)
        {
            // Run the hand-key engine ~0.75 s behind the pitch tracker, so the
            // tone gate is already open when the first dit of a call arrives.
            m_handDelay.insert(m_handDelay.end(), samples, samples + n);
            const size_t delayN = static_cast<size_t>(sampleRate * 0.75f);
            std::string raw;
            if (m_handDelay.size() > delayN)
            {
                const size_t k = m_handDelay.size() - delayN;
                m_hand.process(m_handDelay.data(), static_cast<int>(k), sampleRate, want, toneGate(), raw);
                m_handDelay.erase(m_handDelay.begin(), m_handDelay.begin() + static_cast<std::ptrdiff_t>(k));
            }
            return emitText(raw.data(), static_cast<int>(raw.size()), out);
        }

        if (m_paramsDirty || std::fabs(want - m_appliedPitch) > 2.0f)
        {
            applyParams(want);
        }

        m_buf.insert(m_buf.end(), samples, samples + n);

        // Never fall more than ~4 s behind (e.g. if the worker was starved).
        const size_t cap = static_cast<size_t>(sampleRate * 4.0f);
        if (m_buf.size() - m_readPos > cap)
        {
            m_readPos = m_buf.size() - cap;
        }

        m_gg->decode(
            [this](void *data, uint32_t nMaxBytes) -> uint32_t
            {
                const size_t wantN = nMaxBytes / sizeof(float);
                if (wantN == 0 || m_buf.size() - m_readPos < wantN)
                    return 0;
                std::memcpy(data, m_buf.data() + m_readPos, wantN * sizeof(float));
                m_readPos += wantN;
                return static_cast<uint32_t>(wantN * sizeof(float));
            });

        if (m_readPos > 0)
        {
            m_buf.erase(m_buf.begin(), m_buf.begin() + static_cast<std::ptrdiff_t>(m_readPos));
            m_readPos = 0;
        }

        GGMorse::TxRx rx;
        const int got = m_gg->takeRxData(rx);
        return emitText(reinterpret_cast<const char *>(rx.data()), got, out);
    }

    // Shared output filter: squelch, collapse repeated newlines/spaces.
    int emitText(const char *txt, int got, std::string &out)
    {
        const bool open = squelchOpen();
        int appended = 0;
        for (int i = 0; i < got; ++i)
        {
            const char c = txt[i];
            if (c == '\n')
            {
                // ggmorse emits '\n' whenever the pitch it uses jumps.
                if (m_lastWasNewline)
                    continue;
                m_lastWasNewline = true;
            }
            else
            {
                if (!open)
                    continue;
                if (c == ' ' && m_lastWasSpace)
                    continue;
                m_lastWasNewline = false;
            }
            m_lastWasSpace = (c == ' ');
            out.push_back(c);
            ++appended;
        }
        return appended;
    }

    Stats stats() const
    {
        Stats s;
        if (m_gg)
        {
            const auto &st = m_gg->getStatistics();
            s.pitchHz = st.estimatedPitch_Hz;
            s.speedWpm = st.estimatedSpeed_wpm;
            s.threshold = st.signalThreshold;
        }
        if (m_engine == Engine::Hand)
        {
            s.speedWpm = m_hand.speedWpm();
            s.pitchHz = decodePitch();
        }
        s.snrDb = m_snrDb;
        s.locked = (m_trackHz > 0.0f);
        s.open = squelchOpen();
        return s;
    }

    float sampleRate() const
    {
        return m_sampleRate;
    }

    // Tuning constants (public so the offline test can print them).
    static constexpr float kBlockSec = 0.25f; // tracker analysis block
    static constexpr float kStepHz = 5.0f; // tracker bin spacing
    static constexpr float kLockSnrDb = 7.0f; // tone must beat median by this
                                              // (7 dB copies weak signals; noise alone
                                              // still stays silent)
    static constexpr float kMaxFixedPitchHz = 3000.0f; // highest fixed pitch
    // ggmorse works at 4 kHz: above this pitch another station folds onto it,
    // so the Machine engine is only used up to here.
    static constexpr float kMaxMachinePitchHz = 1900.0f;
    static constexpr float kNearHz = 30.0f; // within this: slew, no re-lock
    static constexpr int kMoveBlocks = 4; // new peak must persist (x 0.25 s)
    static constexpr float kSquelchHoldSec = 2.0f; // keep squelch open after tone fades

private:
    enum class Mode
    {
        Fixed,
        Track
    };

    void create(float sampleRate)
    {
        GGMorse::Parameters p = GGMorse::getDefaultParameters();
        p.sampleRateInp = sampleRate;
        p.sampleRateOut = GGMorse::kBaseSampleRate;
        p.samplesPerFrame = GGMorse::kDefaultSamplesPerFrame;
        p.sampleFormatInp = GGMORSE_SAMPLE_FORMAT_F32;
        p.sampleFormatOut = GGMORSE_SAMPLE_FORMAT_F32;
        m_gg.reset(new GGMorse(p));
        m_sampleRate = sampleRate;
        m_buf.clear();
        m_readPos = 0;
        m_paramsDirty = true;
        resetTracker();
    }

    float decodePitch() const
    {
        if (m_mode == Mode::Fixed)
            return (m_fixedHz > 0.0f) ? m_fixedHz : -1.0f;
        if (m_trackHz > 0.0f)
            return m_trackHz;
        return (m_fallbackHz > 0.0f) ? m_fallbackHz : -1.0f;
    }

    void applyParams(float pitchHz)
    {
        GGMorse::ParametersDecode d = GGMorse::getDefaultParametersDecode();
        d.frequency_hz = pitchHz;
        d.speed_wpm = (m_speedWpm > 0.0f) ? m_speedWpm : -1.0f;
        d.frequencyRangeMin_hz = 200.0f;
        d.frequencyRangeMax_hz = 1200.0f;
        d.applyFilterHighPass = true;
        d.applyFilterLowPass = true;
        m_gg->setParametersDecode(d);
        m_appliedPitch = pitchHz;
        m_paramsDirty = false;
    }

    // ---- tracker -------------------------------------------------------------

    void resetTracker()
    {
        m_decim = 0;
        m_decimAcc = 0.0f;
        m_decimCount = 0;
        m_block.clear();
        m_avgPow.clear();
        m_trackHz = 0.0f;
        m_candHz = 0.0f;
        m_candCount = 0;
        m_snrDb = 0.0f;
        m_secSinceTone = 1e9f;
    }

    // A steady tone is present (independent of the Squelch checkbox).
    bool toneGate() const
    {
        if (m_mode == Mode::Fixed)
            return m_secSinceTone <= kSquelchHoldSec;
        return m_trackHz > 0.0f && m_secSinceTone <= kSquelchHoldSec;
    }

    bool squelchOpen() const
    {
        if (!m_squelch)
            return true;
        if (m_mode == Mode::Fixed)
        {
            // No tracking in Fixed mode: open on a strong tone near the fixed pitch.
            return m_secSinceTone <= kSquelchHoldSec;
        }
        return m_trackHz > 0.0f && m_secSinceTone <= kSquelchHoldSec;
    }

    void trackPitch(const float *s, int n)
    {
        // Box-average decimation to ~8 kHz; plenty for tones <= 1.5 kHz.
        if (m_decim == 0)
        {
            m_decim = std::max(1, static_cast<int>(m_sampleRate / 8000.0f));
            m_trackSr = m_sampleRate / static_cast<float>(m_decim);
            m_blockLen = static_cast<int>(m_trackSr * kBlockSec);
        }
        for (int i = 0; i < n; ++i)
        {
            m_decimAcc += s[i];
            if (++m_decimCount >= m_decim)
            {
                m_block.push_back(m_decimAcc / static_cast<float>(m_decim));
                m_decimAcc = 0.0f;
                m_decimCount = 0;
                if (static_cast<int>(m_block.size()) >= m_blockLen)
                {
                    analyseBlock();
                    m_block.clear();
                }
            }
        }
    }

    void analyseBlock()
    {
        float lo, hi;
        if (m_mode == Mode::Fixed)
        {
            lo = std::max(150.0f, m_fixedHz - 100.0f);
            hi = std::min(kMaxFixedPitchHz + 100.0f, m_fixedHz + 100.0f);
        }
        else
        {
            lo = m_trackLo;
            hi = m_trackHi;
        }
        const int nBins = std::max(3, static_cast<int>((hi - lo) / kStepHz) + 1);
        if (static_cast<int>(m_avgPow.size()) != nBins)
            m_avgPow.assign(nBins, 0.0f);

        const int N = static_cast<int>(m_block.size());
        if (static_cast<int>(m_window.size()) != N)
        {
            m_window.resize(N);
            for (int i = 0; i < N; ++i)
                m_window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * i / (N - 1));
        }

        std::vector<float> pow(nBins);
        for (int k = 0; k < nBins; ++k)
        {
            const float f = lo + k * kStepHz;
            const float w = 2.0f * 3.14159265f * f / m_trackSr;
            const float coeff = 2.0f * std::cos(w);
            float s1 = 0.0f, s2 = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float s0 = m_block[i] * m_window[i] + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            pow[k] = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        }

        // Average over ~1 s so the on/off keying looks like a steady line.
        for (int k = 0; k < nBins; ++k)
            m_avgPow[k] = 0.65f * m_avgPow[k] + 0.35f * pow[k];

        int kPeak = 0;
        for (int k = 1; k < nBins; ++k)
            if (m_avgPow[k] > m_avgPow[kPeak])
                kPeak = k;

        std::vector<float> sorted(m_avgPow);
        std::nth_element(sorted.begin(), sorted.begin() + nBins / 2, sorted.end());
        const float median = std::max(1e-12f, sorted[nBins / 2]);
        const float peak = m_avgPow[kPeak];
        m_snrDb = 10.0f * std::log10(std::max(1e-12f, peak) / median);

        // Parabolic interpolation for sub-bin accuracy.
        float fPeak = lo + kPeak * kStepHz;
        if (kPeak > 0 && kPeak < nBins - 1)
        {
            const float a = m_avgPow[kPeak - 1], b = m_avgPow[kPeak], c = m_avgPow[kPeak + 1];
            const float den = a - 2.0f * b + c;
            if (std::fabs(den) > 1e-20f)
                fPeak += 0.5f * (a - c) / den * kStepHz;
        }

        const bool tone = (m_snrDb >= kLockSnrDb);
        m_secSinceTone = tone ? 0.0f : m_secSinceTone + kBlockSec;

        if (m_mode == Mode::Fixed || !tone)
        {
            m_candCount = 0;
            return;
        }

        if (m_trackHz <= 0.0f)
        {
            // First lock: two consecutive blocks agreeing.
            if (m_candCount > 0 && std::fabs(fPeak - m_candHz) <= 15.0f)
            {
                m_trackHz = fPeak;
                m_candCount = 0;
            }
            else
            {
                m_candHz = fPeak;
                m_candCount = 1;
            }
            return;
        }

        if (std::fabs(fPeak - m_trackHz) <= kNearHz)
        {
            m_trackHz += 0.25f * (fPeak - m_trackHz); // slow drift follow
            m_candCount = 0;
            return;
        }

        // A different tone: move only if it persists.
        if (m_candCount > 0 && std::fabs(fPeak - m_candHz) <= 15.0f)
        {
            if (++m_candCount >= kMoveBlocks)
            {
                m_trackHz = fPeak;
                m_candCount = 0;
            }
        }
        else
        {
            m_candHz = fPeak;
            m_candCount = 1;
        }
    }

    // engines
    Engine m_engine = Engine::Machine;
    CwHandKeyDecoder m_hand;
    std::vector<float> m_handDelay;
    std::unique_ptr<GGMorse> m_gg;
    float m_sampleRate = 0.0f;
    float m_speedWpm = -1.0f;
    float m_appliedPitch = -999.0f;
    bool m_paramsDirty = true;
    bool m_lastWasNewline = true;
    bool m_lastWasSpace = true;
    std::vector<float> m_buf;
    size_t m_readPos = 0;

    // pitch mode
    Mode m_mode = Mode::Track;
    float m_fixedHz = 600.0f;
    float m_trackLo = 200.0f;
    float m_trackHi = 1200.0f;
    float m_fallbackHz = -1.0f;
    bool m_squelch = true;

    // tracker state
    int m_decim = 0;
    float m_trackSr = 0.0f;
    int m_blockLen = 0;
    float m_decimAcc = 0.0f;
    int m_decimCount = 0;
    std::vector<float> m_block;
    std::vector<float> m_window;
    std::vector<float> m_avgPow;
    float m_trackHz = 0.0f;
    float m_candHz = 0.0f;
    int m_candCount = 0;
    float m_snrDb = 0.0f;
    float m_secSinceTone = 1e9f;
};
