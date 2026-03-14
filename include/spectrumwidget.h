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
// SpectrumWidget — lightweight spectrum display for TX/RX audio processors.
//
// Two bin modes:
//   logBins = true  — bins are log-spaced (SPEC_BINS_PER_DECADE per decade,
//                     50 Hz – 8 kHz).  Bin index maps linearly to x position.
//   logBins = false — bins are linearly-spaced FFT bins; freq = i * binRes.
//                     Mapped to x via log10(freq).
//
// Maintained by the processing widget: it feeds spectrumPrimary (input, green)
// and spectrumSecondary (output, orange) with dBFS values.
// The repaint rate is controlled via setFps().
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
        if (!timer.isActive()) timer.start();
    }

    // Stop the repaint timer entirely.  The widget only repaints when
    // the caller explicitly calls update() after setting new data.
    void setStaticMode()
    {
        timer.stop();
    }

    // Set by AudioProcessingWidget to match the active DFT configuration.
    int    fftLength  = 1024;
    double sampleRate = 48000.0;

    // Display range (dBFS).
    double minDb = -90.0;
    double maxDb =   0.0;

    // Spacing between horizontal dBFS grid lines (dB). Change to taste.
    double dbGridStep = 6.0;

    // When true, bins are log-spaced (even resolution per octave on display).
    // When false, bins are linearly-spaced FFT output (legacy).
    bool logBins = false;

    // Spectrum data — dBFS per bin.
    // logBins=true:  one value per log-spaced bin (numBins = decades × binsPerDecade).
    // logBins=false: one value per FFT bin (bins 0 .. fftLength/2 - 1).
    std::vector<double> spectrumPrimary;    // input (pre-DSP)  — green
    std::vector<double> spectrumSecondary;  // output (post-DSP) — orange
    bool showSecondary = true;

    // Legend labels (default: "Input" / "Output").
    QString primaryLabel   = tr("Input");
    QString secondaryLabel = tr("Output");

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
        if (showSecondary) {
            painter.setPen(QColor(0, 200, 100));
            painter.drawText(6, 26, primaryLabel);
            painter.setPen(QColor(255, 140, 0));
            painter.drawText(6, 14, secondaryLabel);
        } else if (!spectrumPrimary.empty()) {
            painter.setPen(QColor(0, 200, 100));
            painter.drawText(6, 14, primaryLabel);
        }

        // ── Paint timing (logged every second) ───────────────────────────────
        m_paintTotalNs += t.nsecsElapsed();
        ++m_paintCallCount;
        if (m_paintLogTimer.elapsed() >= 1000) {
            const bool fpsMiss = m_paintCallCount < (m_targetFps * 8 / 10); // warn below 80%
            Q_UNUSED(fpsMiss)
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
        const QPen gridPen(QColor(50, 50, 50), 1, Qt::DashLine);
        const QPen labelPen(QColor(90, 90, 90));

        // Vertical grid lines — one per octave, 50 Hz to 8 kHz.
        p.setPen(gridPen);
        for (double freq = 50.0; freq <= 8000.0; freq *= 2.0) {
            int x = freqToX(freq);
            p.drawLine(x, 0, x, height());
            p.setPen(labelPen);
            QString label = (freq >= 1000.0)
                ? QString::number(freq / 1000.0, 'f', 1) + "k"
                : QString::number(static_cast<int>(freq));
            p.drawText(x + 3, height() - 4, label);
            p.setPen(gridPen);
        }

        // Horizontal grid lines — one every effectiveStep dBFS.
        // Auto-scale: if lines would be closer than (text height + margin) pixels,
        // increase the step in 3 dB increments until they fit.
        const double h     = static_cast<double>(height());
        const double range = maxDb - minDb;
        const int minSpacing = p.fontMetrics().height() + 4; // margin in px
        double effectiveStep = dbGridStep;
        while (h * effectiveStep / range < minSpacing && effectiveStep < range)
            effectiveStep += 3.0;

        // Snap the first line to the nearest multiple of effectiveStep above minDb.
        const double firstLine = std::ceil(minDb / effectiveStep) * effectiveStep;
        for (double db = firstLine; db <= maxDb; db += effectiveStep) {
            const int y = static_cast<int>(h - (db - minDb) / range * h);
            p.setPen(gridPen);
            p.drawLine(0, y, width(), y);
            p.setPen(labelPen);
            p.drawText(4, y - 2, QString::number(static_cast<int>(db)) + " dB");
        }
    }

    void drawSpectrum(QPainter &p,
                      const std::vector<double> &data,
                      QColor color)
    {
        p.setPen(QPen(color, 1));
        QPolygonF pts;
        const int numBins = static_cast<int>(data.size());
        if (numBins < 2) return;

        if (logBins) {
            // Bins are already log-spaced from 50 Hz to 8 kHz, so bin index
            // maps linearly to x position (the x-axis is also log over the
            // same range).
            const double w = static_cast<double>(width());
            const double h = static_cast<double>(height());
            for (int i = 0; i < numBins; ++i) {
                const double x    = (static_cast<double>(i) / (numBins - 1)) * w;
                const double yN   = (data[i] - minDb) / (maxDb - minDb);
                const double y    = h - yN * h;
                pts << QPointF(x, y);
            }
        } else {
            // Legacy: linearly-spaced FFT bins mapped via log frequency axis.
            const double binRes = sampleRate / fftLength;
            const int maxBin = qMin(numBins,
                                    static_cast<int>(8000.0 / binRes));
            for (int i = 1; i < maxBin; ++i) {
                const double freq  = i * binRes;
                if (freq < 50.0) continue;
                const float  yNorm = static_cast<float>((data[i] - minDb) / (maxDb - minDb));
                const float  y     = static_cast<float>(height()) - yNorm * static_cast<float>(height());
                pts << QPointF(freqToX(freq), static_cast<double>(y));
            }
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
