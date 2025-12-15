#ifndef PALETTEPROXY_H
#define PALETTEPROXY_H

#include <QObject>
#include <QColor>
#include <QPalette>

class PaletteProxy : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor window READ window CONSTANT)
    Q_PROPERTY(QColor text READ text CONSTANT)
    Q_PROPERTY(QColor button READ button CONSTANT)
public:
    QPalette p;
    QColor window() const { return p.color(QPalette::Window); }
    QColor text() const { return p.color(QPalette::WindowText); }
    QColor button() const { return p.color(QPalette::Button); }
};


#endif // PALETTEPROXY_H
