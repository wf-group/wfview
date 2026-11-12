#ifndef SELECTRADIOCONTROLLER_H
#define SELECTRADIOCONTROLLER_H

#include <QObject>
#include <QAbstractTableModel>
#include <QList>
#include <QtEndian>
#include <QHostInfo>

#include "packettypes.h"

// Table model for the radio list
class RadioTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum RadioRoles {
        NameRole = Qt::UserRole + 1,
        CivRole,
        BaudrateRole,
        UserRole,
        IpRole,
        BusyRole,
        RowRole
    };

    explicit RadioTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void populate(const QList<radio_cap_packet>& radios);
    void setInUse(quint8 radio, bool admin, quint8 busy, const QString& user, const QString& ip);
    void clear();

private:
    struct RadioInfo {
        QString name;
        QString civ;
        QString baudrate;
        QString user;
        QString ip;
        quint8 busy;
    };

    QList<RadioInfo> m_radios;
};

class SelectRadioController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RadioTableModel* tableModel READ tableModel CONSTANT)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(QString txAudioOwner READ txAudioOwner NOTIFY txAudioStateChanged)
    Q_PROPERTY(QString txAudioSource READ txAudioSource NOTIFY txAudioStateChanged)

public:
    explicit SelectRadioController(QObject* parent = nullptr);
    ~SelectRadioController();

    RadioTableModel* tableModel() const { return m_tableModel; }
    bool visible() const { return m_visible; }
    QString txAudioOwner() const { return m_txAudioOwner; }
    QString txAudioSource() const { return m_txAudioSource; }
    void setVisible(bool visible);

    void populate(QList<radio_cap_packet> radios);
    void setTxAudioState(const QString& owner, const QString& source);

    Q_INVOKABLE void audioOutputLevel(quint16 level);
    Q_INVOKABLE void audioInputLevel(quint16 level);
    Q_INVOKABLE void addTimeDifference(qint64 time);
    Q_INVOKABLE void onRadioSelected(int radio);

public slots:
    void setInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void spectrumTime(double time);
    void waterfallTime(double time);

signals:
    void selectedRadio(quint8 radio);
    void visibleChanged();
    void audioOutputLevelChanged(int level);
    void audioInputLevelChanged(int level);
    void timeDifferencePointAdded(int counter, double time);
    void waterfallPointAdded(int counter, double time);
    void spectrumPointAdded(int counter, double time);
    void txAudioStateChanged();

private:
    RadioTableModel* m_tableModel;
    bool m_visible;

    int m_timeDifferenceCounter;
    int m_waterfallCounter;
    int m_spectrumCounter;
    QString m_txAudioOwner;
    QString m_txAudioSource;
};

#endif // SELECTRADIOCONTROLLER_H
