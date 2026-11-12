#include "mcphost.h"

#include "mcpprobe.h"

#include <QJsonArray>

McpHost::McpHost(QObject *parent)
    : QObject(parent)
{
    m_lastBeatMs = McpProbe::uptimeMs();
    m_heartbeatTimer.setInterval(heartbeatIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &McpHost::heartbeat);
    m_heartbeatTimer.start();

    m_transmitTimer.setSingleShot(true);
    connect(&m_transmitTimer, &QTimer::timeout, this, &McpHost::transmitTimeLimit);
}

McpHostResult McpHost::settingOptions()
{
    McpHostResult r;
    r.error = QStringLiteral("%1 has no settings that can be changed by name.").arg(appName());
    return r;
}

QJsonObject McpHost::windows()
{
    return QJsonObject{{"windows", QJsonArray()},
                       {"note", QStringLiteral("%1 has no windows.").arg(appName())}};
}

McpHostResult McpHost::setOption(const QString &, const QJsonValue &, bool)
{
    McpHostResult r;
    r.error = QStringLiteral("%1 cannot change settings through MCP.").arg(appName());
    return r;
}

McpHostResult McpHost::setTransmit(bool)
{
    McpHostResult r;
    r.error = QStringLiteral("%1 cannot key the transmitter through MCP.").arg(appName());
    return r;
}

QJsonObject McpHost::radioServer()
{
    return QJsonObject{{"available", false},
                       {"note", QStringLiteral("%1 does not report radio server status through MCP.").arg(appName())}};
}

McpHostResult McpHost::setServerUserAccess(const QString &, const QString &, bool)
{
    McpHostResult r;
    r.error = QStringLiteral("%1 cannot change radio server users through MCP.").arg(appName());
    return r;
}

McpHostResult McpHost::transmitFor(bool on, int maxSeconds)
{
    McpHostResult r = setTransmit(on);
    if (!r.ok)
        return r;

    if (on)
    {
        m_transmitTimer.start(maxSeconds * 1000);
        r.data["unkeyAfterSeconds"] = maxSeconds;
    }
    else
    {
        m_transmitTimer.stop();
    }
    return r;
}

void McpHost::endMcpTransmit()
{
    if (!m_transmitTimer.isActive())
        return;
    qCWarning(logMcp) << "Unkeying the transmitter: MCP is stopping";
    m_transmitTimer.stop();
    setTransmit(false);
}

void McpHost::transmitEnded()
{
    m_transmitTimer.stop();
}

void McpHost::transmitTimeLimit()
{
    qCWarning(logMcp) << "Unkeying the transmitter: the MCP transmit time limit ran out";
    setTransmit(false);
}

void McpHost::heartbeat()
{
    const qint64 now = McpProbe::uptimeMs();
    const qint64 delay = now - m_lastBeatMs.load(std::memory_order_relaxed) - heartbeatIntervalMs;
    m_lastBeatMs.store(now, std::memory_order_relaxed);
    if (delay > m_worstDelayMs.load(std::memory_order_relaxed))
        m_worstDelayMs.store(delay, std::memory_order_relaxed);
}

qint64 McpHost::msSinceHeartbeat() const
{
    return McpProbe::uptimeMs() - m_lastBeatMs.load(std::memory_order_relaxed);
}

qint64 McpHost::worstHeartbeatDelayMs() const
{
    return m_worstDelayMs.load(std::memory_order_relaxed);
}

void McpHost::resetWorstHeartbeatDelay()
{
    m_worstDelayMs.store(0, std::memory_order_relaxed);
}
