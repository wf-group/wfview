#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP).

#include "mcphost.h"

#include <QPointer>

class MainController;

// Reads wfview's MainController state for the MCP server, on the main thread.
class McpHostWfview : public McpHost
{
public:
    explicit McpHostWfview(MainController *controller, QObject *parent = nullptr);

    QString appName() const override;
    QString settingsFileName() const override;
    QSettings::Format settingsFormat() const override;

    QJsonObject connectionInfo() override;
    QJsonObject audioConfig() override;
    QJsonObject rigFiles() override;
    QJsonObject appState() override;
    bool rigCaps(int radio, rigCapabilities *out, QString *error) override;

    McpHostResult settingOptions() override;
    QJsonObject windows() override;
    McpHostResult setOption(const QString &key, const QJsonValue &value, bool save) override;
    McpHostResult setTransmit(bool on) override;
    QJsonObject radioServer() override;
    McpHostResult setServerUserAccess(const QString &username, const QString &access, bool save) override;

private:
    QPointer<MainController> m_mc;
};
