// SpacNrProcessor — real-time streaming adapter of the SPAC noise-reduction
// algorithm (Splicing of AutoCorrelation, Jouji Suzuki 1970s).
//
// Same eight improvements as spac_nr.c (A–H), adapted for streaming use with
// 50 % overlap-add.  Operates on float samples [-1, 1].
//
// NOT thread-safe — call process() exclusively from the converter thread.

#ifndef SPACNRPROCESSOR_H
#define SPACNRPROCESSOR_H

#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

class SpacNrProcessor
{
public:
    // Pitch search range
    static constexpr double PITCH_MIN_HZ    =   50.0;
    static constexpr double PITCH_MAX_HZ    = 1500.0;
    static constexpr double PROMINENCE_RATIO = 2.5;
    static constexpr double ENERGY_GATE_DBFS = -60.0;
    static constexpr double MAX_PITCH_JUMP   =  0.30;
    static constexpr int    CONFIRM_FRAMES   =  2;
    static constexpr double CROSSFADE_FRAC   =  0.125;

    SpacNrProcessor() = default;
    ~SpacNrProcessor() = default;

    SpacNrProcessor(const SpacNrProcessor&)            = delete;
    SpacNrProcessor& operator=(const SpacNrProcessor&) = delete;

    // ── Parameter setters ─────────────────────────────────────────────────────
    void setFrameMs(float ms)          { m_frameMs    = ms;   reset(); }
    void setVoicingThr(float v)        { m_voicingThr = v;   }
    void setVoicingFull(float v)       { m_voicingFull = v;  }
    void setAttenDb(float dB)          { m_attenDb    = dB;  }

    // ── Reset ─────────────────────────────────────────────────────────────────
    void reset()
    {
        m_sampleRate = 0.0f;
        m_frameSize  = 0;
        m_hop        = 0;
        m_inBuf.clear();
        m_outAccum.clear();
        m_readyBuf.clear();
        m_ready     = false;
        m_tracker   = { 0.0, 0, 0 };
    }

    // ── Latency ───────────────────────────────────────────────────────────────
    int   latencySamples() const { return m_frameSize; }
    float latencyMs() const
    {
        return (m_sampleRate > 0) ? 1000.0f * m_frameSize / m_sampleRate : 0.0f;
    }

    // ── Main processing entry ─────────────────────────────────────────────────
    std::vector<float> process(const float* in, int nSamples, float sampleRate)
    {
        std::vector<float> out(nSamples, 0.0f);

        // (Re)initialise on sample-rate change
        const int newFrame = frameSizeSamples(sampleRate);
        if (!m_ready || sampleRate != m_sampleRate || newFrame != m_frameSize) {
            initState(sampleRate);
        }

        // Append new samples to input buffer
        m_inBuf.insert(m_inBuf.end(), in, in + nSamples);

        // Process frames (50 % overlap-add)
        while (static_cast<int>(m_inBuf.size()) >= m_frameSize) {
            processFrame();
        }

        // Drain completed hop samples from the ready buffer
        int toDrain = std::min(nSamples, static_cast<int>(m_readyBuf.size()));
        for (int i = 0; i < toDrain; ++i)
            out[i] = m_readyBuf[i];
        if (toDrain > 0)
            m_readyBuf.erase(m_readyBuf.begin(), m_readyBuf.begin() + toDrain);

        return out;
    }

private:
    // ── Pitch tracker state ───────────────────────────────────────────────────
    struct PitchTracker {
        double lastPitch   = 0.0;
        int    consecutive = 0;
        int    confirmed   = 0;
    };

    // ── Helpers ───────────────────────────────────────────────────────────────

    int frameSizeSamples(float sr) const
    {
        int fs = static_cast<int>(sr * m_frameMs / 1000.0f + 0.5f);
        if (fs < 2) fs = 2;
        if (fs & 1) ++fs;
        return fs;
    }

    void initState(float sr)
    {
        m_sampleRate = sr;
        m_frameSize  = frameSizeSamples(sr);
        m_hop        = m_frameSize / 2;

        // Precompute Hanning synthesis window
        m_window.resize(m_frameSize);
        for (int i = 0; i < m_frameSize; ++i)
            m_window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / m_frameSize));

        m_inBuf.clear();
        m_outAccum.assign(m_frameSize, 0.0);
        m_readyBuf.clear();
        m_tracker = { 0.0, 0, 0 };
        m_ready   = true;
    }

    void processFrame()
    {
        const int N = m_frameSize;

        // Load frame from input buffer (zero-pad if short — shouldn't happen)
        std::vector<double> frame(N, 0.0);
        int avail = std::min(N, static_cast<int>(m_inBuf.size()));
        for (int i = 0; i < avail; ++i)
            frame[i] = static_cast<double>(m_inBuf[i]);

        // Advance input by hop
        m_inBuf.erase(m_inBuf.begin(),
                      m_inBuf.begin() + std::min(m_hop, static_cast<int>(m_inBuf.size())));

        // Ensure output accumulator is long enough
        if (static_cast<int>(m_outAccum.size()) < N)
            m_outAccum.resize(N * 2, 0.0);

        // ── SPAC algorithm ────────────────────────────────────────────────────
        const double attenGain = std::pow(10.0, -m_attenDb / 20.0);

        // (E) Frame energy gate
        double energy = 0.0;
        for (double s : frame) energy += s * s;
        double rms = std::sqrt(energy / N);
        const double energyGateRms = std::pow(10.0, ENERGY_GATE_DBFS / 20.0);

        std::vector<double> processed(N);
        bool gated = false;

        if (rms < energyGateRms) {
            // Quiet frame — attenuate
            for (int i = 0; i < N; ++i) processed[i] = frame[i] * attenGain;
            pitchContinuityReset(m_tracker);
            gated = true;
        } else {
            // Pitch detection range
            const int minLag = std::max(1, static_cast<int>(std::ceil(m_sampleRate / PITCH_MAX_HZ)));
            const int maxLagAbs = static_cast<int>(std::floor(m_sampleRate / PITCH_MIN_HZ));
            int maxLag = N / 3;
            if (maxLag > maxLagAbs) maxLag = maxLagAbs;
            if (maxLag < minLag)    maxLag = minLag;
            const int acfLen = std::min(2 * maxLag + 2, N - 1);

            // Compute autocorrelation
            std::vector<double> R(acfLen + 2, 0.0);
            autocorrelation(frame.data(), N, R.data(), acfLen);

            // Find pitch
            double T_frac = 0.0, confidence = 0.0;
            bool voiced = findPitchPeriod(R.data(), minLag, maxLag, acfLen,
                                          m_voicingThr, T_frac, confidence);

            if (!voiced || !pitchContinuityCheck(m_tracker, T_frac)) {
                if (!voiced) pitchContinuityReset(m_tracker);
                for (int i = 0; i < N; ++i) processed[i] = frame[i] * attenGain;
            } else {
                // Extract, smooth, tile
                int T_int = static_cast<int>(std::lrint(T_frac));
                if (T_int < 1)       T_int = 1;
                if (T_int > maxLag)  T_int = maxLag;

                std::vector<double> cycle(T_int);
                extractAndSmoothCycle(R.data(), acfLen, T_frac, T_int, cycle);

                // Normalise cycle RMS to match frame RMS
                double rmsIn = std::sqrt(R[0] / N);
                double sumSq = 0.0;
                for (double s : cycle) sumSq += s * s;
                double rmsCycle = std::sqrt(sumSq / T_int);
                double scale = (rmsCycle > 1e-10) ? rmsIn / rmsCycle : 0.0;
                for (double& s : cycle) s *= scale;

                // (A) Tile with cosine crossfade
                tileWithCrossfade(cycle.data(), T_int, processed.data(), N);

                // (H) Soft blend
                double blend = (confidence - m_voicingThr) / (m_voicingFull - m_voicingThr);
                if (blend > 1.0) blend = 1.0;
                if (blend < 0.0) blend = 0.0;
                const double unvoicedGain = attenGain;
                for (int i = 0; i < N; ++i)
                    processed[i] = blend * processed[i]
                                 + (1.0 - blend) * frame[i] * unvoicedGain;
            }
        }
        (void)gated;  // stats not tracked in streaming mode

        // Overlap-add with Hanning synthesis window
        for (int i = 0; i < N; ++i)
            m_outAccum[i] += processed[i] * m_window[i];

        // Capture the completed hop samples into the ready buffer BEFORE erasing
        for (int i = 0; i < m_hop; ++i)
            m_readyBuf.push_back(static_cast<float>(m_outAccum[i]));

        // Shift accumulator by hop — the first hop samples are done
        // Append hop zeros for the next frame's contribution
        m_outAccum.erase(m_outAccum.begin(), m_outAccum.begin() + m_hop);
        m_outAccum.insert(m_outAccum.end(), m_hop, 0.0);
    }

    // ── Autocorrelation ───────────────────────────────────────────────────────
    static void autocorrelation(const double* x, int N, double* R, int maxLag)
    {
        for (int tau = 0; tau <= maxLag; ++tau) {
            double sum = 0.0;
            for (int n = 0; n < N - tau; ++n) sum += x[n] * x[n + tau];
            R[tau] = sum;
        }
    }

    // ── Pitch detection (improvements C + D) ─────────────────────────────────
    static bool findPitchPeriod(const double* R, int minLag, int maxLag, int acfLen,
                                double voicingThr, double& pitchOut, double& confidenceOut)
    {
        pitchOut = 0.0; confidenceOut = 0.0;
        if (R[0] < 1e-10) return false;

        double bestVal = -1e30;
        int    bestLag = minLag;
        double sumR = 0.0;

        for (int tau = minLag; tau <= maxLag; ++tau) {
            if (R[tau] > bestVal) { bestVal = R[tau]; bestLag = tau; }
            sumR += R[tau];
        }
        const int count = maxLag - minLag + 1;
        confidenceOut = bestVal / R[0];
        if (confidenceOut < voicingThr) return false;

        // (D) Prominence check
        if (count > 0) {
            double meanR = sumR / count;
            if (meanR > 1e-10 && bestVal / meanR < PROMINENCE_RATIO) return false;
        }

        // (C) Parabolic interpolation
        double T_frac = static_cast<double>(bestLag);
        if (bestLag > minLag && bestLag < maxLag && bestLag < acfLen) {
            double Rm1 = R[bestLag - 1], R0 = R[bestLag], Rp1 = R[bestLag + 1];
            double denom = Rm1 - 2.0 * R0 + Rp1;
            if (std::fabs(denom) > 1e-20) {
                double delta = 0.5 * (Rm1 - Rp1) / denom;
                if (delta >  0.5) delta =  0.5;
                if (delta < -0.5) delta = -0.5;
                T_frac += delta;
            }
        }
        pitchOut = T_frac;
        return true;
    }

    // ── Cycle extraction + (B) 5-tap FIR smoothing ───────────────────────────
    static void extractAndSmoothCycle(const double* R, int acfLen,
                                      double T_frac, int T_int,
                                      std::vector<double>& cycle)
    {
        cycle.resize(T_int);
        for (int i = 0; i < T_int; ++i) {
            double tau = T_frac + i;
            int    t0  = static_cast<int>(std::floor(tau));
            double frac = tau - t0;
            double v0 = (t0 >= 0 && t0 <= acfLen) ? R[t0]     : 0.0;
            double v1 = (t0 + 1 >= 0 && t0 + 1 <= acfLen) ? R[t0 + 1] : 0.0;
            cycle[i] = v0 + frac * (v1 - v0);
        }
        if (T_int >= 5) {
            std::vector<double> tmp(T_int);
            for (int i = 0; i < T_int; ++i) {
                int im2 = (i - 2 + T_int) % T_int, im1 = (i - 1 + T_int) % T_int;
                int ip1 = (i + 1) % T_int, ip2 = (i + 2) % T_int;
                tmp[i] = (cycle[im2] + 2*cycle[im1] + 3*cycle[i] + 2*cycle[ip1] + cycle[ip2]) / 9.0;
            }
            cycle = std::move(tmp);
        }
    }

    // ── (A) Crossfade tiling ──────────────────────────────────────────────────
    static void tileWithCrossfade(const double* cycle, int T,
                                  double* out, int frameSize)
    {
        int taper = static_cast<int>(CROSSFADE_FRAC * T);
        if (taper < 1) taper = 1;
        if (taper > T / 2) taper = T / 2;
        std::fill(out, out + frameSize, 0.0);
        const int advance = std::max(1, T - taper);
        for (int start = 0; start < frameSize; start += advance) {
            for (int j = 0; j < T; ++j) {
                int idx = start + j;
                if (idx >= frameSize) break;
                double w = 1.0;
                if (j < taper && start > 0)
                    w = 0.5 * (1.0 - std::cos(M_PI * j / taper));
                if (j >= T - taper) {
                    int k = j - (T - taper);
                    w *= 0.5 * (1.0 + std::cos(M_PI * k / taper));
                }
                out[idx] += cycle[j] * w;
            }
        }
    }

    // ── (F) Pitch continuity ──────────────────────────────────────────────────
    static bool pitchContinuityCheck(PitchTracker& pt, double pitch)
    {
        if (!pt.confirmed) {
            if (pt.consecutive == 0) { pt.lastPitch = pitch; pt.consecutive = 1; return false; }
            double ratio = pitch / pt.lastPitch;
            if (ratio > 1.0 + MAX_PITCH_JUMP || ratio < 1.0 - MAX_PITCH_JUMP)
                { pt.lastPitch = pitch; pt.consecutive = 1; return false; }
            pt.lastPitch = pitch;
            if (++pt.consecutive >= CONFIRM_FRAMES) { pt.confirmed = 1; return true; }
            return false;
        }
        double ratio = pitch / pt.lastPitch;
        if (ratio > 1.0 + MAX_PITCH_JUMP || ratio < 1.0 - MAX_PITCH_JUMP)
            { pt.confirmed = 0; pt.lastPitch = pitch; pt.consecutive = 1; return false; }
        pt.lastPitch = pitch;
        return true;
    }

    static void pitchContinuityReset(PitchTracker& pt)
    { pt.confirmed = 0; pt.consecutive = 0; }

    // ── State ─────────────────────────────────────────────────────────────────
    float  m_frameMs     = 20.0f;
    float  m_voicingThr  = 0.20f;
    float  m_voicingFull = 0.55f;
    float  m_attenDb     = 80.0f;

    float  m_sampleRate  = 0.0f;
    int    m_frameSize   = 0;
    int    m_hop         = 0;
    bool   m_ready       = false;

    PitchTracker         m_tracker;
    std::vector<double>  m_window;
    std::vector<float>   m_inBuf;
    std::vector<double>  m_outAccum;
    std::vector<float>   m_readyBuf;
};

#endif // SPACNRPROCESSOR_H
