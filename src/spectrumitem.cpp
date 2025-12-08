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

    const quint8 floorVal   = floor;    // 0..255
    const quint8 ceilingVal = ceiling;  // 0..255

    // avoid degenerate range
    int span = int(ceilingVal) - int(floorVal);
    if (span <= 0)
        span = 1;

    for (int i = 0; i < n; ++i) {
        quint8 v = raw[i];

        // ---------- CORRECT FLOOR / CEILING MAPPING ----------
        int vInt = int(v);

        // clamp to [floor, ceiling]
        if (vInt < int(floorVal))
            vInt = int(floorVal);
        else if (vInt > int(ceilingVal))
            vInt = int(ceilingVal);

        // map floor -> 0, ceiling -> 255
        int scaled = (vInt - int(floorVal)) * 255 / span;

        if (scaled < 0)   scaled = 0;
        if (scaled > 255) scaled = 255;

        mags[i] = quint8(scaled);
        if (mags[i] > peaks[i])
            peaks[i] = mags[i];
    }

    update();   // triggers updatePaintNode()
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

    QSGNode *root = static_cast<QSGNode *>(oldNode);
    if (!root) {
        root = new QSGNode;
    } else {
        root->removeAllChildNodes();
    }

    const float w = float(width());
    const float h = float(height());
    const float axisH  = float(qMax(0, axis));
    const float plotH  = h - axisH;
    const float denom  = float(qMax(1, n - 1));

    if (plotH <= 0.0f) {
        // no room for plot -> bail
        delete root;
        return nullptr;
    }

    auto freqToX = [&](double fMHz) -> float {
        double span = endFreq - startFreq;
        if (span <= 0.0)
            return 0.0f;
        double t = (fMHz - startFreq) / span; // 0..1
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        return float(t * w);
    };

    auto yForDb = [&](double db) -> float {
        if (ceiling <= floor)
            return h; // degenerate -> bottom
        double t = (db - floor) / (ceiling - floor); // 0..1 bottom..top
        t = qBound(0.0, t, 1.0);
        return float((1.0 - t) * plotH);
    };

    auto yForSample = [&](quint8 v) -> float {
        double t = double(v) / 255.0;
        double db = floor + t * (ceiling - floor);
        return yForDb(db);
    };

    // --- helper: line for mags or peaks ---
    auto makeLine = [&](const QVector<quint8> &vals, const QColor &color) -> QSGGeometryNode * {
        auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), n);
        geom->setDrawingMode(QSGGeometry::DrawLineStrip);
        geom->setLineWidth(1.0f);

        auto *vtx = geom->vertexDataAsPoint2D();
        for (int i = 0; i < n; ++i) {
            float xNorm = float(i) / denom;
            float x     = xNorm * w;
            float y     = yForSample(vals[i]);
            vtx[i].set(x, y);
        }

        auto *mat = new QSGFlatColorMaterial;
        mat->setColor(color);

        auto *node = new QSGGeometryNode;
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
        return node;
    };



    // --- 1) Background rect ---
    {
        auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);

        auto *v = geom->vertexDataAsPoint2D();
        v[0].set(0,    0);
        v[1].set(0,    h);
        v[2].set(w,    0);
        v[3].set(w,    h);

        auto *mat = new QSGFlatColorMaterial;
        mat->setColor(colors.plotBackground);

        auto *node = new QSGGeometryNode;
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);

        root->appendChildNode(node);
    }


    // --- 2) Gridlines ---
    if (gridStep > 0.0 && ceiling > floor) {
        // major lines only for now
        const int count = int((ceiling - floor) / gridStep) + 1;
        if (count > 1) {
            const int verts = 2 * count;
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), verts);
            geom->setDrawingMode(QSGGeometry::DrawLines);
            auto *v = geom->vertexDataAsPoint2D();

            int idx = 0;
            for (int i = 0; i < count; ++i) {
                double db = floor + i * gridStep;
                float y = yForDb(db);

                v[idx++].set(0.0f, y);
                v[idx++].set(w,    y);
            }

            auto *mat = new QSGFlatColorMaterial;
            mat->setColor(colors.gridColor);

            auto *node = new QSGGeometryNode;
            node->setGeometry(geom);
            node->setFlag(QSGNode::OwnsGeometry);
            node->setMaterial(mat);
            node->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(node);
        }
    }


    // --- FILL under live spectrum
    if (!colors.useSpectrumFillGradient){

        const int vertCount = n * 2;
        auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertCount);
        geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        auto *vtx = geom->vertexDataAsPoint2D();

        for (int i = 0; i < n; ++i) {
            float xNorm = float(i) / denom;
            float x     = xNorm * w;
            float yTop  = yForSample(mags[i]);
            float yBot  = plotH;

            vtx[2 * i].set(x, yTop); // top on trace
            vtx[2 * i + 1].set(x, yBot); // bottom on floor
        }

        auto *mat = new QSGFlatColorMaterial;

        mat->setColor(colors.spectrumFill);

        auto *node = new QSGGeometryNode;
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    } else {
        // --- GRADIENT FILL under live spectrum (multi-band, FlatColorMaterial only) ---
        {
            // Number of gradient bands – tweak to taste (16–32 looks smooth enough)
            const int bands = 24;

            // Ensure we have usable colours
            QColor topCol = colors.spectrumFillTop;
            QColor botCol = colors.spectrumFillBot;

            // Precompute the trace Y for each X once
            QVector<float> yTrace(n);
            for (int i = 0; i < n; ++i) {
                yTrace[i] = yForSample(mags[i]);  // top of fill at this column
            }

            for (int band = 0; band < bands; ++band) {
                // t0/t1 are 0..1 down from the trace towards the bottom
                const float t0 = float(band) / float(bands);
                const float t1 = float(band + 1) / float(bands);
                const float tm = 0.5f * (t0 + t1);     // mid-point for colour interp

                // Interpolate band colour between topCol and botCol
                auto lerp = [](float a, float b, float t) {
                    return a + (b - a) * t;
                };

                QColor bandCol;
                bandCol.setRedF(  lerp(topCol.redF(),   botCol.redF(),   tm) );
                bandCol.setGreenF(lerp(topCol.greenF(), botCol.greenF(), tm) );
                bandCol.setBlueF( lerp(topCol.blueF(),  botCol.blueF(),  tm) );
                bandCol.setAlphaF(lerp(topCol.alphaF(), botCol.alphaF(), tm) );

                if (bandCol.alpha() <= 0.0f)
                    continue; // fully transparent; skip

                // Geometry: triangle strip, 2 vertices per x (top band edge, bottom band edge)
                const int vertCount = n * 2;
                auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), vertCount);
                geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
                auto *vtx = geom->vertexDataAsPoint2D();

                for (int i = 0; i < n; ++i) {
                    float xNorm = float(i) / denom;
                    float x     = xNorm * w;

                    const float yTop   = yTrace[i] + (plotH - yTrace[i]) * t0;
                    const float yBot   = yTrace[i] + (plotH - yTrace[i]) * t1;

                    // top edge of this band
                    vtx[2 * i].set(x, yTop);
                    // bottom edge of this band
                    vtx[2 * i + 1].set(x, yBot);
                }

                auto *mat = new QSGFlatColorMaterial;
                mat->setColor(bandCol);

                auto *node = new QSGGeometryNode;
                node->setGeometry(geom);
                node->setFlag(QSGNode::OwnsGeometry);
                node->setMaterial(mat);
                node->setFlag(QSGNode::OwnsMaterial);

                root->appendChildNode(node);
            }
        }
    }


    // --- 5) Peaks line
    if (!peaks.isEmpty())
        root->appendChildNode(makeLine(peaks, colors.underlayLine));

    // --- 6) Live Spectrum Line
    root->appendChildNode(makeLine(mags, colors.spectrumLine));


    // --- 3) Passband rectangle (fill + border), using normalized [0..1] range ---
    float x1 = freqToX(passbandLow);
    float x2 = freqToX(passbandHigh);

    if (x2 < x1)
        std::swap(x1, x2);

    // fill
    {
        auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        auto *v = geom->vertexDataAsPoint2D();
        v[0].set(x1, 0);
        v[1].set(x1, plotH);
        v[2].set(x2, 0);
        v[3].set(x2, plotH);

        auto *mat = new QSGFlatColorMaterial;
        mat->setColor(colors.passband);

        auto *node = new QSGGeometryNode;
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }


    // --- 7) Center frequency marker ---
    {
        float x = freqToX(center);
        auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 2);
        geom->setDrawingMode(QSGGeometry::DrawLines);
        auto *v = geom->vertexDataAsPoint2D();
        v[0].set(x, 0);
        v[1].set(x, plotH);

        auto *mat = new QSGFlatColorMaterial;
        mat->setColor(Qt::blue);

        auto *node = new QSGGeometryNode;
        node->setGeometry(geom);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(mat);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }

    // --- AXIS at bottom (frequency in MHz) via QImage + QSGSimpleTextureNode ---
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

            const float baselineY = axisH - 1.0f;     // bottom of axis area
            const float tickHeight = axisH * 0.30f;   // SHORTER tick
            const float textMargin = 2.0f;            // small padding

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

                //
                // LABEL (just above baseline)
                //
                QString label;
                if      (span < 0.2) label = QString::number(fMHz, 'f', 3);
                else if (span < 2.0) label = QString::number(fMHz, 'f', 2);
                else                 label = QString::number(fMHz, 'f', 1);

                p.setPen(textColor);
                QRectF textBounds = p.boundingRect(QRectF(), Qt::AlignCenter, label);

                const float textX = x - textBounds.width() * 0.5f;
                const float textY = baselineY - textBounds.height() - textMargin;

                p.drawText(QRectF(textX, textY, textBounds.width(), textBounds.height()),
                           Qt::AlignCenter, label);

                //
                // TICK (above the number, pointing upward)
                //
                p.setPen(axisColor);

                const float tickTopY = textY - tickHeight;     // ABOVE the label
                const float tickBotY = textY - textMargin;     // just above label

                p.drawLine(QPointF(x, tickBotY),
                           QPointF(x, tickTopY));
            }

            p.end();

            // Upload as a texture node
            QSGSimpleTextureNode *axisNode = new QSGSimpleTextureNode;
            QSGTexture *tex = window()->createTextureFromImage(img);

            axisNode->setTexture(tex);
            axisNode->setOwnsTexture(true);
            axisNode->setRect(0, plotH, w, axisH);

            root->appendChildNode(axisNode);
        }


        // --- BAND INDICATORS (multiple bands) ----------------------------------
        if (!bands.isEmpty() && width() > 0 && height() > 0 && endFreq > startFreq) {

            const float w = float(width());
            const float h = float(height());
            const float axisHeight  = float(axis);   // 0 if you don't have an axis
            const float plotHeight  = h - axisHeight;

            const float barThickness = 2.0f;   // px
            const float barY0 = 2.0f;         // top of spectrum area
            const float barY1 = barY0 + barThickness;

            const float arrowLength = 6.0f;   // horizontal arrow length
            const float arrowHeight = 6.0f;   // arrow height
            const float centerY     = barY0 + barThickness * 0.5f;

            const double span = endFreq - startFreq;
            if (span > 0.0) {

                for (const bandType &b : std::as_const(bands)) {

                    double lowF  = double(b.lowFreq) / 1e6;
                    double highF = double(b.highFreq) / 1e6;

                    // completely out of view?
                    if (highF <= startFreq || lowF >= endFreq || b.color.alpha() == 0)
                        continue;

                    double t1 = (lowF  - startFreq) / span;
                    double t2 = (highF - startFreq) / span;

                    // clamp into [0..1]
                    if (t1 < 0.0) t1 = 0.0;
                    if (t2 > 1.0) t2 = 1.0;
                    if (t2 <= t1)
                        continue;

                    const float x1 = float(t1 * w);
                    const float x2 = float(t2 * w);

                    // --- main band bar ---
                    {
                        QSGGeometry *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
                        geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
                        auto *v = reinterpret_cast<QSGGeometry::Point2D *>(geom->vertexData());

                        v[0].set(x1, barY0);
                        v[1].set(x2, barY0);
                        v[2].set(x1, barY1);
                        v[3].set(x2, barY1);

                        auto *mat = new QSGFlatColorMaterial;
                        mat->setColor(b.color);

                        QSGGeometryNode *barNode = new QSGGeometryNode;
                        barNode->setGeometry(geom);
                        barNode->setFlag(QSGNode::OwnsGeometry);
                        barNode->setMaterial(mat);
                        barNode->setFlag(QSGNode::OwnsMaterial);

                        root->appendChildNode(barNode);
                    }

                    // --- right arrow: horizontal, pointing right ---
                    {
                        QSGGeometry *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 3);
                        geom->setDrawingMode(QSGGeometry::DrawTriangles);
                        auto *v = reinterpret_cast<QSGGeometry::Point2D *>(geom->vertexData());

                        float ax = x2;
                        v[0].set(ax + arrowLength, centerY);                 // tip (inside band)
                        v[1].set(ax,                centerY - arrowHeight/2); // base top
                        v[2].set(ax,                centerY + arrowHeight/2); // base bottom

                        auto *mat = new QSGFlatColorMaterial;
                        mat->setColor(b.color);

                        QSGGeometryNode *arrowNode = new QSGGeometryNode;
                        arrowNode->setGeometry(geom);
                        arrowNode->setFlag(QSGNode::OwnsGeometry);
                        arrowNode->setMaterial(mat);
                        arrowNode->setFlag(QSGNode::OwnsMaterial);

                        root->appendChildNode(arrowNode);
                    }

                    // --- left arrow: horizontal, pointing left ---
                    {
                        QSGGeometry *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 3);
                        geom->setDrawingMode(QSGGeometry::DrawTriangles);
                        auto *v = reinterpret_cast<QSGGeometry::Point2D *>(geom->vertexData());

                        float ax = x1;
                        v[0].set(ax - arrowLength, centerY);                 // tip (inside band)
                        v[1].set(ax,                centerY - arrowHeight/2); // base top
                        v[2].set(ax,                centerY + arrowHeight/2); // base bottom

                        auto *mat = new QSGFlatColorMaterial;
                        mat->setColor(b.color);

                        QSGGeometryNode *arrowNode = new QSGGeometryNode;
                        arrowNode->setGeometry(geom);
                        arrowNode->setFlag(QSGNode::OwnsGeometry);
                        arrowNode->setMaterial(mat);
                        arrowNode->setFlag(QSGNode::OwnsMaterial);

                        root->appendChildNode(arrowNode);
                    }

                    // --- band name text (centered under the bar) ---
                    if (!b.name.isEmpty() && window()) {
                        const float labelMargin  = 2.0f;
                        const int   labelHeight  = 14;

                        const float labelY = barY1 + labelMargin;
                        const int bandWidthPx = int(std::max(0.0f, x2 - x1));

                        if (bandWidthPx > 10) {
                            QImage img(bandWidthPx, labelHeight, QImage::Format_ARGB32_Premultiplied);
                            img.fill(Qt::transparent);

                            QPainter p(&img);
                            p.setRenderHint(QPainter::TextAntialiasing, true);

                            QFont font;
                            font.setPointSize(9);
                            p.setFont(font);

                            QColor textColor(b.color);

                            QRect rect(0, 0, bandWidthPx, labelHeight);

                            // outline
                            for (int dx = -1; dx <= 1; ++dx) {
                                for (int dy = -1; dy <= 1; ++dy) {
                                    if (!dx && !dy) continue;
                                    p.drawText(rect.translated(dx, dy), Qt::AlignCenter, b.name);
                                }
                            }

                            p.setPen(textColor);
                            p.drawText(rect, Qt::AlignCenter, b.name);

                            p.end();

                            auto *tex = window()->createTextureFromImage(img);
                            auto *labelNode = new QSGSimpleTextureNode;
                            labelNode->setTexture(tex);
                            labelNode->setOwnsTexture(true);
                            labelNode->setRect(x1, labelY, bandWidthPx, labelHeight);

                            root->appendChildNode(labelNode);
                        }
                    }
                }
            }
        }


        // --- CLUSTER SPOTS (DX spots at top of spectrum) ---

        spotLayouts.clear();
        //hoverActive = false;

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

                root->appendChildNode(spotNode);
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

            // red background
            p.fillRect(rect, QColor(200, 0, 0, 220));

            // white text, centered
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

            root->appendChildNode(node);
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

            root->appendChildNode(node);
        }



    }
    emit processingTimeNs(timer.nsecsElapsed());
    return root;
}
