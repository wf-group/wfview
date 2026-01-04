#include "ReceiverController.h"

#include "logcategories.h"


ReceiverController::ReceiverController(int rxIndex, QObject *parent)
        : QObject(parent), receiver(rxIndex)
{
    qInfo() << "Creating new Receiver(" << rxIndex <<")";

    // We don't really care if the rig disconnects, as we will be destroyed anyway!
    queue = cachingQueue::getInstance();
    rigCaps = queue->getRigCaps();

    if (!receiver)
        setActive(true);

    buildUiSpecs();
}

/*
 * setScopeData(const scopeData &d)
 *
 * This function receives the scope data, processes it as much as possible and then sends to the scope/waterfall for display
 */
void ReceiverController::setScopeData(const scopeData &d)
{

    // Have we received scope data before?
    if (!scopeReceived)
    {
        queue->del(funcScopeOnOff,receiver); // Delete recurring on/off command
        scopeReceived=true;
    }


    double freq = (frequencyA/1000000.0);

    double pbLow = 0.0;
    double pbHigh = 0.0;
    switch (mode.mk)
    {
    case modeLSB:
    case modeRTTY_R:
    case modePSK_R:
        if (rigCaps->manufacturer == manufKenwood) {
            pbLow = freq-passbandWidth;
            pbHigh = freq;
        } else {
            pbLow = freq - passbandCenterFrequency - (passbandWidth / 2);
            pbHigh = freq - passbandCenterFrequency + (passbandWidth / 2);
        }
        break;
    case modeCW:
        if (passbandWidth < 0.0006) {
            pbLow = freq - (passbandWidth / 2);
            pbHigh = freq + (passbandWidth / 2);
        }
        else {
            pbLow = freq + passbandCenterFrequency - passbandWidth;
            pbHigh = freq + passbandCenterFrequency;
        }
        break;
    case modeCW_R:
        if (passbandWidth < 0.0006) {
            pbLow = freq - (passbandWidth / 2);
            pbHigh = freq + (passbandWidth / 2);
        }
        else {
            pbLow = freq - passbandCenterFrequency;
            pbHigh = freq + passbandWidth - passbandCenterFrequency;
        }
        break;
    default:
        if (rigCaps->manufacturer == manufKenwood) {
            pbLow = freq;
            pbHigh = freq+passbandWidth;
        } else {
            pbLow = freq + passbandCenterFrequency - (passbandWidth / 2);
            pbHigh = freq + passbandCenterFrequency + (passbandWidth / 2);
        }
        break;
    }

    if (!qFuzzyCompare(passbandLow,pbLow) || !qFuzzyCompare(passbandHigh,pbHigh))
    {
        qInfo() << "New passband" << pbLow << pbHigh;
        passbandLow = pbLow;
        passbandHigh = pbHigh;
        emit passbandChanged();
    }

    if (!d.valid || d.data.isEmpty())
        return;

    lastScope = d;               // QByteArray is implicitly shared (cheap copy)
    emit scopeUpdated(lastScope);    // fan out to Spectrum/Waterfall
}



void ReceiverController::onWheelTune(int angleDeltaY, int modifiers)
{
    //if (freqLock)
    //    return;

    // Convert angle delta to "steps" like QWidget wheel notches.
    // Typical mouse wheel: 120 per notch.
    const int steps = angleDeltaY / 120;
    if (steps == 0)
        return;

    const qreal stepsToScroll = QApplication::wheelScrollLines() * steps;

    // Did we change direction?
    if ((scrollWheelOffsetAccumulated > 0 && steps > 0) ||
        (scrollWheelOffsetAccumulated < 0 && steps < 0)) {
        scrollWheelOffsetAccumulated += stepsToScroll;
    } else {
        scrollWheelOffsetAccumulated = stepsToScroll;
    }

    const int clicks = int(scrollWheelOffsetAccumulated);
    if (!clicks)
        return;

    unsigned int stepsHz = stepSize;

    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers);

    if ((mods & Qt::ShiftModifier) && stepsHz != 1) {
        stepsHz /= 10;
    } else if (mods & Qt::ControlModifier) {
        stepsHz *= 10;
    }

    vfoCommandType t = queue->getVfoCommand(vfoA, receiver, true);

    freqt f;
    f.Hz = roundFrequency(frequencyA, clicks, stepsHz);
    f.MHzDouble = f.Hz / 1E6;

    queue->add(priorityImmediate,
               queueItem(t.freqFunc, QVariant::fromValue<freqt>(f), false, receiver));

    scrollWheelOffsetAccumulated = 0.0;
}


void ReceiverController::setTitle(QString t)
{
    if (title == t)
        return;
    title = t;
    emit titleChanged();

}

void ReceiverController::setActive(bool a)
{
    if (active == a)
        return;
    active = a;
    emit activeChanged();
}


void ReceiverController::setScopeMode(uchar m, bool u)
{
    if (m != scopeMode) {
        scopeMode = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeMode,QVariant::fromValue(scopeMode),false,t.receiver));
        } else {
            emit scopeModeChanged();
        }
    }
}

void ReceiverController::setScopeSpan(centerSpanData m, bool u)
{
    if (scopeSpan != m)
    {
        scopeSpan = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpan,QVariant::fromValue(scopeSpan),false,t.receiver));
        } else {
            emit scopeSpanChanged();
        }
    }
}

void ReceiverController::setScopeEdge(uchar m, bool u)
{
    if (scopeEdge != m)
    {
        scopeEdge = m;
        if (u) {
            // Code to set scope edge to be added
        }else {
            emit scopeEdgeChanged();
        }
    }
}



void ReceiverController::receiveMeter(meter_t inMeter, double level)
{
    setMeterType(inMeter);
    setMeter(level);
}

void ReceiverController::setMeterType(meter_t t)
{
    if (m_meterType == t) return;
    m_meterType = t;
    emit meterTypeChanged();
}

void ReceiverController::setMeter(double v)
{
    if (qFuzzyCompare(m_meter, v)) return;
    m_meter = v;
    emit meterChanged();
}


void ReceiverController::receiveMode(modeInfo m, uchar vfo)
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
        qInfo(logSystem()) << __func__ << QString("Received new filter for %0 (%1): %2 (%3) filter:%4 data:%5")
        .arg((receiver?"Sub":"Main")).arg(QString::number(m.mk)).arg(m.reg).arg(m.name).arg(m.filter).arg(m.data) ;
        setFilter(m.filter,false);
    }

    if (m.data != 0xff && this->mode.data != m.data)
    {
        qInfo(logSystem()) << __func__ << QString("Received new data mode for %0 (%1): %2 (%3) filter:%4 data:%5")
        .arg((receiver?"Sub":"Main")).arg(QString::number(m.mk)).arg(m.reg).arg(m.name).arg(m.filter).arg(m.data) ;
        setDataMode(m.data,false);
    }

    if (m.mk != modeUnknown && mode.mk != m.mk) {
        qInfo(logSystem()) << __func__ << QString("Received new mode for %0 (%1): %2 (%3) filter:%4 data:%5")
        .arg((receiver?"Sub":"Main")).arg(QString::number(m.mk)).arg(m.reg).arg(m.name).arg(m.filter).arg(m.data) ;

        /*
         * We need to be able to disable mode (or hide it)
        if (m.mk == modeCW || m.mk == modeCW_R || m.mk == modeRTTY || m.mk == modeRTTY_R || m.mk == modePSK || m.mk == modePSK_R)
        {
            setProperty(scopeQuick->rootObject(),"dataMode","enabled",false);
        } else {
            setProperty(scopeQuick->rootObject(),"dataMode","enabled",true);
        }
        */

        if (m.mk != mode.mk) {
            // We have changed mode so "may" need to change regular commands

            passbandCenterFrequency = 0.0;

            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,false);

            // Make sure the filterWidth range is within limits.

            // If new mode doesn't allow bandwidth control, disable filterwidth and pbt.
            //if (scopeQuick) setSlider(scopeQuick->rootObject(),"filterWidth",m.bwMin, m.bwMax,10,m.bwMax);

            if (m.bwMin > 0 || m.bwMax > 0) {
                // Set config specific options)
                if (rigCaps->manufacturer == manufKenwood)
                {
                    if (m.mk == modeCW || m.mk == modeCW_R || m.mk == modePSK || m.mk == modePSK_R) {
                        queue->addUnique(priorityHigh,funcFilterWidth,true,t.receiver);
                        queue->del(funcPBTInner,t.receiver);
                        queue->del(funcPBTOuter,t.receiver);
                        //setProperty(scopeQuick->rootObject(),"pbtInner","enabled",false);
                        //setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",false);
                        //setProperty(scopeQuick->rootObject(),"ifShift","enabled",false);
                        //setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
                    }
                    else if (m.mk == modeAM || m.mk == modeFM) {
                        queue->addUnique(priorityHigh,funcPBTInner,true,t.receiver);
                        queue->addUnique(priorityHigh,funcPBTOuter,true,t.receiver);
                        queue->del(funcFilterWidth,t.receiver);
                        //setProperty(scopeQuick->rootObject(),"pbtInner","enabled",true && rigCaps->commands.contains(funcPBTInner));
                        //setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",true && rigCaps->commands.contains(funcPBTOuter));
                        //setProperty(scopeQuick->rootObject(),"ifShift","enabled",true && (rigCaps->commands.contains(funcIFShift) || rigCaps->commands.contains(funcPBTInner)));
                        //setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
                    } else {
                        queue->addUnique(priorityHigh,funcPBTInner,true,t.receiver);
                        queue->addUnique(priorityHigh,funcPBTOuter,true,t.receiver);
                        queue->addUnique(priorityHigh,funcFilterWidth,true,t.receiver);
                        //setProperty(scopeQuick->rootObject(),"pbtInner","enabled",true);
                        //setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",true);
                        //setProperty(scopeQuick->rootObject(),"filterWidth","enabled",true);
                    }
                } else
                {
                    queue->addUnique(priorityHigh,funcPBTInner,true,t.receiver);
                    queue->addUnique(priorityHigh,funcPBTOuter,true,t.receiver);
                    queue->addUnique(priorityHigh,funcFilterWidth,true,t.receiver);
                    //setProperty(scopeQuick->rootObject(),"pbtInner","enabled",true);
                    //setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",true);
                    //setProperty(scopeQuick->rootObject(),"filterWidth","enabled",true);
                }
            } else{
                queue->del(funcPBTInner,t.receiver);
                queue->del(funcPBTOuter,t.receiver);
                queue->del(funcFilterWidth,t.receiver);
                //setProperty(scopeQuick->rootObject(),"pbtInner","enabled",false);
                //setProperty(scopeQuick->rootObject(),"pbtOuter","enabled",false);
                //setProperty(scopeQuick->rootObject(),"ifShift","enabled",false);
                //setProperty(scopeQuick->rootObject(),"filterWidth","enabled",false);
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
            } else if (queue->getState().vfoMode == vfoModeVfo) { // && !memMode) {
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

        setMode(m,false);
    }
}


void ReceiverController::setMode(modeInfo m, bool u)
{
    if (mode != m)
    {
        modeInfo n;
        n = m;
        n.data = mode.data;
        n.filter = mode.filter;
        mode = n;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);

            queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(n),false,t.receiver));
            if (t.modeFunc == funcModeSet) {
                queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(n),false,t.receiver));
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


void ReceiverController::setDataMode(uchar m, bool u)
{
    if (mode.data != m)
    {
        mode.data = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(mode),false,t.receiver));
            if (t.modeFunc == funcModeSet) {
                queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(mode),false,t.receiver));
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


void ReceiverController::setFilter(uchar m, bool u)
{
    if (mode.filter != m)
    {
        mode.filter = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(mode),false,t.receiver));
            if (t.modeFunc == funcModeSet) {
                queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(mode),false,t.receiver));
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

void ReceiverController::setFilterShape(uchar m, bool u)
{
    if (filterShape != m)
    {
        filterShape = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            uchar f = uchar(m + (mode.filter * 10));
            queue->addUnique(priorityImmediate,queueItem(funcFilterShape,QVariant::fromValue<uchar>(f),false,t.receiver));
        }else {
            emit filterShapeChanged();
        }
    }
}
void ReceiverController::setRoofing(uchar m, bool u)
{
    if (roofing != m)
    {
        roofing = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            uchar f = uchar(m + (mode.filter * 10));
            queue->addUnique(priorityImmediate,queueItem(funcRoofingFilter,QVariant::fromValue<uchar>(f),false,t.receiver));
        }else {
            emit roofingChanged();
        }
    }
}

void ReceiverController::setSpeed(uchar m, bool u)
{
    if (speed != m)
    {
        speed = m;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpeed,QVariant::fromValue(m),false,t.receiver));
        }else {
            emit speedChanged();
        }
    }
}

void ReceiverController::setTheme(WaterfallItem::Theme m, bool u)
{

    qInfo() << "Received new theme: " << m << "from user:" << u;
    if (theme != m)
    {
        theme = m;

        if (!u) {
            emit themeChanged();
        }
    }
}

void ReceiverController::setHold(bool v, bool u)
{
    if (hold != v)
    {
        hold = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->add(priorityImmediate,queueItem(funcScopeHold,QVariant::fromValue<bool>(v),false,t.receiver));
        } else {
            emit holdChanged();
        }
    }
}

void ReceiverController::setRef(int v, bool u)
{
    if (ref != v)
    {
        ref = v; // Step size is 5 so no need to round up/down.
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeRef,QVariant::fromValue(v),false,t.receiver));
        } else {
            emit refChanged();
        }
    }
}

void ReceiverController::setRfGain(ushort v, bool u)
{
    if (rfGain != v)
    {
        rfGain = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcRfGain,QVariant::fromValue(v),false,t.receiver));
        } else {
            emit rfGainChanged();
        }
    }
}


void ReceiverController::setAfGain(ushort v, bool u)
{
    if (afGain != v)
    {
        afGain = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAfGain,QVariant::fromValue(v),false,t.receiver));
        } else {
            emit afGainChanged();
        }
    }
}


void ReceiverController::setSquelch(ushort v, bool u)
{
    if (squelch != v)
    {
        squelch = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcSquelch,QVariant::fromValue(v),false,t.receiver));
        } else {
            emit squelchChanged();
        }
    }
}

void ReceiverController::setAttenuator(uchar v, bool u)
{
    if (attenuator != v)
    {
        attenuator = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAttenuator,QVariant::fromValue(attenuator),false,t.receiver));
        } else {
            emit attenuatorChanged();
        }
    }
}

void ReceiverController::setPreamp(uchar v, bool u)
{
    if (preamp != v)
    {
        preamp = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPreamp,QVariant::fromValue(preamp),false,t.receiver));
        } else {
            emit preampChanged();
        }
    }
}

void ReceiverController::setNr(uchar v, bool u)
{
    if (nr != v)
    {
        qInfo() << "New setNr()" << v;

        nr = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNoiseReduction,QVariant::fromValue(nr),false,t.receiver));
        } else {
            emit nrChanged();
        }
    }
}

void ReceiverController::setNrLevel(ushort v, bool u)
{
    if (nrLevel != v)
    {
        nrLevel = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNRLevel,QVariant::fromValue(nrLevel),false,t.receiver));
        } else {
            emit nrLevelChanged();
        }
    }
}

void ReceiverController::setNb(uchar v, bool u)
{
    if (nb != v)
    {
        qInfo() << "New setNb()" << v;
        nb = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNoiseBlanker,QVariant::fromValue(nb),false,t.receiver));
        } else {
            emit nbChanged();
        }
    }
}

void ReceiverController::setNbLevel(ushort v, bool u)
{
    if (nbLevel != v)
    {
        nbLevel = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNBLevel,QVariant::fromValue(nbLevel),false,t.receiver));
        } else {
            emit nbLevelChanged();
        }
    }
}

void ReceiverController::setAntenna(uchar v, bool u)
{
    if (antenna.antenna != v)
    {
        antenna.antenna = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue(antenna),false,t.receiver));
        } else {
            emit antennaChanged();
        }
    }
}


void ReceiverController::setRxAntenna(bool v, bool u)
{
    if (antenna.rx != v)
    {
        antenna.rx = v;

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue(antenna),false,t.receiver));
        } else {
            emit rxAntennaChanged();
        }
    }
}


void ReceiverController::setPbtInner(int v, bool u)
{
    if (pbtInner != v)
    {
        pbtInner = v; // Step size is 5 so no need to round up/down.
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit pbtInnerChanged();
        }
    }
}

void ReceiverController::setPbtOuter(int v, bool u)
{
    if (pbtOuter != v)
    {
        pbtOuter = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit pbtOuterChanged();
        }
    }
}

void ReceiverController::setIfShift(int v, bool u)
{
    if (ifShift != v)
    {
        ifShift = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcIFShift,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit ifShiftChanged();
        }
    }
}

void ReceiverController::setFilterWidth(int v, bool u)
{
    qInfo() << "*** FILTER WIDTH" << v << "user" <<u;
    if (filterWidth != v)
    {
        filterWidth = v;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcFilterWidth,QVariant::fromValue<ushort>(v),false,t.receiver));
        } else {
            emit filterWidthChanged();
        }
    }
}

void ReceiverController::toFixedEdge(uchar val)
{
    qInfo() << "Setting new fixed edge:" << val;
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    //queue->addUnique(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,QVariant::fromValue(spectrumBounds(lowerFreq, upperFreq, val)),false,t.receiver));
    queue->addUnique(priorityImmediate,queueItem(funcScopeEdge,QVariant::fromValue<uchar>(val),false,t.receiver));
    queue->addUnique(priorityImmediate,queueItem(funcScopeMode,QVariant::fromValue<uchar>(1),false,t.receiver));
}

void ReceiverController::setFrequencyA(quint64 f, bool u)
{
    if (frequencyA != f)
    {
        frequencyA = f;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            f = roundFrequency(f,stepSize);
            freqt fr;
            fr.Hz = f;
            fr.MHzDouble = double(f / 1000000.0);
            queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(fr),false,t.receiver));
        } else {
            emit frequencyAChanged();
        }
    }
}

void ReceiverController::setFrequencyB(quint64 f, bool u)
{
    if (frequencyB != f)
    {
        frequencyB = f;
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoB,receiver,true);
            f = roundFrequency(f,stepSize);
            freqt fr;
            fr.Hz = f;
            fr.MHzDouble = double(f / 1000000.0);
            queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(fr),false,t.receiver));
        } else {
            emit frequencyBChanged();
        }
    }
}


void ReceiverController::buildUiSpecs()
{
    // Build UI Specs
    if (rigCaps == nullptr)
    {
        return;
    }

    QVariantList values;
    values.clear();
    for (auto &sm : rigCaps->scopeModes) {
        values.append(QVariantMap{
            {"text",  sm.name},
            {"value", sm.num}
        });
    }
    uiSpecs["scopeModes"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    for (auto &cs : rigCaps->scopeCenterSpans) {
        values.append(QVariantMap{
            {"text",  cs.name},
            {"value", QVariant::fromValue(cs)}
        });
    }
    uiSpecs["scopeSpans"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    auto it = rigCaps->commands.find(funcScopeEdge);
    values.clear();
    if (it != rigCaps->commands.end())
    {
        for (int i=it->minVal; i<=it->maxVal; i++) {
            values.append(QVariantMap{
                {"text",  QString("Fixed Edge %0").arg(i)},
                {"value", QVariant::fromValue<uchar>(i)}
            });
        }
    }
    uiSpecs["scopeEdges"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    for (auto &m : rigCaps->modes) {
        values.append(QVariantMap{
            {"text",  m.name},
            {"value", QVariant::fromValue(m)}
        });
    }
    uiSpecs["modes"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    auto addDataMode = [&](QString text, int value) {
        values.append(QVariantMap{{"text", text}, {"value", value}});
    };
    addDataMode("Data Off", 0);
    if (rigCaps->commands.contains(funcDATA2Mod)) {
        addDataMode("Data 1", 1);
        addDataMode("Data 2", 2);
    } else if (rigCaps->commands.contains(funcDATA1Mod)) {
        addDataMode("Data On", 1);
    }
    if (rigCaps->commands.contains(funcDATA3Mod)) {
        addDataMode("Data 3", 3);
    }
    uiSpecs["dataModes"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    for (auto &f : rigCaps->filters) {
        values.append(QVariantMap{
            {"text",  f.name},
            {"value", f.num}
        });
    }
    uiSpecs["filters"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    for (auto &r : rigCaps->roofing) {
        values.append(QVariantMap{
            {"text",  r.name},
            {"value", r.num}
        });
    }
    uiSpecs["roofingFilters"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    if (rigCaps->commands.contains(funcFilterShape))
    {
        auto addFilterShape = [&](QString text, uchar value) {
            values.append(QVariantMap{{"text", text}, {"value", value}});
        };
        addFilterShape("Sharp",0);
        if (rigCaps->manufacturer == manufKenwood)
        {
            addFilterShape("Medium",1);
            addFilterShape("Soft",2);

        } else {
            addFilterShape("Soft",1);
        }
    }
    uiSpecs["filterShapes"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    values.append(QVariantMap{{"text", "Fast"}, {"value", uchar(0)}});
    values.append(QVariantMap{{"text", "Mid"}, {"value", uchar(1)}});
    values.append(QVariantMap{{"text", "Slow"}, {"value", uchar(2)}});
    uiSpecs["speeds"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };

    values.clear();
    auto addTheme = [&](QString text, WaterfallItem::Theme value) {
        values.append(QVariantMap{{"text", text}, {"value", value}});
    };
    addTheme("Jet", WaterfallItem::Theme::Jet);
    addTheme("Cold", WaterfallItem::Theme::Cold);
    addTheme("Hot", WaterfallItem::Theme::Hot);
    addTheme("Therm", WaterfallItem::Theme::Thermal);
    addTheme("Night", WaterfallItem::Theme::Night);
    addTheme("Ion", WaterfallItem::Theme::Ion);
    addTheme("Gray", WaterfallItem::Theme::Grayscale);
    addTheme("Geo", WaterfallItem::Theme::Geography);
    addTheme("Hues", WaterfallItem::Theme::Hues);
    addTheme("Polar", WaterfallItem::Theme::Polar);
    addTheme("Spect", WaterfallItem::Theme::Spectrum);
    addTheme("Candy", WaterfallItem::Theme::Candy);
    uiSpecs["themes"] = QVariantMap{
        {"textRole","text"},
        {"valueRole","value"},
        {"defaultIndex", -1},
        {"model", values},
        {"visible",!values.empty()}
    };


    if (rigCaps->commands.contains(funcScopeRef))
    {
        funcType func = rigCaps->commands.find(funcScopeRef).value();
        uiSpecs["refSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }

    if (rigCaps->commands.contains(funcNBLevel))
    {
        funcType func = rigCaps->commands.find(funcNBLevel).value();
        uiSpecs["nbSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }

    if (rigCaps->commands.contains(funcNRLevel))
    {
        funcType func = rigCaps->commands.find(funcNRLevel).value();
        uiSpecs["nrSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }


    if (rigCaps->commands.contains(funcRfGain))
    {
        funcType func = rigCaps->commands.find(funcRfGain).value();
        uiSpecs["rfGainSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }

    if (rigCaps->commands.contains(funcAfGain))
    {
        funcType func = rigCaps->commands.find(funcAfGain).value();
        uiSpecs["afGainSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }


    if (rigCaps->commands.contains(funcSquelch))
    {
        funcType func = rigCaps->commands.find(funcSquelch).value();
        uiSpecs["squelchSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }

    if (rigCaps->commands.contains(funcAttenuator))
    {
        // Find the step size between attenuator values
        int step = 0;

        if (rigCaps->attenuators.size() < 2)
        {
            step = 1;
        } else {
            for (int i = 1; i < rigCaps->attenuators.size(); ++i) {
                const int delta = rigCaps->attenuators[i].num - rigCaps->attenuators[i - 1].num;
                if (delta <= 0)
                    continue;
                step = (step == 0) ? delta : std::gcd(step, delta);
            }
        }

        funcType func = rigCaps->commands.find(funcAttenuator).value();
        uiSpecs["attenuator"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"step",step},
            {"unit","dB"},
            {"visible",true}
        };
    }

    if (rigCaps->commands.contains(funcPreamp))
    {
        int step = 0;
        if (rigCaps->preamps.size() < 2)
        {
            step = 1;
        } else {
            for (int i = 1; i < rigCaps->preamps.size(); ++i) {
                const int delta = rigCaps->preamps[i].num - rigCaps->preamps[i - 1].num;
                if (delta <= 0)
                    continue;
                step = (step == 0) ? delta : std::gcd(step, delta);
            }
        }

        funcType func = rigCaps->commands.find(funcPreamp).value();
        uiSpecs["preamp"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"step",step},
            {"unit",""},
            {"visible",true}
        };
    }


    const auto mkAntennaOptions = [](const std::vector<genericType> &ants) -> QVariantList {
        QVariantList opts;
        opts.reserve(static_cast<int>(ants.size()));

        for (const auto &a : ants) {
            opts.append(QVariantMap{
                {"text", a.name},
                {"value", a.num}
            });
        }
        return opts;
    };

    // usage
    uiSpecs["antenna"] = QVariantMap{
        {"visible", true},
        {"options", mkAntennaOptions(rigCaps->antennas)}
    };


    emit uiSpecsChanged();


    flags |= ShowVFOA | (rigCaps->numVFO > 1 ? ShowVFOB : ShowNothing);
    if (flags & ShowVFOB) {
        flags |= ShowVFOButton;
        flags |= rigCaps->commands.contains(funcVFOSwapAB) ? ShowSwapABButton : ShowNothing;
        flags |= rigCaps->commands.contains(funcVFOEqualAB) ? ShowEqualsABButton : ShowNothing;
        flags |= rigCaps->commands.contains(funcMemoryMode) ? ShowVMButton : ShowNothing;
        flags |= rigCaps->commands.contains(funcSatelliteMode) ? ShowSatButton : ShowNothing;
        flags |= rigCaps->commands.contains(funcSplitStatus) ? ShowSplitButton : ShowNothing;
    }
    drawerTitle = QString("%0 Receiver settings").arg((receiver==0)?"Main":"Sub");
    setTitle((receiver==0) ? "Main":"Sub");

    emit uiFlagsChanged();
    emit drawerTitleChanged();
    emit uiSpecsChanged();
}

void ReceiverController::receiveRigCaps(rigCapabilities* caps)
{
    rigCaps = caps;

    flags |= ShowVFOA | (rigCaps->numVFO > 1 ? ShowVFOB:ShowNothing);

    emit uiFlagsChanged();

    setTitle((receiver==0) ? "Main":"Sub");

    if (!receiver)
        setActive(true);

    buildUiSpecs();

    /*
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
    */
}


void ReceiverController::setBandIndicators(bool show, QString region, std::vector<bandType>* bands)
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
    }
    emit activeBandsChanged();
}


quint64 ReceiverController::roundFrequency(quint64 frequency, unsigned int tsHz)
{
    return roundFrequency(frequency, 0, tsHz);
}

quint64 ReceiverController::roundFrequency(quint64 frequency, int steps, unsigned int tsHz)
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
