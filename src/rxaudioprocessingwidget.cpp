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
#include <QSizePolicy>

// ─────────────────────────────────────────────────────────────────────────────

RxAudioProcessingWidget::RxAudioProcessingWidget(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("RX Audio Processing"));
    setWindowFlags(windowFlags() | Qt::Window);
    buildUi();
    connect(&m_specDiagTimer, &QTimer::timeout,
            this, &RxAudioProcessingWidget::onSpecDiagTimer);
    m_specDiagTimer.setInterval(1000);
    m_specDiagTimer.start();
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
    p.nrMode = static_cast<RxNrMode>(algoId);  // 0=None, 1=Speex, 2=ANR
    // nrEnabled follows master bypass and NR mode selection
    p.nrEnabled     = !p.bypass && (p.nrMode != RxNrMode::None);

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
    p.speexSnrDecay        = speexSnrDecay->value()    * 0.01f;
    p.speexNoiseUpdateRate = speexNoiseUpdate->value()  * 0.01f;
    p.speexPriorBase       = speexPriorBase->value()   * 0.01f;

    // ANR
    p.anrNoiseReductionDb = anrNoiseRedSlider->value();
    p.anrSensitivity      = anrSensSlider->value() * 0.1;
    p.anrFreqSmoothing    = anrSmoothSlider->value();

    // EQ
    p.eqEnabled = eqEnableCheck->isChecked();
    for (int i = 0; i < RX_EQ_BANDS; ++i) {
        p.eqGain[i] = eqGainSlider[i]->value() * 0.1f;  // slider int → dB
        p.eqFreq[i] = static_cast<float>(eqFreqDial[i]->value());
        p.eqQ[i]    = 1.0f;  // Q stored in prefs but not exposed in UI
    }

    // Output gain
    p.outputGainDB   = outputGain->value() * 0.1f;

    // Spectrum
    p.spectrumEnabled       = specEnable->isChecked();
    p.spectrumFPS           = m_spectrumFps;
    p.specInhibitDuringTx   = specInhibitDuringTx->isChecked();

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
        }
        anrCollectBtn->setText(m_anrHasProfile ? tr("Collect New Noise Sample")
                                               : tr("Collect Noise Sample"));
    }
}

void RxAudioProcessingWidget::setAudioChannels(int ch)
{
    m_audioChannels = ch;
    channelGrp->setVisible(ch > 1);
    updateSizeConstraints();
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
    lblSnrDecay->setText(QString::number(speexSnrDecay->value()   * 0.01, 'f', 2));
    lblNoiseUpdate->setText(QString::number(speexNoiseUpdate->value() * 0.01, 'f', 2));
    lblPriorBase->setText(QString::number(speexPriorBase->value()  * 0.01, 'f', 2));

    // EQ gain/freq labels
    for (int i = 0; i < RX_EQ_BANDS; ++i) {
        eqGainLabel[i]->setText(QString::number(eqGainSlider[i]->value() * 0.1f, 'f', 1) + " dB");
        int freq = eqFreqDial[i]->value();
        if (freq >= 1000)
            eqFreqLabel[i]->setText(QString::number(freq * 0.001, 'f', 1) + " kHz");
        else
            eqFreqLabel[i]->setText(QString::number(freq) + " Hz");
    }
    // EQ controls visible when checkbox is ticked
    {
        bool eqVis = eqEnableCheck->isChecked();
        eqBandsWidget->setVisible(eqVis);
        updateSizeConstraints();
    }

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
    // 0 = None, 1 = Speex, 2 = ANR — show/hide directly so layout adapts
    nonePage->setVisible(id == 0);
    speexGrp->setVisible(id == 1);
    anrGrp->setVisible(id == 2);
    updateSizeConstraints();
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
    eqGrp->setEnabled(enabled);
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
        algoNone  = new QRadioButton(tr("None"));
        algoSpeex = new QRadioButton(tr("Speex"));
        algoAnr   = new QRadioButton(tr("ANR"));
        algoNone->setChecked(true);
        algoGroup = new QButtonGroup(this);
        algoGroup->addButton(algoNone,  0);
        algoGroup->addButton(algoSpeex, 1);
        algoGroup->addButton(algoAnr,   2);
        algoNone->setToolTip(tr(
            "No noise reduction — audio passes through to the EQ and output gain only."));
        algoSpeex->setToolTip(tr(
            "Speex preprocessor: statistical noise estimation using a minimum mean-square\n"
            "error estimator using Ephraim-Malah spectral subtraction\n"
            "Works on all audio types.  Latency = one frame (~20 ms)."));
        algoAnr->setToolTip(tr(
            "ANR: Audacity Noise Reduction — requires a noise sample to be collected first.\n"
            "Analyses the noise spectrum and suppresses matching frequencies.\n"
            "Works on broadband noise.  Latency ≈ 40–80 ms depending on sample rate."));
        row->addWidget(algoNone);
        row->addWidget(algoSpeex);
        row->addWidget(algoAnr);
        row->addStretch();
        mainLayout->addWidget(algoGrp);
        connect(algoNone,  &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
        connect(algoSpeex, &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
        connect(algoAnr,   &QRadioButton::toggled, this, &RxAudioProcessingWidget::onAlgorithmGroupToggled);
    }

    // ── Algorithm-specific groups (shown/hidden based on selection) ─────────
    // ── "None" placeholder ───────────────────────────────────────────────────
    {
        nonePage = new QWidget;
        auto* noneLayout = new QVBoxLayout(nonePage);
        noneLayout->setContentsMargins(0, 0, 0, 0);
        auto* noneLabel = new QLabel(tr("No noise reduction selected.\n"
            "Audio passes through to the equalizer and output gain."));
        noneLabel->setWordWrap(true);
        QFont f = noneLabel->font();
        f.setItalic(true);
        noneLabel->setFont(f);
        noneLayout->addWidget(noneLabel);
        mainLayout->addWidget(nonePage);  // visible by default (None selected)
    }

    // ── Speex controls — stack page 1 ─────────────────────────────────────────
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

        // ── Adaptation speed controls ──
        {
            auto* sepLabel = new QLabel(tr("Adaptation Speed"));
            QFont sf = sepLabel->font();
            sf.setBold(true);
            sepLabel->setFont(sf);
            form->addRow(sepLabel);

            // SNR Decay: 0..95 → 0.00..0.95
            auto* rowSD = makeSliderRow(speexSnrDecay, lblSnrDecay, 0, 95, 70, 60);
            speexSnrDecay->setToolTip(tr(
                "SNR smoothing decay (0.00–0.95, default 0.70).\n"
                "Controls how long the algorithm \"remembers\" speech was present.\n"
                "Lower = faster recovery (static suppressed sooner after speech).\n"
                "Higher = smoother but slower adaptation.\n"
                "Recommended: 0.30–0.60 for faster response."));
            lblSnrDecay->setText("0.70");
            form->addRow(tr("  SNR decay:"), rowSD);

            // Noise update rate: 1..50 → 0.01..0.50
            auto* rowNU = makeSliderRow(speexNoiseUpdate, lblNoiseUpdate, 1, 50, 3, 60);
            speexNoiseUpdate->setToolTip(tr(
                "Noise floor update rate (0.01–0.50, default 0.03).\n"
                "How fast the noise floor estimate adapts.\n"
                "Higher = noise estimate catches up faster after speech.\n"
                "Too high may cause the algorithm to treat speech as noise."));
            lblNoiseUpdate->setText("0.03");
            form->addRow(tr("  Noise update:"), rowNU);

            // Prior base: 5..50 → 0.05..0.50
            auto* rowPB = makeSliderRow(speexPriorBase, lblPriorBase, 5, 50, 10, 60);
            speexPriorBase->setToolTip(tr(
                "Prior SNR base weight (0.05–0.50, default 0.10).\n"
                "Minimum weight given to the current frame's observation.\n"
                "Higher = less inertia from previous frames (faster tracking).\n"
                "Too high may cause instability or musical noise."));
            lblPriorBase->setText("0.10");
            form->addRow(tr("  Prior base:"), rowPB);
        }

        speexGrp->setVisible(false);  // hidden by default (None selected)
        mainLayout->addWidget(speexGrp);
        connect(speexSuppress,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcCheck,     &QCheckBox::toggled,    this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcLevel,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexAgcMaxGain,   &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadCheck,     &QCheckBox::toggled,    this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadProbStart, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexVadProbCont,  &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexSnrDecay,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexNoiseUpdate,  &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(speexPriorBase,    &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── ANR controls — stack page 2 ───────────────────────────────────────────
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
            // Stored as 0–100 integer (×0.1 → 0.0–10.0)
            auto* row = makeSliderRow(anrSensSlider, lblAnrSens, 0, 100, 11, 70);
            anrSensSlider->setToolTip(tr(
                "Sensitivity: −log₁₀ of the probability that noise exceeds the threshold.\n"
                "Higher = less aggressive (fewer false positives, may miss noise).\n"
                "Lower  = more aggressive (may suppress signal as well as noise).\n"
                "Strategy 1: Tune to static and lower until you hear chimes, then raise slightly.\n"
                "Strategy 2: Tune to a signal and start low. Raise until the signal is nominally loud (watch the RxAudio meter)\n"
                "Default 1.1 is a good starting point."));
            lblAnrSens->setText("1.1");   // slider 11 × 0.1
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

            // Static noise profile spectrum display
            anrProfileSpec = new SpectrumWidget;
            anrProfileSpec->logBins       = false;
            anrProfileSpec->showSecondary = false;
            anrProfileSpec->primaryLabel  = tr("Noise Floor");
            anrProfileSpec->setMinimumHeight(144);
            anrProfileSpec->setMaximumHeight(144);
            anrProfileSpec->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            anrProfileSpec->setStaticMode();  // no timer; repaint only when data arrives
            anrProfileSpec->setVisible(false);  // shown after profile is collected
            form->addRow(anrProfileSpec);

            // 1-second tick timer for countdown during collection
            anrCollectTimer = new QTimer(this);
            anrCollectTimer->setInterval(1000);
            connect(anrCollectTimer, &QTimer::timeout,
                    this, &RxAudioProcessingWidget::onAnrCollectTimeout);
            connect(anrCollectBtn, &QPushButton::clicked,
                    this, &RxAudioProcessingWidget::onAnrCollectClicked);
        }

        anrGrp->setVisible(false);  // hidden by default (None selected)
        mainLayout->addWidget(anrGrp);

        connect(anrNoiseRedSlider, &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(anrSensSlider,     &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
        connect(anrSmoothSlider,   &QSlider::valueChanged, this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    // ── RX Equalizer ────────────────────────────────────────────────────────
    {
        eqGrp = new QGroupBox(tr("Receive Equalizer"));
        auto* eqOuter = new QVBoxLayout(eqGrp);

        // Enable checkbox (always visible)
        eqEnableCheck = new QCheckBox(tr("Enable EQ"));
        eqEnableCheck->setToolTip(tr("Enable the 4-band receive equalizer."));
        eqOuter->addWidget(eqEnableCheck);

        // Container for EQ controls (hidden when EQ is disabled)
        eqBandsWidget = new QWidget;
        auto* eqBandsLayout = new QVBoxLayout(eqBandsWidget);
        eqBandsLayout->setContentsMargins(0, 0, 0, 0);
        {
            auto* clearRow = new QHBoxLayout;
            eqClearBtn = new QPushButton(tr("Clear"));
            eqClearBtn->setToolTip(tr("Reset all EQ band gains to 0 dB (flat)."));
            clearRow->addStretch();
            clearRow->addWidget(eqClearBtn);
            eqBandsLayout->addLayout(clearRow);
        }

        // Band names, frequency ranges, and dial ranges
        struct BandDef {
            const char* name;
            int freqMin, freqMax, freqDefault;
        };
        static const BandDef bandDefs[RX_EQ_BANDS] = {
            { "Low Shelf",    50,  250,  100 },
            { "Low Mid",     300, 1500,  800 },
            { "High Mid",   1000, 3000, 1200 },
            { "High Shelf", 1200, 4500, 2200 },
        };

        // Band columns: title, gain label, vertical slider, freq dial, freq label
        auto* bandsRow = new QHBoxLayout;
        bandsRow->setSpacing(2);
        for (int i = 0; i < RX_EQ_BANDS; ++i) {
            auto* col = new QVBoxLayout;
            col->setAlignment(Qt::AlignHCenter);

            // Band title
            eqBandTitle[i] = new QLabel(tr(bandDefs[i].name));
            eqBandTitle[i]->setAlignment(Qt::AlignCenter);
            QFont bf = eqBandTitle[i]->font();
            bf.setBold(true);
            eqBandTitle[i]->setFont(bf);
            col->addWidget(eqBandTitle[i]);

            // Gain label (on top of slider)
            eqGainLabel[i] = new QLabel("0.0 dB");
            eqGainLabel[i]->setAlignment(Qt::AlignCenter);
            eqGainLabel[i]->setMinimumWidth(50);
            col->addWidget(eqGainLabel[i]);

            // Vertical gain slider: -90..+90 (×0.1 → -9..+9 dB)
            eqGainSlider[i] = new QSlider(Qt::Vertical);
            eqGainSlider[i]->setRange(-90, 90);
            eqGainSlider[i]->setValue(0);
            eqGainSlider[i]->setTickPosition(QSlider::TicksBothSides);
            eqGainSlider[i]->setTickInterval(10);
            eqGainSlider[i]->setMinimumHeight(100);
            eqGainSlider[i]->setToolTip(tr("%1: adjust gain from -9 to +9 dB").arg(bandDefs[i].name));
            col->addWidget(eqGainSlider[i], 1, Qt::AlignHCenter);

            // Frequency dial (knob)
            eqFreqDial[i] = new QDial;
            eqFreqDial[i]->setRange(bandDefs[i].freqMin, bandDefs[i].freqMax);
            eqFreqDial[i]->setValue(bandDefs[i].freqDefault);
            eqFreqDial[i]->setWrapping(false);
            eqFreqDial[i]->setNotchesVisible(true);
            eqFreqDial[i]->setFixedSize(35, 35);
            eqFreqDial[i]->setToolTip(tr("%1: adjust frequency for this band (%2–%3 Hz)")
                .arg(bandDefs[i].name)
                .arg(bandDefs[i].freqMin)
                .arg(bandDefs[i].freqMax));
            col->addWidget(eqFreqDial[i], 0, Qt::AlignHCenter);

            // Frequency label (below dial)
            int defFreq = bandDefs[i].freqDefault;
            if (defFreq >= 1000)
                eqFreqLabel[i] = new QLabel(QString::number(defFreq * 0.001, 'f', 1) + " kHz");
            else
                eqFreqLabel[i] = new QLabel(QString::number(defFreq) + " Hz");
            eqFreqLabel[i]->setAlignment(Qt::AlignCenter);
            eqFreqLabel[i]->setMinimumWidth(50);
            col->addWidget(eqFreqLabel[i]);

            bandsRow->addLayout(col);
        }
        eqBandsLayout->addLayout(bandsRow);
        eqBandsWidget->setVisible(false);  // hidden by default (EQ disabled)
        eqOuter->addWidget(eqBandsWidget);
        mainLayout->addWidget(eqGrp);

        // Connect signals
        connect(eqEnableCheck, &QCheckBox::toggled,
                this, &RxAudioProcessingWidget::onAnyControlChanged);
        for (int i = 0; i < RX_EQ_BANDS; ++i) {
            connect(eqGainSlider[i], &QSlider::valueChanged,
                    this, &RxAudioProcessingWidget::onAnyControlChanged);
            connect(eqFreqDial[i], &QDial::valueChanged,
                    this, &RxAudioProcessingWidget::onAnyControlChanged);
        }
        connect(eqClearBtn, &QPushButton::clicked, this, [this]() {
            for (int i = 0; i < RX_EQ_BANDS; ++i)
                eqGainSlider[i]->setValue(0);
            // Slider valueChanged signals will trigger onAnyControlChanged
        });
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

    // ── RX Spectrum ───────────────────────────────────────────────────────────
    {
        specGrp = new QGroupBox(tr("RX Spectrum"));
        auto* vbox = new QVBoxLayout(specGrp);

        specEnable = new QCheckBox(tr("Enable spectrum display"));
        specEnable->setToolTip(tr("Show RX audio spectrum (input: green = radio signal before NR, "
                                  "output: orange = after noise reduction and gain)."));
        vbox->addWidget(specEnable);

        specInhibitDuringTx = new QCheckBox(tr("Inhibit during transmit"));
        specInhibitDuringTx->setToolTip(tr(
            "Pause the RX spectrum display while the radio is transmitting."));
        specInhibitDuringTx->setChecked(true);
        vbox->addWidget(specInhibitDuringTx);

        specWidget = new SpectrumWidget;
        specWidget->logBins    = true;
        specWidget->fftLength  = RxAudioProcessor::SPEC_FFT_LEN;
        specWidget->sampleRate = 8000.0;
        specWidget->showSecondary = true;
        specWidget->setMinimumHeight(120);
        specWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        specWidget->setVisible(false);
        vbox->addWidget(specWidget, 1);  // stretch factor so spectrum takes extra space

        mainLayout->addWidget(specGrp, 1);  // stretch factor for the group too

        connect(specEnable, &QCheckBox::toggled,
                this, &RxAudioProcessingWidget::onSpecEnableToggled);
        connect(specInhibitDuringTx, &QCheckBox::toggled,
                this, &RxAudioProcessingWidget::onAnyControlChanged);
    }

    scroll->setWidget(inner);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    // Start disabled — enabled when radio connects
    controlsContainer->setEnabled(false);

    // Minimum width; height adapts to visible content.
    setMinimumWidth(500);
    updateSizeConstraints();
}

// ─── blockAll ─────────────────────────────────────────────────────────────────

void RxAudioProcessingWidget::blockAll(bool block)
{
    bypassCheck->blockSignals(block);
    channelCombo->blockSignals(block);
    algoNone->blockSignals(block);
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
    speexSnrDecay->blockSignals(block);
    speexNoiseUpdate->blockSignals(block);
    speexPriorBase->blockSignals(block);
    anrNoiseRedSlider->blockSignals(block);
    anrSensSlider->blockSignals(block);
    anrSmoothSlider->blockSignals(block);
    eqEnableCheck->blockSignals(block);
    for (int i = 0; i < RX_EQ_BANDS; ++i) {
        eqGainSlider[i]->blockSignals(block);
        eqFreqDial[i]->blockSignals(block);
    }
    outputGain->blockSignals(block);
    specEnable->blockSignals(block);
    specInhibitDuringTx->blockSignals(block);
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
    const int modeId = static_cast<int>(p.nrMode);  // 0=None, 1=Speex, 2=ANR
    algoNone->setChecked(p.nrMode == RxNrMode::None);
    algoSpeex->setChecked(p.nrMode == RxNrMode::Speex);
    algoAnr->setChecked(p.nrMode == RxNrMode::Anr);
    nonePage->setVisible(modeId == 0);
    speexGrp->setVisible(modeId == 1);
    anrGrp->setVisible(modeId == 2);

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

    // Adaptation speed
    speexSnrDecay->setValue(qBound(0, qRound(p.speexSnrDecay * 100.0f), 95));
    speexNoiseUpdate->setValue(qBound(1, qRound(p.speexNoiseUpdateRate * 100.0f), 50));
    speexPriorBase->setValue(qBound(5, qRound(p.speexPriorBase * 100.0f), 50));
    lblSnrDecay->setText(QString::number(p.speexSnrDecay, 'f', 2));
    lblNoiseUpdate->setText(QString::number(p.speexNoiseUpdateRate, 'f', 2));
    lblPriorBase->setText(QString::number(p.speexPriorBase, 'f', 2));

    // ANR
    anrNoiseRedSlider->setValue(qBound(0, static_cast<int>(p.anrNoiseReductionDb), 48));
    lblAnrNoiseRed->setText(QString::number(anrNoiseRedSlider->value()) + " dB");
    // Sensitivity stored as 0–100 integer (×0.1 → 0.0..10.0)
    anrSensSlider->setValue(qBound(0, qRound(p.anrSensitivity * 10.0), 100));
    lblAnrSens->setText(QString::number(anrSensSlider->value() * 0.1, 'f', 1));
    anrSmoothSlider->setValue(qBound(0, p.anrFreqSmoothing, 6));
    lblAnrSmooth->setText(QString::number(anrSmoothSlider->value()));
    updateAnrControlState();

    // EQ
    eqEnableCheck->setChecked(p.eqEnabled);
    eqBandsWidget->setVisible(p.eqEnabled);
    for (int i = 0; i < RX_EQ_BANDS; ++i) {
        eqGainSlider[i]->setValue(qBound(-90, qRound(p.eqGain[i] * 10.0f), 90));
        eqFreqDial[i]->setValue(qRound(p.eqFreq[i]));
        eqGainLabel[i]->setText(QString::number(p.eqGain[i], 'f', 1) + " dB");
        int freq = qRound(p.eqFreq[i]);
        if (freq >= 1000)
            eqFreqLabel[i]->setText(QString::number(freq * 0.001, 'f', 1) + " kHz");
        else
            eqFreqLabel[i]->setText(QString::number(freq) + " Hz");
    }

    // Output gain: -6..+20 dB in 0.1 dB steps → integer range -60..200
    outputGain->setValue(qBound(-60, qRound(p.outputGainDB * 10.0f), 200));
    lblOutputGain->setText(QString::number(p.outputGainDB, 'f', 1) + " dB");

    // Spectrum
    specEnable->setChecked(p.spectrumEnabled);
    specInhibitDuringTx->setChecked(p.specInhibitDuringTx);
    specWidget->setVisible(p.spectrumEnabled);
    m_spectrumFps = qBound(1, p.spectrumFPS, 60);
    specWidget->setFps(m_spectrumFps);

    // Defer size calculation so Qt processes all the visibility/layout changes first.
    QTimer::singleShot(0, this, &RxAudioProcessingWidget::updateSizeConstraints);
}

// ─── Spectrum slots ───────────────────────────────────────────────────────────

void RxAudioProcessingWidget::setTransmitting(bool transmitting)
{
    m_isTransmitting = transmitting;
    // When entering the inhibited state (radio starts transmitting), clear
    // stale spectrum data so the display goes blank rather than freezing.
    if (m_isTransmitting && specInhibitDuringTx && specInhibitDuringTx->isChecked()) {
        if (specWidget) {
            specWidget->spectrumPrimary.clear();
            specWidget->spectrumSecondary.clear();
        }
    }
}

void RxAudioProcessingWidget::onSpecEnableToggled(bool enabled)
{
    specWidget->setVisible(enabled);
    if (!enabled) {
        specWidget->spectrumPrimary.clear();
        specWidget->spectrumSecondary.clear();
    }
    updateSizeConstraints();
    emit prefsChanged(getPrefs());
}

void RxAudioProcessingWidget::onSpectrumBins(QVector<double> inBins,
                                             QVector<double> outBins,
                                             float rawSR)
{
    if (!specEnable || !specEnable->isChecked()) return;
    // Inhibit during transmit: drop bins while transmitting.
    if (m_isTransmitting && specInhibitDuringTx && specInhibitDuringTx->isChecked()) return;
    ++m_batchCount;

    if (rawSR != static_cast<float>(specWidget->sampleRate)) {
        // Effective sample rate after decimation to ~16 kHz.
        const int K = qMax(1, static_cast<int>(std::round(rawSR / 16000.0f)));
        specWidget->sampleRate = static_cast<double>(rawSR) / K;
        specWidget->fftLength  = RxAudioProcessor::SPEC_FFT_LEN;
    }

    specWidget->spectrumPrimary.assign(inBins.cbegin(),   inBins.cend());
    specWidget->spectrumSecondary.assign(outBins.cbegin(), outBins.cend());
}

void RxAudioProcessingWidget::onSpecDiagTimer()
{
    if (!specEnable || !specEnable->isChecked()) {
        m_batchCount = 0;
        return;
    }
    m_batchCount = 0;
}

// ─── ANR profile slots ────────────────────────────────────────────────────────

void RxAudioProcessingWidget::onAnrCollectClicked()
{
    if (m_anrCollecting) {
        // User pressed "Stop Collecting" early
        anrCollectTimer->stop();
        m_anrCollecting = false;
        anrCollectBtn->setText(m_anrHasProfile ? tr("Collect New Noise Sample")
                                               : tr("Collect Noise Sample"));
        lblAnrStatus->setText(tr("Collecting stopped — waiting for profile to build…"));
        anrCollectBtn->setEnabled(false);
        emit anrCollectToggled(false);   // triggers wfmain → rxProc->stopAnrProfile()
    } else {
        // User pressed "Collect Noise Sample"
        m_anrCollecting = true;
        m_anrCountdown  = 5;
        anrCollectBtn->setText(tr("Stop Collecting… (auto-stop in %1 s)").arg(m_anrCountdown));
        lblAnrStatus->setText(tr("Recording noise sample — play audio with only background noise…"));
        anrCollectTimer->start();        // 1-second ticks
        emit anrCollectToggled(true);    // triggers wfmain → rxProc->startAnrProfile()
    }
}

void RxAudioProcessingWidget::onAnrCollectTimeout()
{
    // 1-second tick during collection
    if (!m_anrCollecting) return;

    --m_anrCountdown;
    if (m_anrCountdown > 0) {
        // Update countdown text
        anrCollectBtn->setText(tr("Stop Collecting… (auto-stop in %1 s)").arg(m_anrCountdown));
        return;
    }

    // Countdown reached zero — auto-stop
    anrCollectTimer->stop();
    m_anrCollecting = false;
    anrCollectBtn->setText(m_anrHasProfile ? tr("Collect New Noise Sample")
                                           : tr("Collect Noise Sample"));
    lblAnrStatus->setText(tr("5 s sample collected — building noise profile…"));
    anrCollectBtn->setEnabled(false);
    emit anrCollectToggled(false);  // triggers wfmain → rxProc->stopAnrProfile()
}

void RxAudioProcessingWidget::onAnrProfileReady(bool success)
{
    anrCollectBtn->setEnabled(m_radioConnected);
    if (success) {
        m_anrHasProfile = true;
        anrCollectBtn->setText(tr("Collect New Noise Sample"));
        lblAnrStatus->setText(tr("Noise profile ready — ANR active."));
    } else {
        m_anrHasProfile = false;
        anrCollectBtn->setText(tr("Collect Noise Sample"));
        lblAnrStatus->setText(tr("Profile build failed (sample too short?).  Try again."));
    }
    updateAnrControlState();
}

void RxAudioProcessingWidget::onAnrNoiseProfileBins(QVector<double> bins,
                                                     double sampleRate,
                                                     int windowSize)
{
    if (bins.isEmpty() || !anrProfileSpec) return;

    // Auto-scale: find the range of the dB values
    double lo = bins[0], hi = bins[0];
    for (int i = 1; i < bins.size(); ++i) {
        if (bins[i] < lo) lo = bins[i];
        if (bins[i] > hi) hi = bins[i];
    }
    // Pad the range and round to nice grid boundaries
    double range = hi - lo;
    if (range < 6.0) range = 6.0;
    double margin = range * 0.1;
    lo = std::floor((lo - margin) / 6.0) * 6.0;
    hi = std::ceil((hi + margin) / 6.0) * 6.0;

    anrProfileSpec->fftLength  = windowSize;
    anrProfileSpec->sampleRate = sampleRate;
    anrProfileSpec->minDb      = lo;
    anrProfileSpec->maxDb      = hi;
    anrProfileSpec->spectrumPrimary.assign(bins.cbegin(), bins.cend());
    anrProfileSpec->spectrumSecondary.clear();
    anrProfileSpec->setVisible(true);
    anrProfileSpec->update();   // trigger a single repaint (static mode)
    updateSizeConstraints();
}

// ─── updateAnrControlState ────────────────────────────────────────────────────

void RxAudioProcessingWidget::updateAnrControlState()
{
    // Parameter sliders are always interactable when ANR is selected so the
    // user can pre-configure them before collecting a noise sample.
    // Only the status label reflects whether a profile is ready.
    if (!m_anrHasProfile && !m_anrCollecting)
        lblAnrStatus->setText(tr("No noise sample — collect one before using ANR."));
}

// ─── updateSizeConstraints ───────────────────────────────────────────────────

void RxAudioProcessingWidget::updateSizeConstraints()
{
    // Let the inner layout settle, then compute the ideal height for visible content.
    controlsContainer->adjustSize();

    // The inner widget's sizeHint is the natural height of all visible controls.
    const int contentH = controlsContainer->sizeHint().height();
    // Add a small margin for the outer layout / frame.
    const int minH = contentH + layout()->contentsMargins().top()
                               + layout()->contentsMargins().bottom() + 2;

    setMinimumHeight(minH);

    if (specEnable->isChecked()) {
        // Spectrum visible: allow vertical expansion beyond minimum
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else {
        // No spectrum: lock height to exactly the content size
        setMaximumHeight(minH);
    }
}
