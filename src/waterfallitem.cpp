// WaterfallItem.cpp
#include "waterfallitem.h"

#include "ReceiverController.h"

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

    // Never allow a non-positive height
    const int targetHeight = qMax(1, length);

    // (Re)allocate image when width OR height changes
    if (img.isNull() || imgWidth != w || img.height() != targetHeight) {
        imgWidth = w;
        img = QImage(imgWidth, targetHeight, QImage::Format_RGB32);
        img.fill(Qt::black);
        ringIndex = 0;  // reset ring on size change
    }

    const int imgH = img.height();
    if (imgH <= 0)
        return;

    // ---- ring index: ALWAYS clamp to image height ----
    if (ringIndex < 0 || ringIndex >= imgH)
        ringIndex = 0;

    // advance ring one line up
    ringIndex = (ringIndex - 1 + imgH) % imgH;

    // destination row
    QRgb *dstRow = reinterpret_cast<QRgb *>(img.scanLine(ringIndex));

    // source values – MUST be bytes (0..255) for LUT indexing
    const uchar *src = reinterpret_cast<const uchar *>(data.data.constData());

    // LUT is built for indices 0..255 only
    rebuildLut();
    const QRgb *lut = valueLut;

    for (int i = 0; i < imgWidth; ++i) {
        unsigned idx = src[i];      // uchar => 0..255

        // belt & braces; basically free
        if (idx > 255)
            idx = 255;

        dstRow[i] = lut[idx];
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

    // IMPORTANT: discard any previous node tree completely
    if (oldNode) {
        delete oldNode;
        oldNode = nullptr;
    }

    auto *root = static_cast<WaterfallRootNode *>(oldNode);
    if (!root) {
        root = new WaterfallRootNode;
        root->topNode    = new QSGSimpleTextureNode;
        root->bottomNode = new QSGSimpleTextureNode;
        root->appendChildNode(root->topNode);
        root->appendChildNode(root->bottomNode);
    }

    if (!window()) {
        emit processingTimeNs(timer.nsecsElapsed());
        return root;
    }

    // Create separate textures for top and bottom
    QSGTexture *texTop = window()->createTextureFromImage(img);
    texTop->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    texTop->setMipmapFiltering(QSGTexture::None);
    texTop->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    texTop->setVerticalWrapMode(QSGTexture::ClampToEdge);

    QSGTexture *texBottom = window()->createTextureFromImage(img);
    texBottom->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    texBottom->setMipmapFiltering(QSGTexture::None);
    texBottom->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    texBottom->setVerticalWrapMode(QSGTexture::ClampToEdge);

    root->topNode->setTexture(texTop);
    root->topNode->setOwnsTexture(true);

    root->bottomNode->setTexture(texBottom);
    root->bottomNode->setOwnsTexture(true);

    const int texW = img.width();
    const int texH = img.height();
    if (texH <= 0) {
        emit processingTimeNs(timer.nsecsElapsed());
        return root;
    }

    const qreal itemW = width();
    const qreal itemH = height();

    // Clamp ringIndex defensively
    int ring = ringIndex;
    if (ring < 0)     ring = 0;
    if (ring >= texH) ring = texH - 1;

    const int topLines    = texH - ring; // ring .. texH-1
    const int bottomLines = ring;        // 0 .. ring-1

    // ----- Top segment -----
    if (topLines > 0) {
        const qreal topHeight = itemH * (qreal(topLines) / texH);

        root->topNode->setRect(0, 0, itemW, topHeight);
        root->topNode->setSourceRect(QRectF(0, ring, texW, topLines));
        root->topNode->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    } else {
        root->topNode->setRect(0, 0, 0, 0);
    }

    // ----- Bottom segment -----
    if (bottomLines > 0) {
        const qreal topHeight    = itemH * (qreal(topLines) / texH);
        const qreal bottomHeight = itemH - topHeight;

        root->bottomNode->setRect(0, topHeight, itemW, bottomHeight);
        root->bottomNode->setSourceRect(QRectF(0, 0, texW, bottomLines));
        root->bottomNode->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    } else {
        root->bottomNode->setRect(0, 0, 0, 0);
    }

    emit processingTimeNs(timer.nsecsElapsed());
    return root;
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

void WaterfallItem::setController(QObject* c)
{
    if (m_controller == c) return;
    if (m_controller) disconnect(m_controller, nullptr, this, nullptr);

    m_controller = c;

    auto *rx = qobject_cast<ReceiverController*>(c);
    if (rx) {
        connect(rx, &ReceiverController::scopeUpdated,
                this, &WaterfallItem::updateScope,
                Qt::DirectConnection);          // same thread (GUI)
    }


    emit controllerChanged();
    update();
}

