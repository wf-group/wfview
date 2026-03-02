#ifndef CONTROLLERCONTROLLER_H
#define CONTROLLERCONTROLLER_H

#include <QObject>
#include <QAbstractListModel>
#include <QPoint>
#include <QColor>
#include <QUrl>
#include <QMutex>
#include <QVector>
#include <QMap>
#include <QImage>
#include <QQuickItem>
#include <QJSValue>

#include "usbcontroller.h"

// Forward declarations
struct BUTTON;
struct KNOB;
struct COMMAND;

/**
 * @brief Model for USB controller devices
 */
class DeviceModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum DeviceRoles {
        DeviceNameRole = Qt::UserRole + 1,
        DevicePathRole,
        ConnectedRole,
        CurrentPageRole,
        TotalPagesRole,
        DisabledRole
    };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addDevice(USBDEVICE* dev);
    void removeDevice(const QString& path);
    void updateDevice(const QString& path);
    USBDEVICE* getDevice(const QString& path);
    USBDEVICE* getDevice(int index);

private:
    QVector<USBDEVICE*> m_devices;
    QMap<QString, int> m_pathToIndex;
};

/**
 * @brief Model for command items
 */
class CommandModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum CommandRoles {
        TextRole = Qt::UserRole + 1,
        IndexRole,
        IsSeparatorRole
    };

    explicit CommandModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCommands(QVector<COMMAND>* commands, bool buttonCommands);

private:
    QVector<COMMAND*> m_commands;
};

/**
 * @brief Controller for managing USB controller settings in QML
 */
class ControllerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DeviceModel* deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(CommandModel* commandModel READ commandModel CONSTANT)
    Q_PROPERTY(CommandModel* knobCommandModel READ knobCommandModel CONSTANT)

public:
    explicit ControllerController(QObject *parent = nullptr);
    ~ControllerController();

    DeviceModel* deviceModel() { return m_deviceModel; }
    CommandModel* commandModel() { return m_commandModel; }
    CommandModel* knobCommandModel() { return m_knobCommandModel; }

    // Initialize with data from main application
    Q_INVOKABLE void init(usbDevMap* devices, 
                         QVector<BUTTON>* buttons, 
                         QVector<KNOB>* knobs,
                         QVector<COMMAND>* commands,
                         QMutex* mutex);

    // Device management
    Q_INVOKABLE void newDevice(USBDEVICE* dev);
    Q_INVOKABLE void removeDevice(USBDEVICE* dev);
    Q_INVOKABLE void setConnected(USBDEVICE* dev);

    // Get controller image path based on device type
    Q_INVOKABLE QString getControllerImagePath(const QString& devicePath);
    
    // Get buttons/knobs for a specific page (returns QVariantList for QML)
    Q_INVOKABLE QVariantList getButtonsForPage(const QString& devicePath, int page);
    Q_INVOKABLE QVariantList getKnobsForPage(const QString& devicePath, int page);

    // User interactions
    Q_INVOKABLE void showContextMenu(const QString& devicePath, const QPoint& pos);
    Q_INVOKABLE void buttonPressed(const QString& devicePath, const QPoint& pos, bool pressed);

    // Settings
    Q_INVOKABLE void setPage(const QString& devicePath, int page);
    Q_INVOKABLE void setTotalPages(const QString& devicePath, int pages);
    Q_INVOKABLE void setSensitivity(const QString& devicePath, int value);
    Q_INVOKABLE void setBrightness(const QString& devicePath, int index);
    Q_INVOKABLE void setSpeed(const QString& devicePath, int index);
    Q_INVOKABLE void setOrientation(const QString& devicePath, int index);
    Q_INVOKABLE void setColor(const QString& devicePath, const QColor& color);
    Q_INVOKABLE void setTimeout(const QString& devicePath, int minutes);
    Q_INVOKABLE void setDeviceDisabled(const QString& devicePath, bool disabled);

    // Button/Knob configuration
    Q_INVOKABLE void applyButtonConfig(int onCommand, 
                                       int offCommand, 
                                       int knobCommand,
                                       bool toggle, 
                                       int ledNumber);

    // Backup/Restore
    Q_INVOKABLE void backupSettings(const QString& devicePath, const QUrl& fileUrl);
    Q_INVOKABLE void restoreSettings(const QString& devicePath, const QUrl& fileUrl);

signals:
    void sendRequest(USBDEVICE* dev, usbFeatureType request, int val = 0, 
                    QString text = "", QImage* img = nullptr, QColor* color = nullptr);
    void programDisable(USBDEVICE* dev, bool disable);
    void programPages(USBDEVICE* dev, int pages);
    void backup(USBDEVICE* dev, QString path);
    void restore(USBDEVICE* dev, QString path);

    void deviceAdded(int index);
    void deviceRemoved(int index);
    void deviceUpdated(const QString& path);
    void showConfigDialog(const QString& title, bool isButton, bool isKnob);

private:
    BUTTON* findButton(const QString& devicePath, const QPoint& pos, int page);
    KNOB* findKnob(const QString& devicePath, const QPoint& pos, int page);
    void updatePage(USBDEVICE* dev, int page);

    DeviceModel* m_deviceModel;
    CommandModel* m_commandModel;
    CommandModel* m_knobCommandModel;

    usbDevMap* m_devices;
    QVector<BUTTON>* m_buttons;
    QVector<KNOB>* m_knobs;
    QVector<COMMAND>* m_commands;
    QMutex* m_mutex;

    BUTTON* m_currentButton;
    KNOB* m_currentKnob;
};

#endif // CONTROLLERCONTROLLER_H
