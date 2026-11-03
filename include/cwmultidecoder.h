// cwmultidecoder.h
//
// Multi-signal CW decoding ("skimmer lite") for the wfview CW decoder.
// Qt-free; single-threaded (decoder worker thread).
//
// Finds every steady CW tone in the audio passband and runs one hand-key
// engine (CwHandKeyDecoder) per tone, so several stations - e.g. a
// contest pile-up with a wide filter - are copied at once.
//
//   1. Spectrum: decimate to ~8 kHz, 0.25 s Hann-windowed Goertzel bins
//      every 5 Hz over [lo, hi], averaged over ~1 s (keying looks steady).
//   2. Peaks: local maxima >= kPeakDb over the median. A peak close to a
//      much stronger one is ignored (keying sidebands / splatter).
//   3. Channels: a peak seen in two blocks opens a channel; a channel
//      follows its tone slowly, is gated by its own peak, and is dropped
//      after kDropSec without a tone (its text is kept until then).
//   4. Decoders run kDelaySec behind the spectrum so a channel's gate is
//      already open when the first dit of a new station arrives.

#pragma once

#include "cwhandkeydecoder.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class CwMultiDecoder
{
public:
    struct ChannelInfo
    {
        int id = 0;
        float pitchHz = 0.0f;
        float wpm = 0.0f;
        float snrDb = 0.0f;
        bool active = false; // tone present now
        std::string text; // recent decoded text (tail)
    };

    static constexpr int kMaxChannels = 8;
    static constexpr float kStepHz = 5.0f;
    static constexpr float kBlockSec = 0.25f;
    static constexpr float kPeakDb = 9.0f; // over the whole-range median
    static constexpr float kLocalDb = 12.0f; // over the local floor
    static constexpr float kDelaySec = 0.75f;
    static constexpr float kDropSec = 20.0f;
    static constexpr size_t kTextKeep = 400;

    void setRange(float lo, float hi)
    {
        lo = std::max(150.0f, lo);
        hi = std::min(3500.0f, hi);
        if (std::fabs(lo - m_lo) > 1.0f || std::fabs(hi - m_hi) > 1.0f)
        {
            m_lo = lo;
            m_hi = hi;
            m_avg.clear();
        }
    }

    void reset()
    {
        m_channels.clear();
        m_avg.clear();
        m_block.clear();
        m_delay.clear();
        m_decim = 0;
        m_decimAcc = 0.0f;
        m_decimCount = 0;
        m_sampleRate = 0.0f;
    }

    // Feed mono audio. Returns true when the channel list changed in a way
    // worth redrawing (new text, channel opened/closed).
    bool feed(const float *x, int n, float sampleRate)
    {
        if (n <= 0 || sampleRate <= 0.0f)
            return false;
        if (sampleRate != m_sampleRate)
        {
            reset();
            m_sampleRate = sampleRate;
            m_decim = std::max(1, static_cast<int>(sampleRate / 8000.0f));
            m_fs = sampleRate / static_cast<float>(m_decim);
            m_blockLen = static_cast<int>(m_fs * kBlockSec);
        }
        bool changed = false;

        // Spectrum on current audio.
        for (int i = 0; i < n; ++i)
        {
            m_decimAcc += x[i];
            if (++m_decimCount < m_decim)
                continue;
            m_block.push_back(m_decimAcc / static_cast<float>(m_decim));
            m_decimAcc = 0.0f;
            m_decimCount = 0;
            if (static_cast<int>(m_block.size()) >= m_blockLen)
            {
                changed |= analyseBlock();
                m_block.clear();
            }
        }

        // Decoders on delayed audio.
        m_delay.insert(m_delay.end(), x, x + n);
        const size_t delayN = static_cast<size_t>(sampleRate * kDelaySec);
        if (m_delay.size() > delayN)
        {
            const size_t k = m_delay.size() - delayN;
            for (auto &c : m_channels)
            {
                std::string out;
                c->dec.process(m_delay.data(), static_cast<int>(k), sampleRate, c->pitchHz, c->gate(), out);
                if (!out.empty())
                {
                    appendText(*c, out);
                    changed = true;
                }
            }
            m_delay.erase(m_delay.begin(), m_delay.begin() + static_cast<std::ptrdiff_t>(k));
        }

        // Drop channels that have been silent too long.
        const size_t before = m_channels.size();
        m_channels.erase(std::remove_if(m_channels.begin(),
                                        m_channels.end(),
                                        [](const std::unique_ptr<Channel> &c) { return c->silentSec > kDropSec; }),
                         m_channels.end());
        if (m_channels.size() != before)
            changed = true;
        return changed;
    }

    std::vector<ChannelInfo> channels() const
    {
        std::vector<ChannelInfo> v;
        for (const auto &c : m_channels)
        {
            ChannelInfo ci;
            ci.id = c->id;
            ci.pitchHz = c->pitchHz;
            ci.wpm = c->dec.speedWpm();
            ci.snrDb = c->snrDb;
            ci.active = c->gate();
            ci.text = c->text;
            v.push_back(ci);
        }
        std::sort(v.begin(), v.end(), [](const ChannelInfo &a, const ChannelInfo &b) { return a.pitchHz < b.pitchHz; });
        return v;
    }

private:
    struct Channel
    {
        int id = 0;
        float pitchHz = 0.0f;
        float snrDb = 0.0f;
        float silentSec = 0.0f;
        bool confirmed = false;
        CwHandKeyDecoder dec;
        std::string text;
        bool lastNewline = true;
        bool gate() const
        {
            return confirmed && silentSec <= 2.0f;
        }
    };

    static void appendText(Channel &c, const std::string &s)
    {
        for (char ch : s)
        {
            if (ch == '\n')
                continue;
            if (ch == ' ' && (c.text.empty() || c.text.back() == ' '))
                continue;
            c.text.push_back(ch);
        }
        if (c.text.size() > kTextKeep)
            c.text.erase(0, c.text.size() - kTextKeep);
    }

    bool analyseBlock()
    {
        const int nBins = std::max(3, static_cast<int>((m_hi - m_lo) / kStepHz) + 1);
        if (static_cast<int>(m_avg.size()) != nBins)
            m_avg.assign(nBins, 0.0f);

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
            const float f = m_lo + k * kStepHz;
            const float coeff = 2.0f * std::cos(2.0f * 3.14159265f * f / m_fs);
            float s1 = 0.0f, s2 = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                const float s0 = m_block[i] * m_window[i] + coeff * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            pow[k] = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        }
        for (int k = 0; k < nBins; ++k)
            m_avg[k] = 0.65f * m_avg[k] + 0.35f * pow[k];

        std::vector<float> sorted(m_avg);
        std::nth_element(sorted.begin(), sorted.begin() + nBins / 2, sorted.end());
        const float median = std::max(1e-12f, sorted[nBins / 2]);

        // Local maxima above threshold.
        struct Peak
        {
            float hz;
            float db;
        };
        std::vector<Peak> peaks;
        std::vector<float> local;
        for (int k = 1; k < nBins - 1; ++k)
        {
            if (m_avg[k] < m_avg[k - 1] || m_avg[k] < m_avg[k + 1])
                continue;
            const float db = 10.0f * std::log10(std::max(1e-12f, m_avg[k]) / median);
            if (db < kPeakDb)
                continue;
            // Also beat the LOCAL floor (+/-200 Hz, skipping +/-30 Hz): with
            // the radio's CW filter the noise inside the passband is far
            // above the floor outside it, and bumps in it are not stations.
            local.clear();
            const int span = static_cast<int>(200.0f / kStepHz), skip = static_cast<int>(30.0f / kStepHz);
            for (int j = std::max(0, k - span); j <= std::min(nBins - 1, k + span); ++j)
                if (std::abs(j - k) > skip)
                    local.push_back(m_avg[j]);
            if (local.size() >= 8)
            {
                std::nth_element(local.begin(), local.begin() + local.size() / 2, local.end());
                const float ldb
                    = 10.0f * std::log10(std::max(1e-12f, m_avg[k]) / std::max(1e-12f, local[local.size() / 2]));
                if (ldb < kLocalDb)
                    continue;
            }
            float hz = m_lo + k * kStepHz;
            const float a = m_avg[k - 1], b = m_avg[k], c = m_avg[k + 1];
            const float den = a - 2.0f * b + c;
            if (std::fabs(den) > 1e-20f)
                hz += 0.5f * (a - c) / den * kStepHz;
            peaks.push_back({hz, db});
        }
        // Strongest first; drop peaks that sit beside a much stronger one
        // (keying sidebands, splatter, filter skirts).
        std::sort(peaks.begin(), peaks.end(), [](const Peak &p, const Peak &q) { return p.db > q.db; });
        std::vector<Peak> kept;
        for (const Peak &p : peaks)
        {
            bool shadow = false;
            for (const Peak &q : kept)
            {
                const float d = std::fabs(p.hz - q.hz);
                const float weaker = q.db - p.db;
                if (d < 35.0f || (d < 70.0f && weaker > 10.0f) || (d < 120.0f && weaker > 18.0f))
                {
                    shadow = true;
                    break;
                }
            }
            if (!shadow)
                kept.push_back(p);
            if (kept.size() >= static_cast<size_t>(kMaxChannels) + 2)
                break;
        }

        bool changed = false;
        // Match peaks to channels.
        std::vector<bool> used(kept.size(), false);
        for (auto &c : m_channels)
        {
            int best = -1;
            float bestD = 25.0f;
            for (size_t i = 0; i < kept.size(); ++i)
            {
                if (used[i])
                    continue;
                const float d = std::fabs(kept[i].hz - c->pitchHz);
                if (d < bestD)
                {
                    bestD = d;
                    best = static_cast<int>(i);
                }
            }
            if (best >= 0)
            {
                used[best] = true;
                c->pitchHz += 0.25f * (kept[best].hz - c->pitchHz);
                c->snrDb = kept[best].db;
                c->silentSec = 0.0f;
                if (!c->confirmed)
                {
                    c->confirmed = true;
                    changed = true;
                }
            }
            else
            {
                c->silentSec += kBlockSec;
                c->snrDb = 0.0f;
                if (!c->confirmed && c->silentSec > 1.0f)
                    c->silentSec = kDropSec + 1.0f; // never confirmed
            }
        }
        // New channels for unmatched peaks.
        for (size_t i = 0; i < kept.size(); ++i)
        {
            if (used[i])
                continue;
            if (static_cast<int>(m_channels.size()) >= kMaxChannels)
            {
                // Replace the longest-silent channel if this peak is strong.
                auto it = std::max_element(m_channels.begin(),
                                           m_channels.end(),
                                           [](const std::unique_ptr<Channel> &a, const std::unique_ptr<Channel> &b)
                                           { return a->silentSec < b->silentSec; });
                if (it == m_channels.end() || (*it)->silentSec < 3.0f)
                    continue;
                m_channels.erase(it);
            }
            std::unique_ptr<Channel> c(new Channel());
            c->id = ++m_nextId;
            c->pitchHz = kept[i].hz;
            c->snrDb = kept[i].db;
            m_channels.push_back(std::move(c));
            changed = true;
        }
        return changed;
    }

    float m_lo = 200.0f, m_hi = 1200.0f;
    float m_sampleRate = 0.0f;
    int m_decim = 0;
    float m_fs = 8000.0f;
    int m_blockLen = 2000;
    float m_decimAcc = 0.0f;
    int m_decimCount = 0;
    std::vector<float> m_block;
    std::vector<float> m_window;
    std::vector<float> m_avg;
    std::vector<float> m_delay;
    std::vector<std::unique_ptr<Channel>> m_channels;
    int m_nextId = 0;
};
