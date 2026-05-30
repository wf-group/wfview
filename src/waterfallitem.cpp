#include "waterfallitem.h"
#include "ReceiverController.h"

#include <cmath>
#include <cstring>

WaterfallItem::WaterfallItem()
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(QQuickItem::ItemHasContents, true);
    qInfo() << "Creating new WaterfallItem()";
}

void WaterfallItem::setLength(int d)
{
    if (d <= 0 || d == length)
        return;

    length = d;

    {
        QMutexLocker lock(&imgMu);
        // Pull real history out of the raw ring at the new length instead of
        // resampling the RGB image (which would blur/lose data).
        rebuildImageFromRaw();
    }

    emit lengthChanged();
    update();
}

void WaterfallItem::setTheme(Theme t)
{
    if (t == theme)
        return;
    theme = t;
    {
        QMutexLocker lock(&imgMu);
        // Recolor the entire waterfall with the new theme.
        rebuildImageFromRaw();
    }
    emit themeChanged();
    update();
}

void WaterfallItem::setSmooth(bool s)
{
    if (smooth == s) return;
    smooth = s;
    {
        QMutexLocker lock(&imgMu);
        dirty = true;
    }
    emit smoothChanged();
    update();
}

void WaterfallItem::setFloor(uchar v)
{
    if (floor == v) return;
    floor = v;
    {
        QMutexLocker lock(&imgMu);
        // Rescale the entire waterfall with the new floor.
        rebuildImageFromRaw();
    }
    emit scaleChanged();
    update();
}

void WaterfallItem::setCeiling(uchar v)
{
    if (ceiling == v) return;
    ceiling = v;
    {
        QMutexLocker lock(&imgMu);
        // Rescale the entire waterfall with the new ceiling.
        rebuildImageFromRaw();
    }
    emit scaleChanged();
    update();
}

double WaterfallItem::xToFreq(qreal x) const
{
    if (endFreq <= startFreq || width() <= 0.0)
        return startFreq;

    double t = double(x) / double(width());
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    return startFreq + t * (endFreq - startFreq);
}

void WaterfallItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        event->accept();
    else
        event->ignore();
}

void WaterfallItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const qreal x = event->pos().x();
#else
    const qreal x = event->position().x();
#endif

    emit tuneRequested(xToFreq(x));
    event->accept();
}

void WaterfallItem::updateScope(const scopeData &data)
{
    if (!data.valid || data.data.isEmpty())
        return;

    QElapsedTimer timer;
    timer.start();

    const int w = data.data.size();
    if (w <= 0)
        return;

    if (!qFuzzyCompare(startFreq, data.startFreq) ||
        !qFuzzyCompare(endFreq,   data.endFreq)) {
        startFreq = data.startFreq;
        endFreq   = data.endFreq;
    }

    const int targetHeight = qMax(1, length);

    const uchar *src = reinterpret_cast<const uchar *>(data.data.constData());

    QMutexLocker lock(&imgMu);

    // Always store the raw line first so history is available for redraws.
    pushRawLine(src, w);

    // If the RGB cache doesn't match the current width/length, rebuild it
    // wholesale from the raw ring; otherwise just append one new RGB line
    // (the cheap, cached path).
    if (img.isNull() || imgWidth != w || img.height() != targetHeight) {
        rebuildImageFromRaw();
    } else {
        const int imgH = img.height();
        if (ringIndex < 0 || ringIndex >= imgH)
            ringIndex = 0;

        ringIndex = (ringIndex - 1 + imgH) % imgH;

        rebuildLut();
        const QRgb *lut = valueLut;

        QRgb *dstRow = reinterpret_cast<QRgb *>(img.scanLine(ringIndex));
        for (int i = 0; i < imgWidth; ++i)
            dstRow[i] = lut[src[i]];

        dirty = true;
    }

    scopeUpdateTimeNs.store(timer.nsecsElapsed(), std::memory_order_relaxed);
    update();
}

// Caller must hold imgMu.
void WaterfallItem::pushRawLine(const uchar *src, int w)
{
    if (w <= 0)
        return;

    // A change in bin count invalidates the whole ring.
    if (w != rawWidth) {
        rawWidth = w;
        rawRing.assign(size_t(rawCapacity) * size_t(rawWidth), uchar(0));
        rawHead  = -1;
        rawCount = 0;
    }

    rawHead = (rawHead + 1) % rawCapacity;
    std::memcpy(&rawRing[size_t(rawHead) * size_t(rawWidth)], src, size_t(rawWidth));
    if (rawCount < rawCapacity)
        ++rawCount;
}

// Caller must hold imgMu.
// Rebuilds the entire RGB cache from the raw ring at the current length,
// scaling each value through the LUT (floor/ceiling/theme). The newest line
// is placed at row 0 so the existing top/bottom ring rendering continues to
// work, with subsequent incremental lines filling rows backwards from there.
void WaterfallItem::rebuildImageFromRaw()
{
    const int h = qMax(1, length);

    if (rawWidth <= 0 || rawCount <= 0) {
        // No data yet; just present a black image of the right size.
        if (img.isNull() || img.height() != h || imgWidth <= 0) {
            img = QImage();
            imgWidth = 0;
        }
        ringIndex = 0;
        dirty = true;
        return;
    }

    if (img.isNull() || imgWidth != rawWidth || img.height() != h) {
        imgWidth = rawWidth;
        img = QImage(imgWidth, h, QImage::Format_RGB32);
    }
    img.fill(Qt::black);

    rebuildLut();
    const QRgb *lut = valueLut;

    // Only as many rows as we have history for; the rest stay black.
    const int lines = qMin(h, rawCount);
    for (int row = 0; row < lines; ++row) {
        // row 0 = newest, row k = k-th most recent raw line.
        const int rawIdx = (rawHead - row + rawCapacity) % rawCapacity;
        const uchar *src = &rawRing[size_t(rawIdx) * size_t(rawWidth)];
        QRgb *dst = reinterpret_cast<QRgb *>(img.scanLine(row));
        for (int i = 0; i < imgWidth; ++i)
            dst[i] = lut[src[i]];
    }

    ringIndex = 0;
    dirty = true;
}

QSGNode *WaterfallItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QElapsedTimer timer;
    timer.start();

    // Take a stable snapshot for the render thread.
    QImage snapshot;
    int ring = 0;
    bool doUpload = false;

    {
        QMutexLocker lock(&imgMu);

        if (width() <= 0 || height() <= 0 || img.isNull()) {
            delete oldNode;
            emit processingTimeNs(timer.nsecsElapsed() + scopeUpdateTimeNs.exchange(0, std::memory_order_relaxed));
            return nullptr;
        }

        ring = ringIndex;
        doUpload = dirty;

        // IMPORTANT: deep copy so the render thread never sees live-mutating pixels.
        // This is the thing that stops the crashes.
        snapshot = doUpload ? img.copy() : img; // if not dirty, sharing is fine

        dirty = false;
    }

    auto *root = static_cast<WaterfallRootNode *>(oldNode);
    if (!root) {
        root = new WaterfallRootNode;
        root->topNode    = new QSGSimpleTextureNode;
        root->bottomNode = new QSGSimpleTextureNode;
        root->appendChildNode(root->topNode);
        root->appendChildNode(root->bottomNode);

        root->topNode->setOwnsTexture(false);
        root->bottomNode->setOwnsTexture(false);
    }

    if (!window()) {
        emit processingTimeNs(timer.nsecsElapsed() + scopeUpdateTimeNs.exchange(0, std::memory_order_relaxed));
        return root;
    }

    const int texW = snapshot.width();
    const int texH = snapshot.height();
    if (texW <= 0 || texH <= 0) {
        emit processingTimeNs(timer.nsecsElapsed() + scopeUpdateTimeNs.exchange(0, std::memory_order_relaxed));
        return root;
    }

    // Clamp ring defensively
    ring = qBound(0, ring, texH - 1);

    // Recreate ONE texture only when needed (dirty or size change)
    const QSize wantSize(texW, texH);
    if (!root->tex || root->texSize != wantSize || doUpload) {

        // 1) Create new texture first (so we never set nullptr on the nodes)
        QSGTexture *newTex = window()->createTextureFromImage(snapshot);

        newTex->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
        newTex->setMipmapFiltering(QSGTexture::None);
        newTex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        newTex->setVerticalWrapMode(QSGTexture::ClampToEdge);

        // 2) Point nodes at the new texture (non-null)
        root->topNode->setTexture(newTex);
        root->bottomNode->setTexture(newTex);

        // 3) Now it’s safe to delete the old texture
        delete root->tex;
        root->tex = newTex;
        root->texSize = wantSize;
    }


    const qreal itemW = width();
    const qreal itemH = height();

    const int topLines    = texH - ring;
    const int bottomLines = ring;

    // Snap join to device pixels to reduce “join crawl”
    const qreal dpr = window()->effectiveDevicePixelRatio();
    auto snap = [dpr](qreal v) { return std::round(v * dpr) / dpr; };

    if (topLines > 0) {
        const qreal topH = snap(itemH * (qreal(topLines) / texH));
        root->topNode->setRect(0, 0, snap(itemW), topH);
        root->topNode->setSourceRect(QRectF(0, ring, texW, topLines));
        root->topNode->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    } else {
        root->topNode->setRect(0, 0, 0, 0);
    }

    if (bottomLines > 0) {
        const qreal topH = snap(itemH * (qreal(topLines) / texH));
        const qreal botH = snap(itemH - topH);
        root->bottomNode->setRect(0, topH, snap(itemW), botH);
        root->bottomNode->setSourceRect(QRectF(0, 0, texW, bottomLines));
        root->bottomNode->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    } else {
        root->bottomNode->setRect(0, 0, 0, 0);
    }

    emit processingTimeNs(timer.nsecsElapsed() + scopeUpdateTimeNs.exchange(0, std::memory_order_relaxed));
    return root;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void WaterfallItem::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickItem::geometryChange(n, o);
    if (n.size() != o.size()) update();
}
#else
void WaterfallItem::geometryChanged(const QRectF &n, const QRectF &o)
{
    QQuickItem::geometryChanged(n, o);
    if (n.size() != o.size()) update();
}
#endif

void WaterfallItem::setController(QObject *c)
{
    if (controller == c)
        return;

    //if (controller)
    //    disconnect(controller, nullptr, this, nullptr);

    controller = c;

    auto * rx=qobject_cast<ReceiverController *>(c);

    if (rx) {
        connect(rx, &ReceiverController::scopeUpdated,
                this, &WaterfallItem::updateScope,
                Qt::QueuedConnection);
    }

    emit controllerChanged();
}



QRgb WaterfallItem::colorForValue(quint8 v) const
{
    float t = v / 255.0f;   // 0..1

    auto lerp = [](float a, float b, float t) -> float {
        return a + (b - a) * t;
    };

    int r=0, g=0, b=0;

    switch (theme)
    {
    case Jet:
        if (t < 0.125f) {
            r = 0;
            g = 0;
            b = int(lerp(131, 170, t / 0.125f));
        } else if (t < 0.375f) {
            r = 0;
            g = int(lerp(60, 255, (t - 0.125f) / 0.25f));
            b = 255;
        } else if (t < 0.625f) {
            r = int(lerp(5, 255, (t - 0.375f) / 0.25f));
            g = 255;
            b = int(lerp(255, 0, (t - 0.375f) / 0.25f));
        } else if (t < 0.875f) {
            r = 255;
            g = int(lerp(255, 0, (t - 0.625f) / 0.25f));
            b = 0;
        } else {
            r = int(lerp(250, 128, (t - 0.875f) / 0.125f));
            g = 0;
            b = 0;
        }
        break;

    case Cold:
        r = int(t * 255);
        g = int(t * 255);
        b = 255;
        break;

    case Hot:
        if (t < 0.33f) {
            r = int(lerp(0, 255, t / 0.33f));
            g = 0;
            b = 0;
        } else if (t < 0.66f) {
            r = 255;
            g = int(lerp(0, 255, (t - 0.33f) / 0.33f));
            b = 0;
        } else {
            r = 255;
            g = 255;
            b = int(lerp(0, 255, (t - 0.66f) / 0.34f));
        }
        break;

    case Thermal:
        if (t < 0.20f) {
            r = 0;
            g = 0;
            b = int(lerp(50, 255, t / 0.20f));
        } else if (t < 0.40f) {
            r = 0;
            g = int(lerp(5, 255, (t - 0.20f) / 0.20f));
            b = 255;
        } else if (t < 0.60f) {
            r = int(lerp(0, 255, (t - 0.40f) / 0.20f));
            g = 255;
            b = int(lerp(255, 0, (t - 0.40f) / 0.20f));
        } else if (t < 0.80f) {
            r = 255;
            g = int(lerp(255, 70, (t - 0.60f) / 0.20f));
            b = 0;
        } else {
            r = int(lerp(255, 170, (t - 0.80f) / 0.20f));
            g = 0;
            b = 0;
        }
        break;

    case Night:
        if (t < 0.35f) {
            r = 0;
            g = 0;
            b = int(lerp(0, 150, t / 0.35f));
        } else if (t < 0.75f) {
            r = int(lerp(0, 50, (t - 0.35f) / 0.40f));
            g = int(lerp(0, 50, (t - 0.35f) / 0.40f));
            b = int(lerp(150, 255, (t - 0.35f) / 0.40f));
        } else {
            r = int(lerp(50, 255, (t - 0.75f) / 0.25f));
            g = int(lerp(50, 255, (t - 0.75f) / 0.25f));
            b = 255;
        }
        break;

    case Ion:
        if (t < 0.30f) {
            r = int(lerp(50, 0, t / 0.30f));
            g = 0;
            b = int(lerp(50, 255, t / 0.30f));
        } else if (t < 0.60f) {
            r = 0;
            g = int(lerp(0, 255, (t - 0.30f) / 0.30f));
            b = 255;
        } else {
            r = int(lerp(0, 255, (t - 0.60f) / 0.40f));
            g = 255;
            b = int(lerp(255, 0, (t - 0.60f) / 0.40f));
        }
        break;

    case Grayscale:
        r = g = b = int(t * 255);
        break;

    case Geography:
        if (t < 0.25f) {
            r = int(lerp(70, 150, t / 0.25f));
            g = int(lerp(70, 255, t / 0.25f));
            b = int(lerp(255, 150, t / 0.25f));
        } else if (t < 0.50f) {
            r = int(lerp(150, 255, (t - 0.25f) / 0.25f));
            g = int(lerp(255, 255, (t - 0.25f) / 0.25f));
            b = int(lerp(150, 0, (t - 0.25f) / 0.25f));
        } else if (t < 0.75f) {
            r = 255;
            g = int(lerp(255, 120, (t - 0.50f) / 0.25f));
            b = 0;
        } else {
            r = int(lerp(255, 255, (t - 0.75f) / 0.25f));
            g = int(lerp(120, 0, (t - 0.75f) /0.25f));
            b = int(lerp(0, 0, (t - 0.75f) / 0.25f));
        }
        break;

    case Hues: {
        QColor c = QColor::fromHsvF(t, 1.0, 1.0);
        return c.rgb();
    }

    case Polar:
        if (t < 0.50f) {
            r = int(lerp(0, 255, t / 0.50f));
            g = int(lerp(0, 255, t / 0.50f));
            b = 255;
        } else {
            r = 255;
            g = int(lerp(255, 0, (t - 0.50f) / 0.50f));
            b = int(lerp(255, 0, (t - 0.50f) / 0.50f));
        }
        break;

    case Spectrum:
        if (t < 0.25f) {
            r = 0;
            g = int(lerp(0, 255, t / 0.25f));
            b = 255;
        } else if (t < 0.50f) {
            r = 0;
            g = 255;
            b = int(lerp(255, 0, (t - 0.25f) / 0.25f));
        } else if (t < 0.75f) {
            r = int(lerp(0, 255, (t - 0.50f) / 0.25f));
            g = 255;
            b = 0;
        } else {
            r = 255;
            g = int(lerp(255, 0, (t - 0.75f) / 0.25f));
            b = 0;
        }
        break;

    case Candy:
        if (t < 0.50f) {
            r = int(lerp(255, 255, t / 0.50f));
            g = int(lerp(60, 180, t / 0.50f));
            b = int(lerp(60, 255, t / 0.50f));
        } else {
            r = int(lerp(255, 80, (t - 0.50f) / 0.50f));
            g = int(lerp(180, 0, (t - 0.50f) / 0.50f));
            b = int(lerp(255, 255, (t - 0.50f) / 0.50f));
        }
        break;
    default:
        r=0;g=0;b=0;
        break;
    }

    return qRgb(r, g, b);
}


void WaterfallItem::rebuildLut()
{
    // Quantize and clamp into byte domain
    int floorI = int(std::floor(floor));
    int ceilI  = int(std::ceil(ceiling));

    if (ceilI <= floorI)
        ceilI = floorI + 1;

    floorI = qBound(0, floorI, 255);
    ceilI  = qBound(0, ceilI, 255);

    // Nothing changed? keep old LUT
    if (lutValid &&
        lutFloor == floorI &&
        lutCeil  == ceilI &&
        previousTheme == theme)
        return;

    lutFloor      = floorI;
    lutCeil       = ceilI;
    previousTheme = theme;

    const int span = lutCeil - lutFloor;

    for (int v = 0; v < 256; ++v) {
        int vInt = v;
        if (vInt < lutFloor)      vInt = lutFloor;
        else if (vInt > lutCeil)  vInt = lutCeil;

        int scaled = (vInt - lutFloor) * 255 / span;
        if (scaled < 0)   scaled = 0;
        if (scaled > 255) scaled = 255;

        valueLut[v] = colorForValue(quint8(scaled));
    }

    lutValid = true;
}
