#include "ControllerController.h"

#include "logcategories.h"

#include <QFile>
#include <QSettings>

namespace
{
QString commandText(const COMMAND *command, const QString &fallback = QStringLiteral("None"))
{
    if (command && !command->text.isEmpty())
        return command->text;
    return fallback;
}

const COMMAND *commandOrDefault(const COMMAND *command, QVector<COMMAND> *commandData)
{
    if (command)
        return command;
    if (commandData && !commandData->isEmpty())
        return &commandData->first();
    return nullptr;
}

bool showSensitivityControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;

    switch (dev->type.model)
    {
    case StreamDeckPedal:
    case StreamDeckOriginal:
    case StreamDeckOriginalV2:
    case StreamDeckOriginalMK2:
    case StreamDeckMini:
    case StreamDeckMiniV2:
    case StreamDeckXL:
    case StreamDeckXLV2:
    case XKeysXK3:
        return false;
    default:
        return dev->type.knobs > 0;
    }
}

bool showBrightnessControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;

    switch (dev->type.model)
    {
    case shuttleXpress:
    case shuttlePro2:
    case RC28:
    case xBoxGamepad:
    case eCoderPlus:
    case StreamDeckPedal:
    case XKeysXK3:
        return false;
    default:
        return true;
    }
}

bool showSpeedControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;
    return dev->type.model == QuickKeys;
}

bool showOrientationControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;
    return dev->type.model == QuickKeys;
}

bool showColorControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;

    switch (dev->type.model)
    {
    case shuttleXpress:
    case shuttlePro2:
    case RC28:
    case xBoxGamepad:
    case eCoderPlus:
    case StreamDeckPedal:
    case XKeysXK3:
        return false;
    default:
        return true;
    }
}

bool showTimeoutControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;
    return dev->type.model == QuickKeys;
}
}

// ========== DeviceModel Implementation ==========

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return devices.count();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= devices.count())
        return QVariant();

    USBDEVICE *dev = devices.at(index.row());

    switch (role)
    {
    case DeviceNameRole:
        return dev->product;
    case DevicePathRole:
        return dev->path;
    case ConnectedRole:
        return dev->connected;
    case CurrentPageRole:
        return dev->currentPage;
    case TotalPagesRole:
        return dev->pages;
    case DisabledRole:
        return dev->disabled;
    case SensitivityRole:
        return dev->sensitivity;
    case BrightnessRole:
        return dev->brightness;
    case SpeedRole:
        return dev->speed;
    case OrientationRole:
        return dev->orientation;
    case TimeoutRole:
        return dev->timeout;
    case ShowSensitivityRole:
        return showSensitivityControl(dev);
    case ShowBrightnessRole:
        return showBrightnessControl(dev);
    case ShowSpeedRole:
        return showSpeedControl(dev);
    case ShowOrientationRole:
        return showOrientationControl(dev);
    case ShowColorRole:
        return showColorControl(dev);
    case ShowTimeoutRole:
        return showTimeoutControl(dev);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[DeviceNameRole] = "deviceName";
    roles[DevicePathRole] = "devicePath";
    roles[ConnectedRole] = "connected";
    roles[CurrentPageRole] = "currentPage";
    roles[TotalPagesRole] = "totalPages";
    roles[DisabledRole] = "disabled";
    roles[SensitivityRole] = "sensitivity";
    roles[BrightnessRole] = "brightness";
    roles[SpeedRole] = "speed";
    roles[OrientationRole] = "orientation";
    roles[TimeoutRole] = "timeout";
    roles[ShowSensitivityRole] = "showSensitivity";
    roles[ShowBrightnessRole] = "showBrightness";
    roles[ShowSpeedRole] = "showSpeed";
    roles[ShowOrientationRole] = "showOrientation";
    roles[ShowColorRole] = "showColor";
    roles[ShowTimeoutRole] = "showTimeout";
    return roles;
}

QVariantMap DeviceModel::get(int row) const
{
    QVariantMap item;
    if (row < 0 || row >= devices.count())
        return item;

    const QModelIndex idx = index(row, 0);
    const auto roles = roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        item[QString::fromUtf8(it.value())] = data(idx, it.key());

    return item;
}

void DeviceModel::addDevice(USBDEVICE *dev)
{
    if (pathToIndex.contains(dev->path))
        return; // Already exists

    int row = devices.count();
    beginInsertRows(QModelIndex(), row, row);
    devices.append(dev);
    pathToIndex[dev->path] = row;
    endInsertRows();
}

void DeviceModel::removeDevice(const QString &path)
{
    if (!pathToIndex.contains(path))
        return;

    int row = pathToIndex[path];
    beginRemoveRows(QModelIndex(), row, row);
    devices.removeAt(row);
    pathToIndex.remove(path);

    // Update indices for devices after removed one
    for (int i = row; i < devices.count(); ++i)
    {
        pathToIndex[devices[i]->path] = i;
    }
    endRemoveRows();
}

void DeviceModel::reset()
{
    beginResetModel();
    devices.clear();
    pathToIndex.clear();
    endResetModel();
}

void DeviceModel::updateDevice(const QString &path)
{
    if (!pathToIndex.contains(path))
        return;

    int row = pathToIndex[path];
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
}

USBDEVICE *DeviceModel::getDevice(const QString &path)
{
    if (!pathToIndex.contains(path))
        return nullptr;
    return devices[pathToIndex[path]];
}

USBDEVICE *DeviceModel::getDevice(int index)
{
    if (index < 0 || index >= devices.count())
        return nullptr;
    return devices[index];
}

// ========== CommandModel Implementation ==========

CommandModel::CommandModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CommandModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return commandItems.count();
}

QVariant CommandModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= commandItems.count())
        return QVariant();

    COMMAND *cmd = commandItems.at(index.row());

    switch (role)
    {
    case TextRole:
        return cmd->text;
    case IndexRole:
        return cmd->index;
    case IsSeparatorRole:
        return cmd->command == funcSeparator;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> CommandModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TextRole] = "text";
    roles[IndexRole] = "index";
    roles[IsSeparatorRole] = "isSeparator";
    return roles;
}

void CommandModel::setCommands(QVector<COMMAND> *commandList, bool buttonCommands)
{
    beginResetModel();
    commandItems.clear();

    for (COMMAND &c : *commandList)
    {
        if (buttonCommands)
        {
            if (c.cmdType == commandButton || c.cmdType == commandAny)
            {
                commandItems.append(&c);
            }
        }
        else
        {
            if (c.cmdType == commandKnob || c.cmdType == commandAny)
            {
                commandItems.append(&c);
            }
        }
    }

    endResetModel();
}

// ========== ControllerController Implementation ==========

ControllerController::ControllerController(QObject *parent)
    : QObject(parent)
    , deviceModelObject(new DeviceModel(this))
    , commandModelObject(new CommandModel(this))
    , knobCommandModelObject(new CommandModel(this))
    , usbDevices(nullptr)
    , buttonList(nullptr)
    , knobList(nullptr)
    , commandList(nullptr)
    , controllerMutex(nullptr)
    , currentButton(nullptr)
    , currentKnob(nullptr)
{
}

ControllerController::~ControllerController() { }

void ControllerController::init(usbDevMap *deviceMap,
                                QVector<BUTTON> *buttonData,
                                QVector<KNOB> *knobData,
                                QVector<COMMAND> *commandData,
                                QMutex *sharedMutex)
{
    usbDevices = deviceMap;
    buttonList = buttonData;
    knobList = knobData;
    commandList = commandData;
    controllerMutex = sharedMutex;

    deviceModelObject->reset();

    // Populate command models
    commandModelObject->setCommands(commandData, true); // Button commands
    knobCommandModelObject->setCommands(commandData, false); // Knob commands

    // Do not show saved/stale controllers until the USB worker has opened them.
    // initDevice() emits newDevice() after paths, commands, buttons, and knobs
    // are refreshed for the current HID device.
    for (auto it = deviceMap->begin(); it != deviceMap->end(); ++it)
    {
        if (it->detected && it->connected)
            deviceModelObject->addDevice(&it.value());
    }
}

void ControllerController::newDevice(USBDEVICE *dev)
{
    if (!dev)
        return;

    qInfo(logUsbControl()) << "Adding new device to controller:" << dev->product;
    if (deviceModelObject->getDevice(dev->path))
    {
        deviceModelObject->updateDevice(dev->path);
        emit deviceUpdated(dev->path);
        updatePage(dev, dev->currentPage);
        emit sendRequest(dev, usbFeatureType::featureSensitivity, dev->sensitivity);
        emit sendRequest(dev, usbFeatureType::featureBrightness, dev->brightness);
        emit sendRequest(dev, usbFeatureType::featureSpeed, dev->speed);
        emit sendRequest(dev, usbFeatureType::featureOrientation, dev->orientation);
        emit sendRequest(dev, usbFeatureType::featureTimeout, dev->timeout);
        return;
    }

    deviceModelObject->addDevice(dev);
    emit deviceAdded(deviceModelObject->rowCount() - 1);
    updatePage(dev, dev->currentPage);
    emit sendRequest(dev, usbFeatureType::featureSensitivity, dev->sensitivity);
    emit sendRequest(dev, usbFeatureType::featureBrightness, dev->brightness);
    emit sendRequest(dev, usbFeatureType::featureSpeed, dev->speed);
    emit sendRequest(dev, usbFeatureType::featureOrientation, dev->orientation);
    emit sendRequest(dev, usbFeatureType::featureTimeout, dev->timeout);
}

void ControllerController::removeDevice(USBDEVICE *dev)
{
    qInfo(logUsbControl()) << "Removing device from controller:" << dev->product;
    deviceModelObject->removeDevice(dev->path);
    emit deviceRemoved(0); // Index doesn't matter for removal notification
}

void ControllerController::setConnected(USBDEVICE *dev)
{
    deviceModelObject->updateDevice(dev->path);
    emit deviceUpdated(dev->path);
}

QString ControllerController::getControllerImagePath(const QString &devicePath)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return QString();

    // Return the image path based on device model
    switch (dev->type.model)
    {
    case shuttleXpress:
        return "qrc:/resources/shuttlexpress.png";
    case shuttlePro2:
        return "qrc:/resources/shuttlepro.png";
    case RC28:
        return "qrc:/resources/rc28.png";
    case xBoxGamepad:
        return "qrc:/resources/xbox.png";
    case eCoderPlus:
        return "qrc:/resources/ecoder.png";
    case QuickKeys:
        return "qrc:/resources/quickkeys.png";
    case StreamDeckOriginal:
    case StreamDeckOriginalV2:
    case StreamDeckOriginalMK2:
        return "qrc:/resources/streamdeck.png";
    case StreamDeckMini:
    case StreamDeckMiniV2:
        return "qrc:/resources/streamdeckmini.png";
    case StreamDeckXL:
    case StreamDeckXLV2:
        return "qrc:/resources/streamdeckxl.png";
    case StreamDeckPlus:
        return "qrc:/resources/streamdeckplus.png";
    case StreamDeckPedal:
        return "qrc:/resources/streamdeckpedal.png";
    case MiraBox293:
        return "qrc:/resources/mirabox293.png";
    case MiraBox293S:
        return "qrc:/resources/mirabox293s.png";
    case MiraBoxN3:
        return "qrc:/resources/miraboxn3.png";
    default:
        return QString();
    }
}

QVariantList ControllerController::getButtonsForPage(const QString &devicePath, int page)
{
    QVariantList result;
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev || !buttonList)
        return result;

    QMutexLocker locker(controllerMutex);

    for (const BUTTON &b : *buttonList)
    {
        if (b.parent == dev && b.page == page)
        {
            const COMMAND *onCommand = commandOrDefault(b.onCommand, commandList);
            const COMMAND *offCommand = commandOrDefault(b.offCommand, commandList);
            QVariantMap buttonData;
            buttonData["buttonX"] = b.pos.x();
            buttonData["buttonY"] = b.pos.y();
            buttonData["buttonWidth"] = b.pos.width();
            buttonData["buttonHeight"] = b.pos.height();
            buttonData["buttonText"] = (b.isOn && b.toggle) ? commandText(offCommand) : commandText(onCommand);
            buttonData["buttonOnText"] = commandText(onCommand);
            buttonData["buttonOffText"] = commandText(offCommand);
            buttonData["buttonNum"] = b.num;
            buttonData["buttonIsOn"] = b.isOn;
            buttonData["textColor"] = b.textColour;
            buttonData["buttonOnColor"] = b.backgroundOn;
            buttonData["buttonOffColor"] = b.backgroundOff;
            buttonData["buttonGraphics"] = b.graphics;
            result.append(buttonData);
        }
    }

    return result;
}

QVariantList ControllerController::getKnobsForPage(const QString &devicePath, int page)
{
    QVariantList result;
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev || !knobList)
        return result;

    QMutexLocker locker(controllerMutex);

    for (const KNOB &k : *knobList)
    {
        if (k.parent == dev && k.page == page)
        {
            const COMMAND *command = commandOrDefault(k.command, commandList);
            QVariantMap knobData;
            knobData["knobX"] = k.pos.x();
            knobData["knobY"] = k.pos.y();
            knobData["knobWidth"] = k.pos.width();
            knobData["knobHeight"] = k.pos.height();
            knobData["knobText"] = commandText(command);
            knobData["knobNum"] = k.num;
            knobData["textColor"] = k.textColour;
            result.append(knobData);
        }
    }

    return result;
}

BUTTON *ControllerController::findButton(const QString &devicePath, const QPoint &pos, int page)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev || !buttonList)
        return nullptr;

    auto it = std::find_if(buttonList->begin(),
                           buttonList->end(),
                           [&](BUTTON &b) { return b.parent == dev && b.pos.contains(pos) && b.page == page; });

    return (it != buttonList->end()) ? &(*it) : nullptr;
}

KNOB *ControllerController::findKnob(const QString &devicePath, const QPoint &pos, int page)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev || !knobList)
        return nullptr;

    auto it = std::find_if(knobList->begin(),
                           knobList->end(),
                           [&](KNOB &k) { return k.parent == dev && k.pos.contains(pos) && k.page == page; });

    return (it != knobList->end()) ? &(*it) : nullptr;
}

void ControllerController::showContextMenu(const QString &devicePath, const QPoint &pos)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    if (!controllerMutex)
        return;

    QMutexLocker locker(controllerMutex);

    // Try to find a button first
    BUTTON *button = findButton(devicePath, pos, dev->currentPage);
    if (button)
    {
        currentButton = button;
        currentKnob = nullptr;
        const COMMAND *onCommand = commandOrDefault(button->onCommand, commandList);
        const COMMAND *offCommand = commandOrDefault(button->offCommand, commandList);
        emit showConfigDialog(QString("Configure Button %1").arg(button->num),
                              true,
                              false,
                              onCommand ? onCommand->index : 0,
                              offCommand ? offCommand->index : 0,
                              0,
                              button->toggle,
                              button->led,
                              dev->type.leds > 0 && !button->graphics,
                              button->graphics,
                              button->graphics,
                              button->backgroundOn,
                              button->backgroundOff);
        return;
    }

    // Try to find a knob
    KNOB *knob = findKnob(devicePath, pos, dev->currentPage);
    if (knob)
    {
        currentKnob = knob;
        currentButton = nullptr;
        const COMMAND *command = commandOrDefault(knob->command, commandList);
        emit showConfigDialog(QString("Configure Knob %1").arg(knob->num),
                              false,
                              true,
                              0,
                              0,
                              command ? command->index : 0,
                              false,
                              0,
                              false,
                              false,
                              false,
                              QColor(),
                              QColor());
        return;
    }
}

void ControllerController::buttonPressed(const QString &devicePath, const QPoint &pos, bool pressed)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    const COMMAND *triggerCommand = nullptr;

    {
        QMutexLocker locker(controllerMutex);

        BUTTON *button = findButton(devicePath, pos, dev->currentPage);
        if (button)
        {
            const COMMAND *command = pressed ? commandOrDefault(button->onCommand, commandList)
                                             : commandOrDefault(button->offCommand, commandList);
            // Simulate button press/release on the physical controller display.
            emit sendRequest(dev,
                             usbFeatureType::featureButton,
                             button->num,
                             commandText(command),
                             button->icon,
                             pressed ? &button->backgroundOff : &button->backgroundOn);

            if (command && command->index > 0 && command->command != funcNone)
                triggerCommand = command;
        }
    }

    if (triggerCommand)
    {
        if (triggerCommand->command == funcPageUp)
            setPage(devicePath, dev->currentPage + 1);
        else if (triggerCommand->command == funcPageDown)
            setPage(devicePath, dev->currentPage - 1);
        else if (triggerCommand->command == funcLCDSpectrum)
            dev->lcd = funcLCDSpectrum;
        else if (triggerCommand->command == funcLCDWaterfall)
            dev->lcd = funcLCDWaterfall;
        else if (triggerCommand->command == funcLCDNothing)
            dev->lcd = funcLCDNothing;
        else
            emit commandTriggered(triggerCommand);
    }
}

void ControllerController::setPage(const QString &devicePath, int page)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    updatePage(dev, page);
    deviceModelObject->updateDevice(devicePath);
}

void ControllerController::updatePage(USBDEVICE *dev, int page)
{
    if (!dev || !controllerMutex || !buttonList || !knobList)
        return;

    {
        QMutexLocker locker(controllerMutex);

        if (page > dev->pages)
            page = 1;
        if (page < 1)
            page = dev->pages;

        dev->currentPage = page;

        // Update buttons for this page
        for (BUTTON &b : *buttonList)
        {
            if (b.parent == dev)
            {
                if (b.page == dev->currentPage)
                {
                    const bool useOffState = b.isOn && b.toggle;
                    const COMMAND *command = commandOrDefault(useOffState ? b.offCommand : b.onCommand, commandList);
                    emit sendRequest(dev,
                                     usbFeatureType::featureButton,
                                     b.num,
                                     commandText(command),
                                     b.icon,
                                     useOffState ? &b.backgroundOff : &b.backgroundOn);
                }
            }
        }

        // Update knobs for this page
        for (KNOB &k : *knobList)
        {
            if (k.parent == dev && k.page == dev->currentPage)
            {
                if (k.num >= 0 && k.num < dev->knobValues.size())
                {
                    dev->knobValues[k.num].value = k.value;
                    dev->knobValues[k.num].previous = k.value;
                }
            }
        }
    }

    emit deviceUpdated(dev->path);
}

void ControllerController::setTotalPages(const QString &devicePath, int pages)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    emit programPages(dev, pages);
    deviceModelObject->updateDevice(devicePath);
}

void ControllerController::setSensitivity(const QString &devicePath, int value)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    qInfo(logUsbControl()) << "Setting sensitivity" << value << "for device" << dev->product;
    dev->sensitivity = value;
    emit sendRequest(dev, usbFeatureType::featureSensitivity, value);
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setBrightness(const QString &devicePath, int index)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    dev->brightness = quint8(index);
    emit sendRequest(dev, usbFeatureType::featureBrightness, index);
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setSpeed(const QString &devicePath, int index)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    dev->speed = quint8(index);
    emit sendRequest(dev, usbFeatureType::featureSpeed, index);
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setOrientation(const QString &devicePath, int index)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    dev->orientation = quint8(index);
    emit sendRequest(dev, usbFeatureType::featureOrientation, index);
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setColor(const QString &devicePath, const QColor &color)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    dev->color = color;
    QColor *colorPtr = new QColor(color);
    emit sendRequest(dev, usbFeatureType::featureColor, 0, "", nullptr, colorPtr);
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setTimeout(const QString &devicePath, int minutes)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    dev->timeout = quint8(minutes);
    emit sendRequest(dev, usbFeatureType::featureTimeout, minutes);
    emit sendRequest(
        dev, usbFeatureType::featureOverlay, minutes, QString("Sleep timeout set to %1 minutes").arg(minutes));
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setDeviceDisabled(const QString &devicePath, bool disabled)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    dev->disabled = disabled;
    emit programDisable(dev, disabled);
    deviceModelObject->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::applyButtonConfig(int onCommand, int offCommand, int knobCommand, bool toggle, int ledNumber)
{
    if (!controllerMutex || !commandList)
        return;

    QString updatedPath;

    {
        QMutexLocker locker(controllerMutex);

        if (currentButton)
        {
            // Update button configuration
            auto onCmd = std::find_if(commandList->begin(),
                                      commandList->end(),
                                      [onCommand](const COMMAND &c) { return c.index == onCommand; });

            auto offCmd = std::find_if(commandList->begin(),
                                       commandList->end(),
                                       [offCommand](const COMMAND &c) { return c.index == offCommand; });

            if (onCmd != commandList->end())
            {
                currentButton->onCommand = &(*onCmd);
                currentButton->on = onCmd->text;
            }

            if (offCmd != commandList->end())
            {
                currentButton->offCommand = &(*offCmd);
                currentButton->off = offCmd->text;
            }

            currentButton->toggle = toggle;
            currentButton->led = ledNumber;

            // Update the display
            const COMMAND *command = commandOrDefault(currentButton->onCommand, commandList);
            emit sendRequest(currentButton->parent,
                             usbFeatureType::featureButton,
                             currentButton->num,
                             commandText(command),
                             currentButton->icon,
                             &currentButton->backgroundOn);

            updatedPath = currentButton->parent ? currentButton->parent->path : QString();
        }

        if (currentKnob)
        {
            // Update knob configuration
            auto cmd = std::find_if(commandList->begin(),
                                    commandList->end(),
                                    [knobCommand](const COMMAND &c) { return c.index == knobCommand; });

            if (cmd != commandList->end())
            {
                currentKnob->command = &(*cmd);
                currentKnob->cmd = cmd->text;
            }

            updatedPath = currentKnob->parent ? currentKnob->parent->path : QString();
        }
    }

    if (!updatedPath.isEmpty())
        emit deviceUpdated(updatedPath);
}

void ControllerController::setCurrentButtonColor(bool pressedColor, const QColor &color)
{
    if (!controllerMutex || !currentButton || !color.isValid())
        return;

    USBDEVICE *dev = nullptr;
    int buttonNum = 0;
    QString text;
    QImage *icon = nullptr;
    QColor *displayColor = nullptr;
    QString updatedPath;

    {
        QMutexLocker locker(controllerMutex);
        if (!currentButton)
            return;

        if (pressedColor)
            currentButton->backgroundOff = color;
        else
            currentButton->backgroundOn = color;

        dev = currentButton->parent;
        buttonNum = currentButton->num;
        icon = currentButton->icon;
        const bool useOffState = currentButton->isOn && currentButton->toggle;
        const COMMAND *command
            = commandOrDefault(useOffState ? currentButton->offCommand : currentButton->onCommand, commandList);
        text = commandText(command);
        displayColor = useOffState ? &currentButton->backgroundOff : &currentButton->backgroundOn;
        updatedPath = dev ? dev->path : QString();
    }

    if (dev)
        emit sendRequest(dev, usbFeatureType::featureButton, buttonNum, text, icon, displayColor);
    if (!updatedPath.isEmpty())
        emit deviceUpdated(updatedPath);
}

void ControllerController::backupSettings(const QString &devicePath, const QUrl &fileUrl)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    QString filePath = fileUrl.toLocalFile();
    emit backup(dev, filePath);
}

void ControllerController::restoreSettings(const QString &devicePath, const QUrl &fileUrl)
{
    USBDEVICE *dev = deviceModelObject->getDevice(devicePath);
    if (!dev)
        return;

    QString filePath = fileUrl.toLocalFile();

    // Validate the backup file
    QSettings settings(filePath, QSettings::IniFormat);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    settings.setIniCodec("UTF-8");
#endif
    QString version = settings.value("Version", "").toString();
    settings.beginGroup("Controller");
    QString model = settings.value("Model", "").toString();
    settings.endGroup();

    // In QML, you'd show a dialog here - for now just emit
    // You might want to add a signal for showing confirmation dialogs

    emit restore(dev, filePath);
}
