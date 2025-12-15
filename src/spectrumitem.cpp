// SpectrumItem.cpp
#include "spectrumitem.h"

SpectrumItem::SpectrumItem()
{
    setFlag(ItemHasContents, true);

    // allow mouse events
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

void SpectrumItem::setColors(const colorPrefsType &c)
{

    qInfo() << "Setting new color preset to spectrum" << c.presetNum << c.presetName;
    colors = c;
    update();  // redraw with new colours
}

// grid/scale configuration
void SpectrumItem::setFloor (uchar v)
{
    if (floor == v) return;
    floor = v;
    emit scaleChanged();
    update();
}

void SpectrumItem::setCeiling(uchar v)
{
    if (ceiling == v) return;
    ceiling = v;
    emit scaleChanged();
    update();
}

void SpectrumItem::setGridStep(uchar v)
{
    if (gridStep == v) return;
    gridStep = v;
    emit scaleChanged();
    update();
}

// center marker
void SpectrumItem::setCenter(double x)
{
    if (qFuzzyCompare(center, x)) return;
    center = x;
    emit centerChanged();
    update();
}

// passband
void SpectrumItem::setPassbandLow(double x)
{
    if (qFuzzyCompare(passbandLow, x)) return;
    passbandLow = x;
    emit passbandChanged();
    update();
}

void SpectrumItem::setPassbandHigh(double x)
{
    if (qFuzzyCompare(passbandHigh, x)) return;
    passbandHigh = x;
    emit passbandChanged();
    update();
}

// peak decay
void SpectrumItem::setPeakDecay(float d)
{
    if (qFuzzyCompare(decay, d)) return;
    decay = d;
    emit peakDecayChanged();
}

void SpectrumItem::setOverflow(bool on)
{
    if (overflow == on) return;
    overflow = on;
    emit overflowChanged();
    update();
}

void SpectrumItem::setScopeOutOfRange(bool on)
{
    if (outOfRange == on) return;
    outOfRange = on;
    emit outOfRangeChanged();
    update();
}


double SpectrumItem::xToFreq(qreal x) const
{
    if (endFreq <= startFreq || width() <= 0.0)
        return startFreq;

    double t = double(x) / double(width()); // 0..1
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    return startFreq + t * (endFreq - startFreq);
}

void SpectrumItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    // Double-click: start a tuning drag and emit first tune
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const qreal x = event->pos().x();
#else
    const qreal x = event->position().x();
#endif
    const double f = xToFreq(x);

    dragActive      = false;

    emit tuneRequested(f);   // "false" = initial double-click, not drag yet
    event->accept();
}

void SpectrumItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QPointF pos = event->pos();
#else
    QPointF pos = event->position();
#endif


    const SpotLayout *hit = nullptr;

    for (const SpotLayout &layout : std::as_const(spotLayouts)) {
        if (layout.rect.contains(pos)) {
            hit = &layout;
            emit tuneRequested(hit->spot.frequency);
            event->accept();
            return;
        }
    }

    // If user has clicked on the passband, we need to resize it.


    const double f = xToFreq(pos.x());
    lastTuneFreq = f;
    dragActive = true;

    event->accept();
}

void SpectrumItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!dragActive)
        return;

    if (!(event->buttons() & Qt::LeftButton)) {
        // button released but we didn't get release event for some reason
        dragActive = false;
        return;
    }
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const qreal x = event->pos().x();
#else
    const qreal x = event->position().x();
#endif

    const double f = xToFreq(x);

    const double minStepHz = 10.0; // tune step threshold
    if (qAbs(f - lastTuneFreq) * 1e6 < minStepHz)
        return;

    lastTuneFreq = f;
    emit tuneRequested(f); // drag update
    event->accept();
}

void SpectrumItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    dragActive = false;
    event->accept();
}

void SpectrumItem::hoverMoveEvent(QHoverEvent *event)
{
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QPointF pos = event->pos();
#else
    QPointF pos = event->position();
#endif
    const SpotLayout *hit = nullptr;

    if (!spotLayouts.isEmpty()) {
        for (const auto &layout : std::as_const(spotLayouts)) {
            if (layout.rect.contains(pos)) {
                hit = &layout;
                break;
            }
        }

        if (hit) {
            if (!hoverActive ||
                hit->spot.dxcall != currentHoverSpot.dxcall ||
                hit->spot.frequency != currentHoverSpot.frequency) {

                hoverActive      = true;
                currentHoverSpot = hit->spot;
                currentHoverPos  = hit->rect.center();

                emit hoverSpotChanged(currentHoverSpot, currentHoverPos, true);
            }
        } else if (hoverActive) {
            hoverActive = false;
            emit hoverSpotChanged(currentHoverSpot, currentHoverPos, false);
        }
    }

    event->accept();
}

void SpectrumItem::hoverLeaveEvent(QHoverEvent *event)
{
    Q_UNUSED(event);
    if (hoverActive) {
        hoverActive = false;
        emit hoverSpotChanged(currentHoverSpot, currentHoverPos, false);
    }
}

void SpectrumItem::wheelEvent(QWheelEvent *event)
{

    // Standard: angleDelta is in 1/8 of a degree. 15° per notch -> 120 units.
    const QPoint numDegrees = event->angleDelta() / 8;
    int steps = 0;

    if (!numDegrees.isNull()) {
        steps = numDegrees.y() / 15;   // vertical wheel only
    }

    if (steps != 0) {
        emit wheelTuneRequested(steps, event->modifiers());
        event->accept();
    } else {
        event->ignore();
    }
}


void SpectrumItem::setMaxSpotRows(int rows)
{
    if (rows < 1) rows = 1;
    if (maxSpotRows == rows)
        return;

    maxSpotRows = rows;
    emit spotsChanged();
    update();
}

void SpectrumItem::setSpots(const QVector<spotData> &newSpots)
{
    spots = newSpots;
    emit spotsChanged();
    update();
}

void SpectrumItem::clearSpots()
{
    if (spots.isEmpty())
        return;

    spots.clear();
    emit spotsChanged();
    update();
}

void SpectrumItem::setBands(const QVector<bandType> &newBands)
{
    bands = newBands;
    update();
}

void SpectrumItem::clearBands()
{
    if (bands.isEmpty())
        return;
    bands.clear();
    update();
}


void SpectrumItem::updateScope(const scopeData &data)
{
    if (!data.valid || data.data.isEmpty())
        return;

    const int n = data.data.size();
    if (n <= 1)
        return;

    if (!qFuzzyCompare(startFreq, data.startFreq) ||
        !qFuzzyCompare(endFreq,   data.endFreq)) {
        startFreq = data.startFreq;
        endFreq   = data.endFreq;
        emit freqRangeChanged();
    } else {
        startFreq = data.startFreq;
        endFreq   = data.endFreq;
    }

    if (mags.size() != n) {
        mags.resize(n);
        peaks.resize(n);
        peaks.fill(0);
    }

    // decay existing peaks
    if (!peaks.isEmpty() && decay > 0.0f) {
        const int dec = int(decay);
        for (int i = 0; i < peaks.size(); ++i) {
            int p = peaks[i] - dec;
            if (p < 0) p = 0;
            peaks[i] = quint8(p);
        }
    }

    const uchar *raw = reinterpret_cast<const uchar *>(data.data.constData());

    int span = int(ceiling) - int(floor);
    if (span <= 0)
        span = 1;

    for (int i = 0; i < n; ++i) {
        quint8 v = raw[i];
        int vInt = int(v);
        if (vInt < int(floor))
            vInt = int(floor);
        else if (vInt > int(ceiling))
            vInt = int(ceiling);

        int scaled = (vInt - int(floor)) * 255 / span;

        if (scaled < 0)   scaled = 0;
        if (scaled > 255) scaled = 255;

        mags[i] = quint8(scaled);
        if (mags[i] > peaks[i])
            peaks[i] = mags[i];
    }

    update();
}

static void updateBackgroundNode(QSGGeometryNode *node,
                                 float w, float h,
                                 const QColor &color)
{
    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->allocate(4);
    geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);

    auto *v = geom->vertexDataAsPoint2D();
    v[0].set(0, 0);
    v[1].set(0, h);
    v[2].set(w, 0);
    v[3].set(w, h);

    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    Q_ASSERT(mat);
    mat->setColor(color);

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

static void updateGridNode(QSGGeometryNode *node,
                           float w, float plotH,
                           double floorDb, double ceilingDb,
                           double gridStep,
                           const QColor &color)
{
    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawLines);

    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    Q_ASSERT(mat);
    mat->setColor(color);

    if (gridStep <= 0.0 || ceilingDb <= floorDb) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return;
    }

    const int count = int((ceilingDb - floorDb) / gridStep) + 1;
    if (count <= 1) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return;
    }

    const int verts = 2 * count;
    geom->allocate(verts);

    auto *v = geom->vertexDataAsPoint2D();

    auto yForDb = [&](double db) -> float {
        if (ceilingDb <= floorDb)
            return plotH;
        double t = (db - floorDb) / (ceilingDb - floorDb); // 0..1 bottom..top
        t = qBound(0.0, t, 1.0);
        return float((1.0 - t) * plotH);
    };

    int idx = 0;
    for (int i = 0; i < count; ++i) {
        const double db = floorDb + i * gridStep;
        const float  y  = yForDb(db);
        v[idx++].set(0.0f, y);
        v[idx++].set(w,    y);
    }

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}


static void updateFillStripNode(QSGGeometryNode *node,
                                const QVector<quint8> &mags,
                                float w, float plotH,
                                const QColor &color)
{
    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);

    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    Q_ASSERT(mat);
    mat->setColor(color);

    const int n = mags.size();
    if (n <= 1) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return;
    }

    const int vertCount = n * 2;
    geom->allocate(vertCount);

    auto *vtx = geom->vertexDataAsPoint2D();
    const float denom = float(qMax(1, n - 1));

    for (int i = 0; i < n; ++i) {
        float xNorm = float(i) / denom;
        float x     = xNorm * w;
        float yTop  = (1.0f - (float(mags[i]) / 255.0f)) * plotH;
        float yBot  = plotH;

        vtx[2 * i].set(x, yTop);
        vtx[2 * i + 1].set(x, yBot);
    }

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}


static void updateLineNode(QSGGeometryNode *node,
                           const QVector<quint8> &vals,
                           float w, float plotH,
                           const QColor &color)
{
    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawLineStrip);

    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    Q_ASSERT(mat);
    mat->setColor(color);

    const int n = vals.size();
    if (n <= 1) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return;
    }

    geom->allocate(n);

    auto *vtx = geom->vertexDataAsPoint2D();
    const float denom = float(qMax(1, n - 1));

    for (int i = 0; i < n; ++i) {
        float xNorm = float(i) / denom;
        float x     = xNorm * w;
        float y     = (1.0f - (float(vals[i]) / 255.0f)) * plotH;
        vtx[i].set(x, y);
    }

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}



static void updateFrequencyLineNode(QSGGeometryNode *node,
                           float plotX, float plotH,
                           const QColor &color)
{


    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawLineStrip);

    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    Q_ASSERT(mat);
    mat->setColor(color);

    geom->allocate(2);

    auto *vals = geom->vertexDataAsPoint2D();
    vals[0].set(plotX, 0);
    vals[1].set(plotX, plotH);

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}



static void updatePassbandNode(QSGGeometryNode *node,
                                    float plotX, float plotW, float plotH,
                                    const QColor &color)
{


    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawLineStrip);

    auto *mat = static_cast<QSGFlatColorMaterial *>(node->material());
    Q_ASSERT(mat);
    mat->setColor(color);
    geom->allocate(4);
    auto *vals = geom->vertexDataAsPoint2D();

    vals[0].set(plotX, 0);
    vals[1].set(plotX, plotH);
    vals[2].set(plotW, 0);
    vals[3].set(plotW, plotH);

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

static void updateGradientFillNode(QSGGeometryNode *node,
                                   const QVector<quint8> &mags,
                                   float w,
                                   float plotH,
                                   const QColor &topCol,
                                   const QColor &botCol)
{
    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);

    const int n = mags.size();
    if (n <= 1 || plotH <= 0.0f) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry);
        return;
    }

    const int vertCount = n * 2;
    geom->allocate(vertCount);

    auto *vtx = geom->vertexDataAsColoredPoint2D();
    const float denom = float(qMax(1, n - 1));

    auto lerp = [](float a, float b, float t) {
        return a + (b - a) * t;
    };

    for (int i = 0; i < n; ++i) {
        float xNorm = float(i) / denom;
        float x     = xNorm * w;

        // trace height (0 at bottom, plotH at top)
        float yTrace = (1.0f - (float(mags[i]) / 255.0f)) * plotH;
        float yTop   = yTrace;
        float yBot   = plotH;

        // normalized [0..1], 0 = bottom, 1 = top
        float tTop = (plotH > 0.0f) ? (1.0f - (yTop / plotH)) : 0.0f;

        QColor cTop;
        cTop.setRedF(  lerp(botCol.redF(),   topCol.redF(),   tTop));
        cTop.setGreenF(lerp(botCol.greenF(), topCol.greenF(), tTop));
        cTop.setBlueF( lerp(botCol.blueF(),  topCol.blueF(),  tTop));
        cTop.setAlphaF(lerp(botCol.alphaF(), topCol.alphaF(), tTop));

        QColor cBot = botCol; // floor uses bottom color

        // --- top vertex (on trace) ---
        QSGGeometry::ColoredPoint2D &vtTop = vtx[2 * i];
        vtTop.x = x;
        vtTop.y = yTop;
        vtTop.r = static_cast<uchar>(cTop.red());
        vtTop.g = static_cast<uchar>(cTop.green());
        vtTop.b = static_cast<uchar>(cTop.blue());
        vtTop.a = static_cast<uchar>(cTop.alpha());

        // --- bottom vertex (floor) ---
        QSGGeometry::ColoredPoint2D &vtBot = vtx[2 * i + 1];
        vtBot.x = x;
        vtBot.y = yBot;
        vtBot.r = static_cast<uchar>(cBot.red());
        vtBot.g = static_cast<uchar>(cBot.green());
        vtBot.b = static_cast<uchar>(cBot.blue());
        vtBot.a = static_cast<uchar>(cBot.alpha());
    }

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

}


static void updateBandShapesNode(QSGGeometryNode      *node,
                                 const QVector<bandType> &bands,
                                 double startFreqMHz,
                                 double endFreqMHz,
                                 float w,
                                 float plotH)
{
    auto *geom = node->geometry();
    Q_ASSERT(geom);
    geom->setDrawingMode(QSGGeometry::DrawTriangles);

    const int bandCountTotal = bands.size();
    if (bandCountTotal == 0 || w <= 0.0f || plotH <= 0.0f || endFreqMHz <= startFreqMHz) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry);
        return;
    }

    const double spanMHz = endFreqMHz - startFreqMHz;

    // band bar geometry
    const float barThickness = 2.0f;   // px
    const float barY0        = 2.0f;   // top of spectrum area
    const float barY1        = barY0 + barThickness;
    const float centerY      = barY0 + barThickness * 0.5f;

    // arrows
    const float arrowLength  = 6.0f;   // horizontal arrow length
    const float arrowHeight  = 6.0f;   // arrow height

    // First pass: count visible bands
    int visibleBands = 0;
    for (const bandType &b : bands) {
        if (b.color.alpha() <= 0)
            continue;

        double lowF_MHz  = double(b.lowFreq)  / 1e6;
        double highF_MHz = double(b.highFreq) / 1e6;

        if (highF_MHz <= startFreqMHz || lowF_MHz >= endFreqMHz)
            continue;

        double t1 = (lowF_MHz  - startFreqMHz) / spanMHz;
        double t2 = (highF_MHz - startFreqMHz) / spanMHz;

        if (t2 <= 0.0 || t1 >= 1.0)
            continue;

        visibleBands++;
    }

    if (visibleBands == 0) {
        geom->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry);
        return;
    }

    // Per band: 2 triangles (bar) + 1 triangle (right arrow) + 1 triangle (left arrow)
    // => (2 + 1 + 1) * 3 = 12 vertices per band
    const int vertsPerBand = 12;
    const int totalVerts   = visibleBands * vertsPerBand;

    geom->allocate(totalVerts);
    auto *vtx = geom->vertexDataAsColoredPoint2D();
    int idx   = 0;

    auto addVertex = [&](float x, float y, const QColor &c) {
        QSGGeometry::ColoredPoint2D &v = vtx[idx++];
        v.x = x;
        v.y = y;
        v.r = static_cast<uchar>(c.red());
        v.g = static_cast<uchar>(c.green());
        v.b = static_cast<uchar>(c.blue());
        v.a = static_cast<uchar>(c.alpha());
    };

    for (const bandType &b : bands) {
        if (b.color.alpha() <= 0)
            continue;

        double lowF_MHz  = double(b.lowFreq)  / 1e6;
        double highF_MHz = double(b.highFreq) / 1e6;

        if (highF_MHz <= startFreqMHz || lowF_MHz >= endFreqMHz)
            continue;

        double t1 = (lowF_MHz  - startFreqMHz) / spanMHz;
        double t2 = (highF_MHz - startFreqMHz) / spanMHz;

        // clamp into [0..1]
        if (t1 < 0.0) t1 = 0.0;
        if (t2 > 1.0) t2 = 1.0;
        if (t2 <= t1)
            continue;

        const float x1 = float(t1 * w);
        const float x2 = float(t2 * w);

        const QColor col = b.color;

        // --- main band bar (2 triangles) ---
        // Tri1: (x1, barY0), (x2, barY0), (x1, barY1)
        addVertex(x1, barY0, col);
        addVertex(x2, barY0, col);
        addVertex(x1, barY1, col);

        // Tri2: (x2, barY0), (x2, barY1), (x1, barY1)
        addVertex(x2, barY0, col);
        addVertex(x2, barY1, col);
        addVertex(x1, barY1, col);

        // --- right arrow (triangle pointing right) ---
        {
            float ax = x2;

            // tip is inside band, pointing outward
            float tipX = ax + arrowLength;
            float tipY = centerY;
            float baseTopX = ax;
            float baseTopY = centerY - arrowHeight * 0.5f;
            float baseBotX = ax;
            float baseBotY = centerY + arrowHeight * 0.5f;

            addVertex(tipX,     tipY,    col); // tip
            addVertex(baseTopX, baseTopY,col); // base top
            addVertex(baseBotX, baseBotY,col); // base bottom
        }

        // --- left arrow (triangle pointing left) ---
        {
            float ax = x1;

            float tipX = ax - arrowLength;
            float tipY = centerY;
            float baseTopX = ax;
            float baseTopY = centerY - arrowHeight * 0.5f;
            float baseBotX = ax;
            float baseBotY = centerY + arrowHeight * 0.5f;

            addVertex(tipX,     tipY,    col); // tip
            addVertex(baseTopX, baseTopY,col); // base top
            addVertex(baseBotX, baseBotY,col); // base bottom
        }
    }

    node->markDirty(QSGNode::DirtyGeometry);
}


QSGNode *SpectrumItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QElapsedTimer timer;
    timer.start();

    const int n = mags.size();
    if (n <= 1 || width() <= 0 || height() <= 0) {
        delete oldNode;
        return nullptr;
    }

    auto *root = static_cast<SpectrumRootNode *>(oldNode);

    if (!root) {
        root = new SpectrumRootNode;

        // --- background ---
        root->background = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->background->setGeometry(geom);
            root->background->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->background->setMaterial(mat);
            root->background->setFlag(QSGNode::OwnsMaterial);
        }

        // --- gridLines ---
        root->gridLines = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawLines);
            root->gridLines->setGeometry(geom);
            root->gridLines->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->gridLines->setMaterial(mat);
            root->gridLines->setFlag(QSGNode::OwnsMaterial);
        }

        // --- fillNode ---
        root->fillNode = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->fillNode->setGeometry(geom);
            root->fillNode->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->fillNode->setMaterial(mat);
            root->fillNode->setFlag(QSGNode::OwnsMaterial);
        }

        // ---- gradientFill -----
        root->gradientFill = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->gradientFill->setGeometry(geom);
            root->gradientFill->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGVertexColorMaterial;
            root->gradientFill->setMaterial(mat);
            root->gradientFill->setFlag(QSGNode::OwnsMaterial);
        }

        // --- peaksFill ---
        root->peaksFill = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->peaksFill->setGeometry(geom);
            root->peaksFill->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->peaksFill->setMaterial(mat);
            root->peaksFill->setFlag(QSGNode::OwnsMaterial);
        }

        // ---- peaksGradient -----
        root->peaksGradient = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->peaksGradient->setGeometry(geom);
            root->peaksGradient->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGVertexColorMaterial;
            root->peaksGradient->setMaterial(mat);
            root->peaksGradient->setFlag(QSGNode::OwnsMaterial);
        }

        // --- peaksLine ---
        root->peaksLine = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawLineStrip);
            root->peaksLine->setGeometry(geom);
            root->peaksLine->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->peaksLine->setMaterial(mat);
            root->peaksLine->setFlag(QSGNode::OwnsMaterial);
        }

        // --- spectrumLine ---
        root->spectrumLine = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawLineStrip);
            root->spectrumLine->setGeometry(geom);
            root->spectrumLine->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->spectrumLine->setMaterial(mat);
            root->spectrumLine->setFlag(QSGNode::OwnsMaterial);
        }

        // -- passband rectangle ---
        root->passband = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->passband->setGeometry(geom);
            root->passband->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->passband->setMaterial(mat);
            root->passband->setFlag(QSGNode::OwnsMaterial);
        }

        // -- frequency line ---
        root->frequencyLine = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 2);
            geom->setDrawingMode(QSGGeometry::DrawLines);
            root->frequencyLine->setGeometry(geom);
            root->frequencyLine->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGFlatColorMaterial;
            root->frequencyLine->setMaterial(mat);
            root->frequencyLine->setFlag(QSGNode::OwnsMaterial);
        }

        root->axisNode     = new QSGSimpleTextureNode;

        root->bandShapes   = new QSGGeometryNode;
        {
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawTriangles);  // we’ll use triangles
            root->bandShapes->setGeometry(geom);
            root->bandShapes->setFlag(QSGNode::OwnsGeometry);

            auto *mat = new QSGVertexColorMaterial;
            root->bandShapes->setMaterial(mat);
            root->bandShapes->setFlag(QSGNode::OwnsMaterial);
        }

        // --- dynamic root for all the throwaway stuff ---
        root->dynamicRoot = new QSGNode;

        root->appendChildNode(root->background);
        root->appendChildNode(root->gridLines);
        root->appendChildNode(root->peaksFill);
        root->appendChildNode(root->peaksGradient);
        root->appendChildNode(root->peaksLine);
        root->appendChildNode(root->fillNode);
        root->appendChildNode(root->gradientFill);
        root->appendChildNode(root->spectrumLine);
        root->appendChildNode(root->passband);
        root->appendChildNode(root->frequencyLine);
        root->appendChildNode(root->bandShapes);
        root->appendChildNode(root->axisNode);

        root->appendChildNode(root->dynamicRoot);
    }
    else {
        // clear ONLY dynamic children (axis, bands, spots, overlays...)
        QSGNode *child = root->dynamicRoot->firstChild();
        while (child) {
            QSGNode *next = child->nextSibling();
            root->dynamicRoot->removeChildNode(child);
            delete child;
            child = next;
        }
    }

    const float w = float(width());
    const float h = float(height());
    const float axisH  = float(qMax(0, axis));
    const float plotH  = h - axisH;

    if (plotH <= 0.0f) {
        delete root;
        return nullptr;
    }

    // --- Persistent bits ---

    // 1) Background
    updateBackgroundNode(root->background, w, h, colors.plotBackground);

    // 2) Grid
    updateGridNode(root->gridLines, w, plotH, floor, ceiling, gridStep, colors.gridColor);

    if (!colors.useUnderlayFillGradient) {
        updateFillStripNode(root->peaksFill, peaks, w, plotH, colors.underlayFill);
        QSGGeometry *g = root->peaksGradient->geometry();
        Q_ASSERT(g);
        g->allocate(0);
        root->peaksGradient->markDirty(QSGNode::DirtyGeometry);
    } else {
        // no flat fill in gradient mode
        updateFillStripNode(root->peaksFill, QVector<quint8>(), w, plotH, Qt::transparent);
        // gradient fill using vertex colors
        updateGradientFillNode(root->peaksGradient,
                               peaks,
                               w,
                               plotH,
                               colors.underlayFillTop,
                               colors.underlayFillBot);
    }

    // 4) Peaks line
    updateLineNode(root->peaksLine, peaks, w, plotH,colors.underlayLine);

    // 3) Fill under spectrum (flat only)
    if (!colors.useSpectrumFillGradient) {
        updateFillStripNode(root->fillNode, mags, w, plotH, colors.spectrumFill);
        QSGGeometry *g = root->gradientFill->geometry();
        Q_ASSERT(g);
        g->allocate(0);
        root->gradientFill->markDirty(QSGNode::DirtyGeometry);
    } else {
        updateFillStripNode(root->fillNode, QVector<quint8>(), w, plotH, Qt::transparent);
        updateGradientFillNode(root->gradientFill,
                               mags,
                               w,
                               plotH,
                               colors.spectrumFillTop,
                               colors.spectrumFillBot);
    }

    // 5) Live spectrum line
    updateLineNode(root->spectrumLine, mags, w, plotH,colors.spectrumLine);


    if (endFreq > startFreq) {

        const double span = endFreq - startFreq;

        auto freqToX = [&](double fMHz) -> float {
            double t = (fMHz - startFreq) / span;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            return float(t * w);
        };

        {
            float x1 = freqToX(passbandLow);
            float x2 = freqToX(passbandHigh);
            if (x2 < x1) std::swap(x1, x2);

            auto *geom = root->passband->geometry();
            auto *v = geom->vertexDataAsPoint2D();
            v[0].set(x1, 0.0f);
            v[1].set(x1, plotH);
            v[2].set(x2, 0.0f);
            v[3].set(x2, plotH);
            root->passband->markDirty(QSGNode::DirtyGeometry);

            auto *mat = static_cast<QSGFlatColorMaterial *>(root->passband->material());
            mat->setColor(colors.passband);
            root->passband->markDirty(QSGNode::DirtyMaterial);
        }

        {
            const float x = freqToX(center);

            auto *geom = root->frequencyLine->geometry();
            auto *v = geom->vertexDataAsPoint2D();
            v[0].set(x, 0.0f);
            v[1].set(x, plotH);
            root->frequencyLine->markDirty(QSGNode::DirtyGeometry);

            auto *mat = static_cast<QSGFlatColorMaterial *>(root->frequencyLine->material());
            mat->setColor(colors.tuningLine);
            root->frequencyLine->markDirty(QSGNode::DirtyMaterial);
        }
    } else {
        root->frequencyLine->geometry()->allocate(0);
        root->frequencyLine->markDirty(QSGNode::DirtyGeometry);
        root->passband->geometry()->allocate(0);
        root->passband->markDirty(QSGNode::DirtyGeometry);
    }

    // --- Dynamic stuff (unchanged logic, but append to dynamicRoot instead of root) ---
    QSGNode *dyn = root->dynamicRoot;

    // --- AXIS at bottom (frequency in MHz) via QImage + persistent QSGSimpleTextureNode ---
    if (axisH > 0.0f && endFreq > startFreq && ticks >= 2 && window()) {
        const int imgW = int(w);
        const int imgH = int(axisH);

        if (imgW > 0 && imgH > 0) {

            QImage img(imgW, imgH, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);

            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::TextAntialiasing, true);

            const QColor axisColor(200, 200, 200);
            const QColor textColor(220, 220, 220);

            const float baselineY  = axisH - 1.0f;      // bottom of axis area
            const float tickHeight = axisH * 0.30f;     // shorter tick
            const float textMargin = 2.0f;              // small padding

            // baseline at the bottom of axis
            p.setPen(axisColor);
            p.drawLine(QPointF(0.0, baselineY),
                       QPointF(w - 1.0f, baselineY));

            const double span = endFreq - startFreq;

            QFont font;
            font.setPointSizeF(axisH * 0.45f);
            p.setFont(font);

            for (int i = 0; i < ticks; ++i) {
                const double t    = (ticks == 1) ? 0.0 : double(i) / double(ticks - 1);
                const double fMHz = startFreq + t * span;
                const float  x    = float(t * (w - 1.0f));

                // LABEL (just above baseline)
                QString label;
                if      (span < 0.2) label = QString::number(fMHz, 'f', 3);
                else if (span < 2.0) label = QString::number(fMHz, 'f', 2);
                else                 label = QString::number(fMHz, 'f', 1);

                p.setPen(textColor);
                QRectF textBounds = p.boundingRect(QRectF(), Qt::AlignCenter, label);

                const float textX = x - textBounds.width() * 0.5f;
                const float textY = baselineY - textBounds.height() - textMargin;

                p.drawText(QRectF(textX, textY,
                                  textBounds.width(), textBounds.height()),
                           Qt::AlignCenter, label);

                // TICK (above the number, pointing upward)
                p.setPen(axisColor);

                const float tickTopY = textY - tickHeight;   // above label
                const float tickBotY = textY - textMargin;   // just above label

                p.drawLine(QPointF(x, tickBotY),
                           QPointF(x, tickTopY));
            }

            p.end();

            QSGTexture *tex = window()->createTextureFromImage(img);
            root->axisNode->setTexture(tex);
            root->axisNode->setOwnsTexture(true);

            // Position axis under the plot, full width
            root->axisNode->setRect(0, plotH, w, axisH);
        } else {
            // invalid size -> hide axis
            root->axisNode->setRect(0, 0, 0, 0);
        }
    } else {
        // conditions not met -> hide axis
        root->axisNode->setRect(0, 0, 0, 0);
    }

    // --- BAND INDICATORS (multiple bands) ----------------------------------
    if (!bands.isEmpty() && width() > 0 && height() > 0 && endFreq > startFreq) {

        const float wItem       = float(width());
        const float hItem       = float(height());
        const float axisHeight  = float(axis);       // 0 if no axis
        const float plotHeight  = hItem - axisHeight;

        // 1) shapes (bars + arrows) with a single geometry node
        updateBandShapesNode(root->bandShapes,
                             bands,
                             startFreq,   // MHz
                             endFreq,     // MHz
                             wItem,
                             plotHeight);

        // 2) labels – still as texture nodes under dynamicRoot
        QSGNode *dyn = root->dynamicRoot;

        const float barThickness = 2.0f;
        const float barY0        = 2.0f;
        const float barY1        = barY0 + barThickness;
        const float labelMargin  = 2.0f;
        const int   labelHeight  = 14;

        const double span = endFreq - startFreq;
        if (span > 0.0) {
            for (const bandType &b : std::as_const(bands)) {

                if (b.name.isEmpty() || b.color.alpha() == 0)
                    continue;

                double lowF_MHz  = double(b.lowFreq)  / 1e6;
                double highF_MHz = double(b.highFreq) / 1e6;

                // completely out of view?
                if (highF_MHz <= startFreq || lowF_MHz >= endFreq)
                    continue;

                double t1 = (lowF_MHz  - startFreq) / span;
                double t2 = (highF_MHz - startFreq) / span;

                if (t2 <= 0.0 || t1 >= 1.0)
                    continue;

                if (t1 < 0.0) t1 = 0.0;
                if (t2 > 1.0) t2 = 1.0;
                if (t2 <= t1)
                    continue;

                const float x1 = float(t1 * wItem);
                const float x2 = float(t2 * wItem);
                const int   bandWidthPx = int(std::max(0.0f, x2 - x1));

                if (bandWidthPx <= 10)
                    continue;

                const float labelY = barY1 + labelMargin;

                QImage img(bandWidthPx, labelHeight,
                           QImage::Format_ARGB32_Premultiplied);
                img.fill(Qt::transparent);

                QPainter p(&img);
                p.setRenderHint(QPainter::TextAntialiasing, true);

                QFont font;
                font.setPointSize(9);
                p.setFont(font);

                QColor textColor = b.color;
                QColor outlineColor(0, 0, 0, 200);

                QRect rect(0, 0, bandWidthPx, labelHeight);

                // outline
                p.setPen(outlineColor);
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (!dx && !dy) continue;
                        p.drawText(rect.translated(dx, dy),
                                   Qt::AlignCenter,
                                   b.name);
                    }
                }

                p.setPen(textColor);
                p.drawText(rect, Qt::AlignCenter, b.name);

                p.end();

                QSGTexture *tex = window()->createTextureFromImage(img);
                auto *labelNode = new QSGSimpleTextureNode;
                labelNode->setTexture(tex);
                labelNode->setOwnsTexture(true);
                labelNode->setRect(x1, labelY, bandWidthPx, labelHeight);

                dyn->appendChildNode(labelNode);
            }
        }
    } else {
        // no bands -> clear bandShapes geometry
        root->bandShapes->geometry()->allocate(0);
        root->bandShapes->markDirty(QSGNode::DirtyGeometry);
    }

    // --- CLUSTER SPOTS (DX spots at top of spectrum) ---

    spotLayouts.clear();
    if (!spots.isEmpty() && endFreq > startFreq &&
        width() > 0 && height() > 0 && window()) {

        const float itemWidth  = float(width());
        const float itemHeight = float(height());

        const float axisHeight = float(axis);  // or 0.0f if you don't use axis
        const float plotHeight = itemHeight - axisHeight;

        int fontPx = int(qBound(8.0, plotHeight * 0.08, 16.0));
        int rows   = qMax(1, maxSpotRows);

        QFont font;
        font.setPointSize(fontPx);
        font.setBold(true);

        QFontMetricsF fm(font);
        const int textHeight   = int(std::ceil(fm.height()));
        const int marginTop    = 2;
        const int verticalPad  = 4;
        const int rowHeight    = textHeight + verticalPad;
        const int overlayHeight = marginTop + rows * rowHeight;

        if (overlayHeight > 0) {

            QImage overlay(int(itemWidth), overlayHeight,
                           QImage::Format_ARGB32_Premultiplied);
            overlay.fill(Qt::transparent);

            QPainter painter(&overlay);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            painter.setFont(font);

            QColor baseTextColor(255, 255, 255);
            QColor baseOutlineColor(0, 0, 0, 200);

            QVector<QVector<QPair<float, float>>> rowRanges(rows);
            spotLayouts.reserve(spots.size());

            const QDateTime now = QDateTime::currentDateTimeUtc();

            auto placeSpot = [&](const spotData &s) {
                double alphaFactor = 1.0;
                if (!s.current) {
                    QDateTime ts = s.timestamp;
                    if (ts.timeSpec() != Qt::UTC)
                        ts = ts.toUTC();

                    qint64 ageSec = ts.secsTo(now); // positive if now >= ts

                    if (ageSec) {
                        if (ageSec >= (s.timeout * 60)) {
                            alphaFactor = 0.0;
                        } else {
                            const double span = double(s.timeout * 60);
                            alphaFactor = double((s.timeout * 60) - ageSec) / span;
                        }
                    }
                }

                if (alphaFactor <= 0.0)
                    return;

                double span = endFreq - startFreq;
                if (span <= 0.0)
                    return;

                double freqMHz = s.frequency;

                if (freqMHz < startFreq || freqMHz > endFreq)
                    return;

                double t = (freqMHz - startFreq) / span;
                float x = float(t * itemWidth);

                QString label = s.dxcall;
                if (label.isEmpty())
                    return;

                float textWidth = float(fm.horizontalAdvance(label));
                if (textWidth <= 0.0f)
                    return;

                float halfWidth = textWidth * 0.5f;
                float left  = x - halfWidth;
                float right = x + halfWidth;

                if (left < 0.0f) {
                    right -= left;
                    left = 0.0f;
                }
                if (right > itemWidth) {
                    float excess = right - itemWidth;
                    left  -= excess;
                    right -= excess;
                    if (left < 0.0f) left = 0.0f;
                }

                int chosenRow = -1;
                for (int r = 0; r < rows && chosenRow < 0; ++r) {
                    bool overlaps = false;
                    for (const auto &range : rowRanges[r]) {
                        if (!(right < range.first || left > range.second)) {
                            overlaps = true;
                            break;
                        }
                    }
                    if (!overlaps)
                        chosenRow = r;
                }

                if (chosenRow < 0)
                    return;

                rowRanges[chosenRow].append(qMakePair(left, right));

                float y = float(marginTop + chosenRow * rowHeight);
                QRectF rect(left, y, textWidth, textHeight);

                // record this for hover hit-testing (item coords == overlay coords)
                SpotLayout layout;
                layout.rect = rect;
                layout.spot = s;
                spotLayouts.push_back(layout);


                QColor textColor   = baseTextColor;
                QColor outlineColor = baseOutlineColor;

                int textAlpha   = int(textColor.alpha()   * alphaFactor + 0.5);
                int outlineAlpha = int(outlineColor.alpha() * alphaFactor + 0.5);

                textColor.setAlpha(textAlpha);
                outlineColor.setAlpha(outlineAlpha);

                if (textAlpha <= 0 && outlineAlpha <= 0)
                    return;

                // outline
                painter.setPen(outlineColor);
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (!dx && !dy) continue;
                        painter.drawText(rect.translated(dx, dy),
                                         Qt::AlignCenter,
                                         label);
                    }
                }

                painter.setPen(textColor);
                painter.drawText(rect, Qt::AlignCenter, label);
            };

            QVector<spotData> sorted = spots;
            std::sort(sorted.begin(), sorted.end(),
                      [](const spotData &a, const spotData &b) {
                          return a.frequency < b.frequency;
                      });

            for (const spotData &s : sorted)
                placeSpot(s);

            painter.end();

            auto *spotNode = new QSGSimpleTextureNode;
            QSGTexture *tex = window()->createTextureFromImage(overlay);
            spotNode->setTexture(tex);
            spotNode->setOwnsTexture(true);
            spotNode->setRect(0, 0, itemWidth, overlayHeight);

            dyn->appendChildNode(spotNode);
        }
    }


    // --- OVF label (top-left) ---
    if (overflow && window()) {
        const int wPx = 40;
        const int hPx = 16;

        QImage img(wPx, hPx, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        QFont font;
        font.setBold(true);
        font.setPointSize(9);
        p.setFont(font);

        QRect rect(0, 0, wPx, hPx);

        p.fillRect(rect, QColor(200, 0, 0, 220));   // red background
        p.setPen(Qt::white);
        p.drawText(rect, Qt::AlignCenter, QStringLiteral("OVF"));

        p.end();

        QSGTexture *tex = window()->createTextureFromImage(img);
        auto *node = new QSGSimpleTextureNode;
        node->setTexture(tex);
        node->setOwnsTexture(true);

        const float marginX = 2.0f;
        const float marginY = 6.0f;
        node->setRect(marginX, marginY, wPx, hPx);

        dyn->appendChildNode(node);
    }


    // --- SCOPE OUT OF RANGE (center) ---
    if (outOfRange && window()) {
        const int wPx = 220;
        const int hPx = 24;

        QImage img(wPx, hPx, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        QFont font;
        font.setBold(true);
        font.setPointSize(10);
        p.setFont(font);

        QRect rect(0, 0, wPx, hPx);

        // translucent dark background
        p.fillRect(rect, QColor(0, 0, 0, 160));

        // yellow text
        p.setPen(QColor(255, 255, 0));
        p.drawText(rect, Qt::AlignCenter, QStringLiteral("SCOPE OUT OF RANGE"));

        p.end();

        QSGTexture *tex = window()->createTextureFromImage(img);
        auto *node = new QSGSimpleTextureNode;
        node->setTexture(tex);
        node->setOwnsTexture(true);

        const float wItem = float(width());
        const float hItem = float(height());
        const float x = (wItem - wPx) * 0.5f;
        const float y = (hItem - hPx) * 0.5f;

        node->setRect(x, y, wPx, hPx);

        dyn->appendChildNode(node);
    }

    emit processingTimeNs(timer.nsecsElapsed());
    return root;
}
