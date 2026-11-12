#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#include <QObject>
#include <QQmlPropertyMap>
#include <QSettings>
#include <QFile>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QSerialPortInfo>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QAbstractSocket>
#include <QAbstractTableModel>
#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QMutex>
#include <QHash>
#include <vector>

#include "audioconverter.h"
#include "rigidentities.h"
#include "rigserver.h"
#include "wfviewtypes.h"
#include "cluster.h"
#include "prefs.h" // So we can get all of the enums
#include "colorprefs.h"
#include "audiodevices.h"
#include "freqctrlquick.h"

#include "ControllerController.h"

class ShortcutOptionModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        TextRole = Qt::UserRole + 1,
        ValueRole
    };
    Q_ENUM(Roles)

    explicit ShortcutOptionModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setOptions(const QVector<QPair<QString, QString>>& options);
    int indexOfValue(const QString& value) const;

private:
    QVector<QPair<QString, QString>> m_options;
};

class ShortcutSettingsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ShortcutEnabledRole = Qt::UserRole + 1,
        ShortcutSequenceRole,
        ShortcutCommandRole,
        ShortcutActionRole,
        ShortcutValueRole,
        ShortcutReceiverRole
    };
    Q_ENUM(Roles)

    explicit ShortcutSettingsModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setShortcuts(QVector<shortcutPreference>* shortcuts);
    void beginPrepend();
    void endPrepend();
    void beginDuplicate(int index);
    void endDuplicate();
    void beginRemove(int index);
    void endRemove();
    void notifyRowChanged(int row);
    void beginModelReset();
    void endModelReset();

private:
    QVector<shortcutPreference>* m_shortcuts = nullptr;
};


class ClusterSettingsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        Server = 0,
        Port,
        UserName,
        Password,
        Timeout,
        IsDefault,
        ColumnCount
    };
    Q_ENUM(Column)

    enum Roles {
        ServerRole   = Qt::UserRole + 1,
        PortRole,
        UserNameRole,
        PasswordRole,
        TimeoutRole,
        IsDefaultRole
    };
    Q_ENUM(Roles)

    Q_INVOKABLE void setDefaultRow(int row, bool on)
    {
        if (row < 0 || row >= m_items.count())
            return;

        bool anyChanged = false;

        if (!on) {
            // Keep one active default. Users choose another row rather than clearing all.
            emit dataChanged(index(row, IsDefault), index(row, IsDefault), { IsDefaultRole, Qt::CheckStateRole });
            return;
        } else {
            // turning ON: this row true, all others false
            for (int i = 0; i < m_items.count(); ++i) {
                const bool want = (i == row);
                if (m_items[i].isdefault != want) {
                    m_items[i].isdefault = want;
                    anyChanged = true;
                }
            }
        }

        if (anyChanged && m_items.count() > 0) {
            // update default column + role for all rows (simple + reliable)
            emit dataChanged(index(0, IsDefault),
                             index(m_items.count() - 1, IsDefault),
                             { Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole, IsDefaultRole });
            emit modified();
        }
    }

    Q_INVOKABLE int defaultRow() const
    {
        for (int i = 0; i < m_items.count(); ++i) {
            if (m_items.at(i).isdefault)
                return i;
        }
        return m_items.isEmpty() ? -1 : 0;
    }

    explicit ClusterSettingsModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent) {}

    // ---------------- Model basics ----------------

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid()) return 0;
        return m_items.count();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override {
        Q_UNUSED(parent);
        return ColumnCount;
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            { ServerRole,   "server" },
            { PortRole,     "port" },
            { UserNameRole, "userName" },
            { PasswordRole, "password" },
            { TimeoutRole,  "timeout" },
            { IsDefaultRole,"isdefault" }
        };
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) return {};
        const int r = index.row();
        const int c = index.column();
        if (r < 0 || r >= m_items.count()) return {};

        const clusterSettings &it = m_items.at(r);

        // ---- QML role-based access (ListView/Repeater delegates) ----
        switch (role) {
        case ServerRole:    return it.server;
        case PortRole:      return it.port;
        case UserNameRole:  return it.userName;
        case PasswordRole:  return it.password;
        case TimeoutRole:   return it.timeout;
        case IsDefaultRole: return it.isdefault;
        default: break;
        }

        // ---- Optional: keep table-style column access for other views ----
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (c) {
            case Server:    return it.server;
            case Port:      return it.port;
            case UserName:  return it.userName;
            case Password:  return it.password;
            case Timeout:   return it.timeout;
            case IsDefault: return it.isdefault;
            default:        return {};
            }
        }

        if (role == Qt::CheckStateRole && c == IsDefault)
            return it.isdefault ? Qt::Checked : Qt::Unchecked;

        return {};
    }



    bool setData(const QModelIndex &index, const QVariant &value, int role) override {
        if (!index.isValid()) return false;
        const int r = index.row();
        if (r < 0 || r >= m_items.count()) return false;

        auto &it = m_items[r];
        bool changed = false;

        switch (role) {
        case ServerRole: {
            const auto v = value.toString();
            if (it.server != v) { it.server = v; changed = true; }
            break;
        }
        case PortRole: {
            const int v = value.toInt();
            if (it.port != v) { it.port = v; changed = true; }
            break;
        }
        case UserNameRole: {
            const auto v = value.toString();
            if (it.userName != v) { it.userName = v; changed = true; }
            break;
        }
        case PasswordRole: {
            const auto v = value.toString();
            if (it.password != v) { it.password = v; changed = true; }
            break;
        }
        case TimeoutRole: {
            const int v = qBound(dxClusterClient::spotLifetimeMin, value.toInt(), dxClusterClient::spotLifetimeMax);
            if (it.timeout != v) { it.timeout = v; changed = true; }
            break;
        }
        case IsDefaultRole: {
            const bool v = value.toBool();
            if (it.isdefault != v) { it.isdefault = v; changed = true; if (v) enforceSingleDefault(r); }
            break;
        }
        default:
            // fall back to your existing EditRole-by-column handling if you kept it
            return QAbstractTableModel::setData(index, value, role);
        }

        if (changed) {
            emit dataChanged(this->index(r, 0), this->index(r, ColumnCount - 1),
                             { role, Qt::DisplayRole, Qt::EditRole });
            emit modified();
        }
        return changed;
    }

    Q_INVOKABLE bool setRoleValue(int row, int role, const QVariant &value)
    {
        if (row < 0 || row >= m_items.count())
            return false;

        // Map role -> column so setData can reuse your existing logic
        int col = 0;
        switch (role) {
        case ServerRole:    col = Server;    break;
        case PortRole:      col = Port;      break;
        case UserNameRole:  col = UserName;  break;
        case PasswordRole:  col = Password;  break;
        case TimeoutRole:   col = Timeout;   break;
        case IsDefaultRole: col = IsDefault; break;
        default:
            return false;
        }

        return setData(index(row, col), value, role);
    }

    // QML cannot read this model's role enums through an instance, so the
    // table edits through these named setters instead of setRoleValue().
    Q_INVOKABLE bool setServer(int row, const QString &value)
    {
        return setData(index(row, Server), value, ServerRole);
    }

    Q_INVOKABLE bool setPort(int row, int value)
    {
        return setData(index(row, Port), value, PortRole);
    }

    Q_INVOKABLE bool setUserName(int row, const QString &value)
    {
        return setData(index(row, UserName), value, UserNameRole);
    }

    Q_INVOKABLE bool setPassword(int row, const QString &value)
    {
        return setData(index(row, Password), value, PasswordRole);
    }

    Q_INVOKABLE bool setTimeout(int row, int value)
    {
        return setData(index(row, Timeout), value, TimeoutRole);
    }

    Q_INVOKABLE QVariant roleValue(int row, int role) const
    {
        if (row < 0 || row >= m_items.count())
            return {};
        int col = 0;
        switch (role) {
        case ServerRole:    col = Server;    break;
        case PortRole:      col = Port;      break;
        case UserNameRole:  col = UserName;  break;
        case PasswordRole:  col = Password;  break;
        case TimeoutRole:   col = Timeout;   break;
        case IsDefaultRole: col = IsDefault; break;
        default:
            return {};
        }
        return data(index(row, col), role);
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid())
            return Qt::NoItemFlags;

        Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;

        if (index.column() == IsDefault)
            f |= Qt::ItemIsUserCheckable;

        return f;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};

        switch (section) {
        case Server:    return "Server";
        case Port:      return "Port";
        case UserName:  return "User";
        case Password:  return "Password";
        case Timeout:   return "Spot Lifetime (sec)";
        case IsDefault: return "Activate";
        default:        return {};
        }
    }

    // ---------------- QML API ----------------

    Q_INVOKABLE void addEntry(const QString &server = {},
                              int port = 7300,
                              const QString &userName = {},
                              const QString &password = {},
                              int timeout = 60,
                              bool isdefault = false)
    {
        const int r = m_items.count();
        beginInsertRows(QModelIndex(), r, r);

        const bool makeDefault = isdefault || m_items.isEmpty();
        m_items.append({
            server,
            port,
            userName,
            password,
            timeout,
            makeDefault
        });

        endInsertRows();

        if (makeDefault)
            enforceSingleDefault(r);
        emit modified();
    }

    Q_INVOKABLE void removeEntry(int row) {
        if (row < 0 || row >= m_items.count())
            return;

        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        endRemoveRows();
        emit modified();
    }

    Q_INVOKABLE void clearAll() {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit modified();
    }

    Q_INVOKABLE int count() const {
        return m_items.count();
    }

    // ---------------- Vector / list bridging ----------------

    void setFromList(const QList<clusterSettings> &list) {
        beginResetModel();
        m_items = list;
        bool hasDefault = false;
        for (const auto &item : std::as_const(m_items)) {
            if (item.isdefault) {
                hasDefault = true;
                break;
            }
        }
        if (!hasDefault && !m_items.isEmpty())
            m_items[0].isdefault = true;
        endResetModel();
    }

    const QList<clusterSettings>& toList() const { return m_items; }


    // Convenience if you still receive std::vector
    void setFromVector(const std::vector<clusterSettings> &v) {
        beginResetModel();
        m_items.clear();
        m_items.reserve(static_cast<int>(v.size()));
        for (const auto &it : v)
            m_items.append(it);
        endResetModel();
    }

private:
    void enforceSingleDefault(int keepRow) {
        bool changed = false;
        for (int i = 0; i < m_items.count(); ++i) {
            if (i == keepRow) continue;
            if (m_items[i].isdefault) {
                m_items[i].isdefault = false;
                changed = true;
            }
        }
        if (changed) {
            emit dataChanged(index(0, IsDefault),
                             index(m_items.count() - 1, IsDefault),
                             { Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole });
        }
    }

private:
    QList<clusterSettings> m_items;

signals:
    void modified();
};

class ServerUsersModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        Username = 0,
        Password,
        UserType,
        Status,
        ColumnCount
    };
    Q_ENUM(Column)

    enum Roles {
        UsernameRole = Qt::UserRole + 1,
        PasswordRole,
        UserTypeRole,
        UserTypeNameRole,
        UserStatusRole,
        UserStatusNameRole,
        HasSavedPasswordRole
    };
    Q_ENUM(Roles)

    // Since the radio server last started.
    enum UserStatus {
        NeverSeen = 0,
        LoggedIn,
        LoggedOut
    };
    Q_ENUM(UserStatus)

    explicit ServerUsersModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : items.count();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        Q_UNUSED(parent)
        return ColumnCount;
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            { UsernameRole, "username" },
            { PasswordRole, "password" },
            { UserTypeRole, "userType" },
            { UserTypeNameRole, "userTypeName" },
            { UserStatusRole, "userStatus" },
            { UserStatusNameRole, "userStatusName" },
            { HasSavedPasswordRole, "hasSavedPassword" }
        };
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= items.count())
            return {};

        const SERVERUSER &user = items.at(index.row());
        switch (role) {
        case UsernameRole: return user.username;
        case PasswordRole: return displayedPassword(index.row());
        case UserTypeRole: return user.userType;
        case UserTypeNameRole: return userTypeName(user.userType);
        case UserStatusRole: return userStatus(user.username);
        case UserStatusNameRole: return userStatusName(userStatus(user.username));
        case HasSavedPasswordRole: return !plainPassword.at(index.row()) && !user.password.isEmpty();
        default: break;
        }

        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (index.column()) {
            case Username: return user.username;
            case Password: return displayedPassword(index.row());
            case UserType: return user.userType;
            case Status: return userStatusName(userStatus(user.username));
            default: return {};
            }
        }

        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= items.count())
            return false;

        SERVERUSER &user = items[index.row()];
        bool changed = false;
        switch (role) {
        case UsernameRole: {
            const QString v = value.toString();
            if (user.username != v) {
                user.username = v;
                changed = true;
            }
            break;
        }
        case PasswordRole: {
            // Any new password replaces the stored (encoded) one.
            const QString v = value.toString();
            if (!plainPassword.at(index.row()) || user.password != v) {
                user.password = v;
                plainPassword[index.row()] = true;
                changed = true;
            }
            break;
        }
        case UserTypeRole: {
            const quint8 v = static_cast<quint8>(value.toUInt());
            if (user.userType != v) {
                user.userType = v;
                changed = true;
            }
            break;
        }
        default:
            return false;
        }

        if (changed) {
            emit dataChanged(this->index(index.row(), 0),
                             this->index(index.row(), ColumnCount - 1),
                             { role, Qt::DisplayRole, Qt::EditRole, UserTypeNameRole,
                               UserStatusRole, UserStatusNameRole, HasSavedPasswordRole });
            emit modified();
        }
        return changed;
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        return index.isValid() ? Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable
                               : Qt::NoItemFlags;
    }

    Q_INVOKABLE bool setRoleValue(int row, int role, const QVariant &value)
    {
        if (row < 0 || row >= items.count())
            return false;

        int col = Username;
        switch (role) {
        case UsernameRole: col = Username; break;
        case PasswordRole: col = Password; break;
        case UserTypeRole: col = UserType; break;
        default: return false;
        }

        return setData(index(row, col), value, role);
    }

    // QML cannot read this model's role enums through an instance, so the
    // table edits through these named setters instead of setRoleValue().
    Q_INVOKABLE bool setUsername(int row, const QString &value)
    {
        return setData(index(row, Username), value, UsernameRole);
    }

    Q_INVOKABLE bool setPassword(int row, const QString &value)
    {
        return setData(index(row, Password), value, PasswordRole);
    }

    Q_INVOKABLE bool setUserType(int row, int value)
    {
        return setData(index(row, UserType), value, UserTypeRole);
    }

    Q_INVOKABLE void addEntry(const QString &username = {}, const QString &password = {}, int userType = 1)
    {
        const int row = items.count();
        beginInsertRows(QModelIndex(), row, row);
        SERVERUSER user;
        user.username = username;
        user.password = password;
        user.userType = static_cast<quint8>(userType);
        items.append(user);
        plainPassword.append(true);
        endInsertRows();
        emit modified();
    }

    Q_INVOKABLE void removeEntry(int row)
    {
        if (row < 0 || row >= items.count())
            return;

        beginRemoveRows(QModelIndex(), row, row);
        items.removeAt(row);
        plainPassword.removeAt(row);
        endRemoveRows();
        emit modified();
    }

    Q_INVOKABLE QString userTypeName(int userType) const
    {
        switch (userType) {
        case serverUserAdmin: return tr("Admin User");
        case serverUserNormal: return tr("Normal User");
        case serverUserNoTx: return tr("Normal with no TX");
        case serverUserDisabled: return tr("Disabled");
        default: return tr("Unknown");
        }
    }

    Q_INVOKABLE QString userStatusName(int status) const
    {
        switch (status) {
        case LoggedIn: return tr("Logged in");
        case LoggedOut: return tr("Logged out");
        case NeverSeen:
        default: return tr("Never seen");
        }
    }

    UserStatus userStatus(const QString &username) const
    {
        return statuses.value(username, NeverSeen);
    }

    // Users missing from the map have not been seen since the server started.
    void setUserStatuses(const QHash<QString, UserStatus> &map)
    {
        if (statuses == map)
            return;
        statuses = map;
        if (!items.isEmpty())
            emit dataChanged(index(0, 0), index(items.count() - 1, ColumnCount - 1),
                             { Qt::DisplayRole, UserStatusRole, UserStatusNameRole });
    }

    // Passwords in the list are expected to be encoded already, as stored.
    void setFromList(const QList<SERVERUSER> &list)
    {
        beginResetModel();
        items = list;
        plainPassword = QVector<bool>(items.count(), false);
        endResetModel();
    }

    // Users with every password encoded, as the servers and settings file want.
    QList<SERVERUSER> toList() const
    {
        QList<SERVERUSER> out = items;
        for (int i = 0; i < out.count(); ++i) {
            if (plainPassword.at(i) && !out[i].password.isEmpty()) {
                QByteArray encoded;
                passcode(out[i].password, encoded);
                out[i].password = QString::fromLatin1(encoded);
            }
        }
        return out;
    }

signals:
    void modified();

private:
    // A stored password is never shown, since it is only the encoded form.
    QString displayedPassword(int row) const
    {
        return plainPassword.at(row) ? items.at(row).password : QString();
    }

    QList<SERVERUSER> items;
    // True where the password was typed this session and is not yet encoded.
    QVector<bool> plainPassword;
    QHash<QString, UserStatus> statuses;
};



// Result of checking the configured radio serial port before connecting.
enum class serialPortProblem {
    None,           // port is usable as far as can be told before opening it
    Blank,          // no port configured (normal on first run)
    AutoNoPorts,    // set to auto, but no serial ports are present
    AutoAmbiguous,  // set to auto, but the radio's port could not be picked
    NotPresent,     // the named port is not on this computer
    NoPermission    // the port exists but this user cannot open it
};

struct serialPortResolution {
    QString configured;   // the setting as stored
    QString port;         // port to open; the resolved port when auto
    serialPortProblem problem = serialPortProblem::None;
    int portsFound = 0;   // ports considered when resolving auto
};

class SettingsController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QQmlPropertyMap * options READ options CONSTANT)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QVariantMap uiSpecs READ getUiSpecs NOTIFY uiSpecsChanged)
    Q_PROPERTY(QVariantList radioProfiles READ radioProfiles NOTIFY radioProfilesChanged)
    Q_PROPERTY(int currentRadioProfileIndex READ currentRadioProfileIndex NOTIFY currentRadioProfileIndexChanged)
    Q_PROPERTY(QVariantList shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QAbstractListModel* shortcutsModel READ shortcutsModel CONSTANT)
    Q_PROPERTY(QAbstractListModel* shortcutAppCommandModel READ shortcutAppCommandModel CONSTANT)
    Q_PROPERTY(QAbstractListModel* shortcutRadioCommandModel READ shortcutRadioCommandModel CONSTANT)
    Q_PROPERTY(QVariantList macros READ macros NOTIFY macrosChanged)
    Q_PROPERTY(int macroSpacingMs READ macroSpacingMs WRITE setMacroSpacingMs NOTIFY macrosChanged)
    Q_PROPERTY(bool macroRawWarningShown READ macroRawWarningShown WRITE setMacroRawWarningShown NOTIFY macrosChanged)
    Q_PROPERTY(int macroMaxSteps READ macroMaxStepCount CONSTANT)
    Q_PROPERTY(int macroMaxNameLength READ macroMaxNameChars CONSTANT)

    Q_PROPERTY(ClusterSettingsModel* clusterModel READ clusterModel CONSTANT)
    Q_PROPERTY(ServerUsersModel* serverUsersModel READ serverUsersModel CONSTANT)
    Q_PROPERTY(ControllerController* controllerController READ controllerController CONSTANT)

public:
    explicit SettingsController(QString file, QObject *p=nullptr);
    ~SettingsController() override = default;

    QQmlPropertyMap* options() const { return m_options; }
    bool dirty() const { return m_dirty; }
    // True when the settings file was last saved by wfview 1.x or 2.x.
    Q_INVOKABLE bool upgradedFromLegacyVersion() const { return m_upgradedFromLegacy; }

    // Generic key/value API (still useful for C++ and QML writes)
    Q_INVOKABLE QVariant option(const QString& key) const;
    Q_INVOKABLE void setOption(const QString& key, const QVariant& value);
    QStringList optionKeys() const { return m_bindings.keys(); }
    Q_INVOKABLE QStringList freqFontNames() const { return FreqCtrlQuick::builtinFontNames(); }
    Q_INVOKABLE bool selectClusterRow(int row);

    QVariantMap getUiSpecs() const { return uiSpecs; }
    QVariantList radioProfiles() const { return m_radioProfiles; }
    int currentRadioProfileIndex() const { return m_currentRadioProfileIndex; }
    QVariantList shortcuts() const;
    QVariantList macros() const;
    int macroSpacingMs() const { return prefs.macroSpacingMs; }
    void setMacroSpacingMs(int ms);
    bool macroRawWarningShown() const { return prefs.macroRawWarningShown; }
    void setMacroRawWarningShown(bool shown);
    int macroMaxStepCount() const { return macroMaxSteps; }
    int macroMaxNameChars() const { return macroMaxNameLength; }

    preferences* getPrefs() { return &prefs;}
    udpPreferences* getUdpPrefs() { return &udpPrefs;}
    QString settingsFileName() const { return settings ? settings->fileName() : QString(); }
    QSettings::Format settingsFormat() const { return settings ? settings->format() : QSettings::IniFormat; }
    SERVERCONFIG* getServerConfig() { return &serverConfig;}
    colorPrefsType getCurrentColorPreset() {return colorPreset[prefs.currentColorPresetNumber];}
    audioDevices* getAudioDevices() {return audioDev.get();}

    ClusterSettingsModel* clusterModel() const { return m_clusterModel.get(); }
    ServerUsersModel* serverUsersModel() const { return m_serverUsersModel.get(); }
    ControllerController* controllerController() const { return m_controllerController; }
    QAbstractListModel* shortcutsModel() const { return m_shortcutsModel.get(); }
    QAbstractListModel* shortcutAppCommandModel() const { return m_shortcutAppCommandModel.get(); }
    QAbstractListModel* shortcutRadioCommandModel() const { return m_shortcutRadioCommandModel.get(); }

    usbDevMap* usbDevices() { return &m_usbDevices; }
    QVector<BUTTON>* usbButtons() { return &m_usbButtons; }
    QVector<KNOB>* usbKnobs() { return &m_usbKnobs; }
    QMutex* usbMutex() { return &m_usbMutex; }


    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void markDirty();
    // Re-enumerate audio devices, e.g. after one is plugged in.
    Q_INVOKABLE void rescanAudioDevices();
    // Re-enumerate serial ports, e.g. after a radio is plugged in.
    Q_INVOKABLE void rescanSerialPorts();
    // True when the named serial port is on this system.
    Q_INVOKABLE bool serialPortExists(const QString& name) const;
    // Turn the configured radio serial port into the port to open, and say
    // whether it looks usable. Resolves "auto".
    serialPortResolution resolveSerialPort() const;
    Q_INVOKABLE void addRadioProfile(const QString& description);
    Q_INVOKABLE void updateRadioProfile(int index, const QString& description = QString());
    Q_INVOKABLE void deleteRadioProfile(int index);
    Q_INVOKABLE void selectRadioProfile(int index);
    Q_INVOKABLE void renameRadioProfile(int index, const QString& description);
    Q_INVOKABLE void moveRadioProfile(int index, int direction);
    Q_INVOKABLE void addShortcut();
    Q_INVOKABLE void duplicateShortcut(int index);
    Q_INVOKABLE void deleteShortcut(int index);
    Q_INVOKABLE void updateShortcut(int index, const QString& field, const QVariant& value);
    Q_INVOKABLE void resetShortcutsToDefault();
    Q_INVOKABLE int shortcutAppCommandIndex(const QString& command) const;
    Q_INVOKABLE int shortcutRadioCommandIndex(const QString& command) const;
    Q_INVOKABLE int addMacro();
    Q_INVOKABLE void deleteMacro(int id);
    Q_INVOKABLE QVariantMap macro(int id) const;
    Q_INVOKABLE void updateMacro(int id, const QString& name, bool showButton, const QVariantList& steps);
    Q_INVOKABLE void setMacroShowButton(int id, bool show);
    Q_INVOKABLE QString sanitizeMacroName(const QString& name) const;
    Q_INVOKABLE QVariantList serverListenAddressOptions() const;
    Q_INVOKABLE void saveLocalAFGain(int gain);
    Q_INVOKABLE QVariantMap restoredMainWindowGeometry() const;
    Q_INVOKABLE void saveMainWindowGeometry(int x, int y, int width, int height, bool maximized);
    Q_INVOKABLE QVariantMap receiverSettings(int index) const;
    Q_INVOKABLE QVariant receiverSetting(int index, const QString& key, const QVariant& fallback = QVariant()) const;
    Q_INVOKABLE void saveReceiverSetting(int index, const QString& key, const QVariant& value);
    void ensureReceiverSettings(int index);
    void resetToDefaults();

    // Flags
    Q_DECLARE_FLAGS(prefIfItems, prefIfItem)
    Q_DECLARE_FLAGS(prefRsItems, prefRsItem)
    Q_DECLARE_FLAGS(prefRaItems ,prefRaItem)
    Q_DECLARE_FLAGS(prefCtItems, prefCtItem)
    Q_DECLARE_FLAGS(prefLanItems, prefLanItem)
    Q_DECLARE_FLAGS(prefClusterItems, prefClusterItem)
    Q_DECLARE_FLAGS(prefUDPItems, prefUDPItem)
    Q_DECLARE_FLAGS(prefServerItems, prefServerItem)

    Q_FLAGS(prefIfItems)
    Q_FLAGS(prefRsItems)
    Q_FLAGS(prefRaItems)
    Q_FLAGS(prefCtItems)
    Q_FLAGS(prefLanItems)
    Q_FLAGS(prefClusterItems)
    Q_FLAGS(prefUDPItems)
    Q_FLAGS(prefServerItems)

signals:
    void ifChanged(prefIfItems items);
    void colChanged(prefColItems items);
    void rsChanged(prefRsItems items);
    void raChanged(prefRaItems items);
    void ctChanged(prefCtItems items);
    void lanChanged(prefLanItems items);
    void clusterChanged(prefClusterItems items);
    void udpChanged(prefUDPItems items);
    void serverChanged(prefServerItems items);
    void audioProcChanged();

    void optionChanged(const QString& key, const QVariant& value);
    void dirtyChanged();
    void saved();
    void uiSpecsChanged();
    void radioProfilesChanged();
    void currentRadioProfileIndexChanged();
    void shortcutsChanged();
    void macrosChanged();

private:
    void setDefPrefs();

    void buildUiSpecs();
    void updateDefaultClusterPrefs();
    void refreshShortcutAppCommandOptions();
    int macroIndex(int id) const;

    struct Binding {
        std::function<QVariant()> get;
        std::function<bool(const QVariant&)> set; // returns "changed"
        std::function<void()> notify;
    };


    void buildBindings();
    void seedOptionsFromBindings();     // push current values into the QQmlPropertyMap
    void updateOptionInMap(const QString& key, const QVariant& v);
    void refreshAudioDevices();
    bool resolveAudioDevice(audioSetup& setup, const QString& type);
    void applyManufacturerDefaults();
    void loadRadioProfiles();
    void saveRadioProfiles();
    QVariantMap currentRadioProfileValues() const;
    void applyRadioProfileValues(const QVariantMap& values);
    QString defaultRadioProfileDescription() const;

    void emitGroupChange(const Binding& b);
    void refreshCurrentColorPresetOptions(bool loading=true);
    QVariantMap defaultReceiverSettings(int index) const;

    colorPrefsType& curColor();
    const colorPrefsType& curColor() const;

    QString fileName;

    colorPrefsType colorPreset[numColorPresetsTotal];
    preferences prefs;
    udpPreferences udpPrefs;
    preferences defPrefs;
    udpPreferences udpDefPrefs;

    SERVERCONFIG serverConfig;

    std::unique_ptr<QSettings> settings;
    std::unique_ptr<audioDevices> audioDev;
    std::unique_ptr<ClusterSettingsModel> m_clusterModel;
    std::unique_ptr<ServerUsersModel> m_serverUsersModel;
    std::unique_ptr<ShortcutSettingsModel> m_shortcutsModel;
    std::unique_ptr<ShortcutOptionModel> m_shortcutAppCommandModel;
    std::unique_ptr<ShortcutOptionModel> m_shortcutRadioCommandModel;

    QQmlPropertyMap* m_options = nullptr;
    bool m_dirty = false;
    bool m_upgradedFromLegacy = false;
    QVector<QVariantMap> m_receiverSettings;
    QVariantList m_radioProfiles;
    int m_currentRadioProfileIndex = -1;

    QHash<QString, Binding> m_bindings;

    QVariantMap uiSpecs;

    ControllerController* m_controllerController;

    usbDevMap m_usbDevices;
    QVector<BUTTON> m_usbButtons;
    QVector<KNOB> m_usbKnobs;
    QMutex m_usbMutex;

};



Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefIfItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefRsItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefRaItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefCtItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefLanItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefClusterItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefUDPItems)
Q_DECLARE_OPERATORS_FOR_FLAGS(SettingsController::prefServerItems)

#endif // SETTINGSCONTROLLER_H
