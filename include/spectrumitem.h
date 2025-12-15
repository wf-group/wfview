#ifndef SPECTRUMITEM_H
#define SPECTRUMITEM_H

#include <QQuickItem>
#include <QQuickWindow>
#include <QVector>
#include <QColor>
#include <QSGSimpleTextureNode>
#include <QImage>
#include <QPainter>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QSGVertexColorMaterial>
#include <QSGGeometry>
#include <QMouseEvent>
#include <QElapsedTimer>

#include "wfviewtypes.h"
#include "colorprefs.h"
#include "cluster.h"
#include "rigidentities.h"

class SpectrumRootNode : public QSGNode
{
public:
    QSGGeometryNode *background = nullptr;
    QSGGeometryNode *gridLines = nullptr;
    QSGGeometryNode *fillNode = nullptr;
    QSGGeometryNode *gradientFill = nullptr;
    QSGGeometryNode *peaksLine = nullptr;
    QSGGeometryNode *peaksFill = nullptr;
    QSGGeometryNode *peaksGradient = nullptr;
    QSGGeometryNode *spectrumLine = nullptr;
    QSGGeometryNode *frequencyLine = nullptr;
    QSGGeometryNode *passband = nullptr;
    QSGSimpleTextureNode *axisNode = nullptr;
    QSGGeometryNode *bandShapes = nullptr;

    QSGNode *dynamicRoot = nullptr;   // everything else (axis, bands, spots, overlays)
};


class SpectrumItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(double startFreq READ setStartFreq NOTIFY freqRangeChanged)
    Q_PROPERTY(double endFreq   READ setEndFreq   NOTIFY freqRangeChanged)
    Q_PROPERTY(uchar floor   READ getFloor   WRITE setFloor   NOTIFY scaleChanged)
    Q_PROPERTY(uchar ceiling READ getCeiling WRITE setCeiling NOTIFY scaleChanged)
    Q_PROPERTY(uchar gridStep READ getGridStep WRITE setGridStep NOTIFY scaleChanged)

    Q_PROPERTY(double center READ getCenter WRITE setCenter NOTIFY centerChanged)

    Q_PROPERTY(double passbandLow  READ getPassbandLow  WRITE setPassbandLow  NOTIFY passbandChanged)
    Q_PROPERTY(double passbandHigh READ getPassbandHigh WRITE setPassbandHigh NOTIFY passbandChanged)

    Q_PROPERTY(float peakDecay READ getPeakDecay WRITE setPeakDecay NOTIFY peakDecayChanged)

    Q_PROPERTY(int maxSpotRows READ getMaxSpotRows WRITE setMaxSpotRows NOTIFY spotsChanged)


public:
    SpectrumItem();

    Q_INVOKABLE void setOverflow(bool on);
    Q_INVOKABLE void setScopeOutOfRange(bool on);

    double setStartFreq() const { return startFreq; }
    double setEndFreq()   const { return endFreq;  }
    void setColors(const colorPrefsType &c);

    uchar getFloor()   const { return floor; }
    uchar getCeiling() const { return ceiling; }
    uchar getGridStep() const { return gridStep; }

    void setFloor(uchar v);
    void setCeiling(uchar v);
    void setGridStep(uchar v);

    double getCenter() const { return center; }
    void setCenter(qreal x);

    double getPassbandLow()  const { return passbandLow; }
    double getPassbandHigh() const { return passbandHigh; }
    void  setPassbandLow(qreal x);
    void  setPassbandHigh(qreal x);

    float getPeakDecay() const { return decay; }
    void  setPeakDecay(float d);

    int getMaxSpotRows() const { return maxSpotRows; }
    void setMaxSpotRows(int rows);

    struct SpotLayout {
        QRectF   rect;   // label rect in item coordinates
        spotData spot;
    };

    void setBands(const QVector<bandType> &newBands);
    void clearBands();

public slots:
    void updateScope(const scopeData &data);
    void setSpots(const QVector<spotData> &newSpots);
    void clearSpots();

signals:
    void freqRangeChanged();
    void scaleChanged();
    void centerChanged();
    void passbandChanged();
    void peakDecayChanged();
    void tuneRequested(double freqMHz);
    void spotsChanged();
    void overflowChanged();
    void outOfRangeChanged();
    void hoverSpotChanged(const spotData &spot, QPointF itemPos, bool active);
    void wheelTuneRequested(int steps, Qt::KeyboardModifiers modifiers);
    void passbandResizeRequested(double lowFreq, double highFreq);
    void processingTimeNs(qint64 ns);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QVector<quint8> mags;
    QVector<quint8> peaks;
    QVector<spotData> spots;
    QVector<SpotLayout> spotLayouts;
    QVector<bandType> bands;

    int maxSpotRows = 6;

    bool hoverActive = false;
    int hoverIndex = -1;
    spotData currentHoverSpot;
    QPointF  currentHoverPos;

    double startFreq = 0.0;
    double endFreq   = 0.0;
    colorPrefsType colors;

    uchar floor   = 0;
    uchar ceiling =   140;
    uchar gridStep = 20;

    double center = 0.0;     // 0..1
    double passbandLow  = 0.0;
    double passbandHigh = 0.0;

    float decay = 2.0f;

    int axis = 20;
    int ticks  = 7;

    bool   dragActive       = false;
    double lastTuneFreq  = 0.0;
    double xToFreq(qreal x) const;

    bool overflow = false;
    bool outOfRange = false;


};

#endif // SPECTRUMITEM_H
