#ifndef TXAUDIOPROCESSOR_H
#define TXAUDIOPROCESSOR_H

#include <QObject>
#include <QMutex>
#include <QVector>
#include <memory>
#include <atomic>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef Q_OS_LINUX
#  include <Eigen/Dense>
#else
#  include <eigen3/Eigen/Dense>
#endif

#include "plugins/dysoncompress.h"
#include "plugins/mbeq.h"
#include "plugins/noisegate.h"

// Forward-declare pocketfft plan so the full C header isn't pulled into every
// translation unit that includes this header.
struct rfft_plan_i;
using rfft_plan = rfft_plan_i*;

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
    static constexpr int EQ_BANDS    = MbeqProcessor::BANDS; // 15
    static constexpr int SPEC_FFT_LEN = 1024;               // bins = SPEC_FFT_LEN/2

    // Log-spaced spectrum rebinning parameters.
    // Output bins are spaced logarithmically from SPEC_FREQ_MIN to SPEC_FREQ_MAX,
    // with SPEC_BINS_PER_DECADE bins per frequency decade.  This gives even
    // resolution per octave when displayed on a log frequency axis.
    static constexpr int   SPEC_BINS_PER_DECADE = 48;
    static constexpr float SPEC_FREQ_MIN        = 50.0f;    // Hz
    static constexpr float SPEC_FREQ_MAX        = 8000.0f;  // Hz

    explicit TxAudioProcessor(QObject* parent = nullptr);
    ~TxAudioProcessor();

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
    void setSidetoneDelay(int delay);
    void setMuteRx(bool muted);             // mute RX while self-monitoring
    // Enable/disable spectrum capture and set target frame rate (thread-safe; main thread).
    void setSpectrumEnabled(bool en);
    void setSpectrumFps(int fps);       // 1–60; default 30
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
    void txOutputLevels(float RMS, float peak);
    void txInputLevels(float RMS, float peak);
    // Linear gain from compressor (1.0 = no gain reduction; 0.0 = −∞ dB).
    void txGainReduction(float linearGain);
    // Sidetone: processed float audio at given sample rate, channel count, format description.
    void haveSidetoneFloat(Eigen::VectorXf samples, quint32 sampleRate, int channels, QString format);
    // Emitted when the RX mute state changes; connect to audio class setRxMuted().
    void haveRxMuted(bool muted);
    // Emitted at ~spectrumFps Hz when spectrum capture is enabled.
    // inBins/outBins: SPEC_FFT_LEN/2 dBFS values (bins 0..511, bin k = k*effectiveSR/SPEC_FFT_LEN Hz).
    // rawSR: original audio sample rate (use to set EQ band visibility / derive effective SR).
    void txSpectrumBins(QVector<double> inBins,
                        QVector<double> outBins,
                        float rawSR);

private:
    // ── Sidetone buffer (for RtAudio compatibility) ───────────────────────────
    QList<Eigen::VectorXf>    m_sidetoneBuffer;
    int                       m_sidetoneDelay = 0;
    bool                      m_sidetoneLoggedOnce = false;

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
        bool showRMSValues   = false;
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

    // ── Spectrum capture state ────────────────────────────────────────────────
    // m_specEnabled / m_specTargetFps are atomic: written from main thread,
    // read from converter thread.  All other members are converter-thread-only.
    std::atomic<bool> m_specEnabled    { false };
    std::atomic<int>  m_specTargetFps  { 30 };

    rfft_plan              m_specFftPlan  { nullptr };  // pocketfft real-FFT plan
    std::vector<double>    m_specFftBuf;                // in-place work buffer for rfft_forward
    std::vector<double>    m_specWindow;                // Hanning coefficients
    std::vector<float>     m_specInRing;                // input  ring buffer (SPEC_FFT_LEN floats)
    std::vector<float>     m_specOutRing;               // output ring buffer
    int                    m_specRingPos     = 0;
    int                    m_specDecimFactor = 1;       // K = round(activeSR / 16000)
    int                    m_specDecimCount  = 0;
    int                    m_specFftTrigger  = 0;       // decimated-sample counter
    QVector<double>        m_specInBins;                // reused output bins (deep-copied on emit)
    QVector<double>        m_specOutBins;

    // ── Log-bin resampling (precomputed when sample rate changes) ─────────
    // Each log bin maps to a fractional range of linear FFT bins.
    struct LogBinSpec {
        float linBinLo;   // fractional lower linear-bin index
        float linBinHi;   // fractional upper linear-bin index
    };
    std::vector<LogBinSpec> m_specLogBins;     // one per log-spaced output bin
    std::vector<double>     m_specLinMag;      // scratch: linear magnitudes from FFT
    int                     m_specNumLogBins = 0;

    // Decimates, fills ring buffers, triggers FFT and emits txSpectrumBins.
    void appendSpectrumSamples(const Eigen::VectorXf& in,
                               const Eigen::VectorXf& out);
};

#endif // TXAUDIOPROCESSOR_H
