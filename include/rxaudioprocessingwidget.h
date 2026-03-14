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
#include <QDial>
#include <QSlider>
#include <QSpinBox>
// QStackedWidget removed — NR pages are shown/hidden directly
#include <QTimer>
#include <QVBoxLayout>
#include <QFormLayout>

#include "prefs.h"   // rxAudioProcessingPrefs, RxNrMode
#include "rxaudioprocessor.h"
#include "spectrumwidget.h"

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
    // Emitted when the user clicks the collect/stop button.
    // true = start collecting, false = stop collecting (finalize profile).
    void anrCollectToggled(bool collecting);

public slots:
    // Called by wfmain after the ANR profile has been built (or failed).
    void onAnrProfileReady(bool success);
    // Called from wfmain::receivePTTstatus so the widget can inhibit the
    // spectrum display while the radio is transmitting.
    void setTransmitting(bool transmitting);

private slots:
    void onAnyControlChanged();
    void onBypassToggled(bool bypassed);
    void onNrModeChanged(int id);            // radio-button group id
    void onAlgorithmGroupToggled(bool /*unused*/);
    void onAnrCollectClicked();
    void onAnrCollectTimeout();
    void onSpecEnableToggled(bool enabled);
    void onSpecDiagTimer();

public slots:
    // Connected to RxAudioProcessor::rxSpectrumBins (Qt::AutoConnection).
    void onSpectrumBins(QVector<double> inBins, QVector<double> outBins, float rawSR);
    // Connected to RxAudioProcessor::anrNoiseProfileBins — displays noise profile.
    void onAnrNoiseProfileBins(QVector<double> bins, double sampleRate, int windowSize);

private:
    void buildUi();
    void blockAll(bool block);
    void populateFromPrefs(const rxAudioProcessingPrefs& p);
    void setProcessingControlsEnabled(bool enabled);
    void updateAnrControlState();   // enables/disables ANR sliders based on profile
    void updateSizeConstraints();   // recalc min/max height for current visibility

    // ── Master bypass ─────────────────────────────────────────────────────────
    QCheckBox*    bypassCheck    {nullptr};

    // ── Channel select ────────────────────────────────────────────────────────
    QGroupBox*    channelGrp     {nullptr};
    QComboBox*    channelCombo   {nullptr};  // auto / Ch1 / Ch2 / Ch1+Ch2
    QLabel*       lblChannel     {nullptr};

    // ── Algorithm selector ────────────────────────────────────────────────────
    QGroupBox*     algoGrp       {nullptr};
    QRadioButton*  algoNone      {nullptr};
    QRadioButton*  algoSpeex     {nullptr};
    QRadioButton*  algoAnr       {nullptr};
    QButtonGroup*  algoGroup     {nullptr};
    QWidget*        nonePage     {nullptr};   // shown when NR mode = None

    // ── Speex controls ────────────────────────────────────────────────────────
    QGroupBox*    speexGrp       {nullptr};

    QSlider*      speexSuppress  {nullptr};   // -70..-1 dB
    QLabel*       lblSpeexSuppress{nullptr};

    QComboBox*    speexBandsCombo{nullptr};   // populated from filterbank.h presets

    QComboBox*    speexFrameCombo{nullptr};   // 10 ms / 20 ms

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

    QSlider*      speexSnrDecay     {nullptr}; // 0..95 (×0.01 → 0.0..0.95)
    QLabel*       lblSnrDecay       {nullptr};
    QSlider*      speexNoiseUpdate  {nullptr}; // 1..50 (×0.01 → 0.01..0.50)
    QLabel*       lblNoiseUpdate    {nullptr};
    QSlider*      speexPriorBase    {nullptr}; // 5..50 (×0.01 → 0.05..0.50)
    QLabel*       lblPriorBase      {nullptr};

    // ── ANR controls ──────────────────────────────────────────────────────────
    QGroupBox*    anrGrp             {nullptr};
    QSlider*      anrNoiseRedSlider  {nullptr};   // 0..48 dB
    QLabel*       lblAnrNoiseRed     {nullptr};
    QSlider*      anrSensSlider      {nullptr};   // 0..100 (×0.1 → 0.0..10.0)
    QLabel*       lblAnrSens         {nullptr};
    QSlider*      anrSmoothSlider    {nullptr};   // 0..6 bands
    QLabel*       lblAnrSmooth       {nullptr};
    QPushButton*  anrCollectBtn      {nullptr};   // "Collect Noise Sample" / "Stop Collecting"
    QLabel*       lblAnrStatus       {nullptr};   // status / instruction line
    QTimer*       anrCollectTimer    {nullptr};   // 1-second tick for countdown
    SpectrumWidget* anrProfileSpec   {nullptr};   // static noise profile display
    bool          m_anrCollecting    = false;
    bool          m_anrHasProfile    = false;
    int           m_anrCountdown     = 0;         // seconds remaining during collection

    // ── RX Equalizer ──────────────────────────────────────────────────────────
    static constexpr int RX_EQ_BANDS = 4;
    QGroupBox*    eqGrp          {nullptr};
    QCheckBox*    eqEnableCheck  {nullptr};
    QPushButton*  eqClearBtn     {nullptr};
    QWidget*      eqBandsWidget  {nullptr};  // container for bands+clear (hidden when EQ off)
    QSlider*      eqGainSlider[RX_EQ_BANDS]  = {};  // vertical, -60..+60 (×0.1 dB)
    QLabel*       eqGainLabel[RX_EQ_BANDS]   = {};  // gain readout on top
    QDial*        eqFreqDial[RX_EQ_BANDS]    = {};  // frequency knob
    QLabel*       eqFreqLabel[RX_EQ_BANDS]   = {};  // frequency readout below knob
    QLabel*       eqBandTitle[RX_EQ_BANDS]   = {};  // band name label

    // ── Output gain ───────────────────────────────────────────────────────────
    QGroupBox*    gainGrp        {nullptr};
    QSlider*      outputGain     {nullptr};   // -60..200 (×0.1 dB → -6..+20 dB)
    QLabel*       lblOutputGain  {nullptr};

    // ── Spectrum display ──────────────────────────────────────────────────────
    QGroupBox*    specGrp                {nullptr};
    QCheckBox*    specEnable             {nullptr};
    QCheckBox*    specInhibitDuringTx    {nullptr};
    SpectrumWidget* specWidget           {nullptr};

    // ── Containers ────────────────────────────────────────────────────────────
    // Top-level widget holding all controls (for easy enable/disable on bypass)
    QWidget*      controlsContainer {nullptr};

    int  m_audioChannels  = 1;
    bool m_radioConnected = false;
    bool m_isTransmitting = false;
    int  m_spectrumFps    = 10;
    int  m_batchCount     = 0;
    QTimer m_specDiagTimer;
};

#endif // RXAUDIOPROCESSINGWIDGET_H
