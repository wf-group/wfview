// RxAudioProcessingWidget — RX audio noise reduction settings dialog.
//
// All controls are built programmatically (no .ui file) for easy future
// conversion to QML:
//   QGroupBox   → QML GroupBox
//   QFormLayout → QML GridLayout / ColumnLayout
//   QSlider     → QML Slider
//   QComboBox   → QML ComboBox
//   QCheckBox   → QML CheckBox
//   QRadioButton→ QML RadioButton
//   QLabel      → QML Text / Label

#include "rxaudioprocessingwidget.h"
#include "speexnrprocessor.h"   // SpeexNrProcessor::presetCount() / bandsForPreset()
#include <cmath>
#include <QScrollArea>

// ─────────────────────────────────────────────────────────────────────────────

RxAudioProcessingWidget::RxAudioProcessingWidget(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("RX Audio Processing"));
    setWindowFlags(windowFlags() | Qt::Window);
    buildUi();
}

// ─── setPrefs / getPrefs ─────────────────────────────────────────────────────

void RxAudioProcessingWidget::setPrefs(const rxAudioProcessingPrefs& p)
{
    blockAll(true);
    populateFromPrefs(p);
    blockAll(false);
}

rxAudioProcessingPrefs RxAudioProcessingWidget::getPrefs() const
{
    rxAudioProcessingPrefs p;

    p.bypass        = bypassCheck->isChecked();
    p.channelSelect = channelCombo->currentIndex();  // 0=auto,1=ch1,2=ch2,3=ch1+ch2
    p.nrMode        = (algoGroup->checkedId() == 0) ? RxNrMode::Speex : RxNrMode::Spac;
    // nrEnabled follows master bypass: DSP is active whenever bypass is off
    p.nrEnabled     = !p.bypass;

    // Speex
    p.speexSuppression   = speexSuppress->value();          // already negative dB
    {
        int idx = speexBandsCombo->currentIndex();
        // Map combo index back to preset index (combo item data stores preset index)
        p.speexBandsPreset = speexBandsCombo->itemData(idx).toInt();
    }
    p.speexFrameMs       = (speexFrameCombo->currentIndex() == 0) ? 10 : 20;
    p.speexAgc           = speexAgcCheck->isChecked();
    p.speexAgcLevel      = static_cast<float>(speexAgcLevel->value());
    p.speexAgcMaxGain    = speexAgcMaxGain->value();
    p.speexVad           = speexVadCheck->isChecked();
    p.speexVadProbStart  = speexVadProbStart->value();
    p.speexVadProbCont   = speexVadProbCont->value();

    // SPAC
    p.spacFrameMs    = static_cast<float>(spacFrameMs->value());
    p.spacVoicingThr = spacVoicingThr->value()  * 0.01f;
    p.spacVoicingFull= spacVoicingFull->value() * 0.01f;
    p.spacAttenDb    = static_cast<float>(spacAttenDb->value());

    // Output gain
    p.outputGainDB   = outputGain->value() * 0.1f;

    return p;
}

// ─── setConnected / setAudioChannels ─────────────────────────────────────────

void RxAudioProcessingWidget::setConnected(bool connected)
{
    m_radioConnected = connected;
    controlsContainer->setEnabled(connected);
    if (!connected) {
        lblLatency->setText(tr("Latency: — ms  (not connected)"));
        lblInputPeak->setText(tr("Input: —"));
        lblOutputPeak->setText(tr("Output: —"));
    }
}

void RxAudioProcessingWidget::setAudioChannels(int ch)
{
    m_audioChannels = ch;
    channelGrp->setVisible(ch > 1);
    adjustSize();
}

// ─── Level / latency slots ────────────────────────────────────────────────────

void RxAudioProcessingWidget::updateLatency(float latencyMs)
{
    if (latencyMs <= 0.0f)
        lblLatency->setText(tr("Latency: 0 ms  (bypass / NR off)"));
    else
        lblLatency->setText(tr("Latency: %1 ms").arg(latencyMs, 0, 'f', 1));
}

void RxAudioProcessingWidget::updateInputLevel(float peak)
{
    int pct = static_cast<int>(peak * 100.0f);
    lblInputPeak->setText(tr("In: %1%").arg(pct));
}

void RxAudioProcessingWidget::updateOutputLevel(float peak)
{
    int pct = static_cast<int>(peak * 100.0f);
    lblOutputPeak->setText(tr("Out: %1%").arg(pct));
}

// ─── Any control changed ─────────────────────────────────────────────────────

void RxAudioProcessingWidget::onAnyControlChanged()
{
    // Update value labels
    lblSpeexSuppress->setText(QString::number(speexSuppress->value()) + " dB");
    lblAgcLevel->setText(QString::number(speexAgcLevel->value()));
    lblAgcMaxGain->setText(QString::number(speexAgcMaxGain->value()) + " dB");
    lblSpacFrameMs->setText(QString::number(spacFrameMs->value()) + " ms");
    lblSpacVoicing->setText(QString::number(spacVoicingThr->value() * 0.01f, 'f', 2));
    lblSpacVoicingFull->setText(QString::number(spacVoicingFull->value() * 0.01f, 'f', 2));
    lblSpacAtten->setText(QString::number(spacAttenDb->value()) + " dB");
    lblOutputGain->setText(QString::number(outputGain->value() * 0.1f, 'f', 1) + " dB");

    // Value labels
    lblVadProbStart->setText(QString::number(speexVadProbStart->value()) + " %");
    lblVadProbCont->setText(QString::number(speexVadProbCont->value())   + " %");

    // AGC controls visible only when checkbox is ticked
    bool agcVis = speexAgcCheck->isChecked();
    speexAgcLevel->setVisible(agcVis);   lblAgcLevel->setVisible(agcVis);
    speexAgcMaxGain->setVisible(agcVis); lblAgcMaxGain->setVisible(agcVis);

    // VAD prob sliders visible only when VAD is ticked
    bool vadVis = speexVadCheck->isChecked();
    speexVadProbStart->setVisible(vadVis); lblVadProbStart->setVisible(vadVis);
    speexVadProbCont->setVisible(vadVis);  lblVadProbCont->setVisible(vadVis);

    emit prefsChanged(getPrefs());
}

void RxAudioProcessingWidget::onBypassToggled(bool bypassed)
{
    setProcessingControlsEnabled(!bypassed);
    emit prefsChanged(getPrefs());
}

void RxAudioProcessingWidget::onNrModeChanged(int id)
{
    // 0 = Speex, 1 = SPAC
    speexGrp->setVisible(id == 0);
    spacGrp->setVisible(id == 1);
    adjustSize();
    emit prefsChanged(getPrefs());
}

void RxAudioProcessingWidget::onAlgorithmGroupToggled(bool)
{
    onNrModeChanged(algoGroup->checkedId());
}

// ─── setProcessingControlsEnabled ────────────────────────────────────────────

void RxAudioProcessingWidget::setProcessingControlsEnabled(bool enabled)
{
    algoGrp->setEnabled(enabled);
    speexGrp->setEnabled(enabled);
    spacGrp->setEnabled(enabled);
    gainGrp->setEnabled(enabled);
    // channelGrp intentionally not gated by bypass (user may still change it)
}

// ─── buildUi ─────────────────────────────────────────────────────────────────

void RxAudioProcessingWidget::buildUi()
{
    // Outer scroll area so the dialog doesn't exceed screen height
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* inner = new QWidget;
    auto* mainLayout = new QVBoxLayout(inner);
    mainLayout->setSpacing(6);

    controlsContainer = inner;

    // Helper: slider + label row, returns the layout and label for later access
    auto makeSliderRow = [](QSlider*& sl, QLabel*& lbl, int lo, int hi, int val,
                            int labelWidth = 70) -> QHBoxLayout* {
        sl  = new QSlider(Qt::Horizontal);
        sl->setRange(lo, hi);
        sl->setValue(val);
        lbl = new QLabel;
        lbl->setMinimumWidth(labelWidth);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* row = new QHBoxLayout;
        row->addWidget(sl, 1);
        row->addWidget(lbl);
        return row;
    };

    // ── Master Bypass ─────────────────────────────────────────────────────────
    {
        bypassCheck = new QCheckBox(tr("Master Bypass (pass audio unchanged)"));
        QFont f = bypassCheck->font();
        f.setBold(true);
        bypassCheck->setFont(f);
        mainLayout->addWidget(bypassCheck);
        connect(bypassCheck, &QCheckBox::toggled,
                this, &RxAudioProcessingWidget::onBypassToggled);
    }

    // ── Channel selection (hidden for mono, shown for stereo) ─────────────────
    {
        channelGrp  = new QGroupBox(tr("Input Channel"));
        auto* form  = new QFormLayout(channelGrp);
        channelCombo = new QComboBox;
        channelCombo->addItem(tr("Auto (mono / sum stereo)"));  // index 0
        channelCombo->addItem(tr("Channel 1 (Left)"));           // index 1
        channelCombo->addItem(tr("Channel 2 (Right)"));          // index 2
        channelCombo->addItem(tr("Ch 1 + Ch 2 (sum to mono)"));  // index 3
        channelCombo->setToolTip(tr(
            "Stereo input: select which channel(s) to send through the noise reducer.\n"
            "Auto: mono feeds are passed directly; stereo feeds are summed to mono.\n"
            "The unselected channel is passed without processing (Ch1 or Ch2 modes)."));
        form->addRow(tr("Process:"), channelCombo);
        mainLayout->addWidget(channelGrp);
        channelGrp->setVisible(false);  // shown when setAudioChannels(2) is called
        connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── Algorithm selector ────────────────────────────────────────────────────
    {
        algoGrp   = new QGroupBox(tr("Noise Reduction Algorithm"));
        auto* row = new QHBoxLayout(algoGrp);
        algoSpeex = new QRadioButton(tr("Speex (Ephraim-Malah spectral subtraction)"));
        algoSpac  = new QRadioButton(tr("SPAC  (Splicing of AutoCorrelation, voiced-only)"));
        algoSpeex->setChecked(true);
        algoGroup = new QButtonGroup(this);
        algoGroup->addButton(algoSpeex, 0);
        algoGroup->addButton(algoSpac,  1);
        algoSpeex->setToolTip(tr(
            "Speex preprocessor: statistical noise estimation using a minimum mean-square\n"
            "error estimator.  Works on all audio types.  Latency = one frame (~20 ms)."));
        algoSpac->setToolTip(tr(
            "SPAC: reconstructs voiced speech from its autocorrelation, attenuating unvoiced\n"
            "noise.  Best for SSB/AM voice signals.  Latency = one frame (~20 ms)."));
        row->addWidget(algoSpeex);
        row->addWidget(algoSpac);
        row->addStretch();
        mainLayout->addWidget(algoGrp);
        connect(algoSpeex, &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
        connect(algoSpac,  &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
    }

    // ── Speex controls ────────────────────────────────────────────────────────
    {
        speexGrp = new QGroupBox(tr("Speex Noise Suppressor"));
        auto* form = new QFormLayout(speexGrp);

        // Suppression
        {
            auto* row = makeSliderRow(speexSuppress, lblSpeexSuppress, -70, -1, -30, 80);
            speexSuppress->setToolTip(tr("Maximum noise attenuation in dB (e.g. -30 dB).  "
                                         "More negative = stronger suppression, more artefacts."));
            lblSpeexSuppress->setText("-30 dB");
            form->addRow(tr("Suppression:"), row);
        }

        // Band preset
        {
            speexBandsCombo = new QComboBox;
            const int nPresets = SpeexNrProcessor::presetCount();
            for (int i = 0; i < nPresets; ++i) {
                int bands = SpeexNrProcessor::bandsForPreset(i);
                speexBandsCombo->addItem(tr("%1 bands").arg(bands), QVariant(i));
            }
            speexBandsCombo->setToolTip(tr(
                "Number of Bark-scale frequency bands.\n"
                "More bands → finer spectral resolution, slower noise adaptation.\n"
                "Fewer bands → coarser but faster (good for rapid noise-floor changes)."));
            form->addRow(tr("Bark bands:"), speexBandsCombo);
            connect(speexBandsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &RxAudioProcessingWidget::onAnyControlChanged);
        }

        // Frame size
        {
            speexFrameCombo = new QComboBox;
            speexFrameCombo->addItem(tr("10 ms frames"));
            speexFrameCombo->addItem(tr("20 ms frames"));
            speexFrameCombo->setCurrentIndex(1);  // default 20 ms
            speexFrameCombo->setToolTip(tr(
                "Speex processing frame length.\n"
                "10 ms: lower latency, coarser frequency resolution.\n"
                "20 ms: higher latency, better frequency resolution (recommended)."));
            form->addRow(tr("Frame size:"), speexFrameCombo);
            connect(speexFrameCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &RxAudioProcessingWidget::onAnyControlChanged);
        }

        // AGC
        {
            speexAgcCheck = new QCheckBox(tr("Enable automatic gain control (AGC)"));
            speexAgcCheck->setToolTip(tr(
                "Speex AGC normalises the signal level to a target RMS.\n"
                "Useful when the radio's AF gain drifts or signals vary widely."));
            form->addRow(speexAgcCheck);

            // AGC level: 1000–32000 (RMS target; 8000 is Speex default)
            speexAgcLevel = new QSlider(Qt::Horizontal);
            speexAgcLevel->setRange(500, 32000);
            speexAgcLevel->setValue(8000);
            speexAgcLevel->setToolTip(tr("Target RMS level for AGC (Speex default: 8000)."));
            lblAgcLevel = new QLabel("8000");
            lblAgcLevel->setMinimumWidth(60);
            {
                auto* row = new QHBoxLayout;
                row->addWidget(speexAgcLevel, 1);
                row->addWidget(lblAgcLevel);
                form->addRow(tr("  AGC level:"), row);
            }

            auto* rowMG = makeSliderRow(speexAgcMaxGain, lblAgcMaxGain, 0, 60, 30, 60);
            speexAgcMaxGain->setToolTip(tr("Maximum gain the AGC may apply (dB)."));
            lblAgcMaxGain->setText("30 dB");
            form->addRow(tr("  AGC max gain:"), rowMG);
        }

        // VAD
        {
            speexVadCheck = new QCheckBox(tr("Enable voice activity detection (VAD)"));
            speexVadCheck->setToolTip(tr(
                "Speex VAD classifies each frame as speech or non-speech.\n"
                "When active, non-speech frames are suppressed more aggressively.\n"
                "Adjust prob-start and prob-continue to control sensitivity."));
            form->addRow(speexVadCheck);

            auto* rowPS = makeSliderRow(speexVadProbStart, lblVadProbStart, 0, 100, 85, 60);
            speexVadProbStart->setToolTip(tr(
                "Probability required to switch from silence to voice state (%).\n"
                "Higher = harder to trigger voice detection."));
            lblVadProbStart->setText("85 %");
            form->addRow(tr("  Prob-start:"), rowPS);

            auto* rowPC = makeSliderRow(speexVadProbCont, lblVadProbCont, 0, 100, 65, 60);
            speexVadProbCont->setToolTip(tr(
                "Probability required to remain in voice state (%).\n"
                "Lower = voice state is held longer; higher = drops out faster."));
            lblVadProbCont->setText("65 %");
            form->addRow(tr("  Prob-cont:"), rowPC);
        }

        mainLayout->addWidget(speexGrp);
        connect(speexSuppress,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcCheck,     &QCheckBox::toggled,    this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcLevel,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcMaxGain,   &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadCheck,     &QCheckBox::toggled,    this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadProbStart, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadProbCont,  &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── SPAC controls ─────────────────────────────────────────────────────────
    {
        spacGrp  = new QGroupBox(tr("SPAC Noise Reducer  (voiced speech enhancement)"));
        auto* form = new QFormLayout(spacGrp);

        // Frame size: 10–50 ms
        {
            auto* row = makeSliderRow(spacFrameMs, lblSpacFrameMs, 10, 50, 20, 70);
            spacFrameMs->setToolTip(tr(
                "Autocorrelation frame length (ms).\n"
                "Shorter frames: lower latency, poorer pitch estimation.\n"
                "Longer frames: better pitch tracking, higher latency."));
            lblSpacFrameMs->setText("20 ms");
            form->addRow(tr("Frame size:"), row);
        }

        // Voicing threshold: 0–100 (×0.01 → 0.0–1.0)
        {
            auto* row = makeSliderRow(spacVoicingThr, lblSpacVoicing, 1, 99, 20, 70);
            spacVoicingThr->setToolTip(tr(
                "Voicing threshold (0.0–1.0): normalised autocorrelation peak must exceed\n"
                "this value before a frame is treated as voiced speech.\n"
                "Lower = more permissive (passes more frames); higher = stricter."));
            lblSpacVoicing->setText("0.20");
            form->addRow(tr("Voicing thr:"), row);
        }

        // Voicing full: must be > voicingThr
        {
            auto* row = makeSliderRow(spacVoicingFull, lblSpacVoicingFull, 1, 99, 55, 70);
            spacVoicingFull->setToolTip(tr(
                "Full-blend upper bound (0.0–1.0): above this confidence the output is\n"
                "100%% SPAC-processed.  Must be greater than Voicing threshold.\n"
                "Closer to threshold: fast transition; farther: gradual blend."));
            lblSpacVoicingFull->setText("0.55");
            form->addRow(tr("Full blend at:"), row);
        }

        // Attenuation: 0–120 dB
        {
            auto* row = makeSliderRow(spacAttenDb, lblSpacAtten, 0, 120, 80, 70);
            spacAttenDb->setToolTip(tr(
                "Attenuation applied to unvoiced frames (positive dB).\n"
                "0 = no attenuation; 80–120 = near silence for unvoiced content."));
            lblSpacAtten->setText("80 dB");
            form->addRow(tr("Unvoiced atten:"), row);
        }

        spacGrp->setVisible(false);  // shown when SPAC radio button is selected
        mainLayout->addWidget(spacGrp);

        connect(spacFrameMs,    &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(spacVoicingThr, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(spacVoicingFull,&QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(spacAttenDb,    &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── Output gain ───────────────────────────────────────────────────────────
    {
        gainGrp = new QGroupBox(tr("Output Gain"));
        auto* form = new QFormLayout(gainGrp);
        // -60..200 maps to -6..+20 dB at ×0.1 resolution
        auto* row = makeSliderRow(outputGain, lblOutputGain, -60, 200, 0, 80);
        outputGain->setTickPosition(QSlider::TicksBelow);
        outputGain->setTickInterval(50);
        outputGain->setToolTip(tr(
            "Post-NR output gain adjustment (-6 dB to +20 dB).\n"
            "Use to compensate for any level change caused by noise reduction."));
        lblOutputGain->setText("0.0 dB");
        form->addRow(tr("Output gain:"), row);
        mainLayout->addWidget(gainGrp);
        connect(outputGain, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── Level / latency status ────────────────────────────────────────────────
    {
        auto* statusGrp  = new QGroupBox(tr("Status"));
        auto* statusForm = new QFormLayout(statusGrp);

        lblInputPeak  = new QLabel("In: —");
        lblOutputPeak = new QLabel("Out: —");
        lblLatency    = new QLabel(tr("Latency: — ms"));

        auto* levelRow = new QHBoxLayout;
        levelRow->addWidget(lblInputPeak);
        levelRow->addSpacing(12);
        levelRow->addWidget(lblOutputPeak);
        levelRow->addStretch();

        statusForm->addRow(tr("Signal:"),  levelRow);
        statusForm->addRow(tr("Latency:"), lblLatency);
        mainLayout->addWidget(statusGrp);
    }

    mainLayout->addStretch();

    scroll->setWidget(inner);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    // Start disabled — enabled when radio connects
    controlsContainer->setEnabled(false);

    adjustSize();
}

// ─── blockAll ─────────────────────────────────────────────────────────────────

void RxAudioProcessingWidget::blockAll(bool block)
{
    bypassCheck->blockSignals(block);
    channelCombo->blockSignals(block);
    algoSpeex->blockSignals(block);
    algoSpac->blockSignals(block);
    speexSuppress->blockSignals(block);
    speexBandsCombo->blockSignals(block);
    speexFrameCombo->blockSignals(block);
    speexAgcCheck->blockSignals(block);
    speexAgcLevel->blockSignals(block);
    speexAgcMaxGain->blockSignals(block);
    speexVadCheck->blockSignals(block);
    speexVadProbStart->blockSignals(block);
    speexVadProbCont->blockSignals(block);
    spacFrameMs->blockSignals(block);
    spacVoicingThr->blockSignals(block);
    spacVoicingFull->blockSignals(block);
    spacAttenDb->blockSignals(block);
    outputGain->blockSignals(block);
}

// ─── populateFromPrefs ────────────────────────────────────────────────────────

void RxAudioProcessingWidget::populateFromPrefs(const rxAudioProcessingPrefs& p)
{
    bypassCheck->setChecked(p.bypass);
    setProcessingControlsEnabled(!p.bypass);

    // Channel select — clamp to valid range
    int ch = qBound(0, p.channelSelect, channelCombo->count() - 1);
    channelCombo->setCurrentIndex(ch);

    // Algorithm
    bool isSpeex = (p.nrMode == RxNrMode::Speex);
    algoSpeex->setChecked(isSpeex);
    algoSpac->setChecked(!isSpeex);
    speexGrp->setVisible(isSpeex);
    spacGrp->setVisible(!isSpeex);

    // Speex
    speexSuppress->setValue(qBound(-70, p.speexSuppression, -1));
    lblSpeexSuppress->setText(QString::number(p.speexSuppression) + " dB");

    // Find combo entry whose item data matches the saved preset index.
    // If not found (e.g. filterbank.h has fewer presets now), fall back to last entry.
    {
        int matchIdx = speexBandsCombo->count() - 1;
        for (int i = 0; i < speexBandsCombo->count(); ++i) {
            if (speexBandsCombo->itemData(i).toInt() == p.speexBandsPreset) {
                matchIdx = i;
                break;
            }
        }
        speexBandsCombo->setCurrentIndex(matchIdx);
    }

    speexFrameCombo->setCurrentIndex((p.speexFrameMs <= 10) ? 0 : 1);

    speexAgcCheck->setChecked(p.speexAgc);
    speexAgcLevel->setValue(qBound(500, static_cast<int>(p.speexAgcLevel), 32000));
    speexAgcMaxGain->setValue(qBound(0, p.speexAgcMaxGain, 60));
    lblAgcLevel->setText(QString::number(static_cast<int>(p.speexAgcLevel)));
    lblAgcMaxGain->setText(QString::number(p.speexAgcMaxGain) + " dB");

    bool agcVis = p.speexAgc;
    speexAgcLevel->setVisible(agcVis);   lblAgcLevel->setVisible(agcVis);
    speexAgcMaxGain->setVisible(agcVis); lblAgcMaxGain->setVisible(agcVis);

    speexVadCheck->setChecked(p.speexVad);
    speexVadProbStart->setValue(qBound(0, p.speexVadProbStart, 100));
    speexVadProbCont->setValue(qBound(0, p.speexVadProbCont, 100));
    lblVadProbStart->setText(QString::number(p.speexVadProbStart) + " %");
    lblVadProbCont->setText(QString::number(p.speexVadProbCont)   + " %");
    bool vadVis = p.speexVad;
    speexVadProbStart->setVisible(vadVis); lblVadProbStart->setVisible(vadVis);
    speexVadProbCont->setVisible(vadVis);  lblVadProbCont->setVisible(vadVis);

    // SPAC
    spacFrameMs->setValue(qBound(10, static_cast<int>(p.spacFrameMs), 50));
    spacVoicingThr->setValue(qBound(1, qRound(p.spacVoicingThr * 100.0f), 99));
    spacVoicingFull->setValue(qBound(1, qRound(p.spacVoicingFull * 100.0f), 99));
    spacAttenDb->setValue(qBound(0, static_cast<int>(p.spacAttenDb), 120));
    lblSpacFrameMs->setText(QString::number(spacFrameMs->value()) + " ms");
    lblSpacVoicing->setText(QString::number(spacVoicingThr->value() * 0.01f, 'f', 2));
    lblSpacVoicingFull->setText(QString::number(spacVoicingFull->value() * 0.01f, 'f', 2));
    lblSpacAtten->setText(QString::number(spacAttenDb->value()) + " dB");

    // Output gain: -6..+20 dB in 0.1 dB steps → integer range -60..200
    outputGain->setValue(qBound(-60, qRound(p.outputGainDB * 10.0f), 200));
    lblOutputGain->setText(QString::number(p.outputGainDB, 'f', 1) + " dB");
}
