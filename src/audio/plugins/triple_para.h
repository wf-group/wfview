#pragma once

#include <cmath>

// TriplePara — 4-band parametric EQ with shelves
// Ported from LADSPA plugin #1204 ("triplePara") by Steve Harris <steve@plugin.org.uk>
// Original: GPL.  This port follows the same licence terms.
//
// Topology: Low shelf → Low-mid peaking → High-mid peaking → High shelf
// All filters are biquad IIR (Audio EQ Cookbook formulas).
//
// Design for RX audio EQ:
//   Band 0 — Low shelf    :  50–250 Hz, gain ±6 dB
//   Band 1 — Low-mid peak : 300–1500 Hz, gain ±6 dB, adjustable Q
//   Band 2 — High-mid peak: 1000–3000 Hz, gain ±6 dB, adjustable Q
//   Band 3 — High shelf   : 2000–4500 Hz, gain ±6 dB

class TriplePara
{
public:
    static constexpr int NUM_BANDS = 4;

    explicit TriplePara(float sampleRate = 48000.0f);

    // Reset all filter state (call when stream restarts or sample rate changes).
    void reset();

    // Update sample rate (also resets filter state).
    void setSampleRate(float sr);

    // ── Per-band parameters ──────────────────────────────────────────────────

    // Set gain for band idx (dB).  idx: 0=low shelf, 1=low-mid, 2=high-mid, 3=high shelf
    void setBandGain(int idx, float dB);

    // Set centre/corner frequency for band idx (Hz).
    void setBandFreq(int idx, float hz);

    // Set Q (bandwidth) for peaking bands 1 and 2.
    // Shelf bands (0, 3) use slope instead; this call is ignored for them.
    void setBandQ(int idx, float q);

    // Set shelf slope for bands 0 and 3 (0.0–1.0, default 0.5).
    // Ignored for peaking bands 1 and 2.
    void setShelfSlope(int idx, float slope);

    // Convenience: set all bands to 0 dB gain (flat).
    void clearAllBands();

    // ── Processing ───────────────────────────────────────────────────────────

    // Process one block of mono float32 samples.
    // 'in' and 'out' may alias (in-place is fine).
    void process(const float* in, float* out, int nSamples);

private:
    // ── Biquad filter ────────────────────────────────────────────────────────
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        void clear() { x1 = x2 = y1 = y2 = 0.0f; }

        float run(float x) {
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };

    // Audio EQ Cookbook coefficient calculators
    static void calcLowShelf (Biquad& f, float fc, float gainDB, float slope, float fs);
    static void calcHighShelf(Biquad& f, float fc, float gainDB, float slope, float fs);
    static void calcPeakingEQ(Biquad& f, float fc, float gainDB, float Q, float fs);

    // ── Band parameters ──────────────────────────────────────────────────────
    struct BandParams {
        float gainDB = 0.0f;
        float freqHz = 1000.0f;
        float q      = 1.0f;     // used for peaking bands
        float slope  = 0.5f;     // used for shelf bands
    };

    float       m_fs = 48000.0f;
    BandParams  m_band[NUM_BANDS];
    Biquad      m_filter[NUM_BANDS];
    bool        m_dirty = true;  // recalculate coefficients before next process()

    void updateCoefficients();
};
