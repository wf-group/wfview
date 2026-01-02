#ifndef LOGMODEL_H
#define LOGMODEL_H

#include <QString>
#include <QtGlobal>
#include <QAbstractListModel>
#include <QVector>

struct LogEntry {
    QtMsgType type;
    QString   text;
};

class LogModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

public:
    enum Roles { LineRole = Qt::UserRole + 1, TypeRole };
    Q_ENUM(Roles)

    explicit LogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    QHash<int, QByteArray> roleNames() const override;

    bool paused() const { return m_paused; }
    void setPaused(bool v);

    QString filter() const { return m_filter; }
    void setFilter(const QString& f);

    void append(const LogEntry& e);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString allVisibleText() const;
    Q_INVOKABLE QString lineAt(int row) const;
    Q_INVOKABLE QString rangeText(int firstRow, int lastRow) const;


signals:
    void pausedChanged();
    void filterChanged();

private:
    QVector<LogEntry> m_entries;
    QVector<int>      m_visible;   // filtered indices
    bool m_paused = false;
    QString m_filter;

    void rebuildFilter();
};


#endif // LOGMODEL_H
