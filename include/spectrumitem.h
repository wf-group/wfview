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
#include <atomic>

#include "wfviewtypes.h"
#include "colorprefs.h"
#include "cluster.h"
#include "rigidentities.h"

class ReceiverController; // forward declare

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
    QSGGeometryNode *pbt = nullptr;
    QSGSimpleTextureNode *axisNode = nullptr;
    QSGGeometryNode *bandShapes = nullptr;

    QSGNode *dynamicRoot = nullptr;   // everything else (axis, bands, spots, overlays)
};


class SpectrumItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* controller READ getController WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(double startFreq READ setStartFreq NOTIFY freqRangeChanged)
    Q_PROPERTY(double endFreq   READ setEndFreq   NOTIFY freqRangeChanged)
    Q_PROPERTY(uchar floor   READ getFloor   WRITE setFloor   NOTIFY scaleChanged)
    Q_PROPERTY(uchar ceiling READ getCeiling WRITE setCeiling NOTIFY scaleChanged)
    Q_PROPERTY(uchar gridStep READ getGridStep WRITE setGridStep NOTIFY scaleChanged)

    Q_PROPERTY(quint64 center READ getCenter WRITE setCenter NOTIFY centerChanged)

    Q_PROPERTY(double passbandLow  READ getPassbandLow  WRITE setPassbandLow  NOTIFY passbandChanged)
    Q_PROPERTY(double passbandHigh READ getPassbandHigh WRITE setPassbandHigh NOTIFY passbandChanged)
    Q_PROPERTY(double pbtLow READ getPbtLow WRITE setPbtLow NOTIFY pbtChanged)
    Q_PROPERTY(double pbtHigh READ getPbtHigh WRITE setPbtHigh NOTIFY pbtChanged)

    Q_PROPERTY(float peakDecay READ getPeakDecay WRITE setPeakDecay NOTIFY peakDecayChanged)

    Q_PROPERTY(int maxSpotRows READ getMaxSpotRows WRITE setMaxSpotRows NOTIFY spotsChanged)
    Q_PROPERTY(colorPrefsType colors READ getColors WRITE setColors NOTIFY colorsChanged)
    Q_PROPERTY(const QVector<bandType> bands READ getBands WRITE setBands NOTIFY bandsChanged)


public:
    SpectrumItem();

    QObject* getController() {return m_controller;}
    void setController(QObject* c);

    Q_INVOKABLE void setOverflow(bool on);
    Q_INVOKABLE void setScopeOutOfRange(bool on);

    // Is this correct?
    double setStartFreq() const { return startFreq; }
    double setEndFreq()   const { return endFreq;  }

    colorPrefsType getColors() const {return colors;}
    uchar getFloor()   const { return floor; }
    uchar getCeiling() const { return ceiling; }
    uchar getGridStep() const { return gridStep; }
    double getCenter() const { return quint64(center * 100000.0); }
    void setCenter(quint64 x);
    double getPassbandLow()  const { return passbandLow; }
    double getPassbandHigh() const { return passbandHigh; }
    double getPbtLow() const { return pbtLow; }
    double getPbtHigh() const { return pbtHigh; }
    float getPeakDecay() const { return decay; }
    int getMaxSpotRows() const { return maxSpotRows; }

    struct SpotLayout {
        QRectF   rect;   // label rect in item coordinates
        spotData spot;
    };

    QVector<bandType> getBands() { return bands;}
    void setBands(const QVector<bandType> &newBands);

public slots:
    void setColors(const colorPrefsType &c);
    void setFloor(uchar v);
    void setCeiling(uchar v);
    void setGridStep(uchar v);
    void setPassbandLow(qreal x);
    void setPassbandHigh(qreal x);
    void setPbtLow(qreal x);
    void setPbtHigh(qreal x);
    void setPeakDecay(int d);
    void setMaxSpotRows(int rows);
    void updateScope(const scopeData &data);
    void setSpots(const QVector<spotData> &newSpots);
    void clearSpots();
    void clearPeaks();

signals:
    void controllerChanged();
    void freqRangeChanged();
    void scaleChanged();
    void centerChanged();
    void passbandChanged();
    void pbtChanged();
    void peakDecayChanged();
    void tuneRequested(double freqMHz);
    void spotsChanged();
    void overflowChanged();
    void outOfRangeChanged();
    void hoverSpotChanged(const spotData &spot, QPointF itemPos, bool active);
    void wheelTuneRequested(int steps, Qt::KeyboardModifiers modifiers);
    void passbandResizeRequested(double lowFreq, double highFreq);
    void pbtDragRequested(int action, double deltaMHz);
    void pbtResetRequested();
    void processingTimeNs(qint64 ns);
    void colorsChanged();
    void bandsChanged();

protected:    
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    //void wheelEvent(QWheelEvent *event) override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void geometryChange(const QRectF &newG, const QRectF &oldG) override;
#else
    void geometryChanged(const QRectF &newG, const QRectF &oldG) override;
#endif

private:

    QPointer<QObject> m_controller;

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
    double pbtLow = 0.0;
    double pbtHigh = 0.0;

    int decay = 4;
    int decayCounter = 0;

    int axis = 20;
    int ticks  = 7;

    enum class PassbandDrag {
        None,
        LowEdge,
        HighEdge,
        PbtLowEdge,
        PbtHighEdge,
        PbtMove
    };

    bool   dragActive       = false;
    PassbandDrag passbandDrag = PassbandDrag::None;
    double passbandDragFixedEdge = 0.0;
    double lastPassbandResizeFreq = 0.0;
    double pbtDragStartFreq = 0.0;
    double pbtDragLastDelta = 0.0;
    double lastTuneFreq  = 0.0;
    double xToFreq(qreal x) const;
    double freqToX(double freqMHz) const;
    PassbandDrag passbandDragHit(qreal x) const;
    PassbandDrag pbtDragHit(qreal x) const;
    bool passbandContains(qreal x) const;

    bool overflow = false;
    bool outOfRange = false;
    std::atomic<qint64> scopeUpdateTimeNs{0};

};

#endif // SPECTRUMITEM_H
