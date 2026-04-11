#ifndef RXAUDIOPROCESSOR_H
#define RXAUDIOPROCESSOR_H

// RxAudioProcessor — RX audio noise-reduction engine.
//
// Lives on the main thread; processAudio() is called from the converter
// thread (TimeCriticalPriority).  All parameter access is mutex-protected.
//
// Responsibilities:
//   1. Apply noise reduction (Speex or ANR) to the incoming radio audio.
//   2. Mix sidetone (self-monitor, received via injectSidetone()) AFTER NR,
//      so the user's own voice is not processed by the noise reducer.
//   3. Apply post-NR output gain.
//   4. Handle mono and stereo input:
//        Mono (1 channel) — processed directly, channelSelect is ignored.
//        Stereo (2 channels) — de-interleaved before processing:
//          channelSelect 1 — process L only, pass R unmodified
//          channelSelect 2 — process R only, pass L unmodified
//          channelSelect 3 — sum L+R to mono, process, write result to both channels

#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QVector>
#include <QStandardPaths>
#include <QDateTime>
#include <atomic>

// Forward-declare pocketfft plan (full C header not needed in every TU).
struct rfft_plan_i;
using rfft_plan = rfft_plan_i*;

#ifndef Q_OS_LINUX
#include <Eigen/Eigen>
#include <Eigen/Dense>
#else
#include <eigen3/Eigen/Eigen>
#include <eigen3/Eigen/Dense>
#endif

#include <memory>
#include <vector>

#include "prefs.h"   // RxNrMode, rxAudioProcessingPrefs

class SpeexNrProcessor;   // forward — defined in speexnrprocessor.h
class AnrNrProcessor;     // forward — defined in anrnrprocessor.h
class TriplePara;         // forward — defined in triple_para.h

class RxAudioProcessor : public QObject
{
    Q_OBJECT

public:
    static constexpr int SPEC_FFT_LEN = 1024;  // bins = SPEC_FFT_LEN/2

    // Log-spaced spectrum rebinning parameters (must match TxAudioProcessor).
    static constexpr int   SPEC_BINS_PER_DECADE = 48;
    static constexpr float SPEC_FREQ_MIN        = 50.0f;    // Hz
    static constexpr float SPEC_FREQ_MAX        = 8000.0f;  // Hz

    explicit RxAudioProcessor(QObject* parent = nullptr);
    ~RxAudioProcessor();

    // ── Called from the converter thread ─────────────────────────────────────
    // samples: interleaved if stereo (L0 R0 L1 R1 …), or mono
    // channels: 1 or 2 (from audioSetup)
    Eigen::VectorXf processAudio(Eigen::VectorXf samples,
                                 float sampleRate,
                                 int   channels);

    // ── Parameter setters (any thread, mutex-protected) ───────────────────────
    void setBypassed(bool bypass);
    void setNrEnabled(bool en);
    void setNrMode(RxNrMode mode);
    void setChannelSelect(int ch);   // 1=L, 2=R, 3=mono sum

    // Speex
    void setSpeexSuppression(int dB);
    void setSpeexBandsPreset(int preset);
    void setSpeexFrameMs(int ms);
    void setSpeexAgc(bool en);
    void setSpeexAgcLevel(float v);
    void setSpeexAgcMaxGain(int dB);
    void setSpeexVad(bool en);
    void setSpeexVadProbStart(int pct);  // 0–100
    void setSpeexVadProbCont(int pct);   // 0–100
    void setSpeexSnrDecay(float v);         // 0.0–0.95
    void setSpeexNoiseUpdateRate(float v);  // 0.01–0.5
    void setSpeexPriorBase(float v);        // 0.05–0.5

    // ANR (Audacity Noise Reduction)
    void setAnrNoiseReductionDb(double dB);
    void setAnrSensitivity(double s);
    void setAnrFreqSmoothing(int bands);

    // RX Equalizer
    void setEqEnabled(bool en);
    void setEqBandGain(int idx, float dB);
    void setEqBandFreq(int idx, float hz);
    void setEqBandQ(int idx, float q);

    // Output gain
    void setOutputGainDB(float dB);

    // Spectrum capture — thread-safe; call from main thread.
    void setSpectrumEnabled(bool en);
    void setSpectrumFps(int fps);   // 1–60; default 10

    // ── Getters ───────────────────────────────────────────────────────────────
    bool  bypassed()       const;
    bool  nrEnabled()      const;
    RxNrMode nrMode()      const;
    int   channelSelect()  const;
    int   activeChannels() const;   // current stream channel count (1 or 2)
    float outputGainDB()   const;

    // Latency estimate from the active processor (ms, 0 if bypassed/disabled)
    float estimatedLatencyMs() const;

    // Number of Speex band presets available (reads filterbank.h at compile time)
    static int speexPresetCount();
    static int speexBandsForPreset(int preset);

    // ANR profile control — called from the main thread via wfmain slots.
    // startAnrProfile() flips the collection flag; the converter thread then
    // feeds samples via AnrNrProcessor::addProfileSamples().
    // stopAnrProfile()  finalises the profile and emits anrProfileReady().
    void startAnrProfile();
    void stopAnrProfile();
    bool anrIsProfiling() const;
    bool anrHasProfile()  const;

    // ── Debug WAV capture ──────────────────────────────────────────────────────
    // Captures 5 seconds of raw input audio (before any processing) to a WAV file.
    void startDebugCapture();

    // ── ANR noise profile persistence (main thread only) ──────────────────────
    // setNoiseStorePath() provides the full path to the per-radio .noise file.
    // Must be called before setRxMode() to enable load/save.
    void setNoiseStorePath(const QString& path);
    // Notify the processor that the demodulation mode has changed.
    // Saves the current profile (if any) under the old mode, then loads the
    // profile for the new mode from the store file (if one exists).
    // Emits anrModeProfileStatus() when done.
    void setRxMode(const QString& modeName);

    // Return the ANR noise profile as dB-scaled bins suitable for display.
    // bins: one dBFS value per FFT bin; sampleRate/windowSize from the profile.
    struct AnrProfileBins {
        QVector<double> bins;    // 10*log10(mean power) per bin
        double sampleRate;
        int    windowSize;
    };
    AnrProfileBins getAnrNoiseProfileBins() const;

public slots:
    // Connected to TxAudioProcessor::haveSidetoneFloat (Qt::DirectConnection).
    // Buffers the sidetone for mixing in processAudio().
    void injectSidetone(Eigen::VectorXf samples, quint32 sampleRate, int channels, QString format);

signals:
    void rxInputLevel(float peak);
    void rxOutputLevel(float peak);
    // Emitted when the audio stream channel count changes (1 or 2).
    void rxAudioChannelsChanged(int channels);
    // Emitted from stopAnrProfile() after the profile is built.
    void anrProfileReady(bool success);
    // Emitted at ~spectrumFps Hz when spectrum capture is enabled.
    // inBins/outBins: SPEC_FFT_LEN/2 dBFS values (bin k = k*effectiveSR/SPEC_FFT_LEN Hz).
    // rawSR: original audio sample rate.
    void rxSpectrumBins(QVector<double> inBins,
                        QVector<double> outBins,
                        float rawSR);
    // Emitted alongside anrProfileReady(true) with the dB-scaled noise profile.
    void anrNoiseProfileBins(QVector<double> bins, double sampleRate, int windowSize);
    // Emitted after setRxMode() and after stopAnrProfile() succeeds.
    // modeName is the current mode string; hasProfile is true when a valid
    // profile is active for that mode.
    void anrModeProfileStatus(QString modeName, bool hasProfile);
    // Debug capture signals
    void debugCaptureComplete(QString filePath);
    void debugCaptureStarted();

private:
    // ── Params snapshot (copied once per block under mutex) ───────────────────
    struct Params {
        bool     bypass        = false;
        bool     nrEnabled     = false;
        RxNrMode nrMode        = RxNrMode::None;
        int      channelSelect = 3;   // 1=L, 2=R, 3=mono sum

        // Speex
        int   speexSuppression    = -30;
        int   speexBandsPreset    =  3;
        int   speexFrameMs        = 20;
        bool  speexAgc            = false;
        float speexAgcLevel       = 8000.0f;
        int   speexAgcMaxGain     = 30;
        bool  speexVad            = false;
        int   speexVadProbStart   = 85;
        int   speexVadProbCont    = 65;
        float speexSnrDecay       = 0.7f;
        float speexNoiseUpdateRate= 0.03f;
        float speexPriorBase      = 0.1f;

        // ANR
        double anrNoiseReductionDb = 12.0;
        double anrSensitivity      =  6.0;
        int    anrFreqSmoothing    =  0;

        // EQ
        bool  eqEnabled = false;
        float eqGain[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float eqFreq[4] = {100.0f, 800.0f, 2000.0f, 3500.0f};
        float eqQ[4]    = {1.0f, 1.0f, 1.0f, 1.0f};

        // Output
        float outputGainDB = 0.0f;
    };

    mutable QMutex m_mutex;
    Params         m_params;

    // ── Sidetone ring buffer (protected by m_sidetoneMutex) ──────────────────
    QMutex              m_sidetoneMutex;
    std::vector<float>  m_sidetoneBuf;
    quint32             m_sidetoneSR       = 0;   // sample rate of incoming sidetone
    int                 m_sidetoneChannels = 0;   // channel count (always 1 from TX)
    QString             m_sidetoneFormat;          // format string (e.g. "float32")
    double              m_sidetoneResampleFrac = 0.0; // fractional sample carry-over for interpolation
    bool                m_sidetoneLoggedOnce   = false; // one-shot format diagnostic

    // ── Per-block state (converter thread only, no locking needed) ────────────
    float  m_activeSR       = 0.0f;
    int    m_activeChannels = 1;

    // Cached Speex params — detect changes to push into SpeexNrProcessor
    int    m_cachedSpeexSuppress = 0;
    int    m_cachedSpeexBands    = -1;
    int    m_cachedSpeexFrameMs  = 0;
    bool   m_cachedAgc          = false;
    float  m_cachedAgcLevel     = 0.0f;
    int    m_cachedAgcMax       = 0;
    bool   m_cachedVad          = false;
    int    m_cachedVadProbStart = -1;
    int    m_cachedVadProbCont  = -1;
    float  m_cachedSnrDecay       = -1.0f;
    float  m_cachedNoiseUpdateRate= -1.0f;
    float  m_cachedPriorBase      = -1.0f;

    std::unique_ptr<SpeexNrProcessor> m_speex;
    std::unique_ptr<AnrNrProcessor>   m_anr;
    std::unique_ptr<TriplePara>       m_eq;
    // Scratch buffer used when downmixing stereo for ANR profile collection.
    std::vector<float>                m_anrProfileMono;

    // ── Spectrum capture state ────────────────────────────────────────────────
    // m_specEnabled / m_specTargetFps are atomic: written from main thread,
    // read from converter thread.  All other members are converter-thread-only.
    // SPEC_FFT_LEN is declared public above.
    std::atomic<bool> m_specEnabled   { false };
    std::atomic<int>  m_specTargetFps { 10 };

    rfft_plan              m_specFftPlan  { nullptr };
    std::vector<double>    m_specFftBuf;
    std::vector<double>    m_specWindow;
    std::vector<float>     m_specInRing;
    std::vector<float>     m_specOutRing;
    int                    m_specRingPos     = 0;
    int                    m_specDecimFactor = 1;
    int                    m_specDecimCount  = 0;
    int                    m_specFftTrigger  = 0;
    QVector<double>        m_specInBins;
    QVector<double>        m_specOutBins;

    // ── Log-bin resampling (precomputed when sample rate changes) ─────────
    struct LogBinSpec {
        float linBinLo;   // fractional lower linear-bin index
        float linBinHi;   // fractional upper linear-bin index
    };
    std::vector<LogBinSpec> m_specLogBins;
    std::vector<double>     m_specLinMag;
    int                     m_specNumLogBins = 0;

    void appendSpectrumSamples(const Eigen::VectorXf& in,
                               const Eigen::VectorXf& out);

    // ── ANR persistence (main thread only — no locking required) ─────────────
    QString m_noiseStorePath;    // full path to the per-radio .noise JSON file
    QString m_currentModeName;   // current demodulation mode name

    // Save the current ANR profile under m_currentModeName into m_noiseStorePath.
    // Returns true on success.  Logs warnings on failure but never throws.
    bool saveCurrentProfile();
    // Load and restore the ANR profile for modeName from m_noiseStorePath.
    // Returns true when a valid profile was found and successfully restored.
    bool loadProfileForMode(const QString& modeName);

    // ── Debug WAV capture state (converter-thread-only except atomic flag) ────
    std::atomic<bool>      m_debugCapturing   { false };
    std::vector<float>     m_debugCaptureBuf;
    float                  m_debugCaptureSR   = 0.0f;
    int                    m_debugCaptureCh   = 0;
    int                    m_debugCaptureNeeded = 0;  // total float samples for 5 s

    void writeDebugWav();

    // ── Helpers ───────────────────────────────────────────────────────────────
    void pushSpeexParams(const Params& p);
    void pushAnrParams(const Params& p);
    void pushEqParams(const Params& p);
    std::vector<float> applyNr(const float* in, int n, float sr, const Params& p);
    void mixSidetone(Eigen::VectorXf& samples, int channels, const Params& p);
    static void applyGainDB(Eigen::VectorXf& s, float dB);
};

#endif // RXAUDIOPROCESSOR_H
