#ifndef COLLAPSIBLESECTION_H
#define COLLAPSIBLESECTION_H

// CollapsibleSection — a QGroupBox-like container with a disclosure-triangle toggle.
//
// Header row (always visible):
//   [▶/▼ arrow button]  [bold title label]  [stretch]  [optional pinned widget]
//
// Body widget is shown/hidden when the header is clicked.
// The pinned widget (typically an "Enable" checkbox) remains visible in the header
// regardless of the collapsed/expanded state.
//
// Usage:
//   auto* sec = new CollapsibleSection("My Section", enableCheckbox);
//   auto* body = new QWidget;
//   auto* form = new QFormLayout(body);
//   form->addRow(...);
//   sec->setBodyWidget(body);
//   layout->addWidget(sec);

#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

class CollapsibleSection : public QFrame
{
    Q_OBJECT

public:
    // pinnedWidget (optional) is placed in the header and is always visible.
    // Typically this is the "Enable" checkbox for the section.
    explicit CollapsibleSection(const QString& title,
                                QWidget*       pinnedWidget = nullptr,
                                QWidget*       parent       = nullptr);

    // Set the body widget (must be called once, before showing).
    // The widget will be shown/hidden on toggle.
    void setBodyWidget(QWidget* body);

    bool isExpanded() const { return m_expanded; }
    void setExpanded(bool expanded);

signals:
    void expandedChanged(bool expanded);

private slots:
    void onHeaderClicked();

private:
    void applyState();

    QToolButton* m_toggle   {nullptr};
    QWidget*     m_body     {nullptr};
    bool         m_expanded {true};
};

#endif // COLLAPSIBLESECTION_H
