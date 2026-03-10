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
//        channelSelect 0 — auto (mono pass; stereo summed to mono then processed)
//        channelSelect 1 — process L only, pass R unmodified
//        channelSelect 2 — process R only, pass L unmodified
//        channelSelect 3 — sum L+R to mono, process, write result to both channels

#include <QObject>
#include <QMutex>
#include <QMutexLocker>
#include <QVector>

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

class RxAudioProcessor : public QObject
{
    Q_OBJECT

public:
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
    void setChannelSelect(int ch);   // 0,1,2,3

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

    // ANR (Audacity Noise Reduction)
    void setAnrNoiseReductionDb(double dB);
    void setAnrSensitivity(double s);
    void setAnrFreqSmoothing(int bands);

    // Output gain
    void setOutputGainDB(float dB);

    // ── Getters ───────────────────────────────────────────────────────────────
    bool  bypassed()       const;
    bool  nrEnabled()      const;
    RxNrMode nrMode()      const;
    int   channelSelect()  const;
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

public slots:
    // Connected to TxAudioProcessor::haveSidetoneFloat (Qt::QueuedConnection).
    // Buffers the sidetone for mixing in processAudio().
    void injectSidetone(Eigen::VectorXf samples, quint32 sampleRate);

signals:
    void rxInputLevel(float peak);
    void rxOutputLevel(float peak);
    // Emitted from stopAnrProfile() after the profile is built.
    void anrProfileReady(bool success);

private:
    // ── Params snapshot (copied once per block under mutex) ───────────────────
    struct Params {
        bool     bypass        = false;
        bool     nrEnabled     = false;
        RxNrMode nrMode        = RxNrMode::Speex;
        int      channelSelect = 0;

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

        // ANR
        double anrNoiseReductionDb = 12.0;
        double anrSensitivity      =  6.0;
        int    anrFreqSmoothing    =  0;

        // Output
        float outputGainDB = 0.0f;
    };

    mutable QMutex m_mutex;
    Params         m_params;

    // ── Sidetone ring buffer (protected by m_sidetoneMutex) ──────────────────
    QMutex              m_sidetoneMutex;
    std::vector<float>  m_sidetoneBuf;
    quint32             m_sidetoneSR = 0;

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

    std::unique_ptr<SpeexNrProcessor> m_speex;
    std::unique_ptr<AnrNrProcessor>   m_anr;
    // Scratch buffer used when downmixing stereo for ANR profile collection.
    std::vector<float>                m_anrProfileMono;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void pushSpeexParams(const Params& p);
    void pushAnrParams(const Params& p);
    std::vector<float> applyNr(const float* in, int n, float sr, const Params& p);
    void mixSidetone(Eigen::VectorXf& samples, int channels, const Params& p);
    static void applyGainDB(Eigen::VectorXf& s, float dB);
};

#endif // RXAUDIOPROCESSOR_H
