#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP).

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <atomic>
#include <functional>

class McpHost;
class cachingQueue;
struct audioSetup;
struct cacheItem;
struct rigCapabilities;

// Helpers shared by the tools and the host adapters.
namespace Mcp
{
// "secret" becomes "sXXXt"; very short values are hidden entirely.
QString maskSecret(const QString &value);
// True for settings keys and fields that hold passwords or tokens.
bool isSecretKey(const QString &key);
// Masks the value of every secret key=value line in INI text.
QString maskSettingsText(const QString &text);
// True for settings an MCP client may change by name. Secrets are never
// writable, because a client only ever sees them masked.
bool isWritableOption(const QString &key, QString *reason);
QJsonValue variantToJson(const QVariant &value);
QJsonObject audioSetupToJson(const audioSetup &setup);
QString audioTypeName(int type);
QString codecName(quint8 codec);
QString manufacturerName(int manufacturer);
// The radio's address or model number in decimal and hex. Icom calls it the
// CI-V address; Kenwood and Yaesu use it as a model ID.
QJsonObject radioIdJson(int manufacturer, quint16 id);
QJsonObject rigCapsToJson(const rigCapabilities &caps, const QStringList &sections);
} // namespace Mcp

// Facts captured on the main thread before the MCP thread starts.
struct McpAppInfo
{
    QString appName;
    QString settingsFileName;
    QSettings::Format settingsFormat = QSettings::IniFormat;
    QString bindAddress;
    quint16 port = 0;
    bool tokenRequired = false;
    bool allowWrite = false;
    cachingQueue *queue = nullptr;
};

struct McpToolResult
{
    QJsonObject data;
    QString error;
};

// The MCP tools. Everything here runs on the MCP thread; anything owned by
// the main thread is reached through McpHost with a timeout.
class McpTools
{
public:
    McpTools(McpHost *host, const McpAppInfo &info, const std::atomic_bool *stopping);

    // Tool definitions for tools/list. Write tools are left out unless
    // writes are allowed.
    QJsonArray list() const;
    // Returns false when there is no such tool available.
    bool call(const QString &name, const QJsonObject &args, McpToolResult *result);

    static constexpr int hostTimeoutMs = 2000;

private:
    using Handler = std::function<McpToolResult(const QJsonObject &)>;
    struct Tool
    {
        QString name;
        QString title;
        QString description;
        QJsonObject inputSchema;
        bool writes = false;
        Handler run;
    };

    void addTool(const QString &name,
                 const QString &title,
                 const QString &description,
                 const QJsonObject &properties,
                 Handler run);
    // A tool that changes the radio or the application; only listed and
    // callable with --mcp-allow-write.
    void addWriteTool(const QString &name,
                      const QString &title,
                      const QString &description,
                      const QJsonObject &properties,
                      Handler run);
    const Tool *findTool(const QString &name) const;

    // Runs fn on the main thread and waits up to timeoutMs for its result.
    template <typename T> bool onHost(std::function<T()> fn, T *out, int timeoutMs = hostTimeoutMs);
    QString hostTimeoutText(int timeoutMs) const;
    // True when a radio is connected and the queue is running; fills caps.
    bool radioReady(rigCapabilities *caps, QString *error) const;
    // A cache entry read without asking the radio for a fresh value.
    bool cachedValue(int func, int receiver, cacheItem *out) const;
    // Waits on the MCP thread, returning early if the server is stopping.
    void pause(int ms) const;

    McpToolResult toolOverview(const QJsonObject &args);
    McpToolResult toolMainThreadHealth(const QJsonObject &args);
    McpToolResult toolWindows(const QJsonObject &args);
    McpToolResult toolSettings(const QJsonObject &args);
    McpToolResult toolSettingsFile(const QJsonObject &args);
    McpToolResult toolConnection(const QJsonObject &args);
    McpToolResult toolRigCaps(const QJsonObject &args);
    McpToolResult toolRigFiles(const QJsonObject &args);
    McpToolResult toolAudioConfig(const QJsonObject &args);
    McpToolResult toolAudioLevels(const QJsonObject &args);
    McpToolResult toolCache(const QJsonObject &args);
    McpToolResult toolQueue(const QJsonObject &args);
    McpToolResult toolMeters(const QJsonObject &args);
    McpToolResult toolLog(const QJsonObject &args);
    McpToolResult toolSetTrafficCapture(const QJsonObject &args);
    McpToolResult toolTraffic(const QJsonObject &args);
    McpToolResult toolSpectrum(const QJsonObject &args);
    McpToolResult toolSettingOptions(const QJsonObject &args);
    McpToolResult toolSetSetting(const QJsonObject &args);
    McpToolResult toolSetFrequency(const QJsonObject &args);
    McpToolResult toolSetMode(const QJsonObject &args);
    McpToolResult toolSetPtt(const QJsonObject &args);
    McpToolResult toolSendCommand(const QJsonObject &args);
    McpToolResult toolSendRaw(const QJsonObject &args);
    McpToolResult toolInvalidateCache(const QJsonObject &args);
    McpToolResult toolSetRigctld(const QJsonObject &args);
    McpToolResult toolRigctldCommand(const QJsonObject &args);
    McpToolResult toolRadioServer(const QJsonObject &args);
    McpToolResult toolSetRadioServer(const QJsonObject &args);

    McpHost *m_host;
    McpAppInfo m_info;
    const std::atomic_bool *m_stopping;
    QVector<Tool> m_tools;
};
