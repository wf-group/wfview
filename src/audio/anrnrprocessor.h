// AnrNrProcessor — streaming wrapper for the Audacity Noise Reduction algorithm.
//
// Design overview
// ───────────────
// The upstream Audacity NR code (NoiseReduction.h/cpp) is a batch (offline)
// processor; it expects a complete InputTrack in memory.  This class wraps it
// with a streaming-friendly interface that matches the SpeexNrProcessor
// contract used by RxAudioProcessor:
//
//   std::vector<float> process(const float* in, int n, float sampleRate);
//
// Profiling (noise-sample collection) is decoupled from processing and driven
// by calls from the main thread:
//
//   startProfiling()            — begin accumulating noise samples
//   addProfileSamples(in, n)   — feed samples during recording (converter thread)
//   finishProfiling()          — build the profile, restart the stream worker
//
// FFT window size is chosen automatically from the sample rate so that the
// per-step latency stays in the 20–50 ms range:
//
//   ≤ 11 025 Hz → W = 512,  step = 128 samples (~16 ms at 8 kHz)
//   ≤ 22 050 Hz → W = 1024, step = 256 samples (~16 ms at 16 kHz)
//   > 22 050 Hz → W = 2048, step = 512 samples (~11 ms at 48 kHz)
//
// Total algorithmic latency ≈ historyLen × stepSize ≈ 5 × stepSize samples,
// giving roughly 40–80 ms depending on sample rate.
//
// Thread safety
// ─────────────
// addProfileSamples() is called from the converter thread while m_profiling
// is true.  All other public methods are main-thread only.  process() is
// converter-thread only.  The NR object pointer is swapped under m_nrMutex
// which is held only for the duration of the pointer swap (not during the
// slow ProfileNoise() call).

#pragma once

#include "anr/NoiseReduction.h"
#include "anr/InputTrack.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

class AnrNrProcessor
{
public:
    AnrNrProcessor()  = default;
    ~AnrNrProcessor() = default;

    // ── Parameter setters (main thread) ──────────────────────────────────────
    // Changing any parameter triggers a rebuild of the NR worker on the next
    // process() call.  The noise profile itself is preserved across rebuilds.

    void setNoiseReductionDb(double db)
    {
        if (m_noiseReducDb != db) { m_noiseReducDb = db; m_needsRebuild = true; }
    }

    void setSensitivity(double s)
    {
        if (m_sensitivity != s) { m_sensitivity = s; m_needsRebuild = true; }
    }

    void setFreqSmoothing(int bands)
    {
        if (m_freqSmoothing != bands) { m_freqSmoothing = bands; m_needsRebuild = true; }
    }

    // ── Noise-profile collection ──────────────────────────────────────────────

    // Begin collecting noise samples.  Resets any previously accumulated buffer.
    // Main thread only.
    void startProfiling()
    {
        std::lock_guard<std::mutex> lk(m_profileMutex);
        m_accum.clear();
        m_profiling.store(true, std::memory_order_release);
    }

    // Append audio into the accumulation buffer while profiling is active.
    // CONVERTER THREAD — called from processAudio() alongside process().
    void addProfileSamples(const float* in, int n)
    {
        if (!m_profiling.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(m_profileMutex);
        m_accum.insert(m_accum.end(), in, in + n);
    }

    // Stop collection and build the noise profile.  May take a few milliseconds
    // (one FFT pass over the collected audio).  Main thread only.
    // Returns true on success; false if the accumulated buffer was empty.
    bool finishProfiling()
    {
        // Atomically stop collection so the converter thread won't append more.
        m_profiling.store(false, std::memory_order_release);

        // Grab the accumulated noise samples.
        std::vector<float> snap;
        {
            std::lock_guard<std::mutex> lk(m_profileMutex);
            snap.swap(m_accum);
        }
        if (snap.empty()) return false;

        // Build the NR object and run ProfileNoise() outside any lock so the
        // converter thread is not blocked during the FFT profiling pass.
        const float sr = (m_activeSR > 0.f) ? m_activeSR : 48000.f;
        auto settings = makeSettings(sr);
        std::unique_ptr<NoiseReduction> nr;
        try {
            nr = std::make_unique<NoiseReduction>(settings, sr);
            InputTrack profileTrack(snap);
            nr->ProfileNoise(profileTrack);
            nr->beginStream();
        } catch (...) {
            return false;
        }

        // Swap in the new NR object (brief lock, just a pointer swap).
        {
            std::lock_guard<std::mutex> lk(m_nrMutex);
            m_nr = std::move(nr);
            m_outputQueue.clear();
        }

        // Save the raw profile samples so we can re-profile if settings change.
        {
            std::lock_guard<std::mutex> lk(m_profileMutex);
            m_savedProfile = std::move(snap);
        }

        m_hasProfile   = true;
        m_needsRebuild = false;
        return true;
    }

    bool isProfiling() const { return m_profiling.load(std::memory_order_acquire); }
    bool hasProfile()  const { return m_hasProfile; }

    // Restore a noise profile loaded from persistent storage.
    // Builds a new NR worker immediately using the provided means and marks
    // hasProfile() true.  The means are also kept for future rebuilds so that
    // parameter or sample-rate changes don't silently discard the profile.
    // Main thread only.
    void restoreFromNoiseProfile(const NoiseReduction::NoiseProfile& profile)
    {
        if (profile.means.empty() || profile.windowSize == 0 || profile.sampleRate <= 0.0)
            return;

        // Store the means for future rebuildWorker() calls.
        {
            std::lock_guard<std::mutex> lk(m_profileMutex);
            m_savedMeans   = profile;
            // Raw profile audio is no longer available — clear it so rebuildWorker
            // falls back to the saved means rather than stale samples.
            m_savedProfile.clear();
        }

        // Build the NR worker.  Use m_activeSR if known; fall back to the stored rate.
        const float sr = (m_activeSR > 0.f) ? m_activeSR : static_cast<float>(profile.sampleRate);
        auto settings = makeSettings(sr);
        std::unique_ptr<NoiseReduction> nr;
        try {
            nr = std::make_unique<NoiseReduction>(settings, sr);
            nr->restoreProfile(profile);
            nr->beginStream();
        } catch (...) {
            return;
        }

        {
            std::lock_guard<std::mutex> lk(m_nrMutex);
            m_nr = std::move(nr);
            m_outputQueue.clear();
        }

        m_hasProfile   = true;
        m_needsRebuild = false;
    }

    // Return the noise profile spectrum (mean power per FFT bin).
    // Thread-safe: briefly locks m_nrMutex.
    NoiseReduction::NoiseProfile getNoiseProfile()
    {
        std::lock_guard<std::mutex> lk(m_nrMutex);
        if (m_nr)
            return m_nr->getNoiseProfile();
        return {};
    }

    // ── Processing (converter thread) ─────────────────────────────────────────

    std::vector<float> process(const float* in, int n, float sampleRate)
    {
        // Track sample-rate changes.
        if (sampleRate != m_activeSR) {
            m_activeSR     = sampleRate;
            m_needsRebuild = true;
        }

        // Rebuild the NR worker when parameters or sample rate have changed,
        // re-using the saved noise profile.
        if (m_needsRebuild && m_hasProfile) {
            rebuildWorker(sampleRate);
            m_needsRebuild = false;
        }

        // Pass audio through until a noise profile exists.
        if (!m_hasProfile) {
            return std::vector<float>(in, in + n);
        }

        // Feed input samples to the streaming ANR worker.
        {
            std::lock_guard<std::mutex> lk(m_nrMutex);
            if (m_nr) {
                std::vector<float> chunk;
                m_nr->feedStream(in, static_cast<size_t>(n), chunk);
                for (float s : chunk)
                    m_outputQueue.push_back(s);
            }
        }

        // Dequeue exactly n samples, padding with silence during startup fill.
        std::vector<float> result(static_cast<size_t>(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            if (!m_outputQueue.empty()) {
                result[static_cast<size_t>(i)] = m_outputQueue.front();
                m_outputQueue.pop_front();
            }
        }
        return result;
    }

    // Estimated algorithmic latency in ms (history queue fill time).
    float latencyMs() const
    {
        if (!m_hasProfile || m_activeSR <= 0.f) return 0.f;
        // ≈ 5 steps × stepSize / sampleRate
        const float step = static_cast<float>(windowSizeForSr(m_activeSR)) / 4.f;
        return 5.f * step / m_activeSR * 1000.f;
    }

    // Reset the streaming pipeline (clears output queue and restarts the worker).
    // Converter thread.
    void reset()
    {
        std::lock_guard<std::mutex> lk(m_nrMutex);
        if (m_nr && m_hasProfile) {
            std::vector<float> discard;
            m_nr->endStream(discard);
            m_nr->beginStream();
        }
        m_outputQueue.clear();
    }

private:
    // ── Algorithm parameters ──────────────────────────────────────────────────
    double m_noiseReducDb  = 12.0;
    double m_sensitivity   =  6.0;
    int    m_freqSmoothing =  0;
    bool   m_needsRebuild  = false;

    // ── Runtime state ─────────────────────────────────────────────────────────
    float m_activeSR   = 0.f;
    bool  m_hasProfile = false;

    // Profiling accumulator (shared between main and converter threads)
    std::atomic<bool>  m_profiling{false};
    std::mutex         m_profileMutex;
    std::vector<float>           m_accum;        // grows during active profiling
    std::vector<float>           m_savedProfile; // raw audio samples from live collection
    NoiseReduction::NoiseProfile m_savedMeans;   // means from persistent storage (file restore)

    // NR streaming engine
    std::mutex                      m_nrMutex;
    std::unique_ptr<NoiseReduction> m_nr;
    std::deque<float>               m_outputQueue;

    // ── Helpers ───────────────────────────────────────────────────────────────

    // Choose FFT window size from sample rate for ~20–50 ms per step.
    static size_t windowSizeForSr(float sr)
    {
        if (sr <= 11025.f) return 512u;
        if (sr <= 22050.f) return 1024u;
        return 2048u;
    }

    // Convert window size to the Settings::mWindowSizeChoice integer.
    // WindowSize = 2^(3 + choice)  →  choice = log2(WindowSize) - 3
    static int windowSizeChoice(float sr)
    {
        size_t w = windowSizeForSr(sr);
        int k = 0;
        while (w > 8u) { w >>= 1; ++k; }
        return k;
    }

    NoiseReduction::Settings makeSettings(float sr) const
    {
        NoiseReduction::Settings s;
        s.mDoProfile            = false;
        s.mWindowSizeChoice     = windowSizeChoice(sr);
        s.mStepsPerWindowChoice = 1;           // 4 steps/window, 75 % overlap
        s.mNoiseGain            = m_noiseReducDb;
        s.mNewSensitivity       = m_sensitivity;
        s.mFreqSmoothingBands   = static_cast<double>(m_freqSmoothing);
        s.mAttackTime           = 0.02;
        s.mReleaseTime          = 0.10;
        return s;
    }

    // Rebuild the NR worker using the saved profile.  Called from the converter
    // thread, so the profile lock is held only briefly (to copy the saved data).
    // Prefers raw audio samples (higher fidelity at the current SR); falls back
    // to the saved means from a file-restored profile when no raw samples exist.
    void rebuildWorker(float sr)
    {
        std::vector<float>           profileCopy;
        NoiseReduction::NoiseProfile meansCopy;
        {
            std::lock_guard<std::mutex> lk(m_profileMutex);
            profileCopy = m_savedProfile;
            meansCopy   = m_savedMeans;
        }
        if (profileCopy.empty() && meansCopy.means.empty()) return;

        auto settings = makeSettings(sr);
        std::unique_ptr<NoiseReduction> nr;
        try {
            nr = std::make_unique<NoiseReduction>(settings, sr);
            if (!profileCopy.empty()) {
                // Re-profile from the original raw audio (adapts to new SR/settings).
                InputTrack profileTrack(profileCopy);
                nr->ProfileNoise(profileTrack);
            } else {
                // Fall back to the file-restored means — no raw samples available.
                nr->restoreProfile(meansCopy);
            }
            nr->beginStream();
        } catch (...) {
            return;
        }

        std::lock_guard<std::mutex> lk(m_nrMutex);
        m_nr = std::move(nr);
        m_outputQueue.clear();
    }
};
