#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP).

#include "mcphost.h"

#include <QPointer>

class servermain;

// Reads wfserver's servermain state for the MCP server, on the main thread.
class McpHostServer : public McpHost
{
public:
    explicit McpHostServer(servermain *server, QObject *parent = nullptr);

    QString appName() const override;
    QString settingsFileName() const override;
    QSettings::Format settingsFormat() const override;

    QJsonObject connectionInfo() override;
    QJsonObject audioConfig() override;
    QJsonObject rigFiles() override;
    QJsonObject appState() override;
    bool rigCaps(int radio, rigCapabilities *out, QString *error) override;

    McpHostResult setTransmit(bool on) override;

private:
    QPointer<servermain> m_server;
};
