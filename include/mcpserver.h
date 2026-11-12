#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP).

#include <QObject>
#include <QString>
#include <QThread>
#include <QVariantList>

class McpHost;
class McpWorker;

struct McpOptions
{
    static constexpr quint16 defaultPort = 8765;

    quint16 port = defaultPort;
    QString bindAddress = QStringLiteral("127.0.0.1");
    QString token;
    bool allowWrite = false;
};

// Model Context Protocol server (Streamable HTTP transport, JSON responses).
//
// The listening socket, HTTP parsing, JSON-RPC and tool work all run on a
// dedicated thread, so a busy or frozen main thread does not stop it.
class McpServer : public QObject
{
    Q_OBJECT

public:
    McpServer(const McpOptions &options, McpHost *host, QObject *parent = nullptr);
    ~McpServer() override;

    // Call on the main thread. Returns false, with a reason, if the options
    // are unsafe or the port cannot be opened.
    bool start(QString *error);
    void stop();

signals:
    // The clients seen since the server started, one map per peer address:
    // address, name (from MCP initialize), openConnections, requests and
    // lastSeen (local time). Emitted on the main thread.
    void clientsChanged(const QVariantList &clients);

private:
    McpOptions m_options;
    McpHost *m_host;
    QThread m_thread;
    McpWorker *m_worker = nullptr;
};
