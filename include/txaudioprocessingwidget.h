#ifndef TXAUDIOPROCESSINGWIDGET_H
#define TXAUDIOPROCESSINGWIDGET_H

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
#include "collapsiblesection.h"

// ─────────────────────────────────────────────────────────────────────────────
// TxAudioProcessingWidget — modal-less dialog for TX audio processing.
//
// Emits prefsChanged(audioProcessingPrefs) whenever any control changes.
// Call setPrefs() to populate the controls from saved preferences.
// Connect TxAudioProcessor meter signals to the updateXxxLevel() slots.
// ─────────────────────────────────────────────────────────────────────────────

class TxAudioProcessingWidget : public QDialog
{
    Q_OBJECT

public:
    explicit TxAudioProcessingWidget(QWidget* parent = nullptr);

    void setPrefs(const txAudioProcessingPrefs& p);
    txAudioProcessingPrefs getPrefs() const;

signals:
    void prefsChanged(txAudioProcessingPrefs p);

public slots:
    void updateInputLevel(float peak);      // 0.0–1.0
    void updateOutputLevel(float peak);     // 0.0–1.0
    void updateInputLevels(float RMS, float peak);
    void updateOutputLevels(float RMS, float peak);
    void updateGainReduction(float linear); // 1.0 = no reduction
    void onSpectrumBins(QVector<double> inBins,
                        QVector<double> outBins,
                        float rawSR);
    void setConnected(bool connected);
    // Called from wfmain::receivePTTstatus so the widget can inhibit the
    // spectrum display while the radio is receiving (not transmitting).
    void setTransmitting(bool transmitting);

private slots:
    void onAnyControlChanged();
    void onBypassToggled(bool bypassed);
    void onClearEq();
    void onSpecEnableToggled(bool enabled);
    void onSpecDiagTimer();
    void onPluginOrderChanged(int index);

private:
    void buildUi();
    void blockAll(bool block);
    void populateFromPrefs(const txAudioProcessingPrefs& p);
    void setProcessingControlsEnabled(bool enabled);
    void updateEqBandVisibility(float sampleRate);
    void reorderDspGroups(bool eqFirst);
    void updateSizeConstraints();  // recalc min/max height for current visibility

    // ── Master bypass ────────────────────────────────────────────────────────
    QCheckBox*    bypassCheck   {nullptr};

    // ── Noise gate ───────────────────────────────────────────────────────────
    CollapsibleSection* gateGrp   {nullptr};  // collapsible
    QCheckBox*    gateEnable      {nullptr};
    QSlider*      gateThreshold   {nullptr};
    QSlider*      gateAttack      {nullptr};
    QSlider*      gateHold        {nullptr};
    QSlider*      gateDecay       {nullptr};
    QSlider*      gateRange       {nullptr};
    QSlider*      gateLfCutoff    {nullptr};
    QSlider*      gateHfCutoff    {nullptr};
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
    QLabel*       eqValues  [EQ_BANDS] {};
    QWidget*      eqColumns [EQ_BANDS] {};

    // ── Compressor ──────────────────────────────────────────────────────────
    QCheckBox*    compEnable    {nullptr};
    QSlider*      compPeakLimit {nullptr};
    QSlider*      compRelease   {nullptr};
    QSlider*      compFastRatio {nullptr};
    QSlider*      compSlowRatio {nullptr};
    QLabel*       lblPeak       {nullptr};
    QLabel*       lblRelease    {nullptr};
    QLabel*       lblFast       {nullptr};
    QLabel*       lblSlow       {nullptr};

    // ── Gain ────────────────────────────────────────────────────────────────
    QSlider*      inputGain     {nullptr};
    QSlider*      outputGain    {nullptr};
    QLabel*       lblInputGain  {nullptr};
    QLabel*       lblOutputGain {nullptr};

    // ── Plugin order ────────────────────────────────────────────────────────
    QComboBox*    orderCombo    {nullptr};

    // ── Sidetone ────────────────────────────────────────────────────────────
    QCheckBox*    sidetoneEnable{nullptr};
    QCheckBox*    muteRxCheck   {nullptr};
    QSlider*      sidetoneLevel {nullptr};
    QLabel*       lblSidetone   {nullptr};

    // ── Processing group boxes ───────────────────────────────────────────────
    QGroupBox*         gainGrp       {nullptr};
    CollapsibleSection* eqGrp        {nullptr};  // collapsible
    CollapsibleSection* compGrp      {nullptr};  // collapsible
    QGroupBox*          sidetoneGrp  {nullptr};
    QWidget*            orderRow     {nullptr};

    // ── DSP order layout ─────────────────────────────────────────────────────
    QVBoxLayout*  m_dspOrderLayout {nullptr};

    // ── Spectrum display ─────────────────────────────────────────────────────
    QGroupBox*      specGrp              {nullptr};
    QCheckBox*      specEnable           {nullptr};
    QCheckBox*      specInhibitDuringRx  {nullptr};
    SpectrumWidget* specWidget           {nullptr};

    float m_audioSampleRate = 0.0f;
    bool  m_radioConnected  = false;
    bool  m_isTransmitting  = false;
    int   m_spectrumFps     = 30;

    // ── Meters ──────────────────────────────────────────────────────────────
    meter*        inputMeter    {nullptr};
    meter*        outputMeter   {nullptr};
    meter*        grMeter       {nullptr};

    // ── Spectrum diagnostics ─────────────────────────────────────────────────
    QTimer        m_specDiagTimer;
    int           m_batchCount = 0;
};

#endif // TXAUDIOPROCESSINGWIDGET_H
