#ifndef CACHINGQUEUE_H
#define CACHINGQUEUE_H
#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QMap>
#include <QMultiMap>
#include <QVariant>
#include <QQueue>
#include <QRect>
#include <QWaitCondition>
#include <QDateTime>
#include <QRandomGenerator>
#include <atomic>

#include "wfviewtypes.h"
#include "rigidentities.h"

enum queuePriority {
    // Use prime numbers for priority, so that each queue is processed
    priorityNone=0, priorityImmediate=1, priorityHighest=2, priorityHigh=3, priorityMediumHigh=5, priorityMedium=7, priorityMediumLow=11, priorityLow=19, priorityLowest=23
};

inline QMap<QString,int> priorityMap = {{"None",0},{"Immediate",1},{"Highest",2},{"High",3},{"Medium High",5},{"Medium",7},{"Medium Low",11},{"Low",19},{"Lowest",23}};

// Command with no param is a get by default
struct queueItem {
    queueItem() : id(nextId++){}
    queueItem (queueItem const &q): command(q.command), param(q.param), receiver(q.receiver), recurring(q.recurring), id(nextId++) {};
    queueItem (funcs command, QVariant param, bool recurring, uchar receiver) : command(command), param(param), receiver(receiver), recurring(recurring), id(nextId++) {};
    queueItem (funcs command, QVariant param, bool recurring) : command(command), param(param), receiver(0), recurring(recurring), id(nextId++) {};
    queueItem (funcs command, QVariant param) : command(command), param(param),receiver(0), recurring(0), id(nextId++) {};
    queueItem (funcs command, bool recurring, uchar receiver) : command(command), param(QVariant()), receiver(receiver), recurring(recurring), id(nextId++) {};
    queueItem (funcs command, bool recurring) : command(command), param(QVariant()), receiver(0), recurring(recurring), id(nextId++) {};
    queueItem (funcs command) : command(command), param(QVariant()), receiver(0), recurring(false), id(nextId++){};
    funcs command;
    QVariant param;
    uchar receiver;
    bool recurring;
    static std::atomic<qint64> nextId;
    qint64 id = nextId++;
    //qint64 id = QDateTime::currentMSecsSinceEpoch();

    bool operator==(const queueItem& lhs) const
    {
        return (lhs.command == command &&
                lhs.receiver == receiver &&
                lhs.recurring == recurring &&
                lhs.param.isValid() == param.isValid());
    }
};

struct cacheItem {
    cacheItem() = default;
    cacheItem(const cacheItem&) = default;
    cacheItem& operator=(const cacheItem&) = default;
    //cacheItem (cacheItem const &c): command(c.command), req(c.req), reply(c.reply), value(c.value), receiver(c.receiver) {};
    cacheItem (funcs command, QVariant value, uchar receiver=0) : command(command), req(QDateTime()), reply(QDateTime()), value(value), receiver(receiver){};

    funcs command;
    QDateTime req;
    QDateTime reply;
    QVariant value;
    uchar receiver;
    uchar retries=0;

    /*
    cacheItem &operator=(const cacheItem &i) {
        this->receiver=i.receiver;
        this->command=i.command;
        this->reply=i.reply;
        this->req=i.req;
        this->value=i.value;
        return *this;
    }
    */
};

class cachingQueue : public QThread
{
    Q_OBJECT

signals:
    void haveCommand(funcs func, QVariant param, uchar receiver);
    void sendValue(cacheItem item);
    void sendMessage(QString msg);
    void cacheUpdated(cacheItem item);
    void rigCapsUpdated(rigCapabilities* caps);
    void intervalUpdated(qint64 val);

public slots:
    // Can be called directly or via emit.
    void receiveValue(funcs func, QVariant value, uchar receiver);

private:

    static cachingQueue *instance;
    static QMutex instanceMutex;

    QMutex mutex;

    QMultiMap <queuePriority,queueItem> queue;
    QMultiMap<funcs,cacheItem> cache;
    QQueue<cacheItem> items;
    QQueue<QString> messages;
    QWaitCondition waiting;

    // Command to set cache value
    void setCache(funcs func, QVariant val, uchar receiver=0);
    queuePriority isRecurring(funcs func, uchar receiver=0);
    bool compare(QVariant a, QVariant b);

    // Various other values
    std::atomic_bool aborted{false};
    qint64 queueInterval=-1; // Don't start the timer!
    
    rigCapabilities* rigCaps = nullptr; // Must be NULL until a radio is connected
    
    // Functions
    void run();
    funcs checkCommandAvailable(funcs cmd, bool set=false);
    rigStateType rigState;
    QByteArray yaesuData;

protected:
    cachingQueue(QObject* parent = nullptr) : QThread(parent) {};
    ~cachingQueue();

public:
    cachingQueue(cachingQueue &other) = delete;
    void operator=(const cachingQueue &) = delete;

    static cachingQueue *getInstance(QObject* parent = nullptr);
    void message(QString msg);
    void putYaesuData(QByteArray data) { yaesuData = data;}
    QByteArray getYaesuData() { return yaesuData; }

    void add(queuePriority prio ,funcs func, bool recurring=false, uchar receiver=0);
    void add(queuePriority prio,queueItem item, bool unique=false);
    void addUnique(queuePriority prio ,funcs func, bool recurring=false, uchar receiver=0);
    void addUnique(queuePriority prio, queueItem item);

    queuePriority del(funcs func, uchar receiver=0, bool includePending=false);
    void clear();
    void clearCache();

    void interval(qint64 val);
    qint64 interval() {return queueInterval;}
    void updateCache(bool reply, queueItem item);
    void updateCache(bool reply, funcs func, QVariant value=QVariant(), uchar receiver=0);

    cacheItem getCache(funcs func, uchar receiver=0);

    queuePriority getQueued(funcs func, uchar receiver=0);

    queuePriority changePriority(queuePriority prio, funcs func, uchar receiver=0);

    QMultiMap <funcs,cacheItem>* getCacheItems();
    QMultiMap <queuePriority,queueItem>* getQueueItems();
    void lockMutex() {mutex.lock();}
    void unlockMutex() {mutex.unlock();}
    void setRigCaps(rigCapabilities* caps) { if (rigCaps != caps) { rigCaps = caps; emit rigCapsUpdated(rigCaps);} }
    rigCapabilities* getRigCaps() { return rigCaps; }
    vfoCommandType getVfoCommand(vfo_t vfo, uchar rx, bool set=false);
    rigStateType getState() { QMutexLocker locker(&mutex); return rigState ;}
};

Q_DECLARE_METATYPE(queueItem)
Q_DECLARE_METATYPE(cacheItem)
#endif // CACHINGQUEUE_H
