/*
 * Frequency controller widget (originally from CuteSDR)
 *
 * This file is part of wfview
 *
 * Copyright 2010 Moe Wheatley AE4JY
 * Copyright 2012-2017 Alexandru Csete OZ9AEC
 * Copyright 2024 Phil Taylor M0VSE
 * All rights reserved.
 *
 * This software is released under the "Simplified BSD License".
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <QDebug>
#include <QLocale>
#include <cmath>
#include "freqctrlquick.h"
#include "logcategories.h"

// Manual adjustment of Font size as percent of control height
#define DIGIT_SIZE_PERCENT 90
#define UNITS_SIZE_PERCENT 60

// adjustment for separation between digits
#define SEPRATIO_N 100         // separation rectangle size ratio numerator times 100
#define SEPRATIO_D 3           // separation rectangle size ratio denominator

#define STATUS_TIP \
"Scroll or left-click to increase/decrease digit. " \
    "Right-click to clear digits."

FreqCtrlQuick::FreqCtrlQuick()
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemHasContents, true);
    setFlag(ItemIsFocusScope, true);

    //setAutoFillBackground(false);
    //setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    //setFocusPolicy(Qt::StrongFocus);
    //setMouseTracking(true);
    m_BkColor = QColor(0x1F, 0x1D, 0x1D, 0xFF);
    m_InactiveColor = QColor(0x43, 0x43, 0x43, 0xFF);
    m_DigitColor = QColor(0xFF, 0xFF, 0xFF, 0xFF);
    m_HighlightColor = QColor(0x5A, 0x5A, 0x5A, 0xFF);
    m_UnitsColor = Qt::gray;
    m_freq = 146123456;
    setup(0, 1, 4000000000U, 1, FCTL_UNIT_NONE);
    m_Oldfreq = 0;
    m_LastLeadZeroPos = 0;
    m_LRMouseFreqSel = false;
    m_ActiveEditDigit = -1;
    m_ResetLowerDigits = true;
    m_InvertScrolling = false;
    int fontid = QFontDatabase::addApplicationFont(":/resources/frequency.ttf");
    QString font = QFontDatabase::applicationFontFamilies(fontid).at(0);

    m_UnitsFont  = QFont(font,12,QFont::Normal);
    m_DigitFont  = QFont(font,12,QFont::Normal);
    //m_UnitsFont = QFont("Arial", 12, QFont::Normal);
    //m_DigitFont = QFont("Arial", 12, QFont::Normal);

    //setStatusTip(tr(STATUS_TIP));
}

FreqCtrlQuick::~FreqCtrlQuick()
{
}

/*
QSize FreqCtrlQuick::minimumSizeHint() const
{
    return QSize(100, 20);
}

QSize FreqCtrlQuick::sizeHint() const
{
    return QSize(100, 20);
}
*/
bool FreqCtrlQuick::inRect(QRect &rect, QPointF &point)
{
    if ((point.x() < rect.right()) && (point.x() > rect.x()) &&
        (point.y() < rect.bottom()) && (point.y() > rect.y()))
        return true;
    else
        return false;
}

void FreqCtrlQuick::setActiveDigit(int idx)
{
    for (int i = m_DigStart; i < m_NumDigits; i++)
    {
        if (i == idx)
        {
            if (!m_DigitInfo[i].editmode)
            {
                m_DigitInfo[i].editmode = true;
                m_ActiveEditDigit = i;
            }
        }
        else
        {
            // un-highlight the previous digit if moved off it
            if (m_DigitInfo[i].editmode)
            {
                m_DigitInfo[i].editmode = false;
                m_DigitInfo[i].modified = true;
            }
        }
    }

    updateCtrl(false);

}

static int fmax_to_numdigits(qint64 fmax)
{
    if (fmax < 10e6)
        return 7;
    else if (fmax < 100e6)
        return 8;
    else if (fmax < 1e9)
        return 9;
    else if (fmax < 10e9)
        return 10;
    else if (fmax < 100e9)
        return 11;

    return 12;
}

void FreqCtrlQuick::setup(int NumDigits, qint64 Minf, qint64 Maxf, int MinStep,
                     FctlUnit unit, std::vector<bandType>* bands)
{
    int       i;
    qint64    pwr = 1;
    m_LastEditDigit = 0;
    m_Oldfreq = -1;

    m_NumDigits = NumDigits ? NumDigits : fmax_to_numdigits(Maxf);

    m_Bands = bands;

    if (m_NumDigits > FCTL_MAX_DIGITS)
        m_NumDigits = FCTL_MAX_DIGITS;

    if (m_NumDigits < FCTL_MIN_DIGITS)
        m_NumDigits = FCTL_MIN_DIGITS;

    m_UnitString = "";
    m_MinStep = MinStep;

    if (m_MinStep == 0)
        m_MinStep = 1;

    m_MinFreq = Minf;
    m_MaxFreq = Maxf;

    if (m_freq < m_MinFreq)
        m_freq = m_MinFreq;

    if (m_freq > m_MaxFreq)
        m_freq = m_MaxFreq;

    for (i = 0; i < m_NumDigits; i++)
    {
        m_DigitInfo[i].weight = pwr;
        m_DigitInfo[i].incval = pwr;
        m_DigitInfo[i].modified = true;
        m_DigitInfo[i].editmode = false;
        m_DigitInfo[i].val = 0;
        pwr *= 10;
    }

    if (m_MaxFreq > pwr)
        m_MaxFreq = pwr - 1;

    m_MaxFreq = m_MaxFreq - m_MaxFreq % m_MinStep;

    if (m_MinFreq > pwr)
        m_MinFreq = 1;

    m_MinFreq = m_MinFreq - m_MinFreq % m_MinStep;
    m_DigStart = 0;

    setUnit(unit);

    for (i = m_NumDigits - 1; i >= 0; i--)
    {
        if (m_DigitInfo[i].weight <= m_MinStep)
        {
            if (m_DigStart == 0)
            {
                m_DigitInfo[i].incval = m_MinStep;
                m_DigStart = i;
            }
            else
            {
                if ((m_MinStep % m_DigitInfo[i + 1].weight) != 0)
                    m_DigStart = i;
                m_DigitInfo[i].incval = 0;
            }
        }
    }

    m_NumSeps = (m_NumDigits - 1) / 3 - m_DigStart / 3;

    // At the end of setup(), line 227, add:
    //qInfo() << "After setup: m_NumDigits=" << m_NumDigits << "m_DigStart=" << m_DigStart;
}

void FreqCtrlQuick::setMinFrequency(qint64 freq)
{
    m_MinFreq = freq;
}

void FreqCtrlQuick::setMaxFrequency(qint64 freq)
{
    m_MaxFreq = freq;
}

void FreqCtrlQuick::setFrequency(qint64 freq)
{
    setFrequencyExt(freq);
    emit  newFrequency(m_freq);
}

void FreqCtrlQuick::setFrequencyExt(qint64 freq)
{
    int       i;
    qint64    acc = 0;
    qint64    rem;
    int       val;



    if (freq == m_Oldfreq)
        return;
/*
 * This needs working on, to restrict to only valid bands!
    if (m_Bands != nullptr) {
        // We have bands so make sure the frequency is within at least one of them!
        for (int i=0;i<m_Bands->size();i++)
        {
            if (freq >= m_Bands->at(i).lowFreq && freq <= m_Bands->at(i).highFreq)
            {
                break;
            }

            if (m_Oldfreq >= m_Bands->at(i).lowFreq && m_Oldfreq <= m_Bands->at(i).highFreq)
            {
                // The old frequency WAS in this band but it isn't now!
                if (freq >= m_Bands->at(i).highFreq && i <= m_Bands->size()-1)
                {
                    freq=m_Bands->at(i+1).lowFreq;
                }
                else if (freq < m_Bands->at(i).lowFreq && i > 0)
                {
                    freq=m_Bands->at(i-1).highFreq;
                }
                else
                {
                    // Couldn't match it!
                    freq = m_Oldfreq;
                }
                break;
            }


        }
    } else {
*/
    if (freq < m_MinFreq)
        freq = m_MinFreq;

    if (freq > m_MaxFreq)
        freq = m_MaxFreq;

    m_freq = freq - freq % m_MinStep;
    rem = m_freq;
    m_LeadZeroPos = m_NumDigits;

    //qInfo() << "setFrequencyExt: freq=" << freq << "m_NumDigits=" << m_NumDigits
    //         << "m_DigStart=" << m_DigStart;

    for (i = m_NumDigits - 1; i >= m_DigStart; i--)
    {
        val = (int)(rem / m_DigitInfo[i].weight) % 10;
        if (m_DigitInfo[i].val != val)
        {
            m_DigitInfo[i].val = val;
            m_DigitInfo[i].modified = true;
        }
        rem = rem - val * m_DigitInfo[i].weight;
        acc += val;
        if ((acc == 0) && (i > m_DecPos))
        {
            m_LeadZeroPos = i;
        }
    }

    // If the sign changed and the frequency is less than 1 unit,
    // redraw the leading zero to get the correct sign.
    if ((m_Oldfreq ^ m_freq) < 0 && m_DigitInfo[m_LeadZeroPos - 1].val == 0)
        m_DigitInfo[m_LeadZeroPos - 1].modified = true;

    // When frequency is negative all non-zero digits that
    // have changed will have a negative sign. This loop will
    // change all digits back to positive, except the one at
    // position m_leadZeroPos-1. If that position is zero,
    // it will be checked in the drawing method, drawDigits().
    /** TBC if this works for all configurations */
    if (m_freq < 0)
    {
        if (m_DigitInfo[m_LeadZeroPos - 1].val > 0)
            m_DigitInfo[m_LeadZeroPos -
                        1].val = -m_DigitInfo[m_LeadZeroPos - 1].val;

        for (i = 0; i < (m_LeadZeroPos - 1); i++)
        {
            if (m_DigitInfo[i].val < 0)
                m_DigitInfo[i].val = -m_DigitInfo[i].val;
        }
    }

    // signal the new frequency to world
    m_Oldfreq = m_freq;
    updateCtrl(m_LastLeadZeroPos != m_LeadZeroPos);
    m_LastLeadZeroPos = m_LeadZeroPos;
}

void FreqCtrlQuick::setDigitColor(const QColor &col)
{
    m_UpdateAll = true;
    m_DigitColor = col;
    for (int i = m_DigStart; i < m_NumDigits; i++)
        m_DigitInfo[i].modified = true;
    updateCtrl(true);
}

void FreqCtrlQuick::setUnit(FctlUnit unit)
{
    m_NumDigitsForUnit = 2;
    switch (unit)
    {
    case FCTL_UNIT_NONE:
        m_NumDigitsForUnit = 0;
        m_DecPos = 0;
        m_UnitString = QString();
        break;
    case FCTL_UNIT_HZ:
        m_DecPos = 0;
        m_UnitString = "Hz ";
        break;
    case FCTL_UNIT_KHZ:
        m_DecPos = 3;
        m_UnitString = "kHz";
        break;
    case FCTL_UNIT_MHZ:
        m_DecPos = 6;
        m_UnitString = "MHz";
        break;
    case FCTL_UNIT_GHZ:
        m_DecPos = 9;
        m_UnitString = "GHz";
        break;
    case FCTL_UNIT_SEC:
        m_DecPos = 6;
        m_UnitString = "Sec";
        break;
    case FCTL_UNIT_MSEC:
        m_DecPos = 3;
        m_UnitString = "mS ";
        break;
    case FCTL_UNIT_USEC:
        m_DecPos = 0;
        m_UnitString = "uS ";
        break;
    case FCTL_UNIT_NSEC:
        m_DecPos = 0;
        m_UnitString = "nS ";
        break;
    }
    m_Unit = unit;
    m_UpdateAll = true;
    updateCtrl(true);
    emit unitChanged();
}

void FreqCtrlQuick::setBgColor(const QColor& col)
{
    m_UpdateAll = true;
    m_BkColor = col;

    for (int i = m_DigStart; i < m_NumDigits; i++)
        m_DigitInfo[i].modified = true;

    updateCtrl(true);
    emit colorsChanged();
}

void FreqCtrlQuick::setUnitsColor(const QColor& col)
{
    m_UpdateAll = true;
    m_UnitsColor = col;
    updateCtrl(true);
    emit unitChanged();
}

void FreqCtrlQuick::setHighlightColor(const QColor& col)
{
    m_UpdateAll = true;
    m_HighlightColor = col;
    updateCtrl(true);
    emit colorsChanged();
}

void FreqCtrlQuick::updateCtrl(bool all)
{
    if (all)
    {
        m_UpdateAll = true;
        for (int i = m_DigStart; i < m_NumDigits; i++)
            m_DigitInfo[i].modified = true;
    }
    update();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void FreqCtrlQuick::geometryChange(const QRectF &newGeometry,
                                   const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);

    qreal dpr = window() ? window()->devicePixelRatio() : qreal(1.0);
    m_Pixmap = QPixmap(int(newGeometry.width() * dpr),
                       int(newGeometry.height() * dpr));
    m_Pixmap.setDevicePixelRatio(dpr);
    m_Pixmap.fill(m_BkColor);
    m_UpdateAll = true;
    updateCtrl(true);
}
#else
void FreqCtrlQuick::geometryChanged(const QRectF &newGeometry,
                                    const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);

    qreal dpr = window() ? window()->devicePixelRatio() : qreal(1.0);
    m_Pixmap = QPixmap(int(newGeometry.width() * dpr),
                       int(newGeometry.height() * dpr));
    m_Pixmap.setDevicePixelRatio(dpr);
    m_Pixmap.fill(m_BkColor);
    m_UpdateAll = true;
    updateCtrl(true);
}
#endif


void FreqCtrlQuick::hoverLeaveEvent(QHoverEvent *event)
{
    if (m_DirectEntryMode) {
        QQuickPaintedItem::hoverLeaveEvent(event);
        return;
    }

    // called when mouse cursor leaves this control so deactivate any highlights
    if (m_ActiveEditDigit >= 0)
    {
        if (m_DigitInfo[m_ActiveEditDigit].editmode)
        {
            m_DigitInfo[m_ActiveEditDigit].editmode = false;
            m_DigitInfo[m_ActiveEditDigit].modified = true;
            m_ActiveEditDigit = -1;
            updateCtrl(false);
        }
    }

    QQuickPaintedItem::hoverLeaveEvent(event);
}

void FreqCtrlQuick::paint(QPainter *p)
{
    if (!p)
        return;

    // Draw into the offscreen pixmap as before
    QPainter painter(&m_Pixmap);

    if (m_UpdateAll)           // if need to redraw everything
    {
        drawBkGround(painter);
        m_UpdateAll = false;
    }

    // draw any modified digits into the pixmap
    drawDigits(painter);

    // Now blit the pixmap into the Qt Quick scene
    p->drawPixmap(0, 0, m_Pixmap);

    if (m_DirectEntryMode)
        drawDirectEntry(*p);
}


void FreqCtrlQuick::hoverMoveEvent(QHoverEvent *event)
{
    event->setAccepted(true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QPointF pt = event->posF();
#else
    QPointF pt = event->position();
#endif

    // We don't need window()->isActive() here; just react whenever we get hover
    if (!hasActiveFocus())
        forceActiveFocus(Qt::MouseFocusReason);

    for (int i = m_DigStart; i < m_NumDigits; ++i) {
        if (inRect(m_DigitInfo[i].dQRect, pt)) {
            setActiveDigit(i);
            break;
        }
    }

}

void FreqCtrlQuick::mouseMoveEvent(QMouseEvent *event)
{
    event->setAccepted(true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QPointF pt = event->localPos();
#else
    QPointF pt = event->position();
#endif

    // Optional: reuse same logic when dragging
    for (int i = m_DigStart; i < m_NumDigits; ++i) {
        if (inRect(m_DigitInfo[i].dQRect, pt)) {
            setActiveDigit(i);
            break;
        }
    }

}

void FreqCtrlQuick::mousePressEvent(QMouseEvent *event)
{
    if (m_DirectEntryMode) {
        forceActiveFocus(Qt::MouseFocusReason);
        m_DirectEntryAllSelected = true;
        update();
        event->accept();
        return;
    }

    event->setAccepted(true);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QPointF pt = event->localPos();
#else
    QPointF pt = event->position();
#endif

    if (event->button() == Qt::LeftButton)
    {
        for (int i = m_DigStart; i < m_NumDigits; i++)
        {
            if (inRect(m_DigitInfo[i].dQRect, pt)) // if in i'th digit
            {
                if (inRect(m_DigitInfo[i].dQRect, pt)) // if in i'th digit
                {
                    if (m_LRMouseFreqSel)
                    {
                        incFreq();
                    }
                    else
                    {
                        if (pt.y() < m_DigitInfo[i].dQRect.bottom() / 2) // top half?
                            incFreq();
                        else
                            decFreq();                                   // bottom half
                    }
                }
            }
        }
    }
    else if (event->button() == Qt::RightButton)
    {
        for (int i = m_DigStart; i < m_NumDigits; i++)
        {
            if (inRect(m_DigitInfo[i].dQRect, pt)) // if in i'th digit
            {
                if (m_LRMouseFreqSel)
                {
                    decFreq();
                }
                else
                {
                    clearFreq();
                }
            }
        }
    }
}

void FreqCtrlQuick::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        beginDirectEntry();
        event->accept();
        return;
    }

    QQuickPaintedItem::mouseDoubleClickEvent(event);
}

void FreqCtrlQuick::wheelEvent(QWheelEvent *event)
{
    if (m_DirectEntryMode) {
        event->ignore();
        return;
    }

    event->setAccepted(true);
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    QPointF   pt = QPointF(event->pos());
#else
    QPointF   pt = event->position();
#endif
    int       delta = m_InvertScrolling ? -event->angleDelta().y() : event->angleDelta().y();
    qreal     numDegrees = delta / 8;
    qreal     offset = numDegrees / 15;

    qreal stepsToScroll = QGuiApplication::styleHints()->wheelScrollLines() * offset;

    if( (scrollWheelOffsetAccumulated > 0) && (offset > 0) ) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else if ((scrollWheelOffsetAccumulated < 0) && (offset < 0)) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else {
        // Changed direction, zap the old accumulation:
        scrollWheelOffsetAccumulated = stepsToScroll;
    }

    int numSteps = qRound(scrollWheelOffsetAccumulated);
    scrollWheelOffsetAccumulated -= numSteps;

    for (int i = m_DigStart; i < m_NumDigits; i++)
    {
        if (inRect(m_DigitInfo[i].dQRect, pt)) // if in i'th digit
        {
            if (numSteps > 0)
                incFreq();
            else if (numSteps < 0)
                decFreq();
        }
    }
    scrollWheelOffsetAccumulated = 0;
    event->setAccepted(true);
}

void FreqCtrlQuick::beginDirectEntry()
{
    forceActiveFocus(Qt::MouseFocusReason);
    m_DirectEntryMode = true;
    m_DirectEntryAllSelected = true;
    m_DirectEntryText = directEntryTextForFrequency(m_freq);

    if (m_ActiveEditDigit >= 0 && m_DigitInfo[m_ActiveEditDigit].editmode) {
        m_DigitInfo[m_ActiveEditDigit].editmode = false;
        m_DigitInfo[m_ActiveEditDigit].modified = true;
        m_ActiveEditDigit = -1;
    }

    updateCtrl(false);
}

void FreqCtrlQuick::cancelDirectEntry()
{
    m_DirectEntryMode = false;
    m_DirectEntryAllSelected = false;
    m_DirectEntryText.clear();
    updateCtrl(true);
}

bool FreqCtrlQuick::commitDirectEntry()
{
    qint64 freq = 0;
    if (!parseDirectEntry(m_DirectEntryText, freq)) {
        cancelDirectEntry();
        return true;
    }

    m_DirectEntryMode = false;
    m_DirectEntryAllSelected = false;
    m_DirectEntryText.clear();
    setFrequency(freq);
    updateCtrl(true);
    return true;
}

QString FreqCtrlQuick::directEntryTextForFrequency(qint64 freq) const
{
    qint64 rem = qBound(m_MinFreq, freq, m_MaxFreq);
    QString text;

    for (int i = m_NumDigits - 1; i >= m_DigStart; --i) {
        text.append(QChar('0' + int((rem / m_DigitInfo[i].weight) % 10)));

        if (i > m_DigStart && (i % 3) == 0) {
            QChar sep = gsep;
            if (m_Unit == FCTL_UNIT_NONE)
                sep = (m_LeadZeroPos > i) ? dsep : gsep;
            else
                sep = (i == m_DecPos) ? dsep : gsep;
            text.append(sep);
        }
    }

    while (text.size() > 1 && text.startsWith('0') && text.at(1).isDigit())
        text.remove(0, 1);

    if (text.startsWith(gsep) || text.startsWith(dsep))
        text.prepend('0');

    return text;
}

QString FreqCtrlQuick::directEntryDisplayText() const
{
    QString text = m_DirectEntryText.trimmed();
    if (text.isEmpty())
        return QString();

    if (text.endsWith("mhz", Qt::CaseInsensitive))
        text.chop(3);
    else if (text.endsWith("khz", Qt::CaseInsensitive))
        text.chop(3);
    else if (text.endsWith("ghz", Qt::CaseInsensitive))
        text.chop(3);
    else if (text.endsWith("hz", Qt::CaseInsensitive))
        text.chop(2);
    else if (text.endsWith('m', Qt::CaseInsensitive) ||
             text.endsWith('k', Qt::CaseInsensitive) ||
             text.endsWith('g', Qt::CaseInsensitive))
        text.chop(1);

    return text.trimmed();
}

bool FreqCtrlQuick::parseDirectEntry(const QString &text, qint64 &freq) const
{
    QString s = text.trimmed();
    if (s.isEmpty())
        return false;

    s.remove(' ');
    s.remove('\'');
    s.remove('_');
    if (!gsep.isNull() && gsep != '.' && gsep != ',')
        s.remove(gsep);

    QString lower = s.toLower();
    qreal multiplier = 0.0;

    if (lower.endsWith("mhz")) {
        multiplier = 1000000.0;
        lower.chop(3);
    } else if (lower.endsWith('m')) {
        multiplier = 1000000.0;
        lower.chop(1);
    } else if (lower.endsWith("khz")) {
        multiplier = 1000.0;
        lower.chop(3);
    } else if (lower.endsWith('k')) {
        multiplier = 1000.0;
        lower.chop(1);
    } else if (lower.endsWith("ghz")) {
        multiplier = 1000000000.0;
        lower.chop(3);
    } else if (lower.endsWith('g')) {
        multiplier = 1000000000.0;
        lower.chop(1);
    } else if (lower.endsWith("hz")) {
        multiplier = 1.0;
        lower.chop(2);
    }

    const int dotCount = lower.count('.');
    const int commaCount = lower.count(',');
    if (dotCount + commaCount > 1) {
        lower.remove('.');
        lower.remove(',');
    } else {
        lower.replace(',', '.');
    }

    bool ok = false;
    const double value = QLocale::c().toDouble(lower, &ok);
    if (!ok || !std::isfinite(value) || value < 0.0)
        return false;

    if (multiplier == 0.0) {
        // Decimal input is conventionally MHz. Short integer entry is kHz
        // so "14195" tunes 14.195 MHz, while "14195000" remains Hz.
        multiplier = lower.contains('.') ? 1000000.0
                                         : (lower.length() <= 6 ? 1000.0 : 1.0);
    }

    qint64 parsed = qRound64(value * multiplier);
    if (parsed < m_MinFreq)
        parsed = m_MinFreq;
    if (parsed > m_MaxFreq)
        parsed = m_MaxFreq;

    parsed -= parsed % m_MinStep;
    freq = parsed;
    return true;
}

void FreqCtrlQuick::drawDirectEntry(QPainter &painter)
{
    QString display = directEntryDisplayText();
    int charIndex = display.length() - 1;

    painter.save();
    painter.setFont(m_DigitFont);

    for (int i = m_DigStart; i < m_NumDigits; ++i) {
        if (!m_SepRect[i].isNull())
            painter.fillRect(m_SepRect[i], m_DirectEntryAllSelected ? m_HighlightColor : m_BkColor);
        painter.fillRect(m_DigitInfo[i].dQRect, m_DirectEntryAllSelected ? m_HighlightColor : m_BkColor);
    }

    for (int i = m_DigStart; i < m_NumDigits; ++i) {
        while (charIndex >= 0 && !display.at(charIndex).isDigit()) {
            if (!m_SepRect[i].isNull()) {
                painter.setPen(m_DigitColor);
                painter.drawText(m_SepRect[i],
                                 Qt::AlignHCenter | Qt::AlignVCenter,
                                 display.at(charIndex));
            }
            --charIndex;
        }

        const QChar c = charIndex >= 0 ? display.at(charIndex--) : QChar(' ');
        painter.setPen(c == QChar(' ') ? m_InactiveColor : m_DigitColor);
        painter.drawText(m_DigitInfo[i].dQRect,
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         c);
    }

    painter.setPen(QPen(m_HighlightColor, 1));
    for (int i = m_DigStart; i < m_NumDigits; ++i)
        painter.drawRect(m_DigitInfo[i].dQRect.adjusted(0, 0, -1, -1));

    painter.restore();
}

void FreqCtrlQuick::keyPressEvent(QKeyEvent *event)
{
    if (m_DirectEntryMode) {
        const int key = event->key();

        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (commitDirectEntry())
                event->accept();
            return;
        }

        if (key == Qt::Key_Escape) {
            cancelDirectEntry();
            event->accept();
            return;
        }

        if (key == Qt::Key_Backspace) {
            if (m_DirectEntryAllSelected) {
                m_DirectEntryText.clear();
                m_DirectEntryAllSelected = false;
            } else if (!m_DirectEntryText.isEmpty()) {
                m_DirectEntryText.chop(1);
            }
            update();
            event->accept();
            return;
        }

        if (key == Qt::Key_Delete) {
            m_DirectEntryText.clear();
            m_DirectEntryAllSelected = false;
            update();
            event->accept();
            return;
        }

        if (event->matches(QKeySequence::SelectAll)) {
            m_DirectEntryAllSelected = true;
            update();
            event->accept();
            return;
        }

        const QString text = event->text();
        if (text.size() == 1) {
            const QChar c = text.at(0);
            const bool accepted = c.isDigit() || c == '.' || c == ',' ||
                                  c == 'k' || c == 'K' ||
                                  c == 'm' || c == 'M' ||
                                  c == 'g' || c == 'G' ||
                                  c == 'h' || c == 'H' ||
                                  c == 'z' || c == 'Z';
            if (accepted) {
                if (m_DirectEntryAllSelected) {
                    m_DirectEntryText.clear();
                    m_DirectEntryAllSelected = false;
                }
                m_DirectEntryText.append(c);
                update();
                event->accept();
                return;
            }
        }

        event->ignore();
        return;
    }

    event->setAccepted(true);
    // call base class if dont over ride key
    bool      fSkipMsg = false;
    qint64    tmp;

    // qDebug() <<event->key();

    switch (event->key())
    {
    case Qt::Key_0:
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:
        if (m_ActiveEditDigit >= 0)
        {
            if (m_DigitInfo[m_ActiveEditDigit].editmode)
            {
                tmp = (m_freq / m_DigitInfo[m_ActiveEditDigit].weight) % 10;
                m_freq -= tmp * m_DigitInfo[m_ActiveEditDigit].weight;
                m_freq = m_freq + (event->key() - '0') *
                                      m_DigitInfo[m_ActiveEditDigit].weight;
                setFrequency(m_freq);
            }
        }
        moveCursorRight();
        fSkipMsg = true;
        break;
    case Qt::Key_Backspace:
    case Qt::Key_Left:
        if (m_ActiveEditDigit != -1)
        {
            moveCursorLeft();
            fSkipMsg = true;
        }
        break;
    case Qt::Key_Up:
        if (m_ActiveEditDigit != -1)
        {
            incFreq();
            fSkipMsg = true;
        }
        break;
    case Qt::Key_Down:
        if (m_ActiveEditDigit != -1)
        {
            decFreq();
            fSkipMsg = true;
        }
        break;
    case Qt::Key_Right:
        if (m_ActiveEditDigit != -1)
        {
            moveCursorRight();
            fSkipMsg = true;
        }
        break;
    case Qt::Key_Home:
        cursorHome();
        fSkipMsg = true;
        break;
    case Qt::Key_End:
        cursorEnd();
        fSkipMsg = true;
        break;
    default:
        break;
    }
    if (!fSkipMsg)
        QQuickPaintedItem::keyPressEvent(event);
}

void FreqCtrlQuick::drawBkGround(QPainter &Painter)
{
    QRect    rect(0, 0, width(), height());

    // qDebug() <<rect;
    int    cellwidth = 100 * rect.width() /
                    (100 * (m_NumDigits + m_NumDigitsForUnit) +
                     (m_NumSeps * SEPRATIO_N) / SEPRATIO_D);
    int    sepwidth = (SEPRATIO_N * cellwidth) / (100 * SEPRATIO_D);
    // qDebug() <<cellwidth <<sepwidth;

    // draw unit text
    if (m_Unit != FCTL_UNIT_NONE)
    {
        m_UnitsRect.setRect(rect.right() - 2 * cellwidth, rect.top(),
                            2 * cellwidth, rect.height());
        Painter.fillRect(m_UnitsRect, m_BkColor); // FIXME: not necessary?
        m_UnitsFont.setPixelSize((UNITS_SIZE_PERCENT * rect.height()) / 100);
        //m_UnitsFont.setFamily("Arial");
        Painter.setFont(m_UnitsFont);
        Painter.setPen(m_UnitsColor);
        Painter.drawText(m_UnitsRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         m_UnitString);
    }

    // draw digits
    m_DigitFont.setPixelSize((DIGIT_SIZE_PERCENT * rect.height()) / 100);
    //m_DigitFont.setFamily("Arial");
    Painter.setFont(m_DigitFont);
    Painter.setPen(m_DigitColor);

    QChar    dgsep = gsep;       // digit group separator

    int     digpos = rect.right() - m_NumDigitsForUnit * cellwidth - 1; // starting digit x position
    for (int i = m_DigStart; i < m_NumDigits; i++)
    {
        if ((i > m_DigStart) && ((i % 3) == 0))
        {
            m_SepRect[i].setCoords(digpos - sepwidth,
                                   rect.top(),
                                   digpos,
                                   rect.bottom());
            Painter.fillRect(m_SepRect[i], m_BkColor);
            digpos -= sepwidth;
            if (m_Unit == FCTL_UNIT_NONE)
            {
                if (m_LeadZeroPos > i)
                    dgsep = dsep;
                else
                    dgsep = gsep;
            }
            else
            {
                if (i == m_DecPos)
                    dgsep = dsep;
                else
                    dgsep = gsep;
            }
            if( (i==m_NumDigits-1) && (m_DigitInfo[i].val==0) ) {
                Painter.drawText(m_SepRect[i], Qt::AlignHCenter | Qt::AlignVCenter,
                                                 " ");
            } else {
                // Only draw the digit separator if we are within a number.
                // This eliminates drawing a comma after the last (MSB) digit.
                Painter.drawText(m_SepRect[i], Qt::AlignHCenter | Qt::AlignVCenter,
                                 dgsep);
            }
        }
        else
        {
            m_SepRect[i].setCoords(0, 0, 0, 0);
        }
        m_DigitInfo[i].dQRect.setCoords(digpos - cellwidth, rect.top(),
                                        digpos, rect.bottom());
        digpos -= cellwidth;
    }
}

//  Draws just the Digits that have been modified
void FreqCtrlQuick::drawDigits(QPainter &Painter)
{
    Painter.setFont(m_DigitFont);
    m_FirstEditableDigit = m_DigStart;

    for (int i = m_DigStart; i < m_NumDigits; i++)
    {
        if (m_DigitInfo[i].incval == 0)
            m_FirstEditableDigit++;

        if (m_DigitInfo[i].modified || m_DigitInfo[i].editmode)
        {
            if (m_DigitInfo[i].editmode && m_DigitInfo[i].incval != 0)
                Painter.fillRect(m_DigitInfo[i].dQRect, m_HighlightColor);
            else
                Painter.fillRect(m_DigitInfo[i].dQRect, m_BkColor);

            if (i >= m_LeadZeroPos)
                Painter.setPen(m_InactiveColor);
            else
                Painter.setPen(m_DigitColor);

            if (m_freq < 0 && i == m_LeadZeroPos - 1 && m_DigitInfo[i].val == 0) {
                Painter.drawText(m_DigitInfo[i].dQRect,
                                 Qt::AlignHCenter | Qt::AlignVCenter,
                                 QString("-0"));
            } else {
                if (i == 6 || i == 7) {
                    //qInfo() << "Drawing digit" << i << "value:" << m_DigitInfo[i].val
                    //         << "rect:" << m_DigitInfo[i].dQRect;
                }
                Painter.drawText(m_DigitInfo[i].dQRect,
                                 Qt::AlignHCenter | Qt::AlignVCenter,
                                 QString::number(m_DigitInfo[i].val));
            }
            m_DigitInfo[i].modified = false;
        }
    }
}

// Increment just the digit active in edit mode
void FreqCtrlQuick::incDigit()
{
    /** FIXME: no longer used? */
    int       tmp;
    qint64    tmpl;

    if (m_ActiveEditDigit >= 0)
    {
        if (m_DigitInfo[m_ActiveEditDigit].editmode)
        {
            if (m_DigitInfo[m_ActiveEditDigit].weight ==
                m_DigitInfo[m_ActiveEditDigit].incval)
            {
                // get the current digit value
                tmp =
                    (int)((m_freq / m_DigitInfo[m_ActiveEditDigit].weight) %
                           10);
                // set the current digit value to zero
                m_freq -= tmp * m_DigitInfo[m_ActiveEditDigit].weight;
                tmp++;
                if (tmp > 9)
                    tmp = 0;
                m_freq = m_freq + (qint64)tmp *
                                      m_DigitInfo[m_ActiveEditDigit].weight;
            }
            else
            {
                tmp =
                    (int)((m_freq / m_DigitInfo[m_ActiveEditDigit + 1].weight) %
                           10);
                tmpl = m_freq + m_DigitInfo[m_ActiveEditDigit].incval;
                if (tmp !=
                    (int)((tmpl / m_DigitInfo[m_ActiveEditDigit + 1].weight) %
                           10))
                {
                    tmpl -= m_DigitInfo[m_ActiveEditDigit + 1].weight;
                }
                m_freq = tmpl;
            }
            setFrequency(m_freq);
        }
    }
}

// Increment the frequency by this digit active in edit mode
void FreqCtrlQuick::incFreq()
{
    if (m_ActiveEditDigit >= 0)
    {
        if (m_DigitInfo[m_ActiveEditDigit].editmode)
        {
            m_freq += m_DigitInfo[m_ActiveEditDigit].incval;
            if (m_ResetLowerDigits)
            {
                /* Set digits below the active one to 0 */
                m_freq = m_freq - m_freq %
                                      m_DigitInfo[m_ActiveEditDigit].weight;
            }
            setFrequency(m_freq);
            m_LastEditDigit = m_ActiveEditDigit;
        }
    }
}

// Decrement the digit active in edit mode
void FreqCtrlQuick::decDigit()
{
    /** FIXME: no longer used? */
    int       tmp;
    qint64    tmpl;

    if (m_ActiveEditDigit >= 0)
    {
        if (m_DigitInfo[m_ActiveEditDigit].editmode)
        {
            if (m_DigitInfo[m_ActiveEditDigit].weight ==
                m_DigitInfo[m_ActiveEditDigit].incval)
            {
                // get the current digit value
                tmp =
                    (int)((m_freq / m_DigitInfo[m_ActiveEditDigit].weight) %
                           10);
                // set the current digit value to zero
                m_freq -= tmp * m_DigitInfo[m_ActiveEditDigit].weight;
                tmp--;
                if (tmp < 0)
                    tmp = 9;
                m_freq = m_freq + (qint64)tmp *
                                      m_DigitInfo[m_ActiveEditDigit].weight;
            }
            else
            {
                tmp =
                    (int)((m_freq / m_DigitInfo[m_ActiveEditDigit + 1].weight) %
                           10);
                tmpl = m_freq - m_DigitInfo[m_ActiveEditDigit].incval;
                if (tmp !=
                    (int)((tmpl / m_DigitInfo[m_ActiveEditDigit + 1].weight) %
                           10))
                {
                    tmpl += m_DigitInfo[m_ActiveEditDigit + 1].weight;
                }
                m_freq = tmpl;
            }
            setFrequency(m_freq);
        }
    }
}

// Decrement the frequency by this digit active in edit mode
void FreqCtrlQuick::decFreq()
{
    if (m_ActiveEditDigit >= 0)
    {
        if (m_DigitInfo[m_ActiveEditDigit].editmode)
        {
            m_freq -= m_DigitInfo[m_ActiveEditDigit].incval;
            if (m_ResetLowerDigits)
            {
                /* digits below the active one are reset to 0 */
                m_freq = m_freq - m_freq %
                                      m_DigitInfo[m_ActiveEditDigit].weight;
            }

            setFrequency(m_freq);
            m_LastEditDigit = m_ActiveEditDigit;
        }
    }
}

// Clear the selected digit and the digits below (i.e. set them to 0)
void FreqCtrlQuick::clearFreq()
{
    if (m_ActiveEditDigit >= 0)
    {
        if (m_DigitInfo[m_ActiveEditDigit].editmode)
        {
            m_freq -= m_DigitInfo[m_ActiveEditDigit].val *
                      m_DigitInfo[m_ActiveEditDigit].incval;

            /* digits below the active one are reset to 0 */
            m_freq -= m_freq % m_DigitInfo[m_ActiveEditDigit].weight;

            setFrequency(m_freq);
            m_LastEditDigit = m_ActiveEditDigit;
        }
    }
}

//  Cursor move routines for arrow key editing
void FreqCtrlQuick::moveCursorLeft()
{
    if ((m_ActiveEditDigit >= 0) && (m_ActiveEditDigit < m_NumDigits - 1))
    {
        setActiveDigit(m_ActiveEditDigit + 1);
    }
}

void FreqCtrlQuick::moveCursorRight()
{
    if (m_ActiveEditDigit > m_FirstEditableDigit)
    {
        setActiveDigit(m_ActiveEditDigit - 1);
    }
}

void FreqCtrlQuick::cursorHome()
{
    if (m_ActiveEditDigit >= 0)
    {
        setActiveDigit(m_NumDigits - 1);
    }
}

void FreqCtrlQuick::cursorEnd()
{
    if (m_ActiveEditDigit > 0)
    {
        setActiveDigit(m_FirstEditableDigit);
    }
}

void FreqCtrlQuick::setFrequencyFocus()
{
    // Select last digit or 5th digit (100s of kHz), whatever is bigger.
    int position = std::max(int(log10(m_freq)), 5);

    if (!hasFocus()) {
        forceActiveFocus(Qt::ShortcutFocusReason);
    }

    setActiveDigit(position);
}
