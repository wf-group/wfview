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
    void updateInputLevels(float RMS, float peak);      // 0.0–1.0
    void updateOutputLevels(float RMS, float peak);     // 0.0–1.0

    void updateGainReduction(float linear); // 1.0 = no reduction
    // Receives pre-computed spectrum bins from TxAudioProcessor (audio thread).
    void onSpectrumBins(QVector<double> inBins,
                        QVector<double> outBins,
                        float rawSR);
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
    // FFT is computed by TxAudioProcessor on the converter thread; this widget
    // only receives pre-computed bins and forwards them to SpectrumWidget.
    QGroupBox*      specGrp     {nullptr};
    QCheckBox*      specEnable  {nullptr};
    SpectrumWidget* specWidget  {nullptr};

    float m_audioSampleRate = 0.0f;  // raw audio SR — drives EQ band visibility
    bool  m_radioConnected  = false; // set by setConnected()
    int   m_spectrumFps     = 30;   // stored from prefs; no UI control yet

    // ── Meters ──────────────────────────────────────────────────────────────
    meter*        inputMeter    {nullptr};
    meter*        outputMeter   {nullptr};
    meter*        grMeter       {nullptr};  // gain reduction

    // ── Spectrum diagnostics (1 Hz timer) ────────────────────────────────────
    QTimer        m_specDiagTimer;
    int           m_batchCount = 0;  // onSpectrumBins calls past guards, per second
};

#endif // AUDIOPROCESSINGWIDGET_H
