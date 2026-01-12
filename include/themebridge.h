#ifndef THEMEBRIDGE_H
#define THEMEBRIDGE_H

// ThemeBridge.h
#include <QGuiApplication>
#include <QPalette>

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

    Q_INVOKABLE void syncFromAppPalette() {
        const QPalette p = QGuiApplication::palette();
        setFromPalette(p);
    }

    void setFromPalette(const QPalette& p) {
        QColor nw      = p.color(QPalette::Window);
        QColor nwt     = p.color(QPalette::WindowText);
        QColor nt      = p.color(QPalette::Text);
        QColor nb      = p.color(QPalette::Base);
        QColor nbtn    = p.color(QPalette::Button);
        QColor nh      = p.color(QPalette::Highlight);
        QColor nht     = p.color(QPalette::HighlightedText);
        QColor nbtntxt = p.color(QPalette::ButtonText);

        if (m_window == nw && m_windowText == nwt && m_text == nt && m_base == nb &&
            m_button == nbtn && m_highlight == nh && m_highlightedText == nht && m_buttonText == nbtntxt)
            return;

        m_window = nw; m_windowText = nwt; m_text = nt; m_base = nb;
        m_button = nbtn; m_highlight = nh; m_highlightedText = nht; m_buttonText = nbtntxt;
        emit changed();
    }

    // getters unchanged...
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
