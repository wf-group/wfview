#include "LogModel.h"

LogModel::LogModel(QObject* parent) : QAbstractListModel(parent) {}

int LogModel::rowCount(const QModelIndex&) const
{
    return m_visible.size();
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    const LogEntry& e = m_entries[m_visible[index.row()]];

    switch (role) {
    case LineRole: return e.text;
    case TypeRole: return e.type;
    }
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        { LineRole, "line" },
        { TypeRole, "type" }
    };
}

void LogModel::append(const LogEntry& e)
{
    if (m_paused) return;

    m_entries.push_back(e);

    const int srcIndex = m_entries.size() - 1;
    if (m_filter.isEmpty() || e.text.contains(m_filter, Qt::CaseInsensitive)) {
        beginInsertRows({}, m_visible.size(), m_visible.size());
        m_visible.push_back(srcIndex);
        endInsertRows();
    }
}

void LogModel::clear()
{
    beginResetModel();
    m_entries.clear();
    m_visible.clear();
    endResetModel();
}

void LogModel::setPaused(bool v)
{
    if (m_paused == v) return;
    m_paused = v;
    emit pausedChanged();
}

void LogModel::setFilter(const QString& f)
{
    if (m_filter == f) return;
    m_filter = f;
    rebuildFilter();
    emit filterChanged();
}

void LogModel::rebuildFilter()
{
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_filter.isEmpty() ||
            m_entries[i].text.contains(m_filter, Qt::CaseInsensitive))
            m_visible.push_back(i);
    }
    endResetModel();
}

QString LogModel::allVisibleText() const
{
    QString out;
    out.reserve(m_visible.size() * 80); // rough
    for (int row = 0; row < m_visible.size(); ++row) {
        const auto& e = m_entries[m_visible[row]];
        out += e.text;
        out += '\n';
    }
    return out;
}

QString LogModel::lineAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) return {};
    return m_entries[m_visible[row]].text;
}

QString LogModel::rangeText(int firstRow, int lastRow) const
{
    if (m_visible.isEmpty()) return {};

    if (firstRow < 0) firstRow = 0;
    if (lastRow >= m_visible.size()) lastRow = m_visible.size() - 1;
    if (lastRow < firstRow) return {};

    QString out;
    out.reserve((lastRow - firstRow + 1) * 80);

    for (int row = firstRow; row <= lastRow; ++row) {
        const auto &e = m_entries[m_visible[row]];
        out += e.text;
        out += '\n';
    }
    return out;
}
