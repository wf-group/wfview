#include "cwserialkeyer.h"

#include "commhandler.h"
#include "logcategories.h"
#include "mcphooks.h"

#include <algorithm>

namespace
{
constexpr qint64 nsPerMs = 1000000;
constexpr double nsPerSecond = 1e9;
}

CwSerialKeyer::CwSerialKeyer(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &CwSerialKeyer::nextElement);
}

CwSerialKeyer::~CwSerialKeyer()
{
    close();
}

void CwSerialKeyer::configure(const QString &portName, int line)
{
    close();
    m_line = (line == keyLineRTS) ? keyLineRTS : keyLineDTR;

    if (portName.isEmpty())
        return;

    m_port = new QSerialPort(portName, this);
    if (!m_port->open(QIODevice::ReadWrite))
    {
        const QString reason = serialOpenFailureReason(m_port);
        qWarning(logCW()) << "Could not open CW key port" << portName << ":" << reason;
        delete m_port;
        m_port = nullptr;
        emit portError(tr("Could not open CW key port %1. %2").arg(portName, reason));
        MCP_CW_EVENT(QStringLiteral("keyer"), QStringLiteral("key port open failed"),
                     (QJsonObject{{"port", portName}, {"reason", reason}}));
        return;
    }
    // Some serial drivers assert both lines when a port is opened.
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    connect(m_port, &QSerialPort::errorOccurred, this, &CwSerialKeyer::handlePortError);

    qInfo(logCW()) << "CW key port" << portName << "open, keying on" << (m_line == keyLineRTS ? "RTS" : "DTR");
    emit readyChanged(true);
    publishEvent(QStringLiteral("key port opened"), {});
    publishState();
}

void CwSerialKeyer::close()
{
    stop();
    if (!m_port)
        return;

    publishEvent(QStringLiteral("key port closed"), {});
    m_port->disconnect(this);
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    m_port->close();
    delete m_port;
    m_port = nullptr;
    emit readyChanged(false);
    publishState();
}

void CwSerialKeyer::send(const QString &text)
{
    if (!m_port)
        return;

    for (const QChar ch : text)
        m_pending.append(ch.isSpace() ? QChar(' ') : ch.toUpper());

    if (m_sending || m_pending.isEmpty())
        return;

    m_sending = true;
    m_deadlineNs = 0;
    m_clock.start();
    publishEvent(QStringLiteral("message started"),
                 QJsonObject{{"chars", int(m_pending.size())}, {"wpm", m_wpm}, {"dashRatio", m_ratio}});
    nextElement();
    publishState();
}

void CwSerialKeyer::stop()
{
    if (m_sending)
        publishEvent(QStringLiteral("message stopped"), QJsonObject{{"charsDiscarded", int(m_pending.size())}});
    m_timer->stop();
    m_pending.clear();
    m_elements.clear();
    m_sending = false;
    setKey(false);
    publishState();
}

void CwSerialKeyer::setSpeed(int wpm)
{
    m_wpm = std::max(1, wpm);
    publishState();
}

void CwSerialKeyer::setRatio(double ratio)
{
    m_ratio = std::max(1.0, ratio);
    publishState();
}

void CwSerialKeyer::nextElement()
{
    while (m_elements.empty())
    {
        if (m_pending.isEmpty())
        {
            m_sending = false;
            keyEdge(false, m_deadlineNs);
            publishEvent(QStringLiteral("message finished"),
                         QJsonObject{{"lengthMs", double(m_deadlineNs) / nsPerMs},
                                     {"actualMs", double(m_clock.nsecsElapsed()) / nsPerMs}});
            publishState();
            return;
        }
        appendCwElements(m_pending.front(), m_ratio, m_elements);
        m_pending.remove(0, 1);
    }

    const CwElement element = m_elements.front();
    m_elements.pop_front();
    keyEdge(element.keyDown, m_deadlineNs);

    // Each element ends at a time measured from the start of the message, so
    // a late timer does not make the rest of the message late as well.
    m_deadlineNs += static_cast<qint64>(element.dits * cwDitSecondsAtOneWpm / m_wpm * nsPerSecond);
    const qint64 waitMs = (m_deadlineNs - m_clock.nsecsElapsed() + nsPerMs / 2) / nsPerMs;
    m_timer->start(static_cast<int>(std::max<qint64>(0, waitMs)));
}

void CwSerialKeyer::setKey(bool down)
{
    if (!m_port)
        return;

    if (m_line == keyLineRTS)
        m_port->setRequestToSend(down);
    else
        m_port->setDataTerminalReady(down);
}

void CwSerialKeyer::keyEdge(bool down, qint64 dueNs)
{
#ifdef WFVIEW_MCP
    const qint64 startNs = m_clock.nsecsElapsed();
    setKey(down);
    MCP_CW_KEY_EDGE(down, dueNs, startNs, m_clock.nsecsElapsed());
#else
    Q_UNUSED(dueNs)
    setKey(down);
#endif
}

void CwSerialKeyer::publishState()
{
    MCP_STATE(QStringLiteral("cwKeyer"),
              (QJsonObject{{"port", m_port ? m_port->portName() : QString()},
                           {"open", m_port != nullptr},
                           {"line", m_line == keyLineRTS ? QStringLiteral("RTS") : QStringLiteral("DTR")},
                           {"sending", m_sending},
                           {"charsWaiting", int(m_pending.size())},
                           {"wpm", m_wpm},
                           {"dashRatio", m_ratio}}));
}

void CwSerialKeyer::publishEvent(const QString &what, const QJsonObject &detail)
{
    Q_UNUSED(what)
    Q_UNUSED(detail)
    MCP_CW_EVENT(QStringLiteral("keyer") + (m_port ? QStringLiteral(" ") + m_port->portName() : QString()), what,
                 detail);
}

void CwSerialKeyer::handlePortError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port)
        return;

    const QString message = tr("CW key port %1 failed: %2").arg(m_port->portName(), m_port->errorString());
    qWarning(logCW()) << message;
    close();
    emit portError(message);
}
