#pragma once

// Only built with qmake CONFIG+=mcp (defines WFVIEW_MCP).

#include <QAudioFormat>
#include <QByteArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QString>

#include <atomic>

class QObject;
class audioHandlerBase;
class rigCommander;
struct audioSetup;
struct rigCapabilities;

Q_DECLARE_LOGGING_CATEGORY(logMcp)

// Collects diagnostic data for the MCP server from the threads that produce
// it, so the MCP thread can read it without waiting on the GUI thread.
//
// Every hook returns at once unless MCP is running. The audio level hook is
// lock-free because it runs on real-time audio threads, and the radio status
// hooks never wait for a lock.
class McpProbe
{
public:
    static bool enabled()
    {
        return s_enabled.load(std::memory_order_relaxed);
    }
    static void setEnabled(bool on);

    // Hooks, called from wfview code on any thread.
    static void logMessage(QtMsgType type, const char *category, const QString &msg);
    static void attachRig(rigCommander *rig);
    static void attachAudioHandler(audioHandlerBase *handler,
                                   const audioSetup &setup,
                                   const QAudioFormat &nativeFormat,
                                   const QAudioFormat &radioFormat);

    // Traffic hooks (see mcphooks.h). Capture is off until an MCP client
    // starts it, and stops by itself when its time runs out.
    static bool capturing()
    {
        return s_capturing.load(std::memory_order_relaxed);
    }
    static void radioTraffic(const QObject *source, bool toRadio, const QByteArray &data)
    {
        if (capturing())
            recordRadio(source, toRadio, data);
    }
    static void networkTraffic(const QObject *source, bool toRadio, const QByteArray &data)
    {
        if (capturing())
            recordNetwork(source, toRadio, data);
    }

    // Capture control and readers, called from the MCP thread.
    static void startCapture(int seconds, bool includeScope, bool clear);
    // Makes sure a capture runs for at least the given time, keeping what
    // has been recorded so far.
    static void ensureCapture(int seconds);
    static void stopCapture();
    static QJsonObject captureStatus();
    // kind is "radio", "network" or "all"; direction is "toRadio",
    // "fromRadio" or empty for both. sinceMs (milliseconds since the epoch)
    // leaves out anything older.
    static QJsonObject
    traffic(const QString &kind, const QString &direction, int limit, const QString &contains, qint64 sinceMs = 0);

    // Readers, called from the MCP thread.
    static QJsonObject logLines(int maxLines, QtMsgType minType, const QString &category, const QString &contains);
    static QJsonObject audioLevels();
    static QJsonObject networkStatusJson();
    // The last capabilities a radio reported; current is false once that
    // radio has disconnected.
    static bool rigCapsSnapshot(rigCapabilities *out, qint64 *ageMs, bool *current);
    static qint64 uptimeMs();

private:
    static void recordRadio(const QObject *source, bool toRadio, const QByteArray &data);
    static void recordNetwork(const QObject *source, bool toRadio, const QByteArray &data);

    static std::atomic_bool s_enabled;
    static std::atomic_bool s_capturing;
};
