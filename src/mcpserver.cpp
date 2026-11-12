#include "mcpserver.h"

#include "cachingqueue.h"
#include "mcphost.h"
#include "mcpprobe.h"
#include "mcptools.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <atomic>

namespace
{

constexpr int maxHeaderBytes = 32 * 1024;
constexpr qint64 maxBodyBytes = 1024 * 1024;
constexpr int maxConnections = 16;

// Newest first; an unknown requested version gets the newest.
const QStringList supportedProtocolVersions = {QStringLiteral("2025-06-18"),
                                               QStringLiteral("2025-03-26"),
                                               QStringLiteral("2024-11-05")};

struct HttpRequest
{
    QByteArray method;
    QByteArray path;
    QByteArray version;
    QHash<QByteArray, QByteArray> headers; // names in lower case
    QByteArray body;
};

enum class ParseResult
{
    Incomplete,
    Complete,
    Invalid
};

QByteArray statusText(int status)
{
    switch (status)
    {
    case 200:
        return "OK";
    case 202:
        return "Accepted";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 413:
        return "Payload Too Large";
    case 431:
        return "Request Header Fields Too Large";
    case 501:
        return "Not Implemented";
    }
    return "Error";
}

// Compares without stopping at the first difference, so response timing does
// not reveal how much of a guessed token was right.
bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

bool parseBindAddress(const QString &text, QHostAddress *address)
{
    const QString bind = text.trimmed();
    if (bind.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
    {
        *address = QHostAddress(QHostAddress::LocalHost);
        return true;
    }
    return address->setAddress(bind);
}

ParseResult parseRequest(QByteArray &buffer, HttpRequest *request, int *status)
{
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0)
    {
        if (buffer.size() <= maxHeaderBytes)
            return ParseResult::Incomplete;
        *status = 431;
        return ParseResult::Invalid;
    }
    if (headerEnd > maxHeaderBytes)
    {
        *status = 431;
        return ParseResult::Invalid;
    }

    const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    if (requestLine.size() != 3)
    {
        *status = 400;
        return ParseResult::Invalid;
    }
    request->method = requestLine.at(0);
    request->path = requestLine.at(1).split('?').value(0);
    request->version = requestLine.at(2);

    for (int i = 1; i < lines.size(); ++i)
    {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        request->headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }

    const QByteArray transferEncoding = request->headers.value("transfer-encoding").toLower();
    if (!transferEncoding.isEmpty() && transferEncoding != "identity")
    {
        *status = 501;
        return ParseResult::Invalid;
    }

    bool ok = true;
    const qint64 contentLength = request->headers.value("content-length", "0").toLongLong(&ok);
    if (!ok || contentLength < 0)
    {
        *status = 400;
        return ParseResult::Invalid;
    }
    if (contentLength > maxBodyBytes)
    {
        *status = 413;
        return ParseResult::Invalid;
    }

    const qint64 total = headerEnd + 4 + contentLength;
    if (buffer.size() < total)
        return ParseResult::Incomplete;

    request->body = buffer.mid(headerEnd + 4, int(contentLength));
    buffer.remove(0, int(total));
    return ParseResult::Complete;
}

QJsonObject rpcResult(const QJsonValue &id, const QJsonValue &result)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject rpcError(const QJsonValue &id, int code, const QString &message)
{
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QJsonObject textContent(const QString &text)
{
    return QJsonObject{{"type", "text"}, {"text", text}};
}

} // namespace

// Lives on the MCP thread. It needs no slots of its own, so no Q_OBJECT.
class McpWorker : public QObject
{
public:
    McpWorker(McpServer *owner,
              const McpOptions &options,
              const QHostAddress &address,
              McpHost *host,
              const McpAppInfo &info)
        : m_owner(owner)
        , m_options(options)
        , m_address(address)
        , m_info(info)
        , m_tools(host, info, &stopping)
    {
    }

    bool listen(QString *error);
    void shutdown();

    std::atomic_bool stopping{false};

private:
    void acceptConnections();
    void readSocket(QTcpSocket *socket);
    bool handleHttp(QTcpSocket *socket, const HttpRequest &request);
    void handlePost(QTcpSocket *socket, const HttpRequest &request, bool keepAlive);
    void send(QTcpSocket *socket,
              int status,
              const QByteArray &body,
              bool keepAlive,
              const QByteArray &extraHeaders = QByteArray());
    void sendJson(QTcpSocket *socket, int status, const QJsonValue &json, bool keepAlive);
    bool authorized(const HttpRequest &request) const;
    bool originAllowed(const HttpRequest &request) const;
    QJsonValue handleMessage(const QJsonValue &message);
    QJsonObject initializeResult(const QJsonObject &params) const;
    QJsonObject callTool(const QJsonObject &params, const QJsonValue &id);
    void reportClients();

    struct Client
    {
        QString name;
        int openConnections = 0;
        quint64 requests = 0;
        QDateTime lastSeen;
    };

    McpServer *m_owner;
    McpOptions m_options;
    QHostAddress m_address;
    McpAppInfo m_info;
    McpTools m_tools;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QHash<QTcpSocket *, QString> m_peers;
    QHash<QString, Client> m_clients; // by peer address
    QString m_currentPeer;            // peer of the request being handled
};

bool McpWorker::listen(QString *error)
{
    m_server = new QTcpServer(this);
    m_server->setMaxPendingConnections(maxConnections);
    connect(m_server, &QTcpServer::newConnection, this, &McpWorker::acceptConnections);
    if (!m_server->listen(m_address, m_options.port))
    {
        *error = QStringLiteral("Could not listen on %1 port %2: %3")
                     .arg(m_address.toString())
                     .arg(m_options.port)
                     .arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }
    return true;
}

void McpWorker::shutdown()
{
    // Deleting the server also deletes its sockets.
    m_buffers.clear();
    m_peers.clear();
    delete m_server;
    m_server = nullptr;
    m_clients.clear();
}

void McpWorker::reportClients()
{
    QVariantList clients;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it)
    {
        QVariantMap c;
        c["address"] = it.key();
        c["name"] = it->name;
        c["openConnections"] = it->openConnections;
        c["requests"] = double(it->requests);
        c["lastSeen"] = it->lastSeen.toString(QStringLiteral("hh:mm:ss"));
        clients.append(c);
    }
    McpServer *owner = m_owner;
    // Queued to the main thread; dropped if the server is gone by then.
    QMetaObject::invokeMethod(
        owner, [owner, clients]() { emit owner->clientsChanged(clients); }, Qt::QueuedConnection);
}

void McpWorker::acceptConnections()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (m_buffers.size() >= maxConnections)
        {
            qCWarning(logMcp) << "Refusing MCP connection from" << socket->peerAddress().toString()
                              << "- too many open connections";
            socket->abort();
            socket->deleteLater();
            continue;
        }

        const QString peer = socket->peerAddress().toString();
        m_buffers.insert(socket, QByteArray());
        m_peers.insert(socket, peer);
        Client &client = m_clients[peer];
        ++client.openConnections;
        client.lastSeen = QDateTime::currentDateTime();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            auto client = m_clients.find(m_peers.take(socket));
            if (client != m_clients.end() && client->openConnections > 0)
            {
                --client->openConnections;
                reportClients();
            }
            socket->deleteLater();
        });
        qCDebug(logMcp) << "MCP connection from" << peer;
        reportClients();
    }
}

void McpWorker::readSocket(QTcpSocket *socket)
{
    auto it = m_buffers.find(socket);
    if (it == m_buffers.end())
        return;
    QByteArray &buffer = it.value();
    buffer.append(socket->readAll());

    // Nothing below returns to the event loop, so the buffer reference stays
    // valid until a disconnect is requested.
    while (socket->state() == QAbstractSocket::ConnectedState)
    {
        HttpRequest request;
        int status = 400;
        const ParseResult result = parseRequest(buffer, &request, &status);
        if (result == ParseResult::Incomplete)
            return;
        if (result == ParseResult::Invalid)
        {
            send(socket, status, QByteArray(), false);
            socket->disconnectFromHost();
            return;
        }
        if (!handleHttp(socket, request))
        {
            socket->disconnectFromHost();
            return;
        }
    }
}

bool McpWorker::handleHttp(QTcpSocket *socket, const HttpRequest &request)
{
    const QByteArray connection = request.headers.value("connection").toLower();
    const bool keepAlive = request.version == "HTTP/1.1" ? connection != "close" : connection == "keep-alive";

    if (request.path != "/mcp" && request.path != "/")
    {
        send(socket, 404, QByteArray(), keepAlive);
        return keepAlive;
    }
    if (!authorized(request))
    {
        qCWarning(logMcp) << "Rejected MCP request without a valid token from" << socket->peerAddress().toString();
        send(socket, 401, QByteArray(), false, "WWW-Authenticate: Bearer realm=\"wfview\"\r\n");
        return false;
    }
    if (!originAllowed(request))
    {
        qCWarning(logMcp) << "Rejected MCP request from origin" << request.headers.value("origin");
        send(socket, 403, QByteArray(), false);
        return false;
    }
    if (request.method != "POST")
    {
        // No server-to-client event stream is offered, so only POST is used.
        send(socket, 405, QByteArray(), keepAlive, "Allow: POST\r\n");
        return keepAlive;
    }

    handlePost(socket, request, keepAlive);
    return keepAlive;
}

void McpWorker::handlePost(QTcpSocket *socket, const HttpRequest &request, bool keepAlive)
{
    m_currentPeer = m_peers.value(socket);
    auto client = m_clients.find(m_currentPeer);
    if (client != m_clients.end())
    {
        ++client->requests;
        client->lastSeen = QDateTime::currentDateTime();
    }
    // Reported after the reply, which may name the client.
    struct ReportOnExit
    {
        McpWorker *worker;
        ~ReportOnExit()
        {
            worker->reportClients();
        }
    } report{this};

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        sendJson(socket, 400, rpcError(QJsonValue(), -32700, "Parse error: " + parseError.errorString()), keepAlive);
        return;
    }

    QJsonValue response;
    if (doc.isArray())
    {
        QJsonArray replies;
        for (const QJsonValue &message : doc.array())
        {
            const QJsonValue reply = handleMessage(message);
            if (!reply.isUndefined())
                replies.append(reply);
        }
        if (!replies.isEmpty())
            response = replies;
        else
            response = QJsonValue(QJsonValue::Undefined);
    }
    else if (doc.isObject())
    {
        response = handleMessage(doc.object());
    }
    else
    {
        sendJson(socket, 400, rpcError(QJsonValue(), -32600, "Invalid Request"), keepAlive);
        return;
    }

    // Notifications and client responses get no body.
    if (response.isUndefined())
        send(socket, 202, QByteArray(), keepAlive);
    else
        sendJson(socket, 200, response, keepAlive);
}

void McpWorker::send(QTcpSocket *socket,
                     int status,
                     const QByteArray &body,
                     bool keepAlive,
                     const QByteArray &extraHeaders)
{
    QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
    if (!body.isEmpty())
        head += "Content-Type: application/json\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    head += extraHeaders;
    head += "\r\n";
    socket->write(head);
    if (!body.isEmpty())
        socket->write(body);
}

void McpWorker::sendJson(QTcpSocket *socket, int status, const QJsonValue &json, bool keepAlive)
{
    const QJsonDocument doc = json.isArray() ? QJsonDocument(json.toArray()) : QJsonDocument(json.toObject());
    send(socket, status, doc.toJson(QJsonDocument::Compact), keepAlive);
}

bool McpWorker::authorized(const HttpRequest &request) const
{
    if (m_options.token.isEmpty())
        return true;
    return constantTimeEquals(request.headers.value("authorization"), "Bearer " + m_options.token.toUtf8());
}

// Browsers send Origin; refusing other sites stops a web page from reaching a
// local, token-less server through DNS rebinding.
bool McpWorker::originAllowed(const HttpRequest &request) const
{
    const QByteArray origin = request.headers.value("origin");
    if (origin.isEmpty() || !m_options.token.isEmpty())
        return true;
    const QString host = QUrl(QString::fromLatin1(origin)).host();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1");
}

QJsonValue McpWorker::handleMessage(const QJsonValue &message)
{
    if (!message.isObject())
        return rpcError(QJsonValue(), -32600, "Invalid Request");

    const QJsonObject obj = message.toObject();
    const bool hasId = obj.contains("id");
    const QJsonValue id = obj.value("id");
    const QString method = obj.value("method").toString();

    if (method.isEmpty())
    {
        // A response to a server request; this server sends none.
        if (hasId && !obj.contains("result") && !obj.contains("error"))
            return rpcError(id, -32600, "Invalid Request");
        return QJsonValue(QJsonValue::Undefined);
    }
    if (!hasId)
    {
        qCDebug(logMcp) << "MCP notification" << method;
        return QJsonValue(QJsonValue::Undefined);
    }

    const QJsonObject params = obj.value("params").toObject();
    if (method == QLatin1String("initialize"))
    {
        auto client = m_clients.find(m_currentPeer);
        if (client != m_clients.end())
        {
            const QJsonObject info = params.value("clientInfo").toObject();
            client->name =
                QStringLiteral("%1 %2").arg(info.value("name").toString(), info.value("version").toString()).trimmed();
        }
        return rpcResult(id, initializeResult(params));
    }
    if (method == QLatin1String("ping"))
        return rpcResult(id, QJsonObject());
    if (method == QLatin1String("tools/list"))
        return rpcResult(id, QJsonObject{{"tools", m_tools.list()}});
    if (method == QLatin1String("tools/call"))
        return callTool(params, id);

    return rpcError(id, -32601, "Method not found: " + method);
}

QJsonObject McpWorker::initializeResult(const QJsonObject &params) const
{
    const QString requested = params.value("protocolVersion").toString();
    const QString version =
        supportedProtocolVersions.contains(requested) ? requested : supportedProtocolVersions.first();
    const QJsonObject client = params.value("clientInfo").toObject();
    qCInfo(logMcp) << "MCP client initialized:" << client.value("name").toString()
                   << client.value("version").toString() << "protocol" << version << "from" << m_currentPeer;

    const QString instructions =
        QStringLiteral("Live diagnostics from %1, an amateur radio control program, while it runs. Call get_overview "
                       "first. The server is %2. Passwords in settings and connection details are masked. When "
                       "audio is missing, compare get_audio_config with get_audio_levels to find the stage where "
                       "levels stop. When the radio does not respond, check get_connection, get_queue, get_cache "
                       "reply times and get_log. When the interface is slow or frozen, use "
                       "get_main_thread_health.%3")
            .arg(m_info.appName,
                 m_info.allowWrite ? QStringLiteral("read-write") : QStringLiteral("read-only"),
                 m_info.allowWrite
                     ? QStringLiteral(" The write tools act on a real radio: change only what the user asked "
                                      "for, never transmit (set_ptt) unless asked to, and read the result back "
                                      "with get_cache afterwards. Prefer wfview's own set and read tools to "
                                      "send_raw, since they also test wfview.")
                     : QString());

    return QJsonObject{
        {"protocolVersion", version},
        {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
        {"serverInfo",
         QJsonObject{{"name", m_info.appName}, {"title", m_info.appName + " MCP"}, {"version", WFVIEW_VERSION}}},
        {"instructions", instructions}};
}

QJsonObject McpWorker::callTool(const QJsonObject &params, const QJsonValue &id)
{
    const QString name = params.value("name").toString();
    const QJsonObject args = params.value("arguments").toObject();

    QElapsedTimer timer;
    timer.start();
    McpToolResult result;
    if (!m_tools.call(name, args, &result))
        return rpcError(id, -32602, "Unknown tool: " + name);
    qCDebug(logMcp) << "MCP tool" << name << "took" << timer.elapsed() << "ms";

    QJsonObject reply;
    if (!result.error.isEmpty())
    {
        reply["content"] = QJsonArray{textContent(result.error)};
        reply["isError"] = true;
    }
    else
    {
        const QString text = QString::fromUtf8(QJsonDocument(result.data).toJson(QJsonDocument::Compact));
        reply["content"] = QJsonArray{textContent(text)};
        reply["isError"] = false;
    }
    return rpcResult(id, reply);
}

McpServer::McpServer(const McpOptions &options, McpHost *host, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_host(host)
{
}

McpServer::~McpServer()
{
    stop();
}

bool McpServer::start(QString *error)
{
    if (m_worker || !m_host)
        return false;

    QHostAddress address;
    if (!parseBindAddress(m_options.bindAddress, &address))
    {
        *error = QStringLiteral("Invalid MCP bind address \"%1\".").arg(m_options.bindAddress);
        return false;
    }
    if (!address.isLoopback() && m_options.token.isEmpty())
    {
        *error = QStringLiteral("MCP would listen on %1, which other machines can reach. Set a token with "
                                "--mcp-token or the WFVIEW_MCP_TOKEN environment variable.")
                     .arg(address.toString());
        return false;
    }

    McpAppInfo info;
    info.appName = m_host->appName();
    info.settingsFileName = m_host->settingsFileName();
    info.settingsFormat = m_host->settingsFormat();
    info.bindAddress = address.toString();
    info.port = m_options.port;
    info.tokenRequired = !m_options.token.isEmpty();
    info.allowWrite = m_options.allowWrite;
    // The application created the queue already; asking here, on the main
    // thread, never creates one on the MCP thread.
    info.queue = cachingQueue::getInstance();

    m_worker = new McpWorker(this, m_options, address, m_host, info);
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("mcpServer()"));
    // Below normal priority, so MCP never competes with audio or the GUI.
    m_thread.start(QThread::LowPriority);

    bool listening = false;
    QString listenError;
    McpWorker *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, &listening, &listenError]() { listening = worker->listen(&listenError); },
        Qt::BlockingQueuedConnection);
    if (!listening)
    {
        stop();
        *error = listenError;
        return false;
    }

    qCInfo(logMcp).noquote() << QStringLiteral("MCP server listening on http://%1:%2/mcp (%3%4)")
                                    .arg(address.protocol() == QAbstractSocket::IPv6Protocol
                                             ? QLatin1Char('[') + address.toString() + QLatin1Char(']')
                                             : address.toString())
                                    .arg(m_options.port)
                                    .arg(m_options.allowWrite ? QStringLiteral("read-write")
                                                              : QStringLiteral("read-only"))
                                    .arg(m_options.token.isEmpty() ? QString() : QStringLiteral(", token required"));
    return true;
}

void McpServer::stop()
{
    if (!m_worker)
        return;

    // Set first: a tool waiting on the main thread gives up within 50 ms,
    // so the blocking call below cannot deadlock.
    m_worker->stopping.store(true);
    McpWorker *worker = m_worker;
    if (m_thread.isRunning())
        QMetaObject::invokeMethod(worker, [worker]() { worker->shutdown(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_worker;
    m_worker = nullptr;
    qCInfo(logMcp) << "MCP server stopped";
}
