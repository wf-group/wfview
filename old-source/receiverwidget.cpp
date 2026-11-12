#include "receiverwidget.h"
#include "logcategories.h"
#include "rigidentities.h"
#include "themebridge.h" // Access main Qt theme from QML.

receiverWidget::receiverWidget(bool scope, uchar receiver, uchar vfo, QWidget *parent)
    : QGroupBox{parent}, receiver(receiver), numVFO(vfo)
{
    // Not sure if this should actually be used?
    Q_UNUSED(scope)

    QMutexLocker locker(&mutex);

    this->setObjectName("Spectrum Scope");
    this->setTitle("Band");
    this->defaultStyleSheet = this->styleSheet();
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();

    mainLayout = new QHBoxLayout(this);
    layout = new QVBoxLayout();
    mainLayout->addLayout(layout);

    mainLayout->setContentsMargins(0, 5, 0, 0); // Needs a small top margin
    layout->setContentsMargins(0, 0, 0, 0);

    originalParent = parent;

    scopeQuick = new QQuickWidget(this);
    scopeQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    scopeQuick->setMinimumHeight(200); // Force a sensible minimum
    scopeQuick->setContentsMargins(0,0,0,0);
    auto theme = new ThemeBridge(this);

    QWidget *styled = qobject_cast<QWidget*>(parent);
    if (!styled) styled = this;
    //theme->syncFrom(styled);

    scopeQuick->rootContext()->setContextProperty("Theme", theme);
    scopeQuick->setClearColor(theme->window());

    //scopeQuick->setAutoFillBackground(true);
    // This should make all signals come back to receiverWidget.

    QQmlEngine *engine = scopeQuick->engine();
    engine->rootContext()->setContextProperty("controller",this);

    scopeQuick->setSource(QUrl(QStringLiteral("qrc:/resources/Receiver.qml")));
    layout->addWidget(scopeQuick);

    scopeOrigParent = scopeQuick->parentWidget();
    if (scopeOrigParent)
        scopeOrigLayout = scopeOrigParent->layout();

    if (scopeOrigLayout)
        scopeOrigIndex = scopeOrigLayout->indexOf(scopeQuick);


    QObject *root = scopeQuick->rootObject();
    if (!root) {
        qWarning() << "ScopeView root object is null";
    } else {
        qDebug() << "RX" << receiver << "root object" << root;
        spectrum  = root->findChild<SpectrumItem*>("spectrum");
        waterfall = root->findChild<WaterfallItem*>("waterfall");
        freqDisplayA = root->findChild<FreqCtrlQuick*>("leftFreq");
        freqDisplayB = root->findChild<FreqCtrlQuick*>("rightFreq");
        if (!spectrum)
            qWarning() << "SpectrumItem 'spectrum' not found in QML";
        if (!waterfall)
            qWarning() << "WaterfallItem 'waterfall' not found in QML";
        connect(spectrum, &SpectrumItem::tuneRequested, this, &receiverWidget::setFreqFromScope);
        connect(waterfall, &WaterfallItem::tuneRequested, this, &receiverWidget::setFreqFromScope);
        connect(spectrum, &SpectrumItem::hoverSpotChanged,this, &receiverWidget::onHoverSpotChanged);
        connect(spectrum, &SpectrumItem::wheelTuneRequested,this, &receiverWidget::onWheelTuneRequested);
        connect(waterfall, &WaterfallItem::wheelTuneRequested,this, &receiverWidget::onWheelTuneRequested);
        spectrum->setFloor(plotFloor);
        spectrum->setCeiling(plotCeiling);
        waterfall->setFloor(wfFloor);
        waterfall->setCeiling(wfCeiling);


        connect(spectrum, &SpectrumItem::processingTimeNs, this, [=](const qint64 &time) {
            emit spectrumTime(time/1000000.0);
        });



        connect(waterfall, &WaterfallItem::processingTimeNs, this, [=](const qint64 &time) {
            emit waterfallTime(time/1000000.0);
        });

        freqDisplayA = root->findChild<FreqCtrlQuick*>("leftFreq");
        if (freqDisplayA)
        {
            // Setup the main frequency display
            freqDisplayA->setProperty("visible", true);
            freqDisplayA->setup(this->freqDigits, this->freqMin, this->freqMax, this->freqMinStep, this->freqUnit,nullptr);
            connect(freqDisplayA, &FreqCtrlQuick::newFrequency, this, [=](const qint64 &freq) {
                qInfo() << "got new frequency";
                this->newFrequency(freq,0);
            });

        }

        // These controls should only be displayed if there is a second VFO (not a second receiver)
        if (freqDisplayB && numVFO > 1)
        {
            freqDisplayB->setProperty("visible", true);
            freqDisplayB->setup(this->freqDigits, this->freqMin, this->freqMax, this->freqMinStep, this->freqUnit,nullptr);
            connect(freqDisplayB, &FreqCtrlQuick::newFrequency, this, [=](const qint64 &freq) {
                this->newFrequency(freq,1);
            });

            setProperty(scopeQuick->rootObject(),"vfoButton", "visible", true);
            setProperty(scopeQuick->rootObject(),"swapABButton", "visible", rigCaps->commands.contains(funcVFOSwapAB));
            setProperty(scopeQuick->rootObject(),"equalsABButton", "visible", rigCaps->commands.contains(funcVFOEqualAB));
            setProperty(scopeQuick->rootObject(),"vmButton", "visible", rigCaps->commands.contains(funcMemoryMode));
            setProperty(scopeQuick->rootObject(),"satButton", "visible", rigCaps->commands.contains(funcSatelliteMode));
            setProperty(scopeQuick->rootObject(),"splitButton", "visible", rigCaps->commands.contains(funcSplitStatus));
        }
    }

    this->layout->setContentsMargins(0,0,0,0);


    if (rigCaps->commands.contains(funcScopeRef))
    {
        auto v = rigCaps->commands.find(funcScopeRef);
        setSlider(scopeQuick->rootObject(),"ref",v.value().minVal, v.value().maxVal,5,v.value().minVal);
    } else {
        setProperty(scopeQuick->rootObject(),"ref","enabled",false);
    }

    if (rigCaps->commands.contains(funcPBTInner))
    {
        auto v = rigCaps->commands.find(funcPBTInner);
        setSlider(scopeQuick->rootObject(),"pbtInner",v.value().minVal, v.value().maxVal,10,v.value().minVal);
    } else {
        setProperty(scopeQuick->rootObject(),"pbtInner","enabled",false);
    }

    if (rigCaps->commands.contains(funcPBTOuter))
    {
        auto v = rigCaps->commands.find(funcPBTOuter);
        setSlider(scopeQuick->rootObject(),"pbtOuter",v.value().minVal, v.value().maxVal,10,v.value().minVal);
    } else {
        setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",false);
    }

    if (rigCaps->commands.contains(funcIFShift))
    {
        auto v = rigCaps->commands.find(funcIFShift);
        setSlider(scopeQuick->rootObject(),"ifShift",v.value().minVal, v.value().maxVal,10,v.value().minVal);
    } else {
        setProperty(scopeQuick->rootObject(),"ifShift","enabled",false);
    }


    if (rigCaps->commands.contains(funcFilterWidth))
    {
        auto v = rigCaps->commands.find(funcFilterWidth);
        setSlider(scopeQuick->rootObject(),"filterWidth",v.value().minVal, v.value().maxVal,10,v.value().minVal);
    } else {
        setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
    }

    frequencyNotificationLockoutTimer = new QTimer();
    connect(frequencyNotificationLockoutTimer, SIGNAL(timeout()), this, SLOT(freqNoteLockTimerSlot()));
    frequencyNotificationLockoutTimer->setInterval(200);
    frequencyNotificationLockoutTimer->setSingleShot(true);

/*
 *  This code is still needed to be converted

    pbtIndicator = new QCPItemRect(spectrum);
    pbtIndicator->setAntialiased(true);
    pbtIndicator->setPen(QPen(Qt::red));
    pbtIndicator->setBrush(QBrush(Qt::red));
    pbtIndicator->setSelectable(true);
    pbtIndicator->setVisible(false);

    pbtIndicator->topLeft->setCoords(0.5, 0);
    pbtIndicator->bottomRight->setCoords(0.5, 160);



    connect(configIfShift, &QSlider::valueChanged, this, [=](const int &val) {
        if (rigCaps != nullptr && rigCaps->commands.contains(funcIFShift)) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcIFShift,QVariant::fromValue<ushort>(val),false,t.receiver));
        } else {
            static int previousIFShift=128; // Default value
            unsigned char inner = configPbtInner->value();
            unsigned char outer = configPbtOuter->value();
            int shift = val - previousIFShift;
            inner = qMax( 0, qMin(255,int (inner + shift)) );
            outer = qMax( 0, qMin(255,int (outer + shift)) );

            configPbtInner->setValue(inner);
            configPbtOuter->setValue(outer);
            previousIFShift = val;
        }
    });

    connect(configResetIf, &QPushButton::clicked, this, [=](const bool &val) {
        Q_UNUSED(val)
        double pbFreq = (pbtDefault / passbandWidth) * 127.0;
        qint16 newFreq = pbFreq + 128;
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
        queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newFreq),false,t.receiver));
        queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newFreq),false,t.receiver));
        configIfShift->blockSignals(true);
        configIfShift->setValue(128);
        configIfShift->blockSignals(false);
    });


#if (QT_VERSION < QT_VERSION_CHECK(6,7,0))
    connect(configScopeEnabled, &QCheckBox::stateChanged, this, [=](const int &val) {
#else
    connect(configScopeEnabled, &QCheckBox::checkStateChanged, this, [=](const Qt::CheckState &val) {
#endif
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
        queue->addUnique(priorityHighest,queueItem(funcScopeOnOff,QVariant::fromValue<bool>(val),false,t.receiver));
        qInfo() << "Queueing command to set scope to:" << bool(val);
    });
*/

    lastData.start();

}


receiverWidget::~receiverWidget(){

    /*
    // This stops the property warnings in the log on disconnect, but causes wfview to crash on exit if connected to a radio.
    // Maybe we should simply tell receiver it is closing on disconnect?
    if (scopeQuick)
    {
        scopeQuick->setSource(QUrl());
        QCoreApplication::processEvents();
        auto *ctx = scopeQuick->engine()->rootContext();
        ctx->setContextProperty("receiver", nullptr);
        scopeQuick->engine()->clearComponentCache();
        scopeQuick->deleteLater();
    }
*/
}

void receiverWidget::setSeparators(QChar gsep, QChar dsep)
{
    if (freqDisplayA)
        freqDisplayA->setSeparators(gsep,dsep);
    if (freqDisplayB)
        freqDisplayB->setSeparators(gsep,dsep);
}

void receiverWidget::prepareScope(uint maxAmp, uint spectWidth)
{
    this->spectWidth = spectWidth;
    this->maxAmp = maxAmp;
}

void receiverWidget::changeWfLength(uint wf)
{
    waterfall->setLength(wf);

    if(!scopePrepared)
        return;

    QMutexLocker locker(&mutex);

    this->wfLength = wf;
}

bool receiverWidget::prepareWf(uint wf)
{
    QMutexLocker locker(&mutex);
    bool ret=true;

    return ret;


    this->wfLength = wf;
    this->wfLengthMax = 1024;
    waterfall->setLength(wf);

    scopePrepared = true;
    return ret;
}

void receiverWidget::setRange(int floor, int ceiling)
{
    plotFloor = floor;
    plotCeiling = ceiling;
    wfFloor = floor;
    wfCeiling = ceiling;
    maxAmp = ceiling;
    if (spectrum) {
        spectrum->setFloor(plotFloor);
        spectrum->setCeiling(plotCeiling);
    }

    if (waterfall){
        waterfall->setFloor(wfFloor);
        waterfall->setCeiling(wfCeiling);
    }
}

void receiverWidget::colorPreset(colorPrefsType *cp)
{
    if (cp == nullptr || !spectrum)
    {
        return;
    }

    spectrum->setColors(*cp);

    colors = *cp;
    /*

    pbtIndicator->setPen(QPen(cp->pbt));
    pbtIndicator->setBrush(QBrush(cp->pbt));

    holdButton->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border:1px solid;}")
                                  .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    splitButton->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border:1px solid;}")
                                   .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    satelliteButton->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border:1px solid;}")
                                   .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    vfoSelectButton->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border:1px solid;}")
                                   .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    vfoMemoryButton->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border:1px solid;}")
                                   .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    */

}

void receiverWidget::updateScope(const scopeData &data)
{

    if (!data.valid || data.data.isEmpty() || !spectrum || !waterfall)
        return;

    if (spectrum) {
        spectrum->updateScope(data);
    }
    if (waterfall) {
        waterfall->updateScope(data);
    }

    spectrum->setCenter(freq.MHzDouble);

    double pbStart = 0.0;
    double pbEnd = 0.0;

    switch (mode.mk)
    {
    case modeLSB:
    case modeRTTY_R:
    case modePSK_R:
        if (rigCaps->manufacturer == manufKenwood) {
            pbStart = freq.MHzDouble-passbandWidth;
            pbEnd = freq.MHzDouble;
        } else {
            pbStart = freq.MHzDouble - passbandCenterFrequency - (passbandWidth / 2);
            pbEnd = freq.MHzDouble - passbandCenterFrequency + (passbandWidth / 2);
        }
        break;
    case modeCW:
        if (passbandWidth < 0.0006) {
            pbStart = freq.MHzDouble - (passbandWidth / 2);
            pbEnd = freq.MHzDouble + (passbandWidth / 2);
        }
        else {
            pbStart = freq.MHzDouble + passbandCenterFrequency - passbandWidth;
            pbEnd = freq.MHzDouble + passbandCenterFrequency;
        }
        break;
    case modeCW_R:
        if (passbandWidth < 0.0006) {
            pbStart = freq.MHzDouble - (passbandWidth / 2);
            pbEnd = freq.MHzDouble + (passbandWidth / 2);
        }
        else {
            pbStart = freq.MHzDouble - passbandCenterFrequency;
            pbEnd = freq.MHzDouble + passbandWidth - passbandCenterFrequency;
        }
        break;
    default:
        if (rigCaps->manufacturer == manufKenwood) {
            pbStart = freq.MHzDouble;
            pbEnd = freq.MHzDouble+passbandWidth;
        } else {
            pbStart = freq.MHzDouble + passbandCenterFrequency - (passbandWidth / 2);
            pbEnd = freq.MHzDouble + passbandCenterFrequency + (passbandWidth / 2);
        }
        break;
    }

    spectrum->setPassbandLow(pbStart);
    spectrum->setPassbandHigh(pbEnd);

    if (data.startFreq != lowerFreq || data.endFreq != upperFreq)
    {
        //if(underlayMode == underlayPeakHold)
        //{
        //   // TODO: create non-button function to do this
        //    // This will break if the button is ever moved or renamed.
        //    clearPeaks();
        //}
        // Inform other threads (cluster) that the frequency range has changed.
        emit frequencyRange(receiver, data.startFreq, data.endFreq);
    }

    lowerFreq = data.startFreq;
    upperFreq = data.endFreq;

    if (!scopeReceived)
    {
        queue->del(funcScopeOnOff,0); // Delete recurring on/off command
        scopeReceived=true;
    }

    spectrum->setScopeOutOfRange(data.oor);

    /*
    if (lastData.elapsed() > (spectrum->replotTime(false)+waterfall->replotTime(false)))
    {
        spectrum->replot();
        waterfall->replot();
        lastData.restart();
        redrawSpeed->setText(" ");
    } else {
        redrawSpeed->setText("*");
    }

    emit spectrumTime(spectrum->replotTime(false));
    emit waterfallTime(waterfall->replotTime(false));
    */

    /*
    if (!scopePrepared )
    {
        return false;
    }

    if (!lastData.isValid())
    {
        lastData.start();
    }

    if (!bandIndicatorsVisible) {
        showBandIndicators(true);
    }

    //qint64 spectime = 0;

    bool updateRange = false;


    if (data.startFreq != lowerFreq || data.endFreq != upperFreq)
    {
        if(underlayMode == underlayPeakHold)
        {
            // TODO: create non-button function to do this
            // This will break if the button is ever moved or renamed.
            clearPeaks();
        }
        // Inform other threads (cluster) that the frequency range has changed.
        emit frequencyRange(receiver, data.startFreq, data.endFreq);
    }

    lowerFreq = data.startFreq;
    upperFreq = data.endFreq;

    //qInfo(logSystem()) << "start: " << data.startFreq << " end: " << data.endFreq;
    quint16 specLen = data.data.length();

    if (specLen != spectWidth)
    {
        qWarning(logSystem()) << "Spectrum length error, expected" << spectWidth << "got" << specLen << "(one can be ignored for USB connection)";
        return false;
    }

    // This can be used to run anything that only needs to be run once after valid scope data is received.
    if (!scopeReceived)
    {
        queue->del(funcScopeOnOff,0); // Delete recurring on/off command
        scopeReceived=true;
    }

    QVector <double> x(spectWidth), y(spectWidth), y2(spectWidth);

    for(int i=0; i < spectWidth; i++)
    {
        x[i] = (i * (data.endFreq-data.startFreq)/spectWidth) + data.startFreq;
    }

    for(int i=0; i< specLen; i++)
    {
        y[i] = (quint8)data.data[i];
        if(underlayMode == underlayPeakHold)
        {
            if((quint8)data.data[i] > (quint8)spectrumPeaks[i])
            {
                spectrumPeaks[i] = data.data[i];
            }
            y2[i] = (quint8)spectrumPeaks[i];
        }
    }
    plasmaMutex.lock();
    spectrumPlasma[spectrumPlasmaPosition] = data.data;
    spectrumPlasmaPosition = (spectrumPlasmaPosition+1) % spectrumPlasmaSizeCurrent;
    //spectrumPlasma.push_front(data.data);
//    if(spectrumPlasma.size() > (int)spectrumPlasmaSizeCurrent)
//    {
//        // If we have pushed_front more than spectrumPlasmaSize,
//        // then we cut one off the back.
//        spectrumPlasma.pop_back();
//    }
    plasmaMutex.unlock();
    mutex.lock();
    if ((plotFloor != oldPlotFloor) || (plotCeiling != oldPlotCeiling)){
        updateRange = true;
    }
#if QCUSTOMPLOT_VERSION < 0x020000
    spectrum->graph(0)->setData(x, y);
#else
    spectrum->graph(0)->setData(x, y, true);
#endif


    if((freq.MHzDouble < data.endFreq) && (freq.MHzDouble > data.startFreq))
    {
        freqIndicatorLine->start->setCoords(freq.MHzDouble, 0);
        freqIndicatorLine->end->setCoords(freq.MHzDouble, maxAmp);

        double pbStart = 0.0;
        double pbEnd = 0.0;

        switch (mode.mk)
        {
        case modeLSB:
        case modeRTTY_R:
        case modePSK_R:
            if (rigCaps->manufacturer == manufKenwood) {
                pbStart = freq.MHzDouble-passbandWidth;
                pbEnd = freq.MHzDouble;
            } else {
                pbStart = freq.MHzDouble - passbandCenterFrequency - (passbandWidth / 2);
                pbEnd = freq.MHzDouble - passbandCenterFrequency + (passbandWidth / 2);
            }
            break;
        case modeCW:
            if (passbandWidth < 0.0006) {
                pbStart = freq.MHzDouble - (passbandWidth / 2);
                pbEnd = freq.MHzDouble + (passbandWidth / 2);
            }
            else {
                pbStart = freq.MHzDouble + passbandCenterFrequency - passbandWidth;
                pbEnd = freq.MHzDouble + passbandCenterFrequency;
            }
            break;
        case modeCW_R:
            if (passbandWidth < 0.0006) {
                pbStart = freq.MHzDouble - (passbandWidth / 2);
                pbEnd = freq.MHzDouble + (passbandWidth / 2);
            }
            else {
                pbStart = freq.MHzDouble - passbandCenterFrequency;
                pbEnd = freq.MHzDouble + passbandWidth - passbandCenterFrequency;
            }
            break;            
        default:
            if (rigCaps->manufacturer == manufKenwood) {
                pbStart = freq.MHzDouble;
                pbEnd = freq.MHzDouble+passbandWidth;
            } else {
                pbStart = freq.MHzDouble + passbandCenterFrequency - (passbandWidth / 2);
                pbEnd = freq.MHzDouble + passbandCenterFrequency + (passbandWidth / 2);
            }
            break;
        }

        passbandIndicator->topLeft->setCoords(pbStart, 0);
        passbandIndicator->bottomRight->setCoords(pbEnd, maxAmp);

        if ((mode.mk == modeCW || mode.mk == modeCW_R) && passbandWidth > 0.0006)
        {
            pbtDefault = round((passbandWidth - (cwPitch / 1000000.0)) * 200000.0) / 200000.0;
        }
        else
        {
            pbtDefault = 0.0;
        }

        if ((PBTInner - pbtDefault || PBTOuter - pbtDefault) && passbandAction != passbandResizing && mode.mk != modeFM && mode.mk != modeWFM)
        {
            pbtIndicator->setVisible(true);
        }
        else
        {
            pbtIndicator->setVisible(false);
        }

        if (mode.mk == modeLSB || mode.mk == modeCW || mode.mk == modeRTTY) {
            pbtIndicator->topLeft->setCoords(qMax(pbStart - (PBTInner / 2) + (pbtDefault / 2), pbStart - (PBTOuter / 2) + (pbtDefault / 2)), 0);

            pbtIndicator->bottomRight->setCoords(qMin(pbStart - (PBTInner / 2) + (pbtDefault / 2) + passbandWidth,
                                                      pbStart - (PBTOuter / 2) + (pbtDefault / 2) + passbandWidth), maxAmp);
        }
        else
        {
            pbtIndicator->topLeft->setCoords(qMax(pbStart + (PBTInner / 2) - (pbtDefault / 2), pbStart + (PBTOuter / 2) - (pbtDefault / 2)), 0);

            pbtIndicator->bottomRight->setCoords(qMin(pbStart + (PBTInner / 2) - (pbtDefault / 2) + passbandWidth,
                                                      pbStart + (PBTOuter / 2) - (pbtDefault / 2) + passbandWidth), maxAmp);
        }

        //qDebug() << "Default" << pbtDefault << "Inner" << PBTInner << "Outer" << PBTOuter << "Pass" << passbandWidth << "Center" << passbandCenterFrequency << "CW" << cwPitch;
    }

#if QCUSTOMPLOT_VERSION < 0x020000
    if (underlayMode == underlayPeakHold) {
        spectrum->graph(1)->setData(x, y2); // peaks
    }
    else if (underlayMode != underlayNone) {
        computePlasma();
        spectrum->graph(1)->setData(x, spectrumPlasmaLine);
    }
    else {
        spectrum->graph(1)->setData(x, y2); // peaks, but probably cleared out
    }

#else
    if (underlayMode == underlayPeakHold) {
        spectrum->graph(1)->setData(x, y2, true); // peaks
    }
    else if (underlayMode != underlayNone) {
        computePlasma();
        spectrum->graph(1)->setData(x, spectrumPlasmaLine, true);
    }
    else {
        spectrum->graph(1)->setData(x, y2, true); // peaks, but probably cleared out
    }
#endif

    if(updateRange)
        spectrum->yAxis->setRange(plotFloor, plotCeiling);

    spectrum->xAxis->setRange(data.startFreq, data.endFreq);

    if(specLen == spectWidth)
    {
        wfimage.prepend(data.data);
        wfimage.pop_back();
        QByteArray wfRow;
        // Waterfall:
        for(int row = 0; row < wfLength; row++)
        {
            wfRow = wfimage[row];
            for(int col = 0; col < spectWidth; col++)
            {
                colorMap->data()->setCell( col, row, (quint8)wfRow[col]);
            }
        }
        if(updateRange)
        {
            colorMap->setDataRange(QCPRange(wfFloor, wfCeiling));
        }

        waterfall->yAxis->setRange(0,wfLength - 1);
        waterfall->xAxis->setRange(0, spectWidth-1);

    }

    oldPlotFloor = plotFloor;
    oldPlotCeiling = plotCeiling;

    if (data.oor && !oorIndicator->visible()) {
        oorIndicator->setVisible(true);
        qInfo(logSystem()) << "Scope out of range";
    } else if (!data.oor && oorIndicator->visible()) {
        oorIndicator->setVisible(false);
    }


#if QCUSTOMPLOT_VERSION >= 0x020100

    if (lastData.elapsed() > (spectrum->replotTime(false)+waterfall->replotTime(false)))
    {
        spectrum->replot();
        waterfall->replot();
        lastData.restart();
        redrawSpeed->setText(" ");
    } else {
        redrawSpeed->setText("*");
    }

    emit spectrumTime(spectrum->replotTime(false));
    emit waterfallTime(waterfall->replotTime(false));
#else
    spectrum->replot();
    waterfall->replot();
    lastData.restart();
#endif

    mutex.unlock();
    emit sendScopeImage(receiver);

    */
    return;
}




// Plasma functions

void receiverWidget::resizePlasmaBuffer(int size) {
    // QMutexLocker locker(&plasmaMutex);
    //qDebug() << "Resizing plasma buffer via parameter, from oldsize " << spectrumPlasmaSizeCurrent << " to new size: " << size;
    //spectrumPlasmaSizeCurrent = size;
    if (spectrum) spectrum->setPeakDecay(size);

    return;
}

void receiverWidget::clearPeaks()
{
    if (spectrum)
        spectrum->clearPeaks();
}

void receiverWidget::clearPlasma()
{
    // Clear the buffer of spectrum used for peak and average computation.
    // This is only needed one time, when the VFO is created with spectrum size info.
    //QMutexLocker locker(&plasmaMutex);
    //QByteArray empty((int)spectWidth, '\x01');
    //int pSize = spectrumPlasma.size();
    //for(int i=0; i < pSize; i++)
    //{
    //    spectrumPlasma[i] = empty;
    //}
}

void receiverWidget::computePlasma()
{
    //QMutexLocker locker(&plasmaMutex);
    // Spec PlasmaLine is a single line of spectrum, ~~600 pixels or however many the radio provides.
    // This changes width only when we connect to a new radio.
    //if(spectrumPlasmaLine.size() != spectWidth) {
    //    spectrumPlasmaLine.clear();
    //    spectrumPlasmaLine.resize(spectWidth);
    //}

    // spectrumPlasma is the bufffer of spectrum lines to use when computing the average or peak.
    /*
    int specPlasmaSize = spectrumPlasmaSizeCurrent; // go only this far in
    if(underlayMode == underlayAverageBuffer)
    {
        for(int col=0; col < spectWidth; col++)
        {
            for(int pos=0; pos < specPlasmaSize; pos++)
            {
                spectrumPlasmaLine[col] += (quint8)spectrumPlasma[pos][col];
            }
            spectrumPlasmaLine[col] = spectrumPlasmaLine[col] / specPlasmaSize;
        }
    } else if (underlayMode == underlayPeakBuffer){
        // peak mode, running peak display
        for(int col=0; col < spectWidth; col++)
        {
            spectrumPlasmaLine[col] = spectrumPlasma[0][col]; // initial value
            for(int pos=0; pos < specPlasmaSize; pos++)
            {
                if((double)((quint8)spectrumPlasma[pos][col]) > spectrumPlasmaLine[col])
                    spectrumPlasmaLine[col] = (quint8)spectrumPlasma[pos][col];
            }
        }
    }
    */
}

void receiverWidget::showHideControls(uchar mode)
{
    if(currentScopeMode == mode)
    {
        return;
    }

    /*
    if (!rigCaps->hasSpectrum) {
        scopeQuick->hide();
        scopeModeCombo->hide();
        edgeCombo->hide();
        edgeButton->hide();
        holdButton->hide();
        toFixedButton->hide();
        spanCombo->hide();
        clearPeaksButton->hide();
    }
    else
    {
        scopeQuick->show();
        scopeModeCombo->show();
        clearPeaksButton->show();

        if (rigCaps->manufacturer == manufYaesu) {
            switch (mode)
            {
            case 0:
            case 3:
            case 4:
                edgeCombo->hide();
                edgeButton->hide();
                toFixedButton->show();
                spanCombo->show();
                break;
            case 1:
            case 2:
            case 6:
            case 7:
            case 9:
            case 10:
                toFixedButton->hide();
                spanCombo->hide();
                edgeCombo->show();
                edgeButton->show();
                break;
            default:
                break;
            }
        } else {
            switch (mode)
            {
            case 0: // Center
            case 2: // Scroll-C
                edgeCombo->hide();
                edgeButton->hide();
                toFixedButton->show();
                spanCombo->show();
                break;

            case 1: // Fixed
            case 3: // Scroll-F
                toFixedButton->hide();
                spanCombo->hide();
                edgeCombo->show();
                edgeButton->show();
                break;
            default:
                break;
            }
        }
    }
    detachButton->show();

    if (rigCaps->hasSpectrum || rigCaps->commands.contains(funcIFShift) || rigCaps->commands.contains(funcPBTInner))
    {
        confButton->show();
    }
    else {
        confButton->hide();
    }

    configSpeed->setEnabled(rigCaps->hasSpectrum);
    configBottom->setEnabled(rigCaps->hasSpectrum);
    configLength->setEnabled(rigCaps->hasSpectrum);
    configRef->setEnabled(rigCaps->hasSpectrum);
    configScopeEnabled->setEnabled(rigCaps->hasSpectrum);
    configTheme->setEnabled(rigCaps->hasSpectrum);
    configPbtInner->setEnabled(rigCaps->commands.contains(funcPBTInner));
    configPbtOuter->setEnabled(rigCaps->commands.contains(funcPBTOuter));
    configIfShift->setEnabled(rigCaps->commands.contains(funcIFShift) || rigCaps->commands.contains(funcPBTInner));

    filterCombo->setVisible(rigCaps->filters.size());
    dataCombo->setVisible(rigCaps->inputs.size());
    */
}


void receiverWidget::displayScope(bool en)
{
    //this->splitter->setVisible(en || rigCaps->hasCommand29);
    // Hide these controls if disabled
    /*
    if (!en) {
        this->edgeCombo->setVisible(en);
        this->edgeButton->setVisible(en);
        this->toFixedButton->setVisible(en);
        this->spanCombo->setVisible(en);

        QTimer::singleShot(0, [this]{
            this->resize(this->minimumSizeHint());
        });
    }
    this->clearPeaksButton->setVisible(en && rigCaps->hasSpectrum);
    this->holdButton->setVisible(en && rigCaps->commands.contains(funcScopeHold));
*/
}


void receiverWidget::updatedMode(int index)
{
    Q_UNUSED(index) // We don't know where it came from!
    modeInfo mi = currentMode;
    if (mi.mk == modeCW || mi.mk == modeCW_R || mi.mk == modeRTTY || mi.mk == modeRTTY_R || mi.mk == modePSK || mi.mk == modePSK_R)
    {
        mi.data = 0;
        //dataCombo->setEnabled(false);
    } else {
        //mi.data = dataCombo->currentIndex();
        //dataCombo->setEnabled(true);
    }
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(mi),false,t.receiver));
    if (t.modeFunc == funcModeSet) {
        queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(mi),false,t.receiver));
    }
    // Request current filtershape/roofing
    if (rigCaps->manufacturer == manufIcom)
    {
        queue->addUnique(priorityHighest,funcFilterShape,false,t.receiver);
        queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
    }
}

void receiverWidget::setEdge(uchar index)
{
    //edgeCombo->blockSignals(true);
    //edgeCombo->setCurrentIndex(edgeCombo->findData(index));
    //edgeCombo->blockSignals(false);
}

/*
void receiverWidget::toFixedPressed()
{
    int currentEdge = edgeCombo->currentIndex();
    bool dialogOk = false;
    bool numOk = false;

    QStringList edges;
    edges << "1" << "2" << "3" << "4";

    QString item = QInputDialog::getItem(this, "Select Edge", "Edge to replace:", edges, currentEdge, false, &dialogOk);

    if(dialogOk)
    {
        int edge = QString(item).toInt(&numOk,10);
        if(numOk)
        {
            edgeCombo->blockSignals(true);
            edgeCombo->setCurrentIndex(edgeCombo->findData(edge));
            edgeCombo->blockSignals(false);
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpeed,QVariant::fromValue(spectrumBounds(lowerFreq, upperFreq, edge)),false,receiver));
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpeed,QVariant::fromValue<uchar>(1),false,receiver));
        }
    }
}
*/

void receiverWidget::customSpanPressed()
{

    float maxSpan = 0.0;
    float minSpan = 1.0;
    for (const auto &span: rigCaps->scopeCenterSpans)
    {
        if (double(span.freq / 1000000.0) > maxSpan)
            maxSpan = double(span.freq / 1000000.0);
        if (double(span.freq / 1000000.0) < minSpan)
            minSpan = double(span.freq / 1000000.0);
    }
    maxSpan = maxSpan * 2;
    minSpan = minSpan * 2;

    QDialog* dialog = new QDialog(this);
    dialog->setToolTip(tr("Please enter the lower and upper frequencies (in MHz) for the currently selected Scope Fixed edge"));
    dialog->setWindowTitle(tr("Scope Edges"));
    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    QHBoxLayout* header = new QHBoxLayout();

    layout->addLayout(header);
    QLabel* lowLabel = new QLabel();
    lowLabel->setText(tr("Start Freq (MHz)"));
    lowLabel->setAlignment(Qt::AlignCenter);
    header->addWidget(lowLabel);
    QLabel* highLabel = new QLabel();
    highLabel->setText(tr("End Freq (MHz)"));
    highLabel->setAlignment(Qt::AlignCenter);
    header->addWidget(highLabel);

    QHBoxLayout* spins = new QHBoxLayout();
    layout->addLayout(spins);
    QDoubleSpinBox* low = new QDoubleSpinBox();
    low->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
    low->setValue(lowerFreq);
    low->setDecimals(3);
    low->setSingleStep(minSpan);
    low->setRange(minFreqMhz,maxFreqMhz);
    low->setAlignment(Qt::AlignCenter);
    low->setToolTip(tr("Fixed edge start frequency"));
    spins->addWidget(low);

    low->setMinimumHeight(low->minimumSizeHint().height()*2);
    QDoubleSpinBox* high = new QDoubleSpinBox();
    high->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Expanding);
    high->setValue(upperFreq);
    high->setDecimals(3);
    high->setSingleStep(minSpan);
    high->setMinimumHeight(high->minimumSizeHint().height()*2);
    high->setRange(minFreqMhz,maxFreqMhz);
    high->setAlignment(Qt::AlignCenter);
    high->setToolTip(tr("Fixed edge end frequency"));
    spins->addWidget(high);

    QHBoxLayout* buttons = new QHBoxLayout();
    layout->addLayout(buttons);
    QPushButton *ok = new QPushButton("OK");
    QPushButton *cancel = new QPushButton("Cancel");
    buttons->addWidget(ok);
    buttons->addWidget(cancel);

    connect(ok, &QPushButton::clicked, this, [=]() {
        // Here we need to attempt to update the fixed edge
        if (high->value() - low->value() > maxSpan)
        {
            high->setValue(low->value()+maxSpan);
        }
        else if (high->value() < low->value() || high->value() - low->value() < minSpan)
        {
            high->setValue(low->value()+minSpan);
        }
        else
        {
            //qDebug(logGui()) << "setting edge to: " << low->value() << ", " << high->value() << ", edge num: " << edgeCombo->currentData().toUInt();
            //queue->addUnique(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,QVariant::fromValue(spectrumBounds(low->value(), high->value(), edgeCombo->currentData().toUInt())),false,receiver));
            dialog->close();
        }
    });

    connect(cancel, &QPushButton::clicked, this, [=]() {
        dialog->close();
    });

    dialog->exec();

}


void receiverWidget::doubleClick(QMouseEvent *me)
{
    /*
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    if (me->button() == Qt::LeftButton)
    {
        double x;
        freqt freqGo;
        if (!freqLock)
        {
            //y = plot->yAxis->pixelToCoord(me->pos().y());
            x = spectrum->xAxis->pixelToCoord(me->pos().x());
            freqGo.Hz = x * 1E6;
            freqGo.Hz = roundFrequency(freqGo.Hz, stepSize);
            freqGo.MHzDouble = (float)freqGo.Hz / 1E6;

            emit sendTrack(freqGo.Hz-this->freq.Hz);

            setFrequency(freqGo);
            queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(freqGo),false,t.receiver));
        }
    }
    else if (me->button() == Qt::RightButton)
    {
        QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
        double pbFreq = (pbtDefault / passbandWidth) * 127.0;
        qint16 newFreq = pbFreq + 128;
        queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newFreq),false,t.receiver));
        queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newFreq),false,t.receiver));
        QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
        if (rectItem != nullptr)
        {

        }
    }
    */
}

void receiverWidget::scopeClick(QMouseEvent* me)
{
    /*
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);

    QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);
    QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
#if QCUSTOMPLOT_VERSION < 0x020000
    int leftPix = (int)passbandIndicator->left->pixelPoint().x();
    int rightPix = (int)passbandIndicator->right->pixelPoint().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPoint().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPoint().x();
#else
    int leftPix = (int)passbandIndicator->left->pixelPosition().x();
    int rightPix = (int)passbandIndicator->right->pixelPosition().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPosition().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPosition().x();
#endif
    int pbtCenterPix = pbtLeftPix + ((pbtRightPix - pbtLeftPix) / 2);
    int cursor = me->pos().x();

    if (me->button() == Qt::LeftButton) {
        this->mousePressFreq = spectrum->xAxis->pixelToCoord(cursor);
        if (textItem != nullptr)
        {
            QMap<QString, spotData*>::iterator spot = clusterSpots.find(textItem->text());
            if (spot != clusterSpots.end() && spot.key() == textItem->text() && !freqLock)
            {
                qInfo(logGui()) << "Clicked on spot:" << textItem->text();
                freqt freqGo;
                freqGo.Hz = (spot.value()->frequency) * 1E6;
                freqGo.MHzDouble = spot.value()->frequency;

                emit sendTrack(freqGo.Hz-this->freq.Hz);

                setFrequency(freqGo);
                queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(freqGo),false,t.receiver));

            }
        }
        else if (passbandAction == passbandStatic && rectItem != nullptr)
        {
            if ((cursor <= leftPix && cursor > leftPix - 10) || (cursor >= rightPix && cursor < rightPix + 10))
            {
                passbandAction = passbandResizing;
            }
        }
        // TODO clickdragtuning and sending messages to statusbar

        else if (clickDragTuning)
        {
            emit showStatusBarText(QString("Selected %1 MHz").arg(this->mousePressFreq));
        }
        else {
            emit showStatusBarText(QString("Selected %1 MHz").arg(this->mousePressFreq));
        }    

    }
    else if (me->button() == Qt::RightButton)
    {
        if (textItem != nullptr) {
            QMap<QString, spotData*>::iterator spot = clusterSpots.find(textItem->text());
            if (spot != clusterSpots.end() && spot.key() == textItem->text()) {
                // parent and children are destroyed on close
                QDialog* spotDialog = new QDialog();
                QVBoxLayout* vlayout = new QVBoxLayout;
                //spotDialog->setFixedSize(240, 100);
                spotDialog->setBaseSize(1, 1);
                spotDialog->setWindowTitle(spot.value()->dxcall);
                QLabel* dxcall = new QLabel(QString("DX:%1").arg(spot.value()->dxcall));
                QLabel* spotter = new QLabel(QString("Spotter:%1").arg(spot.value()->spottercall));
                QLabel* frequency = new QLabel(QString("Frequency:%1 MHz").arg(spot.value()->frequency));
                QLabel* comment = new QLabel(QString("Comment:%1").arg(spot.value()->comment));
                QAbstractButton* bExit = new QPushButton("Close");
                vlayout->addWidget(dxcall);
                vlayout->addWidget(spotter);
                vlayout->addWidget(frequency);
                vlayout->addWidget(comment);
                vlayout->addWidget(bExit);
                spotDialog->setLayout(vlayout);
                spotDialog->show();
                spotDialog->connect(bExit, SIGNAL(clicked()), spotDialog, SLOT(close()));
            }
        }
        else if (passbandAction == passbandStatic && rectItem != nullptr)
        {
            if (cursor <= pbtLeftPix && cursor > pbtLeftPix - 10)
            {
                passbandAction = pbtInnerMove;
            }
            else if (cursor >= pbtRightPix && cursor < pbtRightPix + 10)
            {
                passbandAction = pbtOuterMove;
            }
            else if (cursor > pbtCenterPix - 20 && cursor < pbtCenterPix + 20)
            {
                passbandAction = pbtMoving;
            }
            this->mousePressFreq = spectrum->xAxis->pixelToCoord(cursor);
        }
    }
    */
}

void receiverWidget::scopeMouseRelease(QMouseEvent* me)
{
    /*
    QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);

    if (textItem == nullptr && clickDragTuning) {
        this->mouseReleaseFreq = spectrum->xAxis->pixelToCoord(me->pos().x());
        double delta = mouseReleaseFreq - mousePressFreq;
        qInfo(logGui()) << "Mouse release delta: " << delta;
    }

    if (passbandAction != passbandStatic) {
        passbandAction = passbandStatic;
    }
    */
}

void receiverWidget::scopeMouseMove(QMouseEvent* me)
{
    /*
    QCPAbstractItem* item = spectrum->itemAt(me->pos(), true);
    QCPItemText* textItem = dynamic_cast<QCPItemText*> (item);
    QCPItemRect* rectItem = dynamic_cast<QCPItemRect*> (item);
#if QCUSTOMPLOT_VERSION < 0x020000
    int leftPix = (int)passbandIndicator->left->pixelPoint().x();
    int rightPix = (int)passbandIndicator->right->pixelPoint().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPoint().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPoint().x();
#else
    int leftPix = (int)passbandIndicator->left->pixelPosition().x();
    int rightPix = (int)passbandIndicator->right->pixelPosition().x();
    int pbtLeftPix = (int)pbtIndicator->left->pixelPosition().x();
    int pbtRightPix = (int)pbtIndicator->right->pixelPosition().x();
#endif
    int pbtCenterPix = pbtLeftPix + ((pbtRightPix - pbtLeftPix) / 2);
    int cursor = me->pos().x();
    double movedFrequency = spectrum->xAxis->pixelToCoord(cursor) - mousePressFreq;

    if (passbandAction == passbandStatic && rectItem != nullptr)
    {
        if ((cursor <= leftPix && cursor > leftPix - 10) ||
            (cursor >= rightPix && cursor < rightPix + 10) ||
            (cursor <= pbtLeftPix && cursor > pbtLeftPix - 10) ||
            (cursor >= pbtRightPix && cursor < pbtRightPix + 10))
        {
            setCursor(Qt::SizeHorCursor);
        }
        else if (cursor > pbtCenterPix - 20 && cursor < pbtCenterPix + 20) {
            setCursor(Qt::OpenHandCursor);
        }
    }
    else if (passbandAction == passbandResizing)
    {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {

            // We are currently resizing the passband.
            double pb = 0.0;
            double origin = passbandCenterFrequency;
            if (mode.mk == modeLSB)
            {
                origin = - passbandCenterFrequency;
            }

            if (spectrum->xAxis->pixelToCoord(cursor) >= freq.MHzDouble + origin) {
                pb = spectrum->xAxis->pixelToCoord(cursor) - passbandIndicator->topLeft->coords().x();
            }
            else {
                pb = passbandIndicator->bottomRight->coords().x() - spectrum->xAxis->pixelToCoord(cursor);
            }

            if (mode.bwMax != 0 && mode.bwMin != 0) {
                vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
                queue->addUnique(priorityImmediate,queueItem(funcFilterWidth,QVariant::fromValue<ushort>(pb * 1000000),false,t.receiver));
            }
            //qInfo() << "New passband" << uint(pb * 1000000);

            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtMoving) {

        //qint16 shift = (qint16)(level - 128);
        //TPBFInner = (double)(shift / 127.0) * (passbandWidth);
        // Only move if more than 100Hz
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {

            double innerFreq = ((double)(PBTInner + movedFrequency) / passbandWidth) * 127.0;
            double outerFreq = ((double)(PBTOuter + movedFrequency) / passbandWidth) * 127.0;

            qint16 newInFreq = innerFreq + 128;
            qint16 newOutFreq = outerFreq + 128;

            if (newInFreq >= 0 && newInFreq <= 255 && newOutFreq >= 0 && newOutFreq <= 255 && mode.bwMax != 0)
            {
                qDebug() << QString("Moving passband by %1 Hz (Inner %2) (Outer %3) Mode:%4").arg((qint16)(movedFrequency * 1000000))
                                .arg(newInFreq).arg(newOutFreq).arg(mode.mk);
                vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
                queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newInFreq),false,t.receiver));
                queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newInFreq),false,t.receiver));
            }
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtInnerMove) {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {
            double pbFreq = ((double)(PBTInner + movedFrequency) / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255 && mode.bwMax != 0) {
                vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
                queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(newFreq),false,t.receiver));
            }
            lastFreq = movedFrequency;
        }
    }
    else if (passbandAction == pbtOuterMove) {
        static double lastFreq = movedFrequency;
        if (lastFreq - movedFrequency > 0.000049 || movedFrequency - lastFreq > 0.000049) {
            double pbFreq = ((double)(PBTOuter + movedFrequency) / passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255 && mode.bwMax != 0) {
                vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
                queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(newFreq),false,t.receiver));
            }
            lastFreq = movedFrequency;
        }
    }
    else  if (passbandAction == passbandStatic && me->buttons() == Qt::LeftButton && textItem == nullptr && clickDragTuning )
    {
        double delta = spectrum->xAxis->pixelToCoord(cursor) - mousePressFreq;
        qDebug(logGui()) << "Mouse moving delta: " << delta;
        if( (( delta < -0.0001 ) || (delta > 0.0001)) && ((delta < 0.501) && (delta > -0.501)) && !freqLock)
        {
            freqt freqGo;
            freqGo.Hz = (freq.MHzDouble + delta) * 1E6;
            freqGo.Hz = roundFrequency(freqGo.Hz, stepSize);
            freqGo.MHzDouble = (float)freqGo.Hz / 1E6;
            emit sendTrack(freqGo.Hz-this->freq.Hz);

            setFrequency(freqGo);
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(freqGo),false,t.receiver));
        }
    }
    else {
        setCursor(Qt::ArrowCursor);
    }
    */
}

void receiverWidget::waterfallClick(QMouseEvent *me)
{
       // double x = spectrum->xAxis->pixelToCoord(me->pos().x());
       // emit showStatusBarText(QString("Selected %1 MHz").arg(x));
}

void receiverWidget::onWheelTuneRequested(int steps, Qt::KeyboardModifiers mods)
{
    qreal stepsToScroll = QApplication::wheelScrollLines() * steps;

    // Did we change direction?
    if( (scrollWheelOffsetAccumulated > 0) && (steps > 0) ) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else if ((scrollWheelOffsetAccumulated < 0) && (steps < 0)) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else {
        // Changed direction, zap the old accumulation:
        scrollWheelOffsetAccumulated = stepsToScroll;
        //qInfo() << "Scroll changed direction";
    }

    int clicks = int(scrollWheelOffsetAccumulated);

    if (!clicks) {
        return;
    }

    // If we made it this far, it's time to scroll and to ultimately
    // clear the scroll accumulator.

    unsigned int stepsHz = stepSize;

    if ((mods == Qt::ShiftModifier) && (stepsHz != 1))
    {
        stepsHz /= 10;
    }
    else if (mods == Qt::ControlModifier)
    {
        stepsHz *= 10;
    }

    if (!freqLock) {
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
        freqt f;
        f.Hz = roundFrequency(freq.Hz, clicks, stepsHz);
        f.MHzDouble = f.Hz / (double)1E6;

        emit sendTrack(f.Hz-this->freq.Hz); // Where does this go? Nowhere currently!

        setFrequencyLocally(f);
        queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(f),false,receiver));
        tempLockAcceptFreqData();
        //qInfo() << "Moving to freq:" << f.Hz << "step" << stepsHz;
    }
    scrollWheelOffsetAccumulated = 0;

}
void receiverWidget::scroll(QWheelEvent *we)
{
    /*
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);

    // angleDelta is supposed to be eights of a degree of mouse wheel rotation
    int deltaY = we->angleDelta().y();
    int deltaX = we->angleDelta().x();
    int delta = (deltaX==0)?deltaY:deltaX;


    qreal       numDegrees = delta / 8;
    qreal       offset = numDegrees / 15;


    qreal stepsToScroll = QApplication::wheelScrollLines() * offset;

    // Did we change direction?
    if( (scrollWheelOffsetAccumulated > 0) && (offset > 0) ) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else if ((scrollWheelOffsetAccumulated < 0) && (offset < 0)) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else {
        // Changed direction, zap the old accumulation:
        scrollWheelOffsetAccumulated = stepsToScroll;
        //qInfo() << "Scroll changed direction";
    }

    int clicks = int(scrollWheelOffsetAccumulated);

    if (!clicks) {
//        qInfo() << "Rejecting minor scroll motion, too small. Wheel Clicks: " << clicks << ", angleDelta: " << delta;
//        qInfo() << "Accumulator value: " << scrollWheelOffsetAccumulated;
//        qInfo() << "stepsToScroll: " << stepsToScroll;
//        qInfo() << "Storing scroll motion for later use.";
        return;
    } else {
//        qInfo() << "Accepted scroll motion. Wheel Clicks: " << clicks << ", angleDelta: " << delta;
//        qInfo() << "Accumulator value: " << scrollWheelOffsetAccumulated;
//        qInfo() << "stepsToScroll: " << stepsToScroll;
    }

    // If we made it this far, it's time to scroll and to ultimately
    // clear the scroll accumulator.

    unsigned int stepsHz = stepSize;

    Qt::KeyboardModifiers key = we->modifiers();
    if ((key == Qt::ShiftModifier) && (stepsHz != 1))
    {
        stepsHz /= 10;
    }
    else if (key == Qt::ControlModifier)
    {
        stepsHz *= 10;
    }

    if (!freqLock) {
    freqt f;
    f.Hz = roundFrequency(freq.Hz, clicks, stepsHz);
    f.MHzDouble = f.Hz / (double)1E6;

        emit sendTrack(f.Hz-this->freq.Hz); // Where does this go?

        setFrequencyLocally(f);
        queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(f),false,receiver));
        tempLockAcceptFreqData();
        //qInfo() << "Moving to freq:" << f.Hz << "step" << stepsHz;
    }
    scrollWheelOffsetAccumulated = 0;
*/
}

void receiverWidget::receiveMode(modeInfo m, uchar vfo)
{
    // Update mode information if mode/filter/data has changed.
    // Not all rigs send data so this "might" need to be updated independantly?

    if (vfo > 0) {
        if (m.mk != modeUnknown)
            unselectedMode=m;
        return;
    }

    if (m.filter != 0xff && this->mode.filter != m.filter)
    {
        setFilter(m.filter,false);
    }

    if (m.data != 0xff && this->mode.data != m.data)
    {
        emit dataChanged(m); // Signal wfmain that the data mode has been changed.
        setDataMode(m.data,false);
    }

    if (m.mk != modeUnknown && mode.mk != m.mk) {
        qInfo(logSystem()) << __func__ << QString("Received new mode for %0 (%1): %2 (%3) filter:%4 data:%5")
        .arg((receiver?"Sub":"Main")).arg(QString::number(m.mk)).arg(m.reg).arg(m.name).arg(m.filter).arg(m.data) ;

        setMode(m,false);

        if (m.mk == modeCW || m.mk == modeCW_R || m.mk == modeRTTY || m.mk == modeRTTY_R || m.mk == modePSK || m.mk == modePSK_R)
        {
            setProperty(scopeQuick->rootObject(),"dataMode","enabled",false);
        } else {
            setProperty(scopeQuick->rootObject(),"dataMode","enabled",true);
        }

        if (m.mk != mode.mk) {
            // We have changed mode so "may" need to change regular commands

            passbandCenterFrequency = 0.0;

            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);

            // Make sure the filterWidth range is within limits.

            // If new mode doesn't allow bandwidth control, disable filterwidth and pbt.
            if (scopeQuick) setSlider(scopeQuick->rootObject(),"filterWidth",m.bwMin, m.bwMax,10,m.bwMax);

            if (m.bwMin > 0 || m.bwMax > 0) {
                // Set config specific options)
                if (rigCaps->manufacturer == manufKenwood)
                {
                    if (m.mk == modeCW || m.mk == modeCW_R || m.mk == modePSK || m.mk == modePSK_R) {
                        queue->addUnique(priorityHigh,funcFilterWidth,true,t.receiver);
                        queue->del(funcPBTInner,t.receiver);
                        queue->del(funcPBTOuter,t.receiver);
                        setProperty(scopeQuick->rootObject(),"pbtInner","enabled",false);
                        setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",false);
                        setProperty(scopeQuick->rootObject(),"ifShift","enabled",false);
                        setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
                    }
                    else if (m.mk == modeAM || m.mk == modeFM) {
                        queue->addUnique(priorityHigh,funcPBTInner,true,t.receiver);
                        queue->addUnique(priorityHigh,funcPBTOuter,true,t.receiver);
                        queue->del(funcFilterWidth,t.receiver);
                        setProperty(scopeQuick->rootObject(),"pbtInner","enabled",true && rigCaps->commands.contains(funcPBTInner));
                        setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",true && rigCaps->commands.contains(funcPBTOuter));
                        setProperty(scopeQuick->rootObject(),"ifShift","enabled",true && (rigCaps->commands.contains(funcIFShift) || rigCaps->commands.contains(funcPBTInner)));
                        setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
                    } else {
                        queue->addUnique(priorityHigh,funcPBTInner,true,t.receiver);
                        queue->addUnique(priorityHigh,funcPBTOuter,true,t.receiver);
                        queue->addUnique(priorityHigh,funcFilterWidth,true,t.receiver);
                        setProperty(scopeQuick->rootObject(),"pbtInner","enabled",true);
                        setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",true);
                        setProperty(scopeQuick->rootObject(),"filterWidth","enabled",true);
                    }
                } else
                {
                    queue->addUnique(priorityHigh,funcPBTInner,true,t.receiver);
                    queue->addUnique(priorityHigh,funcPBTOuter,true,t.receiver);
                    queue->addUnique(priorityHigh,funcFilterWidth,true,t.receiver);
                    setProperty(scopeQuick->rootObject(),"pbtInner","enabled",true);
                    setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",true);
                    setProperty(scopeQuick->rootObject(),"filterWidth","enabled",true);
                }
            } else{
                queue->del(funcPBTInner,t.receiver);
                queue->del(funcPBTOuter,t.receiver);
                queue->del(funcFilterWidth,t.receiver);
                setProperty(scopeQuick->rootObject(),"pbtInner","enabled",false);
                setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",false);
                setProperty(scopeQuick->rootObject(),"ifShift","enabled",false);
                setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
                passbandWidth = double(m.pass/1000000.0);
            }

            if (m.mk == modeFM)
            {
                queue->addUnique(priorityMediumHigh,funcToneFreq,true,t.receiver);
                queue->addUnique(priorityMediumHigh,funcTSQLFreq,true,t.receiver);
                queue->addUnique(priorityMediumHigh,funcRepeaterTone,true,t.receiver);
                queue->addUnique(priorityMediumHigh,funcRepeaterTSQL,true,t.receiver);
            }
            else
            {
                queue->del(funcToneFreq,t.receiver);
                queue->del(funcTSQLFreq,t.receiver);
                queue->del(funcRepeaterTone,t.receiver);
                queue->del(funcRepeaterTSQL,t.receiver);
            }

            if (m.mk == modeDD || m.mk == modeDV)
            {
                t = queue->getVfoCommand(vfoB,receiver,false);
                queue->del(t.freqFunc,t.receiver);
                queue->del(t.modeFunc,t.receiver);
            } else if (queue->getState().vfoMode == vfoModeVfo && !memMode) {
                t = queue->getVfoCommand(vfoB,receiver,false);
                queue->addUnique(priorityHigh,t.freqFunc,true,t.receiver);
                queue->addUnique(priorityHigh,t.modeFunc,true,t.receiver);
            }

            if (m.mk == modeCW || m.mk == modeCW_R)
            {
                queue->addUnique(priorityMediumHigh,funcCwPitch,true,t.receiver);
                queue->addUnique(priorityMediumHigh,funcDashRatio,true,t.receiver);
                queue->addUnique(priorityMediumHigh,funcKeySpeed,true,t.receiver);
                queue->addUnique(priorityMediumHigh,funcBreakIn,true,t.receiver);
            } else {
                queue->del(funcCwPitch,t.receiver);
                queue->del(funcDashRatio,t.receiver);
                queue->del(funcKeySpeed,t.receiver);
                queue->del(funcBreakIn,t.receiver);
            }

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

            switch (m.mk) {
            // M0VSE this needs to be replaced with 1/2 the "actual" width of the RTTY signal+the mark freq.
            case modeRTTY:
            case modeRTTY_R:
                passbandCenterFrequency = 0.00008925;
                break;
            case modeLSB:
            case modeUSB:
            case modePSK:
            case modePSK_R:
                if (rigCaps->manufacturer == manufIcom)
                    passbandCenterFrequency = 0.0015;
                else if (rigCaps->manufacturer == manufKenwood)
                    passbandCenterFrequency = 0.0;
                else if (rigCaps->manufacturer == manufYaesu)
                    passbandCenterFrequency = 0.002;

            case modeAM:
            case modeCW:
            case modeCW_R:
                break;
            default:
                break;
            }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
        }
        this->mode = m;
    }
}

quint64 receiverWidget::roundFrequency(quint64 frequency, unsigned int tsHz)
{
    return roundFrequency(frequency, 0, tsHz);
}

quint64 receiverWidget::roundFrequency(quint64 frequency, int steps, unsigned int tsHz)
{
    if(steps > 0)
    {
        frequency = frequency + (quint64)(steps*tsHz);
    } else if (steps < 0) {
        frequency = frequency - std::min((quint64)(abs(steps)*tsHz), frequency);
    }

    quint64 rounded = frequency;

    if(tuningFloorZeros)
    {
        rounded = ((frequency % tsHz) > tsHz/2) ? frequency + tsHz - frequency%tsHz : frequency - frequency%tsHz;
    }

    return rounded;
}


void receiverWidget::receiveCwPitch(quint16 pitch)
{
    if (mode.mk == modeCW || mode.mk == modeCW_R) {        
        if (pitch != this->cwPitch)
        {
            passbandCenterFrequency = pitch / 2000000.0;
            qDebug(logSystem()) << QString("%0 Received new CW Pitch %1 Hz was %2 (center freq %3 MHz)").arg((receiver?"Sub":"Main")).arg(pitch).arg(cwPitch).arg(passbandCenterFrequency);
            this->cwPitch = pitch;
        }
    }
}

void receiverWidget::receivePassband(quint16 pass)
{
    double pb = (double)(pass / 1000000.0);
    if (passbandWidth != pb) {
        passbandWidth = pb;
        //trxadj->updatePassband(pass);
        qDebug(logSystem()) << QString("%0 Received new IF Filter/Passband %1 Hz").arg(receiver?"Sub":"Main").arg(pass);
        emit showStatusBarText(QString("%0 IF filter width %1 Hz (%2 MHz)").arg(receiver?"Sub":"Main").arg(pass).arg(passbandWidth));
        setFilterWidth(pass,false);
    }
}

void receiverWidget::selected(bool en)
{
    isActive = en;

    // Hide scope if we are not selected and don't have command 29.
    this->displayScope(en || rigCaps->hasCommand29);
    this->setEnabled(en || rigCaps->hasCommand29);

    // If this scope is not visible set to visible if selected.
    this->setVisible(en || this->isVisible());

    if (en)
    {
        this->setStyleSheet("QGroupBox { border:1px solid red;}");
    }
    else
    {
        this->setStyleSheet(defaultStyleSheet);
    }
}

void receiverWidget::receiveSpots(uchar receiver, QVector<spotData> spots)
{
    if (receiver != this->receiver) {
        return;
    }

    spectrum->setSpots(spots);

    //QElapsedTimer timer;
    //timer.start();
    bool current = false;

    if (clusterSpots.size() > 0) {
        current=clusterSpots.begin().value()->current;
    }

    for(const auto &s: std::as_const(spots))
    {
        bool found = false;
        QMap<QString, spotData*>::iterator spot = clusterSpots.find(s.dxcall);

        while (spot != clusterSpots.end() && spot.key() == s.dxcall && spot.value()->frequency == s.frequency) {
            spot.value()->current = !current;
            found = true;
            ++spot;
        }

        if (!found)
        {
            /*
            QCPRange xrange=spectrum->xAxis->range();
            QCPRange yrange=spectrum->yAxis->range();
            double left = s.frequency;
            double top = yrange.upper-2.0;

            spotData* sp = new spotData(s);
            sp->current = !current;
            sp->text = new QCPItemText(spectrum);
            sp->text->setAntialiased(true);
            sp->text->setColor(colors.clusterSpots);
            sp->text->setText(sp->dxcall);
            sp->text->setFont(QFont(font().family(), 10));
            sp->text->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
            sp->text->position->setType(QCPItemPosition::ptPlotCoords);
            //QMargins margin;
            //int width = (sp->text->right - sp->text->left) / 8;
            //margin.setLeft(width);
            //margin.setRight(width);
            //sp->text->setPadding(margin);
            sp->text->position->setCoords(left, top);

            qDebug(logCluster()) << "ADD:" << sp->dxcall;

            bool conflict = true;

            while (conflict) {
#if QCUSTOMPLOT_VERSION < 0x020000
                QCPItemText* item = dynamic_cast<QCPItemText*>(spectrum->itemAt(QPointF(spectrum->xAxis->coordToPixel(left),spectrum->yAxis->coordToPixel(top)), true));
#else
                QCPItemText* item = dynamic_cast<QCPItemText*>(spectrum->itemAt(QPointF(spectrum->xAxis->coordToPixel(left),spectrum->yAxis->coordToPixel(top)), true));
#endif
                //QCPItemText* item = dynamic_cast<QCPItemText*> (absitem);
                if (item != nullptr) {
                    //qInfo() << "Spot found at top:" << top << "left:" << left << "(" << spectrum->xAxis->coordToPixel(top) <<"," << spectrum->xAxis->coordToPixel(left) << ") spot:" << item->text();
                    top = top - 5.0;
                    if (top < 10.0)
                    {
                        top = yrange.upper-2.0;
                        left = left + (xrange.size()/20);
                    }
                }
                else {
                    conflict = false;
                }
            }
            sp->text->position->setCoords(left, top);
            sp->text->setSelectable(true);
            sp->text->setVisible(true);

            clusterSpots.insert(sp->dxcall, sp);
            */
        }
    }

    QMutableMapIterator<QString, spotData *> sp(clusterSpots);
    while (sp.hasNext())
    {
        auto spot = sp.next();
        if (spot.value()->current == current) {
            //spectrum->removeItem(spot.value()->text);
            delete spot.value();
            sp.remove();
        }
    }

    //qDebug(logCluster()) << "Processing took" << timer.nsecsElapsed() / 1000 << "us";
}

void receiverWidget::configPressed()
{/*
    this->configGroup->setVisible(!this->configGroup->isVisible());
    //QTimer::singleShot(200, this, [this]() {
        if (this->configGroup->isVisible())
            this->confButton->setText(">");
        else
            this->confButton->setText("<");
    //});
*/
}

void receiverWidget::freqNoteLockTimerSlot() {
    this->allowAcceptFreqData = true;
}

void receiverWidget::tempLockAcceptFreqData() {
    this->allowAcceptFreqData = false;
    frequencyNotificationLockoutTimer->start();
}

void receiverWidget::setPBTInner (uchar val) {

    if (rigCaps->manufacturer == manufKenwood)
    {
        ushort width=0;
        if (this->mode.mk == modeLSB || this->mode.mk == modeUSB)
        {
            if (val < 25)
                width = (val+6) * 100;
            else if (val == 25)
                width = 3400;
            else if (val == 26)
                width = 4000;
            else if (val == 27)
                width = 5000;
        }
        if (double(width)/1000000.0 != this->PBTInner)
        {
            this->PBTInner = double(width)/1000000.0;
            //configPbtInner->blockSignals(true);
            //configPbtInner->setValue(val);
            //configPbtInner->blockSignals(false);
        }

    } else
    {
        qint16 shift = (qint16)(val - 128);
        double tempVar = ceil((shift / 127.0) * passbandWidth * 20000.0) / 20000.0;
        // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
        double pitch = 0.0;
        if ((this->mode.mk == modeCW || this->mode.mk == modeCW_R) && this->passbandWidth > 0.0006)
        {
            pitch = (600.0 - cwPitch) / 1000000.0;
        }

        double newPBT = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.

        if (newPBT != this->PBTInner) {
            this->PBTInner = newPBT;
            double pbFreq = ((double)(this->PBTInner) / this->passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255) {
                //configPbtInner->blockSignals(true);
                //configPbtInner->setValue(newFreq);
                //configPbtInner->blockSignals(false);
            }
        }
    }
}

void receiverWidget::setPBTOuter (uchar val) {
    if (rigCaps->manufacturer == manufKenwood)
    {
        ushort width=0;
        if (this->mode.mk == modeLSB || this->mode.mk == modeUSB)
        {
            if (val == 1)
                width = 50;
            else if (val > 1 && val < 22)
                width = (val-1) * 100;
        }
        if (double(width)/1000000.0 != this->PBTOuter)
        {
            this->PBTOuter = double(width)/1000000.0;
            //configPbtOuter->blockSignals(true);
            //configPbtOuter->setValue(val);
            //configPbtOuter->blockSignals(false);
        }

    } else
    {
        qint16 shift = (qint16)(val - 128);
        double tempVar = ceil((shift / 127.0) * this->passbandWidth * 20000.0) / 20000.0;
        // tempVar now contains value to the nearest 50Hz If CW mode, add/remove the cwPitch.
        double pitch = 0.0;
        if ((this->mode.mk == modeCW || this->mode.mk == modeCW_R) && this->passbandWidth > 0.0006)
        {
            pitch = (600.0 - cwPitch) / 1000000.0;
        }

        double newPBT = round((tempVar + pitch) * 200000.0) / 200000.0; // Nearest 5Hz.

        if (newPBT != this->PBTOuter) {
            this->PBTOuter = newPBT;
            double pbFreq = ((double)(this->PBTOuter) / this->passbandWidth) * 127.0;
            qint16 newFreq = pbFreq + 128;
            if (newFreq >= 0 && newFreq <= 255) {
                //configPbtOuter->blockSignals(true);
                //configPbtOuter->setValue(newFreq);
                //configPbtOuter->blockSignals(false);
            }
        }
    }
}

void receiverWidget::setIFShift(uchar val)
{
    //configIfShift->setEnabled(true);
    if (val != this->ifShift)
    {
        //configIfShift->blockSignals(true);
        //configIfShift->setValue(val);
        //configIfShift->blockSignals(false);
        this->ifShift = val;
    }
}

void receiverWidget::setFrequencyLocally(freqt f, uchar vfo) {
    // This function is a duplicate of the regular setFrequency
    // function, but without the locking mechanism.
    // It is separate because we may wish to do additional things
    // differently for locally-generated frequency changes.


    if (vfo==0 && freqDisplayA) {
        freqDisplayA->blockSignals(true);
        freqDisplayA->setFrequency(f.Hz);
        freqDisplayA->blockSignals(false);
        this->freq = f;
        for (const auto &b: rigCaps->bands)
        {
            // Highest frequency band is always first!
            if (f.Hz >= b.lowFreq && f.Hz <= b.highFreq)
            {
                // This frequency is contained within this band!
                if (currentBand.band != b.band) {
                    currentBand = b;
                    emit bandChanged(this->receiver,b);
                }
                break;
            }
        }
    } else if (vfo==1 && numVFO > 1 && freqDisplayB)
    {
        freqDisplayB->blockSignals(true);
        freqDisplayB->setFrequency(f.Hz);
        freqDisplayB->blockSignals(false);
        this->unselectedFreq=f;
    }
}

void receiverWidget::setFreqFromScope(double f)
{

    if (!freqLock)
    {
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
        freqt freqGo;
        freqGo.Hz = f * 1E6;
        freqGo.Hz = roundFrequency(freqGo.Hz, stepSize);
        freqGo.MHzDouble = (float)freqGo.Hz / 1E6;
        freqGo.VFO = selVFO_t::activeVFO;
        queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(freqGo),false,t.receiver));
    }
}

void receiverWidget::setFrequency(freqt f, uchar vfo)
{
    //qInfo() << "receiver:" << receiver << "Setting Frequency vfo=" << vfo << "Freq:" << f.Hz;
    if(!allowAcceptFreqData)
        return;

    if (vfo==0 && freqDisplayA) {
        freqDisplayA->blockSignals(true);
        freqDisplayA->setFrequency(f.Hz);
        freqDisplayA->blockSignals(false);

        this->freq = f;
        for (const auto &b: rigCaps->bands)
        {
            // Highest frequency band is always first!
            if (f.Hz >= b.lowFreq && f.Hz <= b.highFreq)
            {
                // This frequency is contained within this band!
                if (currentBand.band != b.band) {
                    currentBand = b;
                    emit bandChanged(this->receiver,b);
                }
                break;
            }
        }
    } else if (vfo==1 && numVFO > 1 && freqDisplayB)
    {
        freqDisplayA->blockSignals(true);
        freqDisplayA->setFrequency(f.Hz);
        freqDisplayA->blockSignals(false);
        this->unselectedFreq=f;
    }
}

void receiverWidget::showBandIndicators(bool en)
{
    /*
    for (auto &bi: bandIndicators)
    {
        bi.line->setVisible(en);
        bi.text->setVisible(en);
    }
    if (!bandIndicators.empty())
    {
        bandIndicatorsVisible=en;
    }
    */
}



void receiverWidget::setBandIndicators(bool show, QString region, std::vector<bandType>* bands)
{

    this->currentRegion = region;


    // Step through the bands and add all indicators!
    activeBands.clear();
    if (show) {
        for (auto &band: *bands)
        {
            if (band.region == "" || band.region == region) {
                // Add band line to current scope!
                activeBands.append(band);
            }
        }
        //if (spectrum)
        //    spectrum->setBands(activeBands);
    }
}

void receiverWidget::displaySettings(int numDigits, qint64 minf, qint64 maxf, int minStep,FctlUnit unit, std::vector<bandType>* bands)
{
    this->minFreqMhz = minf / 1000000.0;
    this->maxFreqMhz = maxf / 1000000.0;
    this->freqDigits = numDigits;
    this->freqMin = minf;
    this->freqMax = maxf;
    this->freqMinStep = minStep;
    this->freqUnit = unit;

    if (freqDisplayA)
        freqDisplayA->setup(this->freqDigits, this->freqMin, this->freqMax, this->freqMinStep, this->freqUnit,nullptr);
    if (freqDisplayA)
        freqDisplayB->setup(this->freqDigits, this->freqMin, this->freqMax, this->freqMinStep, this->freqUnit,nullptr);
}

void receiverWidget::setUnit(FctlUnit unit)
{
    this->freqUnit = unit;
    if (freqDisplayA)
        freqDisplayA->setUnit(unit);
    if (freqDisplayB)
        freqDisplayB->setUnit(unit);
}


void receiverWidget::newFrequency(qint64 freq,uchar vfo)
{
    freqt f;
    f.Hz = freq;
    f.MHzDouble = f.Hz / (double)1E6;
    if (f.Hz > 0 && !freqLock)
    {
        emit sendTrack(f.Hz-this->freq.Hz);
        vfoCommandType t = queue->getVfoCommand(vfo_t(vfo),receiver,true);
        queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(f),false,t.receiver));
        setFrequencyLocally(f, vfo);
        tempLockAcceptFreqData();
        //qInfo() << "Setting new frequency for rx:" << receiver << "vfo:" << vfo << "freq:" << f.Hz << "using:" << funcString[t.freqFunc];
    }
}

void receiverWidget::detachScopeFromQml(bool state)
{
    // Defer the real detach to the next iteration of the event loop
    QTimer::singleShot(0, this, [this, state]() {
        this->detachScope(state);
    });
}


void receiverWidget::detachScope(bool state)
{
    if (!scopeQuick)
        return;

    qInfo() << "Detaching scope";
    //return;

    if (state) {
        // DETACH
        if (!scopeFloatWindow) {
            scopeFloatWindow = new QWidget(nullptr);
            //scopeFloatWindow->setAttribute(Qt::WA_DeleteOnClose, false);
            scopeFloatWindow->setWindowTitle(QString("Scope: %0").arg(receiver?"Sub":"Main"));
            scopeFloatWindow->setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint );
            auto *layout = new QVBoxLayout(scopeFloatWindow);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(scopeQuick);
        } else {
            if (scopeFloatWindow->layout() &&
                scopeFloatWindow->layout()->indexOf(scopeQuick) == -1) {
                scopeFloatWindow->layout()->addWidget(scopeQuick);
            }
        }

        if (scopeOrigLayout &&
            scopeOrigLayout->indexOf(scopeQuick) != -1) {
            scopeOrigLayout->removeWidget(scopeQuick);
        }

        if (scopeOrigParent)
            scopeFloatWindow->resize(scopeOrigParent->size());

        scopeFloatWindow->show();
        scopeFloatWindow->raise();
        scopeFloatWindow->activateWindow();
    } else {
        // ATTACH
        if (!scopeOrigParent || !scopeOrigLayout) {
            qWarning() << "detachScope: no original parent/layout to attach to";
            return;
        }

        if (scopeFloatWindow && scopeFloatWindow->layout() &&
            scopeFloatWindow->layout()->indexOf(scopeQuick) != -1) {
            scopeFloatWindow->layout()->removeWidget(scopeQuick);
        }

        scopeQuick->setParent(scopeOrigParent);

        // Try to cast to QBoxLayout so we can use insertWidget
        if (auto *box = qobject_cast<QVBoxLayout*>(scopeOrigLayout)) {
            if (scopeOrigIndex >= 0 && scopeOrigIndex <= box->count())
                box->insertWidget(scopeOrigIndex-1, scopeQuick);
            else
                box->addWidget(scopeQuick);
        } else {
            // Fallback: generic QLayout, no insertWidget available
            scopeOrigLayout->addWidget(scopeQuick);
        }

        if (scopeFloatWindow)
            scopeFloatWindow->hide();

        scopeOrigParent->show();
    }
}

void receiverWidget::updateBSR(std::vector<bandType>* bands)
{
    // Send a new BSR value for the current frequency.
    // Not currently used.
    for (auto &b: *bands)
    {
        /*
        if (quint64(freqDisplay[0]->getFrequency()) >= b.lowFreq && quint64(freqDisplay[0]->getFrequency()) <= b.highFreq)
        {
            if(b.bsr != 0)
            {
                bandStackType bs(b.bsr,1);
                bs.data=dataCombo->currentIndex();
                bs.filter=filterCombo->currentData().toInt();
                bs.freq.Hz = freqDisplay[0]->getFrequency();
                bs.freq.MHzDouble=bs.freq.Hz/1000000.0;
                bs.mode=scopeModeCombo->currentData().toInt();
                bs.sql=0;
                bs.tone.tone=770;
                bs.tsql.tone=770;
                vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
                queue->addUnique(priorityImmediate,queueItem(funcBandStackReg,QVariant::fromValue<bandStackType>(bs),false,t.receiver));
            }
            break;
        }*/
    }
}

void receiverWidget::memoryMode(bool en)
{
    this->memMode=en;
    qInfo(logRig) << "Receiver" << receiver << "Memory mode:" << en;
    if (en) {
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);
        queue->del(t.freqFunc,t.receiver);
        queue->del(t.modeFunc,t.receiver);
    }
    //vfoMemoryButton->setChecked(en);
}

QImage receiverWidget::getSpectrumImage()
{
    QMutexLocker locker(&mutex);
    //return spectrum->toPixmap().toImage();
    return QImage();
}
QImage receiverWidget::getWaterfallImage()
{
    QMutexLocker locker(&mutex);
    //return waterfall->toPixmap().toImage();
    return QImage();
}

void receiverWidget::vfoSwap()
{
    if (!tracking) {
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
        if (rigCaps->commands.contains(funcVFOSwapAB))
        {
            queue->add(priorityImmediate,funcVFOSwapAB,false,t.receiver);
        }
        else
        {
            // Manufacture a VFO Swap (should be fun!)
            queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(unselectedFreq),false,t.receiver));
            queue->add(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(unselectedMode),false,t.receiver));
            t = queue->getVfoCommand(vfoB,receiver,true);
            queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(freq),false,t.receiver));
            queue->add(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(mode),false,t.receiver));
        }
    }
}

void receiverWidget::receiveTrack(int f)
{
    return; // Disabled for now.
    qInfo(logRig) << "Got tracking for rx" << receiver<< "amount" << f << "Hz";
    if (tracking && receiver) {
        // OK I am the sub receiver so lets try this.
        freqt freqGo;
        freqGo.Hz=this->freq.Hz+f;
        freqGo.MHzDouble = (float)freqGo.Hz / 1E6;
        freqGo.VFO = activeVFO;
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
        queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(freqGo),false,t.receiver));
        freq=freqGo;
    }
}

// This function will request an update of all freq/mode info from the rig
void receiverWidget::updateInfo()
{
    // If we are not the active receiver, delete selected/unselected commands
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);
    queue->add(priorityImmediate,t.freqFunc,false,t.receiver);
    queue->add(priorityImmediate,t.modeFunc,false,t.receiver);

    if ((rigCaps->hasCommand29 || !receiver) && numVFO > 1) {
        t = queue->getVfoCommand(vfoB,receiver,false);
        queue->add(priorityImmediate,t.freqFunc,false,t.receiver);
        queue->add(priorityImmediate,t.modeFunc,false,t.receiver);
    }
}


void receiverWidget::onHoverSpotChanged(const spotData &spot, QPointF itemPos, bool active)
{
    if (!scopeQuick)
        return;

    if (!active) {
        QToolTip::hideText();
        return;
    }

    // 1. Item coords → Scene coords
    QPointF scenePos = spectrum->mapToScene(itemPos);

    // 2. Scene coords → Global screen coords
    QPoint globalPos = spectrum->window()->mapToGlobal(scenePos.toPoint());

    // 3. Optional: convert global → widget-local (not strictly needed for QToolTip)
    // QPoint widgetPos = scopeQuick->mapFromGlobal(globalPos);

    // --- Build popup text ---
    QString text;
    text += QStringLiteral("<b>%1</b><br>").arg(spot.dxcall);
    text += QStringLiteral("Freq: %1<br>").arg(spot.frequency, 0, 'f', 3);
    text += QStringLiteral("Spotter: %1<br>").arg(spot.spottercall);
    text += QStringLiteral("Mode: %1<br>").arg(spot.mode);
    text += QStringLiteral("Time: %1<br>").arg(spot.timestamp.toString(Qt::ISODate));
    if (!spot.comment.isEmpty())
        text += QStringLiteral("Comment: %1").arg(spot.comment.toHtmlEscaped());

    // Show tooltip at correct global position
    QToolTip::showText(globalPos, text, scopeQuick);
}


// QT Quick ComboBoxes

void receiverWidget::setScopeMode(uchar m, bool u)
{
    if (scopeQuick && m != currentScopeMode) {
        currentScopeMode = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeMode,QVariant::fromValue(currentScopeMode),false,t.receiver));
        } else {
            emit scopeModeChanged();
        }
    }
}

void receiverWidget::setScopeSpan(centerSpanData m, bool u)
{
    if (currentScopeSpan != m)
    {
        currentScopeSpan = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpan,QVariant::fromValue(currentScopeSpan),false,t.receiver));
        } else {
            emit scopeSpanChanged();
        }
    }
}

void receiverWidget::setScopeEdge(uchar m, bool u)
{
    if (currentScopeEdge != m)
    {
        currentScopeEdge = m;
        if (u) {
            // Code to set scope edge to be added
        }else {
            emit scopeEdgeChanged();
        }
    }
}


void receiverWidget::setMode(modeInfo m, bool u)
{
    if (currentMode != m)
    {
        m.data = currentMode.data;
        m.filter = currentMode.filter;
        currentMode = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);

            queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(m),false,t.receiver));
            if (t.modeFunc == funcModeSet) {
                queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(m),false,t.receiver));
            }
            // Request current filtershape/roofing
            if (rigCaps->manufacturer == manufIcom)
            {
                queue->addUnique(priorityHighest,funcFilterShape,false,t.receiver);
                queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
            }
        }else {
            emit modeChanged();
        }
    }

}


void receiverWidget::setDataMode(uchar m, bool u)
{
    if (currentMode.data != m)
    {
        currentMode.data = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(currentMode),false,t.receiver));
            if (t.modeFunc == funcModeSet) {
                queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(currentMode),false,t.receiver));
            }
            // Request current filtershape/roofing
            if (rigCaps->manufacturer == manufIcom)
            {
                queue->addUnique(priorityHighest,funcFilterShape,false,t.receiver);
                queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
            }
        }else {
            emit dataModeChanged();
        }
    }
}


void receiverWidget::setFilter(uchar m, bool u)
{
    if (currentMode.filter != m)
    {
        currentMode.filter = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(currentMode),false,t.receiver));
            if (t.modeFunc == funcModeSet) {
                queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(currentMode),false,t.receiver));
            }
            // Request current filtershape/roofing
            if (rigCaps->manufacturer == manufIcom)
            {
                queue->addUnique(priorityHighest,funcFilterShape,false,t.receiver);
                queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
            }
        }else {
            emit filterChanged();
        }
    }
}

void receiverWidget::setFilterShape(uchar m, bool u)
{
    if (currentFilterShape != m)
    {
        currentFilterShape = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            uchar f = uchar(m + (currentMode.filter * 10));
            queue->addUnique(priorityImmediate,queueItem(funcFilterShape,QVariant::fromValue<uchar>(f),false,t.receiver));
        }else {
            emit filterShapeChanged();
        }
    }
}
void receiverWidget::setRoofing(uchar m, bool u)
{
    if (currentRoofing != m)
    {
        currentRoofing = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            uchar f = uchar(m + (currentMode.filter * 10));
            queue->addUnique(priorityImmediate,queueItem(funcRoofingFilter,QVariant::fromValue<uchar>(f),false,t.receiver));
        }else {
            emit roofingChanged();
        }
    }
}

void receiverWidget::setSpeed(uchar m, bool u)
{
    if (currentSpeed != m)
    {
        currentSpeed = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpeed,QVariant::fromValue(m),false,t.receiver));
        }else {
            emit speedChanged();
        }
    }
}

void receiverWidget::setTheme(WaterfallItem::Theme m, bool u)
{

    if (currentTheme != m)
    {
        currentTheme = m;

        if (!u) {
            emit themeChanged();
        }

        if (waterfall)
            waterfall->setTheme(currentTheme);
    }
}

void receiverWidget::setHold(bool v, bool u)
{
    if (currentHold != v)
    {
        currentHold = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->add(priorityImmediate,queueItem(funcScopeHold,QVariant::fromValue<bool>(v),false,t.receiver));
        } else {
            emit holdChanged();
        }
    }
}

void receiverWidget::setRef(int v, bool u)
{
    if (currentRef != v)
    {
        currentRef = v; // Step size is 5 so no need to round up/down.
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeRef,QVariant::fromValue(v),false,t.receiver));
        } else {
            emit refChanged();
        }
    }
}

void receiverWidget::setPbtInner(int v, bool u)
{
    if (currentPbtInner != v)
    {
        currentPbtInner = v; // Step size is 5 so no need to round up/down.
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit pbtInnerChanged();
        }
    }
}

void receiverWidget::setPbtOuter(int v, bool u)
{
    if (currentPbtOuter != v)
    {
        currentPbtOuter = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit pbtOuterChanged();
        }
    }
}

void receiverWidget::setIfShift(int v, bool u)
{
    if (currentIfShift != v)
    {
        currentIfShift = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcIFShift,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit ifShiftChanged();
        }
    }
}

void receiverWidget::setFilterWidth(int v, bool u)
{
    qInfo() << "*** FILTER WIDTH" << v << "user" <<u;
    if (currentFilterWidth != v)
    {
        currentFilterWidth = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcFilterWidth,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit filterWidthChanged();
        }
    }
}

void receiverWidget::toFixedEdge(uchar val)
{
    qInfo() << "Setting new fixed edge:" << val;
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    queue->addUnique(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,QVariant::fromValue(spectrumBounds(lowerFreq, upperFreq, val)),false,t.receiver));
    queue->addUnique(priorityImmediate,queueItem(funcScopeEdge,QVariant::fromValue<uchar>(val),false,t.receiver));
    queue->addUnique(priorityImmediate,queueItem(funcScopeMode,QVariant::fromValue<uchar>(1),false,t.receiver));
}


QVariantMap receiverWidget::customEdgeFreqDialogParams() const
{


    int maxSpan = 0;
    int minSpan = 1;
    for (const auto &span: rigCaps->scopeCenterSpans)
    {
        if (int(span.freq / 1000) > maxSpan)
            maxSpan = int(span.freq / 1000);
        if (int(span.freq / 1000) < minSpan)
            minSpan = int(span.freq / 1000);
    }
    maxSpan = maxSpan * 2;
    minSpan = minSpan * 2;

    return {
        {"minKHz", quint64(minFreqMhz*1000)},
        {"maxKHz", quint64(maxFreqMhz*1000)},
        {"startKHz", quint64(lowerFreq*1000)},
        {"endKHz", quint64(upperFreq*1000)},
        {"minSpan", minSpan},
        {"maxSpan", maxSpan}
    };
}


void receiverWidget::setCustomEdgeFreqs(quint64 startKHz, quint64 endKHz)
{
    qDebug(logGui()) << "setting edge to: " << startKHz << ", " << endKHz << ", edge num: " << currentScopeEdge;
    queue->addUnique(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,QVariant::fromValue(spectrumBounds(double(startKHz/1000.0), double(endKHz/1000.0), currentScopeEdge)),false,receiver));
}




/*
    displayLayout = new QHBoxLayout();


    vfoSelectButton=new QPushButton(tr("VFO A"),this);
    vfoSelectButton->setHidden(true);
    vfoSelectButton->setCheckable(true);
    vfoSelectButton->setFocusPolicy(Qt::NoFocus);
    connect(vfoSelectButton, &QPushButton::clicked, this, [=](bool en) {
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);
        queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(vfo_t(en)),false,t.receiver));
        queue->add(priorityHighest,t.freqFunc,false,t.receiver);
        queue->add(priorityHighest,t.modeFunc,false,t.receiver);
        t = queue->getVfoCommand(vfoB,receiver,false);
        queue->add(priorityHighest,t.freqFunc,false,t.receiver);
        queue->add(priorityHighest,t.modeFunc,false,t.receiver);
        if (en)
            vfoSelectButton->setText(tr("VFO B"));
        else
            vfoSelectButton->setText(tr("VFO A"));
        selectedVFO = uchar(en);
    });

    vfoSwapButton=new QPushButton(tr("A<>B"),this);
    vfoSwapButton->setHidden(true);
    vfoSwapButton->setFocusPolicy(Qt::NoFocus);
    connect(vfoSwapButton, &QPushButton::clicked, this, [=]() {
        vfoSwap();
        vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);
        queue->add(priorityHighest,t.modeFunc,false,t.receiver);
        queue->add(priorityHighest,t.freqFunc,false,t.receiver);
    });

    vfoEqualsButton=new QPushButton(tr("A=B"),this);
    vfoEqualsButton->setHidden(true);
    vfoEqualsButton->setFocusPolicy(Qt::NoFocus);
    connect(vfoEqualsButton, &QPushButton::clicked, this, [=]() {
        vfoCommandType t = queue->getVfoCommand(vfoB,receiver,false);
        queue->add(priorityImmediate,funcVFOEqualAB,false,0);
        queue->add(priorityHighest,t.modeFunc,false,t.receiver);
        queue->add(priorityHighest,t.freqFunc,false,t.receiver);
    });

    vfoMemoryButton=new QPushButton(tr("V/M"),this);
    vfoMemoryButton->setHidden(true);
    vfoMemoryButton->setCheckable(true);
    vfoMemoryButton->setFocusPolicy(Qt::NoFocus);
    connect(vfoMemoryButton, &QPushButton::clicked, this, [=](bool en) {
        vfo_t v = vfoA;

        if (en)
            v = vfoMem;

        vfoCommandType t = queue->getVfoCommand(v,receiver,false);
        queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false,t.receiver));
        queue->add(priorityHighest,t.freqFunc,false,t.receiver);
        queue->add(priorityHighest,t.modeFunc,false,t.receiver);
    });

    satelliteButton=new QPushButton(tr("SAT"),this);
    satelliteButton->setHidden(true);
    satelliteButton->setCheckable(true);
    satelliteButton->setFocusPolicy(Qt::NoFocus);
    connect(satelliteButton, &QPushButton::clicked, this, [=](bool en) {
        queue->add(priorityImmediate,queueItem(funcSatelliteMode,QVariant::fromValue<bool>(en),false,0));
    });

    splitButton=new QPushButton(tr("SPLIT"),this);
    splitButton->setHidden(true);
    splitButton->setFocusPolicy(Qt::NoFocus);
    splitButton->setCheckable(true);
    connect(splitButton, &QPushButton::clicked, this, [=](bool en) {
        queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue<uchar>(en),false,0));
    });


    controlLayout = new QHBoxLayout();
    detachButton = new QPushButton(tr("Detach"));
    detachButton->setCheckable(true);
    detachButton->setToolTip(tr("Detach/re-attach scope from main window"));
    detachButton->setChecked(false);
    scopeModeCombo = new QComboBox();
    scopeModeCombo->setAccessibleDescription(tr("Spectrum Mode"));
    spanCombo = new QComboBox();
    spanCombo->setAccessibleDescription(tr("Spectrum Span"));
    edgeCombo = new QComboBox();
    edgeCombo->setAccessibleDescription(tr("Spectrum Edge"));
    edgeButton = new QPushButton(tr("Custom Edge"));
    edgeButton->setToolTip(tr("Define a custom (fixed) scope edge"));
    toFixedButton = new QPushButton(tr("To Fixed"));
    toFixedButton->setToolTip(tr("&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;Press button to convert center mode spectrum to fixed mode, preserving the range. This allows you to tune without the spectrum moving, in the same currently-visible range that you see now. &lt;/p&gt;&lt;p&gt;&lt;br/&gt;&lt;/p&gt;&lt;p&gt;The currently-selected edge slot will be overridden.&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;"));

    holdButton = new QPushButton("HOLD");
    holdButton->setCheckable(true);
    holdButton->setFocusPolicy(Qt::NoFocus);

    controlSpacer = new QSpacerItem(0,0,QSizePolicy::Expanding,QSizePolicy::Fixed);
    midSpacer = new QSpacerItem(0,0,QSizePolicy::Expanding,QSizePolicy::Fixed);

    clearPeaksButton = new QPushButton("Clear Peaks");

    confButton = new QPushButton("<");
    confButton->setAccessibleName(tr("Configure Scope"));
    confButton->setAccessibleDescription(tr("Change various settings of the current Scope"));
    confButton->setToolTip(tr("Configure Scope"));
    confButton->setFocusPolicy(Qt::NoFocus);

    modeCombo = new QComboBox();
    modeCombo->setToolTip(tr("Select current radio mode"));
    modeCombo->setAccessibleDescription(tr("Change the current radio mode for this receiver"));

    dataCombo = new QComboBox();
    dataCombo->setToolTip(tr("Select data mode (if supported)"));
    dataCombo->setAccessibleDescription(tr("Change the current data mode (if supported)"));

    filterCombo = new QComboBox();
    filterCombo->setToolTip(tr("Select current filter"));
    dataCombo->setAccessibleDescription(tr("Change the current filter"));

    filterShapeCombo = new QComboBox();
    filterShapeCombo->setToolTip(tr("Select current filter shape"));
    filterShapeCombo->setAccessibleDescription(tr("Change the current filter shape"));
    if (!rigCaps->commands.contains(funcFilterShape))
    {
        filterShapeCombo->hide();
    }
    else
    {
        filterShapeCombo->addItem("Sharp",0);
        if (rigCaps->manufacturer == manufKenwood)
        {
            filterShapeCombo->addItem("Medium",1);
            filterShapeCombo->addItem("Soft",2);

        }
        else
        {
            filterShapeCombo->addItem("Soft",1);
        }
    }
    roofingCombo = new QComboBox();
    roofingCombo->setToolTip(tr("Select roofing filter"));
    roofingCombo->setAccessibleDescription(tr("Change the current selected roofing filter"));
    if (!rigCaps->commands.contains(funcRoofingFilter))
        roofingCombo->hide();

    spanCombo->setVisible(false);
    edgeCombo->setVisible(false);
    edgeButton->setVisible(false);
    toFixedButton->setVisible(false);
    */
