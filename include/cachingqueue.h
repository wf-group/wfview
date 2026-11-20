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
#include <QElapsedTimer>
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
    queueItem (queueItem const &q): command(q.command), param(q.param), receiver(q.receiver), recurring(q.recurring), rawData(q.rawData), id(nextId++) {};
    queueItem (funcs command, QVariant param, bool recurring, uchar receiver) : command(command), param(param), receiver(receiver), recurring(recurring), id(nextId++) {};
    queueItem (funcs command, QVariant param, bool recurring) : command(command), param(param), receiver(0), recurring(recurring), id(nextId++) {};
    queueItem (funcs command, QVariant param) : command(command), param(param),receiver(0), recurring(0), id(nextId++) {};
    queueItem (funcs command, bool recurring, uchar receiver) : command(command), param(QVariant()), receiver(receiver), recurring(recurring), id(nextId++) {};
    queueItem (funcs command, bool recurring) : command(command), param(QVariant()), receiver(0), recurring(recurring), id(nextId++) {};
    queueItem (funcs command) : command(command), param(QVariant()), receiver(0), recurring(false), id(nextId++){};
    queueItem (QByteArray rawData, uchar receiver=0) : command(funcNone), param(QVariant()), receiver(receiver), recurring(false), rawData(rawData), id(nextId++) {};
    funcs command;
    QVariant param;
    uchar receiver;
    bool recurring;
    QByteArray rawData;
    static std::atomic<qint64> nextId;
    qint64 id = nextId++;
    //qint64 id = QDateTime::currentMSecsSinceEpoch();

    bool operator==(const queueItem& lhs) const
    {
        return (lhs.command == command &&
                lhs.receiver == receiver &&
                lhs.recurring == recurring &&
                lhs.param.isValid() == param.isValid() &&
                lhs.rawData == rawData);
    }
};

struct cacheItem {
    cacheItem() = default;
    cacheItem(const cacheItem&) = default;
    cacheItem& operator=(const cacheItem&) = default;
    //cacheItem (cacheItem const &c): command(c.command), req(c.req), reply(c.reply), value(c.value), receiver(c.receiver) {};
    cacheItem (funcs command, QVariant value, uchar receiver=0) : command(command), req(QDateTime()), reply(QDateTime()), value(value), receiver(receiver){};

    funcs command = funcNone;
    QDateTime req;
    QDateTime reply;
    QVariant value;
    uchar receiver = 0;
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

// Health counters for the command queue (shown by MCP get_queue).
struct queueStats {
    quint64 sent = 0;                // commands handed to the radio code
    quint64 immediateSent = 0;       // ... of which immediate priority
    quint64 coalesced = 0;           // reads not added: the same read was already waiting
    quint64 refreshesSkipped = 0;    // getCache() refreshes skipped: one was requested very recently
    quint64 unreadableRejected = 0;  // reads refused: the rig file has no read command for them
    quint64 fairnessTurns = 0;       // routine polls sent while immediate commands were waiting
    quint64 warnings = 0;            // backlog or stall warnings logged
    int immediateWaiting = 0;
    int immediateHighWater = 0;
    int totalWaiting = 0;
    qint64 msSinceRoutine = -1;      // since a routine (non-immediate) command was sent; -1 if none waiting
};

class cachingQueue : public QThread
{
    Q_OBJECT

signals:
    void haveCommand(funcs func, QVariant param, uchar receiver);
    void haveRawCommand(QByteArray data, uchar receiver);
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

    // Queue robustness (see run(), add() and getCache()).
    // After this many immediate commands in a row, routine polling gets a
    // turn, so a stream of immediate commands cannot stop it altogether.
    static constexpr int maxImmediateStreak = 4;
    // getCache() asks the radio again at most this often per function/receiver.
    static constexpr qint64 minRefreshMs = 200;
    // Watchdog thresholds.
    static constexpr int immediateWarnDepth = 50;
    static constexpr qint64 routineStallWarnMs = 3000;
    static constexpr qint64 warnRepeatMs = 10000;
    // The startup reads are never treated as a backlog for longer than this.
    static constexpr qint64 startupReadsMaxMs = 10000;

    bool readRemapped(funcs cmd) const;
    queuePriority rotationSlot(quint16 counter) const;
    qint64 checkStartupReads(qint64 now); // mutex must be held; ms the startup reads took when they end, else -1
    // mutex must be held; returns a warning or "", and sets debugOnly when it is
    // only the expected startup backlog
    QString checkHealth(qint64 now, bool *debugOnly);
    bool readWaiting(queuePriority prio, funcs func, uchar receiver) const; // mutex must be held

    int immediateStreak = 0;
    QElapsedTimer healthClock;       // started by run(); the clock for the two below
    qint64 lastRoutineMs = 0;        // a routine command went out (or none was waiting)
    qint64 lastWarnMs = -warnRepeatMs;
    int immediateHighWater = 0;      // mutex
    quint64 sentCount = 0;           // mutex
    quint64 immediateSentCount = 0;  // mutex
    quint64 coalescedCount = 0;      // mutex
    quint64 refreshesSkippedCount = 0; // mutex
    quint64 fairnessTurnCount = 0;   // mutex
    quint64 warningCount = 0;        // mutex
    // Startup reads (see beginStartupReads()); all mutex.
    bool startupReads = false;       // the connect-time burst is still draining
    bool startupReadsSeen = false;   // immediate commands have been seen since it began
    bool startupDebugLogged = false; // the backlog has been logged once for this burst
    qint64 startupReadsBeganMs = 0;
    std::atomic<quint64> unreadableRejectedCount{0};
    std::atomic_bool unreadableLogged[funcLastFunc] = {};

    // Various other values
    std::atomic_bool aborted{false};
    qint64 queueInterval=-1; // Don't start the timer!
    
    rigCapabilities* rigCaps = nullptr; // Must be NULL until a radio is connected
    
    // Functions
    void run();
    funcs checkCommandAvailable(funcs cmd, bool set=false);
    rigStateType rigState;
    QByteArray yaesuData;
    bool vfoFreqSwappedLocked() const;

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
    // Removes the recurring entry and every waiting read (but not sets) of func.
    // For radio code that finds the radio cannot read func.
    queuePriority delReads(funcs func, uchar receiver=0);
    void clear();
    // Call before queuing the connect-time reads. Until they have drained, a deep
    // immediate list is expected and is logged at debug level, not as a warning.
    void beginStartupReads();
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
    void setRigCaps(rigCapabilities* caps);
    queueStats getStats();
    void resetStats();
    rigCapabilities* getRigCaps() { return rigCaps; }
    vfoCommandType getVfoCommand(vfo_t vfo, uchar rx, bool set=false);
    rigStateType getState() { QMutexLocker locker(&mutex); return rigState ;}
    // True when VFO B is active on a rig whose Selected/Unselected Freq commands are fixed to VFO A/B
    bool vfoFreqSwapped() { QMutexLocker locker(&mutex); return vfoFreqSwappedLocked(); }
};

Q_DECLARE_METATYPE(queueItem)
Q_DECLARE_METATYPE(cacheItem)
#endif // CACHINGQUEUE_H
