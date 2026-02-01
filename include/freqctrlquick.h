#ifndef FREQCTRLQUICK_H
#define FREQCTRLQUICK_H

/*
 * Frequency controller item for Qt Quick (ported from the QWidget version)
 */

#include <QApplication>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QPainter>
#include <QColor>
#include <QFont>
#include <QRect>
#include <QVector>
#include <QPointF>
#include <QPixmap>
#include <QSize>
#include <QFontDatabase>

#include "rigidentities.h"
#include <wfviewtypes.h>

enum FctlUnit {
    FCTL_UNIT_NONE,
    FCTL_UNIT_HZ,
    FCTL_UNIT_KHZ,
    FCTL_UNIT_MHZ,
    FCTL_UNIT_GHZ,
    FCTL_UNIT_SEC,
    FCTL_UNIT_MSEC,
    FCTL_UNIT_USEC,
    FCTL_UNIT_NSEC
};

#define FCTL_MAX_DIGITS 12
#define FCTL_MIN_DIGITS 4

class FreqCtrlQuick : public QQuickPaintedItem
{
    Q_OBJECT

    // QML-visible properties
    Q_PROPERTY(qint64 frequency READ getFrequency WRITE setFrequencyExt NOTIFY newFrequency) // Use setFrequencyExt so it doesn't trigger a signal.
    Q_PROPERTY(qint64 minFrequency READ getMinFrequency WRITE setMinFrequency NOTIFY rangeChanged)
    Q_PROPERTY(qint64 maxFrequency READ getMaxFrequency WRITE setMaxFrequency NOTIFY rangeChanged)
    Q_PROPERTY(bool resetLowerDigits READ getResetLowerDigits WRITE setResetLowerDigits NOTIFY resetLowerDigitsChanged)
    Q_PROPERTY(bool invertScrolling READ getInvertScrolling WRITE setInvertScrolling NOTIFY invertScrollingChanged)
    Q_PROPERTY(FctlUnit unit READ getUnit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(QColor digitColor READ getDigitColor WRITE setDigitColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor backgroundColor READ getBgColor WRITE setBgColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor unitsColor READ getUnitsColor WRITE setUnitsColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor highlightColor READ getHighlightColor WRITE setHighlightColor NOTIFY colorsChanged)

    Q_PROPERTY(int freqDigits READ getFreqDigits WRITE setFreqDigits NOTIFY freqDigitsChanged)
    Q_PROPERTY(qint64 freqMinStep READ getFreqMinStep WRITE setFreqMinStep NOTIFY freqMinStepChanged)
    Q_PROPERTY(QChar gsep READ getGsep WRITE setGsep NOTIFY gsepChanged)
    Q_PROPERTY(QChar dsep READ getDsep WRITE setDsep NOTIFY dsepChanged)

public:
    explicit FreqCtrlQuick();
    ~FreqCtrlQuick() override;

    // Use NumDigits=0 for auto
    Q_INVOKABLE void setup(int NumDigits,
                           qint64 Minf,
                           qint64 Maxf,
                           int MinStep,
                           FctlUnit unit,
                           std::vector<bandType> *bands = nullptr);

    // Properties / API
    qint64 getFrequency() const { return m_freq; }
    qint64 getMinFrequency() const { return m_MinFreq; }
    qint64 getMaxFrequency() const { return m_MaxFreq; }

    int getFreqDigits() const { return m_NumDigits; }

    void setFreqDigits(int f) {
        if (!f) {
            if (m_MaxFreq < 10e6)
                m_NumDigits = 7;
            else if (m_MaxFreq < 100e6)
                m_NumDigits = 8;
            else if (m_MaxFreq < 1e9)
                m_NumDigits = 9;
            else if (m_MaxFreq < 10e9)
                m_NumDigits = 10;
            else if (m_MaxFreq < 100e9)
                m_NumDigits = 11;
        } else {
            m_NumDigits = (f < FCTL_MIN_DIGITS) ? FCTL_MIN_DIGITS : (f > FCTL_MAX_DIGITS) ? FCTL_MAX_DIGITS : f;
        }
        emit freqDigitsChanged();
    }
    qint64 getFreqMinStep() const { return m_MaxFreq; }

    void setFreqMinStep(qint64 s)
    {
        if (s == 0)
            m_MinStep = 1;
        else
            m_MinStep = s;
        emit freqMinStepChanged();
    }

    bool getResetLowerDigits() const { return m_ResetLowerDigits; }
    bool getInvertScrolling() const { return m_InvertScrolling; }

    FctlUnit getUnit() const { return m_Unit; }

    QColor getDigitColor() const { return m_DigitColor; }
    QColor getBgColor() const { return m_BkColor; }
    QColor getUnitsColor() const { return m_UnitsColor; }
    QColor getHighlightColor() const { return m_HighlightColor; }

    void setUnit(FctlUnit unit);
    void setDigitColor(const QColor &col);
    void setBgColor(const QColor &col);
    void setUnitsColor(const QColor &col);
    void setHighlightColor(const QColor &col);

    // group and decimal separators (e.g. ',' and '.')
    Q_INVOKABLE void setSeparators(QChar group, QChar decimal)
    {
        gsep = group;
        dsep = decimal;
        m_UpdateAll = true;
        update();        // trigger repaint in Qt Quick        
    }

    void setDsep( QChar d) { dsep = d;m_UpdateAll = true; update(); emit dsepChanged();}
    void setGsep( QChar g) { gsep = g;m_UpdateAll = true; update(); emit gsepChanged();}
    QChar getDsep() const { return dsep;}
    QChar getGsep() const { return gsep;}

    void setResetLowerDigits(bool reset) { m_ResetLowerDigits = reset; }
    void setInvertScrolling(bool invert) { m_InvertScrolling = invert; }

signals:
    void newFrequency(qint64 freq);     // emitted when frequency has changed
    void rangeChanged();
    void resetLowerDigitsChanged();
    void invertScrollingChanged();
    void unitChanged();
    void colorsChanged();
    void freqDigitsChanged();
    void dsepChanged();
    void gsepChanged();
    void freqMinStepChanged();

public slots:
    void setFrequency(qint64 freq);
    void setFrequencyExt(qint64 freq);
    void setFrequencyFocus();           // optional: to be wired from QML

    void setMinFrequency(qint64 f);
    void setMaxFrequency(qint64 f);

    // QQuickPaintedItem interface
public:
    void paint(QPainter *p) override;

protected:
    // Event handlers ported from QWidget version
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void geometryChange(const QRectF &newGeometry,
                        const QRectF &oldGeometry) override;
#else
    void geometryChanged(const QRectF &newGeometry,
                         const QRectF &oldGeometry) override;
#endif

    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateCtrl(bool all);
    void drawBkGround(QPainter &Painter);
    void drawDigits(QPainter &Painter);
    void incDigit();
    void decDigit();
    void incFreq();
    void decFreq();
    void clearFreq();
    void cursorHome();
    void cursorEnd();
    void moveCursorLeft();
    void moveCursorRight();
    bool inRect(QRect &rect, QPointF &point);
    void setActiveDigit(int idx);

    bool        m_UpdateAll = true;
    bool        m_ExternalKeyActive = false;
    bool        m_LRMouseFreqSel = false;

    bool        m_ResetLowerDigits = false;
    bool        m_InvertScrolling = false;

    int         m_FirstEditableDigit = 0;
    int         m_LastLeadZeroPos = 0;
    int         m_LeadZeroPos = 0;
    int         m_NumDigits = 0;
    int         m_NumDigitsForUnit = 0;
    int         m_DigStart = 0;
    int         m_ActiveEditDigit = 0;
    int         m_LastEditDigit = 0;
    int         m_DecPos = -1;
    int         m_NumSeps = 0;

    int         scrollYperClick = 24;
    int         scrollXperClick = 24;
    qreal       scrollWheelOffsetAccumulated = 0.0;

    qint64      m_MinStep = 1;
    qint64      m_freq = 0;
    qint64      m_Oldfreq = 0;
    qint64      m_MinFreq = 0;
    qint64      m_MaxFreq = 0;

    QColor      m_DigitColor = Qt::white;
    QColor      m_BkColor = Qt::black;
    QColor      m_InactiveColor = Qt::gray;
    QColor      m_UnitsColor = Qt::white;
    QColor      m_HighlightColor = Qt::darkBlue;

    FctlUnit    m_Unit = FCTL_UNIT_HZ;

    QRect       m_rectCtrl;                 // main control rectangle
    QRect       m_UnitsRect;                // rectangle where Units text goes
    QRect       m_SepRect[FCTL_MAX_DIGITS]; // separation rectangles

    QString     m_UnitString;

    QFont       m_DigitFont;
    QFont       m_UnitsFont;

    QChar       gsep = ' ';
    QChar       dsep = ' ';

    QPixmap     m_Pixmap;
    QSize       m_Size;

    std::vector<bandType> *m_Bands = nullptr;

    struct DigStuct {
        qint64    weight = 0;
        qint64    incval = 0;
        QRect     dQRect;
        int       val = 0;
        bool      modified = false;
        bool      editmode = false;
    } m_DigitInfo[FCTL_MAX_DIGITS];

};

#endif // FREQCTRLQUICK_H
