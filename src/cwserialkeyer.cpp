#include "cwserialkeyer.h"

#include "commhandler.h"
#include "logcategories.h"

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
        return;
    }
    // Some serial drivers assert both lines when a port is opened.
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    connect(m_port, &QSerialPort::errorOccurred, this, &CwSerialKeyer::handlePortError);

    qInfo(logCW()) << "CW key port" << portName << "open, keying on" << (m_line == keyLineRTS ? "RTS" : "DTR");
    emit readyChanged(true);
}

void CwSerialKeyer::close()
{
    stop();
    if (!m_port)
        return;

    m_port->disconnect(this);
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    m_port->close();
    delete m_port;
    m_port = nullptr;
    emit readyChanged(false);
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
    nextElement();
}

void CwSerialKeyer::stop()
{
    m_timer->stop();
    m_pending.clear();
    m_elements.clear();
    m_sending = false;
    setKey(false);
}

void CwSerialKeyer::setSpeed(int wpm)
{
    m_wpm = std::max(1, wpm);
}

void CwSerialKeyer::setRatio(double ratio)
{
    m_ratio = std::max(1.0, ratio);
}

void CwSerialKeyer::nextElement()
{
    while (m_elements.empty())
    {
        if (m_pending.isEmpty())
        {
            m_sending = false;
            setKey(false);
            return;
        }
        appendCwElements(m_pending.front(), m_ratio, m_elements);
        m_pending.remove(0, 1);
    }

    const CwElement element = m_elements.front();
    m_elements.pop_front();
    setKey(element.keyDown);

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

void CwSerialKeyer::handlePortError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || !m_port)
        return;

    const QString message = tr("CW key port %1 failed: %2").arg(m_port->portName(), m_port->errorString());
    qWarning(logCW()) << message;
    close();
    emit portError(message);
}
