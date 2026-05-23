#ifndef METERITEM_H
#define METERITEM_H

#include <QQuickPaintedItem>
#include <QPainter>
#include <QImage>
#include <QColor>
#include <vector>

#include "rigcommander.h" // meter_t

class MeterItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(int meterType READ meterType WRITE setMeterType NOTIFY meterTypeChanged)
    Q_PROPERTY(QString meterShortString READ meterShortString WRITE setMeterShortString NOTIFY meterShortStringChanged)

    Q_PROPERTY(double current READ current WRITE setCurrent NOTIFY levelsChanged)
    Q_PROPERTY(double peak READ peak WRITE setPeak NOTIFY levelsChanged)
    Q_PROPERTY(double average READ average WRITE setAverage NOTIFY levelsChanged)

    Q_PROPERTY(double scaleMin READ scaleMin WRITE setScaleMin NOTIFY scaleChanged)
    Q_PROPERTY(double scaleMax READ scaleMax WRITE setScaleMax NOTIFY scaleChanged)
    Q_PROPERTY(double scaleRedline READ scaleRedline WRITE setScaleRedline NOTIFY scaleChanged)
    Q_PROPERTY(bool haveExtremities READ haveExtremities NOTIFY scaleChanged)

    Q_PROPERTY(bool reverseCompMeter READ reverseCompMeter WRITE setReverseCompMeter NOTIFY reverseCompMeterChanged)
    Q_PROPERTY(bool useGradients READ useGradients WRITE setUseGradients NOTIFY useGradientsChanged)
    Q_PROPERTY(bool drawLabels READ drawLabels WRITE setDrawLabels NOTIFY drawLabelsChanged)
    Q_PROPERTY(QColor scaleTextColor READ scaleTextColor WRITE setScaleTextColor NOTIFY scaleColorsChanged)
    Q_PROPERTY(QColor scaleLineColor READ scaleLineColor WRITE setScaleLineColor NOTIFY scaleColorsChanged)
    Q_PROPERTY(QColor scaleHighTextColor READ scaleHighTextColor WRITE setScaleHighTextColor NOTIFY scaleColorsChanged)
    Q_PROPERTY(QColor scaleHighLineColor READ scaleHighLineColor WRITE setScaleHighLineColor NOTIFY scaleColorsChanged)

public:
    explicit MeterItem(QQuickItem *parent = nullptr);
    void paint(QPainter *p) override;

    int meterType() const { return int(m_meterType); }
    void setMeterType(int t);

    QString meterShortString() const { return m_meterShortString; }
    void setMeterShortString(const QString& s);

    double current() const { return m_current; }
    double peak() const { return m_peak; }
    double average() const { return m_average; }

    void setCurrent(double v);
    void setPeak(double v);
    void setAverage(double v);

    double scaleMin() const { return m_scaleMin; }
    double scaleMax() const { return m_scaleMax; }
    double scaleRedline() const { return m_scaleRedline; }
    bool haveExtremities() const { return m_haveExtremities; }

    void setScaleMin(double v);
    void setScaleMax(double v);
    void setScaleRedline(double v);

    bool reverseCompMeter() const { return m_reverseCompMeter; }
    void setReverseCompMeter(bool r);

    bool useGradients() const { return m_useGradients; }
    void setUseGradients(bool g);

    bool drawLabels() const { return m_drawLabels; }
    void setDrawLabels(bool d);

    QColor scaleTextColor() const { return m_lowTextColor; }
    void setScaleTextColor(const QColor &color);

    QColor scaleLineColor() const { return m_lowLineColor; }
    void setScaleLineColor(const QColor &color);

    QColor scaleHighTextColor() const { return m_highTextColor; }
    void setScaleHighTextColor(const QColor &color);

    QColor scaleHighLineColor() const { return m_highLineColor; }
    void setScaleHighLineColor(const QColor &color);

    Q_INVOKABLE void setLevels(double current, double peak, double average);
    Q_INVOKABLE void setLevels2(double current, double peak);   // like your overload
    Q_INVOKABLE void clearMeter();
    Q_INVOKABLE void setMeterExtremities(double min, double max, double redline);

signals:
    void configureMeterRequested(int meterType); // QML will open popup and select
    void meterTypeChanged();
    void meterShortStringChanged();
    void levelsChanged();
    void scaleChanged();
    void reverseCompMeterChanged();
    void useGradientsChanged();
    void drawLabelsChanged();
    void scaleColorsChanged();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    // --- your existing drawing/caching state ---
    meter_t m_meterType = meterNone;
    meter_t m_lastDrawMeterType = meterNone;

    QString m_meterShortString;

    double m_scaleMin = 0;
    double m_scaleMax = 255;
    double m_scaleRedline = 128;
    bool m_haveExtremities = false;

    double m_current = 0.0;
    double m_peak = 0.0;
    double m_average = 0.0;

    bool m_recentlyChangedParameters = false;
    bool m_haveReceivedSomeData = false;

    bool m_reverseCompMeter = true;
    bool m_useGradients = true;
    bool m_drawLabels = true;

    int m_fontSize = 6;
    int m_labelWidth = 0;

    int m_mXstart = 32;
    int m_mYstart = 14;
    int m_barHeight = 10;
    int m_scaleLineYstart = 12;
    int m_scaleTextYstart = 10;

    int m_currentRect = 0;
    int m_averageRect = 0;
    int m_peakRect = 0;
    int m_peakRedLevel = 0;

    // ballistics
    int m_averageBalisticLength = 30;
    int m_peakBalisticLength = 30;
    int m_avgPosition = 0;
    int m_peakPosition = 0;
    std::vector<double> m_avgLevels;
    std::vector<double> m_peakLevels;

    // colors (match your defaults)
    QColor m_currentColor;
    QColor m_averageColor;
    QColor m_peakColor;
    QColor m_lowTextColor;
    QColor m_lowLineColor;
    QColor m_highTextColor;
    QColor m_highLineColor;
    QColor m_midScaleColor;
    QColor m_centerTuningColor;

    // scale cache (your QImage approach)
    std::unique_ptr<QImage> m_scaleCache;
    bool m_scaleReady = false;
    int m_scalePixelWidth = 255;


private:
    void markScaleDirty();
    void regenerateScale(const QRect &windowRect, const QFont &font, QPainter::RenderHints hints);
    void recallScale(QPainter *p);

    // --- helpers lifted from your widget ---
    void scaleLinearNumbersForDrawing();
    void scaleLogNumbersForDrawing(); // for audio
    double getValueFromPixelScale(int p) const;
    int getPixelScaleFromValue(double v) const;
    int nearestStep(double val, int stepSize); // round to nearest step

    // draw value bars
    void drawValue_Linear(QPainter *qp, bool reverse);
    void drawValue_Log(QPainter *qp);
    void drawValue_Center(QPainter *qp);

    // draw scales (copy your existing functions across)
    void drawLabel(QPainter *qp, const QString& labelText);
    void drawScaleS(QPainter *qp);
    void drawScaleCenter(QPainter *qp);
    void drawScalePo(QPainter *qp);
    void drawScaleRxdB(QPainter *qp);
    void drawScaleALC(QPainter *qp);
    void drawScaleSWR(QPainter *qp);
    void drawScaleVd(QPainter *qp);
    void drawScaleId(QPainter *qp);
    void drawScaleComp(QPainter *qp);
    void drawScaleCompInverted(QPainter *qp);
    void drawScale_dBFs(QPainter *qp);
    void drawScaleRaw(QPainter *qp);
    void drawScaledBPositive(QPainter *qp);
    void drawScaledBNegative(QPainter *qp);

    // NOTE: you’ll literally paste your existing implementations in .cpp
};

#endif // METERITEM_H
