#pragma once

#include "wfviewtypes.h" // meter_t

#include <QQuickPaintedItem>
#include <QPainter>
#include <QImage>
#include <QColor>
#include <QTimer>
#include <QElapsedTimer>
#include <QList>
#include <QPair>
#include <QVariantList>

#include <memory>

//
// Vintage-style meter with selectable faces:
//   Analog      - black multi-arc needle meter
//   AnalogCream - cream-faced needle meter
//   LCD         - amber LCD segment bar
//   LCDArc      - same LCD look, segments laid out on an arc
//
// Two channels, each with its own meter type and value (already calibrated
// by the rig file):
//   Primary   - the needle, or the main LCD band
//   Secondary - a dot riding on its scale arc, or a second LCD band
//
// Scales that neither channel uses are dimmed.
//

class AnalogMeterItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(int style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(int primaryType READ primaryType WRITE setPrimaryType NOTIFY primaryTypeChanged)
    Q_PROPERTY(double primaryValue READ primaryValue WRITE setPrimaryValue NOTIFY levelsChanged)
    Q_PROPERTY(int secondaryType READ secondaryType WRITE setSecondaryType NOTIFY secondaryTypeChanged)
    Q_PROPERTY(double secondaryValue READ secondaryValue WRITE setSecondaryValue NOTIFY levelsChanged)
    Q_PROPERTY(bool transmitting READ transmitting WRITE setTransmitting NOTIFY transmittingChanged)
    Q_PROPERTY(bool peakHold READ peakHold WRITE setPeakHold NOTIFY peakHoldChanged)
    Q_PROPERTY(QString caption READ caption WRITE setCaption NOTIFY captionChanged)

    // Candidate colours for the secondary dot; the one with the best
    // contrast against the face is used.
    Q_PROPERTY(QVariantList dotColors READ dotColors WRITE setDotColors NOTIFY dotColorsChanged)

    Q_PROPERTY(int attackMs READ attackMs WRITE setAttackMs NOTIFY ballisticsChanged)
    Q_PROPERTY(int decayMs READ decayMs WRITE setDecayMs NOTIFY ballisticsChanged)

    Q_PROPERTY(double powerFullScale READ powerFullScale WRITE setPowerFullScale NOTIFY scaleChanged)
    Q_PROPERTY(double currentFullScale READ currentFullScale WRITE setCurrentFullScale NOTIFY scaleChanged)
    Q_PROPERTY(double vdLow READ vdLow WRITE setVdLow NOTIFY scaleChanged)
    Q_PROPERTY(double vdHigh READ vdHigh WRITE setVdHigh NOTIFY scaleChanged)
    // Nominal supply voltage, marked on the Vd scale; 0 = no mark.
    Q_PROPERTY(double vdRed READ vdRed WRITE setVdRed NOTIFY scaleChanged)

public:
    enum Style
    {
        StyleAnalog = 0,
        StyleAnalogCream = 1,
        StyleLCD = 2,
        StyleLCDArc = 3
    };
    Q_ENUM(Style)

    explicit AnalogMeterItem(QQuickItem *parent = nullptr);
    void paint(QPainter *p) override;

    int style() const { return int(m_style); }
    void setStyle(int s);

    int primaryType() const { return int(m_ch[0].type); }
    void setPrimaryType(int t) { setChannelType(0, t); }
    double primaryValue() const { return m_ch[0].value; }
    void setPrimaryValue(double v) { setChannelValue(0, v); }

    int secondaryType() const { return int(m_ch[1].type); }
    void setSecondaryType(int t) { setChannelType(1, t); }
    double secondaryValue() const { return m_ch[1].value; }
    void setSecondaryValue(double v) { setChannelValue(1, v); }

    bool transmitting() const { return m_transmitting; }
    void setTransmitting(bool tx);

    bool peakHold() const { return m_peakHold; }
    void setPeakHold(bool on);

    QString caption() const { return m_caption; }
    void setCaption(const QString &c);

    QVariantList dotColors() const { return m_dotColors; }
    void setDotColors(const QVariantList &colors);

    int attackMs() const { return m_attackMs; }
    void setAttackMs(int ms);
    int decayMs() const { return m_decayMs; }
    void setDecayMs(int ms);

    double powerFullScale() const { return m_powerFullScale; }
    void setPowerFullScale(double w);
    double currentFullScale() const { return m_currentFullScale; }
    void setCurrentFullScale(double a);
    double vdLow() const { return m_vdLow; }
    void setVdLow(double v);
    double vdHigh() const { return m_vdHigh; }
    void setVdHigh(double v);
    double vdRed() const { return m_vdRed; }
    void setVdRed(double v);

    // Range for meter types without a printed scale (channel 0 or 1).
    // Placeholder for Center, audio and dBu/dBm meters, which no face has a scale for yet.
    Q_INVOKABLE void setMeterExtremities(int channel, double min, double max);
    Q_INVOKABLE void clearMeterExtremities(int channel);
    Q_INVOKABLE void clearMeter();

signals:
    void styleChanged();
    void primaryTypeChanged();
    void secondaryTypeChanged();
    void levelsChanged();
    void transmittingChanged();
    void peakHoldChanged();
    void captionChanged();
    void dotColorsChanged();
    void ballisticsChanged();
    void scaleChanged();

private:
    //
    // Per-style palette
    //

    struct Palette
    {
        QColor face;
        QColor faceEdge;
        QColor scale;
        QColor high;
        QColor accent;
        QColor needle;
    };
    Palette palette() const;

    //
    // One meter channel: type, value and ballistics state
    //

    struct Channel
    {
        meter_t type = meterNone;
        double value = 0.0;
        double target = 0.0; // 0.0 = left stop, 1.0 = right stop
        double pos = 0.0;
        double peak = 0.0;
        qint64 peakSetAt = 0;
        bool haveExtremities = false;
        double extMin = 0.0;
        double extMax = 255.0;
    };

    void setChannelType(int ch, int t);
    void setChannelValue(int ch, double v);
    void startAnimation();

    //
    // Scales, for dimming the ones not in use
    //

    enum Scale : unsigned
    {
        ScaleNone = 0,
        ScaleS = 1u << 0,
        ScalePo = 1u << 1,
        ScaleSwr = 1u << 2,
        ScaleId = 1u << 3,
        ScaleComp = 1u << 4,
        ScaleAlc = 1u << 5,
        ScaleVd = 1u << 6
    };
    static unsigned scaleFor(meter_t type);
    unsigned activeScales() const;
    QColor ink(const QColor &c, unsigned scale) const;

    //
    // Value to position (0.0 = left stop, 1.0 = right stop)
    //

    double fractionFor(const Channel &ch) const;
    double fracS(double dbOverS9) const;
    double fracPo(double watts) const;
    double fracSwr(double swr) const;
    double fracId(double amps) const;
    double fracComp(double db) const;
    double fracAlc(double alc) const;
    double fracVd(double volts) const;
    // Full-scale value as a scale mark, unless it would crowd the last mark.
    static bool roomForEndMark(double lastFrac, double endFrac) { return endFrac - lastFrac >= 0.12; }

    //
    // Needle faces (Analog, AnalogCream)
    //

    void drawNeedleFace(QPainter *p, const Palette &c) const;
    void drawNeedleDynamic(QPainter *p, const Palette &c) const;
    void drawArc(QPainter *p, double r, double f0, double f1, const QColor &c, double width) const;
    void drawTick(QPainter *p, double r, double frac, double len, const QColor &c, double width) const;
    void drawText(QPainter *p, double r, double frac, const QString &txt, const QColor &c, double px,
                  bool bold = false) const;
    void drawNeedle(QPainter *p, double frac, const QColor &c, double width) const;
    void drawDot(QPainter *p, double r, double frac, const QColor &c) const;
    static double dotRadiusFor(meter_t type);
    QColor dotColor(const Palette &c) const;

    //
    // LCD faces (LCD, LCDArc)
    //

    struct LcdBand
    {
        double base = 0.0; // flat: bar bottom y; arc: inner radius
        double hMin = 0.0; // flat: height of the first segment; arc: same as hMax
        double hMax = 0.0;
    };
    struct LcdLayout
    {
        bool dual = false;
        double topRow = 0.0;    // flat: y; arc: radius
        double midRow = 0.0;
        double bottomRow = 0.0;
        LcdBand primary;
        LcdBand secondary;
    };
    using MarkList = QList<QPair<double, QString>>;

    bool isLcd() const { return m_style == StyleLCD || m_style == StyleLCDArc; }
    bool isArc() const { return m_style == StyleLCDArc; }
    LcdLayout lcdLayout() const;
    double lcdX(double frac) const;
    MarkList lcdMarks(meter_t type, QString *legend) const;
    void drawLcdScale(QPainter *p, meter_t type, double row, const QColor &fg, double px) const;
    void drawLcdFace(QPainter *p, const Palette &c) const;
    void drawLcdDynamic(QPainter *p, const Palette &c) const;
    void drawLcdBand(QPainter *p, const LcdBand &band, int lit, int peakSeg, const QColor &c) const;
    void drawLcdSegment(QPainter *p, const LcdBand &band, int i, const QColor &c) const;
    void drawLcdArcRow(QPainter *p, double r, const MarkList &labels, const QColor &fg, double px) const;
    void drawLcdRow(QPainter *p, double y, const MarkList &labels, const QColor &fg, double px) const;

    //
    // Common
    //

    void markScaleDirty();
    void regenerateFace(int pxW, int pxH);
    void setupTransform(QPainter *p, int pxW, int pxH) const;
    void drawCaption(QPainter *p, const Palette &c) const;
    void drawTxLamp(QPainter *p, const QColor &c, bool filled) const;
    void animate();

    Style m_style = StyleAnalog;
    Channel m_ch[2];
    bool m_transmitting = false;
    bool m_peakHold = true;
    QString m_caption;
    QVariantList m_dotColors;

    int m_attackMs = 40;
    int m_decayMs = 350;
    int m_peakHoldMs = 1000;
    QTimer m_animTimer;
    QElapsedTimer m_clock;
    qint64 m_lastTick = 0;

    double m_powerFullScale = 250.0;
    double m_currentFullScale = 25.0;
    double m_vdLow = 44.0;
    double m_vdHigh = 52.0;
    double m_vdRed = 0.0;

    std::unique_ptr<QImage> m_face;
    bool m_faceDirty = true;
};
