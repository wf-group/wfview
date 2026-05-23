#include "logcategories.h"
#include "selectradio.h"
#include <QQmlEngine>
#include <QQmlComponent>
#include <QMetaObject>

// RadioTableModel implementation
RadioTableModel::RadioTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int RadioTableModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return m_radios.count();
}

int RadioTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 5; // Name, CIV, Baudrate, User, IP
}

QVariant RadioTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_radios.count())
        return QVariant();

    const RadioInfo& radio = m_radios[index.row()];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return radio.name;
        case 1: return radio.civ;
        case 2: return radio.baudrate;
        case 3: return radio.user;
        case 4: return radio.ip;
        }
    }
    else if (role == NameRole) return radio.name;
    else if (role == CivRole) return radio.civ;
    else if (role == BaudrateRole) return radio.baudrate;
    else if (role == UserRole) return radio.user;
    else if (role == IpRole) return radio.ip;
    else if (role == BusyRole) return radio.busy;
    else if (role == RowRole) return index.row();

    return QVariant();
}

QHash<int, QByteArray> RadioTableModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[CivRole] = "civ";
    roles[BaudrateRole] = "baudrate";
    roles[UserRole] = "user";
    roles[IpRole] = "ip";
    roles[BusyRole] = "busy";
    roles[RowRole] = "row";
    roles[Qt::DisplayRole] = "display";
    return roles;
}

void RadioTableModel::populate(const QList<radio_cap_packet>& radios)
{
    beginResetModel();
    m_radios.clear();

    for (const auto& radio : radios) {
        RadioInfo info;
        info.name = QString(radio.name);
        info.civ = QString("0x%1").arg((uchar)radio.civ, 2, 16, QLatin1Char('0')).toUpper();
        info.baudrate = QString::number(qFromBigEndian(radio.baudrate));
        info.user = "";
        info.ip = "";
        info.busy = 0;
        m_radios.append(info);
    }

    endResetModel();
}

void RadioTableModel::setInUse(quint8 radio, bool admin, quint8 busy, const QString& user, const QString& ip)
{
    Q_UNUSED(admin)

    if (radio >= m_radios.count())
        return;

    m_radios[radio].user = user;
    m_radios[radio].ip = ip;
    m_radios[radio].busy = busy;

    QModelIndex topLeft = index(radio, 0);
    QModelIndex bottomRight = index(radio, 4);
    emit dataChanged(topLeft, bottomRight);
}

void RadioTableModel::clear()
{
    beginResetModel();
    m_radios.clear();
    endResetModel();
}

// SelectRadio implementation
SelectRadio::SelectRadio(QObject* parent)
    : QObject(parent)
    , m_view(nullptr)
    , m_rootObject(nullptr)
    , m_tableModel(new RadioTableModel(this))
    , m_timeDifferenceCounter(0)
    , m_waterfallCounter(0)
    , m_spectrumCounter(0)
{
    // Create QML view
    m_view = new QQuickView();
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);

    // Set up context properties
    m_view->rootContext()->setContextProperty("radioTableModel", m_tableModel);

    // Load QML file
    m_view->setSource(QUrl("qrc:/resources/SelectRadio.qml"));

    if (m_view->status() == QQuickView::Error) {
        qCritical() << "Error loading SelectRadio QML";
        return;
    }

    m_rootObject = m_view->rootObject();

    if (m_rootObject) {
        // Connect the selectedRadio signal from QML
        connect(m_rootObject, SIGNAL(selectedRadio(int)),
                this, SLOT(onRadioSelected(int)));
    }
}

SelectRadio::~SelectRadio()
{
    if (m_view) {
        delete m_view;
    }
}

void SelectRadio::populate(QList<radio_cap_packet> radios)
{
    m_tableModel->populate(radios);

    if (radios.count() > 1) {
        show();
    }
}

void SelectRadio::setInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip)
{
    m_tableModel->setInUse(radio, admin, busy, user, ip);
}

void SelectRadio::onRadioSelected(int radio)
{
    emit selectedRadio(static_cast<quint8>(radio));
}

void SelectRadio::show()
{
    if (m_view) {
        m_view->show();
    }
}

void SelectRadio::hide()
{
    if (m_view) {
        m_view->hide();
    }
}

void SelectRadio::audioOutputLevel(quint16 level)
{
    if (m_rootObject) {
        QMetaObject::invokeMethod(m_rootObject, "setAudioOutputLevel",
                                  Q_ARG(QVariant, level));
    }
}

void SelectRadio::audioInputLevel(quint16 level)
{
    if (m_rootObject) {
        QMetaObject::invokeMethod(m_rootObject, "setAudioInputLevel",
                                  Q_ARG(QVariant, level));
    }
}

void SelectRadio::addTimeDifference(qint64 time)
{
    if (m_rootObject) {
        QMetaObject::invokeMethod(m_rootObject, "addTimeDifferencePoint",
                                  Q_ARG(QVariant, m_timeDifferenceCounter),
                                  Q_ARG(QVariant, time));
        m_timeDifferenceCounter++;
    }
}

void SelectRadio::waterfallTime(double time)
{
    if (m_rootObject) {
        QMetaObject::invokeMethod(m_rootObject, "addWaterfallPoint",
                                  Q_ARG(QVariant, m_waterfallCounter),
                                  Q_ARG(QVariant, time));
        m_waterfallCounter++;
    }
}

void SelectRadio::spectrumTime(double time)
{
    if (m_rootObject) {
        QMetaObject::invokeMethod(m_rootObject, "addSpectrumPoint",
                                  Q_ARG(QVariant, m_spectrumCounter),
                                  Q_ARG(QVariant, time));
        m_spectrumCounter++;
    }
}
