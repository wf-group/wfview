#ifndef DEBUGCONTROLLER_H
#define DEBUGCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QByteArray>

#include "cachingqueue.h"
#include "wfviewtypes.h"

class DebugController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString cacheTitle READ cacheTitle NOTIFY cacheTitleChanged)
    Q_PROPERTY(QString queueTitle READ queueTitle NOTIFY queueTitleChanged)

    Q_PROPERTY(QVariantList cacheModel READ cacheModel NOTIFY cacheModelChanged)
    Q_PROPERTY(QVariantList queueModel READ queueModel NOTIFY queueModelChanged)

    Q_PROPERTY(bool cachePaused READ cachePaused WRITE setCachePaused NOTIFY cachePausedChanged)
    Q_PROPERTY(bool queuePaused READ queuePaused WRITE setQueuePaused NOTIFY queuePausedChanged)

    Q_PROPERTY(int cacheIntervalMs READ cacheIntervalMs WRITE setCacheIntervalMs NOTIFY cacheIntervalMsChanged)
    Q_PROPERTY(int queueIntervalMs READ queueIntervalMs WRITE setQueueIntervalMs NOTIFY queueIntervalMsChanged)

public:
    explicit DebugController(QObject* parent=nullptr);

    QString cacheTitle() const { return m_cacheTitle; }
    QString queueTitle() const { return m_queueTitle; }

    QVariantList cacheModel() const { return m_cacheModel; }
    QVariantList queueModel() const { return m_queueModel; }

    bool cachePaused() const { return m_cachePaused; }
    bool queuePaused() const { return m_queuePaused; }

    int cacheIntervalMs() const { return m_cacheIntervalMs; }
    int queueIntervalMs() const { return m_queueIntervalMs; }

    void setCachePaused(bool v);
    void setQueuePaused(bool v);

    void setCacheIntervalMs(int ms);
    void setQueueIntervalMs(int ms);

    Q_INVOKABLE QString yaesuHexAt(int idx) const;
    Q_INVOKABLE bool yaesuChangedAt(int idx) const;

signals:
    void cacheModelChanged();
    void queueModelChanged();

    void cacheTitleChanged();
    void queueTitleChanged();

    void cachePausedChanged();
    void queuePausedChanged();

    void cacheIntervalMsChanged();
    void queueIntervalMsChanged();

    void acceptRequested();
    void rejectRequested();
    void applyRequested();

private slots:
    void refreshCache();
    void refreshQueue();
    void refreshYaesu();

private:
    QString getValue(const QVariant& val) const;

    cachingQueue* m_queue = nullptr;

    QTimer m_cacheTimer;
    QTimer m_queueTimer;
    QTimer m_yaesuTimer;

    QString m_cacheTitle;
    QString m_queueTitle;

    QVariantList m_cacheModel;
    QVariantList m_queueModel;

    bool m_cachePaused = false;
    bool m_queuePaused = false;

    int m_cacheIntervalMs = 500;
    int m_queueIntervalMs = 1000;

    QByteArray m_yaesuData;
    QByteArray m_yaesuPrev;
};

#endif // DEBUGCONTROLLER_H
