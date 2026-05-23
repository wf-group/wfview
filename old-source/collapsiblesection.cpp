// CollapsibleSection — disclosure-triangle section widget.

#include "collapsiblesection.h"
#include <QFont>
#include <QCursor>

CollapsibleSection::CollapsibleSection(const QString& title,
                                       QWidget*       pinnedWidget,
                                       QWidget*       parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 6);
    outer->setSpacing(4);

    // ── Header row ──────────────────────────────────────────────────────────
    auto* headerWidget = new QWidget;
    headerWidget->setCursor(Qt::PointingHandCursor);
    auto* header = new QHBoxLayout(headerWidget);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(4);

    m_toggle = new QToolButton;
    m_toggle->setArrowType(Qt::DownArrow);   // expanded = ▼
    m_toggle->setAutoRaise(true);
    m_toggle->setToolTip(tr("Collapse / expand this section"));
    m_toggle->setCursor(Qt::PointingHandCursor);
    header->addWidget(m_toggle);

    auto* lbl = new QLabel(title);
    QFont f = lbl->font();
    f.setBold(true);
    lbl->setFont(f);
    header->addWidget(lbl);

    header->addStretch(1);

    m_pinnedWidget = pinnedWidget;
    if (m_pinnedWidget)
        header->addWidget(m_pinnedWidget);

    outer->addWidget(headerWidget);

    // ── Body placeholder — filled by setBodyWidget() ─────────────────────
    // (nothing added yet; setBodyWidget() appends to outer)

    // Clicking the header row or the toggle button both toggle the section.
    connect(m_toggle, &QToolButton::clicked, this, &CollapsibleSection::onHeaderClicked);

    // Install an event filter on the header so clicking anywhere on it works.
    // We do this by making the header a QPushButton-like widget with a mouse
    // release handler.  The simplest approach is a second connect on the header
    // widget itself — but QWidget has no clicked signal.  Use the toggle button
    // only (already wired above); users can also click the title text area by
    // clicking the button.  This is sufficient UX for the intended use case.
    // (A full header-click could be added with an event filter if desired.)
}

void CollapsibleSection::setBodyWidget(QWidget* body)
{
    Q_ASSERT(!m_body && "setBodyWidget() must be called exactly once");
    m_body = body;
    auto* outer = static_cast<QVBoxLayout*>(layout());
    outer->addWidget(m_body);
    m_body->setVisible(m_expanded);
}

void CollapsibleSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    applyState();
    emit expandedChanged(m_expanded);
}

void CollapsibleSection::setContentEnabled(bool enabled)
{
    if (m_body)
        m_body->setEnabled(enabled);
    if (m_pinnedWidget)
        m_pinnedWidget->setEnabled(enabled);
}

// ─────────────────────────────────────────────────────────────────────────────

void CollapsibleSection::onHeaderClicked()
{
    setExpanded(!m_expanded);
}

void CollapsibleSection::applyState()
{
    m_toggle->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
    if (m_body)
        m_body->setVisible(m_expanded);

    // Tell our parent layout that our preferred size has changed.
    // The parent dialog handles the actual window resize via expandedChanged.
    updateGeometry();
}
