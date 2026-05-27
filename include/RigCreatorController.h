#ifndef RIGCREATORCONTROLLER_H
#define RIGCREATORCONTROLLER_H

#include <QObject>
#include <QUrl>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <QVariantMap>
#include <QQmlPropertyMap>
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QClipboard>
#include <QStandardPaths>
#include "wfviewtypes.h"

// Not sure if any of these are now needed?
#include "rigidentities.h"
#include "cachingqueue.h"


#pragma once
#include <QObject>
#include <QString>
#include <QGuiApplication>
#include <QClipboard>

class ClipboardProxy : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
public:
    explicit ClipboardProxy(QObject *p=nullptr) : QObject(p) {}

    QString text() const {
        if (auto cb = QGuiApplication::clipboard())
            return cb->text();
        return {};
    }

public slots:
    void setText(const QString &t) {
        qInfo() << "Clipboard set:" << t;

        if (auto cb = QGuiApplication::clipboard()) {
            if (cb->text() == t) return;
            cb->setText(t);
            emit textChanged();
        }
    }

signals:
    void textChanged();
};

class IniStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
    explicit IniStore(QObject *p=nullptr) : QObject(p) {}

    bool dirty() const { return m_dirty; }

    Q_INVOKABLE bool load(QString fileUrlOrPath) {
        const QString path = toLocalPath(fileUrlOrPath);
        if (path.isEmpty()) return false;

        m_data.clear();
        QSettings s(path, QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        s.setIniCodec("UTF-8");
#endif
        for (const QString &k : s.allKeys())
            m_data.insert(k, normalize(s.value(k)));

        setDirty(false);
        emit changed();
        return true;
    }


    Q_INVOKABLE bool saveAs(QString fileUrlOrPath) {
        const QString path = toLocalPath(fileUrlOrPath);
        if (path.isEmpty()) return false;

        QSettings out(path, QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        out.setIniCodec("UTF-8");
#endif
        out.clear();
        for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it)
            out.setValue(it.key(), denormalize(it.value()));
        out.sync();
        const bool ok = out.status() == QSettings::NoError;
        if (ok)
            setDirty(false);
        return ok;
    }

    Q_INVOKABLE QVariant get(const QString &key, const QVariant &def = {}) const {
        auto it = m_data.constFind(key);
        return (it == m_data.constEnd()) ? def : it.value();
    }

    Q_INVOKABLE void set(const QString &key, const QVariant &value) {
        const QVariant v = normalize(value);
        if (m_data.value(key) == v) return;
        m_data.insert(key, v);
        setDirty(true);
        emit valueChanged(key, v);
        emit changed();
    }

    Q_INVOKABLE QStringList keys() const { return m_data.keys(); }

    // Compatibility wrappers (match common Qt naming)
    QVariant value(const QString &key, const QVariant &def = {}) const { return get(key, def); }
    void setValue(const QString &key, const QVariant &v)               { set(key, v); }
    bool contains(const QString &key) const                            { return m_data.contains(key); }
    void remove(const QString &key)
    {
        if (!m_data.contains(key))
            return;

        m_data.remove(key);
        setDirty(true);

        // Tell QML/model listeners something changed.
        // For removals, send an invalid QVariant (or {}), so delegates can clear.
        emit valueChanged(key, QVariant());
        emit changed();
    }

    void clearAll()
    {
        if (m_data.isEmpty()) return;
        m_data.clear();
        setDirty(true);
        emit changed();
    }



signals:
    void valueChanged(const QString &key, const QVariant &value);
    void changed();          // coarse “anything changed” signal
    void dirtyChanged();

private:
    static QString toLocalPath(const QString &in) {
        QUrl url(in);
        if (url.isValid() && url.isLocalFile())
            return url.toLocalFile();
        return in;
    }

    static QVariant normalize(const QVariant &v) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (v.typeId() == QMetaType::QString) {
#else
        if (v.type() == QMetaType::QString) {
#endif
            const QString s = v.toString().trimmed().toLower();
            if (s == "true")  return true;
            if (s == "false") return false;
        }
        return v;
    }

    static QVariant denormalize(const QVariant &v) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (v.typeId() == QMetaType::Bool)
            return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
#else
        if (v.type() == QMetaType::Bool)
            return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
#endif
        return v;
    }

    void setDirty(bool d) {
        if (m_dirty == d) return;
        m_dirty = d;
        emit dirtyChanged();
    }

    QVariantMap m_data;
    bool m_dirty = false;
};


class SettingsMap : public QQmlPropertyMap {
    Q_OBJECT
    Q_PROPERTY(QObject* store READ store WRITE setStore NOTIFY storeChanged)

public:
    explicit SettingsMap(QObject *p=nullptr) : QQmlPropertyMap(p) {}

    QObject* store() const { return m_store; }

    void setStore(QObject *s) {
        auto st = qobject_cast<IniStore*>(s);
        if (m_store == st) return;
        if (m_store) disconnect(m_store, nullptr, this, nullptr);
        m_store = st;

        if (m_store) {
            connect(m_store, &IniStore::valueChanged, this,
                    [this](const QString &k, const QVariant &v){
                        QQmlPropertyMap::insert(k, v);
                    });

            connect(m_store, &IniStore::changed, this, &SettingsMap::rebuild);
            rebuild();
        }

        emit storeChanged();
    }

    Q_INVOKABLE void rebuild() {
        if (!m_store) return;

        // No "clear all" exists; just (re)insert keys we know about.
        const auto ks = m_store->keys();
        for (const auto &k : ks)
            QQmlPropertyMap::insert(k, m_store->get(k));
    }

signals:
    void storeChanged();

protected:
    QVariant updateValue(const QString &key, const QVariant &value) override {
        if (m_store) m_store->set(key, value);   // memory only
        return value;
    }

private:
    IniStore *m_store = nullptr;
};


class IniSortProxy : public QSortFilterProxyModel {

    Q_OBJECT
    Q_PROPERTY(int visibleRows READ visibleRows NOTIFY visibleRowsChanged)

public:
    explicit IniSortProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent)
    {
        connect(this, &QAbstractItemModel::rowsInserted, this, &IniSortProxy::visibleRowsChanged);
        connect(this, &QAbstractItemModel::rowsRemoved, this, &IniSortProxy::visibleRowsChanged);
        connect(this, &QAbstractItemModel::modelReset, this, &IniSortProxy::visibleRowsChanged);
        connect(this, &QAbstractItemModel::layoutChanged, this, &IniSortProxy::visibleRowsChanged);
    }

    int visibleRows() const { return rowCount(); }

    Q_INVOKABLE int mapRowToSource(int proxyRow) const {
        if (proxyRow < 0 || proxyRow >= rowCount()) return -1;
        const QModelIndex p = index(proxyRow, 0);
        const QModelIndex s = mapToSource(p);
        return s.isValid() ? s.row() : -1;
    }

    Q_INVOKABLE int mapRowFromSource(int sourceRow) const {
        if (!sourceModel()) return -1;
        if (sourceRow < 0 || sourceRow >= sourceModel()->rowCount()) return -1;
        const QModelIndex s = sourceModel()->index(sourceRow, 0);
        const QModelIndex p = mapFromSource(s);
        return p.isValid() ? p.row() : -1;
    }

signals:
    void visibleRowsChanged();
protected:
    bool lessThan(const QModelIndex &left,
                  const QModelIndex &right) const override
    {
        //if (left.column() == 2) {
        //    return left.data(Qt::UserRole).toInt()
        //    < right.data(Qt::UserRole).toInt();
        //}
        return QSortFilterProxyModel::lessThan(left, right);
    }
};

class IniTableModel : public QAbstractTableModel {
    Q_OBJECT
    Q_PROPERTY(QObject* store READ store WRITE setStore NOTIFY storeChanged)
    Q_PROPERTY(QString group READ group WRITE setGroup NOTIFY groupChanged)
    Q_PROPERTY(QStringList columns READ columns WRITE setColumns NOTIFY columnsChanged)

public:
    explicit IniTableModel(QObject *p=nullptr) : QAbstractTableModel(p) {}

    QObject* store() const { return m_store; }
    void setStore(QObject *s) {
        auto st = qobject_cast<IniStore*>(s);
        if (m_store == st) return;
        if (m_store) disconnect(m_store, nullptr, this, nullptr);
        m_store = st;
        if (m_store) connect(m_store, &IniStore::changed, this, &IniTableModel::scheduleReload);
        emit storeChanged();
        scheduleReload();
    }

    QString group() const { return m_group; }
    void setGroup(const QString &g) { if (m_group==g) return; m_group=g; emit groupChanged(); scheduleReload(); }

    QStringList columns() const { return m_cols; }
    void setColumns(const QStringList &c)
    {
        m_cols=c;
        emit columnsChanged();
        scheduleReload();
    }

    Q_INVOKABLE void reload() {
        beginResetModel();
        m_rows = (m_store && !m_group.isEmpty()) ? m_store->get(m_group + "/size", 0).toInt() : 0;
        endResetModel();
    }

    int rowCount(const QModelIndex& = {}) const override { return m_rows; }
    int columnCount(const QModelIndex& = {}) const override { return m_cols.size(); }

    QVariant headerData(int section, Qt::Orientation o, int role) const override {
        if (role != Qt::DisplayRole) return {};
        if (o == Qt::Horizontal && section >= 0 && section < m_cols.size()) return m_cols[section];
        return section + 1;
    }

    Qt::ItemFlags flags(const QModelIndex &idx) const override {
        if (!idx.isValid()) return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
    }

    QVariant data(const QModelIndex &idx, int role) const override {
        if (!idx.isValid()) return {};
        if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
        return valueAt(idx.row(), idx.column());
    }

    bool setData(const QModelIndex &idx, const QVariant &v, int role) override {
        if (!idx.isValid()) return false;

        // QML frequently writes via the "display" role
        if (role != Qt::EditRole && role != Qt::DisplayRole)
            return false;

        setValueAt(idx.row(), idx.column(), v);
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    QHash<int, QByteArray> roleNames() const override {
        auto r = QAbstractTableModel::roleNames();
        r[Qt::DisplayRole] = "display";
        r[Qt::EditRole]    = "edit";
        return r;
    }

    Q_INVOKABLE QVariant cell(int row, int col) const {
        if (row < 0 || row >= rowCount() || col < 0 || col >= columnCount())
            return {};
        return data(index(row, col), Qt::DisplayRole);
    }

    Q_INVOKABLE bool setCell(int row, int col, const QVariant &v) {
        if (row < 0 || row >= rowCount() || col < 0 || col >= columnCount())
            return false;
        return setData(index(row, col), v, Qt::EditRole);
    }

    // Append a new row (adds empty keys and bumps size)
    Q_INVOKABLE int appendRow() {
        return insertRowAt(rowCount());
    }

    Q_INVOKABLE int insertRowAt(int row) {
        row = qBound(0, row, rowCount());
        if (!m_store) return -1;            // whatever your store pointer is
        if (m_group.isEmpty()) return -1;

        beginInsertRows(QModelIndex(), row, row);

        // IMPORTANT: Ini is 1-based and you store group/size
        // Shift existing rows down by one to keep contiguous 1..N
        const int oldRows = m_rows;
        const int newRows = oldRows + 1;

        // Move rows [row+1..oldRows] => [row+2..newRows]
        for (int r = oldRows; r >= row + 1; --r) {
            for (int c = 0; c < m_cols.size(); ++c) {
                const QString kFrom = QString("%1/%2/%3").arg(m_group).arg(r).arg(m_cols[c]);
                const QString kTo   = QString("%1/%2/%3").arg(m_group).arg(r + 1).arg(m_cols[c]);
                const QVariant v = m_store->value(kFrom);
                if (v.isValid()) m_store->setValue(kTo, v);
                m_store->remove(kFrom);
            }
        }

        // Create new empty row at (row+1) in INI space
        const int iniRow = row + 1;
        for (int c = 0; c < m_cols.size(); ++c) {
            const QString k = QString("%1/%2/%3").arg(m_group).arg(iniRow).arg(m_cols[c]);
            if (!m_store->contains(k))
                m_store->setValue(k, QVariant{});
        }

        m_rows = newRows;
        m_store->setValue(m_group + "/size", m_rows);

        endInsertRows();
        return row;
    }

    Q_INVOKABLE bool removeRowAt(int row) {
        if (row < 0 || row >= rowCount()) return false;
        if (!m_store) return false;
        if (m_group.isEmpty()) return false;

        beginRemoveRows(QModelIndex(), row, row);

        const int oldRows = m_rows;
        const int iniRow = row + 1;

        // Remove this row keys
        for (int c = 0; c < m_cols.size(); ++c) {
            const QString k = QString("%1/%2/%3").arg(m_group).arg(iniRow).arg(m_cols[c]);
            m_store->remove(k);
        }

        // Shift rows up: [iniRow+1..oldRows] => [iniRow..oldRows-1]
        for (int r = iniRow + 1; r <= oldRows; ++r) {
            for (int c = 0; c < m_cols.size(); ++c) {
                const QString kFrom = QString("%1/%2/%3").arg(m_group).arg(r).arg(m_cols[c]);
                const QString kTo   = QString("%1/%2/%3").arg(m_group).arg(r - 1).arg(m_cols[c]);
                const QVariant v = m_store->value(kFrom);
                if (v.isValid()) m_store->setValue(kTo, v);
                m_store->remove(kFrom);
            }
        }

        m_rows = oldRows - 1;
        m_store->setValue(m_group + "/size", m_rows);

        endRemoveRows();
        return true;
    }

private slots:

    void scheduleReload() {
        if (m_reloadScheduled) return;
        m_reloadScheduled = true;
        QTimer::singleShot(0, this, [this]{
            m_reloadScheduled = false;
            reload();
        });
    }

signals:
    void storeChanged();
    void groupChanged();
    void columnsChanged();

private:

    bool m_reloadScheduled = false;


    QVariant valueAt(int row0, int col) const {
        if (!m_store) return {};
        const int row1 = row0 + 1;
        const QString key = QString("%1/%2/%3").arg(m_group).arg(row1).arg(m_cols[col]);
        return m_store->get(key);
    }

    void setValueAt(int row0, int col, const QVariant &v) {
        if (!m_store) return;
        const int row1 = row0 + 1;
        const QString key = QString("%1/%2/%3").arg(m_group).arg(row1).arg(m_cols[col]);
        m_store->set(key, v);
    }

    IniStore *m_store = nullptr;
    QString m_group;
    QStringList m_cols;
    int m_rows = 0;
};


class RigCreatorController : public QObject
{
    Q_OBJECT
    //Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QObject* store READ store CONSTANT)
    Q_PROPERTY(QQmlPropertyMap* settings READ settings CONSTANT)

    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool loading READ loading WRITE setLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString iniPath READ iniPath NOTIFY iniPathChanged)
    Q_PROPERTY(QVariantList commandTypeChoices READ commandTypeChoices NOTIFY commandTypeChoicesChanged)

public:
    explicit RigCreatorController(QObject *parent = nullptr);
    ~RigCreatorController();

    Q_INVOKABLE bool loadFile(QString filename);
    Q_INVOKABLE bool saveFile(QString filename);

    Q_INVOKABLE QUrl userRigsFolder() const {
        QString p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/rigs";
        QDir().mkpath(p);
        return QUrl::fromLocalFile(QDir::cleanPath(p));
    }

    Q_INVOKABLE QUrl suggestedSaveFile() const {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/rigs";
        QDir().mkpath(dir);
        const QString name = currentFile.isEmpty() ? QStringLiteral("custom.rig") : QFileInfo(currentFile).fileName();
        return QUrl::fromLocalFile(QDir(dir).absoluteFilePath(name));
    }

    Q_INVOKABLE QUrl defaultRigsFolder() const {
        QString p = QCoreApplication::applicationDirPath();
#ifdef Q_OS_LINUX
        p += "/../share/wfview/rigs";
#else
        p += "/rigs";
#endif
        p = QDir::cleanPath(p);
        return QUrl::fromLocalFile(p);
    }

    Q_INVOKABLE QVariantList modeNameChoices() const {
        QVariantList choices;
        if (!m_store)
            return choices;

        const QString prefix = QStringLiteral("Rig/Modes");
        const int count = m_store->get(prefix + QStringLiteral("/size"), 0).toInt();

        for (int row = 1; row <= count; ++row) {
            const QString name = m_store->get(QStringLiteral("%1/%2/Name").arg(prefix).arg(row)).toString().trimmed();
            if (!name.isEmpty())
                choices.append(QVariantMap{{QStringLiteral("text"), name}, {QStringLiteral("value"), name}});
        }

        return choices;
    }

    bool loading() const { return m_loading; }

    void setLoading(bool on) {
        if (m_loading == on) return;
        m_loading = on;
        emit loadingChanged();
    }

    QObject* store() const { return m_store; }
    QQmlPropertyMap* settings() const { return m_settings; }
    bool dirty() const { return m_store && m_store->dirty(); }
    QString iniPath() const { return m_iniPath; }
    QVariantList commandTypeChoices() const { return m_commandTypeChoices; }
    Q_INVOKABLE bool hasCurrentFile() const { return !currentFile.isEmpty(); }
    Q_INVOKABLE bool saveCurrentFile() { return !currentFile.isEmpty() && saveFile(currentFile); }





signals:
    void showMessage(QString title, QString text);
    void loadingChanged();
    void iniPathChanged();
    void commandTypeChoicesChanged();
    void dirtyChanged();


private slots:
    void changed();


private:
    QString currentFile;
    bool settingsChanged=false;

    const float kenwoodTones[42]{67.0f,69.3f,71.9f,74.4f,77.0f,79.7f,82.5f,85.4f,88.5f,91.5f,95.8f,97.4f,100.0f,103.5f,107.2f,110.9f,
                                 114.8f,118.8f,123.0f,127.3f,131.8f,136.5f,141.3f,146.2f,151.4f,156.7f,162.2f,167.9f,173.8f,179.9f,
                                 186.2f,192.8f,203.5f,206.5f,210.7f,218.1f,226.7f,229.1f,233.6f,241.8f,250.3f,254.1f};

    const float yaesuTones[50]{67.0f,69.3f,71.9f,74.4f,77.0f,79.7f,82.5f,85.4f,88.5f,91.5f,94.8f,97.4f,100.0f,103.5f,107.2f,110.9f,
                                 114.8f,118.8f,123.0f,127.3f,131.8f,136.5f,141.3f,146.2f,151.4f,156.7f,159.8f,162.2f,165.5f,167.9f,171.3f,173.8f,177.3f,179.9f,
                                 183.5f,186.2f,189.9f,192.8f,196.6f,199.5f,203.5f,206.5f,210.7f,218.1f,226.7f,229.1f,233.6f,241.8f,250.3f,254.1f};


    const float icomTones[48]{67.0f,69.3f,71.9f,74.4f,77.0f,79.7f,82.5f,85.4f,88.5f,91.5f,94.8f,97.4f,100.0f,103.5f,107.2f,110.9f,
                              114.8f,118.8f,123.0f,127.3f,131.8f,136.5f,141.3f,146.2f,151.4f,156.7f,159.8f,162.2f,165.5f,167.9f,173.8f,177.3f,179.9f,
                              183.5f,186.2f,192.8f,196.6f,199.5f,203.5f,206.5f,210.7f,218.1f,225.7f,229.1f,233.6f,241.8f,250.3f,254.1f};

    const short icomDtcs[104]{23,25,26,31,32,36,43,47,51,53,54,65,71,72,73,74,114,115,116,122,125,131,132,134,143,145,152,155,156,162,165,172,174,
                            205,212,223,225,226,243,244,245,246,251,252,255,261,263,265,266,271,274,306,311,315,325,331,332,343,346,351,356,364,365,371,
                            411,412,413,423,431,432,445,446,452,454,455,462,464,465,466,503,506,516,523,526,532,546,565,
                            606,612,624,627,631,632,654,662,664,703,712,723,731,732,734,746,754};


    const QVector<periodicType> defaultPeriodic = {
        {funcFreq,"Medium",0},
        {funcMode,"Medium",0},
        {funcOverflowStatus,"Medium",0},
        {funcScopeMode,"Medium High",0},
        {funcScopeSpan,"Medium High",0},
        {funcScopeSingleDual,"Medium High",0},
        {funcScopeMainSub,"Medium High",0},
        {funcScopeSpeed,"Medium",0},
        {funcScopeHold,"Medium",0},
        {funcVFODualWatch,"Medium",0},
        {funcTransceiverStatus,"High",0},
        {funcDATAOffMod,"Medium High",0},
        {funcDATA1Mod,"Medium High",0},
        {funcDATA2Mod,"Medium High",0},
        {funcDATA3Mod,"Medium High",0},
        {funcRFPower,"Medium",0},
        {funcAfGain,"Medium High",-1},
        {funcMonitorGain,"Medium Low",0},
        {funcMonitor,"Medium Low",0},
        {funcRfGain,"Medium",0},
        {funcTunerStatus,"Medium",0},
        {funcTuningStep,"Medium Low",0},
        {funcAttenuator,"Medium Low",0},
        {funcPreamp,"Medium Low",0},
        {funcAntenna,"Medium Low",0},
        {funcToneSquelchType,"Medium Low",0},
        {funcSMeter,"Highest",0}
    };

    bool m_loading = false;
    QString m_iniPath;
    QVariantList m_commandTypeChoices;
    QVariantMap m_work;   // working copy: key->value (including groups)
    bool m_dirty = false;
    IniStore *m_store = nullptr;
    SettingsMap *m_settings = nullptr;
};





#endif // RIGCREATORCONTROLLER_H
