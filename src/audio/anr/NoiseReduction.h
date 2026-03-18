/**********************************************************************
  Audacity: A Digital Audio Editor
  NoiseReduction.h
  Dominic Mazzoni
  Vaughan Johnson (Preview)
  Paul Licameli
**********************************************************************/
#pragma once

#include <memory>
#include <vector>
#include "InputTrack.h"
#include "OutputTrack.h"

#define DB_TO_LINEAR(x) (pow(10.0, (x) / 20.0))
#define LINEAR_TO_DB(x) (20.0 * log10(x))

typedef char *samplePtr;

class NoiseReductionWorker;
class Statistics;
class NoiseReduction {
public:
    struct Settings {
        Settings();

        size_t WindowSize() const { return 1u << (3 + mWindowSizeChoice); }
        unsigned StepsPerWindow() const { return 1u << (1 + mStepsPerWindowChoice); }
        bool       mDoProfile;
        double     mNewSensitivity;   // - log10 of a probability... yeah.
        double     mFreqSmoothingBands; // really an integer
        double     mNoiseGain;         // in dB, positive
        double     mAttackTime;        // in secs
        double     mReleaseTime;       // in secs

        // Advanced:
        double     mOldSensitivity;    // in dB, plus or minus

        // Basic:
        int        mNoiseReductionChoice;

        // Advanced:
        int        mWindowTypes;
        int        mWindowSizeChoice;
        int        mStepsPerWindowChoice;
        int        mMethod;
    };

    NoiseReduction(NoiseReduction::Settings& settings, double sampleRate);
    ~NoiseReduction();

    // Offline (whole-file) API — original interface
    void ProfileNoise(InputTrack& profileTrack);
    void ReduceNoise(InputTrack& inputTrack, OutputTrack &outputTrack);

    // ── Streaming API ─────────────────────────────────────────────────────────
    // Call ProfileNoise() first to build the noise profile, then:
    //   beginStream()   — allocate the persistent streaming worker.
    //   feedStream()    — push audio samples; receive processed output.
    //   endStream()     — flush tail samples and release the worker.
    //
    // feedStream() returns zero or more processed samples per call; the count
    // may be less than n during the start-up fill period (history queue fill).
    // The caller should queue output and present a fixed-size buffer upstream.
    bool hasProfile() const;
    size_t windowSize()  const { return mSettings.WindowSize(); }
    size_t stepSize()    const { return mSettings.WindowSize() / mSettings.StepsPerWindow(); }

    // Return the noise profile mean-power spectrum (one value per FFT bin,
    // spectrumSize = 1 + windowSize/2).  Empty if no profile has been built.
    struct NoiseProfile {
        std::vector<float> means;   // mean power per bin (linear scale)
        double sampleRate;
        size_t windowSize;
    };
    NoiseProfile getNoiseProfile() const;

    // Restore a previously-saved noise profile without re-running the profiling pass.
    // The profile means are injected directly into mStatistics so hasProfile() returns
    // true and streaming can begin immediately.  Silently ignored when p is empty,
    // has an incompatible size, or has invalid metadata.
    void restoreProfile(const NoiseProfile& p);

    void beginStream();
    void feedStream(const float* in, size_t n, std::vector<float>& out);
    void endStream(std::vector<float>& out);

private:
    std::unique_ptr<Statistics>           mStatistics;
    std::unique_ptr<NoiseReductionWorker> mStreamWorker;
    NoiseReduction::Settings mSettings;
    double mSampleRate;
};
