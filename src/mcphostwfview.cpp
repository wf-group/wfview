#include "mcphostwfview.h"

#include "MainController.h"
#include "SettingsController.h"
#include "audiorouting.h"
#include "mcptools.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QScreen>
#include <QWindow>
#include <QStandardPaths>

namespace
{

QString connStatusName(MainController::connectionStatus_t status)
{
    switch (status)
    {
    case MainController::connDisconnected:
        return QStringLiteral("disconnected");
    case MainController::connConnecting:
        return QStringLiteral("connecting");
    case MainController::connConnected:
        return QStringLiteral("connected");
    }
    return QString::number(int(status));
}

QString pttTypeName(pttType_t ptt)
{
    switch (ptt)
    {
    case pttCIV:
        return QStringLiteral("CI-V");
    case pttRTS:
        return QStringLiteral("RTS");
    case pttDTR:
        return QStringLiteral("DTR");
    }
    return QString::number(int(ptt));
}

QString lanTypeName(connectionType_t type)
{
    switch (type)
    {
    case connectionUSB:
        return QStringLiteral("USB");
    case connectionLAN:
        return QStringLiteral("LAN");
    case connectionWiFi:
        return QStringLiteral("WiFi");
    case connectionWAN:
        return QStringLiteral("WAN");
    }
    return QString::number(int(type));
}

QString txInputName(TxAudioInput input)
{
    switch (input)
    {
    case TxAudioInput::None:
        return QStringLiteral("none");
    case TxAudioInput::LocalInput:
        return QStringLiteral("local");
    case TxAudioInput::WfShare:
        return QStringLiteral("wfshare");
    case TxAudioInput::Tci:
        return QStringLiteral("tci");
    }
    return QString::number(int(input));
}

QJsonObject dirJson(const QString &path)
{
    const QDir dir(path);
    return QJsonObject{{"path", dir.absolutePath()},
                       {"exists", dir.exists()},
                       {"rigFiles", int(dir.entryList({"*.rig", "*.RIG"}, QDir::Files).size())}};
}

} // namespace

McpHostWfview::McpHostWfview(MainController *controller, QObject *parent)
    : McpHost(parent)
    , m_mc(controller)
{
    connect(controller, &MainController::transmittingChanged, this, [this]() {
        if (m_mc && !m_mc->transmitting())
            transmitEnded();
    });
}

QString McpHostWfview::appName() const
{
    return QStringLiteral("wfview");
}

QString McpHostWfview::settingsFileName() const
{
    return m_mc && m_mc->m_settings ? m_mc->m_settings->settingsFileName() : QString();
}

QSettings::Format McpHostWfview::settingsFormat() const
{
    return m_mc && m_mc->m_settings ? m_mc->m_settings->settingsFormat() : QSettings::IniFormat;
}

QJsonObject McpHostWfview::connectionInfo()
{
    if (!m_mc || !m_mc->prefs || !m_mc->udpPrefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    const preferences *p = m_mc->prefs;
    const udpPreferences *u = m_mc->udpPrefs;

    QString transport = QStringLiteral("serial");
    if (p->wfShareEnabled)
        transport = p->wfShareDirectMode ? QStringLiteral("wfshare-direct") : QStringLiteral("wfshare");
    else if (p->enableLAN)
        transport = QStringLiteral("network");

    QJsonObject d;
    d["status"] = connStatusName(m_mc->connStatus);
    d["transport"] = transport;
    d["manufacturer"] = Mcp::manufacturerName(p->manufacturer);
    d["rigModelName"] = m_mc->m_rigModelName;
    d["radioStatusText"] = m_mc->m_radioStatusText;
    d["footerMessage"] = m_mc->m_footerMessageText;

    d["serial"] = QJsonObject{{"port", p->serialPortRadio},
                              {"baudRate", double(p->serialPortBaud)},
                              {"radioId", Mcp::radioIdJson(p->manufacturer, p->radioCIVAddr)},
                              {"civIsRadioModel", p->CIVisRadioModel},
                              {"pttType", pttTypeName(p->pttType)},
                              {"pollingMs", p->polling_ms},
                              {"virtualSerialPort", p->virtualSerialPort},
                              {"virtualSerialPortUseQueue", p->virtualSerialPortUseQueue}};

    d["network"] = QJsonObject{{"enabled", p->enableLAN},
                               {"connectionType", lanTypeName(u->connectionType)},
                               {"ipAddress", u->ipAddress},
                               {"controlPort", int(u->controlLANPort)},
                               {"serialPort", int(u->serialLANPort)},
                               {"audioPort", int(u->audioLANPort)},
                               {"scopePort", int(u->scopeLANPort)},
                               {"username", u->username},
                               {"password", Mcp::maskSecret(u->password)},
                               {"clientName", u->clientName},
                               {"halfDuplex", u->halfDuplex},
                               {"adminLogin", u->adminLogin},
                               {"waterfallFormat", int(p->waterfallFormat)}};

    d["wfshare"] = QJsonObject{{"enabled", p->wfShareEnabled},
                               {"directMode", p->wfShareDirectMode},
                               {"host", p->wfShareHost},
                               {"port", int(p->wfSharePort)},
                               {"username", p->wfShareUsername},
                               {"password", Mcp::maskSecret(p->wfSharePassword)},
                               {"calledNumber", p->wfShareCalledNumber}};

    const rigCtlD *rigCtl = m_mc->rigCtl;
    d["rigctld"] = QJsonObject{{"enabled", p->enableRigCtlD},
                               {"port", int(p->rigCtlPort)},
                               {"listening", rigCtl && rigCtl->isListening()}};
    d["tci"] = QJsonObject{{"port", int(p->tciPort)}, {"running", m_mc->tci != nullptr}};
    d["virtualSerialTcpPort"] = int(p->tcpPort);

    d["cluster"] = QJsonObject{{"udpEnabled", p->clusterUdpEnable},
                               {"udpPort", p->clusterUdpPort},
                               {"tcpEnabled", p->clusterTcpEnable},
                               {"tcpServer", p->clusterTcpServerName},
                               {"tcpPort", p->clusterTcpPort},
                               {"tcpUser", p->clusterTcpUserName},
                               {"tcpPassword", Mcp::maskSecret(p->clusterTcpPassword)}};

    if (const SERVERCONFIG *server = m_mc->m_settings->getServerConfig())
    {
        QJsonArray users;
        for (const SERVERUSER &user : server->users)
            users.append(QJsonObject{{"username", user.username},
                                     {"password", Mcp::maskSecret(user.password)},
                                     {"userType", int(user.userType)}});
        d["server"] = QJsonObject{{"enabled", server->enabled},
                                  {"listenAddress", server->listenAddress},
                                  {"controlPort", int(server->controlPort)},
                                  {"civPort", int(server->civPort)},
                                  {"audioPort", int(server->audioPort)},
                                  {"scopePort", int(server->scopePort)},
                                  {"users", users}};
    }
    return d;
}

QJsonObject McpHostWfview::audioConfig()
{
    if (!m_mc || !m_mc->prefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    const preferences *p = m_mc->prefs;
    const txAudioProcessingPrefs &tx = p->txAudioProc;
    const rxAudioProcessingPrefs &rx = p->rxAudioProc;

    QJsonObject d;
    d["audioSystem"] = Mcp::audioTypeName(int(p->audioSystem));
    d["route"] = audioRouteDebugString(m_mc->currentAudioRoute());
    d["activeTxAudioSource"] = txInputName(m_mc->m_activeTxAudioSource);
    d["localRx"] = Mcp::audioSetupToJson(p->rxSetup);
    d["localTx"] = Mcp::audioSetupToJson(p->txSetup);
    d["usbAudioEnabled"] = p->enableUsbAudio;
    d["usbRadioRx"] = Mcp::audioSetupToJson(p->usbRxSetup);
    d["usbRadioTx"] = Mcp::audioSetupToJson(p->usbTxSetup);
    d["openHandlers"] = QJsonObject{{"usbRadioRx", m_mc->usbRadioRxAudio != nullptr},
                                    {"usbRxOutput", m_mc->usbRxOutputAudio != nullptr},
                                    {"tciRxOutput", m_mc->tciRxOutputAudio != nullptr},
                                    {"usbTxInput", m_mc->usbTxInputAudio != nullptr},
                                    {"usbRadioTx", m_mc->usbRadioTxAudio != nullptr},
                                    {"tciTxInput", m_mc->tciTxInputAudio != nullptr}};
    d["gains"] = QJsonObject{{"localAfGain", int(p->localAFgain)},
                             {"rxMuted", m_mc->m_rxMuted},
                             {"monitorEnabled", m_mc->m_monitorEnabled},
                             {"monitorGain", m_mc->m_monitorGain},
                             {"modGainLabel", m_mc->m_modGainLabel},
                             {"modGain", m_mc->m_micGain},
                             {"txPower", m_mc->m_txPower}};
    d["displayedLevels"] = QJsonObject{{"rxAudioLevel", m_mc->m_rxAudioLevel},
                                       {"txAudioLevel", m_mc->m_txAudioLevel},
                                       {"txProcessorInput", m_mc->m_txAudioInputLevel},
                                       {"txProcessorOutput", m_mc->m_txAudioOutputLevel},
                                       {"txGainReduction", m_mc->m_txAudioGainReduction},
                                       {"rxProcessorInput", m_mc->m_rxAudioInputLevel},
                                       {"rxProcessorOutput", m_mc->m_rxAudioOutputLevel}};
    d["txProcessing"] = QJsonObject{{"bypass", tx.bypass},
                                    {"compressor", tx.compEnabled},
                                    {"equalizer", tx.eqEnabled},
                                    {"noiseGate", tx.gateEnabled},
                                    {"inputGainDB", double(tx.inputGainDB)},
                                    {"outputGainDB", double(tx.outputGainDB)}};
    d["rxProcessing"] = QJsonObject{{"bypass", rx.bypass},
                                    {"noiseReduction", rx.nrEnabled},
                                    {"noiseReductionMode", int(rx.nrMode)},
                                    {"equalizer", rx.eqEnabled},
                                    {"outputGainDB", double(rx.outputGainDB)}};
    return d;
}

QJsonObject McpHostWfview::rigFiles()
{
    if (!m_mc || !m_mc->prefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    QJsonArray rigs;
    const int manufacturer = m_mc->prefs->manufacturer;
    for (auto it = m_mc->rigList.cbegin(); it != m_mc->rigList.cend(); ++it)
    {
        const rigInfo &info = it.value();
        rigs.append(QJsonObject{{"radioId", Mcp::radioIdJson(manufacturer, info.civ)},
                                {"model", info.model},
                                {"version", double(info.version)},
                                {"path", info.path}});
    }

    QJsonObject d;
    d["manufacturer"] = Mcp::manufacturerName(m_mc->prefs->manufacturer);
    d["systemRigFolder"] = dirJson(MainController::systemRigLocation());
    QJsonArray searched;
    for (const QString &candidate : MainController::systemRigLocationCandidates())
        searched.append(dirJson(candidate));
    d["systemRigFoldersSearched"] = searched;
    d["userRigFolder"] = dirJson(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/rigs");
    d["note"] = QStringLiteral("A user rig file overrides a system one with the same CI-V address when its version "
                               "is the same or newer.");
    d["rigs"] = rigs;
    d["currentRigFile"] = m_mc->rigCaps ? m_mc->rigCaps->filename : QString();
    return d;
}

QJsonObject McpHostWfview::appState()
{
    if (!m_mc || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    QJsonArray receivers;
    for (int i = 0; i < m_mc->receivers.size(); ++i)
    {
        ReceiverController *rx = m_mc->receivers.at(i);
        if (!rx)
            continue;
        receivers.append(QJsonObject{{"index", i},
                                     {"title", rx->getTitle()},
                                     {"active", rx->getActive()},
                                     {"frequencyAHz", double(rx->getFrequencyA())},
                                     {"frequencyBHz", double(rx->getFrequencyB())},
                                     {"mode", int(rx->getMode())},
                                     {"dataMode", int(rx->getDataMode())},
                                     {"filter", int(rx->getFilter())}});
    }

    return QJsonObject{{"connection", connStatusName(m_mc->connStatus)},
                       {"rigModelName", m_mc->m_rigModelName},
                       {"radioStatusText", m_mc->m_radioStatusText},
                       {"footerMessage", m_mc->m_footerMessageText},
                       {"transmitting", m_mc->m_transmitting},
                       {"currentReceiver", int(m_mc->currentReceiver)},
                       {"frequencyLocked", m_mc->freqLock},
                       {"split", m_mc->m_splitEnabled},
                       {"dualWatch", m_mc->m_dualWatch},
                       {"ritEnabled", m_mc->m_ritEnabled},
                       {"ritHz", m_mc->m_ritFrequency},
                       {"tuner", m_mc->m_tunerEnabled},
                       {"stepHz", double(m_mc->stepSize)},
                       {"radioServer", QJsonObject{{"status", m_mc->serverStatusText()},
                                                   {"usersLoggedIn", m_mc->serverUsersLoggedIn()}}},
                       {"receivers", receivers}};
}

bool McpHostWfview::rigCaps(int radio, rigCapabilities *out, QString *error)
{
    Q_UNUSED(radio)
    if (!m_mc || m_mc->shuttingDown)
    {
        *error = QStringLiteral("wfview is shutting down.");
        return false;
    }
    if (!m_mc->rigCaps)
    {
        *error = QStringLiteral("No radio is connected, so there are no rig capabilities yet.");
        return false;
    }
    *out = *m_mc->rigCaps;
    return true;
}

McpHostResult McpHostWfview::settingOptions()
{
    McpHostResult r;
    SettingsController *settings = m_mc && !m_mc->shuttingDown ? m_mc->getSettings() : nullptr;
    if (!settings)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }

    QStringList keys = settings->optionKeys();
    keys.sort();
    QJsonObject values;
    QJsonArray readOnly;
    for (const QString &key : keys)
    {
        const QVariant value = settings->option(key);
        values[key] = Mcp::isSecretKey(key) ? QJsonValue(Mcp::maskSecret(value.toString())) : Mcp::variantToJson(value);
        if (!Mcp::isWritableOption(key, nullptr))
            readOnly.append(key);
    }

    r.ok = true;
    r.data["options"] = values;
    r.data["notWritable"] = readOnly;
    r.data["unsaved"] = settings->dirty();
    return r;
}

McpHostResult McpHostWfview::setOption(const QString &key, const QJsonValue &value, bool save)
{
    McpHostResult r;
    SettingsController *settings = m_mc && !m_mc->shuttingDown ? m_mc->getSettings() : nullptr;
    if (!settings)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }
    if (!settings->optionKeys().contains(key))
    {
        r.error = QStringLiteral("There is no setting named %1; get_setting_options lists them.").arg(key);
        return r;
    }
    if (!Mcp::isWritableOption(key, &r.error))
        return r;

    // The same path the settings dialog uses, so the change takes effect at once.
    const QVariant before = settings->option(key);
    settings->setOption(key, value.toVariant());
    const QVariant after = settings->option(key);
    if (save)
        settings->save();

    r.ok = true;
    r.data["key"] = key;
    r.data["before"] = Mcp::variantToJson(before);
    r.data["after"] = Mcp::variantToJson(after);
    r.data["changed"] = before != after;
    r.data["saved"] = save;
    r.data["unsaved"] = settings->dirty();
    return r;
}

McpHostResult McpHostWfview::setTransmit(bool on)
{
    McpHostResult r;
    if (!m_mc || m_mc->shuttingDown)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }
    if (!m_mc->rigCaps || !m_mc->rigCaps->hasTransmit || !m_mc->rigCaps->commands.contains(funcTransceiverStatus))
    {
        r.error = QStringLiteral("The connected radio cannot be keyed by wfview.");
        return r;
    }
    if (on && (!m_mc->prefs || !m_mc->prefs->enablePTT))
    {
        r.error = QStringLiteral("PTT is turned off in wfview's settings (Controls.EnablePTT).");
        return r;
    }

    // Goes through wfview's own PTT handling, which also selects the TX audio source.
    m_mc->setTransmit(on);
    r.ok = true;
    r.data["requested"] = on ? QStringLiteral("transmit") : QStringLiteral("receive");
    r.data["transmittingBefore"] = m_mc->m_transmitting;
    return r;
}

namespace
{

QJsonObject rectJson(const QRect &r)
{
    return QJsonObject{{"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
}

QString visibilityName(QWindow::Visibility v)
{
    switch (v)
    {
    case QWindow::Hidden:
        return QStringLiteral("hidden");
    case QWindow::AutomaticVisibility:
        return QStringLiteral("automatic");
    case QWindow::Windowed:
        return QStringLiteral("windowed");
    case QWindow::Minimized:
        return QStringLiteral("minimized");
    case QWindow::Maximized:
        return QStringLiteral("maximized");
    case QWindow::FullScreen:
        return QStringLiteral("fullscreen");
    }
    return QString::number(int(v));
}

QString windowTypeName(Qt::WindowFlags flags)
{
    switch (flags & Qt::WindowType_Mask)
    {
    case Qt::Window:
        return QStringLiteral("window");
    case Qt::Dialog:
        return QStringLiteral("dialog");
    case Qt::Sheet:
        return QStringLiteral("sheet");
    case Qt::Drawer:
        return QStringLiteral("drawer");
    case Qt::Popup:
        return QStringLiteral("popup");
    case Qt::Tool:
        return QStringLiteral("tool");
    case Qt::ToolTip:
        return QStringLiteral("tooltip");
    case Qt::SplashScreen:
        return QStringLiteral("splash");
    default:
        break;
    }
    return QStringLiteral("other");
}

QString windowLabel(const QWindow *w)
{
    if (!w)
        return QString();
    if (!w->title().isEmpty())
        return w->title();
    if (!w->objectName().isEmpty())
        return w->objectName();
    return QString::fromLatin1(w->metaObject()->className());
}

} // namespace

QJsonObject McpHostWfview::windows()
{
    QJsonObject d;
    d["platform"] = QGuiApplication::platformName();

    QJsonArray screens;
    for (const QScreen *s : QGuiApplication::screens())
    {
        screens.append(QJsonObject{{"name", s->name()},
                                   {"primary", s == QGuiApplication::primaryScreen()},
                                   {"geometry", rectJson(s->geometry())},
                                   {"availableGeometry", rectJson(s->availableGeometry())},
                                   {"devicePixelRatio", s->devicePixelRatio()},
                                   {"logicalDpi", s->logicalDotsPerInch()}});
    }
    d["screens"] = screens;

    QJsonArray list;
    int shown = 0;
    for (const QWindow *w : QGuiApplication::topLevelWindows())
    {
        QJsonObject o;
        o["title"] = w->title();
        o["objectName"] = w->objectName();
        o["className"] = QString::fromLatin1(w->metaObject()->className());
        o["type"] = windowTypeName(w->flags());
        o["visible"] = w->isVisible();
        o["visibility"] = visibilityName(w->visibility());
        o["active"] = w->isActive();
        o["geometry"] = rectJson(w->geometry());
        o["frameGeometry"] = rectJson(w->frameGeometry());
        o["minimumSize"] = QJsonObject{{"width", w->minimumWidth()}, {"height", w->minimumHeight()}};
        o["maximumSize"] = QJsonObject{{"width", w->maximumWidth()}, {"height", w->maximumHeight()}};
        o["screen"] = w->screen() ? w->screen()->name() : QString();
        o["devicePixelRatio"] = w->devicePixelRatio();
        if (w->transientParent())
            o["transientParent"] = windowLabel(w->transientParent());
        if (w->modality() != Qt::NonModal)
            o["modality"] = w->modality() == Qt::WindowModal ? QStringLiteral("window") : QStringLiteral("application");
        if (w->isVisible())
            ++shown;
        list.append(o);
    }
    d["windows"] = list;
    d["visibleCount"] = shown;
    d["note"] = QStringLiteral("Sizes and positions are in logical (device-independent) pixels; multiply by "
                               "devicePixelRatio for physical pixels. geometry is the client area and frameGeometry "
                               "includes the title bar and borders. Hidden windows are ones QML has created but not "
                               "shown, such as a closed Settings window. On Wayland, applications cannot learn their "
                               "window positions, so x and y are not real: shown windows usually report their "
                               "screen's top-left corner and hidden ones report 0,0. Sizes are still accurate.");
    return d;
}

namespace
{

const QStringList serverAccessNames{QStringLiteral("admin"), QStringLiteral("normal"), QStringLiteral("noTx"),
                                    QStringLiteral("disabled")};

QString serverAccessName(int userType)
{
    return userType >= 0 && userType < serverAccessNames.size() ? serverAccessNames.at(userType)
                                                                : QString::number(userType);
}

QString serverUserStatusName(ServerUsersModel::UserStatus status)
{
    switch (status)
    {
    case ServerUsersModel::LoggedIn:
        return QStringLiteral("loggedIn");
    case ServerUsersModel::LoggedOut:
        return QStringLiteral("loggedOut");
    case ServerUsersModel::NeverSeen:
        break;
    }
    return QStringLiteral("neverSeen");
}

} // namespace

QJsonObject McpHostWfview::radioServer()
{
    if (!m_mc || !m_mc->prefs || m_mc->shuttingDown)
        return QJsonObject{{"error", "wfview is shutting down"}};

    const SERVERCONFIG *cfg = m_mc->m_settings->getServerConfig();
    const ServerUsersModel *model = m_mc->m_settings->serverUsersModel();
    QJsonObject d;
    d["available"] = true;
    d["enabled"] = cfg->enabled;
    d["status"] = m_mc->serverStatusText();
    d["running"] = m_mc->serverRunning();
    d["started"] = m_mc->server != nullptr;
    if (!m_mc->m_serverError.isEmpty())
        d["error"] = m_mc->m_serverError;
    d["protocol"] = Mcp::manufacturerName(int(m_mc->prefs->manufacturer));
    d["serialConnection"] = m_mc->usingSerialConnection();
    if (m_mc->server != nullptr)
    {
        d["startedAt"] = m_mc->m_serverStartedAt.toString(Qt::ISODate);
        d["uptimeSeconds"] = double(m_mc->m_serverStartedAt.secsTo(QDateTime::currentDateTime()));
    }
    d["listenAddress"] = cfg->listenAddress.isEmpty() ? QStringLiteral("automatic") : cfg->listenAddress;
    d["ports"] = QJsonObject{{"control", int(cfg->controlPort)},
                             {"civ", int(cfg->civPort)},
                             {"audio", int(cfg->audioPort)},
                             {"scope", int(cfg->scopePort)}};
    d["disableLocalControlsWhenInUse"] = cfg->disableUI;
    d["suppressSerialWarnings"] = cfg->suppressSerialWarnings;
    if (!cfg->rigs.isEmpty())
    {
        const RIGCONFIG *radio = cfg->rigs.first();
        d["radio"] = QJsonObject{{"modelName", radio->modelName},
                                 {"civAddress", int(radio->civAddr)},
                                 {"baudRate", double(radio->baudRate)},
                                 {"available", radio->rigAvailable}};
        d["audioInput"] = Mcp::audioSetupToJson(radio->rxAudioSetup);
        d["audioOutput"] = Mcp::audioSetupToJson(radio->txAudioSetup);
        d["audioOpen"] = QJsonObject{{"input", radio->rxaudio != nullptr},
                                     {"output", radio->txaudio != nullptr},
                                     {"rtp", radio->rtp != nullptr}};
    }
    d["usersLoggedIn"] = m_mc->serverUsersLoggedIn();
    d["totalLogins"] = m_mc->m_serverTotalLogins;

    QJsonArray sessions;
    for (const MainController::serverSession &session : m_mc->m_serverSessions)
        sessions.append(QJsonObject{{"username", session.username},
                                    {"address", session.address},
                                    {"since", session.since.toString(Qt::ISODate)}});
    d["sessions"] = sessions;

    QJsonArray users;
    for (const SERVERUSER &user : model->toList())
        users.append(QJsonObject{{"username", user.username},
                                 {"password", Mcp::maskSecret(user.password)},
                                 {"access", serverAccessName(user.userType)},
                                 {"status", serverUserStatusName(model->userStatus(user.username))}});
    d["users"] = users;
    d["note"] = QStringLiteral("The server only runs while wfview is connected to a radio on a serial port. "
                               "User status and login counts cover the time since the server last started.");
    return d;
}

McpHostResult McpHostWfview::setServerUserAccess(const QString &username, const QString &access, bool save)
{
    McpHostResult r;
    SettingsController *settings = m_mc && !m_mc->shuttingDown ? m_mc->getSettings() : nullptr;
    if (!settings)
    {
        r.error = QStringLiteral("wfview is shutting down.");
        return r;
    }

    const int userType = int(serverAccessNames.indexOf(access));
    if (userType < 0)
    {
        r.error = QStringLiteral("Unknown access level %1; use one of %2.").arg(access, serverAccessNames.join(", "));
        return r;
    }

    ServerUsersModel *model = settings->serverUsersModel();
    const QList<SERVERUSER> &list = model->toList();
    int row = -1;
    for (int i = 0; i < list.size(); ++i)
    {
        if (list.at(i).username == username)
        {
            row = i;
            break;
        }
    }
    if (row < 0)
    {
        r.error = QStringLiteral("There is no radio server user named %1; get_radio_server lists them.").arg(username);
        return r;
    }

    const QString before = serverAccessName(list.at(row).userType);
    // The same path the settings dialog uses, so a running server is updated at once.
    model->setRoleValue(row, ServerUsersModel::UserTypeRole, userType);
    if (save)
        settings->save();

    r.ok = true;
    r.data["user"] = username;
    r.data["before"] = before;
    r.data["after"] = serverAccessName(model->toList().at(row).userType);
    r.data["saved"] = save;
    return r;
}
