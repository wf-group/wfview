#ifndef CONTROLLERCONTROLLER_H
#define CONTROLLERCONTROLLER_H

#include "usbcontroller.h"

#include <QAbstractListModel>
#include <QColor>
#include <QImage>
#include <QJSValue>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QPoint>
#include <QQuickItem>
#include <QUrl>
#include <QVector>

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
    enum DeviceRoles
    {
        DeviceNameRole = Qt::UserRole + 1,
        DevicePathRole,
        ConnectedRole,
        CurrentPageRole,
        TotalPagesRole,
        DisabledRole,
        SensitivityRole,
        BrightnessRole,
        SpeedRole,
        OrientationRole,
        TimeoutRole,
        ShowSensitivityRole,
        ShowBrightnessRole,
        ShowSpeedRole,
        ShowOrientationRole,
        ShowColorRole,
        ShowTimeoutRole
    };

    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

    void addDevice(USBDEVICE *dev);
    void removeDevice(const QString &path);
    void updateDevice(const QString &path);
    void reset();
    USBDEVICE *getDevice(const QString &path);
    USBDEVICE *getDevice(int index);

private:
    QVector<USBDEVICE *> devices;
    QMap<QString, int> pathToIndex;
};

/**
 * @brief Model for command items
 */
class CommandModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum CommandRoles
    {
        TextRole = Qt::UserRole + 1,
        IndexRole,
        IsSeparatorRole
    };

    explicit CommandModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setCommands(QVector<COMMAND> *commandList, bool buttonCommands);

private:
    QVector<COMMAND *> commandItems;
};

/**
 * @brief Controller for managing USB controller settings in QML
 */
class ControllerController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(DeviceModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(CommandModel *commandModel READ commandModel CONSTANT)
    Q_PROPERTY(CommandModel *knobCommandModel READ knobCommandModel CONSTANT)

public:
    explicit ControllerController(QObject *parent = nullptr);
    ~ControllerController();

    DeviceModel *deviceModel()
    {
        return deviceModelObject;
    }
    CommandModel *commandModel()
    {
        return commandModelObject;
    }
    CommandModel *knobCommandModel()
    {
        return knobCommandModelObject;
    }

    // Initialize with data from main application
    Q_INVOKABLE void init(usbDevMap *deviceMap,
                          QVector<BUTTON> *buttonData,
                          QVector<KNOB> *knobData,
                          QVector<COMMAND> *commandData,
                          QMutex *sharedMutex);

    // Device management
    Q_INVOKABLE void newDevice(USBDEVICE *dev);
    Q_INVOKABLE void removeDevice(USBDEVICE *dev);
    Q_INVOKABLE void setConnected(USBDEVICE *dev);

    // Get controller image path based on device type
    Q_INVOKABLE QString getControllerImagePath(const QString &devicePath);

    // Get buttons/knobs for a specific page (returns QVariantList for QML)
    Q_INVOKABLE QVariantList getButtonsForPage(const QString &devicePath, int page);
    Q_INVOKABLE QVariantList getKnobsForPage(const QString &devicePath, int page);

    // User interactions
    Q_INVOKABLE void showContextMenu(const QString &devicePath, const QPoint &pos);
    Q_INVOKABLE void buttonPressed(const QString &devicePath, const QPoint &pos, bool pressed);

    // Settings
    Q_INVOKABLE void setPage(const QString &devicePath, int page);
    Q_INVOKABLE void setTotalPages(const QString &devicePath, int pages);
    Q_INVOKABLE void setSensitivity(const QString &devicePath, int value);
    Q_INVOKABLE void setBrightness(const QString &devicePath, int index);
    Q_INVOKABLE void setSpeed(const QString &devicePath, int index);
    Q_INVOKABLE void setOrientation(const QString &devicePath, int index);
    Q_INVOKABLE void setColor(const QString &devicePath, const QColor &color);
    Q_INVOKABLE void setTimeout(const QString &devicePath, int minutes);
    Q_INVOKABLE void setDeviceDisabled(const QString &devicePath, bool disabled);

    // Button/Knob configuration
    Q_INVOKABLE void applyButtonConfig(int onCommand, int offCommand, int knobCommand, bool toggle, int ledNumber);
    Q_INVOKABLE void setCurrentButtonColor(bool pressedColor, const QColor &color);

    // Backup/Restore
    Q_INVOKABLE void backupSettings(const QString &devicePath, const QUrl &fileUrl);
    Q_INVOKABLE void restoreSettings(const QString &devicePath, const QUrl &fileUrl);

signals:
    void sendRequest(USBDEVICE *dev,
                     usbFeatureType request,
                     int val = 0,
                     QString text = "",
                     QImage *img = nullptr,
                     QColor *color = nullptr);
    void programDisable(USBDEVICE *dev, bool disable);
    void programPages(USBDEVICE *dev, int pages);
    void backup(USBDEVICE *dev, QString path);
    void restore(USBDEVICE *dev, QString path);
    void commandTriggered(const COMMAND *command);

    void deviceAdded(int index);
    void deviceRemoved(int index);
    void deviceUpdated(const QString &path);
    void showConfigDialog(const QString &title,
                          bool isButton,
                          bool isKnob,
                          int onCommand,
                          int offCommand,
                          int knobCommand,
                          bool toggle,
                          int ledNumber,
                          bool showLed,
                          bool showColor,
                          bool showIcon,
                          const QColor &onColor,
                          const QColor &offColor);

private:
    BUTTON *findButton(const QString &devicePath, const QPoint &pos, int page);
    KNOB *findKnob(const QString &devicePath, const QPoint &pos, int page);
    void updatePage(USBDEVICE *dev, int page);

    DeviceModel *deviceModelObject;
    CommandModel *commandModelObject;
    CommandModel *knobCommandModelObject;

    usbDevMap *usbDevices;
    QVector<BUTTON> *buttonList;
    QVector<KNOB> *knobList;
    QVector<COMMAND> *commandList;
    QMutex *controllerMutex;

    BUTTON *currentButton;
    KNOB *currentKnob;
};

#endif // CONTROLLERCONTROLLER_H
