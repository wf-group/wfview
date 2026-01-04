#include "debugcontroller.h"
#include <QString>

#include "logcategories.h"


DebugController::DebugController(QObject* parent)
    : QObject(parent)
{
    m_queue = cachingQueue::getInstance();

    m_cacheTimer.setInterval(m_cacheIntervalMs);
    m_queueTimer.setInterval(m_queueIntervalMs);
    m_yaesuTimer.setInterval(m_queueIntervalMs);

    connect(&m_cacheTimer, &QTimer::timeout, this, &DebugController::refreshCache);
    connect(&m_queueTimer, &QTimer::timeout, this, &DebugController::refreshQueue);
    connect(&m_yaesuTimer, &QTimer::timeout, this, &DebugController::refreshYaesu);

    m_cacheTimer.start();
    m_queueTimer.start();
    m_yaesuTimer.start();

    qWarning() << "******** Creating instance of DebugController";
    // prime once
    refreshCache();
    refreshQueue();
    refreshYaesu();
}

void DebugController::setCachePaused(bool v)
{
    if (m_cachePaused == v) return;
    m_cachePaused = v;
    if (m_cachePaused) m_cacheTimer.stop(); else m_cacheTimer.start();
    emit cachePausedChanged();
}

void DebugController::setQueuePaused(bool v)
{
    if (m_queuePaused == v) return;
    m_queuePaused = v;
    if (m_queuePaused) m_queueTimer.stop(); else m_queueTimer.start();
    emit queuePausedChanged();
}

void DebugController::setCacheIntervalMs(int ms)
{
    if (ms < 1) ms = 1;
    if (m_cacheIntervalMs == ms) return;
    m_cacheIntervalMs = ms;
    m_cacheTimer.setInterval(m_cacheIntervalMs);
    emit cacheIntervalMsChanged();
}

void DebugController::setQueueIntervalMs(int ms)
{
    if (ms < 1) ms = 1;
    if (m_queueIntervalMs == ms) return;
    m_queueIntervalMs = ms;
    m_queueTimer.setInterval(m_queueIntervalMs);
    m_yaesuTimer.setInterval(m_queueIntervalMs);
    emit queueIntervalMsChanged();
}

void DebugController::refreshCache()
{
    if (!m_queue) return;

    auto cacheItems = m_queue->getCacheItems();
    m_cacheTitle = QString("Current cache items in cachingView(%0)").arg(cacheItems->size());

    QVariantList out;
    out.reserve(int(cacheItems->size()));

    auto it = cacheItems->cbegin();
    while (it != cacheItems->cend()) {
        const auto& ci = it.value();
        QVariantMap row;
        row["id"]    = QString::number(ci.command).rightJustified(3, '0');
        row["func"]  = funcString[ci.command];
        row["value"] = getValue(ci.value);
        row["rx"]    = QString::number(ci.receiver);
        row["req"]   = ci.req.isValid()   ? ci.req.toString("hh:mm:ss.zzz")   : "<none>";
        row["reply"] = ci.reply.isValid() ? ci.reply.toString("hh:mm:ss.zzz") : "<none>";
        out.push_back(row);
        ++it;
    }

    m_queue->unlockMutex();

    m_cacheModel = out;
    emit cacheModelChanged();
    emit cacheTitleChanged();
}

void DebugController::refreshQueue()
{
    if (!m_queue) return;

    auto queueItems = m_queue->getQueueItems();

    m_queueTitle = QString("Current queue items in cachingView(%0)")
                       .arg(queueItems->size());

    QVariantList out;
    out.reserve(int(queueItems->size()));

    auto i = queueItems->cbegin();
    while (i != queueItems->cend())
    {
        const auto &qi = i.value();

        QVariantMap row;
        row["id"]     = QString::number(qi.command).rightJustified(3, '0');
        row["func"]   = funcString[qi.command];
        row["pri"]    = QString::number(i.key());
        row["getset"] = (qi.param.isValid() ? "Set" : "Get");
        row["rx"]     = getValue(i->receiver);        // <- match your widget code
        row["value"]  = getValue(qi.param);
        row["rec"]    = (qi.recurring ? "True" : "False");

        out.push_back(row);
        ++i;
    }

    m_queue->unlockMutex();

    // DEBUG: prove it’s being built
    qDebug() << "refreshQueue: queueItems=" << queueItems->size()
             << "out=" << out.size();

    m_queueModel = out;
    emit queueModelChanged();
    emit queueTitleChanged();
}



void DebugController::refreshYaesu()
{
    if (!m_queue) return;
    m_yaesuPrev = m_yaesuData;
    m_yaesuData = m_queue->getYaesuData();
    // no dedicated signal; QML asks via invokables each frame/timer update.
    // If you want, you can emit a signal and bind a dummy property.
}

QString DebugController::yaesuHexAt(int idx) const
{
    if (idx < 0 || idx >= m_yaesuData.size()) return "";
    return m_yaesuData.mid(idx, 1).toHex();
}

bool DebugController::yaesuChangedAt(int idx) const
{
    if (idx < 0 || idx >= m_yaesuData.size()) return false;
    if (idx < 0 || idx >= m_yaesuPrev.size()) return true; // new data
    return m_yaesuData.at(idx) != m_yaesuPrev.at(idx);
}

// Keep your existing getValue() logic; paste yours here.
// I’m calling your original formatting approach.
QString DebugController::getValue(const QVariant& val) const
{
    // ---- paste your existing debugWindow::getValue() body here ----
    // (exactly as-is, just replace `val` usage accordingly)
    QString value = "";
    if (val.isValid()) {
        if (!strcmp(val.typeName(),"bool")) {
            value = (val.value<bool>() ? "True" : "False");
        } else if (!strcmp(val.typeName(),"uchar")) {
            value = QString("uchar: %0").arg(val.value<uchar>());
        } else if (!strcmp(val.typeName(),"ushort")) {
            value = QString("ushort: %0").arg(val.value<ushort>());
        } else if (!strcmp(val.typeName(),"short")) {
            value = QString("short: %0").arg(val.value<short>());
        } else if (!strcmp(val.typeName(),"uint")) {
            value = QString("Gr: %0 Me: %1").arg(val.value<uint>() >> 16 & 0xffff).arg(val.value<uint>() & 0xffff);
        } else if (!strcmp(val.typeName(),"int")) {
            value = QString("int: %0").arg(val.value<int>());
        } else if (!strcmp(val.typeName(),"double")) {
            value = QString("double: %0").arg(val.value<double>());
        }
        // ...continue the rest of your big chain...
    }
    return value;
}
