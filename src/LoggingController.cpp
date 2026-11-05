#include "LoggingController.h"
#include "logcategories.h"
#include "mcphooks.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QMetaObject>
#include <QTextStream>
#include <QUrl>

#include <cstdio>

std::atomic<LoggingController*> LoggingController::s_instance{nullptr};
QtMessageHandler LoggingController::s_prevHandler = nullptr;
std::atomic_bool LoggingController::s_consoleDebug{false};

static inline const char* typeTag(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DBG";
    case QtInfoMsg:     return "INF";
    case QtWarningMsg:  return "WRN";
    case QtCriticalMsg: return "CRT";
    case QtFatalMsg:    return "FTL";
    }
    return "UNK";
}


LoggingController::LoggingController(QObject *parent)
    : QObject(parent)
{
    s_instance.store(this, std::memory_order_release);

    if (qApp) {
        connect(qApp, &QCoreApplication::aboutToQuit,
                this, &LoggingController::beginShutdown,
                Qt::DirectConnection);
    }

    m_flushTimer.setInterval(m_flushIntervalMs);
    m_flushTimer.setSingleShot(false);
    connect(&m_flushTimer, &QTimer::timeout, this, &LoggingController::flushPendingToModel);
    m_flushTimer.start();

}

LoggingController::~LoggingController()
{
    beginShutdown(); // safe to call multiple times
    s_instance.store(nullptr, std::memory_order_release);
}

void LoggingController::beginShutdown()
{
    bool expected = false;
    if (!m_shuttingDown.compare_exchange_strong(expected, true))
        return; // already shutting down

    if (m_flushTimer.isActive())
        m_flushTimer.stop();

    // Restore previous Qt handler so we stop intercepting messages
    uninstallQtMessageHandler();
}

void LoggingController::setLogText(const QString &t)
{
    if (m_logText == t) return;
    m_logText = t;
    emit logTextChanged();
}

void LoggingController::clearDisplay()
{
    if (m_logText.isEmpty()) return;
    m_logText.clear();
    emit logTextChanged();
}

void LoggingController::setFlushIntervalMs(int v)
{
    if (v < 20) v = 20;
    if (v > 2000) v = 2000;
    if (m_flushIntervalMs == v) return;
    m_flushIntervalMs = v;
    m_flushTimer.setInterval(m_flushIntervalMs);
    emit flushIntervalMsChanged();
}

void LoggingController::setDebugEnabled(bool v)
{
    if (m_debugEnabled == v) return;
    m_debugEnabled = v;
    s_consoleDebug.store(v, std::memory_order_relaxed);
    m_model.setShowDebug(v);
    emit debugEnabledChanged();
}

void LoggingController::setCommDebugEnabled(bool v)
{
    if (m_commDebugEnabled == v) return;
    m_commDebugEnabled = v;
    emit commDebugEnabledChanged();
}

void LoggingController::setRigctlDebugEnabled(bool v)
{
    if (m_rigctlDebugEnabled == v) return;
    m_rigctlDebugEnabled = v;
    emit rigctlDebugEnabledChanged();
}

void LoggingController::setLogFilePath(const QString &p)
{
    if (m_logFilePath == p) return;
    m_logFilePath = p;
    emit logFilePathChanged();

    if (m_logFile.isOpen()) {
        m_logStream.flush();
        m_logFile.close();
    }
}

void LoggingController::ensureLogFileOpen_locked()
{
    if (m_logFile.isOpen()) return;
    if (m_logFilePath.isEmpty()) return;

    m_logFile.setFileName(m_logFilePath);
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_logStream.setDevice(&m_logFile);
    }
}

void LoggingController::annotateRequested(const QString &text)
{

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    const QString msg = QString("[%1] USER: %2").arg(stamp, trimmed);

    // Append to display
    //appendLog(msg);
    enqueueEntry(QtInfoMsg, msg);
}

void LoggingController::scrollToBottomRequested()
{
    emit requestScrollToBottom();
}

void LoggingController::clearDisplayRequested()
{
    clearDisplay();
}

void LoggingController::copyPathRequested()
{
    if (m_logFilePath.isEmpty()) {
        setError("Log file path is empty.");
        return;
    }

    if (auto cb = QGuiApplication::clipboard()) {
        cb->setText(m_logFilePath);
    } else {
        setError("Clipboard is not available.");
    }
}

void LoggingController::openLogDirectoryRequested()
{
    if (m_logFilePath.isEmpty()) {
        setError("Log file path is empty.");
        return;
    }

    const QString dir = QFileInfo(m_logFilePath).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        setError("Could not open the log directory: " + dir);
        qInfo(logLogger()) << "Error, could not open the log directory" << dir;
    }
}

void LoggingController::openLogFileRequested()
{
    if (m_logFilePath.isEmpty()) {
        setError("Log file path is empty.");
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_logFilePath))) {
        setError("Could not open the log file: " + m_logFilePath);
        qInfo(logLogger()) << "Error, could not open the log file" << m_logFilePath;
    }
}

void LoggingController::sendToTermbinRequested()
{
    // termbin.com:9999 is plain TCP: send the text, the server replies with a URL
    // and closes the connection. The log is sent unencrypted.
    if (m_termbinBusy)
        return;

    setError(QString());
    setTermbinUrl(QString());

    // Snapshot what the window shows now, so lines logged during the send aren't included.
    QString payload = m_model.allVisibleText();
    payload += QStringLiteral("\n----------\nSent from wfview version ");
    payload += QStringLiteral(WFVIEW_VERSION);
    payload += QStringLiteral("\n----------\n");

    const QString host = QStringLiteral("termbin.com");
    const quint16 port = 9999;

    auto *sock = new QTcpSocket(this);
    sock->setObjectName("termbinSocket");
    setTermbinBusy(true);

    // Don't leave the button disabled forever if the server never answers.
    auto *timeout = new QTimer(sock);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this, sock]() {
        qWarning(logLogger()) << "Timed out waiting for termbin.com";
        setError(QStringLiteral("termbin.com timed out."));
        sock->abort();
        sock->deleteLater();
        setTermbinBusy(false);
    });
    timeout->start(30000);

    connect(sock, &QTcpSocket::connected, this, [sock, payload]() {
        qInfo(logLogger()) << "Connected to logging host";
        sock->write(payload.toUtf8());
    });

    connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
        const QByteArray data = sock->readAll();
        if (data.length() >= 256) {
            qDebug(logLogger()) << "Error, return from logging host too large. Received" << data.length() << "bytes.";
            return;
        }
        // termbin terminates its reply with a NUL byte, which renders as a stray glyph.
        QByteArray reply = data;
        reply.replace('\0', QByteArray());
        const QString url = QString::fromUtf8(reply).trimmed();
        if (url.isEmpty())
            return;

        if (auto cb = QGuiApplication::clipboard())
            cb->setText(url);
        qInfo(logLogger()).noquote() << "Sent log to URL:" << url;
        qInfo(logLogger()) << "This address already copied to the clipboard. Please paste this URL in to your support questions.";
        setTermbinUrl(url);
        emit termbinUrlReceived(url);
    });

    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError err) {
        if (err == QAbstractSocket::RemoteHostClosedError)
            return; // normal end of a termbin exchange; handled by disconnected()
        qWarning(logLogger()) << "Error connecting to logging host. Check internet connection. Error:" << sock->errorString();
        setError(QString("termbin.com failed: %1").arg(sock->errorString()));
        sock->deleteLater();
        setTermbinBusy(false);
    });

    connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
        qInfo(logLogger()) << "Disconnected from logging host";
        sock->deleteLater();
        setTermbinBusy(false);
    });

    qInfo(logLogger()).noquote().nospace() << "Sending data to " << host << ":" << port << ", Standby.";
    sock->connectToHost(host, port);
}

void LoggingController::setTermbinBusy(bool busy)
{
    if (m_termbinBusy == busy) return;
    m_termbinBusy = busy;
    emit termbinBusyChanged();
}

void LoggingController::setError(const QString &msg)
{
    if (m_lastError == msg) return;
    m_lastError = msg;
    emit lastErrorChanged();
}

void LoggingController::setTermbinUrl(const QString &url)
{
    if (m_lastTermbinUrl == url) return;
    m_lastTermbinUrl = url;
    emit lastTermbinUrlChanged();
}

void LoggingController::installQtMessageHandler(bool /*alsoCaptureStderr*/)
{
    if (m_handlerInstalled) return;

    // Save previous so we can chain/uninstall
    s_prevHandler = qInstallMessageHandler(&LoggingController::qtMessageHandler);
    m_handlerInstalled = true;
}

void LoggingController::uninstallQtMessageHandler()
{
    if (!m_handlerInstalled) return;

    // Hand off to a console-only handler so debug messages emitted during
    // shutdown stay quiet unless debug mode is on
    qInstallMessageHandler(&LoggingController::shutdownMessageHandler);
    m_handlerInstalled = false;
}

void LoggingController::shutdownMessageHandler(QtMsgType type,
                                               const QMessageLogContext& context,
                                               const QString& msg)
{
    if (type == QtDebugMsg && !s_consoleDebug.load(std::memory_order_relaxed))
        return;

    if (s_prevHandler) {
        s_prevHandler(type, context, msg);
    } else {
        fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, msg)));
        fflush(stderr);
    }

    if (type == QtFatalMsg)
        abort();
}

void LoggingController::enqueueEntry(QtMsgType type, const QString& text)
{
    QMutexLocker locker(&m_pendingMutex);
    m_pendingEntries.push_back({type, text});

    // Optional: bound pending size
    const int maxPending = 5000;
    if (m_pendingEntries.size() > maxPending)
        m_pendingEntries.erase(m_pendingEntries.begin(),
                               m_pendingEntries.begin() + (m_pendingEntries.size() - maxPending));
}

void LoggingController::flushPendingToModel()
{
    if (m_shuttingDown.load(std::memory_order_relaxed))
        return;

    QVector<LogEntry> batch;
    {
        QMutexLocker locker(&m_pendingMutex);
        if (m_pendingEntries.isEmpty())
            return;
        batch.swap(m_pendingEntries);
    }

    // GUI thread: safe to touch QAbstractListModel
    ensureLogFileOpen_locked();
    for (const auto& e : batch) {
        m_model.append(e);
        if (m_logFile.isOpen() && (e.type != QtDebugMsg || m_debugEnabled))
            m_logStream << e.text << '\n';
    }

    if (m_logFile.isOpen())
        m_logStream.flush();
}

void LoggingController::qtMessageHandler(QtMsgType type,
                                         const QMessageLogContext& context,
                                         const QString& msg)
{
    static thread_local bool inHandler = false;
    if (inHandler) return;
    inHandler = true;

    // Always clear inHandler no matter how we exit
    struct Guard {
        bool &flag;
        ~Guard() { flag = false; }
    } guard{ inHandler };

    LoggingController* inst = s_instance.load(std::memory_order_acquire);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const bool closing = QCoreApplication::closingDown();
#else
    const bool closing = false;
#endif

    // If we're closing or controller is gone, do nothing (optionally chain)
    if (!inst || inst->m_shuttingDown.load(std::memory_order_relaxed) || closing) {
        // If you want default console output during shutdown, uncomment:
        // if (s_prevHandler) s_prevHandler(type, context, msg);

        if (type == QtFatalMsg) abort();
        return;
    }

    // Filters (NO early return without guard now)
    // Debug messages are always captured; the Debug checkbox only controls
    // whether they are shown, so enabling it reveals earlier messages too.
    if (type == QtWarningMsg && msg.contains("QPainter::"))
        return;

    if (type == QtDebugMsg && !inst->m_commDebugEnabled
        && context.category && qstrncmp(context.category, "rigTraffic", 10) == 0)
        return;

    if (type == QtDebugMsg && !inst->m_rigctlDebugEnabled
        && context.category && qstrncmp(context.category, "rigctld", 6) == 0)
        return;

    MCP_LOG_MESSAGE(type, context.category, msg);

    const QString ts  = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    const QString cat = context.category ? QString::fromUtf8(context.category) : QString("default");

    QString text;
    text.reserve(ts.size() + 4 + cat.size() + 2 + msg.size());
    text += ts;
    text += typeTag(type);
    text += ' ';
    text += cat;
    text += ": ";
    text += msg;

    inst->enqueueEntry(type, text);

    // Optional: console logging
    if (inst->m_consoleLoggingEnabled && s_prevHandler)
        s_prevHandler(type, context, msg);

    if (type == QtFatalMsg)
        abort();
}


void LoggingController::setConsoleLoggingEnabled(bool v)
{
    if (m_consoleLoggingEnabled == v) return;
    m_consoleLoggingEnabled = v;
    emit consoleLoggingEnabledChanged();
}
