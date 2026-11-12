#include "McpController.h"

#ifdef WFVIEW_MCP
#include "mcphostwfview.h"
#include "mcpprobe.h"
#include "mcpserver.h"
#else
// Without CONFIG+=mcp the real classes are not built, but std::unique_ptr
// still needs complete types to destroy m_host and m_server.
class McpHost {};
class McpServer {};
#endif

#include <QHostAddress>
#include <QStringList>
#include <QVariantMap>

McpController::McpController(MainController *mc, const Options &initial, QObject *parent)
    : QObject(parent)
    , m_mc(mc)
    , m_enabled(initial.enabled)
    , m_allowWrite(initial.allowWrite)
    , m_port(initial.port)
    , m_token(initial.token)
{
    const QString bind = initial.bindAddress.trimmed();
    if (bind.isEmpty() || bind == QLatin1String("127.0.0.1")
        || bind.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
    {
        m_bindMode = BindLocal;
    }
    else if (bind == QLatin1String("0.0.0.0"))
    {
        m_bindMode = BindAll;
    }
    else
    {
        m_bindMode = BindCustom;
        m_customBind = bind;
    }

    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(0);
    connect(&m_applyTimer, &QTimer::timeout, this, &McpController::apply);

    // Started right away rather than from the event loop, so --use-mcp
    // behaves as it did before this controller existed.
    apply();
}

McpController::~McpController()
{
    shutdown();
}

bool McpController::available() const
{
#ifdef WFVIEW_MCP
    return true;
#else
    return false;
#endif
}

bool McpController::running() const
{
    return m_server != nullptr;
}

QString McpController::bindAddress() const
{
    switch (m_bindMode)
    {
    case BindAll:
        return QStringLiteral("0.0.0.0");
    case BindCustom:
        return m_customBind.trimmed();
    default:
        return QStringLiteral("127.0.0.1");
    }
}

void McpController::setServerEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit optionsChanged();
    scheduleApply();
}

void McpController::setAllowWrite(bool on)
{
    if (m_allowWrite == on)
        return;
    m_allowWrite = on;
    emit optionsChanged();
    scheduleApply();
}

void McpController::setPort(int port)
{
    if (m_port == port)
        return;
    m_port = port;
    emit optionsChanged();
    scheduleApply();
}

void McpController::setBindMode(int mode)
{
    if (mode < BindLocal || mode > BindCustom || m_bindMode == mode)
        return;
    m_bindMode = mode;
    emit optionsChanged();
    scheduleApply();
}

void McpController::setCustomBindAddress(const QString &address)
{
    if (m_customBind == address)
        return;
    m_customBind = address;
    emit optionsChanged();
    if (m_bindMode == BindCustom)
        scheduleApply();
}

void McpController::setToken(const QString &token)
{
    if (m_token == token)
        return;
    m_token = token;
    emit optionsChanged();
    scheduleApply();
}

void McpController::shutdown()
{
    m_shuttingDown = true;
    m_applyTimer.stop();
    stopServer();
}

void McpController::scheduleApply()
{
    // Only a running server, or a request to start one, needs the restart.
    if (!m_shuttingDown && (m_enabled || running()))
        m_applyTimer.start();
}

void McpController::apply()
{
    if (m_shuttingDown)
        return;

    // Every change restarts the server: the options are fixed for its life.
    // MCP clients send each request on its own, so they carry on afterwards.
    stopServer();
    if (m_enabled)
        startServer();
    else if (available())
        setStatus(tr("Stopped"));
    else
        setStatus(tr("MCP is not available in this build"));
}

void McpController::startServer()
{
#ifdef WFVIEW_MCP
    if (m_port < 1 || m_port > 65535)
    {
        setStatus(tr("Not running: port %1 is out of range (1-65535)").arg(m_port));
        qCWarning(logMcp) << "MCP server not started: port" << m_port << "is out of range";
        return;
    }

    McpOptions options;
    options.port = quint16(m_port);
    options.bindAddress = bindAddress();
    options.token = m_token;
    options.allowWrite = m_allowWrite;

    // Hooks feed the probe only while it is enabled, so turn it on first.
    McpProbe::setEnabled(true);
    m_host = std::make_unique<McpHostWfview>(m_mc);
    m_server = std::make_unique<McpServer>(options, m_host.get());
    connect(m_server.get(), &McpServer::clientsChanged, this, &McpController::updateClients);

    QString error;
    if (!m_server->start(&error))
    {
        qCCritical(logMcp).noquote() << "MCP server not started:" << error;
        m_server.reset();
        m_host.reset();
        McpProbe::setEnabled(false);
        setStatus(tr("Not running: %1").arg(error));
        return;
    }

    QHostAddress address(options.bindAddress);
    const QString host = address.protocol() == QAbstractSocket::IPv6Protocol
                             ? QLatin1Char('[') + options.bindAddress + QLatin1Char(']')
                             : options.bindAddress;
    m_listening = tr("Listening on http://%1:%2/mcp (%3%4)")
                      .arg(host)
                      .arg(m_port)
                      .arg(m_allowWrite ? tr("read-write") : tr("read-only"))
                      .arg(m_token.isEmpty() ? QString() : tr(", token required"));
    updateClients(QVariantList());
#endif
}

void McpController::stopServer()
{
#ifdef WFVIEW_MCP
    if (!m_server)
        return;

    // A transmission MCP started would lose its time limit with the host.
    if (m_host)
        m_host->endMcpTransmit();
    // Deleting the server also drops any client report still queued from
    // its thread, so a stale list cannot arrive after this.
    m_server->stop();
    m_server.reset();
    m_host.reset();
    McpProbe::setEnabled(false);
    m_listening.clear();
    m_clients.clear();
    emit statusChanged();
#endif
}

void McpController::setStatus(const QString &status)
{
    m_status = status;
    emit statusChanged();
}

void McpController::updateClients(const QVariantList &clients)
{
    if (!running())
        return;

    m_clients = clients;
    QStringList lines{m_listening};
    if (clients.isEmpty())
        lines << tr("No clients yet");
    for (const QVariant &v : clients)
    {
        const QVariantMap c = v.toMap();
        const QString name = c.value("name").toString();
        const int open = c.value("openConnections").toInt();
        lines << tr("%1%2: %3, %n request(s), last %4", nullptr, c.value("requests").toInt())
                     .arg(c.value("address").toString(),
                          name.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(name),
                          open > 0 ? tr("%n open connection(s)", nullptr, open) : tr("idle"),
                          c.value("lastSeen").toString());
    }
    setStatus(lines.join(QLatin1Char('\n')));
}
