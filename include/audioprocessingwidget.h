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

#include "prefs.h"
#include "meter.h"
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
    void updateGainReduction(float linear); // 1.0 = no reduction

private slots:
    void onAnyControlChanged();
    void onBypassToggled(bool bypassed);
    void onClearEq();

private:
    void buildUi();
    void blockAll(bool block);
    void populateFromPrefs(const audioProcessingPrefs& p);
    void setProcessingControlsEnabled(bool enabled);

    // ── Master bypass ────────────────────────────────────────────────────────
    QCheckBox*    bypassCheck   {nullptr};

    // ── EQ ─────────────────────────────────────────────────────────────────
    static constexpr int EQ_BANDS = TxAudioProcessor::EQ_BANDS;
    QCheckBox*    eqEnable      {nullptr};
    QPushButton*  clearEqBtn    {nullptr};
    QSlider*      eqSliders [EQ_BANDS] {};
    QLabel*       eqValues  [EQ_BANDS] {};  // shows dB value text

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

    // ── Meters ──────────────────────────────────────────────────────────────
    meter*        inputMeter    {nullptr};
    meter*        outputMeter   {nullptr};
    meter*        grMeter       {nullptr};  // gain reduction
};

#endif // AUDIOPROCESSINGWIDGET_H
