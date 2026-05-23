// Dyson Compressor — standalone C++ implementation
// Ported from LADSPA plugin #1403
// Original algorithm: Copyright (c) 1996, John S. Dyson.  BSD 2-clause.

#include "dysoncompress.h"
#include <cstring>
#include <cmath>
#include <algorithm>

// Helper inlined from the original ladspa-util.h
static inline float DB_CO(float g)
{
    return (g > -90.0f) ? std::pow(10.0f, g * 0.05f) : 0.0f;
}

// ─────────────────────────────────────────────────────────────────────────────

DysonCompressor::DysonCompressor(float sr)
    : sampleRate(sr)
    , ndelay(static_cast<int>(1.0f / RLEVELSQ0FFILTER))   // = 1000
{
    delay     = static_cast<float*>(std::calloc(static_cast<size_t>(ndelay),      sizeof(float)));
    rlevelsqn = static_cast<float*>(std::calloc(static_cast<size_t>(NFILT  + 1),  sizeof(float)));
    rlevelsqe = static_cast<float*>(std::calloc(static_cast<size_t>(NEFILT + 1),  sizeof(float)));
    reset();
}

DysonCompressor::~DysonCompressor()
{
    std::free(delay);
    std::free(rlevelsqn);
    std::free(rlevelsqe);
}

void DysonCompressor::reset()
{
    std::memset(delay,     0, static_cast<size_t>(ndelay)      * sizeof(float));
    std::memset(rlevelsqn, 0, static_cast<size_t>(NFILT  + 1)  * sizeof(float));
    std::memset(rlevelsqe, 0, static_cast<size_t>(NEFILT + 1)  * sizeof(float));

    mingain         = 10000.0f;
    maxgain         = 0.0f;
    rgain           = 1.0f;
    rmastergain0    = 1.0f;
    rlevelsq0       = 0.0f;
    rlevelsq1       = 0.0f;
    rpeakgain0      = 1.0f;
    rpeakgain1      = 1.0f;
    rpeaklimitdelay = 0;
    ndelayptr       = 0;
    lastrgain       = 1.0f;
    extraMaxlevel   = 0.0f;
    peaklimitdelay  = 0;
    lastTotalGain   = 1.0f;
}

void DysonCompressor::setPeakLimit(float dB)   { peakLimitDB = dB; }
void DysonCompressor::setReleaseTime(float s)  { releaseTime = s;  }
void DysonCompressor::setFastRatio(float r)    { fastRatio   = r;  }
void DysonCompressor::setSlowRatio(float r)    { slowRatio   = r;  }

// ─────────────────────────────────────────────────────────────────────────────

void DysonCompressor::process(const float* in, float* out, unsigned long nSamples)
{
    const float targetlevel              = MAXLEVEL * DB_CO(peakLimitDB);
    const float rgainfilter              = 1.0f / (std::fmax(releaseTime, 1e-7f) * sampleRate);
    const float fastgaincompressionratio = fastRatio;
    const float compressionratio         = slowRatio;

    for (unsigned long pos = 0; pos < nSamples; ++pos) {

        // ── Level detection ──────────────────────────────────────────────────
        const float levelsq0 = 2.0f * (in[pos] * in[pos]);

        delay[ndelayptr] = in[pos];
        if (++ndelayptr >= ndelay) ndelayptr = 0;

        if (levelsq0 > rlevelsq0)
            rlevelsq0 = levelsq0 * RLEVELSQ0FFILTER + rlevelsq0 * (1.0f - RLEVELSQ0FFILTER);
        else
            rlevelsq0 = levelsq0 * RLEVELSQ0FILTER  + rlevelsq0 * (1.0f - RLEVELSQ0FILTER);

        // ── AGC (skipped when signal is below noise floor) ───────────────────
        if (rlevelsq0 > FLOORLEVEL * FLOORLEVEL) {

            if (rlevelsq0 > rlevelsq1)
                rlevelsq1 = rlevelsq0;
            else
                rlevelsq1 = rlevelsq0 * RLEVELSQ1FILTER + rlevelsq1 * (1.0f - RLEVELSQ1FILTER);

            rlevelsqn[0] = rlevelsq1;
            for (int i = 0; i < NFILT - 1; ++i) {
                if (rlevelsqn[i] > rlevelsqn[i + 1])
                    rlevelsqn[i + 1] = rlevelsqn[i];
                else
                    rlevelsqn[i + 1] = rlevelsqn[i] * RLEVELSQ1FILTER
                                       + rlevelsqn[i + 1] * (1.0f - RLEVELSQ1FILTER);
            }

            float efilt    = RLEVELSQEFILTER;
            float levelsqe = rlevelsqe[0] = rlevelsqn[NFILT - 1];
            for (int i = 0; i < NEFILT - 1; ++i) {
                rlevelsqe[i + 1] = rlevelsqe[i] * efilt + rlevelsqe[i + 1] * (1.0f - efilt);
                if (rlevelsqe[i + 1] > levelsqe) levelsqe = rlevelsqe[i + 1];
                efilt *= 1.0f / 1.5f;
            }

            float gain = targetlevel / std::sqrt(levelsqe);
            if (compressionratio < 0.99f) {
                if (compressionratio == 0.50f)
                    gain = std::sqrt(gain);
                else
                    gain = std::exp(std::log(gain) * compressionratio);
            }

            if (gain < rgain)
                rgain = gain * RLEVELSQEFILTER / 2.0f + rgain * (1.0f - RLEVELSQEFILTER / 2.0f);
            else
                rgain = gain * rgainfilter + rgain * (1.0f - rgainfilter);

            lastrgain = rgain;
            if (gain < lastrgain) lastrgain = gain;
        }

        // ── Gain application with peak limiting ──────────────────────────────
        const float tgain = lastrgain;
        const float d     = delay[ndelayptr];

        float fastgain = std::min(tgain, MAXFASTGAIN);
        if (fastgain < 1e-4f) fastgain = 1e-4f;

        const float qgain     = std::exp(std::log(fastgain) * fastgaincompressionratio);
        const float tslowgain = std::min(tgain / qgain, MAXSLOWGAIN);

        if (tslowgain < rmastergain0)
            rmastergain0 = tslowgain;
        else
            rmastergain0 = tslowgain * RMASTERGAIN0FILTER
                           + rmastergain0 * (1.0f - RMASTERGAIN0FILTER);

        const float npeakgain = rmastergain0 * qgain;
        const float new_f     = d * npeakgain;

        const float nrgain = (std::fabs(new_f) >= MAXLEVEL)
                             ? MAXLEVEL / std::fabs(new_f)
                             : 1.0f;

        const float ngsq = nrgain * nrgain;
        if (ngsq <= rpeakgain0) {
            rpeakgain0      = ngsq;
            rpeaklimitdelay = peaklimitdelay;
        } else if (rpeaklimitdelay == 0) {
            const float tnrgain = (nrgain > 1.0f) ? 1.0f : nrgain;
            rpeakgain0 = tnrgain * RPEAKGAINFILTER + rpeakgain0 * (1.0f - RPEAKGAINFILTER);
        }

        if (rpeakgain0 <= rpeakgain1) {
            rpeakgain1      = rpeakgain0;
            rpeaklimitdelay = peaklimitdelay;
        } else if (rpeaklimitdelay == 0) {
            rpeakgain1 = RPEAKGAINFILTER * rpeakgain0 + rpeakgain1 * (1.0f - RPEAKGAINFILTER);
        } else {
            --rpeaklimitdelay;
        }

        const float sqrtrpeakgain = std::sqrt(rpeakgain1);
        const float totalgain     = npeakgain * sqrtrpeakgain;

        out[pos] = new_f * sqrtrpeakgain;

        if (totalgain > maxgain)      maxgain = totalgain;
        if (totalgain < mingain)      mingain = totalgain;
        if (out[pos] > extraMaxlevel) extraMaxlevel = out[pos];

        lastTotalGain = totalgain;
    }
}
