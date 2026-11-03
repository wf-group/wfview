import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: firstTimeSetupWindow
    title: qsTr("Welcome to wfview")
    color: palette.window
    width: 540
    height: 520
    minimumWidth: 480
    minimumHeight: 460

    modality: Qt.ApplicationModal
    flags: Qt.Dialog | Qt.FramelessWindowHint

    readonly property bool _sysTheme: MainController.settings.options["Interface.UseSystemTheme"] === true

    palette {
        window: _sysTheme ? undefined : MainController.settings.options["Color.Window"]
        windowText: _sysTheme ? undefined : MainController.settings.options["Color.WindowText"]
        base: _sysTheme ? undefined : MainController.settings.options["Color.Base"]
        alternateBase: _sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
        text: _sysTheme ? undefined : MainController.settings.options["Color.MainText"]
        button: _sysTheme ? undefined : MainController.settings.options["Color.Button"]
        buttonText: _sysTheme ? undefined : MainController.settings.options["Color.ButtonText"]
        brightText: _sysTheme ? undefined : MainController.settings.options["Color.BrightText"]
        highlight: _sysTheme ? undefined : MainController.settings.options["Color.Highlight"]
        highlightedText: _sysTheme ? undefined : MainController.settings.options["Color.HighlightedText"]
        mid: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
        dark: _sysTheme ? undefined : MainController.settings.options["Color.Dark"]
        light: _sysTheme ? undefined : MainController.settings.options["Color.Light"]
        placeholderText: _sysTheme ? undefined : MainController.settings.options["Color.PlaceholderText"]
    }

    // Exit: quit wfview, setup is not marked complete.
    // showSettings: finished the wizard, go to Settings -> Radio Access.
    // skipSetup: user will configure on their own.
    signal exitProgram()
    signal showSettings(bool isNetwork, int manufacturer, int ituRegion)
    signal skipSetup()

    // Steps: 0 = manufacturer, 1 = connection, 2 = ITU region, 3 = information needed
    readonly property int stepManufacturer: 0
    readonly property int stepConnection: 1
    readonly property int stepRegion: 2
    readonly property int stepInfo: 3
    readonly property int lastStep: stepInfo

    property int setupState: stepManufacturer
    property int manufacturer: 0   // manufacturersType_t: 0 Icom, 1 Kenwood, 2 Yaesu
    property bool isNetwork: true
    property int ituRegion: 1      // ITU region 1, 2 or 3

    readonly property var manufacturerNames: [qsTr("Icom"), qsTr("Kenwood"), qsTr("Yaesu")]

    readonly property var neededDetails: {
        if (!isNetwork) {
            var serial = [qsTr("Serial port name (for example COM3 or /dev/ttyUSB0)"),
                          qsTr("Baud rate, as set in the radio's menu")]
            if (manufacturer === 0)
                serial.push(qsTr("The radio's CI-V address, only if it is an older radio without CI-V Transceive"))
            return serial
        }
        switch (manufacturer) {
        case 1:
            return [qsTr("Radio IP address"),
                    qsTr("KNS administrator username and password"),
                    qsTr("Mic and speaker on THIS PC")]
        case 2:
            return [qsTr("Radio IP address"),
                    qsTr("CAT, audio and scope port numbers"),
                    qsTr("Mic and speaker on THIS PC")]
        default:
            return [qsTr("Radio IP address and UDP port numbers"),
                    qsTr("Radio network username and password"),
                    qsTr("Mic and speaker on THIS PC")]
        }
    }

    // Reset to the first step, preselect the current settings, and show.
    function begin() {
        const s = MainController.settings
        const m = s ? Number(s.option("Radio.Manufacturer")) : 0
        manufacturer = (m >= 0 && m <= 2) ? m : 0
        const r = s ? Number(s.option("Interface.Region")) : 1
        ituRegion = (r >= 1 && r <= 3) ? r : 1
        setupState = stepManufacturer

        const owner = transientParent
        const platform = String(MainController.platformName())
        if (owner && platform.indexOf("wayland") === -1) {
            x = owner.x + Math.round((owner.width - width) / 2)
            y = owner.y + Math.round((owner.height - height) / 2)
        }
        show()
        raise()
        requestActivate()
    }

    // Frameless window, so draw a border
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: palette.mid
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Label {
            Layout.fillWidth: true
            text: qsTr("Welcome to wfview!")
            font.pixelSize: 24
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("A few quick questions to get you started. You will then be taken to the Settings so you can enter your radio's connection details.")
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Step %1 of %2").arg(setupState + 1).arg(lastStep + 1)
            horizontalAlignment: Text.AlignHCenter
            opacity: 0.7
        }

        // Step 1: Manufacturer
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("What brand is your radio?")
            visible: setupState === stepManufacturer

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Repeater {
                    model: manufacturerNames
                    RadioButton {
                        text: modelData
                        Accessible.name: qsTr("%1 radio").arg(modelData)
                        checked: firstTimeSetupWindow.manufacturer === index
                        onToggled: if (checked) firstTimeSetupWindow.manufacturer = index
                    }
                }
            }
        }

        // Step 2: Connection type
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("How is your radio connected?")
            visible: setupState === stepConnection

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                ButtonGroup { id: connectionGroup }

                RadioButton {
                    text: qsTr("Serial Port on this PC")
                    ButtonGroup.group: connectionGroup
                    onToggled: if (checked) isNetwork = false
                }

                RadioButton {
                    text: qsTr("USB Port on This PC")
                    ButtonGroup.group: connectionGroup
                    onToggled: if (checked) isNetwork = false
                }

                RadioButton {
                    text: qsTr("Ethernet Network")
                    ButtonGroup.group: connectionGroup
                    onToggled: if (checked) isNetwork = true
                }

                RadioButton {
                    text: qsTr("WiFi Network")
                    ButtonGroup.group: connectionGroup
                    checked: true
                    onToggled: if (checked) isNetwork = true
                }
            }
        }

        // Step 3: ITU region
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Which ITU region are you in?")
            visible: setupState === stepRegion

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Repeater {
                    model: 3
                    RadioButton {
                        text: qsTr("Region %1").arg(index + 1)
                        Accessible.name: qsTr("ITU Region %1").arg(index + 1)
                        checked: firstTimeSetupWindow.ituRegion === index + 1
                        onToggled: if (checked) firstTimeSetupWindow.ituRegion = index + 1
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("Region 1 comprises Europe, Africa, the Commonwealth of Independent States, Mongolia, and the Middle East west of the Persian Gulf, including Iraq.") + "\n"
                          + qsTr("Region 2 covers the Americas including Greenland, and some of the eastern Pacific Islands.") + "\n"
                          + qsTr("Region 3 contains most of non-FSU Asia east of and including Iran, and most of Oceania.") + "\n"
                          + qsTr("(Text paraphrased from Wikipedia)")
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                }
            }
        }

        // Step 4: What you'll need
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("For your %1 radio, you will need the following information:")
                   .arg(manufacturerNames[manufacturer])
            visible: setupState === stepInfo

            ColumnLayout {
                anchors.fill: parent
                spacing: 12

                Repeater {
                    model: neededDetails
                    Label {
                        Layout.fillWidth: true
                        text: "• " + modelData
                        wrapMode: Text.WordWrap
                        font.pixelSize: 14
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: qsTr("Exit Program")
                Accessible.name: qsTr("Exit wfview")
                onClicked: {
                    firstTimeSetupWindow.close()
                    exitProgram()
                }
            }

            Button {
                text: qsTr("I'll Do This On My Own")
                Accessible.name: qsTr("Skip the welcome screen")
                onClicked: {
                    firstTimeSetupWindow.close()
                    skipSetup()
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Back")
                visible: setupState > stepManufacturer
                onClicked: setupState--
            }

            Button {
                text: setupState < lastStep ? qsTr("Next") : qsTr("Finish")
                Accessible.name: setupState < lastStep ? qsTr("Next Step")
                                                       : qsTr("Finish Welcome Screen and proceed to Settings")
                highlighted: true
                onClicked: {
                    if (setupState < lastStep) {
                        setupState++
                    } else {
                        firstTimeSetupWindow.close()
                        showSettings(isNetwork, manufacturer, ituRegion)
                    }
                }
            }
        }
    }
}
