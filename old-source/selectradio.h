#ifndef SELECTRADIO_H
#define SELECTRADIO_H

#include <QObject>
#include <QQmlContext>
#include <QQuickView>
#include <QQuickItem>
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

class SelectRadio : public QObject
{
    Q_OBJECT

public:
    explicit SelectRadio(QObject* parent = nullptr);
    ~SelectRadio();

    void audioOutputLevel(quint16 level);
    void audioInputLevel(quint16 level);
    void addTimeDifference(qint64 time);
    void show();
    void hide();

public slots:
    void setInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip);
    void spectrumTime(double time);
    void waterfallTime(double time);
    void populate(QList<radio_cap_packet> radios);

signals:
    void selectedRadio(quint8 radio);

private slots:
    void onRadioSelected(int radio);

private:
    QQuickView* m_view;
    QQuickItem* m_rootObject;
    RadioTableModel* m_tableModel;

    int m_timeDifferenceCounter;
    int m_waterfallCounter;
    int m_spectrumCounter;
};

#endif // SELECTRADIO_H
