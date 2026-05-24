#include "ControllerController.h"

#include <QtTest/QtTest>

class ControllerControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void commandModelFiltersByCommandType();
    void deviceModelExposesControllerCapabilities();
    void controllerReturnsButtonPageData();
    void contextMenuExposesGraphicsButtonOptions();
};

void ControllerControllerTest::commandModelFiltersByCommandType()
{
    QVector<COMMAND> commands;
    commands.append(COMMAND(0, "None", commandAny, funcNone, 0));
    commands.append(COMMAND(1, "Button Only", commandButton, funcTransceiverStatus, 1));
    commands.append(COMMAND(2, "Knob Only", commandKnob, funcAfGain, 1));

    CommandModel buttonModel;
    buttonModel.setCommands(&commands, true);
    QCOMPARE(buttonModel.rowCount(), 2);
    QCOMPARE(buttonModel.data(buttonModel.index(0, 0), CommandModel::TextRole).toString(), QString("None"));
    QCOMPARE(buttonModel.data(buttonModel.index(1, 0), CommandModel::TextRole).toString(), QString("Button Only"));

    CommandModel knobModel;
    knobModel.setCommands(&commands, false);
    QCOMPARE(knobModel.rowCount(), 2);
    QCOMPARE(knobModel.data(knobModel.index(0, 0), CommandModel::TextRole).toString(), QString("None"));
    QCOMPARE(knobModel.data(knobModel.index(1, 0), CommandModel::TextRole).toString(), QString("Knob Only"));
}

void ControllerControllerTest::deviceModelExposesControllerCapabilities()
{
    USBDEVICE streamDeck(USBTYPE(StreamDeckMini, 0x0fd9, 0x0063, 0, 0, 6, 0, 0, 0, 1024, 80));
    streamDeck.product = "Stream Deck Mini";
    streamDeck.path = "streamdeck";
    streamDeck.connected = true;

    USBDEVICE shuttle(USBTYPE(shuttleXpress, 0x0b33, 0x0020, 0, 0, 15, 0, 2, 0, 5, 0));
    shuttle.product = "Shuttle Xpress";
    shuttle.path = "shuttle";
    shuttle.connected = true;

    DeviceModel model;
    model.addDevice(&streamDeck);
    model.addDevice(&shuttle);

    const QVariantMap streamDeckData = model.get(0);
    QCOMPARE(streamDeckData.value("deviceName").toString(), QString("Stream Deck Mini"));
    QCOMPARE(streamDeckData.value("showSensitivity").toBool(), false);
    QCOMPARE(streamDeckData.value("showBrightness").toBool(), true);
    QCOMPARE(streamDeckData.value("showSpeed").toBool(), false);
    QCOMPARE(streamDeckData.value("showOrientation").toBool(), false);
    QCOMPARE(streamDeckData.value("showColor").toBool(), true);
    QCOMPARE(streamDeckData.value("showTimeout").toBool(), false);

    const QVariantMap shuttleData = model.get(1);
    QCOMPARE(shuttleData.value("showSensitivity").toBool(), true);
    QCOMPARE(shuttleData.value("showBrightness").toBool(), false);
    QCOMPARE(shuttleData.value("showSpeed").toBool(), false);
    QCOMPARE(shuttleData.value("showOrientation").toBool(), false);
    QCOMPARE(shuttleData.value("showColor").toBool(), false);
    QCOMPARE(shuttleData.value("showTimeout").toBool(), false);
}

void ControllerControllerTest::controllerReturnsButtonPageData()
{
    QVector<COMMAND> commands;
    commands.append(COMMAND(0, "None", commandAny, funcNone, 0));
    commands.append(COMMAND(1, "PTT On", commandButton, funcTransceiverStatus, 1));
    commands.append(COMMAND(2, "PTT Off", commandButton, funcTransceiverStatus, 0));

    usbDevMap devices;
    devices.insert("streamdeck", USBDEVICE(USBTYPE(StreamDeckMini, 0x0fd9, 0x0063, 0, 0, 6, 0, 0, 0, 1024, 80)));
    USBDEVICE &device = devices["streamdeck"];
    device.path = "streamdeck";
    device.product = "Stream Deck Mini";
    device.detected = true;
    device.connected = true;
    device.currentPage = 1;

    QVector<BUTTON> buttons;
    BUTTON button(StreamDeckMini, 3, QRect(10, 20, 30, 40), Qt::white, &commands[1], &commands[2], true);
    button.parent = &device;
    button.path = device.path;
    button.page = 1;
    button.backgroundOn = QColor("#112233");
    button.backgroundOff = QColor("#445566");
    buttons.append(button);

    QVector<KNOB> knobs;
    QMutex mutex;
    ControllerController controller;
    controller.init(&devices, &buttons, &knobs, &commands, &mutex);

    const QVariantList pageButtons = controller.getButtonsForPage(device.path, 1);
    QCOMPARE(pageButtons.size(), 1);

    const QVariantMap buttonData = pageButtons.first().toMap();
    QCOMPARE(buttonData.value("buttonNum").toInt(), 3);
    QCOMPARE(buttonData.value("buttonOnText").toString(), QString("PTT On"));
    QCOMPARE(buttonData.value("buttonOffText").toString(), QString("PTT Off"));
    QCOMPARE(buttonData.value("buttonGraphics").toBool(), true);
    QCOMPARE(buttonData.value("buttonOnColor").value<QColor>(), QColor("#112233"));
    QCOMPARE(buttonData.value("buttonOffColor").value<QColor>(), QColor("#445566"));
}

void ControllerControllerTest::contextMenuExposesGraphicsButtonOptions()
{
    QVector<COMMAND> commands;
    commands.append(COMMAND(0, "None", commandAny, funcNone, 0));
    commands.append(COMMAND(1, "PTT On", commandButton, funcTransceiverStatus, 1));
    commands.append(COMMAND(2, "PTT Off", commandButton, funcTransceiverStatus, 0));

    usbDevMap devices;
    devices.insert("streamdeck", USBDEVICE(USBTYPE(StreamDeckMini, 0x0fd9, 0x0063, 0, 0, 6, 0, 0, 0, 1024, 80)));
    USBDEVICE &device = devices["streamdeck"];
    device.path = "streamdeck";
    device.product = "Stream Deck Mini";
    device.detected = true;
    device.connected = true;
    device.currentPage = 1;

    QVector<BUTTON> buttons;
    BUTTON button(StreamDeckMini, 1, QRect(0, 0, 50, 50), Qt::white, &commands[1], &commands[2], true);
    button.parent = &device;
    button.path = device.path;
    button.page = 1;
    button.toggle = true;
    button.backgroundOn = QColor("#010203");
    button.backgroundOff = QColor("#040506");
    buttons.append(button);

    QVector<KNOB> knobs;
    QMutex mutex;
    ControllerController controller;
    controller.init(&devices, &buttons, &knobs, &commands, &mutex);

    QSignalSpy spy(&controller, &ControllerController::showConfigDialog);
    controller.showContextMenu(device.path, QPoint(25, 25));

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(1).toBool(), true); // isButton
    QCOMPARE(args.at(2).toBool(), false); // isKnob
    QCOMPARE(args.at(3).toInt(), 1); // on command index
    QCOMPARE(args.at(4).toInt(), 2); // off command index
    QCOMPARE(args.at(6).toBool(), true); // toggle
    QCOMPARE(args.at(8).toBool(), false); // show LED
    QCOMPARE(args.at(9).toBool(), true); // show color
    QCOMPARE(args.at(10).toBool(), true); // show icon
    QCOMPARE(args.at(11).value<QColor>(), QColor("#010203"));
    QCOMPARE(args.at(12).value<QColor>(), QColor("#040506"));
}

int runControllerControllerTests(int argc, char **argv)
{
    ControllerControllerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_controllercontroller.moc"
