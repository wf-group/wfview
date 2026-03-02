#ifndef TXAUDIOPROCESSOR_H
#define TXAUDIOPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <QVector>
#include <memory>
#include <atomic>

#ifndef Q_OS_LINUX
#  include <Eigen/Dense>
#else
#  include <eigen3/Eigen/Dense>
#endif

#include "plugins/dysoncompress.h"
#include "plugins/mbeq.h"
#include "plugins/noisegate.h"

// ─────────────────────────────────────────────────────────────────────────────
// TxAudioProcessor
//
// Lives on the main thread (owned by wfmain).  The processAudio() method is
// called directly from the audioConverter thread (TimeCriticalPriority);
// thread safety is achieved with a QMutex that is held only for a brief
// parameter-snapshot at the start of each block.
//
// Signals are emitted from the converter thread; because the AudioProcessing-
// Widget connects with default (Qt::AutoConnection) they arrive on the main
// thread via the event queue — no extra care needed on the receiving side.
// ─────────────────────────────────────────────────────────────────────────────

class TxAudioProcessor : public QObject
{
    Q_OBJECT

public:
    static constexpr int EQ_BANDS = MbeqProcessor::BANDS; // 15

    explicit TxAudioProcessor(QObject* parent = nullptr);

    // ── Called from the audioConverter thread ────────────────────────────────
    // Processes one block of float samples.  Returns the processed block.
    // The sample rate is supplied so the processor can (re)create plugins when
    // it changes (e.g. different radio connection).
    Eigen::VectorXf processAudio(Eigen::VectorXf samples, float sampleRate);

    // ── Parameter setters — safe to call from the main thread at any time ────
    void setBypassed(bool bypass);          // master bypass — skips all DSP/gain
    void setCompEnabled(bool enabled);
    void setEqEnabled(bool enabled);
    void setEqFirst(bool eqFirst);          // true = EQ→Comp, false = Comp→EQ
    void setInputGainDB(float dB);          // applied before plugins
    void setOutputGainDB(float dB);         // applied after plugins
    void setEqBand(int idx, float gainDB);  // idx 0‥EQ_BANDS-1, dB -70‥+30
    void setCompPeakLimit(float dB);        // -30‥0, default -10
    void setCompRelease(float seconds);     // 0‥1, default 0.1
    void setCompFastRatio(float ratio);     // 0‥1, default 0.5
    void setCompSlowRatio(float ratio);     // 0‥1, default 0.3
    void setSidetoneEnabled(bool enabled);
    void setSidetoneLevel(float level);     // 0‥1 linear gain
    void setMuteRx(bool muted);             // mute RX while self-monitoring
    // Enable/disable spectrum capture (thread-safe; main thread).
    void setSpectrumEnabled(bool en);
    // Noise gate (runs before input gain)
    void setGateEnabled(bool enabled);
    void setGateThreshold(float dB);        // -70‥0, default -40
    void setGateAttack(float ms);           // 0.01‥1000, default 10
    void setGateHold(float ms);             // 2‥2000, default 100
    void setGateDecay(float ms);            // 2‥4000, default 200
    void setGateRange(float dB);            // -90‥0, default -90
    void setGateLfCutoff(float hz);         // 20‥4000, default 80
    void setGateHfCutoff(float hz);         // 200‥20000, default 8000

    // ── Getters (main thread) ─────────────────────────────────────────────────
    bool bypassed()       const;
    bool compEnabled()    const;
    bool eqEnabled()      const;
    bool eqFirst()        const;
    float inputGainDB()   const;
    float outputGainDB()  const;
    float eqBand(int idx) const;
    float compPeakLimit() const;
    float compRelease()   const;
    float compFastRatio() const;
    float compSlowRatio() const;
    bool sidetoneEnabled()  const;
    float sidetoneLevel()   const;
    bool muteRx()           const;
    bool gateEnabled()      const;
    float gateThreshold()   const;
    float gateAttack()      const;
    float gateHold()        const;
    float gateDecay()       const;
    float gateRange()       const;
    float gateLfCutoff()    const;
    float gateHfCutoff()    const;

signals:
    // Peak amplitude values 0.0–1.0, emitted each processed block.
    void txInputLevel(float peak);
    void txOutputLevel(float peak);
    // Linear gain from compressor (1.0 = no gain reduction; 0.0 = −∞ dB).
    void txGainReduction(float linearGain);
    // Sidetone: processed float audio at given sample rate.
    void haveSidetoneFloat(Eigen::VectorXf samples, quint32 sampleRate);
    // Emitted when the RX mute state changes; connect to audio class setRxMuted().
    void haveRxMuted(bool muted);
    // Emitted ~30 Hz when spectrum capture is enabled.
    // inputSamples:  audio after input gain, before DSP (matches input meter).
    // outputSamples: audio after output gain + clip (matches output meter).
    // In bypass mode both carry the unmodified microphone signal.
    void txSpectrumSamples(QVector<float> inputSamples,
                           QVector<float> outputSamples,
                           float sampleRate);

private:
    // ── Thread-safe parameter block ──────────────────────────────────────────
    struct Params {
        bool  bypass        = false;   // master bypass
        bool  compEnabled   = false;
        bool  eqEnabled     = false;
        bool  eqFirst       = true;    // true = EQ first
        float inputGainDB   = 0.0f;
        float outputGainDB  = 0.0f;
        float eqBands[MbeqProcessor::BANDS] = {};
        float compPeakLimit = -10.0f;
        float compRelease   = 0.1f;
        float compFastRatio = 0.5f;
        float compSlowRatio = 0.3f;
        bool  sidetoneEnabled = false;
        float sidetoneLevel   = 0.5f;
        bool  muteRx          = false;
        // Noise gate
        bool  gateEnabled    = false;
        float gateThreshold  = -40.0f;
        float gateAttack     =  10.0f;
        float gateHold       = 100.0f;
        float gateDecay      = 200.0f;
        float gateRange      = -90.0f;
        float gateLfCutoff   =  80.0f;
        float gateHfCutoff   = 8000.0f;
    };

    mutable QMutex            m_mutex;
    Params                    m_params;   // written from main thread under mutex

    // ── DSP state (converter thread only after first call) ───────────────────
    float                     m_activeSR  = 0.0f;
    std::unique_ptr<NoiseGate>       m_gate;
    std::unique_ptr<DysonCompressor> m_comp;
    std::unique_ptr<MbeqProcessor>   m_eq;

    // Helper: apply linear gain to samples in-place
    static void applyGainDB(Eigen::VectorXf& s, float dB);

    // ── Spectrum capture state (converter thread only after construction) ─────
    // Enable flag is atomic so the main thread can toggle it safely.
    std::atomic<bool> m_specEnabled { false };
    QVector<float>    m_specInBuf;   // input  accumulator
    QVector<float>    m_specOutBuf;  // output accumulator
    int               m_specThresh = 0;  // samples per 30 Hz frame (sr/30)

    // Append one audio block to the accumulators; emit when threshold reached.
    void appendSpectrumSamples(const Eigen::VectorXf& in,
                               const Eigen::VectorXf& out);
};

#endif // TXAUDIOPROCESSOR_H
