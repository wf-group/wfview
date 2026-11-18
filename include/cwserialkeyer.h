#pragma once

#include "cwmorse.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include <deque>

// Keys CW on a control line of a serial port, for radios that take a CW key on
// DTR or RTS (for example the Yaesu "PC KEYING" menu item). The port is separate
// from the radio's CAT port and carries no data.
//
// The other control line is held de-asserted while the port is open, because
// radios commonly treat it as PTT.
//
// Lives in its own thread so that element timing does not depend on the GUI.
// Call the slots through queued connections.
class CwSerialKeyer : public QObject
{
    Q_OBJECT

public:
    enum KeyLine
    {
        keyLineDTR = 0,
        keyLineRTS = 1
    };

    explicit CwSerialKeyer(QObject* parent = nullptr);
    ~CwSerialKeyer() override;

public slots:
    // An empty port name closes the port. line is a KeyLine.
    void configure(const QString& portName, int line);
    void send(const QString& text);
    // Key up and discard anything not yet sent.
    void stop();
    // Speed and dash ratio apply from the next element.
    void setSpeed(int wpm);
    void setRatio(double ratio);
    void close();

signals:
    // True while the key port is open and ready to key.
    void readyChanged(bool ready);
    void portError(const QString& message);

private:
    void nextElement();
    void setKey(bool down);
    void handlePortError(QSerialPort::SerialPortError error);

    QSerialPort* m_port = nullptr;
    KeyLine m_line = keyLineDTR;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_deadlineNs = 0; // end of the current element, on m_clock
    bool m_sending = false;
    QString m_pending;                // characters not yet keyed
    std::deque<CwElement> m_elements; // elements of the character being keyed
    int m_wpm = 20;
    double m_ratio = 3.0;
};
