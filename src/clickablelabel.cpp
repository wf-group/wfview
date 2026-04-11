#include "clickablelabel.h"

ClickableLabel::ClickableLabel(QWidget *parent)
    : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableLabel::setActive(bool active)
{
    m_active = active;
    updateStyle();
}

bool ClickableLabel::isActive() const
{
    return m_active;
}

void ClickableLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_active = !m_active;
        updateStyle();
        emit clicked();
    }
    QLabel::mousePressEvent(event);
}

void ClickableLabel::updateStyle()
{
    QFont f = font();
    f.setBold(m_active);
    setFont(f);
}
