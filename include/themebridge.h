#ifndef THEMEBRIDGE_H
#define THEMEBRIDGE_H

#include <QObject>
#include <QColor>
#include <QPalette>
#include <QWidget>

class ThemeBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor windowText READ windowText NOTIFY changed)
    Q_PROPERTY(QColor window READ window NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor base READ base NOTIFY changed)
    Q_PROPERTY(QColor button READ button NOTIFY changed)
    Q_PROPERTY(QColor highlight READ highlight NOTIFY changed)
    Q_PROPERTY(QColor highlightedText READ highlightedText NOTIFY changed)
    Q_PROPERTY(QColor buttonText READ buttonText NOTIFY changed)
public:
    explicit ThemeBridge(QObject* parent=nullptr) : QObject(parent) {}

    void syncFrom(const QWidget* w) {
        const QPalette p = w ? w->palette() : QPalette{};
        m_window    = p.color(QPalette::Window);
        m_windowText = p.color(QPalette::WindowText);
        m_text      = p.color(QPalette::Text);
        m_base      = p.color(QPalette::Base);
        m_button    = p.color(QPalette::Button);
        m_highlight = p.color(QPalette::Highlight);
        m_highlightedText = p.color(QPalette::HighlightedText);
        m_buttonText = p.color(QPalette::ButtonText);
        emit changed();
    }

    QColor window() const { return m_window; }
    QColor windowText() const { return m_windowText; }
    QColor text() const { return m_text; }
    QColor base() const { return m_base; }
    QColor button() const { return m_button; }
    QColor highlight() const { return m_highlight; }
    QColor highlightedText() const { return m_highlightedText; }
    QColor buttonText() const { return m_buttonText; }

signals:
    void changed();

private:
    QColor m_window, m_windowText, m_text, m_base, m_button, m_highlight, m_highlightedText, m_buttonText;
};

#endif // THEMEBRIDGE_H
