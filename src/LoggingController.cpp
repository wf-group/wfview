#include "LoggingController.h"

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

std::atomic<LoggingController*> LoggingController::s_instance{nullptr};
QtMessageHandler LoggingController::s_prevHandler = nullptr;

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

void LoggingController::sendToTermbinRequested()
{
    // termbin.com:9999 is plain TCP. You send text, it replies with a URL.
    // WARNING: This sends your log content unencrypted over the network.
    // Your UI tooltip already warns about personal info.

    setError(QString()); // clear previous
    setTermbinUrl(QString());

    const QString payload = m_logText.isEmpty() ? QString("(empty)") : m_logText;

    auto *sock = new QTcpSocket(this);
    sock->setObjectName("termbinSocket");

    connect(sock, &QTcpSocket::connected, this, [this, sock, payload]() {
        QByteArray data = payload.toUtf8();
        if (!data.endsWith('\n'))
            data.append('\n');
        sock->write(data);
        sock->flush();
        sock->disconnectFromHost(); // termbin sends URL then closes; but this is fine
    });

    connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
        const QByteArray r = sock->readAll();
        const QString s = QString::fromUtf8(r).trimmed();
        if (!s.isEmpty()) {
            // termbin typically returns a single URL line
            setTermbinUrl(s);
        }
    });

    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError) {
        setError(QString("termbin.com failed: %1").arg(sock->errorString()));
        sock->deleteLater();
    });

    connect(sock, &QTcpSocket::disconnected, this, [sock]() {
        sock->deleteLater();
    });

    sock->connectToHost("termbin.com", 9999);
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

    qInstallMessageHandler(s_prevHandler);
    m_handlerInstalled = false;
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
    for (const auto& e : batch)
        m_model.append(e);
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
    if (type == QtDebugMsg && !inst->m_debugEnabled)
        return;

    if (type == QtWarningMsg && msg.contains("QPainter::"))
        return;

    if (type == QtDebugMsg && !inst->m_commDebugEnabled
        && context.category && qstrncmp(context.category, "rigTraffic", 10) == 0)
        return;

    if (type == QtDebugMsg && !inst->m_rigctlDebugEnabled
        && context.category && qstrncmp(context.category, "rigctld", 6) == 0)
        return;

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

