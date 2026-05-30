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
#include <QPointer>
#include <QMutex>
#include <atomic>
#include <vector>

#include "wfviewtypes.h"

class ReceiverController; // forward declare

class WaterfallRootNode : public QSGNode
{
public:
    QSGSimpleTextureNode *topNode    = nullptr;
    QSGSimpleTextureNode *bottomNode = nullptr;

    QSGTexture *tex = nullptr;   // shared texture
    QSize texSize;

    ~WaterfallRootNode() override {
        delete tex;
        tex = nullptr;
    }
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

    QObject* getController() {return controller;}
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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void geometryChange(const QRectF &newG, const QRectF &oldG) override;
#else
    void geometryChanged(const QRectF &newG, const QRectF &oldG) override;
#endif

private:
    QObject* controller = nullptr;

    // --- guarded by imgMu ---
    QMutex imgMu;
    int imgWidth = 0;
    QImage img;           // RGB cache (circular buffer); newest line at ringIndex
    int ringIndex = 0;    // newest line index
    bool  dirty = false;

    // Raw scope-data ring buffer (source of truth, fixed length).
    // RGB is derived from this on demand so length/floor/ceiling/theme
    // changes can redraw real history rather than blank/stale pixels.
    static constexpr int rawCapacity = 1024;
    std::vector<uchar> rawRing; // rawCapacity * rawWidth bytes
    int rawWidth = 0;           // bins per line currently stored
    int rawHead  = -1;          // index of newest raw line (-1 = empty)
    int rawCount = 0;           // number of valid lines (<= rawCapacity)
    // ------------------------

    // Append one raw scope line; resets the ring if the bin width changed.
    void pushRawLine(const uchar *src, int w);
    // Rebuild the entire RGB cache from the raw ring at the current length.
    void rebuildImageFromRaw();

    int length = 128;
    double startFreq = 0.0;
    double endFreq   = 0.0;

    bool smooth = false;
    Theme theme = Jet;
    Theme previousTheme = Unknown;
    uchar floor = 0;
    uchar ceiling = 140;

    double xToFreq(qreal x) const;

    QRgb colorForValue(quint8 v) const;
    QRgb valueLut[256];
    bool lutValid = false;
    int  lutFloor = 0;
    int  lutCeil  = 255;
    void rebuildLut();

    std::atomic<qint64> scopeUpdateTimeNs{0};
};

#endif // WATERFALLITEM_H
