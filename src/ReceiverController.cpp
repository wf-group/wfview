#include "ReceiverController.h"

#include "logcategories.h"
#include <QGuiApplication>
#include <QStyleHints>
#include <QTimer>
#include <cmath>
#include <limits>


ReceiverController::ReceiverController(int rxIndex, QString region, QObject *parent)
    : QObject(parent), region(region), receiver(rxIndex)
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


    if (!d.valid || d.data.isEmpty())
        return;

    if (!qFuzzyCompare(lowerFreq, d.startFreq) || !qFuzzyCompare(upperFreq, d.endFreq)) {
        lowerFreq = d.startFreq;
        upperFreq = d.endFreq;
    }

    updatePassband();

    lastScope = d;               // QByteArray is implicitly shared (cheap copy)
    emit scopeUpdated(lastScope);    // fan out to Spectrum/Waterfall
}

void ReceiverController::setClusterSpots(const QList<spotData> &newSpots)
{
    QVector<spotData> out;
    out.reserve(newSpots.size());
    for (const auto &spot : newSpots)
        out.append(spot);
    emit clusterSpotsUpdated(out);
}

void ReceiverController::clearClusterSpots()
{
    emit clusterSpotsUpdated({});
}

void ReceiverController::applyModeSpecificPeriodicCommands(const modeInfo &mode)
{
    if (!rigCaps)
        return;

    const QString modeName = mode.name.trimmed().toUpper();

    for (const auto &periodic : std::as_const(rigCaps->periodic)) {
        if (periodic.modes.isEmpty())
            continue;

        const bool allReceivers = periodic.receiver == char(-1);
        const uchar periodicReceiver = allReceivers ? receiver : uchar(periodic.receiver);
        if (!allReceivers && periodicReceiver != receiver)
            continue;

        if (periodic.modes.contains(modeName))
            queue->addUnique(queuePriority(periodic.prioVal), periodic.func, true, periodicReceiver);
        else {
            qDebug(logSystem()) << "Removing mode-specific periodic command" << funcString[periodic.func]
                                << "for mode" << modeName << "receiver" << periodicReceiver;
            queue->del(periodic.func, periodicReceiver, true);
        }
    }
}

bool ReceiverController::periodicCommandAllowedByMode(funcs func, uchar receiver, const modeInfo &mode) const
{
    if (!rigCaps)
        return true;

    return periodicCommandAllowedForMode(rigCaps->periodic, func, receiver, mode.name);
}

void ReceiverController::updateModeSensitiveUiSpecs()
{
    bool changed = false;
    for (auto it = uiSpecs.begin(); it != uiSpecs.end(); ++it) {
        QVariantMap spec = it.value().toMap();
        if (!spec.contains("command"))
            continue;

        const funcs func = funcs(spec.value("command").toInt());
        const bool enabled = periodicCommandAllowedByMode(func, receiver, mode);
        if (spec.value("enabled", true).toBool() == enabled)
            continue;

        spec["enabled"] = enabled;
        it.value() = spec;
        changed = true;
    }

    if (changed)
        emit uiSpecsChanged();
}

void ReceiverController::updatePassband()
{
    if (frequencyA == 0 || passbandWidth <= 0.0)
        return;

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
        updatePbt();
    }
}

int ReceiverController::pbtDefaultValue(const funcType &func) const
{
    return pbtOffsetToRegister(pbtNeutralOffsetMHz(), func);
}

bool ReceiverController::pbtLowerSideMode() const
{
    return mode.mk == modeLSB || mode.mk == modeCW || mode.mk == modeRTTY;
}

double ReceiverController::pbtNeutralOffsetMHz() const
{
    if ((mode.mk == modeCW || mode.mk == modeCW_R) && passbandWidth > 0.0006)
        return std::round((passbandWidth - (double(cwPitch) / 1000000.0)) * 200000.0) / 200000.0;

    return 0.0;
}

double ReceiverController::pbtPitchCompensationMHz() const
{
    if ((mode.mk == modeCW || mode.mk == modeCW_R) && passbandWidth > 0.0006)
        return (600.0 - double(cwPitch)) / 1000000.0;

    return 0.0;
}

double ReceiverController::pbtRegisterToOffsetMHz(int value) const
{
    if (passbandWidth <= 0.0)
        return 0.0;

    const double shift = double(value - 128);
    const double roundedShift = std::ceil((shift / 127.0) * passbandWidth * 20000.0) / 20000.0;
    return std::round((roundedShift + pbtPitchCompensationMHz()) * 200000.0) / 200000.0;
}

int ReceiverController::pbtOffsetToRegister(double offsetMHz, const funcType &func) const
{
    if (passbandWidth <= 0.0)
        return qBound(int(func.minVal), 128, int(func.maxVal));

    int bestValue = qBound(int(func.minVal), 128, int(func.maxVal));
    double bestError = std::numeric_limits<double>::max();
    for (int value = int(func.minVal); value <= int(func.maxVal); ++value) {
        const double error = qAbs(pbtRegisterToOffsetMHz(value) - offsetMHz);
        if (error < bestError) {
            bestError = error;
            bestValue = value;
        }
    }

    return bestValue;
}

bool ReceiverController::pbtOffsetIsNeutral(double offsetMHz) const
{
    if (passbandWidth <= 0.0)
        return true;

    const double halfRegisterStepMHz = (passbandWidth / 127.0) / 2.0;
    const double minimumToleranceMHz = 0.000005;
    return qAbs(offsetMHz - pbtNeutralOffsetMHz()) <= qMax(halfRegisterStepMHz, minimumToleranceMHz);
}

void ReceiverController::updatePbt()
{
    double newLow = 0.0;
    double newHigh = 0.0;

    if (!pbtOverlaySuppressed &&
        rigCaps && passbandWidth > 0.0 && passbandHigh > passbandLow &&
        mode.mk != modeFM && mode.mk != modeWFM &&
        rigCaps->commands.contains(funcPBTInner) &&
        rigCaps->commands.contains(funcPBTOuter)) {
        const int innerValue = pbtInner == 0 ? pbtDefaultValue(rigCaps->commands.value(funcPBTInner)) : pbtInner;
        const int outerValue = pbtOuter == 0 ? pbtDefaultValue(rigCaps->commands.value(funcPBTOuter)) : pbtOuter;
        const double neutralOffset = pbtNeutralOffsetMHz();
        const double innerOffset = pbtRegisterToOffsetMHz(innerValue);
        const double outerOffset = pbtRegisterToOffsetMHz(outerValue);
        const double innerDelta = innerOffset - neutralOffset;
        const double outerDelta = outerOffset - neutralOffset;
        const bool lowerSide = pbtLowerSideMode();
        const double innerLow = passbandLow + (lowerSide ? -innerDelta / 2.0 : innerDelta / 2.0);
        const double outerLow = passbandLow + (lowerSide ? -outerDelta / 2.0 : outerDelta / 2.0);

        if (!pbtOffsetIsNeutral(innerOffset) || !pbtOffsetIsNeutral(outerOffset)) {
            newLow = qMax(innerLow, outerLow);
            newHigh = qMin(innerLow + passbandWidth, outerLow + passbandWidth);
            if (newHigh <= newLow) {
                newLow = 0.0;
                newHigh = 0.0;
            }
        }
    }

    if (!qFuzzyCompare(pbtLow, newLow) || !qFuzzyCompare(pbtHigh, newHigh)) {
        pbtLow = newLow;
        pbtHigh = newHigh;
        emit pbtChanged();
    }
}

void ReceiverController::suppressPbtOverlay(int milliseconds)
{
    pbtOverlaySuppressed = true;
    ++pbtSuppressionGeneration;
    const quint64 generation = pbtSuppressionGeneration;

    if (pbtLow != 0.0 || pbtHigh != 0.0) {
        pbtLow = 0.0;
        pbtHigh = 0.0;
        emit pbtChanged();
    }

    QTimer::singleShot(milliseconds, this, [this, generation]() {
        if (generation != pbtSuppressionGeneration)
            return;

        pbtOverlaySuppressed = false;
        updatePbt();
    });
}

void ReceiverController::updatePassbandModeParameters(const modeInfo &m)
{
    if (m.mk == modeUnknown)
        return;

    const double previousPassbandWidth = passbandWidth;
    const double previousPassbandCenter = passbandCenterFrequency;
    passbandCenterFrequency = 0.0;
    if (m.pass > 0)
        passbandWidth = double(m.pass / 1000000.0);

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
        break;
    case modeAM:
        break;
    case modeCW:
    case modeCW_R:
        passbandCenterFrequency = double(cwPitch) / 2000000.0;
        break;
    default:
        break;
    }

    if (!qFuzzyCompare(previousPassbandWidth, passbandWidth) ||
        !qFuzzyCompare(previousPassbandCenter, passbandCenterFrequency)) {
        suppressPbtOverlay();
    }

    updatePassband();
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

    const qreal stepsToScroll = QGuiApplication::styleHints()->wheelScrollLines() * steps;

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

    tuneSteps(clicks, modifiers);
    scrollWheelOffsetAccumulated = 0.0;
}

void ReceiverController::tuneSteps(int steps, int modifiers, bool uniqueQueue)
{
    if (steps == 0)
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
    f.Hz = roundFrequency(frequencyA, steps, stepsHz);
    f.MHzDouble = f.Hz / 1E6;

    if (f.Hz == 0)
        return;

    setFrequencyA(f.Hz, false);
    queueItem item(t.freqFunc, QVariant::fromValue<freqt>(f), false, t.receiver);
    if (uniqueQueue) {
        queue->addUnique(priorityImmediate, item);
    } else {
        queue->add(priorityImmediate, item);
    }
}

void ReceiverController::resizePassband(double lowFreqMHz, double highFreqMHz)
{
    if (mode.bwMin <= 0 || mode.bwMax <= 0)
        return;

    const double widthMHz = qAbs(highFreqMHz - lowFreqMHz);
    if (widthMHz <= 0.0)
        return;

    int widthHz = qRound(widthMHz * 1000000.0);
    widthHz = qBound(mode.bwMin, widthHz, mode.bwMax);

    if (filterWidth == widthHz)
        return;

    suppressPbtOverlay();
    setFilterWidth(widthHz, true);
}

void ReceiverController::dragPbt(int action, double deltaMHz)
{
    if (!rigCaps || passbandWidth <= 0.0 ||
        !rigCaps->commands.contains(funcPBTInner) ||
        !rigCaps->commands.contains(funcPBTOuter))
        return;

    const funcType inner = rigCaps->commands.value(funcPBTInner);
    const funcType outer = rigCaps->commands.value(funcPBTOuter);
    const int innerStart = pbtInner == 0 ? pbtDefaultValue(inner) : pbtInner;
    const int outerStart = pbtOuter == 0 ? pbtDefaultValue(outer) : pbtOuter;
    const double direction = pbtLowerSideMode() ? -1.0 : 1.0;
    const int delta = qRound((direction * deltaMHz / passbandWidth) * 127.0);

    if (delta == 0)
        return;

    if (action == 2) {
        const int minDelta = qMax(int(inner.minVal) - innerStart, int(outer.minVal) - outerStart);
        const int maxDelta = qMin(int(inner.maxVal) - innerStart, int(outer.maxVal) - outerStart);
        const int boundedDelta = qBound(minDelta, delta, maxDelta);
        if (boundedDelta == 0)
            return;

        setPbtInner(innerStart + boundedDelta, true);
        setPbtOuter(outerStart + boundedDelta, true);
        return;
    }

    if (action == 0) {
        const int next = qBound(int(inner.minVal), innerStart + delta, int(inner.maxVal));
        setPbtInner(next, true);
    }
    if (action == 1) {
        const int next = qBound(int(outer.minVal), outerStart + delta, int(outer.maxVal));
        setPbtOuter(next, true);
    }
}

void ReceiverController::resetPbt()
{
    if (!rigCaps || !rigCaps->commands.contains(funcPBTInner) || !rigCaps->commands.contains(funcPBTOuter))
        return;

    const funcType inner = rigCaps->commands.value(funcPBTInner);
    const funcType outer = rigCaps->commands.value(funcPBTOuter);
    const int innerDefault = pbtDefaultValue(inner);
    const int outerDefault = pbtDefaultValue(outer);

    setPbtInner(innerDefault, true);
    setPbtOuter(outerDefault, true);
}

void ReceiverController::selectVfoB(bool enabled)
{
    setMemoryMode(false, false);

    const vfo_t target = enabled ? vfoB : vfoA;
    vfoCommandType t = queue->getVfoCommand(target, receiver, false);

    queue->add(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(target), false, t.receiver));
    queue->add(priorityHighest, t.freqFunc, false, t.receiver);
    queue->add(priorityHighest, t.modeFunc, false, t.receiver);

    t = queue->getVfoCommand(enabled ? vfoA : vfoB, receiver, false);
    queue->add(priorityHighest, t.freqFunc, false, t.receiver);
    queue->add(priorityHighest, t.modeFunc, false, t.receiver);

    if (m_vfoBSelected != enabled) {
        m_vfoBSelected = enabled;
        emit vfoBSelectedChanged();
    }
}

void ReceiverController::swapVfoAB()
{
    if (!rigCaps || !rigCaps->commands.contains(funcVFOSwapAB))
        return;

    queue->add(priorityImmediate, funcVFOSwapAB, false, receiver);

    vfoCommandType t = queue->getVfoCommand(vfoA, receiver, false);
    queue->add(priorityHighest, t.modeFunc, false, t.receiver);
    queue->add(priorityHighest, t.freqFunc, false, t.receiver);

    t = queue->getVfoCommand(vfoB, receiver, false);
    queue->add(priorityHighest, t.modeFunc, false, t.receiver);
    queue->add(priorityHighest, t.freqFunc, false, t.receiver);
}

void ReceiverController::equalizeVfoAB()
{
    if (!rigCaps || !rigCaps->commands.contains(funcVFOEqualAB))
        return;

    vfoCommandType t = queue->getVfoCommand(vfoB, receiver, false);
    queue->add(priorityImmediate, funcVFOEqualAB, false, uchar(0));
    queue->add(priorityHighest, t.modeFunc, false, t.receiver);
    queue->add(priorityHighest, t.freqFunc, false, t.receiver);
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
        emit scopeModeChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeMode,QVariant::fromValue(scopeMode),false,t.receiver));
        }
    }
}

void ReceiverController::setScopeSpan(uchar m, bool u)
{
    if (scopeSpan.reg != m)
    {
        auto it = std::find_if(rigCaps->scopeCenterSpans.begin(), rigCaps->scopeCenterSpans.end(),
                               [&](const centerSpanData &cs) {
                                   return cs.reg == m;
                               });

        if (it != rigCaps->scopeCenterSpans.end()) {
            scopeSpan = *it;
            emit scopeSpanChanged();
            if (u) {
                vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
                queue->addUnique(priorityImmediate,queueItem(funcScopeSpan,QVariant::fromValue(scopeSpan),false,t.receiver));
            }
        }
    }
}

void ReceiverController::setScopeEdge(uchar m, bool u)
{
    if (scopeEdge != m)
    {
        scopeEdge = m;
        emit scopeEdgeChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeEdge,QVariant::fromValue<uchar>(scopeEdge),false,t.receiver));
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

    if (m.mk == modeUnknown)
        return;

    modeInfo confirmed = m;
    if (confirmed.filter == 0xff)
        confirmed.filter = mode.filter;
    if (confirmed.data == 0xff)
        confirmed.data = mode.data;

    const bool modeValueChanged = mode.mk != confirmed.mk;
    const bool filterValueChanged = confirmed.filter != 0xff && mode.filter != confirmed.filter;
    const bool dataValueChanged = confirmed.data != 0xff && mode.data != confirmed.data;

    if (modeValueChanged || filterValueChanged || dataValueChanged) {
        qInfo(logSystem()) << __func__ << QString("Received mode update for %0 (%1): %2 (%3) filter:%4 data:%5")
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

        if (modeValueChanged) {
            // We have changed mode so "may" need to change regular commands.
            updatePassbandModeParameters(confirmed);

            if (confirmed.bwMin <= 0 && confirmed.bwMax <= 0 && confirmed.pass > 0)
                passbandWidth = double(confirmed.pass / 1000000.0);

            applyModeSpecificPeriodicCommands(confirmed);
        }

        mode = confirmed;
        updateModeSensitiveUiSpecs();
        if (modeValueChanged)
            emit modeChanged();
        if (filterValueChanged)
            emit filterChanged();
        if (dataValueChanged)
            emit dataModeChanged();
    } else if (confirmed.pass > 0) {
        mode.pass = confirmed.pass;
        updatePassbandModeParameters(mode);
    }
}


void ReceiverController::setMode(uchar m, bool u)
{
    if (!u || mode.mk == m)
        return;

    auto it = std::find_if(rigCaps->modes.begin(), rigCaps->modes.end(),
                           [&](const modeInfo &md) {
                               return md.mk == m;
                           });

    if (it == rigCaps->modes.end())
        return;

    modeInfo requested = *it;
    requested.data = mode.data;
    requested.filter = mode.filter;

    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(requested),false,t.receiver));
    if (t.modeFunc == funcModeSet)
        queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(requested),false,t.receiver));
    if (rigCaps->manufacturer == manufIcom)
        queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
}


void ReceiverController::setDataMode(uchar m, bool u)
{
    if (!u || mode.data == m)
        return;

    modeInfo requested = mode;
    requested.data = m;
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(requested),false,t.receiver));
    if (t.modeFunc == funcModeSet)
        queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(requested),false,t.receiver));
    if (rigCaps->manufacturer == manufIcom)
        queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
}


void ReceiverController::setFilter(uchar m, bool u)
{
    if (!u || mode.filter == m)
        return;

    modeInfo requested = mode;
    requested.filter = m;
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    queue->addUnique(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(requested),false,t.receiver));
    if (t.modeFunc == funcModeSet)
        queue->addUnique(priorityImmediate,queueItem(funcDataModeWithFilter,QVariant::fromValue(requested),false,t.receiver));
    if (rigCaps->manufacturer == manufIcom)
        queue->addUnique(priorityHighest,funcRoofingFilter,false,t.receiver);
}

void ReceiverController::setFilterShape(uchar m, bool u)
{
    if (filterShape != m)
    {
        filterShape = m;
        emit filterShapeChanged();
        if (u) {
            if (!periodicCommandAllowedByMode(funcFilterShape, receiver, mode))
                return;

            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            uchar f = uchar(m + (mode.filter * 10));
            queue->addUnique(priorityImmediate,queueItem(funcFilterShape,QVariant::fromValue<uchar>(f),false,t.receiver));
        }
    }
}
void ReceiverController::setRoofing(uchar m, bool u)
{
    if (roofing != m)
    {
        roofing = m;
        emit filterShapeChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            uchar f = uchar(m + (mode.filter * 10));
            queue->addUnique(priorityImmediate,queueItem(funcRoofingFilter,QVariant::fromValue<uchar>(f),false,t.receiver));
        }
    }
}

void ReceiverController::setSpeed(uchar m, bool u)
{
    if (speed != m)
    {
        speed = m;
        emit speedChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeSpeed,QVariant::fromValue(m),false,t.receiver));
        }
    }
}

void ReceiverController::setTheme(WaterfallItem::Theme m, bool u)
{

    qInfo() << "Received new theme: " << m << "from user:" << u;
    if (theme != m)
    {
        theme = m;
        emit themeChanged();
    }
}

void ReceiverController::setWaterfallLength(int v)
{
    v = qBound(1, v, 4096);
    if (waterfallLength == v)
        return;

    waterfallLength = v;
    emit waterfallLengthChanged();
}

void ReceiverController::setPlotFloor(int v)
{
    v = qBound(0, v, 255);
    if (plotFloor == v)
        return;

    plotFloor = v;
    emit plotScaleChanged();
}

void ReceiverController::setPlotCeiling(int v)
{
    v = qBound(0, v, 255);
    if (plotCeiling == v)
        return;

    plotCeiling = v;
    emit plotScaleChanged();
}

void ReceiverController::setWaterfallSmooth(bool v)
{
    if (waterfallSmooth == v)
        return;

    waterfallSmooth = v;
    emit waterfallSmoothChanged();
}

void ReceiverController::setWaterfallAntiAlias(bool v)
{
    if (waterfallAntiAlias == v)
        return;

    waterfallAntiAlias = v;
    emit waterfallAntiAliasChanged();
}

void ReceiverController::setScopeDisplaySettings(int floor, int ceiling, int length,
                                                 WaterfallItem::Theme newTheme,
                                                 bool smooth, bool antiAlias)
{
    setPlotFloor(floor);
    setPlotCeiling(ceiling);
    setWaterfallLength(length);
    setTheme(newTheme, false);
    setWaterfallSmooth(smooth);
    setWaterfallAntiAlias(antiAlias);
}

void ReceiverController::setHold(bool v, bool u)
{
    if (hold != v)
    {
        hold = v;
        emit holdChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->add(priorityImmediate,queueItem(funcScopeHold,QVariant::fromValue<bool>(v),false,t.receiver));
        }
    }
}

void ReceiverController::setRef(int v, bool u)
{
    if (ref != v)
    {
        ref = v; // Step size is 5 so no need to round up/down.
        emit refChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcScopeRef,QVariant::fromValue(v),false,t.receiver));
        }
    }
}

void ReceiverController::setRfGain(ushort v, bool u)
{
    if (rfGain != v)
    {
        rfGain = v;
        emit rfGainChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcRfGain,QVariant::fromValue(v),false,t.receiver));
        }
    }
}


void ReceiverController::setAfGain(ushort v, bool u)
{
    if (afGain != v)
    {
        afGain = v;
        emit afGainChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAfGain,QVariant::fromValue(v),false,t.receiver));
        }
    }
}


void ReceiverController::setSquelch(ushort v, bool u)
{
    if (squelch != v)
    {
        squelch = v;
        emit squelchChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcSquelch,QVariant::fromValue(v),false,t.receiver));
        }
    }
}

void ReceiverController::setAttenuator(uchar v, bool u)
{
    if (attenuator != v)
    {
        attenuator = v;
        emit attenuatorChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAttenuator,QVariant::fromValue(attenuator),false,t.receiver));
        }
    }
}

void ReceiverController::setPreamp(uchar v, bool u)
{
    if (preamp != v)
    {
        preamp = v;
        emit preampChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPreamp,QVariant::fromValue(preamp),false,t.receiver));
        }
    }
}

void ReceiverController::setNr(uchar v, bool u)
{
    if (nr != v)
    {
        nr = v;
        emit nrChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNoiseReduction,QVariant::fromValue(nr),false,t.receiver));
        }
    }
}

void ReceiverController::setNrLevel(ushort v, bool u)
{
    if (nrLevel != v)
    {
        nrLevel = v;
        emit nrLevelChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNRLevel,QVariant::fromValue(nrLevel),false,t.receiver));
        }
    }
}

void ReceiverController::setNb(uchar v, bool u)
{
    if (nb != v)
    {
        nb = v;
        emit nbChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNoiseBlanker,QVariant::fromValue(nb),false,t.receiver));
        }
    }
}

void ReceiverController::setNbLevel(ushort v, bool u)
{
    if (nbLevel != v)
    {
        nbLevel = v;
        emit nbLevelChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcNBLevel,QVariant::fromValue(nbLevel),false,t.receiver));
        }
    }
}

void ReceiverController::setIpPlus(bool v, bool u)
{
    if (ipPlus != v)
    {
        ipPlus = v;
        emit ipPlusChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcIPPlus,QVariant::fromValue(ipPlus),false,t.receiver));
        }
    }
}

void ReceiverController::setMn(bool v, bool u)
{
    if (mn != v)
    {
        mn = v;
        emit mnChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcManualNotch,QVariant::fromValue(mn),false,t.receiver));
        }
    }
}

void ReceiverController::setDs(bool v, bool u)
{
    if (ds != v)
    {
        ds = v;
        emit dsChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcDigiSel,QVariant::fromValue(ds),false,t.receiver));
        }
    }
}


void ReceiverController::setAntenna(uchar v, bool u)
{
    if (antenna.antenna != v)
    {
        antenna.antenna = v;
        emit antennaChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue(antenna),false,t.receiver));
        }
    }
}


void ReceiverController::setRxAntenna(bool v, bool u)
{
    if (antenna.rx != v)
    {
        antenna.rx = v;
        emit rxAntennaChanged();

        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue(antenna),false,t.receiver));
        }
    }
}


void ReceiverController::setPbtInner(int v, bool u)
{
    if (pbtInner != v)
    {
        pbtInner = v;
        emit pbtInnerChanged();
        updatePbt();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPBTInner,QVariant::fromValue<ushort>(v),false,t.receiver));
        }
    }
}

void ReceiverController::setPbtOuter(int v, bool u)
{
    if (pbtOuter != v)
    {
        pbtOuter = v;
        emit pbtOuterChanged();
        updatePbt();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcPBTOuter,QVariant::fromValue<ushort>(v),false,t.receiver));
        }
    }
}

void ReceiverController::setIfShift(int v, bool u)
{
    if (ifShift != v)
    {
        ifShift = v;
        emit ifShiftChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcIFShift,QVariant::fromValue<ushort>(v),false,t.receiver));
        }
    }
}

void ReceiverController::setFilterWidth(int v, bool u)
{
    qInfo() << "*** FILTER WIDTH" << v << "user" <<u;
    if (filterWidth != v)
    {
        filterWidth = v;
        emit filterWidthChanged();
        setPassbandWidth(v);
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            queue->addUnique(priorityImmediate,queueItem(funcFilterWidth,QVariant::fromValue<ushort>(v),false,t.receiver));
        }
    }
}

void ReceiverController::receiveCwPitch(quint16 pitch)
{
    if (pitch == cwPitch)
        return;

    cwPitch = pitch;
    if (mode.mk == modeCW || mode.mk == modeCW_R) {
        passbandCenterFrequency = double(cwPitch) / 2000000.0;
        updatePassband();
        updatePbt();
    }
}

void ReceiverController::toFixedEdge(uchar val)
{
    qInfo() << "Setting new fixed edge:" << val;
    vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
    if (upperFreq > lowerFreq) {
        queue->addUnique(priorityImmediate,queueItem(funcScopeFixedEdgeFreq,QVariant::fromValue(spectrumBounds(lowerFreq, upperFreq, val)),false,t.receiver));
    }
    queue->addUnique(priorityImmediate,queueItem(funcScopeEdge,QVariant::fromValue<uchar>(val),false,t.receiver));
    queue->addUnique(priorityImmediate,queueItem(funcScopeMode,QVariant::fromValue<uchar>(1),false,t.receiver));
}

void ReceiverController::setFrequencyA(quint64 f, bool u)
{
    if (frequencyA != f)
    {
        frequencyA = f;
        emit frequencyAChanged();
        updatePassband();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoA,receiver,true);
            f = roundFrequency(f,stepSize);
            freqt fr;
            fr.Hz = f;
            fr.MHzDouble = double(f / 1000000.0);
            queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(fr),false,t.receiver));
        }

        if (f < band.lowFreq || f > band.highFreq || band.band == bandGen)
        {
            // Band may have changed but we don't know how, so set band to unknown
            // It will be checked below and updated with correct band information.
            // We need to do this every time if bandGen.
            band.band = bandUnknown;
        }

        if (band.band == bandUnknown) {

            auto it = std::find_if(rigCaps->bands.begin(), rigCaps->bands.end(),
                                   [&](const bandType &bt) {
                                       return (frequencyA >= bt.lowFreq && frequencyA <= bt.highFreq &&
                                            (bt.region == "" || bt.region == region));
                                   });

            if (it != rigCaps->bands.end()) {
                const bandType &b = *it;
                if (b.band != band.band)
                {
                    band = b;
                    emit bandChanged();
                }
            }
        }
    }
}

void ReceiverController::setFrequencyB(quint64 f, bool u)
{
    if (frequencyB != f)
    {
        frequencyB = f;
        emit frequencyBChanged();
        if (u) {
            vfoCommandType t = queue->getVfoCommand(vfoB,receiver,true);
            f = roundFrequency(f,stepSize);
            freqt fr;
            fr.Hz = f;
            fr.MHzDouble = double(f / 1000000.0);
            queue->addUnique(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(fr),false,t.receiver));
        }
    }
}

void ReceiverController::setMemoryMode(bool enabled, bool u)
{
    if (m_memoryMode != enabled) {
        m_memoryMode = enabled;
        emit memoryModeChanged();
    }

    if (!u)
        return;

    const vfo_t target = enabled ? vfoMem : vfoA;
    vfoCommandType t = queue->getVfoCommand(target, receiver, false);

    queue->add(priorityImmediate, queueItem(funcSelectVFO, QVariant::fromValue<vfo_t>(target), false, t.receiver));
    queue->add(priorityHighest, t.freqFunc, false, t.receiver);
    queue->add(priorityHighest, t.modeFunc, false, t.receiver);

    if (enabled && m_vfoBSelected) {
        m_vfoBSelected = false;
        emit vfoBSelectedChanged();
    }
}

void ReceiverController::setSatelliteMode(bool enabled, bool u)
{
    if (m_satelliteMode != enabled) {
        m_satelliteMode = enabled;
        emit satelliteModeChanged();
    }

    if (u && rigCaps && rigCaps->commands.contains(funcSatelliteMode))
        queue->add(priorityImmediate, queueItem(funcSatelliteMode, QVariant::fromValue<bool>(enabled), false, uchar(0)));
}

void ReceiverController::setSplitEnabled(bool enabled, bool u)
{
    if (m_splitEnabled != enabled) {
        m_splitEnabled = enabled;
        emit splitEnabledChanged();
    }

    if (u && rigCaps && rigCaps->commands.contains(funcSplitStatus))
        queue->add(priorityImmediate, queueItem(funcSplitStatus, QVariant::fromValue<uchar>(enabled), false, uchar(0)));
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
    for (auto &b : rigCaps->bands) {
        if (b.region == region || b.region.isEmpty()) {
            values.append(QVariantMap{
                {"value",  b.band},
                {"text", b.name}
            });
        }
    }
    uiSpecs["bands"] = QVariantMap{
        {"model", values},
        {"visible",!values.empty()}
    };

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
            {"value", cs.reg}
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
            {"value", m.mk}
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
    const bool hasDataModes = rigCaps->commands.contains(funcDataModeWithFilter) ||
                              rigCaps->commands.contains(funcDataMode);
    if (hasDataModes) {
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
        {"visible",!values.empty()},
        {"command", int(funcFilterShape)},
        {"enabled", periodicCommandAllowedByMode(funcFilterShape, receiver, mode)}
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


    if (rigCaps->commands.contains(funcPBTInner))
    {
        funcType func = rigCaps->commands.find(funcPBTInner).value();
        uiSpecs["pbtInnerSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }

    if (rigCaps->commands.contains(funcPBTOuter))
    {
        funcType func = rigCaps->commands.find(funcPBTOuter).value();
        uiSpecs["pbtOuterSlider"] = QVariantMap{
            {"from",func.minVal},
            {"to",func.maxVal},
            {"visible",true}
        };
    }


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

    this->region = region;

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

void ReceiverController::setBand(availableBands b, bool u)
{
    // Always trigger setBand as BSR may have changed.
    qInfo() << "Got new band:" << b << "in region" << region;
    auto it = std::find_if(rigCaps->bands.begin(), rigCaps->bands.end(),
                           [&](const bandType &bt) {
                               return bt.band == b && (bt.region == region  || bt.region.isEmpty());
                           });

    if (it != rigCaps->bands.end()) {
        // Found
        if (u) {
            const bandType &t = *it;
            band = t;
            if(t.bsr == 0)
            {
                qDebug(logGui()) << "requested to drop to band that does not have a BSR, using direct mode.";
                freqt f;
                f.Hz = (t.lowFreq+t.highFreq)/2.0;
                f.MHzDouble = f.Hz/1000000.0;
                f.VFO = activeVFO;
                vfoCommandType t = queue->getVfoCommand(vfoMain,receiver,true);
                queue->add(priorityImmediate,queueItem(t.freqFunc, QVariant::fromValue<freqt>(f),false,t.receiver));
            } else {
                awaitingBSR = true;
                bsr.band = t.bsr;
                // Need to create an empty bandStackType to stop it trying to set a new one.
                queue->add(priorityImmediate,queueItem(funcBandStackReg,
                                                        QVariant::fromValue<bandStackType>(bandStackType(t.bsr,bsr.reg)),false,uchar(0)));
            }
        }
    } else {
        // Not found not sure what to do here as it can't happen in theory!
        qWarning(logRig()) << "Band" << b << "not found in rigCaps!";
    }
}

void ReceiverController::receiveBSR(bandStackType& b)
{

    if (b.band == bsr.band && awaitingBSR) {
        // We are awaiting a new BSR
        qInfo(logRig()) << __func__ << "BSR received into" << receiver << "Freq:" << bsr.freq.Hz << ", mode: " << bsr.mode << ", filter: " << bsr.filter << ", data mode: " << bsr.data;
        awaitingBSR = false;
        bsr = b;
        vfoCommandType t = queue->getVfoCommand(vfoMain,receiver,true);

        queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(b.freq),false,receiver));

        auto it = std::find_if(rigCaps->modes.begin(), rigCaps->modes.end(),
                   [&](const modeInfo &m) {
                       return m.reg == b.mode;
                   });

        if (it != rigCaps->modes.end()) {
            modeInfo requested = *it;
            requested.filter = b.filter;
            requested.data = b.data;
            qDebug(logRig()) << __func__ << "Requesting Mode/Data for new mode" << requested.name << "data" << requested.data << "filter" << requested.filter << "reg" << requested.reg;
            queue->add(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(requested),false,receiver));
            // We can also set CTCSS here if we want.
        }
    }
}

void ReceiverController::storeBsr()
{
    // First find which band we are in    
    auto it = std::find_if(rigCaps->bands.begin(), rigCaps->bands.end(),
                           [&](const bandType &bt) {
                               return (frequencyA >= bt.lowFreq && frequencyA <= bt.highFreq &&
                                       (bt.region == "" || bt.region == region));
                           });

    if (it != rigCaps->bands.end()) {
        const bandType &b = *it;
        // Current frequency is within this band and it has a BSR
        freqt fr;
        fr.Hz = frequencyA;
        fr.MHzDouble = fr.Hz/1000000.0;
        bsr.band = b.bsr;
        bsr.data = mode.data;
        bsr.freq = fr;
        bsr.filter = mode.filter;
        bsr.mode = mode.reg;
        // If we haven't received a tone yet, use default.
        if (bsr.tone.tone == 0) {
            bsr.tone.tone = 885;
            bsr.tsql.tone = 885;
        }
        queue->add(priorityImmediate,queueItem(funcBandStackReg, QVariant::fromValue<bandStackType>(bsr),false,uchar(0)));
    }
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
