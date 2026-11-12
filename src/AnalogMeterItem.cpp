#include "AnalogMeterItem.h"

#include <QQuickWindow>
#include <QRadialGradient>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{

//
// Face geometry, in logical units. paint() scales this box to the item.
//

constexpr double kFaceW = 420.0;
constexpr double kFaceH = 200.0;
constexpr double kPivotX = 210.0;
constexpr double kPivotY = 345.0;
constexpr double kSweepDeg = 34.0;  // needle travel each side of vertical

constexpr double kArcA = 300.0;     // S (outside) / Po (inside)
constexpr double kArcB = 252.0;     // SWR (outside) / Id (inside)
constexpr double kArcC = 212.0;     // COMP, ALC zone, Vd

constexpr double kNeedleInner = 150.0;
constexpr double kNeedleOuter = 322.0;

// Scales that no channel uses are drawn at this opacity.
constexpr double kInactiveScaleAlpha = 0.75;

//
// LCD layout
//

constexpr double kLcdX0 = 44.0;
constexpr double kLcdX1 = 392.0;
constexpr int kLcdSegments = 30;

constexpr int kFrameMs = 16;

double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

double angleFor(double frac)
{
    return -kSweepDeg + frac * 2.0 * kSweepDeg;
}

QString numText(double v)
{
    return QString::number(v, 'g', 4);
}

// WCAG relative luminance and contrast ratio.
double luminance(const QColor &c)
{
    auto lin = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

double rgbDistance(const QColor &a, const QColor &b)
{
    const double dr = a.red() - b.red();
    const double dg = a.green() - b.green();
    const double db = a.blue() - b.blue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

} // namespace

AnalogMeterItem::AnalogMeterItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
#if defined(Q_OS_LINUX)
    // Same reason as MeterItem: avoid the OpenGL QPainter path on Mesa.
    setRenderTarget(QQuickPaintedItem::Image);
#else
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
#endif

    setImplicitWidth(kFaceW);
    setImplicitHeight(kFaceH);

    m_animTimer.setInterval(kFrameMs);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, &AnalogMeterItem::animate);
    m_clock.start();
}

//
// Property setters
//

void AnalogMeterItem::setStyle(int s)
{
    const Style st = Style(std::clamp(s, int(StyleAnalog), int(StyleLCDArc)));
    if (st == m_style)
        return;
    m_style = st;
    markScaleDirty();
    emit styleChanged();
}

void AnalogMeterItem::setChannelType(int ch, int t)
{
    Channel &c = m_ch[ch];
    const meter_t mt = meter_t(t);
    if (mt == c.type)
        return;
    c.type = mt;
    c.target = fractionFor(c);
    c.peak = 0.0;
    startAnimation();
    // Dimming, LCD scale rows and the LCD layout follow the meter types.
    markScaleDirty();
    if (ch == 0)
        emit primaryTypeChanged();
    else
        emit secondaryTypeChanged();
}

void AnalogMeterItem::setChannelValue(int ch, double v)
{
    Channel &c = m_ch[ch];
    c.value = v;
    c.target = fractionFor(c);
    startAnimation();
    emit levelsChanged();
}

void AnalogMeterItem::startAnimation()
{
    if (!m_animTimer.isActive()) {
        m_lastTick = m_clock.elapsed();
        m_animTimer.start();
    }
}

void AnalogMeterItem::setTransmitting(bool tx)
{
    if (tx == m_transmitting)
        return;
    m_transmitting = tx;
    update();
    emit transmittingChanged();
}

void AnalogMeterItem::setPeakHold(bool on)
{
    if (on == m_peakHold)
        return;
    m_peakHold = on;
    update();
    emit peakHoldChanged();
}

void AnalogMeterItem::setCaption(const QString &c)
{
    if (c == m_caption)
        return;
    m_caption = c;
    update();
    emit captionChanged();
}

void AnalogMeterItem::setDotColors(const QVariantList &colors)
{
    if (colors == m_dotColors)
        return;
    m_dotColors = colors;
    update();
    emit dotColorsChanged();
}

void AnalogMeterItem::setAttackMs(int ms)
{
    m_attackMs = std::max(1, ms);
    emit ballisticsChanged();
}

void AnalogMeterItem::setDecayMs(int ms)
{
    m_decayMs = std::max(1, ms);
    emit ballisticsChanged();
}

void AnalogMeterItem::setPowerFullScale(double w)
{
    if (w <= 0.0 || qFuzzyCompare(w, m_powerFullScale))
        return;
    m_powerFullScale = w;
    markScaleDirty();
    emit scaleChanged();
}

void AnalogMeterItem::setCurrentFullScale(double a)
{
    if (a <= 0.0 || qFuzzyCompare(a, m_currentFullScale))
        return;
    m_currentFullScale = a;
    markScaleDirty();
    emit scaleChanged();
}

void AnalogMeterItem::setVdLow(double v)
{
    m_vdLow = v;
    markScaleDirty();
    emit scaleChanged();
}

void AnalogMeterItem::setVdHigh(double v)
{
    m_vdHigh = v;
    markScaleDirty();
    emit scaleChanged();
}

void AnalogMeterItem::setVdRed(double v)
{
    m_vdRed = v;
    markScaleDirty();
    emit scaleChanged();
}

void AnalogMeterItem::setMeterExtremities(int channel, double min, double max)
{
    if (channel < 0 || channel > 1)
        return;
    Channel &c = m_ch[channel];
    c.haveExtremities = max > min;
    c.extMin = min;
    c.extMax = max;
    c.target = fractionFor(c);
    startAnimation();
}

void AnalogMeterItem::clearMeterExtremities(int channel)
{
    if (channel < 0 || channel > 1)
        return;
    m_ch[channel].haveExtremities = false;
    update();
}

void AnalogMeterItem::clearMeter()
{
    for (Channel &c : m_ch) {
        c.value = 0.0;
        c.target = 0.0;
        c.pos = 0.0;
        c.peak = 0.0;
    }
    m_animTimer.stop();
    update();
    emit levelsChanged();
}

//
// Palettes
//

AnalogMeterItem::Palette AnalogMeterItem::palette() const
{
    switch (m_style) {
    case StyleAnalogCream:
        return { QColor(244, 236, 214), QColor(205, 190, 155), QColor(25, 25, 25),
                 QColor(200, 30, 30), QColor(25, 25, 25), QColor(150, 20, 20) };
    case StyleLCD:
    case StyleLCDArc:
        return { QColor(246, 196, 112), QColor(226, 160, 70), QColor(84, 40, 100),
                 QColor(84, 40, 100), QColor(84, 40, 100), QColor(84, 40, 100) };
    case StyleAnalog:
    default:
        return { QColor(0, 0, 0), QColor(0, 0, 0), QColor(235, 235, 235),
                 QColor(230, 40, 40), QColor(40, 110, 230), QColor(255, 255, 255) };
    }
}

//
// Scale selection and dimming
//

unsigned AnalogMeterItem::scaleFor(meter_t type)
{
    switch (type) {
    case meterS:
    case meterSubS:
        return ScaleS;
    case meterPower:
        return ScalePo;
    case meterSWR:
        return ScaleSwr;
    case meterCurrent:
        return ScaleId;
    case meterComp:
        return ScaleComp;
    case meterALC:
        return ScaleAlc;
    case meterVoltage:
        return ScaleVd;
    default:
        // Placeholder: Center, audio and dBu/dBm meters have no printed scale yet.
        return ScaleNone;
    }
}

unsigned AnalogMeterItem::activeScales() const
{
    return scaleFor(m_ch[0].type) | scaleFor(m_ch[1].type);
}

QColor AnalogMeterItem::ink(const QColor &c, unsigned scale) const
{
    const unsigned active = activeScales();
    if (active == ScaleNone || (active & scale))
        return c;
    QColor dim = c;
    dim.setAlphaF(c.alphaF() * kInactiveScaleAlpha);
    return dim;
}

//
// Scale mappings. Input values are already calibrated by the rig file:
// S in dB relative to S9, Po in W, Id in A, Vd in V, COMP in dB,
// ALC 0..2 with 1 = top of the ALC zone.
//

double AnalogMeterItem::fracS(double db) const
{
    // S0..S9 takes the left 55 % of the scale, S9..+60 dB the rest.
    if (db <= 0.0)
        return clamp01(0.55 * (db + 54.0) / 54.0);
    return clamp01(0.55 + 0.45 * db / 60.0);
}

double AnalogMeterItem::fracPo(double w) const
{
    // Deflection follows RF voltage, so power is on a square-root scale.
    return clamp01(std::sqrt(std::max(0.0, w) / m_powerFullScale));
}

double AnalogMeterItem::fracSwr(double swr) const
{
    // Reflection coefficient: 1 -> 0, 3 -> 0.5, infinity -> 1.
    if (swr <= 1.0)
        return 0.0;
    return clamp01((swr - 1.0) / (swr + 1.0));
}

double AnalogMeterItem::fracId(double a) const
{
    return clamp01(a / m_currentFullScale);
}

double AnalogMeterItem::fracComp(double db) const
{
    return clamp01(0.08 + 0.45 * std::clamp(db, 0.0, 30.0) / 30.0);
}

double AnalogMeterItem::fracAlc(double alc) const
{
    return clamp01(0.30 * std::clamp(alc, 0.0, 2.0));
}

double AnalogMeterItem::fracVd(double v) const
{
    const double span = std::max(0.1, m_vdHigh - m_vdLow);
    return clamp01(0.72 + 0.16 * (v - m_vdLow) / span);
}

double AnalogMeterItem::fractionFor(const Channel &ch) const
{
    const double v = ch.value;
    switch (ch.type) {
    case meterNone:
        return 0.0;
    case meterS:
    case meterSubS:
        return fracS(v);
    case meterPower:
        return fracPo(v);
    case meterSWR:
        return fracSwr(v);
    case meterALC:
        return fracAlc(v);
    case meterComp:
        return fracComp(v);
    case meterVoltage:
        return fracVd(v);
    case meterCurrent:
        return fracId(v);
    default:
        break;
    }
    // Placeholder for meters without a printed scale.
    if (ch.haveExtremities)
        return clamp01((v - ch.extMin) / (ch.extMax - ch.extMin));
    return clamp01(v / 255.0);
}

//
// Ballistics: first-order lag with separate attack and decay,
// plus a peak marker that holds, then falls.
//

void AnalogMeterItem::animate()
{
    const qint64 now = m_clock.elapsed();
    const double dt = std::max<qint64>(1, now - m_lastTick);
    m_lastTick = now;

    bool settled = true;
    for (Channel &c : m_ch) {
        const double tau = (c.target > c.pos) ? m_attackMs : m_decayMs;
        c.pos += (c.target - c.pos) * (1.0 - std::exp(-dt / tau));

        if (c.pos >= c.peak) {
            c.peak = c.pos;
            c.peakSetAt = now;
        } else if (now - c.peakSetAt > m_peakHoldMs) {
            c.peak = std::max(c.pos, c.peak - dt / 1500.0);
        }

        const bool posSettled = std::abs(c.target - c.pos) < 1e-4;
        const bool peakSettled = !m_peakHold || std::abs(c.peak - c.pos) < 1e-4;
        if (posSettled)
            c.pos = c.target;
        settled = settled && posSettled && peakSettled;
    }

    if (settled)
        m_animTimer.stop();
    update();
}

//
// Common drawing
//

void AnalogMeterItem::markScaleDirty()
{
    m_faceDirty = true;
    update();
}

void AnalogMeterItem::setupTransform(QPainter *p, int pxW, int pxH) const
{
    const double s = std::min(pxW / kFaceW, pxH / kFaceH);
    p->translate((pxW - kFaceW * s) / 2.0, (pxH - kFaceH * s) / 2.0);
    p->scale(s, s);
}

void AnalogMeterItem::regenerateFace(int pxW, int pxH)
{
    const Palette c = palette();
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    m_face = std::make_unique<QImage>(QSize(int(pxW * dpr), int(pxH * dpr)), QImage::Format_ARGB32_Premultiplied);
    m_face->setDevicePixelRatio(dpr);
    m_face->fill(c.faceEdge);

    QPainter p(m_face.get());
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // Backlight glow: lighter in the middle, darker at the edges.
    if (m_style != StyleAnalog) {
        QRadialGradient g(QPointF(pxW / 2.0, pxH * 0.45), std::max(pxW, pxH) * 0.65);
        g.setColorAt(0.0, c.face.lighter(104));
        g.setColorAt(0.6, c.face);
        g.setColorAt(1.0, c.faceEdge);
        p.fillRect(QRectF(0, 0, pxW, pxH), g);
    }

    setupTransform(&p, pxW, pxH);
    if (isLcd())
        drawLcdFace(&p, c);
    else
        drawNeedleFace(&p, c);

    m_faceDirty = false;
}

void AnalogMeterItem::drawCaption(QPainter *p, const Palette &c) const
{
    if (m_caption.isEmpty())
        return;
    QFont f = p->font();
    f.setPixelSize(12);
    f.setBold(true);
    p->setFont(f);
    QColor col = c.scale;
    col.setAlphaF(0.7);
    p->setPen(col);
    p->drawText(QRectF(kFaceW - 86, kFaceH - 22, 80, 18), Qt::AlignRight | Qt::AlignVCenter, m_caption);
}

void AnalogMeterItem::drawTxLamp(QPainter *p, const QColor &c, bool filled) const
{
    if (!m_transmitting)
        return;
    QFont f = p->font();
    f.setPixelSize(13);
    f.setBold(true);
    p->setFont(f);
    const QRectF lamp(6, kFaceH - 22, 30, 17);
    p->setPen(QPen(c, 1.5));
    if (filled)
        p->setBrush(QColor(c.red(), c.green(), c.blue(), 60));
    else
        p->setBrush(Qt::NoBrush);
    p->drawRect(lamp);
    p->setPen(c);
    p->drawText(lamp, Qt::AlignCenter, "TX");
}

void AnalogMeterItem::paint(QPainter *p)
{
    const int pxW = int(width());
    const int pxH = int(height());
    if (pxW <= 0 || pxH <= 0)
        return;

    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    if (m_faceDirty || !m_face || m_face->size() != QSize(int(pxW * dpr), int(pxH * dpr)))
        regenerateFace(pxW, pxH);

    p->drawImage(QPointF(0, 0), *m_face);

    const Palette c = palette();
    p->setRenderHint(QPainter::Antialiasing, true);
    setupTransform(p, pxW, pxH);
    p->setClipRect(QRectF(0, 0, kFaceW, kFaceH));

    if (isLcd())
        drawLcdDynamic(p, c);
    else
        drawNeedleDynamic(p, c);

    drawCaption(p, c);
}

//
// Needle faces
//

void AnalogMeterItem::drawArc(QPainter *p, double r, double f0, double f1, const QColor &c, double width) const
{
    const QRectF rect(kPivotX - r, kPivotY - r, 2 * r, 2 * r);
    const double a0 = angleFor(f0);
    const double a1 = angleFor(f1);
    QPen pen(c, width);
    pen.setCapStyle(Qt::FlatCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    // Qt: 0 deg at 3 o'clock, counter-clockwise positive, 1/16 deg units.
    p->drawArc(rect, int((90.0 - a0) * 16.0), int(-(a1 - a0) * 16.0));
}

void AnalogMeterItem::drawTick(QPainter *p, double r, double frac, double len, const QColor &c, double width) const
{
    p->save();
    p->translate(kPivotX, kPivotY);
    p->rotate(angleFor(frac));
    p->setPen(QPen(c, width, Qt::SolidLine, Qt::FlatCap));
    p->drawLine(QPointF(0, -r), QPointF(0, -(r + len)));
    p->restore();
}

void AnalogMeterItem::drawText(QPainter *p, double r, double frac, const QString &txt, const QColor &c,
                               double px, bool bold) const
{
    QFont f = p->font();
    f.setPixelSize(int(px));
    f.setBold(bold);
    p->save();
    p->setFont(f);
    p->setPen(c);
    p->translate(kPivotX, kPivotY);
    p->rotate(angleFor(frac));
    p->translate(0, -r);
    p->drawText(QRectF(-40, -px, 80, 2 * px), Qt::AlignCenter, txt);
    p->restore();
}

void AnalogMeterItem::drawNeedle(QPainter *p, double frac, const QColor &c, double width) const
{
    p->save();
    p->translate(kPivotX, kPivotY);
    p->rotate(angleFor(frac));
    p->setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap));
    p->drawLine(QPointF(0, -kNeedleInner), QPointF(0, -kNeedleOuter));
    p->restore();
}

void AnalogMeterItem::drawDot(QPainter *p, double r, double frac, const QColor &c) const
{
    const double a = qDegreesToRadians(angleFor(frac));
    const QPointF centre(kPivotX + r * std::sin(a), kPivotY - r * std::cos(a));
    const bool lightDot = luminance(c) > 0.4;
    p->setPen(QPen(lightDot ? QColor(0, 0, 0, 160) : QColor(255, 255, 255, 160), 1.2));
    p->setBrush(c);
    p->drawEllipse(centre, 5.5, 5.5);
}

double AnalogMeterItem::dotRadiusFor(meter_t type)
{
    switch (type) {
    case meterS:
    case meterSubS:
    case meterPower:
        return kArcA;
    case meterSWR:
    case meterCurrent:
        return kArcB;
    case meterComp:
        return kArcC + 2.0;
    case meterALC:
        return kArcC - 4.0;
    case meterVoltage:
        return kArcC;
    default:
        // Placeholder for meters without a printed scale.
        return kArcB;
    }
}

QColor AnalogMeterItem::dotColor(const Palette &c) const
{
    // Best contrast against the face, but not mistaken for the needle.
    QColor best;
    double bestContrast = 0.0;
    for (const QVariant &v : m_dotColors) {
        QColor col = v.value<QColor>();
        if (!col.isValid())
            col = QColor(v.toString());
        if (!col.isValid())
            continue;
        col.setAlpha(255);
        if (rgbDistance(col, c.needle) < 90.0)
            continue;
        const double ratio = contrast(col, c.face);
        if (ratio > bestContrast) {
            bestContrast = ratio;
            best = col;
        }
    }
    if (!best.isValid() || bestContrast < 3.0)
        return c.high;
    return best;
}

void AnalogMeterItem::drawNeedleFace(QPainter *p, const Palette &c) const
{
    const bool cream = m_style == StyleAnalogCream;
    const double bigPx = 15.0;
    const double smallPx = 11.0;

    //
    // Arc A: S above, Po below
    //

    {
        const QColor fg = ink(c.scale, ScaleS);
        const QColor hi = ink(c.high, ScaleS);
        drawArc(p, kArcA, 0.0, fracS(0.0), fg, 1.6);
        drawArc(p, kArcA, fracS(0.0), 1.0, hi, 1.6);
        drawArc(p, kArcA + 3.0, fracS(0.0), 1.0, hi, 4.0);

        for (int db = -54; db <= 0; db += 6) {
            const bool major = ((db + 54) / 6) % 2 == 1;
            drawTick(p, kArcA, fracS(db), major ? 9.0 : 5.0, fg, major ? 2.0 : 1.4);
            if (major)
                drawText(p, kArcA + 20.0, fracS(db), QString::number((db + 54) / 6), fg, bigPx, true);
        }
        for (int db = 10; db <= 60; db += 10) {
            const bool major = db % 20 == 0;
            drawTick(p, kArcA, fracS(db), major ? 9.0 : 5.0, hi, major ? 2.0 : 1.4);
            if (major)
                drawText(p, kArcA + 20.0, fracS(db), QString("+%1").arg(db), hi, bigPx, true);
        }
        drawText(p, kArcA + 18.0, -0.035, "S", fg, bigPx, true);
        drawText(p, kArcA + 18.0, 1.075, "dB", hi, smallPx, true);
    }

    {
        const QColor fg = ink(c.scale, ScalePo);
        const double poMarks[] = {0, 5, 10, 25, 50, 100, 150, 200, 250, 300, 400, 500, 1000};
        double lastFrac = 0.0;
        for (double w : poMarks) {
            if (w > m_powerFullScale + 0.01)
                break;
            drawTick(p, kArcA, fracPo(w), -6.0, fg, 1.4);
            const bool crowded = (w == 5 || w == 25) && m_powerFullScale > 100;
            if (!crowded) {
                drawText(p, kArcA - 16.0, fracPo(w), numText(w), fg, smallPx);
                lastFrac = fracPo(w);
            }
        }
        if (lastFrac < 1.0 - 1e-6) {
            drawTick(p, kArcA, 1.0, -6.0, fg, 1.4);
            if (roomForEndMark(lastFrac, 1.0))
                drawText(p, kArcA - 16.0, 1.0, numText(m_powerFullScale), fg, smallPx);
        }
        drawText(p, kArcA - 16.0, -0.045, cream ? "PWR" : "Po", fg, smallPx * (cream ? 0.85 : 1.0));
        drawText(p, kArcA - 16.0, 1.04, "W", fg, smallPx);
    }

    //
    // Arc B: SWR above, Id below
    //

    drawArc(p, kArcB, 0.0, 1.0, ink(c.scale, ScaleSwr | ScaleId), 1.4);

    {
        const QColor fg = ink(c.scale, ScaleSwr);
        const struct { double v; const char *t; } swrMarks[] = {
            {1.0, "1"}, {1.5, "1.5"}, {2.0, "2"}, {3.0, "3"}, {5.0, "5"},
        };
        for (const auto &m : swrMarks) {
            drawTick(p, kArcB, fracSwr(m.v), 6.0, fg, 1.6);
            drawText(p, kArcB + 15.0, fracSwr(m.v), m.t, fg, smallPx);
        }
        drawTick(p, kArcB, 1.0, 6.0, fg, 1.6);
        drawText(p, kArcB + 15.0, 1.0, QString(QChar(0x221E)), fg, bigPx);
        drawText(p, kArcB + 14.0, -0.05, "SWR", fg, smallPx * 0.9);
    }

    {
        const QColor fg = ink(c.scale, ScaleId);
        const double idStep = m_currentFullScale > 30 ? 10.0 : 5.0;
        double lastFrac = 0.0;
        for (double a = 0; a <= m_currentFullScale + 0.01; a += idStep) {
            drawTick(p, kArcB, fracId(a), -5.0, fg, 1.4);
            drawText(p, kArcB - 14.0, fracId(a), numText(a), fg, smallPx);
            lastFrac = fracId(a);
        }
        if (lastFrac < 1.0 - 1e-6) {
            drawTick(p, kArcB, 1.0, -5.0, fg, 1.4);
            if (roomForEndMark(lastFrac, 1.0))
                drawText(p, kArcB - 14.0, 1.0, numText(m_currentFullScale), fg, smallPx);
        }
        drawText(p, kArcB - 14.0, -0.04, cream ? "ID" : "Id", fg, smallPx);
        drawText(p, kArcB - 14.0, 1.04, "A", fg, smallPx);
    }

    //
    // Arc C: COMP band, ALC zone, Vd segment
    //

    {
        const QColor acc = ink(c.accent, ScaleComp);
        drawArc(p, kArcC + 2.0, fracComp(0), fracComp(30), acc, cream ? 2.0 : 4.0);
        for (int db = 0; db <= 30; db += 10) {
            drawTick(p, kArcC + 4.0, fracComp(db), 5.0, acc, 1.6);
            if (db > 0)
                drawText(p, kArcC + 16.0, fracComp(db), QString::number(db), acc, smallPx);
        }
        drawText(p, kArcC + 16.0, fracComp(30) + 0.07, "dB", acc, smallPx);
        drawText(p, kArcC + 16.0, 0.0, "COMP", acc, smallPx * 0.85);
    }

    {
        const QColor hi = ink(c.high, ScaleAlc);
        drawArc(p, kArcC - 4.0, fracAlc(0), fracAlc(1), hi, 5.0);
        drawText(p, kArcC - 17.0, fracAlc(0.5), "ALC", hi, smallPx * 0.9);
    }

    {
        const QColor fg = ink(c.scale, ScaleVd);
        drawArc(p, kArcC, fracVd(m_vdLow), fracVd(m_vdHigh), fg, 2.0);
        drawTick(p, kArcC, fracVd(m_vdLow), -6.0, fg, 1.6);
        drawTick(p, kArcC, fracVd(m_vdHigh), -6.0, fg, 1.6);
        if (m_vdRed > m_vdLow && m_vdRed < m_vdHigh)
            drawTick(p, kArcC, fracVd(m_vdRed), -6.0, ink(c.high, ScaleVd), 2.0);
        drawText(p, kArcC - 15.0, fracVd(m_vdLow), numText(m_vdLow), fg, smallPx);
        drawText(p, kArcC - 15.0, fracVd(m_vdHigh), QString("%1 V").arg(numText(m_vdHigh)), fg, smallPx);
        drawText(p, kArcC - 15.0, fracVd(m_vdLow) - 0.1, cream ? "VD" : "Vd", fg, smallPx);
    }
}

void AnalogMeterItem::drawNeedleDynamic(QPainter *p, const Palette &c) const
{
    const Channel &primary = m_ch[0];
    const Channel &secondary = m_ch[1];

    if (m_peakHold && primary.type != meterNone && primary.peak > primary.pos + 0.005) {
        QColor pk = c.needle;
        pk.setAlphaF(0.45);
        p->save();
        p->translate(kPivotX, kPivotY);
        p->rotate(angleFor(primary.peak));
        p->setPen(QPen(pk, 2.0, Qt::SolidLine, Qt::FlatCap));
        p->drawLine(QPointF(0, -(kArcA - 8.0)), QPointF(0, -kNeedleOuter));
        p->restore();
    }

    const bool darkFace = c.face.lightnessF() < 0.5;
    const QColor shadow = darkFace ? QColor(0, 0, 0, 140) : QColor(60, 40, 0, 70);
    p->save();
    p->translate(2.0, 2.0);
    drawNeedle(p, primary.pos, shadow, 3.0);
    p->restore();
    drawNeedle(p, primary.pos, c.needle, 2.0);

    if (secondary.type != meterNone)
        drawDot(p, dotRadiusFor(secondary.type), secondary.pos, dotColor(c));

    drawTxLamp(p, c.high, true);
}

//
// LCD faces
//

AnalogMeterItem::LcdLayout AnalogMeterItem::lcdLayout() const
{
    LcdLayout l;
    l.dual = m_ch[1].type != meterNone;

    if (isArc()) {
        // Radii from the shared pivot. All segments are the same size.
        if (l.dual) {
            l.topRow = 316.0;
            l.primary = { 284.0, 18.0, 18.0 };
            l.midRow = 270.0;
            l.secondary = { 236.0, 18.0, 18.0 };
            l.bottomRow = 220.0;
        } else {
            l.topRow = 302.0;
            l.primary = { 248.0, 24.0, 24.0 };
            l.midRow = 228.0;
        }
    } else {
        // Flat bar: segment height ramps up left to right.
        if (l.dual) {
            l.topRow = 30.0;
            l.primary = { 72.0, 8.0, 26.0 };
            l.midRow = 88.0;
            l.secondary = { 128.0, 8.0, 24.0 };
            l.bottomRow = 146.0;
        } else {
            l.topRow = 46.0;
            l.primary = { 98.0, 10.0, 32.0 };
            l.midRow = 120.0;
        }
    }
    return l;
}

double AnalogMeterItem::lcdX(double frac) const
{
    return kLcdX0 + clamp01(frac) * (kLcdX1 - kLcdX0);
}

AnalogMeterItem::MarkList AnalogMeterItem::lcdMarks(meter_t type, QString *legend) const
{
    MarkList marks;
    QString unit;
    legend->clear();

    switch (type) {
    case meterS:
    case meterSubS:
        // "S" is the first label, so no legend.
        marks.append({0.0, "S"});
        for (int s = 1; s <= 9; s += 2)
            marks.append({fracS(-54.0 + 6.0 * s), QString::number(s)});
        marks.append({fracS(20), "20"});
        marks.append({fracS(40), "40"});
        marks.append({1.0, "60dB"});
        return marks;
    case meterPower:
        *legend = "PWR";
        unit = "W";
        marks.append({0.0, QString()});
        for (double w : {10.0, 25.0, 50.0, 100.0, 150.0, 200.0, 500.0, 1000.0}) {
            if (w <= m_powerFullScale + 0.01)
                marks.append({fracPo(w), numText(w)});
        }
        if (marks.last().first < 1.0 - 1e-6 && roomForEndMark(marks.last().first, 1.0))
            marks.append({1.0, numText(m_powerFullScale)});
        break;
    case meterSWR:
        *legend = "SWR";
        for (double s : {1.0, 1.5, 2.0, 3.0})
            marks.append({fracSwr(s), numText(s)});
        marks.append({1.0, QString(QChar(0x221E))});
        break;
    case meterALC:
        // Drawn as a bracket by drawLcdScale.
        *legend = "ALC";
        break;
    case meterComp:
        *legend = "COMP";
        unit = "dB";
        for (int d = 0; d <= 30; d += 10)
            marks.append({fracComp(d), QString::number(d)});
        break;
    case meterCurrent:
        *legend = "ID";
        unit = "A";
        for (double a = 0; a <= m_currentFullScale + 0.01; a += (m_currentFullScale > 30 ? 10 : 5))
            marks.append({fracId(a), numText(a)});
        if (marks.last().first < 1.0 - 1e-6 && roomForEndMark(marks.last().first, 1.0))
            marks.append({1.0, numText(m_currentFullScale)});
        break;
    case meterVoltage:
        *legend = "VD";
        unit = "V";
        marks.append({fracVd(m_vdLow), numText(m_vdLow)});
        marks.append({fracVd(m_vdHigh), numText(m_vdHigh)});
        break;
    default:
        // Placeholder: Center, audio and dBu/dBm meters have no printed scale yet.
        break;
    }
    if (!unit.isEmpty() && !marks.isEmpty())
        marks.last().second += " " + unit;
    return marks;
}

void AnalogMeterItem::drawLcdScale(QPainter *p, meter_t type, double row, const QColor &fg, double px) const
{
    if (type == meterNone)
        return;

    QString legend;
    const MarkList marks = lcdMarks(type, &legend);

    if (type == meterALC) {
        // ALC zone as a bracket.
        p->setPen(QPen(fg, 1.4));
        if (isArc()) {
            drawArc(p, row, fracAlc(0), fracAlc(1), fg, 1.4);
            drawTick(p, row, fracAlc(0), 6.0, fg, 1.4);
            drawTick(p, row, fracAlc(1), 6.0, fg, 1.4);
        } else {
            const double x0 = lcdX(fracAlc(0));
            const double x1 = lcdX(fracAlc(1));
            p->drawLine(QPointF(x0, row - 6), QPointF(x0, row));
            p->drawLine(QPointF(x0, row), QPointF(x1, row));
            p->drawLine(QPointF(x1, row), QPointF(x1, row - 6));
        }
    } else if (isArc()) {
        drawLcdArcRow(p, row, marks, fg, px);
    } else {
        drawLcdRow(p, row, marks, fg, px);
    }

    if (legend.isEmpty())
        return;
    if (isArc()) {
        drawText(p, row, -0.07, legend, fg, px * 0.8);
    } else {
        QFont f = p->font();
        f.setPixelSize(int(px * 0.8));
        p->setFont(f);
        p->setPen(fg);
        p->drawText(QRectF(0, row - 9, kLcdX0 - 6, 18), Qt::AlignRight | Qt::AlignVCenter, legend);
    }
}

void AnalogMeterItem::drawLcdRow(QPainter *p, double y, const MarkList &labels, const QColor &fg, double px) const
{
    // Labels joined by line pieces, like "S-1-3-5-7-9 — 20 — 40 — 60dB".
    QFont f = p->font();
    f.setPixelSize(int(px));
    p->setFont(f);
    const QFontMetricsF fm(f);

    QList<QRectF> rects;
    for (const auto &l : labels) {
        const double w = fm.horizontalAdvance(l.second);
        rects.append(QRectF(lcdX(l.first) - w / 2.0, y - px / 2.0 - 1, w, px + 2));
    }

    p->setPen(QPen(fg, 1.4));
    for (int i = 1; i < rects.size(); ++i) {
        const double xa = rects[i - 1].right() + 4.0;
        const double xb = rects[i].left() - 4.0;
        if (xb > xa)
            p->drawLine(QPointF(xa, y), QPointF(xb, y));
    }

    p->setPen(fg);
    for (int i = 0; i < rects.size(); ++i)
        p->drawText(rects[i].adjusted(-4, 0, 4, 0), Qt::AlignCenter, labels[i].second);
}

void AnalogMeterItem::drawLcdArcRow(QPainter *p, double r, const MarkList &labels, const QColor &fg, double px) const
{
    // Arc version of drawLcdRow: labels on the arc, joined by arc pieces.
    QFont f = p->font();
    f.setPixelSize(int(px));
    const QFontMetricsF fm(f);
    const double fracPerUnit = 1.0 / (r * qDegreesToRadians(2.0 * kSweepDeg));

    QList<double> half;
    for (const auto &l : labels)
        half.append((fm.horizontalAdvance(l.second) / 2.0 + 4.0) * fracPerUnit);

    for (int i = 1; i < labels.size(); ++i) {
        const double fa = labels[i - 1].first + half[i - 1];
        const double fb = labels[i].first - half[i];
        if (fb > fa)
            drawArc(p, r, fa, fb, fg, 1.4);
    }
    for (const auto &l : labels)
        drawText(p, r, l.first, l.second, fg, px);
}

void AnalogMeterItem::drawLcdSegment(QPainter *p, const LcdBand &band, int i, const QColor &c) const
{
    const double h = band.hMin + (band.hMax - band.hMin) * i / (kLcdSegments - 1);

    if (!isArc()) {
        const double pitch = (kLcdX1 - kLcdX0) / kLcdSegments;
        p->fillRect(QRectF(kLcdX0 + i * pitch + 1, band.base - h, pitch - 3, h), c);
        return;
    }

    // Every arc segment is the same rectangle, rotated into place.
    const double stepRad = qDegreesToRadians(2.0 * kSweepDeg) / kLcdSegments;
    const double gap = 2.5;
    const double w = (band.base + h / 2.0) * stepRad - gap;

    p->save();
    p->translate(kPivotX, kPivotY);
    p->rotate(angleFor((i + 0.5) / kLcdSegments));
    p->fillRect(QRectF(-w / 2.0, -(band.base + h), w, h), c);
    p->restore();
}

void AnalogMeterItem::drawLcdBand(QPainter *p, const LcdBand &band, int lit, int peakSeg, const QColor &c) const
{
    for (int i = 0; i < kLcdSegments; ++i) {
        const bool on = i < lit || (i == peakSeg && peakSeg >= lit);
        if (on)
            drawLcdSegment(p, band, i, c);
    }
}

void AnalogMeterItem::drawLcdFace(QPainter *p, const Palette &c) const
{
    const QColor &fg = c.scale;
    const double px = 15.0;
    const LcdLayout l = lcdLayout();
    const meter_t primary = m_ch[0].type;

    //
    // Primary band: S above and PWR below, as on the classic LCD, unless
    // the primary is some other meter, which then gets the upper row.
    //

    const bool sPoPair = primary == meterNone || primary == meterS || primary == meterSubS || primary == meterPower;
    if (sPoPair) {
        drawLcdScale(p, meterS, l.topRow, ink(fg, ScaleS), px);
        drawLcdScale(p, meterPower, l.midRow, ink(fg, ScalePo), px);
    } else {
        drawLcdScale(p, primary, l.topRow, fg, px);
    }

    //
    // Secondary band, scale below it
    //

    if (l.dual)
        drawLcdScale(p, m_ch[1].type, l.bottomRow, fg, px);

    //
    // Unlit segment ghosts, as on a real LCD
    //

    QColor ghost = fg;
    ghost.setAlphaF(0.07);
    for (int i = 0; i < kLcdSegments; ++i) {
        drawLcdSegment(p, l.primary, i, ghost);
        if (l.dual)
            drawLcdSegment(p, l.secondary, i, ghost);
    }
}

void AnalogMeterItem::drawLcdDynamic(QPainter *p, const Palette &c) const
{
    const LcdLayout l = lcdLayout();
    const Channel &primary = m_ch[0];
    const Channel &secondary = m_ch[1];

    if (primary.type != meterNone) {
        const int lit = int(std::round(primary.pos * kLcdSegments));
        const int peakSeg = m_peakHold ? int(std::round(primary.peak * kLcdSegments)) - 1 : -1;
        drawLcdBand(p, l.primary, lit, peakSeg, c.scale);
    }
    if (l.dual) {
        const int lit = int(std::round(secondary.pos * kLcdSegments));
        drawLcdBand(p, l.secondary, lit, -1, c.scale);
    }

    drawTxLamp(p, c.scale, false);
}
