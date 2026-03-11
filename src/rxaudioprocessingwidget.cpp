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
    const int algoId = algoGroup->checkedId();
    p.nrMode = (algoId == 0) ? RxNrMode::Speex : RxNrMode::Anr;
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

    // ANR
    p.anrNoiseReductionDb = anrNoiseRedSlider->value();
    p.anrSensitivity      = anrSensSlider->value() * 0.1;
    p.anrFreqSmoothing    = anrSmoothSlider->value();

    // Output gain
    p.outputGainDB   = outputGain->value() * 0.1f;

    return p;
}

// ─── setConnected / setAudioChannels ─────────────────────────────────────────

void RxAudioProcessingWidget::setConnected(bool connected)
{
    m_radioConnected = connected;
    controlsContainer->setEnabled(connected);
    anrCollectBtn->setEnabled(connected);
    if (!connected) {
        if (m_anrCollecting) {
            anrCollectTimer->stop();
            m_anrCollecting = false;
            anrCollectBtn->setText(tr("Collect Noise Sample"));
        }
    }
}

void RxAudioProcessingWidget::setAudioChannels(int ch)
{
    m_audioChannels = ch;
    channelGrp->setVisible(ch > 1);
    adjustSize();
}

// ─── Any control changed ─────────────────────────────────────────────────────

void RxAudioProcessingWidget::onAnyControlChanged()
{
    // Update value labels
    lblSpeexSuppress->setText(QString::number(speexSuppress->value()) + " dB");
    lblAgcLevel->setText(QString::number(speexAgcLevel->value()));
    lblAgcMaxGain->setText(QString::number(speexAgcMaxGain->value()) + " dB");
    lblOutputGain->setText(QString::number(outputGain->value() * 0.1f, 'f', 1) + " dB");
    lblAnrNoiseRed->setText(QString::number(anrNoiseRedSlider->value()) + " dB");
    lblAnrSens->setText(QString::number(anrSensSlider->value() * 0.1, 'f', 1));
    lblAnrSmooth->setText(QString::number(anrSmoothSlider->value()));

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
    // 0 = Speex, 1 = ANR
    algoStack->setCurrentIndex(id);
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
    algoStack->setEnabled(enabled);
    gainGrp->setEnabled(enabled);
    // ANR group: outer box enabled by bypass, but inner controls gated by profile
    anrGrp->setEnabled(enabled);
    if (enabled)
        updateAnrControlState();
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
        algoSpeex = new QRadioButton(tr("Speex"));
        algoAnr   = new QRadioButton(tr("ANR"));
        algoSpeex->setChecked(true);
        algoGroup = new QButtonGroup(this);
        algoGroup->addButton(algoSpeex, 0);
        algoGroup->addButton(algoAnr,   1);
        algoSpeex->setToolTip(tr(
            "Speex preprocessor: statistical noise estimation using a minimum mean-square\n"
            "error estimator using Ephraim-Malah spectral subtraction\n"
            "Works on all audio types.  Latency = one frame (~20 ms)."));
        algoAnr->setToolTip(tr(
            "ANR: Audacity Noise Reduction — requires a noise sample to be collected first.\n"
            "Analyses the noise spectrum and suppresses matching frequencies.\n"
            "Works on broadband noise.  Latency ≈ 40–80 ms depending on sample rate."));
        row->addWidget(algoSpeex);
        row->addWidget(algoAnr);
        row->addStretch();
        mainLayout->addWidget(algoGrp);
        connect(algoSpeex, &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
        connect(algoAnr,   &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
    }

    // ── Algorithm-specific groups (in a QStackedWidget so width never changes) ─
    algoStack = new QStackedWidget;
    mainLayout->addWidget(algoStack);

    // ── Speex controls — stack page 0 ─────────────────────────────────────────
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

        algoStack->addWidget(speexGrp);  // page 0
        connect(speexSuppress,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcCheck,     &QCheckBox::toggled,    this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcLevel,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcMaxGain,   &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadCheck,     &QCheckBox::toggled,    this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadProbStart, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadProbCont,  &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── ANR controls — stack page 1 ───────────────────────────────────────────
    {
        anrGrp = new QGroupBox(tr("ANR — Audacity Noise Reduction"));
        auto* form = new QFormLayout(anrGrp);

        // Noise reduction
        {
            auto* row = makeSliderRow(anrNoiseRedSlider, lblAnrNoiseRed, 0, 48, 12, 70);
            anrNoiseRedSlider->setToolTip(tr(
                "Amount of noise suppression in dB.\n"
                "Higher values reduce more noise but may introduce artefacts.\n"
                "Typical range: 6–24 dB."));
            lblAnrNoiseRed->setText("12 dB");
            form->addRow(tr("Noise reduction:"), row);
        }

        // Sensitivity
        {
            // Stored as 0–240 integer (×0.1 → 0.0–24.0)
            auto* row = makeSliderRow(anrSensSlider, lblAnrSens, 0, 100, 60, 70);
            anrSensSlider->setToolTip(tr(
                "Sensitivity: −log₁₀ of the probability that noise exceeds the threshold.\n"
                "Higher = less aggressive (fewer false positives, may miss noise).\n"
                "Lower  = more aggressive (may suppress signal as well as noise).\n"
                "Strategy 1: Tune to static and lower until you hear chimes, then raise slightly.\n"
                "Strategy 2: Tune to a signal and start low. Raise until the signal is nominally loud (watch the RxAudio meter)\n"
                "Default 1.1 is a good starting point."));
            lblAnrSens->setText("1.1");
            form->addRow(tr("Sensitivity:"), row);
        }

        // Frequency smoothing
        {
            auto* row = makeSliderRow(anrSmoothSlider, lblAnrSmooth, 0, 6, 0, 70);
            anrSmoothSlider->setToolTip(tr(
                "Frequency smoothing (bands).\n"
                "Averages gain across neighbouring frequency bins to reduce musical noise.\n"
                "0 = off (sharper but may produce tonal artefacts); 3–6 = smooth."));
            lblAnrSmooth->setText("0");
            form->addRow(tr("Freq smoothing:"), row);
        }

        // Noise sample collection
        {
            anrCollectBtn = new QPushButton(tr("Collect Noise Sample"));
            anrCollectBtn->setToolTip(tr(
                "Click to start collecting a noise sample.\n"
                "Play audio containing only background noise, then click Stop.\n"
                "Collection stops automatically after 5 seconds.\n"
                "ANR will be disabled until a valid noise sample is collected."));
            anrCollectBtn->setEnabled(false); // enabled when radio connects

            lblAnrStatus = new QLabel(tr("No noise sample — collect one before using ANR."));
            lblAnrStatus->setWordWrap(true);
            QFont f = lblAnrStatus->font();
            f.setItalic(true);
            lblAnrStatus->setFont(f);

            form->addRow(anrCollectBtn);
            form->addRow(lblAnrStatus);

            // 5-second auto-stop timer
            anrCollectTimer = new QTimer(this);
            anrCollectTimer->setSingleShot(true);
            anrCollectTimer->setInterval(5000);
            connect(anrCollectTimer, &QTimer::timeout,
                    this, &RxAudioProcessingWidget::onAnrCollectTimeout);
            connect(anrCollectBtn, &QPushButton::clicked,
                    this, &RxAudioProcessingWidget::onAnrCollectClicked);
        }

        algoStack->addWidget(anrGrp);  // page 1

        connect(anrNoiseRedSlider, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(anrSensSlider,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(anrSmoothSlider,   &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
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

    mainLayout->addStretch();

    scroll->setWidget(inner);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    // Start disabled — enabled when radio connects
    controlsContainer->setEnabled(false);

    // Lock width: size the dialog to its full content, then fix the horizontal
    // dimension so it never changes when switching between NR algorithm pages.
    adjustSize();
    setFixedWidth(500);
    setFixedHeight(1000);
}

// ─── blockAll ─────────────────────────────────────────────────────────────────

void RxAudioProcessingWidget::blockAll(bool block)
{
    bypassCheck->blockSignals(block);
    channelCombo->blockSignals(block);
    algoSpeex->blockSignals(block);
    speexSuppress->blockSignals(block);
    speexBandsCombo->blockSignals(block);
    speexFrameCombo->blockSignals(block);
    speexAgcCheck->blockSignals(block);
    speexAgcLevel->blockSignals(block);
    speexAgcMaxGain->blockSignals(block);
    speexVadCheck->blockSignals(block);
    speexVadProbStart->blockSignals(block);
    speexVadProbCont->blockSignals(block);
    anrNoiseRedSlider->blockSignals(block);
    anrSensSlider->blockSignals(block);
    anrSmoothSlider->blockSignals(block);
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
    const bool isSpeex = (p.nrMode == RxNrMode::Speex);
    const bool isAnr   = (p.nrMode == RxNrMode::Anr);
    algoSpeex->setChecked(isSpeex);
    algoAnr->setChecked(isAnr);
    algoStack->setCurrentIndex(isAnr ? 1 : 0);

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

    // ANR
    anrNoiseRedSlider->setValue(qBound(0, static_cast<int>(p.anrNoiseReductionDb), 48));
    lblAnrNoiseRed->setText(QString::number(anrNoiseRedSlider->value()) + " dB");
    // Sensitivity stored as 0–240 integer (×0.1 → 0.0..24.0)
    anrSensSlider->setValue(qBound(0, qRound(p.anrSensitivity * 10.0), 240));
    lblAnrSens->setText(QString::number(anrSensSlider->value() * 0.1, 'f', 1));
    anrSmoothSlider->setValue(qBound(0, p.anrFreqSmoothing, 6));
    lblAnrSmooth->setText(QString::number(anrSmoothSlider->value()));
    updateAnrControlState();

    // Output gain: -6..+20 dB in 0.1 dB steps → integer range -60..200
    outputGain->setValue(qBound(-60, qRound(p.outputGainDB * 10.0f), 200));
    lblOutputGain->setText(QString::number(p.outputGainDB, 'f', 1) + " dB");
}

// ─── ANR profile slots ────────────────────────────────────────────────────────

void RxAudioProcessingWidget::onAnrCollectClicked()
{
    if (m_anrCollecting) {
        // User pressed "Stop Collecting" early
        anrCollectTimer->stop();
        m_anrCollecting = false;
        anrCollectBtn->setText(tr("Collect Noise Sample"));
        lblAnrStatus->setText(tr("Collecting stopped — waiting for profile to build…"));
        anrCollectBtn->setEnabled(false);
        emit anrCollectToggled(false);   // triggers wfmain → rxProc->stopAnrProfile()
    } else {
        // User pressed "Collect Noise Sample"
        m_anrCollecting = true;
        anrCollectBtn->setText(tr("Stop Collecting  (auto-stops in 5 s)"));
        lblAnrStatus->setText(tr("Recording noise sample — play audio with only background noise…"));
        anrCollectTimer->start();        // auto-stop after 5 seconds
        emit anrCollectToggled(true);    // triggers wfmain → rxProc->startAnrProfile()
    }
}

void RxAudioProcessingWidget::onAnrCollectTimeout()
{
    // Timer fired: auto-stop collection
    if (!m_anrCollecting) return;
    m_anrCollecting = false;
    anrCollectBtn->setText(tr("Collect Noise Sample"));
    lblAnrStatus->setText(tr("5 s sample collected — building noise profile…"));
    anrCollectBtn->setEnabled(false);
    emit anrCollectToggled(false);  // triggers wfmain → rxProc->stopAnrProfile()
}

void RxAudioProcessingWidget::onAnrProfileReady(bool success)
{
    anrCollectBtn->setEnabled(m_radioConnected);
    if (success) {
        m_anrHasProfile = true;
        lblAnrStatus->setText(tr("Noise profile ready — ANR active."));
    } else {
        m_anrHasProfile = false;
        lblAnrStatus->setText(tr("Profile build failed (sample too short?).  Try again."));
    }
    updateAnrControlState();
}

// ─── updateAnrControlState ────────────────────────────────────────────────────

void RxAudioProcessingWidget::updateAnrControlState()
{
    const bool canProcess = m_anrHasProfile && !m_anrCollecting;
    anrNoiseRedSlider->setEnabled(canProcess);
    anrSensSlider->setEnabled(canProcess);
    anrSmoothSlider->setEnabled(canProcess);
    if (!m_anrHasProfile && !m_anrCollecting)
        lblAnrStatus->setText(tr("No noise sample — collect one before using ANR."));
}
