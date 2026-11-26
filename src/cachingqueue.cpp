/*
 *  This is a singleton queue (only 1 instance can be created)
 *
 *  Although subclassing QThread is often not recommended, I think that this is
 *  a valid use case for subclassing.
 *
*/
#include "logcategories.h"
#include "cachingqueue.h"

#include <algorithm>
#include <QHash>

#define CACHE_LOCK_TIME 100

cachingQueue* cachingQueue::instance{};
QMutex cachingQueue::instanceMutex;

std::atomic<qint64> queueItem::nextId{1};

cachingQueue *cachingQueue::getInstance(QObject* parent)
{
    QMutexLocker locker(&instanceMutex);
    if (instance == nullptr)
    {
        // Bind to qApp (parent only used for logging)
        instance = new cachingQueue(qApp);
        instance->setObjectName(("cachingQueue()"));
        connect (instance, SIGNAL(finished()),instance, SLOT(deleteLater()));
        instance->start(QThread::TimeCriticalPriority);
    }
    qDebug() << "Returning instance of cachingQueue() to calling process:" << ((parent != nullptr) ? parent->objectName(): "<unknown>");
    return instance;
}

cachingQueue::~cachingQueue()
{
    qDebug() << "Destroying caching queue (app closing)";

    {
        QMutexLocker locker(&mutex);
        aborted = true;
        waiting.wakeAll();        // wake the thread so it can see aborted and exit
    } // <- mutex MUST be released before wait()

    wait();
}

void cachingQueue::run()
{
    // Setup queue.
    qInfo() << "Starting caching queue handler thread (ThreadId:" << QThread::currentThreadId() << ")";

    QDeadlineTimer deadline(queueInterval);
    QMutexLocker locker(&mutex);
    healthClock.start();

    quint16 counter=priorityImmediate;

    while (!aborted)
    {
        if (!waiting.wait(&mutex, deadline.remainingTime()))
        {
            // Time to process the queue - mutex is locked
            queuePriority prio = priorityImmediate;

            // priorityNone=0, priorityImmediate=1, priorityHighest=2, priorityHigh=3, priorityMediumHigh=5, priorityMedium=7, priorityMediumLow=11, priorityLow=19, priorityLowest=23

            // Immediate commands go first, but not for ever: after
            // maxImmediateStreak of them in a row, the routine slot for this
            // tick is used if it has anything waiting. Without this, anything
            // that adds immediate commands faster than one per tick stops all
            // routine polling, and radios that watch for CAT traffic (the
            // Kenwood LAN link, for one) then drop the connection.
            const bool haveImmediate = queue.contains(priorityImmediate);
            bool useRotation = !haveImmediate;
            if (haveImmediate && immediateStreak >= maxImmediateStreak) {
                const queuePriority slot = rotationSlot(counter);
                if (slot != priorityImmediate && queue.contains(slot)) {
                    useRotation = true;
                    ++fairnessTurnCount;
                }
            }
            if (useRotation) {
                prio = rotationSlot(counter);
                if (prio == priorityLowest)
                    counter = priorityImmediate;
            }
            counter++;

            queueItem item;
            bool haveItem = false;

            auto first = queue.lowerBound(prio);
            auto last  = queue.upperBound(prio);

            if (first != last) {
                auto it = last;
                --it;

                item = it.value();
                queue.erase(it);

                haveItem = true;
                ++sentCount;
                if (prio == priorityImmediate) {
                    ++immediateSentCount;
                    ++immediateStreak;
                } else {
                    immediateStreak = 0;
                    lastRoutineMs = healthClock.elapsed();
                }

                // Immediate commands update the cache on insert to make sure there is no delay
                if (prio != priorityImmediate) {
                    updateCache(false, item.command, item.param, item.receiver);
                }

                // Requeue recurring while locked (also make sure that immediate can't be recurring)
                if (item.recurring && prio != priorityImmediate) {
                    queue.insert(prio, item);
                }
            }

            if (!haveImmediate)
                immediateStreak = 0;

            const qint64 now = healthClock.elapsed();
            const qint64 startupReadsMs = checkStartupReads(now);
            bool warningDebugOnly = false;
            const QString warning = checkHealth(now, &warningDebugOnly);

            if (haveItem || !warning.isEmpty() || startupReadsMs >= 0) {
                locker.unlock();
                if (startupReadsMs >= 0)
                    qDebug(logRig()) << "Startup reads finished after" << startupReadsMs << "ms";
                if (!warning.isEmpty()) {
                    if (warningDebugOnly)
                        qDebug(logRig()) << qPrintable(warning);
                    else
                        qWarning(logRig()) << qPrintable(warning);
                }
                if (haveItem && !aborted) {
                    if (!item.rawData.isEmpty())
                        emit haveRawCommand(item.rawData, item.receiver);
                    else
                        emit haveCommand(item.command, item.param, item.receiver);
                }
                locker.relock();  // restore original "mutex held" assumption
            }

#ifdef Q_OS_MACOS
            QQueue<cacheItem> valuesToSend;
            QStringList msgsToSend;

            while (!items.isEmpty())
                valuesToSend.enqueue(items.dequeue());
            while (!messages.isEmpty())
                msgsToSend.append(messages.dequeue());

            locker.unlock();
            while (!valuesToSend.isEmpty())
                emit sendValue(valuesToSend.dequeue());
            for (const auto& m : msgsToSend)
                emit sendMessage(m);
            locker.relock();
#else
            QCoreApplication::processEvents();
#endif
            deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency

        }
        else if (!aborted) {
            // Copy current values/messages before emitting them so that we can unlock before emit
            QQueue<cacheItem> valuesToSend;
            QStringList msgsToSend;

            while (!items.isEmpty())
                valuesToSend.enqueue(items.dequeue());
            while (!messages.isEmpty())
                msgsToSend.append(messages.dequeue());

            locker.unlock();
            // Just to make sure we haven't aborted while we were building the commands.
            if (!aborted)
            {
                while (!valuesToSend.isEmpty())
                    emit sendValue(valuesToSend.dequeue());
                for (const auto& m : msgsToSend)
                    emit sendMessage(m);
            }
            locker.relock();

            // Start the timer, or bring the next tick forward if the interval was shortened.
            if (queueInterval != -1 && (deadline.isForever() || deadline.remainingTime() > queueInterval))
                deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency
        }
    }
}

void cachingQueue::interval(qint64 val)
{
    this->queueInterval = val;
    // Wake the thread so a shorter interval applies now (see run()).
    waiting.wakeAll();
    emit intervalUpdated(val);
    qDebug() << "Changing queue interval to" << val << "ms";
}

queuePriority cachingQueue::rotationSlot(quint16 counter) const
{
    // The routine (non-immediate) priority served on this tick, or
    // priorityImmediate if this tick has no routine slot.
    if (counter % priorityHighest == 0)
        return priorityHighest;
    if (counter % priorityHigh == 0)
        return priorityHigh;
    if (counter % priorityMediumHigh == 0)
        return priorityMediumHigh;
    if (counter % priorityMedium == 0)
        return priorityMedium;
    if (counter % priorityMediumLow == 0)
        return priorityMediumLow;
    if (counter % priorityLow == 0)
        return priorityLow;
    if (counter % priorityLowest == 0)
        return priorityLowest;
    return priorityImmediate;
}

qint64 cachingQueue::checkStartupReads(qint64 now)
{
    // Mutex MUST be locked by the calling function.
    // The startup reads are over once the immediate list, having filled, is
    // empty again. "Seen" stops a tick that lands between beginStartupReads()
    // and the first add from ending it early. The time limit stops it from
    // hiding a real backlog for long if the list never empties.
    if (!startupReads)
        return -1;
    const bool immediateWaiting = queue.contains(priorityImmediate);
    if (immediateWaiting)
        startupReadsSeen = true;
    if ((startupReadsSeen && !immediateWaiting) || now - startupReadsBeganMs > startupReadsMaxMs) {
        startupReads = false;
        return now - startupReadsBeganMs;   // logged by the caller once unlocked
    }
    return -1;
}

QString cachingQueue::checkHealth(qint64 now, bool *debugOnly)
{
    // Mutex MUST be locked by the calling function.
    // A watchdog only: it logs (rate limited) and counts, it never drops commands.
    const int immediate = int(queue.count(priorityImmediate));
    if (immediate > immediateHighWater)
        immediateHighWater = immediate;

    const bool routineWaiting = queue.size() > immediate;
    if (!routineWaiting || queueInterval <= 0)
        lastRoutineMs = now;

    if (now - lastWarnMs < warnRepeatMs)
        return QString();

    // The lowest priority only gets a slot every 23 ticks, so allow for slow intervals.
    const qint64 stallLimitMs = qMax(routineStallWarnMs, 30 * queueInterval);
    const qint64 stalledMs = now - lastRoutineMs;
    const bool stalled = stalledMs > stallLimitMs;
    if (immediate <= immediateWarnDepth && !stalled)
        return QString();

    // While the connect-time reads drain, a deep immediate list is expected:
    // log it once at debug level and leave the warning timer and count alone.
    // A stall in routine polling is still a warning.
    const bool startupBacklog = startupReads && !stalled;
    if (startupBacklog && startupDebugLogged)
        return QString();

    // Name the commands filling the immediate list, to show where they come from.
    QHash<int, int> counts;
    for (auto it = queue.constFind(priorityImmediate); it != queue.cend() && it.key() == priorityImmediate; ++it)
        ++counts[int(it.value().command)];
    QList<QPair<int, int>> top;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        top.append(qMakePair(it.value(), it.key()));
    std::sort(top.begin(), top.end(), [](const QPair<int,int> &a, const QPair<int,int> &b) { return a.first > b.first; });
    QStringList names;
    for (int i = 0; i < top.size() && i < 3; ++i)
        names << QStringLiteral("%1 x%2").arg(top[i].second < funcLastFunc ? funcString[top[i].second] : QStringLiteral("?")).arg(top[i].first);

    if (startupBacklog) {
        startupDebugLogged = true;
        if (debugOnly)
            *debugOnly = true;
    } else {
        lastWarnMs = now;
        ++warningCount;
    }
    return QStringLiteral("Command queue backlog: %1 immediate commands waiting (%2), %3 in total; "
                          "last routine command sent %4 ms ago%5")
        .arg(immediate).arg(names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", ")))
        .arg(queue.size()).arg(routineWaiting ? stalledMs : 0)
        .arg(startupBacklog ? QStringLiteral(" (startup reads)") : QString());
}

bool cachingQueue::readRemapped(funcs cmd) const
{
    // Reads that the radio code turns into another command before checking the
    // rig file, so the queue cannot judge them by their own entry.
    switch (cmd) {
    case funcSelectVFO:
    case funcFreq:            // Yaesu: the sub receiver reads funcFreqSub
    case funcDATAOffMod:      // Yaesu: mode-specific mod source
    case funcUSBModLevel:     // Yaesu: mode-specific USB mod gain
    case funcACCAModLevel:    // Yaesu: mode-specific rear mod gain
        return true;
    default:
        return false;
    }
}

funcs cachingQueue::checkCommandAvailable(funcs cmd,bool set)
{
    QMutexLocker locker(&mutex);
    if (rigCaps == nullptr || cmd == funcNone || cmd == funcSelectVFO)
        return cmd;

    auto it = rigCaps->commands.constFind(cmd);
    if (it == rigCaps->commands.constEnd()) {
        // We don't have the requested command!
        return funcNone;
    }

    // A read the radio cannot do would only be thrown away by the radio code
    // after using up a queue slot, so do not queue it at all.
    if (!set && !it->getCmd && !readRemapped(cmd)) {
        ++unreadableRejectedCount;
        if (cmd < funcLastFunc && !unreadableLogged[cmd].exchange(true))
            qInfo(logRig()) << "Not queuing reads of" << funcString[cmd]
                            << "as the rig file has no read command for it (logged once)";
        return funcNone;
    }
    return cmd;
}

void cachingQueue::add(queuePriority prio ,funcs func, bool recurring, uchar receiver)
{
    queueItem q(func,recurring,receiver);
    add(prio,q,false);
}

void cachingQueue::addUnique(queuePriority prio ,funcs func, bool recurring, uchar receiver)
{
    queueItem q(func,recurring, receiver);
    add(prio,q,true);
}

void cachingQueue::addUnique(queuePriority prio, queueItem item)
{
    add(prio,item,true);
}

void cachingQueue::add(queuePriority prio ,queueItem item, bool unique)
{
    // If the queue isn't running, no point adding to it!
    if (this->queueInterval != -1)
    {
        const bool isRawCommand = !item.rawData.isEmpty();
        if (!isRawCommand)
            item.command=checkCommandAvailable(item.command,item.param.isValid());
        if ((item.receiver != 0xff || rigState.receiver == item.receiver) && (isRawCommand || item.command != funcNone))
        {
            QMutexLocker locker(&mutex);

            // Do not add a duplicate recurring command of the same priority
            if (!item.recurring || isRecurring(item.command,item.receiver) != prio)
            {
                if (item.recurring && prio == queuePriority::priorityImmediate) {
                    qWarning() << "cachingQueue::add() Warning, cannot add recurring command with immediate priority!" << funcString[item.command];
                } else {
                    if (unique) {
                        int count=queue.remove(prio,item);
                        if (count>0)
                            qDebug() << "cachingQueue::add() deleted" << count << "entries from queue for" << funcString[item.command] << "on receiver" << item.receiver;
                    } else if (!isRawCommand && !item.recurring && !item.param.isValid() &&
                               readWaiting(prio, item.command, item.receiver)) {
                        // The same read is already waiting; a second one would
                        // only return the same value. Merging them keeps the
                        // waiting reads bounded however often they are asked for.
                        ++coalescedCount;
                        return;
                    }

                    // Don't immediately request funcTransceiverId, wait for the queue to run
                    if (!isRawCommand && item.recurring && item.command != funcTransceiverId &&
                        !readWaiting(priorityImmediate, item.command, item.receiver)) {
                        // also insert an immediate command to get the current value "now" (removes the need to get rigstate)
                        queueItem it=item;
                        it.recurring=false;
                        it.param.clear();
                        queue.insert(queue.cend(),priorityImmediate, it);
                    }
                    queue.insert(prio, item);
                }
            }
            //Immediately update the cache (even if we aren't sending a value)
            if (!isRawCommand && !item.recurring && prio == priorityImmediate)
            {
                updateCache(false,item.command,item.param,item.receiver);
            }
        }
    }
}

vfoCommandType cachingQueue::getVfoCommand(vfo_t vfo,uchar rx, bool set)
{
    vfoCommandType cmd;
    cmd.receiver = rx;
    cmd.vfo = vfo;
    QMutexLocker locker(&mutex);
    if (rigCaps != nullptr) {

        if (set) {
            cmd.modeFunc = ((rigCaps->commands.contains(funcMode)) ? funcMode: funcModeSet) ;
            cmd.freqFunc = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: funcFreqSet) ;
        } else {
            cmd.modeFunc = ((rigCaps->commands.contains(funcMode)) ? funcMode: funcModeGet) ;
            cmd.freqFunc = ((rigCaps->commands.contains(funcFreq)) ? funcFreq: funcFreqGet) ;
        }

        if (!rigCaps->hasCommand29) {
            // This radio has no direct access to each receiver (main/sub
            if (rigState.vfoMode == vfoModeType_t::vfoModeVfo && (rigState.receiver == 0 && rx == 0))
            {
                // This is acting on main
                // When the freq commands are fixed to VFO A/B and B is active, the active VFO is the "unselected" one
                const bool swapFreq = vfoFreqSwappedLocked();
                const funcs activeFreq = swapFreq ? funcUnselectedFreq : funcSelectedFreq;
                const funcs otherFreq = swapFreq ? funcSelectedFreq : funcUnselectedFreq;
                if (vfo == vfoA) {
                    cmd.modeFunc = ((rigCaps->commands.contains(funcSelectedMode)) ? funcSelectedMode: cmd.modeFunc);
                    cmd.freqFunc = ((rigCaps->commands.contains(activeFreq)) ? activeFreq: cmd.freqFunc);
                } else if (vfo == vfoB){
                    cmd.modeFunc = ((rigCaps->commands.contains(funcUnselectedMode)) ? funcUnselectedMode: cmd.modeFunc);
                    cmd.freqFunc = ((rigCaps->commands.contains(otherFreq)) ? otherFreq: cmd.freqFunc);
                }
            }
            else if (rx == rigState.receiver)
            {
                // Requesting receiver is current
                cmd.receiver = 0;
            }
            else
            {
                // Requesting receiver is not current.
                cmd.modeFunc = funcNone;
                cmd.freqFunc = funcNone;
                cmd.receiver = 0xff;
            }
        }
    }
    //qDebug(logRig()) << "Queue VFO:" << vfo << "rx:" << rx<< "set:" << set << "mode:" << funcString[cmd.modeFunc] << "freq:" << funcString[cmd.freqFunc] << "rigstate: vfoMode:" << rigState.vfoMode << "vfo" << rigState.vfo << "rx" << rigState.receiver;
    return cmd;
}

bool cachingQueue::vfoFreqSwappedLocked() const
{
    // Mutex MUST be locked by the calling function.
    return rigCaps != nullptr && rigCaps->absoluteVFOFreq && !rigCaps->hasCommand29
           && rigState.vfoMode == vfoModeType_t::vfoModeVfo && rigState.vfo == vfoB;
}

queuePriority cachingQueue::del(funcs func, uchar receiver, bool includePending)
{
    // This will immediately delete any matching commands.
    queuePriority prio = priorityNone;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        int count = 0;
        auto it = queue.begin();
        while (it != queue.end()) {
            if (it.value().command == func && it.value().receiver == receiver &&
                (it.value().recurring || includePending)) {
                if (prio == priorityNone)
                    prio = it.key();
                it = queue.erase(it);
                ++count;
            } else {
                ++it;
            }
        }

        if (count > 0)
            qDebug() << "cachingQueue()::del" << count << "entries from queue for" << funcString[func] << "on receiver" << receiver;
    }
    return prio;
}

queuePriority cachingQueue::delReads(funcs func, uchar receiver)
{
    // Like del(), but also removes waiting one-off reads (sets are kept).
    queuePriority prio = priorityNone;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        int count = 0;
        auto it = queue.begin();
        while (it != queue.end()) {
            const queueItem &q = it.value();
            if (q.command == func && q.receiver == receiver && q.rawData.isEmpty() &&
                (q.recurring || !q.param.isValid())) {
                if (prio == priorityNone)
                    prio = it.key();
                it = queue.erase(it);
                ++count;
            } else {
                ++it;
            }
        }

        if (count > 0)
            qDebug() << "cachingQueue()::delReads" << count << "entries from queue for" << funcString[func] << "on receiver" << receiver;
    }
    return prio;
}

bool cachingQueue::readWaiting(queuePriority prio, funcs func, uchar receiver) const
{
    // Mutex MUST be locked by the calling function.
    for (auto it = queue.constFind(prio); it != queue.cend() && it.key() == prio; ++it) {
        const queueItem &q = it.value();
        if (q.command == func && q.receiver == receiver && !q.recurring &&
            !q.param.isValid() && q.rawData.isEmpty())
            return true;
    }
    return false;
}


queuePriority cachingQueue::getQueued(funcs func, uchar receiver)
{
    queuePriority prio = priorityNone;
    if (func != funcNone)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        //auto it(queue.begin());
        if (it != queue.end()) {
            prio = it.key();
        }
    }
    return prio;
}

queuePriority cachingQueue::changePriority(queuePriority newprio, funcs func, uchar receiver)
{
    queuePriority prio = priorityNone;
    if (func != funcNone && newprio != priorityImmediate)
    {
        QMutexLocker locker(&mutex);
        auto it = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
        //auto it(queue.begin());
        if (it != queue.end()) {
            prio = it.key();
            auto item = it.value();
            it=queue.erase(it);
            queue.insert(newprio,item);
        }
    }
    return prio;
}

queuePriority cachingQueue::isRecurring(funcs func, uchar receiver)
{
    // Does NOT lock the mutex
    auto rec = std::find_if(queue.begin(), queue.end(), [func,receiver](const queueItem& c) {  return (c.command == func && c.receiver == receiver && c.recurring); });
    if (rec != queue.end())
    {
        return rec.key();
    }
    return queuePriority::priorityNone;
}

void cachingQueue::clear()
{
    QMutexLocker locker(&mutex);
    queue.clear();
    startupReads = false;
}

void cachingQueue::beginStartupReads()
{
    QMutexLocker locker(&mutex);
    startupReads = true;
    startupReadsSeen = false;
    startupDebugLogged = false;
    startupReadsBeganMs = healthClock.isValid() ? healthClock.elapsed() : 0;
}

void cachingQueue::setRigCaps(rigCapabilities* caps)
{
    {
        QMutexLocker locker(&mutex);
        if (rigCaps == caps)
            return;
        rigCaps = caps;
    }
    // A new rig file may read what the old one could not.
    for (auto &logged : unreadableLogged)
        logged.store(false);
    // Emitted unlocked: direct-connected slots call back into the queue.
    emit rigCapsUpdated(caps);
}

queueStats cachingQueue::getStats()
{
    QMutexLocker locker(&mutex);
    queueStats st;
    st.sent = sentCount;
    st.immediateSent = immediateSentCount;
    st.coalesced = coalescedCount;
    st.refreshesSkipped = refreshesSkippedCount;
    st.unreadableRejected = unreadableRejectedCount.load();
    st.fairnessTurns = fairnessTurnCount;
    st.warnings = warningCount;
    st.immediateWaiting = int(queue.count(priorityImmediate));
    st.immediateHighWater = qMax(immediateHighWater, st.immediateWaiting);
    st.totalWaiting = int(queue.size());
    if (st.totalWaiting > st.immediateWaiting && healthClock.isValid())
        st.msSinceRoutine = healthClock.elapsed() - lastRoutineMs;
    return st;
}

void cachingQueue::resetStats()
{
    QMutexLocker locker(&mutex);
    sentCount = immediateSentCount = coalescedCount = refreshesSkippedCount = 0;
    fairnessTurnCount = warningCount = 0;
    unreadableRejectedCount.store(0);
    immediateHighWater = 0;
    lastWarnMs = -warnRepeatMs;
}
void cachingQueue::message(QString msg)
{

    if (mutex.tryLock(CACHE_LOCK_TIME)) {
        messages.append(msg);
        mutex.unlock();
        qDebug() << "Received:" << msg;
#ifndef Q_OS_MACOS
        waiting.wakeOne();
#endif
    } else {
        qWarning(logRig()) << "Queue failed to send message() after" << CACHE_LOCK_TIME << "ms, mutex locked";
    }
}

void cachingQueue::receiveValue(funcs func, QVariant value, uchar receiver)
{
    if (mutex.tryLock(CACHE_LOCK_TIME)) {
        cacheItem c = cacheItem(func,value,receiver);
        items.enqueue(c);
        updateCache(true,func,value,receiver);        
        mutex.unlock();
#ifndef Q_OS_MACOS
        waiting.wakeOne();
#endif
    } else {
        qWarning(logRig()) << "Failed to receiveValue() after" << CACHE_LOCK_TIME << "ms, mutex locked";
    }
}


void cachingQueue::updateCache(bool reply, queueItem item)
{
    // Mutex MUST be locked by the calling function.

    // We need to make sure that all "main" frequencies/modes are updated.
    if (reply)
    {
        // Is this a command that might have updated our state?
        if (item.command == funcSatelliteMode && item.param.value<bool>())
            rigState.vfoMode=vfoModeType_t::vfoModeSat;
        if (item.command == funcMemoryMode && item.param.value<bool>())
            rigState.vfoMode=vfoModeType_t::vfoModeMem;
        if (item.command == funcVFOMode && item.param.value<bool>())
            rigState.vfoMode=vfoModeType_t::vfoModeVfo;
        if (item.command == funcVFOBandMS)
            rigState.receiver = item.param.value<uchar>();
    } else {
        // If we are requesting a particular VFO, set our state as the rig will not reply
        if (item.command == funcSelectVFO) {
            rigState.vfo = item.param.value<vfo_t>();
        } else if (item.command == funcVFOASelect) {
            rigState.vfo = vfo_t::vfoA;
        } else if (item.command == funcVFOBSelect && rigCaps->numVFO > 1) {
            rigState.vfo = vfo_t::vfoB;
        } else if (item.command == funcVFOMainSelect) {
            rigState.vfo = vfo_t::vfoMain;
            rigState.receiver=0;
        } else if (item.command == funcVFOSubSelect && rigCaps->numReceiver > 1) {
            rigState.vfo = vfo_t::vfoSub;
            rigState.receiver=1;
        }

    }

    auto cv = cache.find(item.command);
    while (cv != cache.end() && cv->command == item.command) {
        if (cv->receiver == item.receiver) {
            if (reply) {
                cv->reply = QDateTime::currentDateTime();
            } else {
                cv->req = QDateTime::currentDateTime();
            }
            // If we are sending an actual value, update the cache with it
            // Value will be replaced if invalid on next get()

            if (compare(item.param,cv.value().value))
            {
                cv->value.clear();
                cv->value.setValue(item.param);

                emit cacheUpdated(cv.value());
            }
            return;
            // We have found (and updated) a matching item so return
        }
        ++cv;
    }

    cacheItem c;
    c.command = item.command;
    c.receiver = item.receiver;

    if (reply) {
        c.reply = QDateTime::currentDateTime();
        c.retries = 0;
    } else {
        c.req = QDateTime::currentDateTime();
        c.retries++;
    }
    // If we are sending an actual value, update the cache with it
    // Value will be replaced if invalid on next get()
    if (item.param.isValid())
    {
        c.value.setValue(item.param);
    }

    cache.insert(item.command,c);

}


void cachingQueue::updateCache(bool reply, funcs func, QVariant value, uchar receiver)
{
    queueItem q(func,value,false,receiver);
    updateCache(reply,q);
}

void cachingQueue::clearCache()
{
    QMutexLocker locker(&mutex);
    cache.clear();
}

cacheItem cachingQueue::getCache(funcs func, uchar receiver)
{
    cacheItem ret;
    bool refresh = false;
    if (func != funcNone) {
        QMutexLocker locker(&mutex);
        auto it = cache.find(func);
        while (it != cache.end() && it->command == func)
        {
            if (it->receiver == receiver) {
                ret = cacheItem(*it);
                break;
            }
            ++it;
        }

        // If the cache is more than 5-20 seconds old, re-request it as it may be stale (maybe make this a config option?)
        // Using priorityhighest WILL slow down the S-Meter when a command intensive client is connected to rigctl
        // SWR is always re-read, but only where the rig file has a read command
        // for SWR on its own. Some radios have none: the TS-890S reads its meters
        // together with "RM;" and, once a meter is enabled ("RM21;"), sends it by
        // itself while transmitting, so its rig file marks SWR GetCommand=false.
        if (func != funcPowerControl && func != funcSelectVFO) {
            const QDateTime now = QDateTime::currentDateTime();
            const bool swrReadable = func == funcSWRMeter && rigCaps != nullptr &&
                                     rigCaps->commands.value(funcSWRMeter).getCmd;
            if ((!ret.value.isValid() && ret.retries < 5) || swrReadable ||
                ret.reply.addSecs(QRandomGenerator::global()->bounded(5,20)) <= now) {
                // Reading the cache must not turn into radio traffic at the
                // rate the cache is read: ask again only if no request for
                // this value went out in the last minRefreshMs (a recurring
                // poll counts too).
                if (ret.req.isValid() && ret.req.msecsTo(now) < minRefreshMs)
                    ++refreshesSkippedCount;
                else
                    refresh = true;
            }
        }
    }
    if (refresh)
        add(priorityImmediate,func,false,receiver);
    return ret;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap<funcs,cacheItem>* cachingQueue::getCacheItems()
{
    mutex.lock();
    return &cache;
}

//Calling function MUST call unlockMutex() once finished with data
QMultiMap <queuePriority,queueItem>* cachingQueue::getQueueItems()
{
    mutex.lock();
    return &queue;
}

bool cachingQueue::compare(QVariant a, QVariant b)
{
    bool changed = false;

    if (a.isValid() && b.isValid()) {

        // Compare the details
        if (!strcmp(a.typeName(),"bool")){
            if (a.value<bool>() != b.value<bool>())
                changed=true;
        } else if (!strcmp(a.typeName(),"QString")) {
            changed=true;
        } else if (!strcmp(a.typeName(),"uchar")) {
            if (a.value<uchar>() != b.value<uchar>())
                changed=true;
        } else if (!strcmp(a.typeName(),"ushort")) {
            if (a.value<ushort>() != b.value<ushort>())
                changed=true;
        } else if (!strcmp(a.typeName(),"short")) {
            if (a.value<short>() != a.value<short>())
                changed=true;
        } else if (!strcmp(a.typeName(),"uint")) {
            if (a.value<uint>() != b.value<uint>())
                changed=true;
        } else if (!strcmp(a.typeName(),"int")) {
            if (a.value<int>() != b.value<int>())
                changed=true;
        } else if (!strcmp(a.typeName(),"double")) {
            if (a.value<double>() != b.value<double>())
                changed=true;
        } else if (!strcmp(a.typeName(),"modeInfo")) {
            if (a.value<modeInfo>().mk != b.value<modeInfo>().mk || a.value<modeInfo>().reg != b.value<modeInfo>().reg
                || a.value<modeInfo>().filter != b.value<modeInfo>().filter || a.value<modeInfo>().data != b.value<modeInfo>().data) {
                changed=true;
            }
        } else if(!strcmp(a.typeName(),"freqt")) {
            if (a.value<freqt>().Hz != b.value<freqt>().Hz)
                changed=true;
        } else if(!strcmp(a.typeName(),"antennaInfo")) {
            if (a.value<antennaInfo>().antenna != b.value<antennaInfo>().antenna || a.value<antennaInfo>().rx != b.value<antennaInfo>().rx)
                changed=true;
        } else if(!strcmp(a.typeName(),"rigInput")) {
            if (a.value<rigInput>() != b.value<rigInput>())
                changed=true;
        } else if (!strcmp(a.typeName(),"duplexMode_t")) {
            if (a.value<duplexMode_t>() != b.value<duplexMode_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"toneInfo")) {
            if (a.value<toneInfo>().tone != b.value<toneInfo>().tone)
                changed=true;
        } else if (!strcmp(a.typeName(),"meter_t")) {
            if (a.value<meter_t>() != b.value<meter_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"vfo_t")) {
            if (a.value<vfo_t>() != b.value<vfo_t>())
                changed=true;
        } else if (!strcmp(a.typeName(),"lpfhpf")) {
            if (a.value<lpfhpf>().lpf != b.value<lpfhpf>().lpf || a.value<lpfhpf>().hpf != b.value<lpfhpf>().hpf  )
                changed=true;
        } else if (!strcmp(a.typeName(),"spectrumBounds")) {
            if (a.value<spectrumBounds>().edge != b.value<spectrumBounds>().edge ||
                    a.value<spectrumBounds>().start != b.value<spectrumBounds>().start ||
                    a.value<spectrumBounds>().end != b.value<spectrumBounds>().end  )
                changed=true;
        } else if (!strcmp(a.typeName(),"centerSpanData")) {
            if (a.value<centerSpanData>().reg != b.value<centerSpanData>().reg || a.value<centerSpanData>().freq != b.value<centerSpanData>().freq  )
                changed=true;
        } else if (!strcmp(a.typeName(),"rptrAccessData")) {
            if (a.value<rptrAccessData>().accessMode != b.value<rptrAccessData>().accessMode ||
                            a.value<rptrAccessData>().turnOffTSQL != b.value<rptrAccessData>().turnOffTSQL ||
                            a.value<rptrAccessData>().turnOffTone != b.value<rptrAccessData>().turnOffTone)
                changed=true;
        } else if (!strcmp(a.typeName(),"scopeData") || !strcmp(a.typeName(),"memoryType")
                   || !strcmp(a.typeName(), "memoryTagType") || !strcmp(a.typeName(), "memorySplitType")
                   || !strcmp(a.typeName(),"bandStackType")  || !strcmp(a.typeName(),"timekind") || !strcmp(a.typeName(),"datekind")
                   || !strcmp(a.typeName(),"meterkind") || !strcmp(a.typeName(),"udpPreferences")) {
            changed=true; // Always different
        } else {
            // Maybe Try simple comparison?
            qInfo () << "Unsupported cache value:" << a.typeName();
        }
    } else if (a.isValid()) {
        changed = true;
    }

    return changed;
}
