#ifndef WATERFALLITEM_H
#define WATERFALLITEM_H

#pragma once

#include <QQuickItem>
#include <QQuickWindow>
#include <QImage>
#include <QMouseEvent>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QElapsedTimer>
#include "wfviewtypes.h"

class ReceiverController; // forward declare

class WaterfallRootNode : public QSGNode
{
public:
    QSGSimpleTextureNode *topNode    = nullptr;
    QSGSimpleTextureNode *bottomNode = nullptr;
};


class WaterfallItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ getController WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(int length READ getLength WRITE setLength NOTIFY lengthChanged)
    Q_PROPERTY(Theme theme READ getTheme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool smooth READ getSmooth WRITE setSmooth NOTIFY smoothChanged)
    Q_PROPERTY(uchar floor   READ getFloor   WRITE setFloor   NOTIFY scaleChanged)
    Q_PROPERTY(uchar ceiling READ getCeiling WRITE setCeiling NOTIFY scaleChanged)

public:
    enum Theme {
        Unknown, Jet, Cold, Hot, Thermal, Night, Ion, Grayscale, Geography, Hues, Polar, Spectrum, Candy
    };
    Q_ENUM(Theme)

    WaterfallItem();

    QObject* getController() {return m_controller;}
    int getLength() const { return length; }
    Theme getTheme() const { return theme; }
    bool getSmooth() const { return smooth; }
    uchar getFloor()   const { return floor; }
    uchar getCeiling() const { return ceiling; }


public slots:
    void updateScope(const scopeData &data);
    void setController(QObject* c);
    void setLength(int d);
    void setTheme(Theme t);
    void setSmooth(bool s);
    void setFloor(uchar v);
    void setCeiling(uchar v);

signals:
    void controllerChanged();
    void lengthChanged();
    void themeChanged();
    void smoothChanged();
    void scaleChanged();
    void tuneRequested(double freqMHz);
    void wheelTuneRequested(int steps, Qt::KeyboardModifiers modifiers);
    void processingTimeNs(qint64 ns);


protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void geometryChange(const QRectF &newG, const QRectF &oldG) override;
#else
    void geometryChanged(const QRectF &newG, const QRectF &oldG) override;
#endif

private:
    QPointer<QObject> m_controller;
    int imgWidth = 0;
    int length = 128;
    double startFreq = 0.0;
    double endFreq   = 0.0;
    QImage img;
    int ringIndex = 0;    // index of newest line in the image
    bool  dirty = false;
    bool smooth = false;
    Theme  theme = Jet;
    Theme  previousTheme = Unknown;
    uchar floor = 0;
    uchar ceiling = 140;
    double xToFreq(qreal x) const;

    QRgb colorForValue(quint8 v) const;  // 0..255 → colour
    QRgb valueLut[256];
    bool lutValid = false;
    int  lutFloor = 0;
    int  lutCeil  = 255;
    void rebuildLut();
};
#endif // WATERFALLITEM_H
