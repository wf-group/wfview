/*
 *  This is a singleton queue (only 1 instance can be created)
 *
 *  Although subclassing QThread is often not recommended, I think that this is
 *  a valid use case for subclassing.
 *
*/
#include "logcategories.h"
#include "cachingqueue.h"

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
    qInfo() << "Destroying caching queue (app closing)";

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

    quint16 counter=priorityImmediate;

    while (!aborted)
    {
        if (!waiting.wait(&mutex, deadline.remainingTime()))
        {
            // Time to process the queue - mutex is locked
            queuePriority prio = priorityImmediate;

            // priorityNone=0, priorityImmediate=1, priorityHighest=2, priorityHigh=3, priorityMediumHigh=5, priorityMedium=7, priorityMediumLow=11, priorityLow=19, priorityLowest=23

            // If no immediate commands then process the rest of the queue
            if (!queue.contains(prio)) {
                if (counter % priorityHighest == 0)
                    prio = priorityHighest;
                else if (counter % priorityHigh == 0)
                    prio = priorityHigh;
                else if (counter % priorityMediumHigh == 0)
                    prio = priorityMediumHigh;
                else if (counter % priorityMedium == 0)
                    prio = priorityMedium;
                else if (counter % priorityMediumLow == 0)
                    prio = priorityMediumLow;
                else if (counter % priorityLow == 0)
                    prio = priorityLow;
                else if (counter % priorityLowest == 0) {
                    prio = priorityLowest;
                    counter = priorityImmediate;
                }
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

                // Immediate commands update the cache on insert to make sure there is no delay
                if (prio != priorityImmediate) {
                    updateCache(false, item.command, item.param, item.receiver);
                }

                // Requeue recurring while locked (also make sure that immediate can't be recurring)
                if (item.recurring && prio != priorityImmediate) {
                    queue.insert(prio, item);
                }
            }

            if (haveItem) {
                locker.unlock();
                if (!aborted) {
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

            if (queueInterval != -1 && deadline.isForever())
                deadline.setRemainingTime(queueInterval); // reset the deadline to the poll frequency
        }
    }
}

void cachingQueue::interval(qint64 val)
{
    this->queueInterval = val;
    if (mutex.tryLock()) {
        mutex.unlock();
        waiting.wakeAll();
    }
    emit intervalUpdated(val);
    qInfo() << "Changing queue interval to" << val << "ms";
}

funcs cachingQueue::checkCommandAvailable(funcs cmd,bool set)
{
    Q_UNUSED(set)
    if (rigCaps != nullptr && cmd != funcNone && cmd != funcSelectVFO && !rigCaps->commands.contains(cmd)) {
        // We don't have the requested command!
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
                    }

                    // Don't immediately request funcTransceiverId, wait for the queue to run
                    if (!isRawCommand && item.recurring && item.command != funcTransceiverId) {
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
    if (rigCaps != nullptr) {
        QMutexLocker locker(&mutex);

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
                if (vfo == vfoA) {
                    cmd.modeFunc = ((rigCaps->commands.contains(funcSelectedMode)) ? funcSelectedMode: cmd.modeFunc);
                    cmd.freqFunc = ((rigCaps->commands.contains(funcSelectedFreq)) ? funcSelectedFreq: cmd.freqFunc);
                } else if (vfo == vfoB){
                    cmd.modeFunc = ((rigCaps->commands.contains(funcUnselectedMode)) ? funcUnselectedMode: cmd.modeFunc);
                    cmd.freqFunc = ((rigCaps->commands.contains(funcUnselectedFreq)) ? funcUnselectedFreq: cmd.freqFunc);
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
    }
    // If the cache is more than 5-20 seconds old, re-request it as it may be stale (maybe make this a config option?)
    // Using priorityhighest WILL slow down the S-Meter when a command intensive client is connected to rigctl
    if (func != funcPowerControl && func != funcSelectVFO && ((!ret.value.isValid() && ret.retries < 5)|| ret.command == funcSWRMeter || ret.reply.addSecs(QRandomGenerator::global()->bounded(5,20)) <= QDateTime::currentDateTime())) {
        add(priorityImmediate,func,false,receiver);
    }
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
