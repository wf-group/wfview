#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP).

#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QTimer>

#include <atomic>

struct rigCapabilities;

// The answer to a main-thread request that can fail.
struct McpHostResult
{
    bool ok = false;
    QJsonObject data;
    QString error;
};

// The part of the MCP server that lives on the application's main thread.
// wfview and wfserver each provide a subclass that reads their own state.
//
// The virtual query functions are only ever called on the main thread, through
// McpServer, so they may read the application's members directly.
class McpHost : public QObject
{
    Q_OBJECT

public:
    explicit McpHost(QObject *parent = nullptr);

    // Fixed facts, read once on the main thread before the MCP thread starts.
    virtual QString appName() const = 0;
    virtual QString settingsFileName() const = 0;
    virtual QSettings::Format settingsFormat() const = 0;

    // Main-thread queries. Passwords must already be masked.
    virtual QJsonObject connectionInfo() = 0;
    virtual QJsonObject audioConfig() = 0;
    virtual QJsonObject rigFiles() = 0;
    virtual QJsonObject appState() = 0;
    // radio is an index for wfserver, which can host several; wfview has one.
    virtual bool rigCaps(int radio, rigCapabilities *out, QString *error) = 0;

    // Settings that can be read and changed by name, with their current
    // values; secrets are masked. The default reports that there are none.
    virtual McpHostResult settingOptions();

    // Top-level windows with their sizes and positions, and the screens. The
    // default reports that the application has no windows.
    virtual QJsonObject windows();

    // Writes, only used when the MCP server allows them. The defaults report
    // that the application does not support them.
    virtual McpHostResult setOption(const QString &key, const QJsonValue &value, bool save);
    virtual McpHostResult setTransmit(bool on);

    // The built-in radio server: its state, logged-in users and accounts.
    // The default reports that the application does not report one.
    virtual QJsonObject radioServer();
    // Changes the access level of an existing radio server user; access is
    // one of admin, normal, noTx or disabled.
    virtual McpHostResult setServerUserAccess(const QString &username, const QString &access, bool save);

    // Keys or unkeys the transmitter. When keying, MCP unkeys it again after
    // maxSeconds unless a client unkeys it first.
    McpHostResult transmitFor(bool on, int maxSeconds);
    // Unkeys the transmitter if MCP keyed it and it is still keyed. Call
    // before stopping the MCP server, so its time limit is not lost with it.
    void endMcpTransmit();

    // Readable from any thread: how long since the main event loop last ran a
    // timer, and the worst delay seen since the last reset.
    qint64 msSinceHeartbeat() const;
    qint64 worstHeartbeatDelayMs() const;
    void resetWorstHeartbeatDelay();

    static constexpr int heartbeatIntervalMs = 250;

protected:
    // Subclasses call this when the radio stops transmitting for any reason,
    // so the time limit cannot unkey a later transmission it did not start.
    void transmitEnded();

private:
    void heartbeat();
    void transmitTimeLimit();

    QTimer m_heartbeatTimer;
    QTimer m_transmitTimer;
    std::atomic<qint64> m_lastBeatMs{0};
    std::atomic<qint64> m_worstDelayMs{0};
};
