#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <memory>

class MainController;
class McpHost;
class McpServer;

// Runs the optional MCP diagnostics server for the wfview GUI and lets the
// Settings page start, stop and reconfigure it while wfview runs.
//
// Built in every GUI build so QML can always use it; without qmake
// CONFIG+=mcp, available is false and the server never starts. The options
// start from the command line (--use-mcp and friends) and are never stored in
// the settings file.
class McpController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool serverEnabled READ serverEnabled WRITE setServerEnabled NOTIFY optionsChanged)
    Q_PROPERTY(bool allowWrite READ allowWrite WRITE setAllowWrite NOTIFY optionsChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY optionsChanged)
    Q_PROPERTY(int bindMode READ bindMode WRITE setBindMode NOTIFY optionsChanged)
    Q_PROPERTY(QString customBindAddress READ customBindAddress WRITE setCustomBindAddress NOTIFY optionsChanged)
    Q_PROPERTY(QString token READ token WRITE setToken NOTIFY optionsChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList clients READ clients NOTIFY statusChanged)

public:
    enum BindMode
    {
        BindLocal = 0,
        BindAll = 1,
        BindCustom = 2
    };
    Q_ENUM(BindMode)

    struct Options
    {
        bool enabled = false;
        bool allowWrite = false;
        int port = 8765;
        QString bindAddress = QStringLiteral("127.0.0.1");
        QString token;
    };

    McpController(MainController *mc, const Options &initial, QObject *parent = nullptr);
    ~McpController() override;

    bool available() const;
    bool serverEnabled() const { return m_enabled; }
    bool allowWrite() const { return m_allowWrite; }
    int port() const { return m_port; }
    int bindMode() const { return m_bindMode; }
    QString customBindAddress() const { return m_customBind; }
    QString token() const { return m_token; }
    bool running() const;
    QString status() const { return m_status; }
    QVariantList clients() const { return m_clients; }

    void setServerEnabled(bool on);
    void setAllowWrite(bool on);
    void setPort(int port);
    void setBindMode(int mode);
    void setCustomBindAddress(const QString &address);
    void setToken(const QString &token);

    // Stops the server for good; connect to QCoreApplication::aboutToQuit
    // before MainController::shutdown.
    void shutdown();

signals:
    void optionsChanged();
    void statusChanged();

private:
    QString bindAddress() const;
    void scheduleApply();
    void apply();
    void startServer();
    void stopServer();
    void setStatus(const QString &status);
    void updateClients(const QVariantList &clients);

    MainController *m_mc;
    bool m_enabled = false;
    bool m_allowWrite = false;
    int m_port = 8765;
    int m_bindMode = BindLocal;
    QString m_customBind;
    QString m_token;
    bool m_shuttingDown = false;

    QString m_status;
    QString m_listening; // "address:port (mode)" while running
    QVariantList m_clients;

    // Several options can change in one go (for example on dialog close);
    // the server restarts once, from the event loop, after they all land.
    QTimer m_applyTimer;

    std::unique_ptr<McpHost> m_host;
    std::unique_ptr<McpServer> m_server;
};
