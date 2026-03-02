#ifndef AUDIOPROCESSINGWIDGET_H
#define AUDIOPROCESSINGWIDGET_H

#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>

#include <QVector>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#include <vector>

#include "prefs.h"
#include "meter.h"
#include "spectrumwidget.h"
#include "txaudioprocessor.h"

// Eigen FFT for the block-FFT spectrum analyser
// Use the compiler-builtin __linux__ rather than Qt's Q_OS_LINUX so that
// this file can be compiled without pulling in any Qt headers.
#ifdef __linux__
#  include <eigen3/unsupported/Eigen/FFT>
#else
// TODO: Verify macOS and Windows path
#  include <unsupported/Eigen/FFT>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// AudioProcessingWidget — modal-less dialog for TX audio processing.
//
// Emits prefsChanged(audioProcessingPrefs) whenever any control changes.
// Call setPrefs() to populate the controls from saved preferences.
// Connect TxAudioProcessor meter signals to the updateXxxLevel() slots.
// ─────────────────────────────────────────────────────────────────────────────

class AudioProcessingWidget : public QDialog
{
    Q_OBJECT

public:
    explicit AudioProcessingWidget(QWidget* parent = nullptr);

    void setPrefs(const audioProcessingPrefs& p);
    audioProcessingPrefs getPrefs() const;

signals:
    void prefsChanged(audioProcessingPrefs p);

public slots:
    void updateInputLevel(float peak);      // 0.0–1.0
    void updateOutputLevel(float peak);     // 0.0–1.0
    void updateGainReduction(float linear); // 1.0 = no reduction
    // Receives batches of raw audio from TxAudioProcessor (~30 Hz).
    void onSpectrumSamples(QVector<float> inputSamples,
                           QVector<float> outputSamples,
                           float sampleRate);
    void setConnected(bool connected);      // clears spectrum when disconnected

private slots:
    void onAnyControlChanged();
    void onBypassToggled(bool bypassed);
    void onClearEq();
    void onSpecEnableToggled(bool enabled);
    void onSpecDiagTimer();  // 1 Hz diagnostic log for spectrum FFT stats
    void onPluginOrderChanged(int index); // reorders eqGrp/compGrp visually

private:
    void buildUi();
    void blockAll(bool block);
    void populateFromPrefs(const audioProcessingPrefs& p);
    void setProcessingControlsEnabled(bool enabled);
    void updateEqBandVisibility(float sampleRate); // hide bands above Nyquist
    void reorderDspGroups(bool eqFirst);           // swaps eqGrp/compGrp in m_dspOrderLayout

    // ── Master bypass ────────────────────────────────────────────────────────
    QCheckBox*    bypassCheck   {nullptr};

    // ── Noise gate ───────────────────────────────────────────────────────────
    QGroupBox*    gateGrp         {nullptr};
    QCheckBox*    gateEnable      {nullptr};
    QSlider*      gateThreshold   {nullptr};  // -70..0 dB (×1)
    QSlider*      gateAttack      {nullptr};  // 1..500 ms (×1)
    QSlider*      gateHold        {nullptr};  // 2..2000 ms (×1)
    QSlider*      gateDecay       {nullptr};  // 2..2000 ms (×1)
    QSlider*      gateRange       {nullptr};  // -90..0 dB (×1)
    QSlider*      gateLfCutoff    {nullptr};  // 20..4000 Hz (×1)
    QSlider*      gateHfCutoff    {nullptr};  // 200..20000 Hz (×1)
    QLabel*       lblGateThreshold{nullptr};
    QLabel*       lblGateAttack   {nullptr};
    QLabel*       lblGateHold     {nullptr};
    QLabel*       lblGateDecay    {nullptr};
    QLabel*       lblGateRange    {nullptr};
    QLabel*       lblGateLfCutoff {nullptr};
    QLabel*       lblGateHfCutoff {nullptr};

    // ── EQ ─────────────────────────────────────────────────────────────────
    static constexpr int EQ_BANDS = TxAudioProcessor::EQ_BANDS;
    QCheckBox*    eqEnable      {nullptr};
    QPushButton*  clearEqBtn    {nullptr};
    QSlider*      eqSliders [EQ_BANDS] {};
    QLabel*       eqValues  [EQ_BANDS] {};  // shows dB value text
    QWidget*      eqColumns [EQ_BANDS] {};  // column containers for show/hide

    // ── Compressor ──────────────────────────────────────────────────────────
    QCheckBox*    compEnable    {nullptr};
    QSlider*      compPeakLimit {nullptr};  // -30..0 dB (×10 for int steps)
    QSlider*      compRelease   {nullptr};  // 1..100 (×0.01 s)
    QSlider*      compFastRatio {nullptr};  // 0..100 (×0.01)
    QSlider*      compSlowRatio {nullptr};  // 0..100 (×0.01)
    QLabel*       lblPeak       {nullptr};
    QLabel*       lblRelease    {nullptr};
    QLabel*       lblFast       {nullptr};
    QLabel*       lblSlow       {nullptr};

    // ── Gain ────────────────────────────────────────────────────────────────
    QSlider*      inputGain     {nullptr};  // -200..200 (×0.1 dB)
    QSlider*      outputGain    {nullptr};
    QLabel*       lblInputGain  {nullptr};
    QLabel*       lblOutputGain {nullptr};

    // ── Plugin order ────────────────────────────────────────────────────────
    QComboBox*    orderCombo    {nullptr};

    // ── Sidetone ────────────────────────────────────────────────────────────
    QCheckBox*    sidetoneEnable{nullptr};
    QCheckBox*    muteRxCheck   {nullptr};
    QSlider*      sidetoneLevel {nullptr};  // 0..100 (×0.01)
    QLabel*       lblSidetone   {nullptr};

    // ── Processing group boxes (disabled while bypassed) ─────────────────────
    QGroupBox*    gainGrp       {nullptr};
    QGroupBox*    eqGrp         {nullptr};
    QGroupBox*    compGrp       {nullptr};
    QGroupBox*    sidetoneGrp   {nullptr};
    QWidget*      orderRow      {nullptr};

    // ── DSP order layout (contains eqGrp + compGrp, swapped on combo change) ─
    QVBoxLayout*  m_dspOrderLayout {nullptr};

    // ── Spectrum display ─────────────────────────────────────────────────────
    // Block FFT analyser: input decimated to ~16 kHz effective rate.
    // One FFT per received batch (O(N log N)) vs. SlidingDFT's O(N) per sample
    // (O(N×M) per batch).  N=1024 gives 15.6 Hz/bin, 512 bins up to 8 kHz.
    static constexpr int SPEC_FFT_LEN = 1024;

    QGroupBox*    specGrp       {nullptr};
    QCheckBox*    specEnable    {nullptr};
    SpectrumWidget* specWidget  {nullptr};

    // Ring buffers hold the most recent SPEC_FFT_LEN decimated samples.
    // One Hanning-windowed FFT is computed per incoming batch.
    Eigen::FFT<float>  m_fft;
    std::vector<float> m_specWindow;    // precomputed Hanning coefficients
    std::vector<float> m_specInRing;    // input  ring buffer
    std::vector<float> m_specOutRing;   // output ring buffer
    int                m_specRingPos  = 0;
    float              m_specSampleRate  = 0.0f;
    int                m_specDecimFactor = 1;
    int                m_specDecimCount  = 0;
    int                m_specFftTrigger  = 0;   // decimated-sample counter; runs FFT every triggerEvery samples
    float              m_audioSampleRate = 0.0f;  // raw audio SR; drives EQ visibility
    bool               m_radioConnected  = false; // set by setConnected()
    int                m_spectrumFps     = 10;   // stored from prefs; no UI control yet

    // ── Meters ──────────────────────────────────────────────────────────────
    meter*        inputMeter    {nullptr};
    meter*        outputMeter   {nullptr};
    meter*        grMeter       {nullptr};  // gain reduction

    // ── Spectrum diagnostics (always-on 1 Hz timer) ───────────────────────────
    // m_specDiagTimer fires regardless of whether audio is arriving, so
    // "no FFT output" is itself a visible diagnostic state.
    QTimer        m_specDiagTimer;           // 1 Hz — logs FFT rate always
    QElapsedTimer m_dftCallTimer;            // times each individual FFT pair
    qint64        m_dftTotalNs      = 0;     // accumulated FFT CPU time
    int           m_dftCallCount    = 0;     // FFT pairs computed this second
    int           m_batchCount      = 0;     // onSpectrumSamples calls past guards
    int           m_lastTriggerEvery = 1;    // cached triggerEvery for diag
    int           m_lastBatchSize   = 0;     // last n (raw samples per batch)
};

#endif // AUDIOPROCESSINGWIDGET_H
