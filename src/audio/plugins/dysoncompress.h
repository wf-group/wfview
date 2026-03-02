#pragma once

#include <cstdlib>
#include <cmath>
#include <algorithm>

// Dyson Compressor — standalone C++ class
// Ported from LADSPA plugin #1403
// Original algorithm: Copyright (c) 1996, John S. Dyson

class DysonCompressor
{
public:
    explicit DysonCompressor(float sampleRate = 48000.0f);
    ~DysonCompressor();

    // Reset all filter state (call when stream restarts or parameters change drastically)
    void reset();

    // Parameters — safe to update between process() calls from any thread;
    // changes take effect at the next process() call.
    void setPeakLimit(float dB);        // Target output level in dBFS, [-30, 0], default -10
    void setReleaseTime(float seconds); // Gain recovery time, [0.0, 1.0], default 0.1
    void setFastRatio(float ratio);     // Fast compression ratio, [0.0, 1.0], default 0.5
    void setSlowRatio(float ratio);     // Slow compression ratio, [0.0, 1.0], default 0.3

    // Process one block of mono float32 samples in the range [-1, +1].
    // 'in' and 'out' may point to the same buffer (in-place processing).
    void process(const float* in, float* out, unsigned long nSamples);

    // Metering: linear gain factor applied to the last sample of the most
    // recent process() call.  Values < 1.0 indicate gain reduction.
    float getLastTotalGain() const { return lastTotalGain; }

private:
    float sampleRate;

    // User-settable parameters
    float peakLimitDB  = -10.0f;
    float releaseTime  =   0.1f;
    float fastRatio    =   0.5f;
    float slowRatio    =   0.3f;

    // Filter / delay state
    int    ndelay      {0};
    float* delay       {nullptr};
    float* rlevelsqn   {nullptr};
    float* rlevelsqe   {nullptr};

    float  mingain;
    float  maxgain;
    int    rpeaklimitdelay;
    float  rgain;
    float  rmastergain0;
    float  rlevelsq0;
    float  rlevelsq1;
    float  rpeakgain0;
    float  rpeakgain1;
    int    ndelayptr;
    float  lastrgain;
    float  extraMaxlevel;
    int    peaklimitdelay;

    float  lastTotalGain {1.0f};

    static constexpr float MAXLEVEL           = 0.9f;
    static constexpr int   NFILT              = 12;
    static constexpr int   NEFILT             = 17;
    static constexpr float RLEVELSQ0FILTER    = 0.001f;
    static constexpr float RLEVELSQ1FILTER    = 0.010f;
    static constexpr float RLEVELSQ0FFILTER   = 0.001f;
    static constexpr float RLEVELSQEFILTER    = 0.001f;
    static constexpr float RMASTERGAIN0FILTER = 0.000003f;
    static constexpr float RPEAKGAINFILTER    = 0.001f;
    static constexpr float MAXFASTGAIN        = 3.0f;
    static constexpr float MAXSLOWGAIN        = 9.0f;
    static constexpr float FLOORLEVEL         = 0.06f;
};
