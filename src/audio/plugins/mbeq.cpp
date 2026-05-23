// Multiband EQ — standalone C++ implementation using Eigen FFT
// Ported from LADSPA plugin #1197 (Steve Harris, GPL)
// Band range remapped; FFT back-end replaced with Eigen::FFT<float>.
//
// Normalization note:
//   Eigen::FFT normalizes its inverse transform by 1/N (unlike FFTW v2 rfftw,
//   which was unnormalized with round-trip scale = N).  With Eigen the round-trip
//   is unity, so the OLA accumulation divides only by OVER_SAMP (not FFT_LEN*OVER_SAMP).

#include "mbeq.h"

#include <cstring>
#include <cmath>
#include <algorithm>

// Use the compiler-builtin __linux__ rather than Qt's Q_OS_LINUX so that
// this file can be compiled without pulling in any Qt headers.
#ifdef __linux__
#  include <eigen3/unsupported/Eigen/FFT>
#else
// TODO: Verify macOS and Windows path
#  include <unsupported/Eigen/FFT>
#endif

// ─── constexpr definitions ────────────────────────────────────────────────────
// (values declared in the header as constexpr must be defined here for ODR)
constexpr float MbeqProcessor::bandFreqs[MbeqProcessor::BANDS];

// ─────────────────────────────────────────────────────────────────────────────

MbeqProcessor::MbeqProcessor(float sr)
    : sampleRate(sr)
{
    for (int i = 0; i < BANDS; ++i)
        gains[i] = 0.0f;   // all bands flat by default

    binBase .resize(FFT_LEN / 2, 0);
    binDelta.resize(FFT_LEN / 2, 0.0f);

    window  .resize(FFT_LEN,     0.0f);
    inFifo  .resize(FFT_LEN,     0.0f);
    outFifo .resize(FFT_LEN,     0.0f);
    outAccum.resize(FFT_LEN * 2, 0.0f);

    buildWindow();
    buildDbTable();
    buildBinMaps();

    // Initialise fifo position to the latency offset so the first output
    // block lines up correctly (matches original LADSPA behaviour).
    const int latency = FFT_LEN - FFT_LEN / OVER_SAMP;
    fifoPos = latency;
}

// ─── public API ───────────────────────────────────────────────────────────────

void MbeqProcessor::setBand(int idx, float gainDB)
{
    if (idx >= 0 && idx < BANDS)
        gains[idx] = gainDB;
}

float MbeqProcessor::getBand(int idx) const
{
    return (idx >= 0 && idx < BANDS) ? gains[idx] : 0.0f;
}

void MbeqProcessor::process(const float* in, float* out, int nSamples)
{
    const int step_size = FFT_LEN / OVER_SAMP;                   // 256
    const int latency   = FFT_LEN - step_size;                    // 768

    // Compute per-bin coefficients from current dB gains
    float coefs[FFT_LEN / 2];
    computeCoefs(coefs);

    Eigen::FFT<float> fft;
    std::vector<float>                realBuf(FFT_LEN);
    std::vector<std::complex<float>>  freqBuf;

    for (int pos = 0; pos < nSamples; ++pos) {
        inFifo[fifoPos] = in[pos];
        out[pos] = outFifo[fifoPos - latency];
        ++fifoPos;

        if (fifoPos >= FFT_LEN) {
            fifoPos = latency;

            // Window the input FIFO
            for (int i = 0; i < FFT_LEN; ++i)
                realBuf[i] = inFifo[i] * window[i];

            // Forward FFT (unnormalized)
            fft.fwd(freqBuf, realBuf);

            // Apply gain coefficients to both positive and negative frequencies
            // to maintain Hermitian symmetry required for a real IFFT output.
            freqBuf[0] = std::complex<float>(0.0f, 0.0f);  // DC: kill it (coefs[0] == 0)
            for (int i = 1; i < FFT_LEN / 2; ++i) {
                freqBuf[i]           *= coefs[i];
                freqBuf[FFT_LEN - i] *= coefs[i];
            }
            // Nyquist bin
            freqBuf[FFT_LEN / 2] *= coefs[FFT_LEN / 2 - 1];

            // Inverse FFT (also unnormalized — round-trip scale = FFT_LEN)
            fft.inv(realBuf, freqBuf);

            // Window and accumulate with overlap-add scaling.
            // Note: Eigen::FFT normalizes the inverse transform by 1/N (unlike the original
            // FFTW v2 rfftw which was unnormalized, round-trip scale = N).  The original
            // LADSPA scale factor was 4/(FFT_LEN*OVER_SAMP) = 1/FFT_LEN to cancel FFTW's N.
            // With Eigen the round-trip is already unity, so we only divide by OVER_SAMP.
            const float scale = 4.0f / static_cast<float>(OVER_SAMP);  // = 1.0 for OVER_SAMP=4
            for (int i = 0; i < FFT_LEN; ++i)
                outAccum[i] += window[i] * realBuf[i] * scale;

            // Push this step to the output FIFO
            for (int i = 0; i < step_size; ++i)
                outFifo[i] = outAccum[i];

            // Shift the accumulator
            std::memmove(outAccum.data(), outAccum.data() + step_size,
                         static_cast<size_t>(FFT_LEN) * sizeof(float));
            std::memset(outAccum.data() + FFT_LEN, 0,
                        static_cast<size_t>(step_size) * sizeof(float));

            // Shift the input FIFO
            for (int i = 0; i < latency; ++i)
                inFifo[i] = inFifo[i + step_size];
        }
    }
}

// ─── private helpers ──────────────────────────────────────────────────────────

void MbeqProcessor::buildWindow()
{
    for (int i = 0; i < FFT_LEN; ++i)
        window[i] = -0.5f * std::cos(2.0f * M_PIf * i / FFT_LEN) + 0.5f;
}

void MbeqProcessor::buildDbTable()
{
    // Index: (dB + 70) * 10, covering -70 dB to +30 dB in 0.1 dB steps.
    dbTable.resize(1000);
    for (int i = 0; i < 1000; ++i) {
        const float db = (static_cast<float>(i) / 10.0f) - 70.0f;
        dbTable[i] = std::pow(10.0f, db / 20.0f);
    }
}

void MbeqProcessor::buildBinMaps()
{
    const float hzPerBin = sampleRate / static_cast<float>(FFT_LEN);

    // Bins below the lowest band all map to band 0
    int bin = 1;
    while (bin <= static_cast<int>(bandFreqs[0] / hzPerBin)) {
        binBase [bin] = 0;
        binDelta[bin] = 0.0f;
        ++bin;
    }

    // Interpolate between successive bands
    for (int i = 0; i < BANDS - 1 && bin < FFT_LEN / 2 - 1; ++i) {
        const float lastBinF = static_cast<float>(bin);
        const float nextBinF = bandFreqs[i + 1] / hzPerBin;
        while (bin <= static_cast<int>(nextBinF) && bin < FFT_LEN / 2 - 1) {
            binBase [bin] = i;
            binDelta[bin] = (static_cast<float>(bin) - lastBinF)
                            / (nextBinF - lastBinF);
            ++bin;
        }
    }

    // Remaining high bins all map to the last band
    while (bin < FFT_LEN / 2 - 1) {
        binBase [bin] = BANDS - 1;
        binDelta[bin] = 0.0f;
        ++bin;
    }
}

void MbeqProcessor::computeCoefs(float* coefs) const
{
    // Convert dB gains to linear coefficients via the lookup table
    float linGains[BANDS];
    for (int i = 0; i < BANDS; ++i) {
        const int idx = static_cast<int>(gains[i] * 10.0f + 700.0f);
        const int clamped = std::max(0, std::min(999, idx));
        linGains[i] = dbTable[clamped];
    }

    // DC bin: always zero (see original)
    coefs[0] = 0.0f;

    // Interpolate linearly between adjacent band coefficients
    for (int bin = 1; bin < FFT_LEN / 2; ++bin) {
        const int   b = binBase [bin];
        const float d = binDelta[bin];
        const float next = (b + 1 < BANDS) ? linGains[b + 1] : linGains[b];
        coefs[bin] = (1.0f - d) * linGains[b] + d * next;
    }
}
