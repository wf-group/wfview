// TxAudioProcessingWidget — TX audio processing settings dialog

#include "txaudioprocessingwidget.h"
#include "collapsiblesection.h"
#include "logcategories.h"
#include <cmath>

// MBEQ band centre frequencies (Hz) — must match MbeqProcessor::bandFreqs
static const float kBandFreqs[TxAudioProcessor::EQ_BANDS] = {
    50, 100, 200, 300, 400, 600, 800, 1000,
    1200, 1600, 2000, 2500, 3200, 5000, 8000
};

static QString freqLabel(float hz)
{
    if (hz >= 1000.0f) return QString::number(hz / 1000.0f, 'f', 1) + "K";
    return QString::number(int(hz));
}

// ─────────────────────────────────────────────────────────────────────────────

TxAudioProcessingWidget::TxAudioProcessingWidget(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("TX Audio Processing"));
    setWindowFlags(windowFlags() | Qt::Window);
    buildUi();
    connect(&m_specDiagTimer, &QTimer::timeout,
            this, &TxAudioProcessingWidget::onSpecDiagTimer);
    m_specDiagTimer.setInterval(1000);
    m_specDiagTimer.start();
}

// ─── setPrefs / getPrefs ─────────────────────────────────────────────────────

void TxAudioProcessingWidget::setPrefs(const txAudioProcessingPrefs& p)
{
    blockAll(true);
    populateFromPrefs(p);
    blockAll(false);
}

txAudioProcessingPrefs TxAudioProcessingWidget::getPrefs() const
{
    txAudioProcessingPrefs p;

    p.bypass      = bypassCheck->isChecked();
    p.eqEnabled   = eqEnable->isChecked();
    p.compEnabled = compEnable->isChecked();
    p.eqFirst     = (orderCombo->currentIndex() == 0); // 0=EQ→Comp, 1=Comp→EQ

    for (int i = 0; i < EQ_BANDS; ++i)
        p.eqBands[i] = eqSliders[i]->value() * 0.1f;  // slider is ×10

    p.compPeakLimit = compPeakLimit->value() * 0.1f;
    p.compRelease   = compRelease->value()   * 0.01f;
    p.compFastRatio = (100-compFastRatio->value()) * 0.01f;
    p.compSlowRatio = (100-compSlowRatio->value()) * 0.01f;

    p.inputGainDB   = inputGain->value()  * 0.1f;
    p.outputGainDB  = outputGain->value() * 0.1f;

    p.sidetoneEnabled = sidetoneEnable->isChecked();
    p.sidetoneLevel   = sidetoneLevel->value() * 0.01f;
    p.muteRx = muteRxCheck->isChecked();

    p.spectrumEnabled     = specEnable->isChecked();
    p.spectrumFPS         = m_spectrumFps;
    p.specInhibitDuringRx = specInhibitDuringRx->isChecked();

    p.gateEnabled   = gateEnable->isChecked();
    p.gateThreshold = static_cast<float>(gateThreshold->value());
    p.gateAttack    = static_cast<float>(gateAttack->value());
    p.gateHold      = static_cast<float>(gateHold->value());
    p.gateDecay     = static_cast<float>(gateDecay->value());
    p.gateRange     = static_cast<float>(gateRange->value());
    p.gateLfCutoff  = static_cast<float>(gateLfCutoff->value());
    p.gateHfCutoff  = static_cast<float>(gateHfCutoff->value());

    return p;
}

// ─── Level meter updates ──────────────────────────────────────────────────────

void TxAudioProcessingWidget::updateInputLevel(float peak)
{
    inputMeter->setLevel(static_cast<double>(peak) * 255.0);
}

void TxAudioProcessingWidget::updateOutputLevel(float peak)
{
    outputMeter->setLevel(static_cast<double>(peak) * 255.0);
}

void TxAudioProcessingWidget::updateInputLevels(float rms, float peak)
{
    inputMeter->setLevels(static_cast<double>(rms) * 255.0, static_cast<double>(peak) * 255.0);
}

void TxAudioProcessingWidget::updateOutputLevels(float rms, float peak)
{
    outputMeter->setLevels(static_cast<double>(rms) * 255.0, static_cast<double>(peak) * 255.0);
}

void TxAudioProcessingWidget::updateGainReduction(float linear)
{
    // Gain reduction meter: 0 dB = no reduction (1.0), shown on meterComp scale.
    // Convert linear gain → raw 0-255 value for the meter.
    // meterComp typically shows 0–20 dB reduction; map linearly.
    const float dBgain = (linear > 0.0001f) ? 20.0f * std::log10(linear) : -40.0f;
    // dBgain ≤ 0 for compression.  Map [0, -20] dB → [0, 255]
    const double val = std::min(255.0, std::max(0.0, -dBgain * 255.0 / 20.0));
    grMeter->setLevel(val);
}

// ─── Any control changed ──────────────────────────────────────────────────────

void TxAudioProcessingWidget::onAnyControlChanged()
{
    // Update value labels
    for (int i = 0; i < EQ_BANDS; ++i) {
        float db = eqSliders[i]->value() * 0.1f;
        eqValues[i]->setText(QString::number(db, 'f', 1));
    }
    lblPeak->setText(QString::number(compPeakLimit->value() * 0.1f, 'f', 1) + " dB");
    lblRelease->setText(QString::number(compRelease->value() * 0.01f, 'f', 2) + " s");
    lblFast->setText(QString("%1:1").arg(1 + compFastRatio->value() * 99 / 100, 3));
    lblSlow->setText(QString("%1:1").arg(1 + compSlowRatio->value() * 99 / 100, 3));
    lblInputGain->setText(QString::number(inputGain->value() * 0.1f, 'f', 1) + " dB");
    lblOutputGain->setText(QString::number(outputGain->value() * 0.1f, 'f', 1) + " dB");
    lblSidetone->setText(QString::number(sidetoneLevel->value()) + "%");
    lblGateThreshold->setText(QString::number(gateThreshold->value()) + " dB");
    lblGateAttack->setText(QString::number(gateAttack->value()) + " ms");
    lblGateHold->setText(QString::number(gateHold->value()) + " ms");
    lblGateDecay->setText(QString::number(gateDecay->value()) + " ms");
    lblGateRange->setText(QString::number(gateRange->value()) + " dB");
    lblGateLfCutoff->setText(QString::number(gateLfCutoff->value()) + " Hz");
    lblGateHfCutoff->setText(QString::number(gateHfCutoff->value()) + " Hz");

    emit prefsChanged(getPrefs());
}

// ─── Bypass / Clear EQ slots ──────────────────────────────────────────────────

void TxAudioProcessingWidget::onBypassToggled(bool bypassed)
{
    setProcessingControlsEnabled(!bypassed);
    emit prefsChanged(getPrefs());
}

void TxAudioProcessingWidget::onClearEq()
{
    blockAll(true);
    for (int i = 0; i < EQ_BANDS; ++i) {
        eqSliders[i]->setValue(0);
        eqValues[i]->setText("0.0");
    }
    blockAll(false);
    emit prefsChanged(getPrefs());
}

void TxAudioProcessingWidget::reorderDspGroups(bool eqFirst)
{
    // Remove both widgets from the sub-layout, then re-add in the chosen order.
    m_dspOrderLayout->removeWidget(eqGrp);
    m_dspOrderLayout->removeWidget(compGrp);
    if (eqFirst) {
        m_dspOrderLayout->addWidget(eqGrp);
        m_dspOrderLayout->addWidget(compGrp);
    } else {
        m_dspOrderLayout->addWidget(compGrp);
        m_dspOrderLayout->addWidget(eqGrp);
    }
}

void TxAudioProcessingWidget::onPluginOrderChanged(int index)
{
    // index 0 = "EQ → Compressor" (EQ on top), 1 = "Compressor → EQ"
    reorderDspGroups(index == 0);
    emit prefsChanged(getPrefs());
}

void TxAudioProcessingWidget::onSpecEnableToggled(bool enabled)
{
    specWidget->setVisible(enabled);
    // setVisible() on a child posts an async LayoutRequest but does not
    // synchronously invalidate ancestor layout caches.  Explicitly dirty
    // specGrp's own layout and then propagate upward so updateSizeConstraints()
    // sees the correct (smaller) sizeHint for specGrp.
    specGrp->layout()->invalidate();
    specGrp->updateGeometry();
    if (!enabled) {
        specWidget->spectrumPrimary.clear();
        specWidget->spectrumSecondary.clear();
    }
    updateSizeConstraints();
    emit prefsChanged(getPrefs());
}

void TxAudioProcessingWidget::setConnected(bool connected)
{
    m_radioConnected = connected;
    if (!connected) {
        specWidget->spectrumPrimary.clear();
        specWidget->spectrumSecondary.clear();
        m_audioSampleRate = 0.0f;
    }
}

// ─── onSpectrumBins ───────────────────────────────────────────────────────────
// Receives pre-computed dBFS spectrum bins from TxAudioProcessor (runs FFT on
// the converter thread at TimeCriticalPriority).  This slot only needs to
// update EQ band visibility on SR change and forward bins to SpectrumWidget.

void TxAudioProcessingWidget::setTransmitting(bool transmitting)
{
    m_isTransmitting = transmitting;
    // When entering the inhibited state (radio starts receiving), clear
    // stale spectrum data so the display goes blank rather than freezing.
    if (!m_isTransmitting && specInhibitDuringRx && specInhibitDuringRx->isChecked()) {
        if (specWidget) {
            specWidget->spectrumPrimary.clear();
            specWidget->spectrumSecondary.clear();
        }
    }
}

void TxAudioProcessingWidget::onSpectrumBins(QVector<double> inBins,
                                            QVector<double> outBins,
                                            float rawSR)
{
    if (!m_radioConnected) return;
    if (!specEnable || !specEnable->isChecked()) return;
    // Inhibit during receive: drop bins while not transmitting.
    if (!m_isTransmitting && specInhibitDuringRx && specInhibitDuringRx->isChecked()) return;
    ++m_batchCount;

    if (rawSR != m_audioSampleRate) {
        m_audioSampleRate = rawSR;
        updateEqBandVisibility(rawSR);
        // Effective sample rate after decimation to ~16 kHz.
        const int K = qMax(1, static_cast<int>(std::round(rawSR / 16000.0f)));
        specWidget->sampleRate = static_cast<double>(rawSR) / K;
        specWidget->fftLength  = TxAudioProcessor::SPEC_FFT_LEN;
    }

    specWidget->spectrumPrimary.assign(inBins.cbegin(),   inBins.cend());
    specWidget->spectrumSecondary.assign(outBins.cbegin(), outBins.cend());
}

void TxAudioProcessingWidget::updateEqBandVisibility(float sampleRate)
{
    if (sampleRate <= 0.0f) return;
    const float nyquist = sampleRate * 0.5f;
    for (int i = 0; i < EQ_BANDS; ++i)
        eqColumns[i]->setVisible(kBandFreqs[i] <= nyquist);
}

// ─── onSpecDiagTimer ──────────────────────────────────────────────────────────
// Fires every second.  FFT now runs on the converter thread (TxAudioProcessor);
// we just log how many bin-sets per second reach this widget.

void TxAudioProcessingWidget::onSpecDiagTimer()
{
    if (!specEnable || !specEnable->isChecked()) {
        m_batchCount = 0;
        return;
    }

    const bool audioArriving = (m_batchCount > 0);
    const bool fpsMiss       = audioArriving && (m_batchCount < m_spectrumFps * 8 / 10);
    Q_UNUSED(fpsMiss)

    QString noDataReason;
    if (!audioArriving) {
        if      (!m_radioConnected)          noDataReason = "radio not connected";
        else                                 noDataReason = "no TX audio (not transmitting?)";
    }

    /*
    qCDebug(logAudio) << "[SpectrumBins]"
                      << m_batchCount << "bin-sets/s"
                      << "(target" << m_spectrumFps << "fps)"
                      << (fpsMiss             ? "*** FPS NOT MET ***" : "")
                      << (!noDataReason.isEmpty() ? "— " + noDataReason : "");
    */
    m_batchCount = 0;
}

void TxAudioProcessingWidget::setProcessingControlsEnabled(bool enabled)
{
    orderRow->setEnabled(enabled);
    gateGrp->setEnabled(enabled);
    gainGrp->setEnabled(enabled);
    eqGrp->setEnabled(enabled);
    compGrp->setEnabled(enabled);
    // Self-Monitor group is always available regardless of bypass state.
}

// ─── buildUi ─────────────────────────────────────────────────────────────────

void TxAudioProcessingWidget::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);

    // ── Master bypass ────────────────────────────────────────────────────────
    {
        bypassCheck = new QCheckBox(tr("Master Bypass (disable all DSP)"));
        QFont f = bypassCheck->font();
        f.setBold(true);
        bypassCheck->setFont(f);
        mainLayout->addWidget(bypassCheck);
        connect(bypassCheck, &QCheckBox::toggled,
                this, &TxAudioProcessingWidget::onBypassToggled);
    }

    // ── Noise gate (runs first — before input gain) ──────────────────────────
    {
        gateEnable = new QCheckBox(tr("Enable Noise Gate"));
        gateEnable->setToolTip(tr("Gate attenuates audio below threshold before input gain. "
                                  "Use to eliminate background noise between speech bursts."));

        auto* gateBody = new QWidget;
        auto* form = new QFormLayout(gateBody);

        auto makeGateSlider = [](int lo, int hi, int val) {
            auto* s = new QSlider(Qt::Horizontal);
            s->setRange(lo, hi);
            s->setValue(val);
            return s;
        };

        // Threshold: -70..0 dB
        gateThreshold  = makeGateSlider(-70, 0, -40);
        lblGateThreshold = new QLabel("-40 dB");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateThreshold);
            row->addWidget(lblGateThreshold);
            form->addRow(tr("Threshold:"), row);
        }
        gateThreshold->setToolTip(tr("Signal level at which the gate opens. "
                                     "Lower = gate opens for quieter signals."));

        // Attack: 1..500 ms
        gateAttack  = makeGateSlider(1, 500, 10);
        lblGateAttack = new QLabel("10 ms");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateAttack);
            row->addWidget(lblGateAttack);
            form->addRow(tr("Attack:"), row);
        }
        gateAttack->setToolTip(tr("How quickly the gate fully opens after threshold is crossed (ms)."));

        // Hold: 2..1000 ms
        gateHold  = makeGateSlider(2, 1000, 100);
        lblGateHold = new QLabel("100 ms");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateHold);
            row->addWidget(lblGateHold);
            form->addRow(tr("Hold:"), row);
        }
        gateHold->setToolTip(tr("Minimum time the gate stays open after signal drops below threshold (ms). "
                                "Prevents rapid chattering."));

        // Decay: 2..2000 ms
        gateDecay  = makeGateSlider(2, 1000, 200);
        lblGateDecay = new QLabel("200 ms");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateDecay);
            row->addWidget(lblGateDecay);
            form->addRow(tr("Decay:"), row);
        }
        gateDecay->setToolTip(tr("How quickly the gate closes after hold time expires (ms)."));

        // Range: -90..0 dB
        gateRange  = makeGateSlider(-90, 0, -60);
        lblGateRange = new QLabel("-90 dB");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateRange);
            row->addWidget(lblGateRange);
            form->addRow(tr("Range:"), row);
        }
        gateRange->setToolTip(tr("Attenuation applied when the gate is closed. "
                                 "-90 dB = virtually silent; 0 dB = no gating effect."));

        // LF key filter cutoff: 20..4000 Hz
        gateLfCutoff  = makeGateSlider(20, 500, 380);
        lblGateLfCutoff = new QLabel("380 Hz");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateLfCutoff);
            row->addWidget(lblGateLfCutoff);
            form->addRow(tr("Key LF cut:"), row);
        }
        gateLfCutoff->setToolTip(tr("High-pass frequency of the gate key detector (Hz). "
                                    "Raise to prevent low-frequency rumble from triggering the gate."));

        // HF key filter cutoff: 200..20000 Hz
        gateHfCutoff  = makeGateSlider(200, 4000, 2700);
        lblGateHfCutoff = new QLabel("8000 Hz");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(gateHfCutoff);
            row->addWidget(lblGateHfCutoff);
            form->addRow(tr("Key HF cut:"), row);
        }
        gateHfCutoff->setToolTip(tr("Low-pass frequency of the gate key detector (Hz). "
                                    "Lower to prevent high-frequency noise from triggering the gate."));

        gateGrp = new CollapsibleSection(tr("Noise Gate  (pre-gain)"), gateEnable);
        gateGrp->setBodyWidget(gateBody);
        mainLayout->addWidget(gateGrp);
    }

    // ── Plugin order ────────────────────────────────────────────────────────
    {
        orderRow = new QWidget;
        auto* row = new QHBoxLayout(orderRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(new QLabel(tr("Plugin order:")));
        orderCombo = new QComboBox;
        orderCombo->addItem(tr("EQ → Compressor"));
        orderCombo->addItem(tr("Compressor → EQ"));
        row->addWidget(orderCombo);
        row->addStretch();
        mainLayout->addWidget(orderRow);
    }

    // ── Gain controls ────────────────────────────────────────────────────────
    {
        gainGrp = new QGroupBox(tr("Gain"));
        auto* grp = gainGrp;
        auto* form = new QFormLayout(grp);

        auto makeGainSlider = [&](QSlider*& sl, QLabel*& lbl) {
            sl = new QSlider(Qt::Horizontal);
            sl->setRange(-200, 200);  // ×0.1 dB → -20..+20 dB
            sl->setValue(0);
            sl->setTickPosition(QSlider::TicksBelow);
            sl->setTickInterval(50);
            lbl = new QLabel("0.0 dB");
            lbl->setMinimumWidth(60);
            auto* row = new QHBoxLayout;
            row->addWidget(sl);
            row->addWidget(lbl);
            return row;
        };

        form->addRow(tr("Input gain:"),  makeGainSlider(inputGain,  lblInputGain));
        form->addRow(tr("Output gain:"), makeGainSlider(outputGain, lblOutputGain));
        inputGain->setToolTip(tr("Tip: Increase to around +10dB to drive the compressor into harder compression."));
        outputGain->setToolTip(tr("Tip: Use the output gain to makeup for lost amplitude from the compression stage."));
        mainLayout->addWidget(grp);
    }

    // ── Compressor ──────────────────────────────────────────────────────────
    {
        compEnable = new QCheckBox(tr("Enable Compressor"));

        auto* compBody = new QWidget;
        auto* form = new QFormLayout(compBody);

        auto makeSlider = [](int lo, int hi, int val) {
            auto* s = new QSlider(Qt::Horizontal);
            s->setRange(lo, hi);
            s->setValue(val);
            return s;
        };

        // Peak limit: -30..0 dB, stored ×10, so range -300..0
        compPeakLimit = makeSlider(-300, 0, -100);
        lblPeak       = new QLabel("-10.0 dB");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(compPeakLimit);
            row->addWidget(lblPeak);
            form->addRow(tr("Peak limit:"), row);
        }

        // Release: 0.01..1.0 s → stored ×100, range 1..100
        compRelease = makeSlider(1, 100, 10);
        lblRelease  = new QLabel("0.10 s");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(compRelease);
            row->addWidget(lblRelease);
            form->addRow(tr("Release time:"), row);
        }

        // Fast ratio: DSP 0.0=1:1 (no compression) … 1.0=100:1 (max), stored ×100
        compFastRatio = makeSlider(0, 100, 50);
        lblFast       = new QLabel(" 50:1");
        lblFast->setMinimumWidth(55);
        {
            auto* row = new QHBoxLayout;
            row->addWidget(compFastRatio);
            row->addWidget(lblFast);
            form->addRow(tr("Fast ratio:"), row);
        }

        // Slow ratio: DSP 0.0=1:1 (no compression) … 1.0=100:1 (max), stored ×100
        compSlowRatio = makeSlider(0, 100, 30);
        lblSlow       = new QLabel(" 30:1");
        lblSlow->setMinimumWidth(55);
        {
            auto* row = new QHBoxLayout;
            row->addWidget(compSlowRatio);
            row->addWidget(lblSlow);
            form->addRow(tr("Slow ratio:"), row);
        }

        compGrp = new CollapsibleSection(tr("Dyson Compressor"), compEnable);
        compGrp->setBodyWidget(compBody);
    }

    // ── Multiband EQ ─────────────────────────────────────────────────────────
    {
        // Header: Enable checkbox + Clear button (always visible when collapsed)
        auto* eqHeader = new QWidget;
        {
            auto* hl = new QHBoxLayout(eqHeader);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(4);
            eqEnable   = new QCheckBox(tr("Enable EQ"));
            clearEqBtn = new QPushButton(tr("Clear"));
            clearEqBtn->setToolTip(tr("Reset all EQ bands to 0 dB"));
            hl->addWidget(eqEnable);
            hl->addWidget(clearEqBtn);
        }

        auto* eqBody = new QWidget;
        auto* vbox   = new QVBoxLayout(eqBody);
        vbox->setContentsMargins(0, 0, 0, 0);

        auto* sliderRow = new QHBoxLayout;
        for (int i = 0; i < EQ_BANDS; ++i) {
            // Wrap each band in a QWidget so it can be hidden when its
            // centre frequency exceeds the audio Nyquist.
            eqColumns[i] = new QWidget;
            auto* col = new QVBoxLayout(eqColumns[i]);
            col->setContentsMargins(1, 0, 1, 0);

            eqSliders[i] = new QSlider(Qt::Vertical);
            eqSliders[i]->setRange(-100, 100);  // ×0.1 dB → -10..+10 dB
            eqSliders[i]->setValue(0);
            eqSliders[i]->setTickPosition(QSlider::TicksRight);
            eqSliders[i]->setTickInterval(10); // 1.0 dB ticks
            eqSliders[i]->setMinimumHeight(120);

            eqValues[i] = new QLabel("0.0");
            eqValues[i]->setAlignment(Qt::AlignHCenter);

            auto* freqLbl = new QLabel(freqLabel(kBandFreqs[i]));
            freqLbl->setAlignment(Qt::AlignHCenter);

            col->addWidget(eqValues[i]);
            col->addWidget(eqSliders[i]);
            col->addWidget(freqLbl);

            sliderRow->addWidget(eqColumns[i]);
            connect(eqSliders[i], &QSlider::valueChanged,
                    this, &TxAudioProcessingWidget::onAnyControlChanged);
        }
        vbox->addLayout(sliderRow);

        eqGrp = new CollapsibleSection(tr("Multiband EQ"), eqHeader);
        eqGrp->setBodyWidget(eqBody);
    }

    // ── DSP order container: holds eqGrp + compGrp in combo-selected order ──
    {
        auto* dspContainer = new QWidget;
        m_dspOrderLayout = new QVBoxLayout(dspContainer);
        m_dspOrderLayout->setContentsMargins(0, 0, 0, 0);
        // Default index 0 = "EQ → Compressor": EQ on top.
        m_dspOrderLayout->addWidget(eqGrp);
        m_dspOrderLayout->addWidget(compGrp);
        mainLayout->addWidget(dspContainer);
    }

    // ── TX Spectrum ──────────────────────────────────────────────────────────
    {
        specGrp = new QGroupBox(tr("TX Spectrum"));
        auto* grp  = specGrp;
        auto* vbox = new QVBoxLayout(grp);

        specEnable = new QCheckBox(tr("Enable spectrum display"));
        specEnable->setToolTip(tr("Show TX audio spectrum (input: green, output: orange). "
                                  "Uses a 1024-point sliding DFT, 50 Hz – 8 kHz."));
        vbox->addWidget(specEnable);

        specInhibitDuringRx = new QCheckBox(tr("Inhibit during receive"));
        specInhibitDuringRx->setToolTip(tr(
            "Pause the TX spectrum display while the radio is receiving.\n"
            "Automatically disabled when self-monitoring is enabled."));
        specInhibitDuringRx->setChecked(true);
        vbox->addWidget(specInhibitDuringRx);

        specWidget = new SpectrumWidget;
        specWidget->logBins    = true;
        specWidget->fftLength  = TxAudioProcessor::SPEC_FFT_LEN;
        specWidget->sampleRate = 48000.0;
        specWidget->showSecondary = true;
        specWidget->setMinimumHeight(120);
        specWidget->setVisible(false);   // hidden until checkbox is ticked
        // Expanding so specWidget (and therefore specGrp) absorbs all extra
        // vertical space when the user drags the window taller.
        specWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        vbox->addWidget(specWidget);

        // Stretch factor 1: when the window is taller than minimum, this group
        // (and only this group) grows to fill the extra space.
        mainLayout->addWidget(grp, 1);

        connect(specEnable, &QCheckBox::toggled,
                this, &TxAudioProcessingWidget::onSpecEnableToggled);
        connect(specInhibitDuringRx, &QCheckBox::toggled,
                this, &TxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── Monitoring (via SideTone) ────────────────────────────────────────────
    {
        sidetoneGrp = new QGroupBox(tr("Self-Monitor"));
        auto* grp = sidetoneGrp;
        auto* form = new QFormLayout(grp);

        auto* sidetoneEnableRow = new QHBoxLayout;
        sidetoneEnable = new QCheckBox(tr("Enable self-monitoring"));
        sidetoneEnable->setToolTip(tr("Enables Talk Back"));
        muteRxCheck    = new QCheckBox(tr("Mute RX Audio"));
        muteRxCheck->setToolTip(tr("Check to mute the receiver temporarily"));
        sidetoneEnableRow->addWidget(sidetoneEnable);
        sidetoneEnableRow->addWidget(muteRxCheck);
        sidetoneEnableRow->addStretch();
        form->addRow(sidetoneEnableRow);

        sidetoneLevel = new QSlider(Qt::Horizontal);
        sidetoneLevel->setRange(0, 100);
        sidetoneLevel->setValue(50);
        lblSidetone = new QLabel("50%");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(sidetoneLevel);
            row->addWidget(lblSidetone);
            form->addRow(tr("Monitor level:"), row);
        }

        mainLayout->addWidget(grp);
    }

    // ── Meters ──────────────────────────────────────────────────────────────
    {
        auto* grp = new QGroupBox(tr("Meters"));
        auto* hbox = new QHBoxLayout(grp);

        auto makeMeterBox = [&](const QString& label, meter*& m, meter_t type,
                                double lo, double hi, double red) {
            auto* vbox = new QVBoxLayout;
            vbox->addWidget(new QLabel(label));
            m = new meter;
            m->setMeterType(type);
            m->setMeterExtremities(lo, hi, red);
            m->setMinimumWidth(150);
            m->setMinimumHeight(40);
            m->enableCombo(false);
            vbox->addWidget(m);
            return vbox;
        };

        // meterAudio uses the log audiopot taper so scaleMin/Max are not used for
        // bar drawing; only haveExtremities matters. Redline at 241/255 ≈ -0.9 dBfs.
        // meterComp uses linear 0–255 (our updateGainReduction maps 0–20 dB → 0–255).
        hbox->addLayout(makeMeterBox(tr("Input"),          inputMeter,  meterAudio, 0, 255, 241));
        hbox->addLayout(makeMeterBox(tr("Output"),         outputMeter, meterAudio, 0, 255, 241));
        hbox->addLayout(makeMeterBox(tr("Gain Reduction"), grMeter,     meterComp,  0, 100, 75));
        grMeter->setCompReverse(true);

        mainLayout->addWidget(grp);
    }

    // ── Wire remaining signals ───────────────────────────────────────────────
    connect(clearEqBtn,    &QPushButton::clicked, this, &TxAudioProcessingWidget::onClearEq);
    connect(muteRxCheck,   &QCheckBox::toggled, this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(eqEnable,      &QCheckBox::toggled, this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(compEnable,    &QCheckBox::toggled, this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(orderCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TxAudioProcessingWidget::onPluginOrderChanged);
    connect(sidetoneEnable,&QCheckBox::toggled, this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(sidetoneLevel, &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(inputGain,     &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(outputGain,    &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(compPeakLimit, &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(compRelease,   &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(compFastRatio, &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(compSlowRatio, &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateEnable,    &QCheckBox::toggled,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateThreshold, &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateAttack,    &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateHold,      &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateDecay,     &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateRange,     &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateLfCutoff,  &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);
    connect(gateHfCutoff,  &QSlider::valueChanged,
            this, &TxAudioProcessingWidget::onAnyControlChanged);

    // Collapse/expand: mirror the RX widget pattern — set min/max height
    // constraints before resizing so Qt cannot redistribute freed space.
    connect(gateGrp,  &CollapsibleSection::expandedChanged, this,
            [this](bool) { updateSizeConstraints(); });
    connect(eqGrp,   &CollapsibleSection::expandedChanged, this,
            [this](bool) { updateSizeConstraints(); });
    connect(compGrp, &CollapsibleSection::expandedChanged, this,
            [this](bool) { updateSizeConstraints(); });

    updateSizeConstraints();
    // The initial call above runs before the widget is shown and Qt's style
    // has been polished, so sizeHints for controls like vertical QSliders
    // can be underestimates.  Fire a second pass after the first event-loop
    // tick, by which time the layout has been fully realized.
    QTimer::singleShot(0, this, [this]() { updateSizeConstraints(); });
}

// ─── updateSizeConstraints ───────────────────────────────────────────────────

void TxAudioProcessingWidget::updateSizeConstraints()
{
    // Force the layout chain to recompute cached sizes from scratch.
    // Without this, a previously-set setMaximumHeight() can leave stale
    // sizeHint values that cause extra space to be redistributed into
    // collapsed-section headers.
    layout()->invalidate();

    // Use the larger of sizeHint and minimumSizeHint so that widgets with
    // explicit setMinimumHeight() (e.g. vertical EQ sliders at 120 px) are
    // fully accounted for — their sizeHint() can be smaller than their
    // minimum, which would make the window too short at startup.
    const int minH = qMax(sizeHint().height(), minimumSizeHint().height());
    setMinimumHeight(minH);

    if (specEnable->isChecked()) {
        // Spectrum visible: allow vertical expansion beyond minimum so the
        // spectrum widget (the only Expanding item) absorbs extra space.
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else {
        // No spectrum: lock height to exactly the content size so Qt cannot
        // redistribute freed space into remaining sections.
        setMaximumHeight(minH);
    }

    resize(width(), minH);
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void TxAudioProcessingWidget::blockAll(bool block)
{
    bypassCheck->blockSignals(block);
    for (int i = 0; i < EQ_BANDS; ++i) eqSliders[i]->blockSignals(block);
    eqEnable->blockSignals(block);
    compEnable->blockSignals(block);
    compPeakLimit->blockSignals(block);
    compRelease->blockSignals(block);
    compFastRatio->blockSignals(block);
    compSlowRatio->blockSignals(block);
    inputGain->blockSignals(block);
    outputGain->blockSignals(block);
    orderCombo->blockSignals(block);
    sidetoneEnable->blockSignals(block);
    sidetoneLevel->blockSignals(block);
    muteRxCheck->blockSignals(block);
    specEnable->blockSignals(block);
    gateEnable->blockSignals(block);
    gateThreshold->blockSignals(block);
    gateAttack->blockSignals(block);
    gateHold->blockSignals(block);
    gateDecay->blockSignals(block);
    gateRange->blockSignals(block);
    gateLfCutoff->blockSignals(block);
    gateHfCutoff->blockSignals(block);
    specInhibitDuringRx->blockSignals(block);
}

void TxAudioProcessingWidget::populateFromPrefs(const txAudioProcessingPrefs& p)
{
    bypassCheck->setChecked(p.bypass);
    setProcessingControlsEnabled(!p.bypass);

    eqEnable->setChecked(p.eqEnabled);
    compEnable->setChecked(p.compEnabled);
    orderCombo->setCurrentIndex(p.eqFirst ? 0 : 1);
    reorderDspGroups(p.eqFirst);

    for (int i = 0; i < EQ_BANDS; ++i) {
        eqSliders[i]->setValue(static_cast<int>(std::round(p.eqBands[i] * 10.0f)));
        eqValues[i]->setText(QString::number(p.eqBands[i], 'f', 1));
    }

    compPeakLimit->setValue(static_cast<int>(std::round(p.compPeakLimit * 10.0f)));
    compRelease->setValue(static_cast<int>(std::round(p.compRelease * 100.0f)));
    compFastRatio->setValue(static_cast<int>(std::round((1-p.compFastRatio) * (100.0f))));
    compSlowRatio->setValue(static_cast<int>(std::round((1-p.compSlowRatio) * (100.0f))));

    inputGain->setValue(static_cast<int>(std::round(p.inputGainDB * 10.0f)));
    outputGain->setValue(static_cast<int>(std::round(p.outputGainDB * 10.0f)));

    sidetoneEnable->setChecked(p.sidetoneEnabled);
    sidetoneLevel->setValue(static_cast<int>(std::round(p.sidetoneLevel * 100.0f)));
    muteRxCheck->setChecked(false);

    specEnable->setChecked(p.spectrumEnabled);
    specInhibitDuringRx->setChecked(p.specInhibitDuringRx);
    specWidget->setVisible(p.spectrumEnabled);

    m_spectrumFps = qBound(1, p.spectrumFPS, 60);
    specWidget->setFps(m_spectrumFps);

    // Update labels
    lblPeak->setText(QString::number(p.compPeakLimit, 'f', 1) + " dB");
    lblRelease->setText(QString::number(p.compRelease, 'f', 2) + " s");
    { int s = qRound((1-p.compFastRatio) * 100.0f); lblFast->setText(QString("%1:1").arg(1 + s * 99 / 100, 3)); }
    { int s = qRound((1-p.compSlowRatio) * 100.0f); lblSlow->setText(QString("%1:1").arg(1 + s * 99 / 100, 3)); }
    lblInputGain->setText(QString::number(p.inputGainDB, 'f', 1) + " dB");
    lblOutputGain->setText(QString::number(p.outputGainDB, 'f', 1) + " dB");
    lblSidetone->setText(QString::number(static_cast<int>(p.sidetoneLevel * 100.0f)) + "%");

    gateEnable->setChecked(p.gateEnabled);
    gateThreshold->setValue(static_cast<int>(std::round(p.gateThreshold)));
    gateAttack->setValue(static_cast<int>(std::round(p.gateAttack)));
    gateHold->setValue(static_cast<int>(std::round(p.gateHold)));
    gateDecay->setValue(static_cast<int>(std::round(p.gateDecay)));
    gateRange->setValue(static_cast<int>(std::round(p.gateRange)));
    gateLfCutoff->setValue(static_cast<int>(std::round(p.gateLfCutoff)));
    gateHfCutoff->setValue(static_cast<int>(std::round(p.gateHfCutoff)));
    lblGateThreshold->setText(QString::number(static_cast<int>(p.gateThreshold)) + " dB");
    lblGateAttack->setText(QString::number(static_cast<int>(p.gateAttack)) + " ms");
    lblGateHold->setText(QString::number(static_cast<int>(p.gateHold)) + " ms");
    lblGateDecay->setText(QString::number(static_cast<int>(p.gateDecay)) + " ms");
    lblGateRange->setText(QString::number(static_cast<int>(p.gateRange)) + " dB");
    lblGateLfCutoff->setText(QString::number(static_cast<int>(p.gateLfCutoff)) + " Hz");
    lblGateHfCutoff->setText(QString::number(static_cast<int>(p.gateHfCutoff)) + " Hz");
}
