#pragma once

#include <vector>
#include <complex>

#ifndef M_PIf
#define M_PIf 3.14159265358979323846f
#endif

// Multiband EQ — standalone C++ class using Eigen FFT
// Ported from LADSPA plugin #1197 (Steve Harris, GPL)
// Band range remapped to 50 Hz – 8 kHz for voice/ham-radio TX use.
//
// Algorithm: FFT-based overlap-add graphical EQ with OVER_SAMP=4 (75% overlap).
// Latency: FFT_LEN - FFT_LEN/OVER_SAMP = 768 samples (~16 ms at 48 kHz).

class MbeqProcessor
{
public:
    static constexpr int BANDS     = 15;
    static constexpr int FFT_LEN   = 1024;
    static constexpr int OVER_SAMP = 4;

    // Band centre frequencies (Hz) — voice-optimised 50 Hz to 8 kHz.
    static constexpr float bandFreqs[BANDS] = {
          50.0f,   // 0  low shelf  — rumble
         100.0f,   // 1             — warmth / mud
         200.0f,   // 2             — boxy low-mids
         300.0f,   // 3             — voice fundamental
         400.0f,   // 4             — male resonance
         600.0f,   // 5             — nasal low
         800.0f,   // 6             — nasal high
        1000.0f,   // 7             — presence mid
        1200.0f,   // 8             — clarity
        1600.0f,   // 9             — intelligibility
        2000.0f,   // 10            — consonants
        2500.0f,   // 11            — sibilance onset
        3200.0f,   // 12            — brilliance
        5000.0f,   // 13            — brightness
        8000.0f    // 14 high shelf — air / bandwidth limit
    };

    explicit MbeqProcessor(float sampleRate = 48000.0f);

    // Set per-band gain.  gainDB in [-70, +30].  0 dB = unity gain (flat).
    void setBand(int bandIndex, float gainDB);

    // Retrieve current gain for a band.
    float getBand(int bandIndex) const;

    // Process a block of mono float32 samples in-place or with separate buffers.
    // 'in' and 'out' may point to the same buffer.
    void process(const float* in, float* out, int nSamples);

private:
    float sampleRate;
    float gains[BANDS];          // dB per band

    // Per-FFT-bin interpolation tables (built once from bandFreqs + sampleRate)
    std::vector<int>   binBase;   // which band index maps to each bin
    std::vector<float> binDelta;  // interpolation weight toward next band

    // dB → linear coefficient lookup  (index = (dB + 70) * 10, range 0..999)
    std::vector<float> dbTable;

    // Overlap-add FIFO/accumulator buffers
    std::vector<float> window;    // raised-cosine window, length FFT_LEN
    std::vector<float> inFifo;    // input FIFO,           length FFT_LEN
    std::vector<float> outFifo;   // output FIFO (step),   length FFT_LEN
    std::vector<float> outAccum;  // overlap-add acc.,     length FFT_LEN * 2

    long fifoPos {0};

    void buildWindow();
    void buildDbTable();
    void buildBinMaps();

    // Compute per-bin gain coefficients from current dB gains
    void computeCoefs(float* coefs) const;
};
