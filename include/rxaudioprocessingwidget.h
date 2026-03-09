#ifndef RXAUDIOPROCESSINGWIDGET_H
#define RXAUDIOPROCESSINGWIDGET_H

// RxAudioProcessingWidget — modal-less dialog for RX audio noise reduction.
//
// Design notes for future QML migration:
//   - All business logic lives in RxAudioProcessor; the widget is pure view/control.
//   - Parameters are exchanged via a plain-struct snapshot (rxAudioProcessingPrefs),
//     mirroring the pattern used by TxAudioProcessingWidget.
//   - Each logical section maps to one QGroupBox, making QML Column/GroupBox
//     conversion straightforward.

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QStackedWidget>

#include "prefs.h"   // rxAudioProcessingPrefs, RxNrMode
#include "rxaudioprocessor.h"

class RxAudioProcessingWidget : public QDialog
{
    Q_OBJECT

public:
    explicit RxAudioProcessingWidget(QWidget* parent = nullptr);

    void setPrefs(const rxAudioProcessingPrefs& p);
    rxAudioProcessingPrefs getPrefs() const;

    // Call when the radio connects/disconnects (enables or disables the whole dialog)
    void setConnected(bool connected);
    // Call when the audio feed channel count is known (1 or 2)
    void setAudioChannels(int ch);

signals:
    void prefsChanged(rxAudioProcessingPrefs p);

public slots:
    // Latency estimate from RxAudioProcessor (ms); 0 = bypass / disabled
    void updateLatency(float latencyMs);

    void updateInputLevel(float peak);
    void updateOutputLevel(float peak);

private slots:
    void onAnyControlChanged();
    void onBypassToggled(bool bypassed);
    void onNrModeChanged(int id);            // radio-button group id
    void onAlgorithmGroupToggled(bool /*unused*/);

private:
    void buildUi();
    void blockAll(bool block);
    void populateFromPrefs(const rxAudioProcessingPrefs& p);
    void setProcessingControlsEnabled(bool enabled);

    // ── Master bypass ─────────────────────────────────────────────────────────
    QCheckBox*    bypassCheck    {nullptr};

    // ── Channel select ────────────────────────────────────────────────────────
    QGroupBox*    channelGrp     {nullptr};
    QComboBox*    channelCombo   {nullptr};  // auto / Ch1 / Ch2 / Ch1+Ch2
    QLabel*       lblChannel     {nullptr};

    // ── Algorithm selector ────────────────────────────────────────────────────
    QGroupBox*     algoGrp       {nullptr};
    QRadioButton*  algoSpeex     {nullptr};
    QRadioButton*  algoSpac      {nullptr};
    QButtonGroup*  algoGroup     {nullptr};

    // ── Speex controls ────────────────────────────────────────────────────────
    QGroupBox*    speexGrp       {nullptr};

    QSlider*      speexSuppress  {nullptr};   // -70..-1 dB
    QLabel*       lblSpeexSuppress{nullptr};

    QComboBox*    speexBandsCombo{nullptr};   // populated from filterbank.h presets

    QComboBox*    speexFrameCombo{nullptr};   // 10 ms / 20 ms

    QCheckBox*    speexDerevCheck{nullptr};
    QSlider*      speexDRLevel   {nullptr};   // 0..100 (×0.01)
    QLabel*       lblDRLevel     {nullptr};
    QSlider*      speexDRDecay   {nullptr};   // 0..100 (×0.01)
    QLabel*       lblDRDecay     {nullptr};

    QCheckBox*    speexAgcCheck  {nullptr};
    QSlider*      speexAgcLevel  {nullptr};   // 1000..32000
    QLabel*       lblAgcLevel    {nullptr};
    QSlider*      speexAgcMaxGain{nullptr};   // 0..60 dB
    QLabel*       lblAgcMaxGain  {nullptr};

    QCheckBox*    speexVadCheck  {nullptr};
    QSlider*      speexVadProbStart {nullptr}; // 0..100 %
    QLabel*       lblVadProbStart   {nullptr};
    QSlider*      speexVadProbCont  {nullptr}; // 0..100 %
    QLabel*       lblVadProbCont    {nullptr};

    // ── SPAC controls ─────────────────────────────────────────────────────────
    QGroupBox*    spacGrp        {nullptr};

    QSlider*      spacFrameMs    {nullptr};   // 10..50 ms (×1)
    QLabel*       lblSpacFrameMs {nullptr};

    QSlider*      spacVoicingThr {nullptr};   // 0..100 (×0.01)
    QLabel*       lblSpacVoicing {nullptr};

    QSlider*      spacVoicingFull{nullptr};   // 0..100 (×0.01), must exceed voicingThr
    QLabel*       lblSpacVoicingFull{nullptr};

    QSlider*      spacAttenDb    {nullptr};   // 0..120 dB
    QLabel*       lblSpacAtten   {nullptr};

    // ── Output gain ───────────────────────────────────────────────────────────
    QGroupBox*    gainGrp        {nullptr};
    QSlider*      outputGain     {nullptr};   // -60..200 (×0.1 dB → -6..+20 dB)
    QLabel*       lblOutputGain  {nullptr};

    // ── Latency display ───────────────────────────────────────────────────────
    QLabel*       lblLatency     {nullptr};

    // ── Level meters (simple labels for now; meter widgets available if desired)
    QLabel*       lblInputPeak   {nullptr};
    QLabel*       lblOutputPeak  {nullptr};

    // ── Containers ────────────────────────────────────────────────────────────
    // Top-level widget holding all controls (for easy enable/disable on bypass)
    QWidget*      controlsContainer {nullptr};

    int  m_audioChannels = 1;
    bool m_radioConnected = false;
};

#endif // RXAUDIOPROCESSINGWIDGET_H
