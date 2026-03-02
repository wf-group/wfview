// AudioProcessingWidget — TX audio processing settings dialog

#include "audioprocessingwidget.h"
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

AudioProcessingWidget::AudioProcessingWidget(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("TX Audio Processing"));
    setWindowFlags(windowFlags() | Qt::Window);
    buildUi();
}

// ─── setPrefs / getPrefs ─────────────────────────────────────────────────────

void AudioProcessingWidget::setPrefs(const audioProcessingPrefs& p)
{
    blockAll(true);
    populateFromPrefs(p);
    blockAll(false);
}

audioProcessingPrefs AudioProcessingWidget::getPrefs() const
{
    audioProcessingPrefs p;

    p.bypass      = bypassCheck->isChecked();
    p.eqEnabled   = eqEnable->isChecked();
    p.compEnabled = compEnable->isChecked();
    p.eqFirst     = (orderCombo->currentIndex() == 0); // 0=EQ→Comp, 1=Comp→EQ

    for (int i = 0; i < EQ_BANDS; ++i)
        p.eqBands[i] = eqSliders[i]->value() * 0.1f;  // slider is ×10

    p.compPeakLimit = compPeakLimit->value() * 0.1f;
    p.compRelease   = compRelease->value()   * 0.01f;
    p.compFastRatio = compFastRatio->value() * 0.01f;
    p.compSlowRatio = compSlowRatio->value() * 0.01f;

    p.inputGainDB   = inputGain->value()  * 0.1f;
    p.outputGainDB  = outputGain->value() * 0.1f;

    p.sidetoneEnabled = sidetoneEnable->isChecked();
    p.sidetoneLevel   = sidetoneLevel->value() * 0.01f;
    p.muteRx          = muteRxCheck->isChecked();

    return p;
}

// ─── Level meter updates ──────────────────────────────────────────────────────

void AudioProcessingWidget::updateInputLevel(float peak)
{
    inputMeter->setLevel(static_cast<double>(peak) * 255.0);
}

void AudioProcessingWidget::updateOutputLevel(float peak)
{
    outputMeter->setLevel(static_cast<double>(peak) * 255.0);
}

void AudioProcessingWidget::updateGainReduction(float linear)
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

void AudioProcessingWidget::onAnyControlChanged()
{
    // Update value labels
    for (int i = 0; i < EQ_BANDS; ++i) {
        float db = eqSliders[i]->value() * 0.1f;
        eqValues[i]->setText(QString::number(db, 'f', 1));
    }
    lblPeak->setText(QString::number(compPeakLimit->value() * 0.1f, 'f', 1) + " dB");
    lblRelease->setText(QString::number(compRelease->value() * 0.01f, 'f', 2) + " s");
    lblFast->setText(QString::number(compFastRatio->value() * 0.01f, 'f', 2));
    lblSlow->setText(QString::number(compSlowRatio->value() * 0.01f, 'f', 2));
    lblInputGain->setText(QString::number(inputGain->value() * 0.1f, 'f', 1) + " dB");
    lblOutputGain->setText(QString::number(outputGain->value() * 0.1f, 'f', 1) + " dB");
    lblSidetone->setText(QString::number(sidetoneLevel->value()) + "%");

    emit prefsChanged(getPrefs());
}

// ─── Bypass / Clear EQ slots ──────────────────────────────────────────────────

void AudioProcessingWidget::onBypassToggled(bool bypassed)
{
    setProcessingControlsEnabled(!bypassed);
    emit prefsChanged(getPrefs());
}

void AudioProcessingWidget::onClearEq()
{
    blockAll(true);
    for (int i = 0; i < EQ_BANDS; ++i) {
        eqSliders[i]->setValue(0);
        eqValues[i]->setText("0.0");
    }
    blockAll(false);
    emit prefsChanged(getPrefs());
}

void AudioProcessingWidget::setProcessingControlsEnabled(bool enabled)
{
    orderRow->setEnabled(enabled);
    gainGrp->setEnabled(enabled);
    eqGrp->setEnabled(enabled);
    compGrp->setEnabled(enabled);
    sidetoneGrp->setEnabled(enabled);
}

// ─── buildUi ─────────────────────────────────────────────────────────────────

void AudioProcessingWidget::buildUi()
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
                this, &AudioProcessingWidget::onBypassToggled);
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
        mainLayout->addWidget(grp);
    }

    // ── Multiband EQ ─────────────────────────────────────────────────────────
    {
        eqGrp = new QGroupBox(tr("Multiband EQ"));
        auto* grp = eqGrp;
        auto* vbox = new QVBoxLayout(grp);

        auto* eqEnableRow = new QHBoxLayout;
        eqEnable = new QCheckBox(tr("Enable EQ"));
        clearEqBtn = new QPushButton(tr("Clear"));
        clearEqBtn->setToolTip(tr("Reset all EQ bands to 0 dB"));
        eqEnableRow->addWidget(eqEnable);
        eqEnableRow->addWidget(clearEqBtn);
        eqEnableRow->addStretch();
        vbox->addLayout(eqEnableRow);

        auto* sliderRow = new QHBoxLayout;
        for (int i = 0; i < EQ_BANDS; ++i) {
            auto* col = new QVBoxLayout;

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

            sliderRow->addLayout(col);
            connect(eqSliders[i], &QSlider::valueChanged,
                    this, &AudioProcessingWidget::onAnyControlChanged);
        }
        vbox->addLayout(sliderRow);
        mainLayout->addWidget(grp);
    }

    // ── Compressor ──────────────────────────────────────────────────────────
    {
        compGrp = new QGroupBox(tr("Compressor (Dyson)"));
        auto* grp = compGrp;
        auto* form = new QFormLayout(grp);

        compEnable = new QCheckBox(tr("Enable compressor"));
        form->addRow(compEnable);

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

        // Fast ratio: 0.0..1.0 → stored ×100
        compFastRatio = makeSlider(0, 100, 50);
        lblFast       = new QLabel("0.50");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(compFastRatio);
            row->addWidget(lblFast);
            form->addRow(tr("Fast ratio:"), row);
        }

        // Slow ratio: 0.0..1.0 → stored ×100
        compSlowRatio = makeSlider(0, 100, 30);
        lblSlow       = new QLabel("0.30");
        {
            auto* row = new QHBoxLayout;
            row->addWidget(compSlowRatio);
            row->addWidget(lblSlow);
            form->addRow(tr("Slow ratio:"), row);
        }

        mainLayout->addWidget(grp);
    }

    // ── Sidetone ────────────────────────────────────────────────────────────
    {
        sidetoneGrp = new QGroupBox(tr("Self-monitor"));
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
    connect(clearEqBtn,    &QPushButton::clicked, this, &AudioProcessingWidget::onClearEq);
    connect(muteRxCheck,   &QCheckBox::toggled, this, &AudioProcessingWidget::onAnyControlChanged);
    connect(eqEnable,      &QCheckBox::toggled, this, &AudioProcessingWidget::onAnyControlChanged);
    connect(compEnable,    &QCheckBox::toggled, this, &AudioProcessingWidget::onAnyControlChanged);
    connect(orderCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(sidetoneEnable,&QCheckBox::toggled, this, &AudioProcessingWidget::onAnyControlChanged);
    connect(sidetoneLevel, &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(inputGain,     &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(outputGain,    &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(compPeakLimit, &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(compRelease,   &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(compFastRatio, &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);
    connect(compSlowRatio, &QSlider::valueChanged,
            this, &AudioProcessingWidget::onAnyControlChanged);

    adjustSize();
}

// ─── Private helpers ─────────────────────────────────────────────────────────

void AudioProcessingWidget::blockAll(bool block)
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
}

void AudioProcessingWidget::populateFromPrefs(const audioProcessingPrefs& p)
{
    bypassCheck->setChecked(p.bypass);
    setProcessingControlsEnabled(!p.bypass);

    eqEnable->setChecked(p.eqEnabled);
    compEnable->setChecked(p.compEnabled);
    orderCombo->setCurrentIndex(p.eqFirst ? 0 : 1);

    for (int i = 0; i < EQ_BANDS; ++i) {
        eqSliders[i]->setValue(static_cast<int>(std::round(p.eqBands[i] * 10.0f)));
        eqValues[i]->setText(QString::number(p.eqBands[i], 'f', 1));
    }

    compPeakLimit->setValue(static_cast<int>(std::round(p.compPeakLimit * 10.0f)));
    compRelease->setValue(static_cast<int>(std::round(p.compRelease * 100.0f)));
    compFastRatio->setValue(static_cast<int>(std::round(p.compFastRatio * 100.0f)));
    compSlowRatio->setValue(static_cast<int>(std::round(p.compSlowRatio * 100.0f)));

    inputGain->setValue(static_cast<int>(std::round(p.inputGainDB * 10.0f)));
    outputGain->setValue(static_cast<int>(std::round(p.outputGainDB * 10.0f)));

    sidetoneEnable->setChecked(p.sidetoneEnabled);
    sidetoneLevel->setValue(static_cast<int>(std::round(p.sidetoneLevel * 100.0f)));
    muteRxCheck->setChecked(p.muteRx);

    // Update labels
    lblPeak->setText(QString::number(p.compPeakLimit, 'f', 1) + " dB");
    lblRelease->setText(QString::number(p.compRelease, 'f', 2) + " s");
    lblFast->setText(QString::number(p.compFastRatio, 'f', 2));
    lblSlow->setText(QString::number(p.compSlowRatio, 'f', 2));
    lblInputGain->setText(QString::number(p.inputGainDB, 'f', 1) + " dB");
    lblOutputGain->setText(QString::number(p.outputGainDB, 'f', 1) + " dB");
    lblSidetone->setText(QString::number(static_cast<int>(p.sidetoneLevel * 100.0f)) + "%");
}
