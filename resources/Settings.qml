// SettingsWidget.qml
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

Window {
    id: window
    title: "Settings"
    //parent: Overlay.overlay      // <- IMPORTANT: ensures correct parenting to ApplicationWindow
    //modal: false
    //focus: true
    //standardButtons: Dialog.Close

    width: 1193
    height: 625

    // add later if/when you want it
    property var controller: null

    // ---- Helpers ----

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
                    text: model.title
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
                                    spacing: 6

                                    ComboBox {
                                        id: manufacturerCombo
                                        Accessible.name: "Radio Brand Select"
                                        model: [] // TODO: bind to C++
                                    }

                                    RadioButton {
                                        id: serialEnableBtn
                                        text: "Serial (USB)"
                                        Accessible.name: "Serial Port (USB)"
                                    }

                                    RadioButton {
                                        id: lanEnableBtn
                                        text: "Network"
                                        Accessible.name: "Network"
                                    }
                                }
                            }

                            GroupBox {
                                title: "CI-V and Model"
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
                                        ToolTip.text: "If you are using an older (year 2010) radio, you may need to enable this option to manually specify the CI-V address. This option is also useful for radios that do not have CI-V Transceive enabled and thus will not answer our broadcast query for connected rigs on the CI-V bus.\n\nIf you have a modern radio with CI-V Transceive enabled, you should not need to check this box.\n\nYou will need to Save Settings and re-launch wfview for this to take effect."
                                    }

                                    TextField {
                                        id: rigCIVaddrHexLine
                                        enabled: false
                                        Layout.preferredWidth: 50
                                        Layout.maximumWidth: 50
                                        text: "auto"
                                        Accessible.name: "Manual CI-V Textbox"
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Enter the address in hexadecimal, no prefix. Examples: IC-706:58, IC-756:50, IC-7300:94, IC-7100:88, etc. After changing, press Save Settings and re-launch wfview."
                                    }

                                    CheckBox {
                                        id: useCIVasRigIDChk
                                        Layout.columnSpan: 2
                                        text: "Use CI-V address as Model ID too"
                                        Accessible.name: "Use CI-V address as Model ID checkbox"
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Only check for older radios! Forces wfview to trust the CI-V address is also the model number. Do not check unless you have an older radio."
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
                                          "ONLY use Manual CI-V when Transceive mode is not supported"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // Serial Connected Radios
                        GroupBox {
                            id: groupSerial
                            title: "Serial Connected Radios"
                            Layout.fillWidth: true

                            RowLayout {
                                spacing: 8

                                Label { text: "Serial Device:" }
                                ComboBox {
                                    id: serialDeviceListCombo
                                    Layout.preferredWidth: 180
                                    Layout.maximumWidth: 180
                                    Accessible.name: "Serial (USB) Port Selection Combo"
                                    model: [] // TODO
                                }

                                Label { text: "Baud Rate" }
                                ComboBox {
                                    id: baudRateCombo
                                    Layout.preferredWidth: 120
                                    Layout.maximumWidth: 120
                                    Accessible.name: "Serial Baud Rate Combo"
                                    model: [] // TODO
                                }

                                Label { id: pttTypeLabel; text: "PTT Type" }
                                ComboBox {
                                    id: pttTypeCombo
                                    Accessible.name: "PTT Type Combo"
                                    model: ["CI-V", "RTS", "DTR"]
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        // Network Connected Radios
                        GroupBox {
                            id: groupNetwork
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
                                    }

                                    Label { id: controlPortLabel; text: "Control Port" }
                                    TextField {
                                        id: controlPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: "50001"
                                        Accessible.name: "Control Port Textbox"
                                    }

                                    Label { id: catPortLabel; text: "CAT" }
                                    TextField {
                                        id: catPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                    }

                                    Label { id: audioPortLabel; text: "Audio" }
                                    TextField {
                                        id: audioPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                    }

                                    Label { id: scopePortLabel; text: "Scope" }
                                    TextField {
                                        id: scopePortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                    }

                                    Label { text: "Connection Type" }
                                    ComboBox {
                                        id: networkConnectionTypeCombo
                                        Accessible.name: "Connection Type Combo"
                                        model: [] // TODO
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
                                    }

                                    Label { text: "Password" }
                                    TextField {
                                        id: passwordTxt
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        echoMode: TextInput.Password
                                        Accessible.name: "Password Textbox"
                                    }

                                    CheckBox {
                                        id: adminLoginChk
                                        text: "Admin Login"
                                        Accessible.name: "Admin Login Checkbox"
                                        Accessible.description: "Check this box if you are using the admin login (Kenwood radios only)"
                                    }

                                    Item { Layout.fillWidth: true }
                                }

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
                                        onValueChanged: rxLatencyValue.text = Math.round(value).toString()
                                    }
                                    Label { id: rxLatencyValue; text: "0" }

                                    Label { text: "TX Latency (ms)" }
                                    Slider {
                                        id: txLatencySlider
                                        from: 30
                                        to: 500
                                        stepSize: 1
                                        Accessible.name: "TX Latency Buffer Size"
                                        Layout.preferredWidth: 220
                                        onValueChanged: txLatencyValue.text = Math.round(value).toString()
                                    }
                                    Label { id: txLatencyValue; text: "0" }

                                    Label { text: "RX Codec" }
                                    ComboBox {
                                        id: audioRXCodecCombo
                                        Accessible.name: "Receive Audio Codec Selector"
                                        model: [] // TODO
                                    }

                                    Label { text: "TX Codec" }
                                    ComboBox {
                                        id: audioTXCodecCombo
                                        Accessible.name: "Transmit Audio Codec Selector"
                                        model: [] // TODO
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
                                        model: ["48000", "24000", "16000", "8000"]
                                    }

                                    Label { text: "Duplex" }
                                    ComboBox {
                                        id: audioDuplexCombo
                                        Accessible.name: "Full or Half Duplex Combo"
                                        model: ["Full Duplex", "Half Duplex"]
                                    }

                                    Label { text: "Audio System" }
                                    ComboBox {
                                        id: audioSystemCombo
                                        Accessible.name: "Audio System Selection Combo"
                                        model: ["Qt Audio", "PortAudio", "RT Audio", "TCI Audio"]
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: audio output/input
                                RowLayout {
                                    spacing: 8
                                    Label { text: "Audio Output" }
                                    ComboBox {
                                        id: audioOutputCombo
                                        Layout.preferredWidth: 200
                                        Layout.maximumWidth: 200
                                        Accessible.name: "Audio Output Selector"
                                        model: [] // TODO
                                    }

                                    Label { text: "Audio Input" }
                                    ComboBox {
                                        id: audioInputCombo
                                        Layout.preferredWidth: 200
                                        Layout.maximumWidth: 200
                                        Accessible.name: "Audio Input Selector"
                                        model: [] // TODO
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                Label {
                                    id: radioDisconnectLabel
                                    text: "You MUST disconnect from the radio before making any changes in the above form.\n\nPlease use the Connect/Disconnect button below"
                                    wrapMode: Text.WordWrap
                                }

                                Item { Layout.fillHeight: true }
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
                                text: "When tuning, set lower digits to zero"
                                checked: true
                            }
                            CheckBox {
                                id: autoSSBchk
                                text: "Auto SSB"
                                Accessible.name: "Auto SSB Switching"
                                Accessible.description: "When using SSB, automatically switch to the standard sideband for a given band."
                                ToolTip.visible: hovered
                                ToolTip.text: "When using SSB, automatically switch to the standard sideband for a given band."
                            }
                            CheckBox { id: pttEnableChk; text: "Enable PTT Controls" }
                            CheckBox {
                                id: rigCreatorChk
                                text: "Enable Rig Creator Feature (use with care)"
                                ToolTip.visible: hovered
                                ToolTip.text: "Rig creator allows changing of all rig features and adding new rig profiles"
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
                                placeholderText: "1"
                            }
                        }

                        RowLayout {
                            spacing: 12
                            CheckBox {
                                id: wfInterpolateChk
                                text: "Interpolate Waterfall"
                                checked: true
                                ToolTip.visible: hovered
                                ToolTip.text: "Enables interpolation between pixels. Note that this will increase CPU usage."
                            }
                            CheckBox { id: wfAntiAliasChk; text: "Anti-Alias Waterfall" }
                            CheckBox { id: clickDragTuningEnableChk; text: "Allow tuning via click and drag (experimental)" }
                            CheckBox { id: useSystemThemeChk; text: "Use System Theme" }
                            CheckBox { id: fullScreenChk; text: "Show full screen (F11)" }
                            Item { Layout.fillWidth: true }
                        }

                        // Frequency display separators
                        RowLayout {
                            spacing: 8
                            Label { text: "Frequency Display:" }
                            Label { text: "Units" }
                            ComboBox {
                                id: frequencyUnitsCombo
                                model: ["None", "Hz", "kHz", "MHz", "GHz"]
                            }
                            Label { text: "Separators:" }
                            Label { text: "Decimal" }
                            ComboBox { id: decimalSeparatorsCombo; model: [] } // TODO
                            Label { text: "Groups" }
                            ComboBox { id: groupSeparatorsCombo; model: [] } // TODO
                            CheckBox { id: forceVfoModeChk; text: "Force VFO Mode"; checked: true }
                            CheckBox { id: autoPowerOnChk; text: "Auto Power-on radio"; checked: true }
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
                            ComboBox { id: meter2selectionCombo; model: [] } // TODO
                            ComboBox { id: meter3selectionCombo; model: [] } // TODO
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
                                model: ["1", "2", "3", "4", "5"]
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
                                    id: gridLayout_5_qml
                                    columns: 12
                                    columnSpacing: 6
                                    rowSpacing: 6

                                    // ---- Row 0: Grid, Tuning line, Meter Scale ----
                                    Button { id: colorSetBtnGrid; text: "Grid" }
                                    Item { } // column 1 unused in .ui grid
                                    TextField {
                                        id: colorEditGrid
                                        text: "#AARRGGBB"
                                        Layout.preferredWidth: 90
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Color format is #AARRGGBB (AA alpha). 00 transparent, ff opaque."
                                    }
                                    LedSwatch { id: colorSwatchGrid }
                                    Item { width: 20 } // fixed spacer
                                    Button { id: colorSetBtnTuningLine; text: "Tuning Line" }
                                    TextField { id: colorEditTuningLine; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchTuningLine }
                                    Item { width: 20 } // spacer
                                    Button { id: colorSetBtnMeterScale; text: "Meter Scale" }
                                    TextField { id: colorEditMeterScale; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchMeterScale }

                                    // ---- Row 1: Axis, Passband, Meter Text ----
                                    Button { id: colorSetBtnAxis; text: "Axis" }
                                    Item { }
                                    TextField { id: colorEditAxis; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchAxis }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnPassband; text: "Passband" }
                                    TextField { id: colorEditPassband; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchPassband }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnMeterText; text: "Meter Text" }
                                    TextField { id: colorEditMeterText; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchMeterText }

                                    // ---- Row 2: Text, PBT, Waterfall Back ----
                                    Button { id: colorSetBtnText; text: "Text" }
                                    Item { }
                                    TextField { id: colorEditText; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchText }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnPBT; text: "PBT Indicator" }
                                    TextField { id: colorEditPBT; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchPBT }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnwfBackground; text: "Waterfall Back" }
                                    TextField { id: colorEditWfBackground; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchWfBackground }

                                    // ---- Row 3: Plot background, Meter level, Waterfall Grid ----
                                    Button { id: colorSetBtnPlotBackground; text: "Plot Background" }
                                    Item { }
                                    TextField { id: colorEditPlotBackground; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchPlotBackground }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnMeterLevel; text: "Meter Level" }
                                    TextField { id: colorEditMeterLevel; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchMeterLevel }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnWfGrid; text: "Waterfall Grid" }
                                    TextField { id: colorEditWfGrid; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchWfGrid }

                                    // ---- Row 4: Spectrum line, Meter average, Waterfall axis ----
                                    Button { id: colorSetBtnSpecLine; text: "Spectrum Line" }
                                    Item { }
                                    TextField { id: colorEditSpecLine; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchSpecLine }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnMeterAvg; text: "Meter Average" }
                                    TextField { id: colorEditMeterAvg; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchMeterAverage }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnWfAxis; text: "Waterfall Axis" }
                                    TextField { id: colorEditWfAxis; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchWfAxis }

                                    // ---- Row 5: Spectrum fill, Meter peak, Waterfall text ----
                                    Button { id: colorSetBtnSpecFill; text: "Spectrum Fill" }
                                    Item { }
                                    TextField { id: colorEditSpecFill; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchSpecFill }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnMeterPeakLevel; text: "Meter Peak Level" }
                                    TextField { id: colorEditMeterPeakLevel; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchMeterPeakLevel }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnWfText; text: "Waterfall Text" }
                                    TextField { id: colorEditWfText; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchWfText }

                                    // ---- Row 6: Spectrum gradient + Meter high scale ----
                                    CheckBox { id: useSpectrumFillGradientChk; text: "Spectrum Gradient" }
                                    Item { }
                                    Item { }
                                    Item { } // align
                                    Item { width: 20 }
                                    Button { id: colorSetBtnMeterPeakScale; text: "Meter High Scale" }
                                    TextField { id: colorEditMeterPeakScale; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchMeterPeakScale }
                                    Item { width: 20 }
                                    CheckBox { id: useUnderlayFillGradientChk; text: "Underlay Gradient" }
                                    Item { Layout.columnSpan: 3 } // keep grid sane

                                    // ---- Row 7: Spectrum fill top + underlay line + underlay fill top ----
                                    Button { id: colorSetBtnSpectFillTop; text: "Spectrum Fill Top" }
                                    Item { }
                                    TextField { id: colorEditSpecFillTop; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchSpecFillTop }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnUnderlayLine; text: "Underlay Line" }
                                    TextField { id: colorEditUnderlayLine; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchUnderlayLine }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnUnderlayFillTop; text: "Underlay Fill Top" }
                                    TextField { id: colorEditUnderlayFillTop; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchUnderlayFillTop }

                                    // ---- Row 8: Spectrum fill bot + underlay fill + underlay fill bot ----
                                    Button { id: colorSetBtnSpectFillBot; text: "Spectrum Fill Bot" }
                                    Item { }
                                    TextField { id: colorEditSpecFillBot; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchSpecFillBot }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnUnderlayFill; text: "Underlay Fill" }
                                    TextField { id: colorEditUnderlayFill; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchUnderlayFill }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnUnderlayFillBot; text: "Underlay Fill Bot" }
                                    TextField { id: colorEditUnderlayFillBot; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchUnderlayFillBot }

                                    // ---- Row 15: Cluster spots + Button off/on ----
                                    Button { id: colorSetBtnClusterSpots; text: "Cluster Spots" }
                                    Item { }
                                    TextField { id: colorEditClusterSpots; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchClusterSpots }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnButtonOff; text: "Button Off" }
                                    TextField { id: colorEditButtonOff; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchButtonOff }
                                    Item { width: 20 }
                                    Button { id: colorSetBtnButtonOn; text: "Button On" }
                                    TextField { id: colorEditButtonOn; text: "#AARRGGBB"; Layout.preferredWidth: 90 }
                                    LedSwatch { id: colorSwatchButtonOn }
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

                                    // Stand-in for custom tableWidget: use a TableView.
                                    TableView {
                                        id: serverUsersTable
                                        anchors.fill: parent
                                        clip: true

                                        // TODO: bind to model from C++
                                        model: ListModel {
                                            // Example placeholders:
                                            // ListElement { username: "user"; password: "pass"; admin: "no" }
                                        }

                                        delegate: Rectangle {
                                            implicitHeight: 28
                                            border.width: 1
                                            RowLayout {
                                                anchors.fill: parent
                                                spacing: 8
                                                Label { Layout.preferredWidth: 140; text: model.username ?? "" }
                                                Label { Layout.preferredWidth: 140; text: model.password ?? "" }
                                                Label { Layout.preferredWidth: 140; text: model.admin ?? "" }
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
                    id: cluster
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

                            label: CheckBox {
                                id: clusterTcpEnable
                                text: "TCP Cluster Connection"
                                checked: true
                            }

                            enabled: clusterTcpEnable.checked
                            RowLayout {
                                spacing: 12

                                // approximate FormLayout using GridLayout
                                GridLayout {
                                    columns: 2
                                    columnSpacing: 8
                                    rowSpacing: 8

                                    Label { text: "Server Name" }
                                    RowLayout {
                                        spacing: 6
                                        ComboBox {
                                            id: clusterServerNameCombo
                                            editable: true
                                            model: [] // TODO
                                            Layout.preferredWidth: 240
                                        }
                                        Button { id: clusterTcpAddBtn; text: "Add/Update"; Layout.preferredWidth: 90 }
                                        Button { id: clusterTcpDelBtn; text: "Del"; Layout.preferredWidth: 60 }
                                    }

                                    Label { text: "Server Port" }
                                    TextField { id: clusterTcpPortLineEdit; inputMask: "00000"; Layout.preferredWidth: 120 }

                                    Label { text: "Username" }
                                    TextField { id: clusterUsernameLineEdit; Layout.preferredWidth: 240 }

                                    Label { text: "Password" }
                                    TextField { id: clusterPasswordLineEdit; echoMode: TextInput.Normal; Layout.preferredWidth: 240 }

                                    Label { text: "Spot Timeout (minutes)" }
                                    TextField { id: clusterTimeoutLineEdit; inputMask: "0000"; Layout.preferredWidth: 120 }

                                    Item { } // empty label cell
                                    RowLayout {
                                        spacing: 8
                                        Button { id: clusterTcpConnectBtn; text: "Connect" }
                                        Button { id: clusterTcpDisconnectBtn; text: "Disconnect" }
                                        Item { Layout.fillWidth: true }
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
            }

            Button {
                id: revertSettingsBtn
                text: "Revert to Default"
                Accessible.name: "Revert to Default"
            }

            Item { Layout.fillWidth: true }

            Button {
                id: connectBtn
                text: "Connect to Radio"
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
