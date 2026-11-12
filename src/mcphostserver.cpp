#include "mcphostserver.h"

#include "mcptools.h"
#include "servermain.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QStandardPaths>

namespace
{

// Matches servermain::setManufacturer().
QString systemRigLocation()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return QCoreApplication::applicationDirPath() + "/../Resources/rigs";
#elif defined(Q_OS_LINUX)
    return QStringLiteral(PREFIX "/share/wfview/rigs");
#else
    return QCoreApplication::applicationDirPath() + "/rigs";
#endif
}

QJsonObject dirJson(const QString &path)
{
    const QDir dir(path);
    return QJsonObject{{"path", dir.absolutePath()},
                       {"exists", dir.exists()},
                       {"rigFiles", int(dir.entryList({"*.rig", "*.RIG"}, QDir::Files).size())}};
}

const QJsonObject goneError{{"error", "wfserver is shutting down"}};

} // namespace

McpHostServer::McpHostServer(servermain *server, QObject *parent)
    : McpHost(parent)
    , m_server(server)
{
}

QString McpHostServer::appName() const
{
    return QStringLiteral("wfserver");
}

QString McpHostServer::settingsFileName() const
{
    return m_server && m_server->settings ? m_server->settings->fileName() : QString();
}

QSettings::Format McpHostServer::settingsFormat() const
{
    return m_server && m_server->settings ? m_server->settings->format() : QSettings::IniFormat;
}

QJsonObject McpHostServer::connectionInfo()
{
    if (!m_server)
        return goneError;

    const SERVERCONFIG &config = m_server->serverConfig;
    QJsonArray users;
    for (const SERVERUSER &user : config.users)
        users.append(QJsonObject{{"username", user.username},
                                 {"password", Mcp::maskSecret(user.password)},
                                 {"userType", int(user.userType)}});

    QJsonArray radios;
    for (int i = 0; i < config.rigs.size(); ++i)
    {
        const RIGCONFIG *radio = config.rigs.at(i);
        if (!radio)
            continue;
        radios.append(QJsonObject{{"index", i},
                                  {"rigName", radio->rigName},
                                  {"modelName", radio->modelName},
                                  {"serialPort", radio->serialPort},
                                  {"baudRate", double(radio->baudRate)},
                                  {"radioId", Mcp::radioIdJson(m_server->prefs.manufacturer, radio->civAddr)},
                                  {"civIsRadioModel", radio->civIsRadioModel},
                                  {"pttType", int(radio->pttType)},
                                  {"rigAvailable", radio->rigAvailable},
                                  {"rigRunning", radio->rig != nullptr},
                                  {"hasEthernet", radio->hasEthernet},
                                  {"hasWiFi", radio->hasWiFi},
                                  {"waterfallFormat", int(radio->waterfallFormat)},
                                  {"queueIntervalMs", double(radio->queueInterval)}});
    }

    const auto &p = m_server->prefs;
    QJsonObject d;
    d["manufacturer"] = Mcp::manufacturerName(p.manufacturer);
    d["server"] = QJsonObject{{"enabled", config.enabled},
                              {"lan", config.lan},
                              {"listenAddress", config.listenAddress},
                              {"controlPort", int(config.controlPort)},
                              {"civPort", int(config.civPort)},
                              {"audioPort", int(config.audioPort)},
                              {"scopePort", int(config.scopePort)},
                              {"users", users}};
    d["radios"] = radios;
    d["wfshare"] = QJsonObject{{"enabled", p.wfShareEnabled},
                               {"host", p.wfShareHost},
                               {"port", int(p.wfSharePort)},
                               {"username", p.wfShareUsername},
                               {"password", Mcp::maskSecret(p.wfSharePassword)},
                               {"directEnabled", p.wfShareDirectEnabled},
                               {"directPort", int(p.wfShareDirectPort)},
                               {"clientConnected", m_server->wfShareClientConnected}};
    return d;
}

QJsonObject McpHostServer::audioConfig()
{
    if (!m_server)
        return goneError;

    const SERVERCONFIG &config = m_server->serverConfig;
    QJsonArray radios;
    for (int i = 0; i < config.rigs.size(); ++i)
    {
        const RIGCONFIG *radio = config.rigs.at(i);
        if (!radio)
            continue;
        radios.append(QJsonObject{{"index", i},
                                  {"rigName", radio->rigName},
                                  {"rx", Mcp::audioSetupToJson(radio->rxAudioSetup)},
                                  {"tx", Mcp::audioSetupToJson(radio->txAudioSetup)},
                                  {"rxHandlerOpen", radio->rxaudio != nullptr},
                                  {"txHandlerOpen", radio->txaudio != nullptr}});
    }

    QJsonObject d;
    d["audioSystem"] = Mcp::audioTypeName(int(m_server->prefs.audioSystem));
    d["resampleQuality"] = int(config.resampleQuality);
    d["radios"] = radios;
    return d;
}

QJsonObject McpHostServer::rigFiles()
{
    if (!m_server)
        return goneError;

    QJsonArray rigs;
    const int manufacturer = m_server->prefs.manufacturer;
    for (auto it = m_server->rigList.cbegin(); it != m_server->rigList.cend(); ++it)
    {
        const rigInfo &info = it.value();
        rigs.append(QJsonObject{{"radioId", Mcp::radioIdJson(manufacturer, info.civ)},
                                {"model", info.model},
                                {"version", double(info.version)},
                                {"path", info.path}});
    }

    QJsonObject d;
    d["manufacturer"] = Mcp::manufacturerName(m_server->prefs.manufacturer);
    d["systemRigFolder"] = dirJson(systemRigLocation());
    d["userRigFolder"] = dirJson(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/rigs");
    d["rigs"] = rigs;
    return d;
}

QJsonObject McpHostServer::appState()
{
    if (!m_server)
        return goneError;

    QJsonArray radios;
    const SERVERCONFIG &config = m_server->serverConfig;
    for (int i = 0; i < config.rigs.size(); ++i)
    {
        const RIGCONFIG *radio = config.rigs.at(i);
        if (!radio)
            continue;
        radios.append(QJsonObject{{"index", i},
                                  {"rigName", radio->rigName},
                                  {"modelName", radio->modelName},
                                  {"rigAvailable", radio->rigAvailable},
                                  {"haveRigCaps", radio->rigCaps != nullptr}});
    }

    return QJsonObject{{"transmitting", m_server->amTransmitting},
                       {"lastMessage", m_server->lastMessage},
                       {"wfshareClientConnected", m_server->wfShareClientConnected},
                       {"radios", radios}};
}

bool McpHostServer::rigCaps(int radio, rigCapabilities *out, QString *error)
{
    if (!m_server)
    {
        *error = QStringLiteral("wfserver is shutting down.");
        return false;
    }

    const QList<RIGCONFIG *> &rigs = m_server->serverConfig.rigs;
    if (radio < 0 || radio >= rigs.size() || !rigs.at(radio))
    {
        *error = QStringLiteral("There is no radio %1; wfserver has %2.").arg(radio).arg(rigs.size());
        return false;
    }
    if (!rigs.at(radio)->rigCaps)
    {
        *error = QStringLiteral("Radio %1 has not reported its capabilities yet.").arg(radio);
        return false;
    }
    *out = *rigs.at(radio)->rigCaps;
    return true;
}

McpHostResult McpHostServer::setTransmit(bool on)
{
    McpHostResult r;
    if (!m_server || !m_server->queue)
    {
        r.error = QStringLiteral("wfserver is shutting down.");
        return r;
    }
    const rigCapabilities *caps = m_server->queue->getRigCaps();
    if (!caps || !caps->hasTransmit || !caps->commands.contains(funcTransceiverStatus))
    {
        r.error = QStringLiteral("The connected radio cannot be keyed by wfserver.");
        return r;
    }

    m_server->queue->add(priorityImmediate,
                         queueItem(funcTransceiverStatus, QVariant::fromValue<bool>(on), false, uchar(0)));
    r.ok = true;
    r.data["requested"] = on ? QStringLiteral("transmit") : QStringLiteral("receive");
    r.data["transmittingBefore"] = m_server->amTransmitting;
    return r;
}
