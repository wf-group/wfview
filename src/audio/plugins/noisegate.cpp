// NoiseGate — standalone C++ implementation
// Ported from LADSPA plugin #1921 ("gate") by Steve Harris <steve@plugin.org.uk>
// Original: GPL.
//
// Porting notes vs. the original LADSPA C source:
//  - LADSPA plugin_data struct and port pointers removed; state is class members.
//  - biquad.h / ladspa-util.h inlined directly — no external headers needed.
//  - Only the mono "gate" variant is implemented (TX audio is always mono).
//  - The "output select" port (key-listen / gate / bypass) is not exposed;
//    the gate always runs in normal gate mode (output = gated input signal).
//  - lf_fc and hf_fc are now in Hz (not fraction-of-sample-rate); the
//    shelving-filter helpers convert to radians internally.
//  - getGain() added to expose the current gate coefficient for metering.

#include "noisegate.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── DB helper (from ladspa-util.h) ──────────────────────────────────────────

static inline float DB_CO(float g)
{
    return (g > -90.0f) ? std::pow(10.0f, g * 0.05f) : 0.0f;
}

// ─── Shelving filter coefficient calculation ──────────────────────────────────
// Standard Audio EQ Cookbook formulas.  slope is the shelf slope parameter S
// (0.6 matches the original plugin).

void NoiseGate::setLowShelf(Biquad& f, float fc, float gainDB, float slope, float fs)
{
    const float A  = std::pow(10.0f, gainDB / 40.0f);
    const float w0 = 2.0f * float(M_PI) * fc / fs;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float sqA   = 2.0f * std::sqrt(A) * alpha;

    const float a0 =  (A + 1.0f) + (A - 1.0f) * cw + sqA;
    f.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + sqA)) / a0;
    f.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw))  / a0;
    f.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - sqA)) / a0;
    f.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw))     / a0;
    f.a2 = ((A + 1.0f) + (A - 1.0f) * cw - sqA)         / a0;
}

void NoiseGate::setHighShelf(Biquad& f, float fc, float gainDB, float slope, float fs)
{
    const float A  = std::pow(10.0f, gainDB / 40.0f);
    const float w0 = 2.0f * float(M_PI) * fc / fs;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);
    const float sqA   = 2.0f * std::sqrt(A) * alpha;

    const float a0 =  (A + 1.0f) - (A - 1.0f) * cw + sqA;
    f.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + sqA)) / a0;
    f.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    f.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - sqA)) / a0;
    f.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw))      / a0;
    f.a2 = ((A + 1.0f) - (A - 1.0f) * cw - sqA)         / a0;
}

// ─── Constructor / reset ──────────────────────────────────────────────────────

NoiseGate::NoiseGate(float sampleRate)
    : m_fs(sampleRate)
    , m_state(CLOSED)
{
    reset();
}

void NoiseGate::reset()
{
    m_env       = 0.0f;
    m_gate      = 0.0f;
    m_holdCount = 0;
    m_state     = CLOSED;
    m_lf.clear();
    m_hf.clear();
}

// ─── Parameter setters ────────────────────────────────────────────────────────

void NoiseGate::setThreshold(float dB) { m_threshold = dB; }
void NoiseGate::setAttack(float ms)    { m_attack    = ms; }
void NoiseGate::setHold(float ms)      { m_hold      = ms; }
void NoiseGate::setDecay(float ms)     { m_decay     = ms; }
void NoiseGate::setRange(float dB)     { m_range     = dB; }
void NoiseGate::setLfCutoff(float hz)  { m_lfCutoff  = hz; }
void NoiseGate::setHfCutoff(float hz)  { m_hfCutoff  = hz; }

// ─── process ─────────────────────────────────────────────────────────────────

void NoiseGate::process(const float* in, float* out, unsigned long nSamples)
{
    // Clamp parameters to safe ranges before computing derived rates.
    const float attack = std::max(0.001f, m_attack);
    const float decay  = std::max(0.01f,  m_decay);

    const float cut     = DB_CO(m_range);
    const float t_level = DB_CO(m_threshold);
    const float a_rate  = 1000.0f / (attack * m_fs);
    const float d_rate  = 1000.0f / (decay  * m_fs);

    // Update key-detector biquad coefficients every block (cheap).
    // The low shelf with a large negative gain acts as a steep highpass;
    // the high shelf with a large negative gain acts as a steep lowpass.
    // Together they create a bandpass window for the key signal.
    const float lfHz = std::max(10.0f,    std::min(m_lfCutoff, m_fs * 0.45f));
    const float hfHz = std::max(lfHz + 1, std::min(m_hfCutoff, m_fs * 0.45f));
    setLowShelf (m_lf, lfHz, -40.0f, 0.6f, m_fs);
    setHighShelf(m_hf, hfHz, -50.0f, 0.6f, m_fs);

    // Envelope release time constant: ~10 ms, derived from sample rate.
    // The one-pole IIR is: env = akey * env_tr + env * (1 - env_tr)
    // Time constant τ = -1/ln(1-env_tr) ≈ 1/(env_tr * fs).
    // 10 ms at any sample rate keeps the envelope responsive so the hold timer
    // (not the envelope decay) governs how long the gate stays open.
    const float env_tr = 1.0f - std::exp(-1.0f / (0.010f * m_fs));

    // Local copies of state variables (matches original LADSPA style).
    float env       = m_env;
    float gate      = m_gate;
    int   holdCount = m_holdCount;
    int   state     = m_state;

    for (unsigned long pos = 0; pos < nSamples; ++pos) {
        // ── Key signal: filtered version of input ─────────────────────────
        float key = m_lf.run(in[pos]);
        key       = m_hf.run(key);
        const float akey = std::fabs(key);

        // ── Envelope follower (peak with fast attack, ~10 ms release) ─────
        if (akey > env)
            env = akey;
        else
            env = akey * env_tr + env * (1.0f - env_tr);

        // ── Gate state machine ────────────────────────────────────────────
        if (state == CLOSED) {
            if (env >= t_level)
                state = OPENING;

        } else if (state == OPENING) {
            gate += a_rate;
            if (gate >= 1.0f) {
                gate = 1.0f;
                state = OPEN;
                holdCount = static_cast<int>(m_hold * m_fs * 0.001f);
            }

        } else if (state == OPEN) {
            if (holdCount <= 0) {
                if (env < t_level)
                    state = CLOSING;
            } else {
                --holdCount;
            }

        } else if (state == CLOSING) {
            gate -= d_rate;
            if (env >= t_level) {
                state = OPENING;
            } else if (gate <= 0.0f) {
                gate  = 0.0f;
                state = CLOSED;
            }
        }

        // ── Output: blend between closed-state gain and fully open (1.0) ─
        // cut * (1 - gate) + gate:
        //   gate=0 → cut (closed attenuation)
        //   gate=1 → 1.0 (fully open, no attenuation)
        out[pos] = in[pos] * (cut * (1.0f - gate) + gate);
    }

    // Write state back.
    m_env       = env;
    m_gate      = gate;
    m_holdCount = holdCount;
    m_state     = state;
}
