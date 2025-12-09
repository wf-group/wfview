// WaterfallItem.cpp
#include "waterfallitem.h"
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

WaterfallItem::WaterfallItem()
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}


void WaterfallItem::setLength(int d)
{
    if (d <= 0 || d == length)
        return;

    const int oldH = img.isNull() ? 0 : img.height();
    const int oldW = imgWidth;   // width is fixed in your case

    length = d;

    if (!img.isNull() && oldH > 1 && oldW > 0) {
        // Vertically resample old image into new height
        QImage newImg(oldW, length, QImage::Format_RGB32);
        newImg.fill(Qt::black);

        const int newH = length;

        for (int yNew = 0; yNew < newH; ++yNew) {
            // Map new row to old row (nearest neighbour)
            int yOld = (newH > 1) ? (yNew * (oldH - 1) / (newH - 1)) : 0;
            if (yOld < 0)      yOld = 0;
            if (yOld >= oldH)  yOld = oldH - 1;

            const QRgb *srcRow = reinterpret_cast<const QRgb *>(
                img.constScanLine(yOld)
                );
            QRgb *dstRow = reinterpret_cast<QRgb *>(
                newImg.scanLine(yNew)
                );

            // width is fixed, so copy one row as-is
            memcpy(dstRow, srcRow, oldW * int(sizeof(QRgb)));
        }

        img = std::move(newImg);
    } else {
        // No valid old image; let updateScope allocate later
        img = QImage();
    }

    dirty = true;
    emit lengthChanged();
    update();
}

void WaterfallItem::setTheme(Theme t)
{
    if (t == theme)
        return;
    theme = t;
    dirty = true;     // force redraw with new colours
    emit themeChanged();
    update();
}

// WaterfallItem.cpp
void WaterfallItem::setSmooth(bool s)
{
    if (smooth == s) return;
    smooth = s;
    emit smoothChanged();
    dirty = true;
    update();
}

// grid/scale configuration
void WaterfallItem::setFloor (uchar v)
{
    if (floor == v) return;
    floor = v;
    emit scaleChanged();
    update();
}

void WaterfallItem::setCeiling(uchar v)
{
    if (ceiling == v) return;
    ceiling = v;
    emit scaleChanged();
    update();
}

double WaterfallItem::xToFreq(qreal x) const
{
    if (endFreq <= startFreq || width() <= 0.0)
        return startFreq;

    double t = double(x) / double(width()); // 0..1
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;

    return startFreq + t * (endFreq - startFreq);
}

void WaterfallItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    event->accept();
}

void WaterfallItem::mouseDoubleClickEvent(QMouseEvent *event)
{

    qInfo() << "Double click event!";

    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    const qreal x = event->pos().x();   // Qt 6
#else
    const qreal x = event->position().x();   // Qt 6
#endif
    const double f = xToFreq(x);

    emit tuneRequested(f);
    event->accept();
}

void WaterfallItem::wheelEvent(QWheelEvent *event)
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


void WaterfallItem::updateScope(const scopeData &data)
{
    if (!data.valid || data.data.isEmpty())
        return;

    const int w = data.data.size();
    if (w <= 0)
        return;

    if (!qFuzzyCompare(startFreq, data.startFreq) ||
        !qFuzzyCompare(endFreq,   data.endFreq)) {
        startFreq = data.startFreq;
        endFreq   = data.endFreq;
    }

    // Allocate or resize image if needed
    if (img.isNull()) {
        imgWidth = w;
        img = QImage(imgWidth, length, QImage::Format_RGB32);
        img.fill(Qt::black);
    }

    if (img.height() != length) {
        QImage newImg(imgWidth, length, QImage::Format_RGB32);
        newImg.fill(Qt::black);

        const int oldH = img.height();
        const int newH = length;

        for (int yNew = 0; yNew < newH; ++yNew) {
            int yOld = (newH > 1 && oldH > 1) ? (yNew * (oldH - 1) / (newH - 1)) : 0;
            if (yOld < 0)      yOld = 0;
            if (yOld >= oldH)  yOld = oldH - 1;

            const QRgb *srcRow = reinterpret_cast<const QRgb *>(img.constScanLine(yOld));
            QRgb *dstRow       = reinterpret_cast<QRgb *>(newImg.scanLine(yNew));
            memcpy(dstRow, srcRow, imgWidth * int(sizeof(QRgb)));
        }

        img = std::move(newImg);
    }

    const int rowBytes = img.bytesPerLine();
    const int H = length;

    for (int y = H - 1; y > 0; --y) {
        uchar *dst = img.scanLine(y);
        const uchar *src = img.constScanLine(y - 1);
        memmove(dst, src, rowBytes);
    }

    // ---- Write newest line at TOP (row 0)
    const uchar *src = reinterpret_cast<const uchar *>(data.data.constData());
    QRgb *dstRow = reinterpret_cast<QRgb *>(img.scanLine(0));


    const quint8 floorVal   = floor;    // 0..255
    const quint8 ceilingVal = ceiling;  // 0..255

    // avoid degenerate range
    int span = int(ceilingVal) - int(floorVal);
    if (span <= 0)
        span = 1;

    for (int i = 0; i < imgWidth; ++i) {
        quint8 v = src[i];

        int vInt = int(v);

        if (vInt < int(floorVal))
            vInt = int(floorVal);
        else if (vInt > int(ceilingVal))
            vInt = int(ceilingVal);

        int scaled = (vInt - int(floorVal)) * 255 / span;

        if (scaled < 0)   scaled = 0;
        if (scaled > 255) scaled = 255;

        dstRow[i] = colorForValue(quint8(scaled));
    }


    dirty = true;
    update();
}


QSGNode *WaterfallItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{    
    if (width() <= 0 || height() <= 0 || img.isNull()) {
        delete oldNode;
        return nullptr;
    }

    QElapsedTimer timer;
    timer.start();

    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);

    if (!node) {
        node = new QSGSimpleTextureNode;
    } else {
        while (QSGNode *child = node->firstChild()) {
            node->removeChildNode(child);
            delete child;
        }
    }

    if (dirty) {

        QSGTexture *tex = window()->createTextureFromImage(img);
        // Filtering:
        tex->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
        tex->setMipmapFiltering(QSGTexture::None);
        tex->setHorizontalWrapMode(QSGTexture::ClampToEdge);
        tex->setVerticalWrapMode(QSGTexture::ClampToEdge);

        node->setTexture(tex);
        node->setOwnsTexture(true);
        dirty = false;
    }

    node->setRect(0, 0, width(), height());
    emit processingTimeNs(timer.nsecsElapsed());

    return node;
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
    }

    return qRgb(r, g, b);
}




