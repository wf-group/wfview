#ifndef LOGGINGCONTROLLER_H
#define LOGGINGCONTROLLER_H

#include "qtimer.h"
#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutex>
#include <QFile>
#include <QTextStream>

#include "LogModel.h"

class LoggingController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString logText READ logText WRITE setLogText NOTIFY logTextChanged)
    Q_PROPERTY(bool debugEnabled READ debugEnabled WRITE setDebugEnabled NOTIFY debugEnabledChanged)
    Q_PROPERTY(bool commDebugEnabled READ commDebugEnabled WRITE setCommDebugEnabled NOTIFY commDebugEnabledChanged)
    Q_PROPERTY(bool rigctlDebugEnabled READ rigctlDebugEnabled WRITE setRigctlDebugEnabled NOTIFY rigctlDebugEnabledChanged)

    Q_PROPERTY(QString logFilePath READ logFilePath WRITE setLogFilePath NOTIFY logFilePathChanged)
    Q_PROPERTY(int flushIntervalMs READ flushIntervalMs WRITE setFlushIntervalMs NOTIFY flushIntervalMsChanged)

    // Useful for UI enable/disable, status bars etc.
    Q_PROPERTY(QString lastTermbinUrl READ lastTermbinUrl NOTIFY lastTermbinUrlChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool consoleLoggingEnabled READ consoleLoggingEnabled WRITE setConsoleLoggingEnabled NOTIFY consoleLoggingEnabledChanged)
    Q_PROPERTY(LogModel* model READ model CONSTANT)

    Q_PROPERTY(int debugMsg READ debugMsg CONSTANT)
    Q_PROPERTY(int infoMsg READ infoMsg CONSTANT)
    Q_PROPERTY(int warningMsg READ warningMsg CONSTANT)
    Q_PROPERTY(int criticalMsg READ criticalMsg CONSTANT)
    Q_PROPERTY(int fatalMsg READ fatalMsg CONSTANT)

public:
    explicit LoggingController(QObject *parent = nullptr);
    ~LoggingController() override;

    QString logText() const { return m_logText; }
    void setLogText(const QString &t);

    bool debugEnabled() const { return m_debugEnabled; }
    void setDebugEnabled(bool v);

    bool commDebugEnabled() const { return m_commDebugEnabled; }
    void setCommDebugEnabled(bool v);

    bool rigctlDebugEnabled() const { return m_rigctlDebugEnabled; }
    void setRigctlDebugEnabled(bool v);

    QString logFilePath() const { return m_logFilePath; }
    void setLogFilePath(const QString &p);

    QString lastTermbinUrl() const { return m_lastTermbinUrl; }
    QString lastError() const { return m_lastError; }

    int flushIntervalMs() const { return m_flushIntervalMs; }
    void setFlushIntervalMs(int v);

    bool consoleLoggingEnabled() const { return m_consoleLoggingEnabled; }
    void setConsoleLoggingEnabled(bool v);

    LogModel* model() { return &m_model; }

    // Convenience for C++ side
    Q_INVOKABLE void clearDisplay(); // clears display only (not file)

    Q_INVOKABLE void installQtMessageHandler(bool alsoCaptureStderr = false);
    Q_INVOKABLE void uninstallQtMessageHandler();
    int debugMsg() const { return int(QtDebugMsg); }
    int infoMsg() const { return int(QtInfoMsg); }
    int warningMsg() const { return int(QtWarningMsg); }
    int criticalMsg() const { return int(QtCriticalMsg); }
    int fatalMsg() const { return int(QtFatalMsg); }

    Q_INVOKABLE void debug(const QString &msg) { enqueueEntry(QtDebugMsg, msg); }
    Q_INVOKABLE void info (const QString &msg) { enqueueEntry(QtInfoMsg,  msg); }
    Q_INVOKABLE void warn (const QString &msg) { enqueueEntry(QtWarningMsg, msg); }
    Q_INVOKABLE void error(const QString &msg) { enqueueEntry(QtCriticalMsg, msg); }
    Q_INVOKABLE void fatal(const QString &msg) { enqueueEntry(QtFatalMsg, msg); }


public slots:
    // From QML button signals
    void annotateRequested(const QString &text);
    void scrollToBottomRequested();
    void clearDisplayRequested();
    void copyPathRequested();
    void sendToTermbinRequested();

private slots:
    void flushPendingToModel();


signals:
    void logTextChanged();
    void debugEnabledChanged();
    void commDebugEnabledChanged();
    void rigctlDebugEnabledChanged();
    void logFilePathChanged();
    void logDirPathChanged();

    void lastTermbinUrlChanged();
    void lastErrorChanged();

    // Let QML do the scroll if you prefer (or connect in C++)
    void requestScrollToBottom();
    void flushIntervalMsChanged();
    void consoleLoggingEnabledChanged();

private:
    void beginShutdown();
    void enqueueEntry(QtMsgType type, const QString& text);
    void ensureLogFileOpen_locked();
    void setError(const QString &msg);
    void setTermbinUrl(const QString &url);

    QString m_lastTermbinUrl;
    QString m_lastError;

    static void qtMessageHandler(QtMsgType type,
                                 const QMessageLogContext &context,
                                 const QString &msg);

    static std::atomic<LoggingController*> s_instance;   // <-- atomic, not raw
    static QtMessageHandler s_prevHandler;

    std::atomic_bool m_shuttingDown{false};

    bool m_debugEnabled = false;
    bool m_commDebugEnabled = false;
    bool m_rigctlDebugEnabled = false;

    QString m_logFilePath;
    QFile m_logFile;
    QTextStream m_logStream;

    mutable QMutex m_textMutex;

    QString m_logText;
    QString m_pending; // accumulated since last flush

    // throttling timer
    QTimer m_flushTimer;
    int m_flushIntervalMs = 100;

    bool m_handlerInstalled = false;
    bool m_consoleLoggingEnabled = false;

    LogModel m_model;
    QMutex m_pendingMutex;
    QVector<LogEntry> m_pendingEntries;
};


#endif // LOGGINGCONTROLLER_H
