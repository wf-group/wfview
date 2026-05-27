#include "MeterItem.h"
#include <QPainter>
#include <QMouseEvent>
#include <QtMath>

//static QColor colorFromString(const QString& s) { return QColor(s); }

MeterItem::MeterItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(false);               // you were not using AA for crisp text
#if defined(Q_OS_LINUX)
    // Avoid the OpenGL QPainter path on Linux/Mesa. Qt regressions in that
    // path can crash from the scenegraph render thread during fill operations.
    setRenderTarget(QQuickPaintedItem::Image);
#else
    setRenderTarget(QQuickPaintedItem::FramebufferObject); // good default
#endif
    setAcceptedMouseButtons(Qt::LeftButton);

    qDebug(logSystem) << "Creating MeterItem";
    m_currentColor = colorFromString("#148CD2").darker();
    m_peakColor    = colorFromString("#3CA0DB").lighter();
    m_averageColor = colorFromString("#3FB7CD");
    m_lowTextColor = colorFromString("#000000");
    m_lowLineColor = m_lowTextColor;
    m_highTextColor = Qt::red;
    m_highLineColor = Qt::red;

    m_midScaleColor = Qt::yellow;
    m_centerTuningColor = Qt::green;

    m_avgLevels.resize(m_averageBalisticLength, 0);
    m_peakLevels.resize(m_peakBalisticLength, 0);

    setImplicitWidth(m_scalePixelWidth + m_mXstart + 15);
    setImplicitHeight(24); // whatever looks right for your header

    m_peakDecayTimer.setInterval(50);
    connect(&m_peakDecayTimer, &QTimer::timeout, this, &MeterItem::decayPeak);
    m_peakDecayTimer.start();

    markScaleDirty();
}

void MeterItem::markScaleDirty()
{
    m_recentlyChangedParameters = true;
    m_scaleReady = false;
    update();
}

void MeterItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    event->accept();
    emit configureMeterRequested(int(m_meterType));
}

void MeterItem::paint(QPainter *p)
{
    if (!m_haveExtremities)
        return;

    p->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Pixel-space layout (Qt Quick item pixels)
    m_mXstart = m_drawLabels ? 32 : 0;
    const int rightPad = 15;

    const int pxW = int(width());
    const int pxH = int(height());
    if (pxW <= 0 || pxH <= 0) return;

    // The scale was hard-coded to 255 before; make it match available width
    m_scalePixelWidth = qMax(1, pxW - m_mXstart - rightPad);

    p->setFont(QFont(p->fontInfo().family(), m_fontSize));

    // IMPORTANT: no setWindow. Draw in real pixels.
    // p->setWindow(...)  <-- DELETE THIS
    m_barHeight = pxH / 2;

    // Cache now depends on real pixel size, not the old fake window
    const QRect cacheRect(0, 0, pxW, pxH);
    if (m_lastDrawMeterType != m_meterType || m_recentlyChangedParameters || !m_scaleReady) {
        regenerateScale(cacheRect, p->font(), p->renderHints());
        m_lastDrawMeterType = m_meterType;
        m_recentlyChangedParameters = false;
    }

    if (m_scaleReady)
        recallScale(p);

    if (!m_haveReceivedSomeData)
        return;

    if (m_meterType == meterCenter) {
        drawValue_Center(p);
    } else if (m_meterType == meterAudio || m_meterType == meterTxMod || m_meterType == meterRxAudio) {
        drawValue_Log(p);
    } else {
        drawValue_Linear(p, (m_meterType == meterComp) ? m_reverseCompMeter : false);
    }
}


void MeterItem::regenerateScale(const QRect &windowRect, const QFont &font, QPainter::RenderHints hints)
{
    if (!m_scaleCache || m_scaleCache->size() != windowRect.size()) {
        m_scaleCache = std::make_unique<QImage>(windowRect.size(), QImage::Format_ARGB32_Premultiplied);
    }
    m_scaleCache->fill(Qt::transparent);

    QPainter painter(m_scaleCache.get());
    painter.setRenderHints(hints);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.setFont(font);

    QFontMetrics fm(font);

    QString label;
    switch (m_meterType) {
    case meterS:      label = "S"; break;
    case meterSubS:   label = "Sub"; break;
    case meterPower:  label = "PWR"; break;
    case meterALC:    label = "ALC"; break;
    case meterSWR:    label = "SWR"; break;
    case meterCenter: label = "CTR"; break;
    case meterVoltage:label = "Vd"; break;
    case meterCurrent:label = "Id"; break;
    case meterComp:   label = "CMP(dB)"; break;
    case meterAudio:  label = "dBfs"; m_peakRedLevel = 241; break;
    case meterRxAudio:label = "Rx(dBfs)"; m_peakRedLevel = 241; break;
    case meterTxMod:  label = "Tx(dBfs)"; m_peakRedLevel = 241; break;
    case meterdBu:    label = "dBu"; m_peakRedLevel = 241; break;
    case meterdBuEMF: label = "dBu(EMF)"; m_peakRedLevel = 241; break;
    case meterdBm:    label = "dBm"; m_peakRedLevel = 241; break;
    case meterNone:
        drawLabel(&painter, tr("Double-click to set meter"));
        m_scaleReady = true;
        return;
    default:
        label = "DN"; break;
    }

    m_labelWidth = fm.boundingRect(label).width();

    // call your existing scale drawers (copy/paste from meter.cpp)
    switch (m_meterType) {
    case meterS:
    case meterSubS:    drawScaleS(&painter); break;
    case meterPower:   drawScalePo(&painter); break;
    case meterALC:     drawScaleALC(&painter); break;
    case meterSWR:     drawScaleSWR(&painter); break;
    case meterCenter:  drawScaleCenter(&painter); break;
    case meterVoltage: drawScaleVd(&painter); break;
    case meterCurrent: drawScaleId(&painter); break;
    case meterComp:
        if (m_reverseCompMeter) drawScaleCompInverted(&painter);
        else drawScaleComp(&painter);
        break;
    case meterAudio:
    case meterRxAudio:
    case meterTxMod:   drawScale_dBFs(&painter); break;
    case meterdBu:
    case meterdBuEMF:  drawScaledBPositive(&painter); break;
    case meterdBm:     drawScaledBNegative(&painter); break;
    default:           drawScaleRaw(&painter); break;
    }

    if (m_drawLabels) drawLabel(&painter, label);

    painter.end();
    m_scaleReady = true;
}

void MeterItem::drawLabel(QPainter *qp, const QString& label)
{
    qp->setPen(m_lowTextColor);
    qp->drawText(0,m_scaleTextYstart, label );
}

void MeterItem::recallScale(QPainter *p)
{
    if (!m_scaleCache) return;
    p->drawImage(QPoint(0,0), *m_scaleCache, m_scaleCache->rect());
}

// ---- properties / invokables ----

void MeterItem::setMeterType(int t)
{
    auto nt = meter_t(t);
    if (nt == m_meterType) return;
    m_meterType = nt;
    m_meterShortString = getMeterDebug(m_meterType);
    emit meterTypeChanged();
    emit meterShortStringChanged();
    clearMeter();        // matches your QWidget behaviour
    markScaleDirty();
    qDebug(logSystem) << "meterType set to" << m_meterShortString;
}

void MeterItem::setMeterShortString(const QString &s)
{
    if (s == m_meterShortString) return;
    m_meterShortString = s;
    emit meterShortStringChanged();
}

void MeterItem::setCurrent(double c)
{
    // "current" means "now", ie, the value at this moment.
    this->m_current = c;
    updateAverage(c);
    updatePeak(c);

    //m_haveUpdatedData = true;
    m_haveReceivedSomeData = true;

    this->update();
    emit levelsChanged();
}

void MeterItem::updateAverage(double current)
{
    m_avgLevels[(m_avgPosition++)%m_averageBalisticLength] = current;

    double sum=0.0;

    for(unsigned int i=0; i < (unsigned int)std::min(m_avgPosition, (int)m_avgLevels.size()); i++)
    {
        sum += m_avgLevels[i];
    }

    // This never divides by zero because the above section of this code
    // inserts data, thus assuring that we will never have a zero for both
    // the position and the size.
    m_average = sum / std::min(m_avgPosition, (int)m_avgLevels.size());
}

void MeterItem::updatePeak(double current)
{
    if (m_peakPosition == 0 || current >= m_peak) {
        m_peak = current;
        m_peakHoldSamples = m_peakHoldSampleCount;
    }

    m_peakLevels[(m_peakPosition++)%m_peakBalisticLength] = m_peak;
}

void MeterItem::decayPeak()
{
    if (!m_haveReceivedSomeData || m_peak <= m_current)
        return;

    if (m_peakHoldSamples > 0) {
        --m_peakHoldSamples;
        return;
    }

    const double range = qMax(1.0, qAbs(m_scaleMax - m_scaleMin));
    const double decayStep = range / qMax(1, m_peakDecaySampleCount);
    const double nextPeak = qMax(m_current, m_peak - decayStep);

    if (qFuzzyCompare(nextPeak, m_peak))
        return;

    m_peak = nextPeak;
    emit levelsChanged();
    update();
}

void MeterItem::setLevels(double c, double pk, double avg)
{
    m_current = c;
    m_peak = pk;
    m_average = avg;
    m_peakHoldSamples = m_peakHoldSampleCount;
    m_haveReceivedSomeData = true;
    emit levelsChanged();
    update();
}

void MeterItem::setLevels2(double c, double pk)
{
    // if you want your previous averaging behaviour, port it here
    setLevels(c, pk, m_average);
}

void MeterItem::clearMeter()
{
    m_current = m_peak = m_average = 0;
    std::fill(m_avgLevels.begin(), m_avgLevels.end(), 0.0);
    std::fill(m_peakLevels.begin(), m_peakLevels.end(), 0.0);
    m_avgPosition = m_peakPosition = 0;
    m_peakHoldSamples = 0;
    m_haveReceivedSomeData = false;
    emit levelsChanged();
    update();
}

void MeterItem::setMeterExtremities(double min, double max, double redline)
{
    if (!qIsFinite(min) || !qIsFinite(max) || !qIsFinite(redline) || max <= min) {
        clearMeterExtremities();
        return;
    }

    if (m_haveExtremities && m_scaleMin == min && m_scaleMax == max && m_scaleRedline == redline)
        return;

    qDebug(logSystem) << "Meter extremities changed for" << m_meterShortString << "min" << min << "max" << max;
    m_scaleMin = min;
    m_scaleMax = max;
    m_scaleRedline = redline;
    m_haveExtremities = true;
    emit scaleChanged();
    markScaleDirty();
}

void MeterItem::clearMeterExtremities()
{
    if (!m_haveExtremities)
        return;

    m_haveExtremities = false;
    m_scaleReady = false;
    emit scaleChanged();
    update();
}

// trivial setters (same pattern)
void MeterItem::setPeak(double v){ if (m_peak==v) return; m_peak=v; m_haveReceivedSomeData=true; emit levelsChanged(); update(); }
void MeterItem::setAverage(double v){ if (m_average==v) return; m_average=v; m_haveReceivedSomeData=true; emit levelsChanged(); update(); }

void MeterItem::setScaleMin(double v){ if (m_scaleMin==v) return; m_scaleMin=v; m_haveExtremities=true; emit scaleChanged(); markScaleDirty(); }
void MeterItem::setScaleMax(double v){ if (m_scaleMax==v) return; m_scaleMax=v; m_haveExtremities=true; emit scaleChanged(); markScaleDirty(); }
void MeterItem::setScaleRedline(double v){ if (m_scaleRedline==v) return; m_scaleRedline=v; m_haveExtremities=true; emit scaleChanged(); markScaleDirty(); }

void MeterItem::setReverseCompMeter(bool r){ if (m_reverseCompMeter==r) return; m_reverseCompMeter=r; emit reverseCompMeterChanged(); markScaleDirty(); }
void MeterItem::setUseGradients(bool g){ if (m_useGradients==g) return; m_useGradients=g; emit useGradientsChanged(); update(); }

void MeterItem::setDrawLabels(bool d)
{
    if (m_drawLabels == d) return;
    m_drawLabels = d;

    m_mXstart = m_drawLabels ? 32 : 0;
    setImplicitWidth(m_scalePixelWidth + m_mXstart + 15);

    emit drawLabelsChanged();
    markScaleDirty();
}

void MeterItem::setScaleTextColor(const QColor &color)
{
    if (!color.isValid() || m_lowTextColor == color)
        return;
    m_lowTextColor = color;
    emit scaleColorsChanged();
    markScaleDirty();
}

void MeterItem::setScaleLineColor(const QColor &color)
{
    if (!color.isValid() || m_lowLineColor == color)
        return;
    m_lowLineColor = color;
    emit scaleColorsChanged();
    markScaleDirty();
}

void MeterItem::setScaleHighTextColor(const QColor &color)
{
    if (!color.isValid() || m_highTextColor == color)
        return;
    m_highTextColor = color;
    emit scaleColorsChanged();
    markScaleDirty();
}

void MeterItem::setScaleHighLineColor(const QColor &color)
{
    if (!color.isValid() || m_highLineColor == color)
        return;
    m_highLineColor = color;
    emit scaleColorsChanged();
    markScaleDirty();
}

double MeterItem::getValueFromPixelScale(int p) const
{
    return (p * (m_scaleMax - m_scaleMin) / double(m_scalePixelWidth)) + m_scaleMin;
}

int MeterItem::getPixelScaleFromValue(double v) const
{
    if (m_scaleMax == m_scaleMin || !qIsFinite(v))
        return 0;
    const int pixel = int((v - m_scaleMin) * (double(m_scalePixelWidth) / (m_scaleMax - m_scaleMin)));
    return qBound(0, pixel, m_scalePixelWidth);
}


void MeterItem::scaleLinearNumbersForDrawing()
{
    m_currentRect = getPixelScaleFromValue(m_current);
    m_averageRect = getPixelScaleFromValue(m_average);
    m_peakRect    = getPixelScaleFromValue(m_peak);
}

void MeterItem::scaleLogNumbersForDrawing() {
    const int currentIndex = qBound(0, 255 - int(m_current), 255);
    const int averageIndex = qBound(0, 255 - int(m_average), 255);
    const int peakIndex = qBound(0, 255 - int(m_peak), 255);
    m_currentRect = qBound(0, int((1 - audiopot[currentIndex]) * m_scalePixelWidth), m_scalePixelWidth);
    m_averageRect = qBound(0, int((1 - audiopot[averageIndex]) * m_scalePixelWidth), m_scalePixelWidth);
    m_peakRect = qBound(0, int((1 - audiopot[peakIndex]) * m_scalePixelWidth), m_scalePixelWidth);
}

int MeterItem::nearestStep(double d, int stepSize) {
    // This function was only tested with negative numbers,
    // and produces the rounding edge at the correct spot for
    // our dBm (R8600) scale.
    bool flipped = false;
    if(d < 0) {
        d *= -1;
        flipped = true;
    }

    int n = round(d/stepSize);
    // 99 becomes 9.9 becomes 10
    // 95 becomes 9.5 becomes 10
    // 94 becomes 9.4 becomes  9

    if(flipped)
        n*=-stepSize;
    else
        n *= stepSize;

    return n;
}
// The drawScale functions draw the numbers and number unerline for each type of meter

void MeterItem::drawScaleRaw(QPainter *qp)
{
    qp->setPen(m_lowTextColor);
    //qp->setFont(QFont("Arial", fontSize));
    int i=m_mXstart;
    for(; i<m_mXstart+256; i+=25)
    {
        qp->drawText(i,m_scaleTextYstart, QString("%1").arg(i) );
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,m_scalePixelWidth/2+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(m_scalePixelWidth/2+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaledBPositive(QPainter *qp)
{
    // dB scale which is mostly (or all) positive numbers
    // used for dBu EMF and dBu



    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan/10) * 10;
    int stepDelta = 0;
    if(scaleSpan < 100) {
        stepDelta = 10;
    } else {
        stepDelta = 20;
    }

    QFontMetrics fm = qp->fontMetrics();
    QRect textRect = fm.boundingRect(QString::number(m_scaleMax));
    int textMaxLength = textRect.width();
    int scaleCacheWidth = m_scaleCache->size().width();

    int db = 0;
    int dbPrior = -10;
    double val = 0;

    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        db = ((int)round(val)/stepDelta) * stepDelta;
        //db = ((int)(round(val/10.0))*10 / stepDelta) * stepDelta; // round to nearest step

        if(db != dbPrior) {
            if(db > m_scaleRedline) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(db);
            //            qDebug() << "p: " << p << "val: " << val
            //                     << "dB: " << db << "dBprior: " << dbPrior;
            if( (p + textMaxLength < scaleCacheWidth)
                && (p > m_labelWidth ) ){
                qp->drawText(p,m_scaleTextYstart, formattedString );
            }
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            dbPrior = db;
        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);

}

void MeterItem::drawScaledBNegative(QPainter *qp)
{
    // dB scale which is mostly (or all) negative numbers
    // used for dBm

    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);
    QFontMetrics fm = qp->fontMetrics();
    QRect textRect = fm.boundingRect(QString::number(-100));
    int textMaxLength = textRect.width();
    int scaleCacheWidth = m_scaleCache->size().width();

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 100) {
        stepDelta = 10;
    } else {
        stepDelta = 20;
    }

    int db = 0;
    int dbPrior = -10;
    double val = 0;

    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        db = nearestStep(val, stepDelta);

        if( (db != dbPrior) && ((int)round(val)%10==0)) {
            if(db > m_scaleRedline) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(db);
            //            qDebug() << "p: " << p << "val: " << val
            //                     << "dB: " << db << "dBprior: " << dbPrior;
            if ( (p + textMaxLength < scaleCacheWidth)
                && (p > m_labelWidth) ){
                qp->drawText(p,m_scaleTextYstart, formattedString );
            }
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            dbPrior = db;
        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);

}

void MeterItem::drawScale_dBFs(QPainter *qp)
{
    qp->setPen(m_lowTextColor);
    m_peakRedLevel = 193;

    if(m_meterType==meterAudio)
        qp->drawText(20+m_mXstart,m_scaleTextYstart, QString("-30"));
    qp->drawText(38+m_mXstart+2,m_scaleTextYstart, QString("-24"));
    qp->drawText(71+m_mXstart,m_scaleTextYstart, QString("-18"));
    qp->drawText(124+m_mXstart,m_scaleTextYstart, QString("-12"));
    qp->drawText(193+m_mXstart,m_scaleTextYstart, QString("-6"));
    qp->drawText(255+m_mXstart,m_scaleTextYstart, QString("0"));

    // Low ticks:
    qp->setPen(m_lowLineColor);
    qp->drawLine(20+m_mXstart,m_scaleTextYstart, 20+m_mXstart, m_scaleTextYstart+5);
    qp->drawLine(38+m_mXstart,m_scaleTextYstart, 38+m_mXstart, m_scaleTextYstart+5);
    qp->drawLine(71+m_mXstart,m_scaleTextYstart, 71+m_mXstart, m_scaleTextYstart+5);
    qp->drawLine(124+m_mXstart,m_scaleTextYstart, 124+m_mXstart, m_scaleTextYstart+5);


    // High ticks:
    qp->setPen(m_highLineColor);
    qp->drawLine(193+m_mXstart,m_scaleTextYstart, 193+m_mXstart, m_scaleTextYstart+5);
    qp->drawLine(255+m_mXstart,m_scaleTextYstart, 255+m_mXstart, m_scaleTextYstart+5);

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,m_peakRedLevel+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(m_peakRedLevel+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleVd(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 20) {
        stepDelta = 1;
    } else {
        stepDelta = 2;
    }

    int volt = int(m_scaleMin);
    int voltPrior = int(m_scaleMin); // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        volt = ((int)val / stepDelta) * stepDelta; // round to nearest step

        if(volt != voltPrior) {
            //qDebug() << "val: " << val << ", power: " << power << ", powerPrior: " << powerPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(volt > m_scaleRedline) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(volt);
            qp->drawText(p,m_scaleTextYstart, formattedString );
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            voltPrior = volt;

        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleCenter(QPainter *qp)
{
    // No known units
    qp->setPen(m_lowLineColor);
    qp->drawText(60+m_mXstart,m_scaleTextYstart, QString("-"));

    qp->setPen(m_centerTuningColor);
    // Attempt to draw the zero at the actual center
    qp->drawText(128-2+m_mXstart,m_scaleTextYstart, QString("0"));

    qp->setPen(m_lowLineColor);
    qp->drawText(195+m_mXstart,m_scaleTextYstart, QString("+"));

    qp->setPen(m_lowLineColor);
    qp->drawLine(m_mXstart,m_scaleLineYstart,128-32+m_mXstart,m_scaleLineYstart);

    qp->setPen(m_centerTuningColor);
    qp->drawLine(128-32+m_mXstart,m_scaleLineYstart,128+32+m_mXstart,m_scaleLineYstart);

    qp->setPen(m_lowLineColor);
    qp->drawLine(128+32+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}


void MeterItem::drawScalePo(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 20) {
        // Low power radio, show each 1 watt on the scale
        stepDelta = 1;
    } else {
        // High power radio, show every 10 watts on the scale.
        stepDelta = 20;
    }

    int power = 0;
    int powerPrior = 0; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        power = ((int)val / stepDelta) * stepDelta; // round to nearest step


        if(power != powerPrior) {
            //qDebug() << "val: " << val << ", power: " << power << ", powerPrior: " << powerPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(power > m_scaleRedline) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(power);
            qp->drawText(p,m_scaleTextYstart, formattedString );
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            powerPrior = power;

        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleRxdB(QPainter *qp)
{
    // TODO: remove
    (void)qp;
}

void MeterItem::drawScaleALC(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 25;


    int alc = 0;
    int alcPrior = -1; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart)*100; // values are 0-1-2, with 1 being 100%
        alc = ((int)val / stepDelta) * stepDelta; // round to nearest step


        if(alc != alcPrior) {
            //qDebug() << "val: " << val << ", alc: " << alc << ", alcPrior: " << alcPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(alc > m_scaleRedline*99) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(alc);
            qp->drawText(p,m_scaleTextYstart, formattedString );
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            //            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);

            alcPrior = alc;
        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleComp(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan <= 30) {
        stepDelta = 3;
    } else {
        stepDelta = 12;
    }

    int comp = 0;
    int compPrior = 0; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    // Adding stepDelta to the start means we skip the first step.
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        comp = ((int)val / stepDelta) * stepDelta; // round to nearest step
        if(comp != compPrior) {
            //qDebug() << "val: " << val << ", comp: " << comp << ", compPrior: " << compPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(compPrior != 0) {
                if(comp > m_scaleRedline) {
                    qp->setPen(m_highTextColor);
                } else {
                    qp->setPen(m_lowTextColor);
                }
                formattedString = QString::number(comp);
                qp->drawText(p,m_scaleTextYstart, formattedString );
                qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            }
            compPrior = comp;
        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleCompInverted(QPainter *qp) {
    // inverted scale

    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan <= 30) {
        stepDelta = 3;
    } else {
        stepDelta = 12;
    }

    int comp = 0;
    int compPrior = -1; // -1 allows the first value to be drawn
    double val = 0;
    QString formattedString;
    // Adding stepDelta to the start means we skip the first step.
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth-64; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        comp = ((int)val / stepDelta) * stepDelta; // round to nearest step
        if(comp != compPrior) {
            //qDebug() << "p: " << p << ", val: " << val << ", comp: " << comp << ", compPrior: " << compPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(comp > m_scaleRedline-3) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(comp);
            qp->drawText(m_scalePixelWidth+m_mXstart-p,m_scaleTextYstart, formattedString );
            qp->drawLine(m_scalePixelWidth+m_mXstart-p,m_scaleTextYstart, m_scalePixelWidth+m_mXstart-p, m_scaleTextYstart+5);

            compPrior = comp;
        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_scalePixelWidth+m_mXstart,m_scaleLineYstart,m_scalePixelWidth-redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(m_scalePixelWidth-redLevelRect+m_mXstart,m_scaleLineYstart,m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleSWR(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    bool smallSteps = false;
    // Icom SWR scale goes up to 20:1
    // Kenwood SWR scale goes up to 5:1
    if(scaleSpan <= 6) {
        smallSteps = true;
    }

    // Draw vertical tic marks and scale numbers:
    qp->setPen(m_lowTextColor);
    int swr = 0;
    int swrPrior = -1;
    double val = 0;
    int fives = 0;
    int tens = 0;
    int decimals = 0;
    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        if(smallSteps) {
            swr = (val * 50)/50;
            swr = swr * 10;
            fives = ((int)(val*10))%5;
            tens = ((int)(val*10))%10;
            if( (fives==0) && (tens!=0)) {
                swr = swr + 5;
                decimals = 1;
            } else {
                decimals = 0;
            }
        } else {
            swr = (val * 50)/50;
            swr *= 10;
        }

        if( (swr != swrPrior) && (swr > swrPrior) ) {
            if( (swr > 50) && (swr - swrPrior < 20) )
                continue;

            // qDebug() << "SWR meter: p: " << p << ", val: " << val << "swr: " << swr << "swrPrior: " << swrPrior << "fives: " << fives << "tens:" << tens;
            if(val >= m_scaleRedline) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(swr/10.0, 'f', decimals);
            qp->drawText(p,m_scaleTextYstart, formattedString );
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            swrPrior = swr;
        }
    }

    // Draw horizontal underline:
    qp->setPen(m_lowLineColor);
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleId(QPainter *qp)
{
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    int scaleSpan = m_scaleMax - m_scaleMin;
    scaleSpan = (scaleSpan*10) / 10;
    int stepDelta = 0;
    if(scaleSpan < 20) {
        stepDelta = 1;
    } else if (scaleSpan < 40 ){
        stepDelta = 2;
    } else {
        stepDelta = 5;
    }

    int m_current = 0;
    int currentPrior = 0; // causes the zero to be skipped
    double val = 0;

    QString formattedString;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        m_current = ((int)val / stepDelta) * stepDelta; // round to nearest step

        if(m_current != currentPrior) {
            //qDebug() << "val: " << val << ", power: " << power << ", powerPrior: " << powerPrior << " scaleSpan:" << scaleSpan << "stepDelta: " << stepDelta;
            if(m_current > m_scaleRedline) {
                qp->setPen(m_highTextColor);
            } else {
                qp->setPen(m_lowTextColor);
            }
            formattedString = QString::number(m_current);
            qp->drawText(p,m_scaleTextYstart, formattedString );
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            currentPrior = m_current;

        }
    }

    // Now the lines:
    qp->setPen(m_lowLineColor);

    // Line: X1, Y1 -->to--> X2, Y2
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}

void MeterItem::drawScaleS(QPainter *qp)
{
    // double --> S-Unit:
    // -54 = S0
    // -48 = S1
    // -42 = S2
    // -36 = S3
    // -30 = S4
    // -24 = S5
    // -18 = S6
    // -12 = S4
    // -6 = S8
    // 0 = S9
    // 10 = +10
    // 20 = +20
    // 30 = +30 ... etc
    int redLevelRect = getPixelScaleFromValue(m_scaleRedline);

    qp->setPen(m_lowTextColor);

    // We must find the pixel values (0-255) which correspond to those s-units
    // and we are assuming a linear scale because 6dB per s-unit is a constant rate

    int sunit = 0;
    int sunitPrior = -1;
    double val = 0;
    bool plus = false;
    for(int p=m_mXstart; p < m_mXstart+m_scalePixelWidth; p++) {
        val = getValueFromPixelScale(p-m_mXstart);
        // Due to low precision, we have to set this threshold at 1 instead of 0.
        // We also have to be carefuly not to double-write at S9
        if(val <=1) {
            sunit = (val-m_scaleMin)/6;
        } else if (val >= 10 ){
            sunit = ((int)val/10) * 10; // round to nearest ten
            if(sunit==10) {
                plus = true;
            } else {
                plus = false;
            }
        }

        if(sunit != sunitPrior) {
            if(val >= m_scaleRedline) {
                qp->setPen(m_highTextColor);
                //qDebug() << "pval: " << p-m_mXstart << "val: " << val <<   ", s-unit: " << sunit << ", sunit prior: " << sunitPrior;
            } else {
                qp->setPen(m_lowTextColor);
            }
            if(plus)
                qp->drawText(p-10,m_scaleTextYstart, QString("+%1").arg(sunit) );
            else
                qp->drawText(p,m_scaleTextYstart, QString("%1").arg(sunit) );
            qp->drawLine(p,m_scaleTextYstart, p, m_scaleTextYstart+5);
            sunitPrior = sunit;
        }
    }

    // Horizontal lines:
    qp->setPen(m_lowLineColor);
    qp->drawLine(m_mXstart,m_scaleLineYstart,redLevelRect+m_mXstart,m_scaleLineYstart);
    qp->setPen(m_highLineColor);
    qp->drawLine(redLevelRect+m_mXstart,m_scaleLineYstart,m_scalePixelWidth+m_mXstart,m_scaleLineYstart);
}


void MeterItem::drawValue_Linear(QPainter *qp, bool reverse) {
    // Draw a rectangle.

    // The parameters m_current, peak, and average need to be scaled
    // to 0-255.

    // Data input:  scaleMin ---- scaleMax
    // Data output:     0    ----    255
    scaleLinearNumbersForDrawing();

    //this->setAccessibleName(QString("Meter: %1 percent").arg( (int)(100*(double)(m_currentRect/255.0))));

    if(m_currentRect < 0) {
        return;
    }

    if (m_meterType == meterdBuEMF || m_meterType == meterdBm || m_meterType == meterdBu)
    {
        qp->setPen(m_lowTextColor);
        uchar prec=1;
        if (m_current >= 100.0 || m_current <= -100.0)
            prec=0;
        qp->drawText(0,m_scaleTextYstart+20, QString("%0").arg(m_current,0,'f',prec,'0'));

    }
    // And then, we can just plot them, since we already scaled
    // the scales, right?
    if(m_useGradients) {
        QLinearGradient grad(QPointF(0, 0), QPointF(255, 0));
        if(reverse) {
            grad.setColorAt(1, m_currentColor.darker().darker());
            grad.setColorAt(0, m_currentColor.lighter());
        } else {
            grad.setColorAt(0, m_currentColor.darker().darker());
            grad.setColorAt(1, m_currentColor.lighter());
        }
        qp->setBrush(grad);
    } else {
        qp->setBrush(m_currentColor);
    }
    qp->setPen(m_currentColor);
    if(reverse) {
        qp->drawRect(m_scalePixelWidth+m_mXstart,m_mYstart,-m_currentRect,m_barHeight);
    } else {
        qp->drawRect(m_mXstart,m_mYstart,m_currentRect,m_barHeight);
    }

    // Average:
    qp->setPen(m_averageColor);
    qp->setBrush(m_averageColor);
    if(reverse) {
        qp->drawRect(m_scalePixelWidth+m_mXstart-m_averageRect,m_mYstart,-1,m_barHeight); // bar is 1 pixel wide, height = meter start?
    } else {
        qp->drawRect(m_mXstart+m_averageRect-1,m_mYstart,1,m_barHeight); // bar is 1 pixel wide, height = meter start?
    }

    // Peak:
    qp->setPen(m_peakColor);
    qp->setBrush(m_peakColor);
    if(m_peak > m_peakRedLevel)
    {
        qp->setBrush(Qt::red);
        qp->setPen(Qt::red);
    }
    if(reverse) {
        qp->drawRect(m_scalePixelWidth+m_mXstart-m_peakRect+1,m_mYstart,-1,m_barHeight);
    } else {
        qp->drawRect(m_mXstart+m_peakRect-1,m_mYstart,2,m_barHeight);
    }
}

void MeterItem::drawValue_Center(QPainter *qp) {
    // Draw a center-referenced rectangle

    // First, scale the data for 0-255:
    // Not exactly needed for this since only icom data uses this function currently and that data are already 0-255
    // But we are going to call it anyway to prove that our code is good:
    scaleLinearNumbersForDrawing();

    // Current value:
    // starting at the center (m_mXstart+128) and offset by (m_current-128)
    if(m_useGradients) {
        QLinearGradient grad(QPointF(0, 0), QPointF(255, 0));
        grad.setColorAt(0, m_currentColor.darker().darker());
        grad.setColorAt(0.5, m_currentColor.lighter());
        grad.setColorAt(1, m_currentColor.darker().darker());
        qp->setBrush(grad);
    } else {
        qp->setBrush(m_currentColor);
    }

    qp->setPen(m_currentColor);
    qp->drawRect(m_mXstart+128,m_mYstart,m_currentRect-128,m_barHeight);

    // Average:
    qp->setPen(m_averageColor);
    qp->setBrush(m_averageColor);
    qp->drawRect(m_mXstart+m_averageRect-1,m_mYstart,1,m_barHeight); // bar is 1 pixel wide, height = meter start?

    // Peak: (what does peak mean for off-center deviation?)
    qp->setPen(m_peakColor);
    qp->setBrush(m_peakColor);
    if((m_peak > 191) || (m_peak < 63))
    {
        qp->setBrush(Qt::red);
        qp->setPen(Qt::red);
    }

    qp->drawRect(m_mXstart+m_peakRect-1,m_mYstart,1,m_barHeight);
}

void MeterItem::drawValue_Log(QPainter *qp) {
    // Log scale but still 0-255:
    scaleLogNumbersForDrawing();

    // Current value:
    // X, Y, Width, Height
    if(m_useGradients) {
        QLinearGradient grad(QPointF(0, 0), QPointF(255, 0));
        grad.setColorAt(0, m_currentColor.darker().darker());
        grad.setColorAt(1, m_currentColor.lighter());

        qp->setBrush(grad);
    } else {
        qp->setBrush(m_currentColor);
    }
    qp->setPen(m_currentColor);

    qp->drawRect(m_mXstart,m_mYstart,m_currentRect,m_barHeight);

    // Average:
    qp->setPen(m_averageColor);
    qp->setBrush(m_averageColor);
    qp->drawRect(m_mXstart+m_averageRect-1,m_mYstart,1,m_barHeight); // bar is 1 pixel wide, height = meter start?

    // Peak:
    qp->setPen(m_peakColor);
    qp->setBrush(m_peakColor);
    if(m_peak > m_peakRedLevel)
    {
        qp->setBrush(Qt::red);
        qp->setPen(Qt::red);
    }

    qp->drawRect(m_mXstart+m_peakRect-1,m_mYstart,2,m_barHeight);
}
