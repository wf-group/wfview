#ifndef TXAUDIOPROCESSOR_H
#define TXAUDIOPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <memory>

#ifndef Q_OS_LINUX
#  include <Eigen/Dense>
#else
#  include <eigen3/Eigen/Dense>
#endif

#include "plugins/dysoncompress.h"
#include "plugins/mbeq.h"

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
    };

    mutable QMutex            m_mutex;
    Params                    m_params;   // written from main thread under mutex

    // ── DSP state (converter thread only after first call) ───────────────────
    float                     m_activeSR  = 0.0f;
    std::unique_ptr<DysonCompressor> m_comp;
    std::unique_ptr<MbeqProcessor>   m_eq;

    // Helper: apply linear gain to samples in-place
    static void applyGainDB(Eigen::VectorXf& s, float dB);
};

#endif // TXAUDIOPROCESSOR_H
