// AudioProcessingWidget — TX audio processing settings dialog

#include "audioprocessingwidget.h"
#include "logcategories.h"
#include <cmath>
#include <complex>

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
    connect(&m_specDiagTimer, &QTimer::timeout,
            this, &AudioProcessingWidget::onSpecDiagTimer);
    m_specDiagTimer.setInterval(1000);
    m_specDiagTimer.start();
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

    p.spectrumEnabled = specEnable->isChecked();
    p.spectrumFPS     = m_spectrumFps;

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

void AudioProcessingWidget::onSpecEnableToggled(bool enabled)
{
    specWidget->setVisible(enabled);
    if (!enabled) {
        // Clear stale data so the widget doesn't show an old trace on re-enable.
        specWidget->spectrumPrimary.clear();
        specWidget->spectrumSecondary.clear();
        m_specInRing.clear();
        m_specOutRing.clear();
        m_specRingPos     = 0;
        m_specFftTrigger  = 0;
        m_specSampleRate  = 0.0f;
    }
    adjustSize();
    emit prefsChanged(getPrefs());
}

void AudioProcessingWidget::setConnected(bool connected)
{
    m_radioConnected = connected;
    if (!connected) {
        // Clear ring buffers and spectrum traces so stale data isn't shown
        // when the widget is later re-opened after a reconnect.
        specWidget->spectrumPrimary.clear();
        specWidget->spectrumSecondary.clear();
        m_specInRing.clear();
        m_specOutRing.clear();
        m_specRingPos     = 0;
        m_specFftTrigger  = 0;
        m_specSampleRate  = 0.0f;
        m_audioSampleRate = 0.0f;
    }
}

// ─── onSpectrumSamples ────────────────────────────────────────────────────────
// Received from TxAudioProcessor (~10 Hz) via queued connection.
// Decimates the input to an ~16 kHz effective rate before feeding the sliding
// DFTs, keeping computation low (~128 output bins, 10 Hz refresh).

void AudioProcessingWidget::onSpectrumSamples(QVector<float> inSamples,
                                               QVector<float> outSamples,
                                               float sampleRate)
{
    if (!isVisible()) return;
    if (!m_radioConnected) return;
    if (!specEnable->isChecked()) return;

    // Count how many batches actually reach the processing code.
    // onSpecDiagTimer() reads this every second to distinguish "no audio
    // arriving" from "FFT trigger math is wrong".
    ++m_batchCount;

    // Decimation factor K: take every Kth sample to target ~16 kHz effective SR.
    const int K = qMax(1, static_cast<int>(std::round(sampleRate / 16000.0f)));

    // Update EQ band visibility the first time we learn the sample rate
    // and whenever it changes (Nyquist determines which bands are relevant).
    if (sampleRate != m_audioSampleRate) {
        m_audioSampleRate = sampleRate;
        updateEqBandVisibility(sampleRate);
    }

    // (Re)initialise on sample-rate change or first call.
    if (sampleRate != m_specSampleRate || m_specInRing.empty()) {
        m_specSampleRate  = sampleRate;
        m_specDecimFactor = K;
        m_specDecimCount  = 0;
        m_specRingPos     = 0;
        m_specFftTrigger  = 0;
        m_specInRing.assign (SPEC_FFT_LEN, 0.0f);
        m_specOutRing.assign(SPEC_FFT_LEN, 0.0f);

        // Precompute Hanning window coefficients.
        m_specWindow.resize(SPEC_FFT_LEN);
        for (int i = 0; i < SPEC_FFT_LEN; ++i)
            m_specWindow[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i
                                                       / (SPEC_FFT_LEN - 1));

        specWidget->sampleRate = static_cast<double>(sampleRate) / K;
        specWidget->fftLength  = SPEC_FFT_LEN;
    }

    // ── FFT scratch buffers (allocated once per batch, reused per trigger) ────
    // Normaliser: Hanning coherent gain = 0.5, unnormalized Eigen FFT →
    // full-scale sine gives |bin[k]| = N/4.
    static constexpr int    N     = SPEC_FFT_LEN;
    static constexpr int    halfN = N / 2;
    static constexpr double kNorm = N / 4.0;
    std::vector<float>               timeBuf(N);
    std::vector<std::complex<float>> freqBins(N);

    auto runFFT = [&](const std::vector<float>& ring, std::vector<double>& out) {
        for (int i = 0; i < N; ++i)
            timeBuf[i] = m_specWindow[i] * ring[(m_specRingPos + i) % N];
        m_fft.fwd(freqBins, timeBuf);
        out.resize(halfN);
        for (int k = 0; k < halfN; ++k)
            out[k] = 20.0 * std::log10(std::abs(freqBins[k]) / kNorm + 1e-10);
    };

    // ── How often to fire the FFT to meet the target frame rate ──────────────
    // decimSampleRate ≈ sampleRate / K ≈ 16 kHz.
    // triggerEvery = decimSampleRate / targetFps — e.g. at 48 kHz, K=3, 30 fps:
    //   16000 / 30 ≈ 533 decimated samples per FFT → 3 FFTs per 100 ms batch.
    // At 10 fps (default): 1600 — one FFT per batch, same as before.
    // At <10 fps: counter accumulates across batches (member persists).
    const float decimSampleRate = static_cast<float>(sampleRate) / m_specDecimFactor;
    const int   triggerEvery    = qMax(1, static_cast<int>(
                                    std::round(decimSampleRate / m_spectrumFps)));
    m_lastTriggerEvery = triggerEvery;

    // ── Fill ring buffers; trigger FFT every triggerEvery decimated samples ──
    const int n = inSamples.size();
    m_lastBatchSize = n;
    for (int i = 0; i < n; ++i) {
        if (++m_specDecimCount >= m_specDecimFactor) {
            m_specDecimCount = 0;
            m_specInRing [m_specRingPos] = inSamples [i];
            m_specOutRing[m_specRingPos] = outSamples[i];
            m_specRingPos = (m_specRingPos + 1) % SPEC_FFT_LEN;

            if (++m_specFftTrigger >= triggerEvery) {
                m_specFftTrigger = 0;
                m_dftCallTimer.start();
                runFFT(m_specInRing,  specWidget->spectrumPrimary);
                runFFT(m_specOutRing, specWidget->spectrumSecondary);
                m_dftTotalNs += m_dftCallTimer.nsecsElapsed();
                ++m_dftCallCount;
            }
        }
    }

}

void AudioProcessingWidget::updateEqBandVisibility(float sampleRate)
{
    if (sampleRate <= 0.0f) return;
    const float nyquist = sampleRate * 0.5f;
    for (int i = 0; i < EQ_BANDS; ++i)
        eqColumns[i]->setVisible(kBandFreqs[i] <= nyquist);
}

// ─── onSpecDiagTimer ──────────────────────────────────────────────────────────
// Fires every second from m_specDiagTimer regardless of whether audio is
// arriving.  This guarantees a log line appears even when onSpectrumSamples
// is never called (signal not connected, guard returning early, etc.).
//
// FPS NOT MET logic:
//   The achievable FFT rate = batches/s × floor(decimSamples/triggerEvery).
//   We warn only when batches > 0 (TX audio IS flowing) but actual FFTs fall
//   below 80% of that achievable rate — meaning the trigger math is wrong or
//   the processing is too slow.  We do NOT warn when batches = 0 because that
//   just means the user is not transmitting.

void AudioProcessingWidget::onSpecDiagTimer()
{
    if (!specEnable || !specEnable->isChecked()) {
        // Spectrum disabled — reset counters silently and return.
        m_dftCallCount = 0;
        m_dftTotalNs   = 0;
        m_batchCount   = 0;
        return;
    }

    // Achievable FFT pairs per second given the actual audio delivery rate.
    // decimSamplesPerBatch ≈ lastBatchSize / triggerEvery batches of decimated samples.
    const int decimPerBatch     = (m_specDecimFactor > 0)
                                      ? m_lastBatchSize / m_specDecimFactor : 0;
    const int fftsPerBatch      = (m_lastTriggerEvery > 0)
                                      ? qMax(1, decimPerBatch / m_lastTriggerEvery) : 1;
    const int achievableFfts    = m_batchCount * fftsPerBatch;

    // FPS NOT MET: audio IS arriving but FFTs are below 80% of achievable.
    const bool audioArriving    = (m_batchCount > 0);
    const bool fpsMiss          = audioArriving &&
                                  (m_dftCallCount < achievableFfts * 8 / 10);

    // Visibility/connection diagnostics shown when no audio arrives.
    QString noDataReason;
    if (!audioArriving) {
        if (!isVisible())             noDataReason = "widget hidden";
        else if (!m_radioConnected)   noDataReason = "radio not connected";
        else                          noDataReason = "no TX audio (not transmitting?)";
    }

    qCDebug(logAudio) << "[SpectrumFFT]"
                      << m_batchCount   << "batches/s,"
                      << m_dftCallCount << "ffts/s"
                      << "(target" << m_spectrumFps << "fps,"
                      << "achievable" << achievableFfts << ")"
                      << (fpsMiss      ? "*** FPS NOT MET ***"  : "")
                      << (!noDataReason.isEmpty() ? "— " + noDataReason : "")
                      << (audioArriving && m_dftCallCount > 0
                              ? QString(", avg %1 us/fft, total %2 ms/s")
                                    .arg(m_dftTotalNs / m_dftCallCount / 1000)
                                    .arg(m_dftTotalNs / 1000000)
                              : QString())
                      << (m_lastBatchSize > 0
                              ? QString(", samples/batch %1").arg(m_lastBatchSize)
                              : QString());

    m_dftCallCount = 0;
    m_dftTotalNs   = 0;
    m_batchCount   = 0;
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
        inputGain->setToolTip(tr("Tip: Increase to around +10dB to drive the compressor into harder compression."));
        outputGain->setToolTip(tr("Tip: Use the output gain to makeup for lost amplitude from the compression stage."));
        mainLayout->addWidget(grp);
    }

    // ── Compressor ──────────────────────────────────────────────────────────
    {
        compGrp = new QGroupBox(tr("Dyson Compressor"));
        auto* grp = compGrp;
        auto* form = new QFormLayout(grp);

        compEnable = new QCheckBox(tr("Enable Compressor"));
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
                    this, &AudioProcessingWidget::onAnyControlChanged);
        }
        vbox->addLayout(sliderRow);
        mainLayout->addWidget(grp);
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

        specWidget = new SpectrumWidget;
        specWidget->fftLength  = SPEC_FFT_LEN;
        specWidget->sampleRate = 48000.0;
        specWidget->showSecondary = true;
        specWidget->setMinimumHeight(120);
        specWidget->setVisible(false);   // hidden until checkbox is ticked
        vbox->addWidget(specWidget);

        mainLayout->addWidget(grp);

        connect(specEnable, &QCheckBox::toggled,
                this, &AudioProcessingWidget::onSpecEnableToggled);
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
    specEnable->blockSignals(block);
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

    specEnable->setChecked(p.spectrumEnabled);
    specWidget->setVisible(p.spectrumEnabled);

    m_spectrumFps = qBound(1, p.spectrumFPS, 60);
    specWidget->setFps(m_spectrumFps);

    // Update labels
    lblPeak->setText(QString::number(p.compPeakLimit, 'f', 1) + " dB");
    lblRelease->setText(QString::number(p.compRelease, 'f', 2) + " s");
    lblFast->setText(QString::number(p.compFastRatio, 'f', 2));
    lblSlow->setText(QString::number(p.compSlowRatio, 'f', 2));
    lblInputGain->setText(QString::number(p.inputGainDB, 'f', 1) + " dB");
    lblOutputGain->setText(QString::number(p.outputGainDB, 'f', 1) + " dB");
    lblSidetone->setText(QString::number(static_cast<int>(p.sidetoneLevel * 100.0f)) + "%");
}
