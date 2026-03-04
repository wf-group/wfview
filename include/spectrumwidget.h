#ifndef SPECTRUMWIDGET_H
#define SPECTRUMWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QElapsedTimer>
#include <QPolygonF>
#include <vector>
#include <cmath>

#include "logcategories.h"

// ─────────────────────────────────────────────────────────────────────────────
// SpectrumWidget — lightweight spectrum display for the TX audio processor.
//
// Maintained by AudioProcessingWidget: it feeds spectrumPrimary (input, green)
// and spectrumSecondary (output, orange) with dBFS values (one per DFT bin,
// bins 0..fftLength/2-1) and updates sampleRate / fftLength when the radio
// sample rate changes.  The repaint rate is controlled via setFps().
// ─────────────────────────────────────────────────────────────────────────────

class SpectrumWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SpectrumWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        timer.setInterval(1000 / m_targetFps);
        connect(&timer, &QTimer::timeout, this, QOverload<>::of(&SpectrumWidget::update));
        timer.start();
        m_paintLogTimer.start();
        setBackgroundRole(QPalette::Dark);
        setAutoFillBackground(true);
    }

    void setFps(int fps)
    {
        m_targetFps = qBound(1, fps, 60);
        timer.setInterval(1000 / m_targetFps);
    }

    // Set by AudioProcessingWidget to match the active DFT configuration.
    int    fftLength  = 1024;
    double sampleRate = 48000.0;

    // Display range (dBFS).
    double minDb = -90.0;
    double maxDb =   0.0;

    // Spectrum data — dBFS per bin for the positive-frequency half of the DFT
    // (bins 0 .. fftLength/2 - 1).  Updated each time onSpectrumSamples fires.
    std::vector<double> spectrumPrimary;    // input (pre-DSP)  — green
    std::vector<double> spectrumSecondary;  // output (post-DSP) — orange
    bool showSecondary = true;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QElapsedTimer t;
        t.start();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(20, 20, 20));
        drawGrid(painter);

        // Draw input (green) below output so output is always readable on top.
        if (!spectrumPrimary.empty())
            drawSpectrum(painter, spectrumPrimary,   QColor(0, 200, 100));
        if (showSecondary && !spectrumSecondary.empty())
            drawSpectrum(painter, spectrumSecondary, QColor(255, 140, 0, 180));

        // Legend
        QFont f = painter.font();
        f.setPointSize(8);
        painter.setFont(f);
        painter.setPen(QColor(0, 200, 100));
        painter.drawText(6, 26, tr("Input"));
        painter.setPen(QColor(255, 140, 0));
        painter.drawText(6, 14, tr("Output"));

        // ── Paint timing (logged every second) ───────────────────────────────
        m_paintTotalNs += t.nsecsElapsed();
        ++m_paintCallCount;
        if (m_paintLogTimer.elapsed() >= 1000) {
            const bool fpsMiss = m_paintCallCount < (m_targetFps * 8 / 10); // warn below 80%
            /*
            qCDebug(logAudio) << "[SpectrumPaint]"
                              << m_paintCallCount << "paints/s"
                              << "(target" << m_targetFps << "fps)"
                              << (fpsMiss ? "*** FPS NOT MET ***" : "")
                              << ", avg"
                              << (m_paintCallCount > 0
                                      ? m_paintTotalNs / m_paintCallCount / 1000
                                      : 0LL)
                              << "us/paint, total" << m_paintTotalNs / 1000000 << "ms/s";
            */
            m_paintTotalNs   = 0;
            m_paintCallCount = 0;
            m_paintLogTimer.restart();
        }
    }

private:
    QTimer        timer;
    int           m_targetFps      = 10;
    QElapsedTimer m_paintLogTimer;
    qint64        m_paintTotalNs   = 0;
    int           m_paintCallCount = 0;

    void drawGrid(QPainter &p)
    {
        p.setPen(QPen(QColor(50, 50, 50), 1, Qt::DashLine));
        for (double freq = 50.0; freq <= 8000.0; freq *= 2.0) {
            int x = freqToX(freq);
            p.drawLine(x, 0, x, height());
            p.setPen(QColor(90, 90, 90));
            QString label = (freq >= 1000.0)
                ? QString::number(freq / 1000.0, 'f', 1) + "k"
                : QString::number(static_cast<int>(freq));
            p.drawText(x + 3, height() - 4, label);
            p.setPen(QPen(QColor(50, 50, 50), 1, Qt::DashLine));
        }
    }

    void drawSpectrum(QPainter &p,
                      const std::vector<double> &data,
                      QColor color)
    {
        p.setPen(QPen(color, 1));
        QPolygonF pts;
        const double binRes = sampleRate / fftLength;
        const int maxBin = qMin(static_cast<int>(data.size()),
                                static_cast<int>(8000.0 / binRes));
        for (int i = 1; i < maxBin; ++i) {  // skip DC bin 0
            const double freq  = i * binRes;
            if (freq < 50.0) continue;      // below log-axis minimum; skip
            const float  yNorm = static_cast<float>((data[i] - minDb) / (maxDb - minDb));
            const float  y     = static_cast<float>(height()) - yNorm * static_cast<float>(height());
            pts << QPointF(freqToX(freq), static_cast<double>(y));
        }
        p.drawPolyline(pts);
    }

    int freqToX(double freq) const
    {
        // Logarithmic frequency axis: 50 Hz at left edge, 8 kHz at right edge.
        // Each octave occupies equal width — standard for audio spectrum displays.
        // log10(50) ≈ 1.69897; log10(8000) ≈ 3.90309.
        static constexpr double kLogMin = 1.6989700043360188;
        static constexpr double kLogMax = 3.9030899869920159;
        const double logF = std::log10(std::max(freq, 50.0));
        return static_cast<int>((logF - kLogMin) / (kLogMax - kLogMin) * width());
    }
};

#endif // SPECTRUMWIDGET_H
