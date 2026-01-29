// Settings.qml
// Conversion of your QWidget .ui to QML (Qt Quick Controls 2 + Layouts).
//
// Notes (tell-it-like-it-is):
// - This is a *UI structure* conversion. Your C++ wiring (models, signals, validators, persistence)
//   still needs to be connected.
// - QWidget-only custom controls (QLedLabel, tableWidget) don’t exist in QML, so I’ve provided
//   QML stand-ins: LedSwatch + a basic users TableView.
// - The huge color editor grid is included, but it’s best maintained as data-driven (Repeater)
//   later. For now it’s a faithful “static” layout.
//
// Requires: QtQuick 2.15+, QtQuick.Controls 2.15+, QtQuick.Layouts 1.15+
// If you use Qt6, change imports accordingly.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import Qt.labs.platform 1.1 as PLATFORM
import WFVIEW 1.0

Window {
    id: window
    title: "Settings"

    width: 1193
    height: 625

    required property var controller


    property int connStatus: Number(MainController.connStatus)

    // ---- Helpers ----

    function indexFromValue(cb, v) {
        if (!cb || v === undefined || v === null)
            return -1

        var sv = String(v)

        for (var i = 0; i < cb.count; ++i) {
            var it = (cb.model && cb.model.get) ? cb.model.get(i) : cb.model[i]
            if (!it)
                continue

            var iv = it.value
            if (iv === v || String(iv) === sv)
                return i
        }
        return -1
    }


    component ColorRow : RowLayout {
        spacing: 8
        Layout.fillWidth: true

        property string key: "Color.SpectrumLine"
        property string label: "Spectrum line"

        // Can be QColor-ish OR a string in some cases; we handle both.
        readonly property var cur: controller ? controller.options[key] : null

        function clamp01(x) { return Math.max(0, Math.min(1, x)) }

        function qmlColor(v) {
            // Accept QColor, QML color, or strings "#RRGGBB" / "#AARRGGBB"
            if (v === null || v === undefined) return Qt.rgba(0.266, 0.266, 0.266, 1) // "#444444"
            if (typeof v === "string") return parseHexToColor(v) ?? Qt.rgba(0.266, 0.266, 0.266, 1)
            // If it's already a color/QColor, just return it (QML can consume it directly)
            return v
        }

        function parseHexToColor(s) {
            if (!s) return null
            var t = ("" + s).trim()
            if (t.length === 0) return null
            if (t[0] !== "#") t = "#" + t

            // #RRGGBB
            if (t.length === 7) {
                var r = parseInt(t.slice(1,3), 16)
                var g = parseInt(t.slice(3,5), 16)
                var b = parseInt(t.slice(5,7), 16)
                if (isNaN(r) || isNaN(g) || isNaN(b)) return null
                return Qt.rgba(r/255, g/255, b/255, 1)
            }

            // #AARRGGBB
            if (t.length === 9) {
                var a = parseInt(t.slice(1,3), 16)
                var r2 = parseInt(t.slice(3,5), 16)
                var g2 = parseInt(t.slice(5,7), 16)
                var b2 = parseInt(t.slice(7,9), 16)
                if (isNaN(a) || isNaN(r2) || isNaN(g2) || isNaN(b2)) return null
                return Qt.rgba(r2/255, g2/255, b2/255, a/255)
            }

            return null
        }

        function byteHex(n) {
            var h = Math.round(n).toString(16).toUpperCase()
            return (h.length === 1) ? ("0" + h) : h
        }

        function colorToHexArgb(c) {
            // Works for QML colors (r,g,b,a exist). Also works for QColor-ish values in QML.
            if (!c) return ""
            var cc = qmlColor(c)
            // QML color has r/g/b/a in 0..1
            var a = byteHex(clamp01(cc.a) * 255)
            var r = byteHex(clamp01(cc.r) * 255)
            var g = byteHex(clamp01(cc.g) * 255)
            var b = byteHex(clamp01(cc.b) * 255)
            return "#" + a + r + g + b
        }

        // ---------- UI ----------
        Button {
            text: label
            Layout.preferredWidth: 150
            onClicked: {
                if (!controller) return
                dlg.color = qmlColor(cur)    // IMPORTANT: use dlg.color (common across dialogs)
                dlg.open()
            }
        }

        TextField {
            id: hexField
            Layout.preferredWidth: 110
            placeholderText: "#AARRGGBB"

            // Don’t bind text permanently. We update it when cur changes.
            onEditingFinished: {
                if (!controller) return
                var c = parseHexToColor(text)
                if (!c) {
                    text = colorToHexArgb(cur)   // revert
                    return
                }
                controller.setOption(key, c)
            }
        }

        Rectangle {
            // QLedLabel stand-in
            width: 10
            height: 18
            radius: 2
            border.width: 1
            color: qmlColor(cur)
        }

        // Keep textbox synced when controller changes the color (including preset switches),
        // but don’t overwrite while user is typing.
        onCurChanged: {
            if (!hexField.activeFocus)
                hexField.text = colorToHexArgb(cur)
        }

        Component.onCompleted: {
            hexField.text = colorToHexArgb(cur)
        }

        // --------- ColorDialog ---------
        // This version assumes your ColorDialog has a 'color' property.
        // (Qt.labs.platform ColorDialog does; many others do too.)
        PLATFORM.ColorDialog {
            id: dlg
            title: "Select " + label
            options: ColorDialog.ShowAlphaChannel

            // This is the key: use dlg.color on accept.
            onAccepted: {
                if (!controller) return
                controller.setOption(key, dlg.color)
            }
        }
    }


    component LedSwatch: Rectangle {
        // QLedLabel stand-in
        width: 10
        height: 18
        radius: 2
        border.width: 1
        color: "#444444"
    }

    component LabeledField: RowLayout {
        // Small helper for Label + TextField
        property string label: ""
        property alias text: field.text
        property alias placeholderText: field.placeholderText
        property int fieldWidth: 150
        spacing: 6
        Label { text: parent.label }
        TextField {
            id: field
            Layout.preferredWidth: parent.fieldWidth
        }
    }
    // ---- Main Layout ----
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // Left category list
            ListView {
                id: settingsList
                Layout.preferredWidth: 150
                Layout.minimumWidth: 150
                Layout.maximumWidth: 150
                Layout.fillHeight: true
                clip: true

                Accessible.name: "Setting Category"
                Accessible.description: "Select the settings category you wish to edit. Selection may also be performed with keyboard shortcuts: Shift F1 is Radio Access, Shift F2 is User Interface, Shift F3 is Radio Settings, Shift F4 is Radio Server, Shift F5 is External Control, Shift F6 is DX Cluster,  Shift F7 is Experimental"

                model: [
                    { title: "Radio Access" },
                    { title: "User Interface" },
                    { title: "Radio Settings" },
                    { title: "Radio Server" },
                    { title: "External Control" },
                    { title: "DX Cluster" },
                    { title: "Experimental" },
                    { title: "Audio Processing" }
                ]

                currentIndex: settingsStack.currentIndex

                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData.title
                    highlighted: ListView.isCurrentItem
                    onClicked: settingsStack.currentIndex = index
                }
            }

            // Stacked pages
            StackLayout {
                id: settingsStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0

                // =========================
                // Page 0: Radio Access
                // =========================
                Item {
                    id: radioAccess
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        // Top row: Connection / CIV+Model / Info
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            GroupBox {
                                id: groupConnection
                                title: "Radio Connection"
                                Layout.preferredWidth: 140

                                ColumnLayout {
                                    enabled: !connStatus
                                    spacing: 6

                                    ComboBox {
                                        id: manufacturerCombo
                                        Accessible.name: "Radio Manufacturer Select"
                                        textRole: "item"
                                        valueRole: "value"
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(manufacturerCombo,controller.options["Radio.Manufacturer"])
                                                      : -1

                                        onActivated: if (controller) controller.setOption("Radio.Manufacturer", currentValue)
                                        model: [
                                            { item: "Icom", value: 0 },
                                            { item: "Kenwood", value: 1 },
                                            { item: "Yaesu", value: 2 }
                                        ]
                                    }

                                    ButtonGroup { id: connGroup; exclusive: true ; }

                                    RadioButton {
                                        id: serialEnableBtn
                                        text: "Serial (USB)"
                                        ButtonGroup.group: connGroup
                                        checked: controller ? !Boolean(controller.options["LAN.EnableLAN"]) : true
                                        onClicked: if (controller) controller.setOption("LAN.EnableLAN", false)
                                    }

                                    RadioButton {
                                        id: lanEnableBtn
                                        text: "Network"
                                        ButtonGroup.group: connGroup
                                        checked: controller ? Boolean(controller.options["LAN.EnableLAN"]) : false
                                        onClicked: if (controller) controller.setOption("LAN.EnableLAN", true)
                                    }

                                }
                            }

                            GroupBox {
                                title: "CI-V and Model"
                                enabled: !connStatus
                                Layout.preferredWidth: 300
                                Layout.maximumWidth: 300

                                GridLayout {
                                    columns: 2
                                    columnSpacing: 8
                                    rowSpacing: 8

                                    CheckBox {
                                        id: rigCIVManualAddrChk
                                        text: "Manual Radio CI-V Address:"
                                        Accessible.name: "Manual CI-V address Checkbox"
                                        ToolTip.visible: hovered
                                        checked: false
                                        ToolTip.text: "If you are using an older (year 2010) radio, you may need to enable this option to manually specify the CI-V address. This option is also useful for radios that do not have CI-V Transceive enabled and thus will not answer our broadcast query for connected rigs on the CI-V bus.\n\nIf you have a modern radio with CI-V Transceive enabled, you should not need to check this box.\n\nYou will need to Save Settings and re-launch wfview for this to take effect."
                                    }

                                    TextField {
                                        id: rigCIVaddrHexLine
                                        enabled: rigCIVManualAddrChk.checked
                                        Layout.preferredWidth: 50
                                        Layout.maximumWidth: 50
                                        Accessible.name: "Manual CI-V Textbox"
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Enter the address in hexadecimal, no prefix. Examples: IC-706:58, IC-756:50, IC-7300:94, IC-7100:88, etc. After changing, press Save Settings and re-launch wfview."
                                        text: (controller && controller.options && controller.options["Radio.CIVAddr"] !== undefined)
                                              ? controller.options["Radio.CIVAddr"].toString(16).toUpperCase()
                                              : ""


                                        onEditingFinished: {
                                            const v = parseInt(text, 16)
                                            if (!isNaN(v))
                                                controller.setOption("Radio.CIVAddr", v)
                                        }
                                    }

                                    CheckBox {
                                        id: useCIVasRigIDChk
                                        enabled: rigCIVManualAddrChk.checked
                                        Layout.columnSpan: 2
                                        text: "Use CI-V address as Model ID too"
                                        Accessible.name: "Use CI-V address as Model ID checkbox"
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Only check for older radios! Forces wfview to trust the CI-V address is also the model number. Do not check unless you have an older radio."

                                        checked: controller ? Boolean(controller.options["Radio.CIVisRadioModel"]) : false
                                        onClicked: if (controller) controller.setOption("Radio.CIVisRadioModel", true)
                                    }
                                }
                            }

                            GroupBox {
                                title: "Information"
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: "Audio controls on this page are ONLY for network radios\n" +
                                          "Please use the \"Radio Server\" page to select server audio.\n" +
                                          "ONLY use Manual CI-V when Transceive mode is not supported\n\n"+
                                          "You MUST disconnect from the radio before making any changes.\n\n"+
                                          "Please use the Connect/Disconnect button below"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // Serial Connected Radios
                        GroupBox {
                            id: groupSerial
                            title: "Serial Connected Radios"
                            Layout.fillWidth: true
                            enabled: controller
                                     && connStatus === 0
                                     && !Boolean(controller.options["LAN.EnableLAN"])
                            RowLayout {
                                spacing: 8

                                Label { text: "Serial Device:" }
                                ComboBox {
                                    id: serialDeviceListCombo
                                    Layout.preferredWidth: 180
                                    Layout.maximumWidth: 180
                                    Accessible.name: "Serial (USB) Port Selection Combo"
                                    readonly property var spec: controller ? controller.uiSpecs["SerialPorts"] : null
                                    model: spec ? spec.model : []
                                    textRole: spec ? spec.textRole : "text"
                                    valueRole: spec ? spec.valueRole : "value"
                                    visible: spec ? (spec.visible ?? true) : false
                                    currentIndex: (controller && controller.options)
                                                  ? indexFromValue(serialDeviceListCombo,controller.options["Radio.SerialPortRadio"])
                                                  : -1
                                    onActivated: controller.setOption("Radio.SerialPortRadio", currentValue)
                                }

                                Label { text: "Baud Rate" }
                                ComboBox {
                                    id: baudRateCombo
                                    Layout.preferredWidth: 120
                                    Layout.maximumWidth: 120
                                    Accessible.name: "Serial Baud Rate Combo"
                                    textRole: "text"
                                    valueRole: "value"
                                    model: [
                                        { text: "115,200", value: 115200 },
                                        { text: "57,600", value: 57600 },
                                        { text: "38,400", value: 38400 },
                                        { text: "28,800", value: 38400 },
                                        { text: "19,200", value: 38400 },
                                        { text: "9,600", value: 38400 },
                                        { text: "4,800", value: 38400 },
                                        { text: "2,400", value: 38400 },
                                        { text: "1,200", value: 38400 },
                                        { text: "600", value: 38400 }
                                    ]
                                }

                                Label { id: pttTypeLabel; text: "PTT Type" }
                                ComboBox {
                                    id: pttTypeCombo
                                    Accessible.name: "PTT Type Combo"
                                    model: [
                                        { text: "CI-V", value: 0 },
                                        { text: "RTS", value: 1 },
                                        { text: "DTR", value: 2 }
                                    ]
                                    textRole: "text"
                                    valueRole: "value"

                                    currentIndex: (controller && controller.options)
                                                  ? indexFromValue(pttTypeCombo,controller.options["Radio.PTTType"])
                                                  : -1

                                    onActivated: controller.setOption("Radio.PTTType",currentValue)
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        // Network Connected Radios
                        GroupBox {
                            id: groupLan
                            enabled: controller
                                     && connStatus === 0
                                     && Boolean(controller.options["LAN.EnableLAN"])

                            title: "Network Connected Radios"
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                spacing: 8

                                // Row: host + ports + conn type
                                RowLayout {
                                    spacing: 8

                                    Label { text: "Hostname" }
                                    TextField {
                                        id: ipAddressTxt
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        Accessible.name: "Hostname Textbox"
                                        text: (controller && controller.options && controller.options["UDP.IPAddress"] !== undefined)
                                              ? controller.options["UDP.IPAddress"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["UDP.IPAddress"])
                                                controller.setOption("UDP.IPAddress", text)
                                        }
                                    }

                                    Label { id: controlPortLabel; text: "Control Port" }
                                    TextField {
                                        id: controlPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        Accessible.name: "Control Port Textbox"
                                        text: (controller && controller.options && controller.options["UDP.ControlLANPort"] !== undefined)
                                              ? String(controller.options["UDP.ControlLANPort"])
                                              : "50001"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.ControlLANPort", v)
                                        }
                                    }

                                    Label { id: catPortLabel; text: "CAT"; visible: manufacturerCombo.currentValue === 2 }
                                    TextField {
                                        id: catPortTxt
                                        visible: manufacturerCombo.currentValue === 2
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: (controller && controller.options && controller.options["UDP.SerialLANPort"] !== undefined)
                                              ? String(controller.options["UDP.SerialLANPort"])
                                              : "50002"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.SerialLANPort", v)
                                        }
                                    }

                                    Label { id: audioPortLabel; text: "Audio";  visible: manufacturerCombo.currentValue === 2 }
                                    TextField {
                                        visible: manufacturerCombo.currentValue === 2
                                        id: audioPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: (controller && controller.options && controller.options["UDP.AudioLANPort"] !== undefined)
                                              ? String(controller.options["UDP.AudioLANPort"])
                                              : "50003"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.AudioLANPort", v)
                                        }
                                    }

                                    Label { id: scopePortLabel; text: "Scope"; visible: manufacturerCombo.currentValue === 2}
                                    TextField {
                                        id: scopePortTxt
                                        visible: manufacturerCombo.currentValue === 2
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: (controller && controller.options && controller.options["UDP.ScopeLANPort"] !== undefined)
                                              ? String(controller.options["UDP.ScopeLANPort"])
                                              : "50004"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.ScopeLANPort", v)
                                        }
                                    }

                                    Label { text: "Connection Type"; visible: manufacturerCombo.currentValue === 1 }
                                    ComboBox {
                                        id: networkConnectionTypeCombo
                                        visible: manufacturerCombo.currentValue === 1
                                        Accessible.name: "Connection Type Combo"
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "LAN", value: 1 },
                                            { text: "WiFi", value: 2 },
                                            { text: "WAN", value: 3 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(networkConnectionTypeCombo,controller.options["UDP.ConnectionType"])
                                                      : -1

                                        onActivated: controller.setOption("UDP.ConnectionType",currentValue)
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: username/pass/admin
                                RowLayout {
                                    spacing: 8

                                    Label { text: "Username" }
                                    TextField {
                                        id: usernameTxt
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        Accessible.name: "Username Textbox"
                                        text: (controller && controller.options && controller.options["UDP.Username"] !== undefined)
                                              ? controller.options["UDP.Username"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["UDP.Username"])
                                                controller.setOption("UDP.Username", text)
                                        }

                                    }

                                    Label { text: "Password" }
                                    TextField {
                                        id: passwordTxt
                                        echoMode: TextInput.Password
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        Accessible.name: "Password Textbox"
                                        text: (controller && controller.options && controller.options["UDP.Password"] !== undefined)
                                              ? controller.options["UDP.Password"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["UDP.Password"])
                                                controller.setOption("UDP.Password", text)
                                        }

                                    }

                                    CheckBox {
                                        id: adminLoginChk
                                        visible: manufacturerCombo.currentValue === 1;
                                        text: "Admin Login"
                                        Accessible.name: "Admin Login Checkbox"
                                        checked: controller ? Boolean(controller.options["UDP.AdminLogin"]) : false
                                        onClicked: if (controller) controller.setOption("UDP.AdminLogin", checked)

                                        Accessible.description: "Check this box if you are using the admin login (Kenwood radios only)"
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: codecs
                                RowLayout {
                                    spacing: 8

                                    Label { text: "RX Codec" }
                                    ComboBox {
                                        id: audioRXCodecCombo
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "LPCM 1ch 16bit", value: 4 },
                                            { text: "PCM 1ch 8bit", value: 2 },
                                            { text: "uLaw 1ch 8bit", value: 1 },
                                            { text: "LPCM 2ch 16bit", value: 16 },
                                            { text: "uLaw 2ch 8bit", value: 32 },
                                            { text: "PCM 2ch 8bit", value: 8 },
                                            { text: "Opus 1ch", value: 64 },
                                            { text: "Opus 2ch", value: 65 },
                                            { text: "ADPCM 1ch", value: 128 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioRXCodecCombo,controller.options["UDP.RxCodec"])
                                                      : -1
                                        onActivated: controller.setOption("UDP.RxCodec",currentValue)
                                        Accessible.name: "Receive Audio Codec Selector"
                                    }

                                    Label { text: "TX Codec" }
                                    ComboBox {
                                        id: audioTXCodecCombo
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "LPCM 1ch 16bit", value: 4 },
                                            { text: "PCM 1ch 8bit", value: 2 },
                                            { text: "uLaw 1ch 8bit", value: 1 },
                                            { text: "Opus 1ch", value: 64 },
                                            { text: "ADPCM 1ch", value: 128 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioTXCodecCombo,controller.options["UDP.TxCodec"])
                                                      : -1

                                        onActivated: controller.setOption("UDP.TxCodec",currentValue)
                                        Accessible.name: "Transmit Audio Codec Selector"
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: samplerate / duplex / audio system
                                RowLayout {
                                    spacing: 8
                                    Label { text: "Sample Rate" }
                                    ComboBox {
                                        id: audioSampleRateCombo
                                        Accessible.name: "Audio Sample Rate Selector"
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioSampleRateCombo,controller.options["UDP.SampleRate"])
                                                      : -1

                                        onActivated: controller.setOption("UDP.SampleRate",currentValue)
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "48 kHz", value: 48000 },
                                            { text: "24 kHz", value: 24000 },
                                            { text: "16 kHz", value: 16000 },
                                            { text: "8 kHz", value: 8000 }
                                        ]
                                    }

                                    Label { text: "Duplex" }
                                    ComboBox {
                                        id: audioDuplexCombo
                                        model: [
                                            { text: "Full Duplex", value: 0 },
                                            { text: "Half Duplex", value: 1 }
                                        ]
                                        Accessible.name: "Full or Half Duplex Combo"
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioDuplexCombo,controller.options["UDP.HalfDuplex"])
                                                      : -1
                                        onActivated: controller.setOption("UDP.HalfDuplex",currentValue)
                                    }

                                    Label { text: "Audio System" }
                                    ComboBox {
                                        id: audioSystemCombo
                                        Accessible.name: "Audio System Selection Combo"
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "Qt Audio", value: 0 },
                                            { text: "PortAudio", value: 1 },
                                            { text: "RT Audio", value: 2 },
                                            { text: "TCI Audio", value: 3 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioSystemCombo,controller.options["Radio.AudioSystem"])
                                                      : -1
                                        onActivated: controller.setOption("Radio.AudioSystem",currentValue)
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: audio output/input
                                RowLayout {
                                    spacing: 8
                                    Label { text: "Audio Output" }
                                    ComboBox {
                                        id: audioOutputCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioOutputs"] : null
                                        model: spec ? spec.model : []
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        visible: spec ? (spec.visible ?? true) : false

                                        currentIndex: controller ? indexFromValue(audioOutputCombo,controller.options["UDP.RxAudio"]) : -1
                                        onActivated: controller.setOption("UDP.RxAudio", currentValue)

                                        // --- Autosize to widest item ---
                                        readonly property real widestText: {
                                            let max = 0
                                            for (let i = 0; i < count; ++i) {
                                                max = Math.max(max, ipmetrics.advanceWidth(textAt(i)))
                                            }
                                            // also include current text in case model is empty/late
                                            max = Math.max(max, ipmetrics.advanceWidth(displayText))
                                            return max
                                        }

                                        implicitWidth: widestText + leftPadding + rightPadding + indicator.width + 12
                                        Layout.preferredWidth: implicitWidth
                                        Layout.maximumWidth: implicitWidth

                                        FontMetrics { id: ipmetrics; font: audioOutputCombo.font }

                                        Accessible.name: "Audio Output Selector"
                                    }


                                    Label { text: "Audio Input" }
                                    ComboBox {
                                        id: audioInputCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioInputs"] : null
                                        model: spec ? spec.model : []
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        visible: spec ? (spec.visible ?? true) : false
                                        currentIndex: controller ? indexFromValue(audioInputCombo,controller.options["UDP.TxAudio"]) : -1
                                        onActivated: controller.setOption("UDP.TxAudio", currentValue)
                                        // --- Autosize to widest item ---
                                        readonly property real widestText: {
                                            let max = 0
                                            for (let i = 0; i < count; ++i) {
                                                max = Math.max(max, opmetrics.advanceWidth(textAt(i)))
                                            }
                                            // also include current text in case model is empty/late
                                            max = Math.max(max, opmetrics.advanceWidth(displayText))
                                            return max
                                        }

                                        implicitWidth: widestText + leftPadding + rightPadding + indicator.width + 12
                                        Layout.preferredWidth: implicitWidth
                                        Layout.maximumWidth: implicitWidth

                                        FontMetrics { id: opmetrics; font: audioOutputCombo.font }

                                        // Autosize
                                        Accessible.name: "Audio Input Selector"
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                        GroupBox {
                            id: groupLatency
                            enabled: controller
                                     && Boolean(controller.options["LAN.EnableLAN"])

                            title: "Network latency settings"
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                spacing: 8
                                // Row: latency sliders + codecs
                                RowLayout {
                                    spacing: 8

                                    Label { text: "RX Latency (ms)" }
                                    Slider {
                                        id: rxLatencySlider
                                        from: 30
                                        to: 500
                                        stepSize: 1
                                        Accessible.name: "RX Latency Buffer Size"
                                        Layout.preferredWidth: 220
                                        value: controller ? controller.options["UDP.RxLatency"] : 0
                                        onMoved:controller.setOption("UDP.RxLatency", value)
                                    }
                                    Label { id: rxLatencyValue; text: rxLatencySlider.value }

                                    Label { text: "TX Latency (ms)" }
                                    Slider {
                                        id: txLatencySlider
                                        from: 30
                                        to: 500
                                        stepSize: 1
                                        Accessible.name: "TX Latency Buffer Size"
                                        Layout.preferredWidth: 220
                                        value: controller ? controller.options["UDP.TxLatency"] : 0
                                        onMoved:controller.setOption("UDP.TxLatency", value)
                                    }
                                    Label { id: txLatencyValue; text: txLatencySlider.value }

                                }
                            }


                        }
                    }
                }

                // =========================
                // Page 1: User Interface
                // =========================
                Item {
                    id: userInterface
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 12

                            CheckBox {
                                id: tuningFloorZerosChk
                                checked: controller ? Boolean(controller.options["Controls.NiceTS"]) : false
                                onClicked: if (controller) controller.setOption("Controls.NiceTS", checked)

                                text: "When tuning, set lower digits to zero"
                            }
                            CheckBox {
                                id: autoSSBchk
                                text: "Auto SSB"
                                checked: controller ? Boolean(controller.options["Controls.AutomaticSidebandSwitching"]) : false
                                onClicked: if (controller) controller.setOption("Controls.AutomaticSidebandSwitching", checked)
                                Accessible.name: "Auto SSB Switching"
                                Accessible.description: "When using SSB, automatically switch to the standard sideband for a given band."
                                ToolTip.visible: hovered
                                ToolTip.text: "When using SSB, automatically switch to the standard sideband for a given band."
                            }
                            CheckBox {
                                id: pttEnableChk;
                                text: "Enable PTT Controls"
                                checked: controller ? Boolean(controller.options["Controls.EnablePTT"]) : false
                                onClicked: if (controller) controller.setOption("Controls.EnablePTT", checked)
                            }
                            CheckBox {
                                id: rigCreatorChk
                                text: "Enable Rig Creator Feature (use with care)"
                                ToolTip.visible: hovered
                                ToolTip.text: "Rig creator allows changing of all rig features and adding new rig profiles"
                                checked: controller ? Boolean(controller.options["Interface.RigCreatorEnable"]) : false
                                onClicked: if (controller) controller.setOption("Interface.RigCreatorEnable", checked)
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                text: "Region:"
                                ToolTip.visible: hovered
                                ToolTip.text: "ITU Region. Used to display band limits. (See the QWidget tooltip text in your .ui.)"
                            }
                            TextField {
                                id: regionTxt
                                Layout.preferredWidth: 35
                                Layout.maximumWidth: 35
                                inputMethodHints: Qt.ImhDigitsOnly
                                placeholderText: "1"
                                text: (controller && controller.options && controller.options["Interface.Region"] !== undefined)
                                      ? controller.options["Interface.Region"]
                                      : ""

                                onEditingFinished: {
                                    if (text !== controller.options["Interface.Region"])
                                        controller.setOption("Interface.Region", text)
                                }
                            }
                        }

                        RowLayout {
                            spacing: 12
                            CheckBox {
                                id: wfInterpolateChk
                                text: "Interpolate Waterfall"
                                checked: controller ? Boolean(controller.options["Interface.WFInterpolate"]) : false
                                onClicked: if (controller) controller.setOption("Interface.WFInterpolate", checked)
                                ToolTip.visible: hovered
                                ToolTip.text: "Enables interpolation between pixels. Note that this will increase CPU usage."
                            }
                            CheckBox {
                                id: wfAntiAliasChk;
                                checked: controller ? Boolean(controller.options["Interface.WFAntiAlias"]) : false
                                onClicked: if (controller) controller.setOption("Interface.WFAntiAlias", checked)
                                text: "Anti-Alias Waterfall"
                            }
                            CheckBox {
                                id: clickDragTuningEnableChk;
                                checked: controller ? Boolean(controller.options["Interface.ClickDragTuningEnable"]) : false
                                onClicked: if (controller) controller.setOption("Interface.ClickDragTuningEnable", checked)
                                text: "Allow tuning via click and drag (experimental)"
                            }
                            CheckBox {
                                id: useSystemThemeChk;
                                checked: controller ? Boolean(controller.options["Interface.UseSystemTheme"]) : false
                                onClicked: if (controller) controller.setOption("Interface.UseSystemTheme", checked)
                                text: "Use System Theme"
                            }
                            CheckBox {
                                id: fullScreenChk;
                                checked: controller ? Boolean(controller.options["Interface.UseFullScreen"]) : false
                                onClicked: if (controller) controller.setOption("Interface.UseFullScreen", checked)
                                text: "Show full screen (F11)"
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Frequency display separators
                        RowLayout {
                            spacing: 8
                            Label { text: "Frequency Display:" }
                            Label { text: "Units" }
                            ComboBox {
                                id: frequencyUnitsCombo
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    { text: "None", value: 0 },
                                    { text: "Hz", value: 1 },
                                    { text: "kHz", value: 2 },
                                    { text: "MHz", value: 3 },
                                    { text: "GHz", value: 4 }
                                ]
                                currentIndex: (controller && controller.options)
                                              ? indexFromValue(frequencyUnitsCombo,controller.options["Interface.FrequencyUnits"])
                                              : -1
                                onActivated: controller.setOption("Interface.FrequencyUnits",currentValue)
                            }
                            Label { text: "Separators:" }
                            Label { text: "Decimal" }
                            ComboBox {
                                id: decimalSeparatorsCombo;
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    { text: "\",\"", value: "," },
                                    { text: "\".\"", value: "." },
                                    { text: "\":\"", value: ":" },
                                    { text: "\" \"", value: " " }
                                ]
                                currentIndex: (controller && controller.options)
                                              ? indexFromValue(decimalSeparatorsCombo,controller.options["Interface.DecimalSeparator"])
                                              : -1

                                onActivated: controller.setOption("Interface.DecimalSeparator",currentValue)
                            }
                            Label { text: "Groups" }
                            ComboBox {
                                id: groupSeparatorsCombo;
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    { text: "\",\"", value: "," },
                                    { text: "\".\"", value: "." },
                                    { text: "\":\"", value: ":" },
                                    { text: "\" \"", value: " " }
                                ]
                                currentIndex: (controller && controller.options)
                                              ? indexFromValue(groupSeparatorsCombo,controller.options["Interface.GroupSeparator"])
                                              : -1

                                onActivated: controller.setOption("Interface.GroupSeparator",currentValue)
                            }
                            CheckBox {
                                id: forceVfoModeChk;
                                text: "Force VFO Mode";
                                checked: true
                            }
                            CheckBox {
                                id: autoPowerOnChk;
                                text: "Auto Power-on radio";
                                checked: true
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Underlay controls
                        RowLayout {
                            spacing: 8
                            Label { id: underlayLabel; text: "Underlay Mode" }
                            ButtonGroup { id: underlayGroup }

                            RadioButton { id: underlayNone; text: "None"; checked: true; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: "No underlay graphics" }
                            RadioButton { id: underlayPeakHold; text: "Peak Hold"; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: "Indefinite peak hold" }
                            RadioButton { id: underlayPeakBuffer; text: "Peak"; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: "Peak value within the buffer" }
                            RadioButton { id: underlayAverageBuffer; text: "Average"; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: "Average value within the buffer" }

                            Label { text: "Underlay Buffer Size:" }
                            Slider {
                                id: underlayBufferSlider
                                from: 8
                                to: 128
                                value: 64
                                stepSize: 1
                                Layout.preferredWidth: 100
                                ToolTip.visible: hovered
                                ToolTip.text: "Size of buffer for spectrum data. Shorter values are more responsive."
                            }

                            CheckBox { id: showBandsChk; text: "Show Bands" }
                            Item { Layout.fillWidth: true }
                        }

                        // Additional meter selection + polling
                        RowLayout {
                            spacing: 8

                            Item { width: 20 } // fixed spacer
                            Label { id: secondaryMeterSelectionLabel; text: "Additional Meter Selection:" }
                            ComboBox {
                                id: meter2selectionCombo;
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    { text: "None", value: 0 },
                                    { text: "SWR", value: 3 },
                                    { text: "ALC", value: 5 },
                                    { text: "Compression", value: 6 },
                                    { text: "Voltage", value: 7 },
                                    { text: "Current", value: 8 },
                                    { text: "Center", value: 2 },
                                    { text: "Tx/Rx Audio", value: 12 },
                                    { text: "Tx Audio", value: 10 },
                                    { text: "Rx Audio", value: 11 }
                                ]
                            }

                            ComboBox {
                                id: meter3selectionCombo;
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    { text: "None", value: 0 },
                                    { text: "SWR", value: 3 },
                                    { text: "ALC", value: 5 },
                                    { text: "Compression", value: 6 },
                                    { text: "Voltage", value: 7 },
                                    { text: "Current", value: 8 },
                                    { text: "Center", value: 2 },
                                    { text: "Tx/Rx Audio", value: 12 },
                                    { text: "Tx Audio", value: 10 },
                                    { text: "Rx Audio", value: 11 }
                                ]
                            }
                            CheckBox { id: revCompMeterBtn; text: "Reverse Comp Meter"; ToolTip.visible: hovered; ToolTip.text: "Broadcast-style reduction meter" }

                            ButtonGroup { id: pollingButtonGroup }
                            RadioButton {
                                id: autoPollBtn
                                text: "AutoPolling"
                                checked: true
                                ButtonGroup.group: pollingButtonGroup
                                ToolTip.visible: hovered
                                ToolTip.text: "wfview will automatically calculate command polling. Recommended."
                            }
                            RadioButton {
                                id: manualPollBtn
                                text: "Manual Polling Interval:"
                                ButtonGroup.group: pollingButtonGroup
                                ToolTip.visible: hovered
                                ToolTip.text: "Manual (user-defined) command polling"
                            }
                            SpinBox {
                                id: pollTimeMsSpin
                                from: 1
                                to: 250
                                value: 25
                                editable: true
                                ToolTip.visible: hovered
                                ToolTip.text: "Sets the polling interval, in ms. Automatic polling is recommended. Serial/USB radios should not poll quicker than about 75ms."
                            }
                            Label { text: "ms" }

                            Item { Layout.fillWidth: true }
                        }

                        // Color scheme / preset controls
                        RowLayout {
                            spacing: 8
                            Label { text: "Color scheme" }
                            Label { id: colorPresetLabel; text: "Preset:" }
                            ComboBox {
                                id: colorPresetCombo
                                Layout.preferredWidth: 90
                                Layout.maximumWidth: 90

                                // show 1..N, but store 0..N-1
                                model: [
                                    { text: "1", value: 0 },
                                    { text: "2", value: 1 },
                                    { text: "3", value: 2 },
                                    { text: "4", value: 3 },
                                    { text: "5", value: 4 }
                                ]
                                textRole: "text"
                                valueRole: "value"

                                // display current selection
                                currentIndex: (controller && controller.options)
                                              ? indexFromValue(colorPresetCombo,controller.options["Interface.CurrentColorPresetNumber"])
                                              : -1

                                // commit user change
                                onActivated: if (controller) controller.setOption("Interface.CurrentColorPresetNumber", model[currentIndex].value)

                                ToolTip.visible: hovered
                                ToolTip.text: "Select a color preset here."
                            }

                            Button { id: colorRevertPresetBtn; text: "Revert"; ToolTip.visible: hovered; ToolTip.text: "Revert the selected color preset to the default." }
                            Button { id: colorRenamePresetBtn; text: "Rename Preset"; ToolTip.visible: hovered; ToolTip.text: "Rename the selected color preset. Max length is 10 characters." }
                            Item { Layout.fillWidth: true }
                        }

                        // Color editor (scrollable)

                        GroupBox {
                            id: colorEditorGroupBox
                            title: "User-defined Color Editor"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 250

                            ScrollView {
                                id: colorEditorScrollArea
                                anchors.fill: parent
                                clip: true

                                GridLayout {
                                    id: gridLayoutColors
                                    columns: 3
                                    columnSpacing: 40
                                    rowSpacing: 6

                                    // ---- Row 0: Grid, Tuning line, Meter Scale ----
                                    ColorRow { key: "Color.Foreground"; label: "Foreground" }
                                    ColorRow { key: "Color.Background"; label: "Background" }
                                    Item {}

                                    ColorRow { key: "Color.Grid"; label: "Grid" }
                                    ColorRow { key: "Color.TuningLine"; label: "Tuning Line" }
                                    ColorRow { key: "Color.MeterScale"; label: "Meter Scale" }

                                    ColorRow { key: "Color.Axis"; label: "Axis" }
                                    ColorRow { key: "Color.Passband"; label: "Passband" }
                                    ColorRow { key: "Color.MeterText"; label: "Meter Text" }

                                    ColorRow { key: "Color.Text"; label: "Text" }
                                    ColorRow { key: "Color.PbtIndicator"; label: "PBT Indicator" }
                                    ColorRow { key: "Color.WaterfallBack"; label: "Waterfall Back" }

                                    ColorRow { key: "Color.PlotBackround"; label: "Plot Background" }
                                    ColorRow { key: "Color.MeterLevel"; label: "Meter Level" }
                                    ColorRow { key: "Color.WaterfallGrid"; label: "Waterfall Grid" }

                                    ColorRow { key: "Color.SpectrumLine"; label: "Spectrum Line" }
                                    ColorRow { key: "Color.MeterAverage"; label: "Meter Average" }
                                    ColorRow { key: "Color.WaterfallAxis"; label: "Waterfall Axis" }

                                    ColorRow { key: "Color.SpectrumFill"; label: "Spectrum Fill" }
                                    ColorRow { key: "Color.MeterPeakLevel"; label: "Meter Peak Level" }
                                    ColorRow { key: "Color.WaterfallText"; label: "Waterfall Text" }

                                    CheckBox {
                                        id: useSpectrumFillGradientChk;
                                        text: "Spectrum Gradient"
                                        checked: controller ? Boolean(controller.options["Color.UseSpectrumFillGradient"]) : false
                                        onClicked: if (controller) controller.setOption("Color.UseSpectrumFillGradient", checked)

                                    }
                                    ColorRow { key: "Color.MeterHighScale"; label: "Meter High Scale" }
                                    CheckBox {
                                        id: useUnderlayFillGradientChk;
                                        text: "Underlay Gradient"
                                        checked: controller ? Boolean(controller.options["Color.UseUnderlayFillGradient"]) : false
                                        onClicked: if (controller) controller.setOption("Color.UseUnderlayFillGradient", checked)
                                    }

                                    ColorRow { key: "Color.SpectrumFillTop"; label: "Spectrum Fill Top" }
                                    ColorRow { key: "Color.UnderlayLine"; label: "Underlay Line" }
                                    ColorRow { key: "Color.UnderlayFillTop"; label: "Underlay Fill Top" }

                                    ColorRow { key: "Color.SpectrumFillBot"; label: "Spectrum Fill Bot" }
                                    ColorRow { key: "Color.UnderlayFill"; label: "Underlay Fill" }
                                    ColorRow { key: "Color.WaterfallText"; label: "Underlay Fill Bot" }

                                    ColorRow { key: "Color.ClusterSpots"; label: "Cluster Spots" }
                                    ColorRow { key: "Color.ButtonOff"; label: "Button Off" }
                                    ColorRow { key: "Color.ButtonOn"; label: "Button On" }
                                }
                            }
                        }
                    }
                }

                // =========================
                // Page 2: Radio Settings
                // =========================
                Item {
                    id: radioSettings
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 8
                            Label { id: modInputDataOffComboText; text: "Data Off Modulation Input:" }
                            ComboBox {
                                id: modInputCombo
                                Accessible.name: "Modulation Input"
                                Accessible.description: "Transmit modulation source"
                                model: [] // TODO
                            }
                            Label { id: modInputData1ComboText; text: "(Data Mod Inputs) DATA1:" }
                            ComboBox { id: modInputData1Combo; model: [] }
                            Label { id: modInputData2ComboText; text: "DATA2:" }
                            ComboBox { id: modInputData2Combo; model: [] }
                            Label { id: modInputData3ComboText; text: "DATA3:" }
                            ComboBox { id: modInputData3Combo; model: [] }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Button {
                                id: setClockBtn
                                text: "Set Clock"
                                ToolTip.visible: hovered
                                ToolTip.text: "Sets the radio clock when seconds go to zero."
                            }
                            CheckBox {
                                id: useUTCChk
                                text: "Use UTC"
                                ToolTip.visible: hovered
                                ToolTip.text: "Set radio clock to UTC; otherwise uses local timezone."
                            }
                            CheckBox { id: setRadioTimeChk; text: "Set radio time on connect (takes up to a minute)" }
                            Item { Layout.fillWidth: true }
                            Button { id: satOpsBtn; text: "Satellite Ops" }
                            Button {
                                id: adjRefBtn
                                text: "Adjust Reference"
                                ToolTip.visible: hovered
                                ToolTip.text: "Adjust the frequency reference on the IC-9700."
                            }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: "Manual PTT Toggle" }
                            Button { id: pttOnBtn; text: "PTT On" }
                            Button { id: pttOffBtn; text: "PTT Off" }
                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 3: Radio Server
                // =========================
                Item {
                    id: radioServer
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 12
                            CheckBox { id: serverEnableCheckbox; text: "Enable" }
                            Item { width: 20 }
                            CheckBox { id: serverDisableUIChk; text: "Disable local user controls when in use (restart required)" }
                            Item { Layout.fillWidth: true }
                        }

                        GroupBox {
                            id: serverSetupGroup
                            title: "Server Setup"
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                spacing: 10

                                // ports row
                                RowLayout {
                                    spacing: 8

                                    Label { id: serverControlPortLabel; text: "Control Port" }
                                    TextField { id: serverControlPortText; text: "50001"; Layout.preferredWidth: 130; inputMask: "99999" }

                                    Label { id: serverCATPortLabel; text: "CAT Port" }
                                    TextField { id: serverCivPortText; text: "50002"; Layout.preferredWidth: 130; inputMask: "99999" }

                                    Label { id: serverAudioPortLabel; text: "Audio Port" }
                                    TextField { id: serverAudioPortText; text: "50003"; Layout.preferredWidth: 130; inputMask: "99999" }

                                    Label { id: serverScopePortLabel; text: "Scope Port" }
                                    TextField { id: serverScopePortText; text: "50004"; Layout.preferredWidth: 130; inputMask: "99999" }

                                    Item { Layout.fillWidth: true }
                                }

                                // server audio routing row
                                RowLayout {
                                    spacing: 8
                                    Label { text: "RX Audio Input" }
                                    ComboBox { id: serverRXAudioInputCombo; model: []; Layout.preferredWidth: 160 }
                                    Label { text: "TX Audio Output" }
                                    ComboBox { id: serverTXAudioOutputCombo; model: []; Layout.preferredWidth: 160 }
                                    Item { width: 20 }
                                    Label { text: "Audio System" }
                                    ComboBox { id: audioSystemServerCombo; model: ["Qt Audio", "PortAudio", "RT Audio"] }
                                    Item { Layout.fillWidth: true }
                                }

                                // users table
                                GroupBox {
                                    title: "Users"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 220

                                    ListView {
                                        id: serverUsersTable
                                        anchors.fill: parent
                                        clip: true

                                        model: ListModel {
                                            // ListElement { username: "user"; password: "pass"; admin: "no" }
                                        }

                                        delegate: Rectangle {
                                            width: serverUsersTable.width
                                            implicitHeight: 28
                                            border.width: 1

                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 8

                                                Label { Layout.preferredWidth: 140; text: username }   // ListModel roles are direct
                                                Label { Layout.preferredWidth: 140; text: password }
                                                Label { Layout.preferredWidth: 140; text: admin }
                                            }
                                        }
                                    }
                                }


                                Label {
                                    id: serverDisconnectLabel
                                    text: "Please disconnect from radio to make changes to the server settings"
                                    wrapMode: Text.WordWrap
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                    }
                }

                // =========================
                // Page 4: External Control
                // =========================
                Item {
                    id: externalControl
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 8
                            CheckBox {
                                id: enableRigctldChk
                                text: "Enable RigCtld"
                                Accessible.name: "Enable RIGCTLD checkbox"
                            }
                            Item { width: 20 }
                            Label { text: "Port" }
                            TextField {
                                id: rigctldPortTxt
                                Layout.preferredWidth: 75
                                Layout.maximumWidth: 75
                                Accessible.name: "RIGCTLD server port"
                            }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { id: vspLabel; text: "Virtual Serial Port" }
                            ComboBox {
                                id: vspCombo
                                Layout.preferredWidth: 250
                                Layout.maximumWidth: 250
                                Accessible.name: "Virtual Serial Port Selector"
                                ToolTip.visible: hovered
                                ToolTip.text: "Define a virtual serial port. On Windows: loopback device for other programs. On Linux/macOS: pseudo-terminal."
                                model: [] // TODO
                            }
                            Label { id: ptyDeviceLabel; text: "" }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: "TCP Server Port" }
                            TextField { id: tcpServerPortTxt; Layout.preferredWidth: 80 }
                            Label { text: "Enter port for TCP server, 0 = disabled (restart required if changed)" }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: "TCI Server Port" }
                            TextField { id: tciServerPortTxt; Layout.preferredWidth: 80 }
                            Label { text: "Enter port for TCI server 0 = disabled (restart required if changed)" }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: "Waterfall Format" }
                            ComboBox { id: waterfallFormatCombo; model: ["Default", "Single (network)", "Multi (serial)"] }
                            Label { text: "Only change this if you are absolutely sure you need it (connecting to N1MM+ or similar)" }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            CheckBox { id: enableUsbChk; text: "Enable USB Controllers"; Accessible.name: "Enable USB Controllers Checkbox" }
                            Button { id: usbControllersSetup; text: "Setup USB Controller"; Accessible.name: "Setup USB Controller button" }
                            Item { width: 20 }
                            Button { id: usbControllersReset; text: "Reset Buttons"; Accessible.name: "Reset USB Controllers Button" }
                            Label { id: usbResetLbl; text: "Only reset buttons/commands if you have issues." }
                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 5: DX Cluster
                // =========================
                Item {
                    id: clusterPage
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        Label {
                            text: "This page contains configuration for DX Cluster, either UDP style broadcast (from N1MM+/DXlog) or TCP connection to your favourite cluster"
                            wrapMode: Text.WordWrap
                        }

                        GroupBox {
                            id: clusterTcpGroup
                            title: "TCP Cluster Connection"
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            label: CheckBox {
                                id: clusterTcpEnable
                                text: "TCP Cluster Connection"
                                checked: true
                            }

                            enabled: clusterTcpEnable.checked
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 12

                                Item {
                                    id: root
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    readonly property var clusterModel: controller ? controller.clusterModel : null
                                    property int currentRow: -1

                                    function selectRow(r) { root.currentRow = r }

                                    // Column widths (easy to tweak)
                                    readonly property int wServer: 260
                                    readonly property int wPort: 90
                                    readonly property int wUser: 160
                                    readonly property int wPass: 160
                                    readonly property int wTimeout: 100
                                    readonly property int wDef: 90
                                    readonly property int rowH: 34
                                    readonly property int pad: 6

                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Button {
                                                text: "Add"
                                                enabled: root.clusterModel !== null
                                                onClicked: root.clusterModel.addEntry("localhost", 7300, "", "", 30, false)
                                            }
                                            Button {
                                                text: "Remove"
                                                enabled: root.clusterModel !== null && root.currentRow >= 0
                                                onClicked: {
                                                    root.clusterModel.removeEntry(root.currentRow)
                                                    root.currentRow = -1
                                                }
                                            }
                                            Item { Layout.fillWidth: true }
                                        }

                                        // Header (DON'T use RowLayout here; use Row + fixed widths for predictability)
                                        Rectangle {
                                            Layout.fillWidth: true
                                            height: root.rowH
                                            color: "#111"
                                            border.color: "#333"

                                            Row {
                                                anchors.fill: parent
                                                anchors.leftMargin: root.pad
                                                anchors.rightMargin: root.pad
                                                spacing: root.pad

                                                Label { width: root.wServer;  height: parent.height; verticalAlignment: Text.AlignVCenter; text: "Server";  font.bold: true; color: "white" }
                                                Label { width: root.wPort;    height: parent.height; verticalAlignment: Text.AlignVCenter; text: "Port";    font.bold: true; color: "white" }
                                                Label { width: root.wUser;    height: parent.height; verticalAlignment: Text.AlignVCenter; text: "User";    font.bold: true; color: "white" }
                                                Label { width: root.wPass;    height: parent.height; verticalAlignment: Text.AlignVCenter; text: "Password";font.bold: true; color: "white" }
                                                Label { width: root.wTimeout; height: parent.height; verticalAlignment: Text.AlignVCenter; text: "Timeout"; font.bold: true; color: "white" }
                                                Label { width: root.wDef;     height: parent.height; verticalAlignment: Text.AlignVCenter; text: "Default"; font.bold: true; color: "white" }
                                            }
                                        }

                                        ScrollView {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            clip: true

                                            // Critical for Qt5: give the content item a width
                                            contentItem: ListView {
                                                id: list
                                                width: ScrollView.view.width   // forces stable column layout
                                                model: root.clusterModel
                                                clip: true
                                                boundsBehavior: Flickable.StopAtBounds

                                                delegate: Rectangle {
                                                    width: list.width
                                                    height: root.rowH
                                                    color: (index === root.currentRow) ? "#2a2a2a" : ((index % 2) ? "#1f1f1f" : "#171717")
                                                    border.color: "#333"

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        onClicked: root.currentRow = index
                                                    }

                                                    Row {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: root.pad
                                                        anchors.rightMargin: root.pad
                                                        spacing: root.pad

                                                        TextField {
                                                            width: root.wServer
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            text: model.server
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onEditingFinished: root.clusterModel.setRoleValue(index, root.clusterModel.ServerRole, text)
                                                        }

                                                        TextField {
                                                            width: root.wPort
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            inputMethodHints: Qt.ImhDigitsOnly
                                                            validator: IntValidator { bottom: 1; top: 65535 }
                                                            text: String(model.port)
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onEditingFinished: root.clusterModel.setRoleValue(index, root.clusterModel.PortRole, parseInt(text))
                                                        }

                                                        TextField {
                                                            width: root.wUser
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            text: model.userName
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onEditingFinished: root.clusterModel.setRoleValue(index, root.clusterModel.UserNameRole, text)
                                                        }

                                                        TextField {
                                                            width: root.wPass
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            echoMode: TextInput.Password
                                                            text: model.password
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onEditingFinished: root.clusterModel.setRoleValue(index, root.clusterModel.PasswordRole, text)
                                                        }

                                                        TextField {
                                                            width: root.wTimeout
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            inputMethodHints: Qt.ImhDigitsOnly
                                                            validator: IntValidator { bottom: 1; top: 600 }
                                                            text: String(model.timeout)
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onEditingFinished: root.clusterModel.setRoleValue(index, root.clusterModel.TimeoutRole, parseInt(text))
                                                        }

                                                        Item {
                                                            width: root.wDef
                                                            height: parent.height
                                                            CheckBox {
                                                                anchors.centerIn: parent
                                                                checked: !!model.isdefault
                                                                onToggled: root.clusterModel.setDefaultRow(index, checked)
                                                                onPressed: root.currentRow = index
                                                                onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        GroupBox {
                            id: clusterUdpGroup
                            title: "UDP Broadcast Connection"
                            Layout.fillWidth: true

                            label: CheckBox {
                                    id: clusterUdpEnable
                                    text: "UDP Broadcast Connection"
                                    checked: true
                                }


                            RowLayout {
                                spacing: 8
                                Label { text: "UDP Port" }
                                TextField { id: clusterUdpPortLineEdit; inputMask: "00000"; Layout.preferredWidth: 120 }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        TextArea {
                            id: clusterOutputTextEdit
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            wrapMode: TextArea.Wrap
                        }

                        RowLayout {
                            spacing: 8
                            CheckBox { id: clusterSkimmerSpotsEnable; text: "Show Skimmer Spots" }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                // =========================
                // Page 6: Experimental
                // =========================
                Item {
                    id: experimental
                    anchors.fill: parent

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        Label {
                            text: "This page contains experimental features. Use at your own risk."
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            spacing: 8
                            Button {
                                id: debugBtn
                                text: "Debug"
                                ToolTip.visible: hovered
                                ToolTip.text: "Runs debug functions (see wfmain::on_debugBtn_clicked in wfmain.cpp)."
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 7: Audio Processing (empty in .ui)
                // =========================
                Item {
                    id: audioProcessing
                    anchors.fill: parent
                    Label { anchors.centerIn: parent; text: "Audio Processing (not implemented in .ui snippet)" }
                }
            }
        }

        // Bottom buttons
        RowLayout {
            id: bottomButtonsLayout
            Layout.fillWidth: true
            spacing: 8

            Button {
                id: saveSettingsBtn
                text: "Save Settings"
                Accessible.name: "Save Settings"
                onClicked: controller && controller.save()
            }

            Button {
                id: revertSettingsBtn
                text: "Revert to Default"
                Accessible.name: "Revert to Default"
            }

            Item { Layout.fillWidth: true }

            Button {
                id: connectBtn
                text: (connStatus === 0) ?
                          "Connect to Radio" :
                          (connStatus === 1) ?
                          "Cancel Connection" :
                          (connStatus === 2) ?
                          "Disconnect from Radio" : "Unknown status!"
                onClicked: MainController.connectionHandler()

                Accessible.name: "Connect to Radio"
            }

            Item { width: 20 } // fixed spacer (matches the .ui)
        }
    }

    // ---- Keyboard shortcuts from the .ui accessibleDescription ----
    Shortcut { sequence: "Shift+F1"; onActivated: settingsStack.currentIndex = 0 }
    Shortcut { sequence: "Shift+F2"; onActivated: settingsStack.currentIndex = 1 }
    Shortcut { sequence: "Shift+F3"; onActivated: settingsStack.currentIndex = 2 }
    Shortcut { sequence: "Shift+F4"; onActivated: settingsStack.currentIndex = 3 }
    Shortcut { sequence: "Shift+F5"; onActivated: settingsStack.currentIndex = 4 }
    Shortcut { sequence: "Shift+F6"; onActivated: settingsStack.currentIndex = 5 }
    Shortcut { sequence: "Shift+F7"; onActivated: settingsStack.currentIndex = 6 }
}
