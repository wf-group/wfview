// TriplePara — 4-band parametric EQ with shelves
// Ported from LADSPA plugin #1204 ("triplePara") by Steve Harris <steve@plugin.org.uk>
// Original: GPL.
//
// Porting notes vs. the original LADSPA C source:
//  - LADSPA plugin_data struct and port pointers removed; state is class members.
//  - util/biquad.h inlined directly — no external headers needed.
//  - Reduced from 5 bands (low shelf + 3 peaking + high shelf) to 4 bands
//    (low shelf + 2 peaking + high shelf) for RX audio EQ use.
//  - Biquad coefficient formulas from Audio EQ Cookbook by Robert Bristow-Johnson.
//  - Compatible with macOS, Windows, and Linux (no platform-specific code).

#include "triple_para.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

// ─── Coefficient calculators (Audio EQ Cookbook) ─────────────────────────────

void TriplePara::calcLowShelf(Biquad& f, float fc, float gainDB, float slope, float fs)
{
    if (std::fabs(gainDB) < 0.001f) {
        // Unity pass-through when gain is essentially 0 dB
        f.b0 = 1; f.b1 = 0; f.b2 = 0; f.a1 = 0; f.a2 = 0;
        return;
    }
    const float A  = std::pow(10.0f, gainDB / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * fc / fs;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float S  = std::max(0.01f, slope);
    const float alpha = sw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
    const float sqA   = 2.0f * std::sqrt(A) * alpha;

    const float a0 =  (A + 1.0f) + (A - 1.0f) * cw + sqA;
    f.b0 = (A * ((A + 1.0f) - (A - 1.0f) * cw + sqA)) / a0;
    f.b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw)) / a0;
    f.b2 = (A * ((A + 1.0f) - (A - 1.0f) * cw - sqA)) / a0;
    f.a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw))    / a0;
    f.a2 = ((A + 1.0f) + (A - 1.0f) * cw - sqA)        / a0;
}

void TriplePara::calcHighShelf(Biquad& f, float fc, float gainDB, float slope, float fs)
{
    if (std::fabs(gainDB) < 0.001f) {
        f.b0 = 1; f.b1 = 0; f.b2 = 0; f.a1 = 0; f.a2 = 0;
        return;
    }
    const float A  = std::pow(10.0f, gainDB / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * fc / fs;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float S  = std::max(0.01f, slope);
    const float alpha = sw / 2.0f * std::sqrt((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f);
    const float sqA   = 2.0f * std::sqrt(A) * alpha;

    const float a0 =  (A + 1.0f) - (A - 1.0f) * cw + sqA;
    f.b0 = (A * ((A + 1.0f) + (A - 1.0f) * cw + sqA)) / a0;
    f.b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)) / a0;
    f.b2 = (A * ((A + 1.0f) + (A - 1.0f) * cw - sqA)) / a0;
    f.a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cw))     / a0;
    f.a2 = ((A + 1.0f) - (A - 1.0f) * cw - sqA)        / a0;
}

void TriplePara::calcPeakingEQ(Biquad& f, float fc, float gainDB, float Q, float fs)
{
    if (std::fabs(gainDB) < 0.001f) {
        f.b0 = 1; f.b1 = 0; f.b2 = 0; f.a1 = 0; f.a2 = 0;
        return;
    }
    const float A  = std::pow(10.0f, gainDB / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * fc / fs;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float Qc = std::max(0.1f, Q);
    const float alpha = sw / (2.0f * Qc);

    const float a0 = 1.0f + alpha / A;
    f.b0 = (1.0f + alpha * A) / a0;
    f.b1 = (-2.0f * cw)       / a0;
    f.b2 = (1.0f - alpha * A) / a0;
    f.a1 = (-2.0f * cw)       / a0;
    f.a2 = (1.0f - alpha / A) / a0;
}

// ─── Constructor / reset ─────────────────────────────────────────────────────

TriplePara::TriplePara(float sampleRate)
    : m_fs(sampleRate)
{
    // Default frequencies for each band
    m_band[0].freqHz = 100.0f;   // low shelf
    m_band[0].slope  = 0.5f;
    m_band[1].freqHz = 800.0f;   // low-mid peaking
    m_band[1].q      = 1.0f;
    m_band[2].freqHz = 2000.0f;  // high-mid peaking
    m_band[2].q      = 1.0f;
    m_band[3].freqHz = 3500.0f;  // high shelf
    m_band[3].slope  = 0.5f;
    reset();
}

void TriplePara::reset()
{
    for (int i = 0; i < NUM_BANDS; ++i)
        m_filter[i].clear();
    m_dirty = true;
}

void TriplePara::setSampleRate(float sr)
{
    if (sr != m_fs) {
        m_fs = sr;
        reset();
    }
}

// ─── Parameter setters ──────────────────────────────────────────────────────

void TriplePara::setBandGain(int idx, float dB)
{
    if (idx < 0 || idx >= NUM_BANDS) return;
    m_band[idx].gainDB = dB;
    m_dirty = true;
}

void TriplePara::setBandFreq(int idx, float hz)
{
    if (idx < 0 || idx >= NUM_BANDS) return;
    // Clamp to safe range: 10 Hz .. Nyquist * 0.9
    m_band[idx].freqHz = std::max(10.0f, std::min(hz, m_fs * 0.45f));
    m_dirty = true;
}

void TriplePara::setBandQ(int idx, float q)
{
    if (idx != 1 && idx != 2) return;  // only peaking bands
    m_band[idx].q = std::max(0.1f, q);
    m_dirty = true;
}

void TriplePara::setShelfSlope(int idx, float slope)
{
    if (idx != 0 && idx != 3) return;  // only shelf bands
    m_band[idx].slope = std::max(0.01f, std::min(slope, 1.0f));
    m_dirty = true;
}

void TriplePara::clearAllBands()
{
    for (int i = 0; i < NUM_BANDS; ++i)
        m_band[i].gainDB = 0.0f;
    m_dirty = true;
}

// ─── Coefficient update ──────────────────────────────────────────────────────

void TriplePara::updateCoefficients()
{
    // Band 0: low shelf
    calcLowShelf(m_filter[0], m_band[0].freqHz, m_band[0].gainDB, m_band[0].slope, m_fs);
    // Band 1: low-mid peaking
    calcPeakingEQ(m_filter[1], m_band[1].freqHz, m_band[1].gainDB, m_band[1].q, m_fs);
    // Band 2: high-mid peaking
    calcPeakingEQ(m_filter[2], m_band[2].freqHz, m_band[2].gainDB, m_band[2].q, m_fs);
    // Band 3: high shelf
    calcHighShelf(m_filter[3], m_band[3].freqHz, m_band[3].gainDB, m_band[3].slope, m_fs);
    m_dirty = false;
}

// ─── process ─────────────────────────────────────────────────────────────────

void TriplePara::process(const float* in, float* out, int nSamples)
{
    if (m_dirty)
        updateCoefficients();

    for (int i = 0; i < nSamples; ++i) {
        float s = m_filter[0].run(in[i]);
        s = m_filter[1].run(s);
        s = m_filter[2].run(s);
        s = m_filter[3].run(s);
        out[i] = s;
    }
}
