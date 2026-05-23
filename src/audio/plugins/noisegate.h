#pragma once

#include <cmath>

// NoiseGate — standalone C++ class
// Ported from LADSPA plugin #1921 ("gate") by Steve Harris <steve@plugin.org.uk>
// Original: GPL.  This port follows the same licence terms.
//
// Runs on the raw microphone signal, BEFORE input gain.
// The key detector filters (LF / HF biquads) determine which frequency range
// of the signal is used to open/close the gate; the gate itself attenuates the
// full-bandwidth signal by `range` dB when closed.

class NoiseGate
{
public:
    explicit NoiseGate(float sampleRate = 48000.0f);

    // Reset envelope / gate state (call when stream restarts).
    void reset();

    // ── Parameters — update between process() calls ───────────────────────────

    // Level threshold at which the gate opens (dBFS, -70 .. 0).  Default -40.
    void setThreshold(float dB);

    // Time for gate to fully open once threshold is exceeded (ms, 0.01 .. 1000).
    void setAttack(float ms);

    // Minimum time gate stays open after signal drops below threshold (ms, 2 .. 2000).
    void setHold(float ms);

    // Time for gate to fully close after hold expires (ms, 2 .. 4000).
    void setDecay(float ms);

    // Gain applied when gate is fully closed (dB, -90 .. 0).  -90 = full gate.
    void setRange(float dB);

    // Low-frequency key filter cutoff — acts as a highpass on the key signal
    // (Hz, 20 .. 4000).  Raising this prevents low rumble from opening the gate.
    // Default 80 Hz.
    void setLfCutoff(float hz);

    // High-frequency key filter cutoff — acts as a lowpass on the key signal
    // (Hz, 200 .. 20000).  Lowering this prevents high-frequency noise from
    // opening the gate.  Default 8000 Hz.
    void setHfCutoff(float hz);

    // ── Processing ───────────────────────────────────────────────────────────

    // Process one block of mono float32 samples in [-1, +1].
    // 'in' and 'out' may alias (in-place is fine).
    void process(const float* in, float* out, unsigned long nSamples);

    // Current gate gain factor (1.0 = fully open, ≤ 1.0 when closing/closed).
    float getGain() const { return m_gate; }

private:
    // ── Biquad filter ────────────────────────────────────────────────────────
    struct Biquad {
        float b0=1, b1=0, b2=0, a1=0, a2=0;
        float x1=0, x2=0, y1=0, y2=0;

        void clear() { x1=x2=y1=y2=0.0f; }

        float run(float x) {
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2=x1; x1=x; y2=y1; y1=y;
            return y;
        }
    };

    // Audio EQ Cookbook shelving filters (used for key-detector bandpass).
    // gain is in dB; slope is shelf slope (use 0.6 to match original plugin).
    static void setLowShelf (Biquad& f, float fc, float gainDB, float slope, float fs);
    static void setHighShelf(Biquad& f, float fc, float gainDB, float slope, float fs);

    // ── Parameters ───────────────────────────────────────────────────────────
    float m_fs        = 48000.0f;
    float m_threshold = -40.0f;   // dB
    float m_attack    =  10.0f;   // ms
    float m_hold      = 100.0f;   // ms
    float m_decay     = 200.0f;   // ms
    float m_range     = -90.0f;   // dB
    float m_lfCutoff  =  80.0f;   // Hz
    float m_hfCutoff  = 8000.0f;  // Hz

    // ── State ────────────────────────────────────────────────────────────────
    float m_env       = 0.0f;
    float m_gate      = 0.0f;
    int   m_holdCount = 0;
    int   m_state;

    Biquad m_lf, m_hf;

    static constexpr int   CLOSED  = 1;
    static constexpr int   OPENING = 2;
    static constexpr int   OPEN    = 3;
    static constexpr int   CLOSING = 4;
};
