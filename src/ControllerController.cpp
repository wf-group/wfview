#include "ControllerController.h"
#include "logcategories.h"
#include <QSettings>
#include <QFile>

namespace {
QString commandText(const COMMAND *command, const QString &fallback = QStringLiteral("None"))
{
    if (command && !command->text.isEmpty())
        return command->text;
    return fallback;
}

const COMMAND *commandOrDefault(const COMMAND *command, QVector<COMMAND> *commands)
{
    if (command)
        return command;
    if (commands && !commands->isEmpty())
        return &commands->first();
    return nullptr;
}

bool showSensitivityControl(const USBDEVICE *dev)
{
    if (!dev)
        return false;

    switch (dev->type.model) {
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

    switch (dev->type.model) {
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

    switch (dev->type.model) {
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
    return m_devices.count();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.count())
        return QVariant();

    USBDEVICE* dev = m_devices.at(index.row());
    
    switch (role) {
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
    if (row < 0 || row >= m_devices.count())
        return item;

    const QModelIndex idx = index(row, 0);
    const auto roles = roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        item[QString::fromUtf8(it.value())] = data(idx, it.key());

    return item;
}

void DeviceModel::addDevice(USBDEVICE* dev)
{
    if (m_pathToIndex.contains(dev->path))
        return; // Already exists

    int row = m_devices.count();
    beginInsertRows(QModelIndex(), row, row);
    m_devices.append(dev);
    m_pathToIndex[dev->path] = row;
    endInsertRows();
}

void DeviceModel::removeDevice(const QString& path)
{
    if (!m_pathToIndex.contains(path))
        return;

    int row = m_pathToIndex[path];
    beginRemoveRows(QModelIndex(), row, row);
    m_devices.removeAt(row);
    m_pathToIndex.remove(path);
    
    // Update indices for devices after removed one
    for (int i = row; i < m_devices.count(); ++i) {
        m_pathToIndex[m_devices[i]->path] = i;
    }
    endRemoveRows();
}

void DeviceModel::reset()
{
    beginResetModel();
    m_devices.clear();
    m_pathToIndex.clear();
    endResetModel();
}

void DeviceModel::updateDevice(const QString& path)
{
    if (!m_pathToIndex.contains(path))
        return;

    int row = m_pathToIndex[path];
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx);
}

USBDEVICE* DeviceModel::getDevice(const QString& path)
{
    if (!m_pathToIndex.contains(path))
        return nullptr;
    return m_devices[m_pathToIndex[path]];
}

USBDEVICE* DeviceModel::getDevice(int index)
{
    if (index < 0 || index >= m_devices.count())
        return nullptr;
    return m_devices[index];
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
    return m_commands.count();
}

QVariant CommandModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_commands.count())
        return QVariant();

    COMMAND* cmd = m_commands.at(index.row());
    
    switch (role) {
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

void CommandModel::setCommands(QVector<COMMAND>* commands, bool buttonCommands)
{
    beginResetModel();
    m_commands.clear();
    
    for (COMMAND& c : *commands) {
        if (buttonCommands) {
            if (c.cmdType == commandButton || c.cmdType == commandAny) {
                m_commands.append(&c);
            }
        } else {
            if (c.cmdType == commandKnob || c.cmdType == commandAny) {
                m_commands.append(&c);
            }
        }
    }
    
    endResetModel();
}

// ========== ControllerController Implementation ==========

ControllerController::ControllerController(QObject *parent)
    : QObject(parent)
    , m_deviceModel(new DeviceModel(this))
    , m_commandModel(new CommandModel(this))
    , m_knobCommandModel(new CommandModel(this))
    , m_devices(nullptr)
    , m_buttons(nullptr)
    , m_knobs(nullptr)
    , m_commands(nullptr)
    , m_mutex(nullptr)
    , m_currentButton(nullptr)
    , m_currentKnob(nullptr)
{
}

ControllerController::~ControllerController()
{
}

void ControllerController::init(usbDevMap* devices, 
                                QVector<BUTTON>* buttons, 
                                QVector<KNOB>* knobs,
                                QVector<COMMAND>* commands,
                                QMutex* mutex)
{
    m_devices = devices;
    m_buttons = buttons;
    m_knobs = knobs;
    m_commands = commands;
    m_mutex = mutex;

    m_deviceModel->reset();

    // Populate command models
    m_commandModel->setCommands(commands, true);  // Button commands
    m_knobCommandModel->setCommands(commands, false); // Knob commands

    // Do not show saved/stale controllers until the USB worker has opened them.
    // initDevice() emits newDevice() after paths, commands, buttons, and knobs
    // are refreshed for the current HID device.
    for (auto it = devices->begin(); it != devices->end(); ++it) {
        if (it->detected && it->connected)
            m_deviceModel->addDevice(&it.value());
    }
}

void ControllerController::newDevice(USBDEVICE* dev)
{
    if (!dev)
        return;

    qInfo(logUsbControl()) << "Adding new device to controller:" << dev->product;
    if (m_deviceModel->getDevice(dev->path)) {
        m_deviceModel->updateDevice(dev->path);
        emit deviceUpdated(dev->path);
        updatePage(dev, dev->currentPage);
        emit sendRequest(dev, usbFeatureType::featureSensitivity, dev->sensitivity);
        emit sendRequest(dev, usbFeatureType::featureBrightness, dev->brightness);
        emit sendRequest(dev, usbFeatureType::featureSpeed, dev->speed);
        emit sendRequest(dev, usbFeatureType::featureOrientation, dev->orientation);
        emit sendRequest(dev, usbFeatureType::featureTimeout, dev->timeout);
        return;
    }

    m_deviceModel->addDevice(dev);
    emit deviceAdded(m_deviceModel->rowCount() - 1);
    updatePage(dev, dev->currentPage);
    emit sendRequest(dev, usbFeatureType::featureSensitivity, dev->sensitivity);
    emit sendRequest(dev, usbFeatureType::featureBrightness, dev->brightness);
    emit sendRequest(dev, usbFeatureType::featureSpeed, dev->speed);
    emit sendRequest(dev, usbFeatureType::featureOrientation, dev->orientation);
    emit sendRequest(dev, usbFeatureType::featureTimeout, dev->timeout);
}

void ControllerController::removeDevice(USBDEVICE* dev)
{
    qInfo(logUsbControl()) << "Removing device from controller:" << dev->product;
    m_deviceModel->removeDevice(dev->path);
    emit deviceRemoved(0); // Index doesn't matter for removal notification
}

void ControllerController::setConnected(USBDEVICE* dev)
{
    m_deviceModel->updateDevice(dev->path);
    emit deviceUpdated(dev->path);
}

QString ControllerController::getControllerImagePath(const QString& devicePath)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return QString();
    
    // Return the image path based on device model
    switch (dev->type.model) {
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

QVariantList ControllerController::getButtonsForPage(const QString& devicePath, int page)
{
    QVariantList result;
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev || !m_buttons)
        return result;

    QMutexLocker locker(m_mutex);

    for (const BUTTON& b : *m_buttons) {
        if (b.parent == dev && b.page == page) {
            const COMMAND *onCommand = commandOrDefault(b.onCommand, m_commands);
            const COMMAND *offCommand = commandOrDefault(b.offCommand, m_commands);
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

QVariantList ControllerController::getKnobsForPage(const QString& devicePath, int page)
{
    QVariantList result;
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev || !m_knobs)
        return result;

    QMutexLocker locker(m_mutex);

    for (const KNOB& k : *m_knobs) {
        if (k.parent == dev && k.page == page) {
            const COMMAND *command = commandOrDefault(k.command, m_commands);
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

BUTTON* ControllerController::findButton(const QString& devicePath, const QPoint& pos, int page)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev || !m_buttons)
        return nullptr;

    auto it = std::find_if(m_buttons->begin(), m_buttons->end(), 
        [&](BUTTON& b) {
            return b.parent == dev && b.pos.contains(pos) && b.page == page;
        });

    return (it != m_buttons->end()) ? &(*it) : nullptr;
}

KNOB* ControllerController::findKnob(const QString& devicePath, const QPoint& pos, int page)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev || !m_knobs)
        return nullptr;

    auto it = std::find_if(m_knobs->begin(), m_knobs->end(), 
        [&](KNOB& k) {
            return k.parent == dev && k.pos.contains(pos) && k.page == page;
        });

    return (it != m_knobs->end()) ? &(*it) : nullptr;
}

void ControllerController::showContextMenu(const QString& devicePath, const QPoint& pos)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    if (!m_mutex)
        return;

    QMutexLocker locker(m_mutex);

    // Try to find a button first
    BUTTON* button = findButton(devicePath, pos, dev->currentPage);
    if (button) {
        m_currentButton = button;
        m_currentKnob = nullptr;
        const COMMAND *onCommand = commandOrDefault(button->onCommand, m_commands);
        const COMMAND *offCommand = commandOrDefault(button->offCommand, m_commands);
        emit showConfigDialog(QString("Configure Button %1").arg(button->num),
                              true, false,
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
    KNOB* knob = findKnob(devicePath, pos, dev->currentPage);
    if (knob) {
        m_currentKnob = knob;
        m_currentButton = nullptr;
        const COMMAND *command = commandOrDefault(knob->command, m_commands);
        emit showConfigDialog(QString("Configure Knob %1").arg(knob->num),
                              false, true,
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

void ControllerController::buttonPressed(const QString& devicePath, 
                                        const QPoint& pos, 
                                        bool pressed)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    const COMMAND *triggerCommand = nullptr;

    {
        QMutexLocker locker(m_mutex);

        BUTTON* button = findButton(devicePath, pos, dev->currentPage);
        if (button) {
            const COMMAND *command = pressed ? commandOrDefault(button->onCommand, m_commands)
                                             : commandOrDefault(button->offCommand, m_commands);
            // Simulate button press/release on the physical controller display.
            emit sendRequest(dev, usbFeatureType::featureButton,
                            button->num,
                            commandText(command),
                            button->icon,
                            pressed ? &button->backgroundOff : &button->backgroundOn);

            if (command && command->index > 0 && command->command != funcNone)
                triggerCommand = command;
        }
    }

    if (triggerCommand) {
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

void ControllerController::setPage(const QString& devicePath, int page)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    updatePage(dev, page);
    m_deviceModel->updateDevice(devicePath);
}

void ControllerController::updatePage(USBDEVICE* dev, int page)
{
    if (!dev || !m_mutex || !m_buttons || !m_knobs)
        return;

    {
        QMutexLocker locker(m_mutex);

        if (page > dev->pages)
            page = 1;
        if (page < 1)
            page = dev->pages;

        dev->currentPage = page;

        // Update buttons for this page
        for (BUTTON& b : *m_buttons) {
            if (b.parent == dev) {
                if (b.page == dev->currentPage) {
                    const bool useOffState = b.isOn && b.toggle;
                    const COMMAND *command = commandOrDefault(useOffState ? b.offCommand : b.onCommand, m_commands);
                    emit sendRequest(dev, usbFeatureType::featureButton,
                                   b.num,
                                   commandText(command),
                                   b.icon,
                                   useOffState ? &b.backgroundOff : &b.backgroundOn);
                }
            }
        }

        // Update knobs for this page
        for (KNOB& k : *m_knobs) {
            if (k.parent == dev && k.page == dev->currentPage) {
                if (k.num >= 0 && k.num < dev->knobValues.size()) {
                    dev->knobValues[k.num].value = k.value;
                    dev->knobValues[k.num].previous = k.value;
                }
            }
        }
    }

    emit deviceUpdated(dev->path);
}

void ControllerController::setTotalPages(const QString& devicePath, int pages)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    emit programPages(dev, pages);
    m_deviceModel->updateDevice(devicePath);
}

void ControllerController::setSensitivity(const QString& devicePath, int value)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    qInfo(logUsbControl()) << "Setting sensitivity" << value << "for device" << dev->product;
    dev->sensitivity = value;
    emit sendRequest(dev, usbFeatureType::featureSensitivity, value);
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setBrightness(const QString& devicePath, int index)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    dev->brightness = quint8(index);
    emit sendRequest(dev, usbFeatureType::featureBrightness, index);
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setSpeed(const QString& devicePath, int index)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    dev->speed = quint8(index);
    emit sendRequest(dev, usbFeatureType::featureSpeed, index);
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setOrientation(const QString& devicePath, int index)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    dev->orientation = quint8(index);
    emit sendRequest(dev, usbFeatureType::featureOrientation, index);
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setColor(const QString& devicePath, const QColor& color)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    dev->color = color;
    QColor* colorPtr = new QColor(color);
    emit sendRequest(dev, usbFeatureType::featureColor, 0, "", nullptr, colorPtr);
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setTimeout(const QString& devicePath, int minutes)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    dev->timeout = quint8(minutes);
    emit sendRequest(dev, usbFeatureType::featureTimeout, minutes);
    emit sendRequest(dev, usbFeatureType::featureOverlay, minutes,
                     QString("Sleep timeout set to %1 minutes").arg(minutes));
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::setDeviceDisabled(const QString& devicePath, bool disabled)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    dev->disabled = disabled;
    emit programDisable(dev, disabled);
    m_deviceModel->updateDevice(devicePath);
    emit deviceUpdated(devicePath);
}

void ControllerController::applyButtonConfig(int onCommand, 
                                            int offCommand, 
                                            int knobCommand,
                                            bool toggle, 
                                            int ledNumber)
{
    if (!m_mutex || !m_commands)
        return;

    QString updatedPath;

    {
        QMutexLocker locker(m_mutex);

        if (m_currentButton) {
            // Update button configuration
            auto onCmd = std::find_if(m_commands->begin(), m_commands->end(),
                [onCommand](const COMMAND& c) { return c.index == onCommand; });

            auto offCmd = std::find_if(m_commands->begin(), m_commands->end(),
                [offCommand](const COMMAND& c) { return c.index == offCommand; });

            if (onCmd != m_commands->end()) {
                m_currentButton->onCommand = &(*onCmd);
                m_currentButton->on = onCmd->text;
            }

            if (offCmd != m_commands->end()) {
                m_currentButton->offCommand = &(*offCmd);
                m_currentButton->off = offCmd->text;
            }

            m_currentButton->toggle = toggle;
            m_currentButton->led = ledNumber;

            // Update the display
            const COMMAND *command = commandOrDefault(m_currentButton->onCommand, m_commands);
            emit sendRequest(m_currentButton->parent, usbFeatureType::featureButton,
                            m_currentButton->num,
                            commandText(command),
                            m_currentButton->icon,
                            &m_currentButton->backgroundOn);

            updatedPath = m_currentButton->parent ? m_currentButton->parent->path : QString();
        }

        if (m_currentKnob) {
            // Update knob configuration
            auto cmd = std::find_if(m_commands->begin(), m_commands->end(),
                [knobCommand](const COMMAND& c) { return c.index == knobCommand; });

            if (cmd != m_commands->end()) {
                m_currentKnob->command = &(*cmd);
                m_currentKnob->cmd = cmd->text;
            }

            updatedPath = m_currentKnob->parent ? m_currentKnob->parent->path : QString();
        }
    }

    if (!updatedPath.isEmpty())
        emit deviceUpdated(updatedPath);
}

void ControllerController::setCurrentButtonColor(bool pressedColor, const QColor& color)
{
    if (!m_mutex || !m_currentButton || !color.isValid())
        return;

    USBDEVICE *dev = nullptr;
    int buttonNum = 0;
    QString text;
    QImage *icon = nullptr;
    QColor *displayColor = nullptr;
    QString updatedPath;

    {
        QMutexLocker locker(m_mutex);
        if (!m_currentButton)
            return;

        if (pressedColor)
            m_currentButton->backgroundOff = color;
        else
            m_currentButton->backgroundOn = color;

        dev = m_currentButton->parent;
        buttonNum = m_currentButton->num;
        icon = m_currentButton->icon;
        const bool useOffState = m_currentButton->isOn && m_currentButton->toggle;
        const COMMAND *command = commandOrDefault(useOffState ? m_currentButton->offCommand : m_currentButton->onCommand, m_commands);
        text = commandText(command);
        displayColor = useOffState ? &m_currentButton->backgroundOff : &m_currentButton->backgroundOn;
        updatedPath = dev ? dev->path : QString();
    }

    if (dev)
        emit sendRequest(dev, usbFeatureType::featureButton, buttonNum, text, icon, displayColor);
    if (!updatedPath.isEmpty())
        emit deviceUpdated(updatedPath);
}

void ControllerController::backupSettings(const QString& devicePath, const QUrl& fileUrl)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    QString filePath = fileUrl.toLocalFile();
    emit backup(dev, filePath);
}

void ControllerController::restoreSettings(const QString& devicePath, const QUrl& fileUrl)
{
    USBDEVICE* dev = m_deviceModel->getDevice(devicePath);
    if (!dev)
        return;

    QString filePath = fileUrl.toLocalFile();
    
    // Validate the backup file
    QSettings settings(filePath, QSettings::IniFormat);
    QString version = settings.value("Version", "").toString();
    settings.beginGroup("Controller");
    QString model = settings.value("Model", "").toString();
    settings.endGroup();

    // In QML, you'd show a dialog here - for now just emit
    // You might want to add a signal for showing confirmation dialogs
    
    emit restore(dev, filePath);
}
