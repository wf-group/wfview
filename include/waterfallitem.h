#ifndef WATERFALLITEM_H
#define WATERFALLITEM_H

#pragma once

#include <QQuickItem>
#include <QImage>
#include <QMouseEvent>
#include <QElapsedTimer>
#include "wfviewtypes.h"

class WaterfallItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int length READ getLength WRITE setLength NOTIFY lengthChanged)
    Q_PROPERTY(Theme theme READ getTheme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool smooth READ getSmooth WRITE setSmooth NOTIFY smoothChanged)
    Q_PROPERTY(uchar floor   READ getFloor   WRITE setFloor   NOTIFY scaleChanged)
    Q_PROPERTY(uchar ceiling READ getCeiling WRITE setCeiling NOTIFY scaleChanged)

public:
    enum Theme {
        Jet, Cold, Hot, Thermal, Night, Ion, Grayscale, Geography, Hues, Polar, Spectrum, Candy
    };
    Q_ENUM(Theme)

    WaterfallItem();

    int getLength() const { return length; }
    void setLength(int d);

    Theme getTheme() const { return theme; }
    void setTheme(Theme t);

    bool getSmooth() const { return smooth; }
    void setSmooth(bool s);

    uchar getFloor()   const { return floor; }
    uchar getCeiling() const { return ceiling; }

    void setFloor(uchar v);
    void setCeiling(uchar v);

public slots:
    void updateScope(const scopeData &data);


signals:
    void lengthChanged();
    void themeChanged();
    void smoothChanged();
    void scaleChanged();
    void tuneRequested(double freqMHz);
    void wheelTuneRequested(int steps, Qt::KeyboardModifiers modifiers);
    void processingTimeNs(quint64 ns);


protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent *event) override;
    virtual void wheelEvent(QWheelEvent *event) override;

private:
    int imgWidth = 0;
    int length = 128;
    double startFreq = 0.0;
    double endFreq   = 0.0;
    QImage img;
    bool  dirty = false;
    bool smooth = false;
    Theme  theme = Jet;
    uchar floor = 0;
    uchar ceiling = 140;
    QRgb colorForValue(quint8 v) const;  // 0..255 → colour
    double xToFreq(qreal x) const;

};
#endif // WATERFALLITEM_H
