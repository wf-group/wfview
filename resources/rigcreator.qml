// RigCreator.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: dlg
    modal: true
    title: "Rig Creator"

    // QWidget had sizeGripEnabled=false; keep it fixed-ish unless you want resizable
    width: 2106
    height: 1042

    // optional: remove default dialog padding so it feels like your QWidget
    padding: 8

    // ---- reusable table panel (replaces your tableWidget/QTableWidget blocks) ----
    component TablePanel : GroupBox {
        id: panel
        property var model: null          // set from C++: QAbstractItemModel*
        property var columns: []          // [{ title, role, width? }, ...]
        property bool alternating: true
        property bool multiSelect: false

        Layout.fillWidth: true
        Layout.fillHeight: true

        TableView {
            id: tv
            anchors.fill: parent
            clip: true
            model: panel.model

            selectionModel: ItemSelectionModel { model: panel.model }

            // Basic delegate; you’ll probably want per-column delegates later
            delegate: Rectangle {
                implicitHeight: 24
                border.width: 0

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    elide: Text.ElideRight
                    text: {
                        // If you use a QAbstractTableModel, use display role by default
                        // For roles, adapt this.
                        // model.display is common in QML TableView with QAbstractItemModel
                        return (model.display !== undefined) ? model.display : ""
                    }
                }
            }

            // Optional alternating rows look
            rowSpacing: 0
            columnSpacing: 0
        }
    }

    // ---- main layout ----
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // =========================
        // Header row (Load/Save/Defaults + meta fields)
        // =========================
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button { text: "Load File"; onClicked: rigCreator.loadFile() }
            Button { text: "Save File"; onClicked: rigCreator.saveFile() }

            Item { Layout.fillWidth: true } // spacer (like your horizontalSpacer_2)

            Button { text: "Default Rigs"; onClicked: rigCreator.loadDefaultRigs() }

            Label { text: "Manufacturer" }
            ComboBox {
                id: manufacturer
                Layout.preferredWidth: 180
                model: rigCreator.manufacturersModel   // expose from C++
                textRole: "text"
                valueRole: "value"
                onActivated: rigCreator.setManufacturer(currentValue)
            }

            Label { text: "Model" }
            TextField {
                id: modelField
                Layout.preferredWidth: 220
                text: rigCreator.modelName ?? ""
                onEditingFinished: rigCreator.modelName = text
            }

            Label { text: "C-IV Address" }
            TextField {
                id: civAddress
                Layout.preferredWidth: 120
                text: rigCreator.civAddress ?? ""
                onEditingFinished: rigCreator.civAddress = text
            }

            Label { text: "RigCtlD Model" }
            TextField {
                id: rigctldModel
                Layout.preferredWidth: 90
                inputMask: "9999"
                text: rigCreator.rigctldModel ?? ""
                onEditingFinished: rigCreator.rigctldModel = text
            }
        }

        // =========================
        // Upper grid region (Inputs/ScopeModes/Widths/Modes, Commands, Features, Spectrum/Memories, Bands)
        // =========================
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 12
            columnSpacing: 8
            rowSpacing: 8

            // ---- Row 1 area: Inputs + Scope Modes + Widths + Modes (your row="1" col="1" layout "_2")
            RowLayout {
                Layout.row: 0
                Layout.column: 0
                Layout.columnSpan: 6
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                TablePanel {
                    title: "Inputs"
                    Layout.preferredWidth: 210
                    model: rigCreator.inputsModel
                }
                TablePanel {
                    title: "Scope Modes"
                    Layout.preferredWidth: 170
                    model: rigCreator.scopeModesModel
                }
                TablePanel {
                    title: "Widths"
                    Layout.preferredWidth: 220
                    model: rigCreator.widthsModel
                }
                TablePanel {
                    title: "Modes"
                    Layout.fillWidth: true
                    model: rigCreator.modesModel
                }
            }

            // ---- Commands (row=1 col=6 rowspan=3 colspan=3)
            TablePanel {
                title: "Commands"
                Layout.row: 0
                Layout.column: 6
                Layout.columnSpan: 3
                Layout.rowSpan: 3
                model: rigCreator.commandsModel
            }

            // ---- Features (row=1 col=11 rowspan=2)
            GroupBox {
                title: "Features"
                Layout.row: 0
                Layout.column: 11
                Layout.rowSpan: 2
                Layout.fillHeight: true
                Layout.preferredWidth: 220

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    CheckBox { text: "Has Spectrum"; checked: rigCreator.hasSpectrum; onToggled: rigCreator.hasSpectrum = checked }
                    CheckBox { text: "Has LAN"; checked: rigCreator.hasLAN; onToggled: rigCreator.hasLAN = checked }
                    CheckBox { text: "Has Ethernet"; checked: rigCreator.hasEthernet; onToggled: rigCreator.hasEthernet = checked }
                    CheckBox { text: "Has WiFi"; checked: rigCreator.hasWifi; onToggled: rigCreator.hasWifi = checked }
                    CheckBox { text: "Has Transmit"; checked: rigCreator.hasTransmit; onToggled: rigCreator.hasTransmit = checked }
                    CheckBox { text: "HasFDComms"; checked: rigCreator.hasFDComms; onToggled: rigCreator.hasFDComms = checked }
                    CheckBox { text: "Has Command 29"; checked: rigCreator.hasCommand29; onToggled: rigCreator.hasCommand29 = checked }

                    Item { Layout.fillHeight: true }
                }
            }

            // ---- Bands (row=2 col=0 rowspan=5 colspan=6)
            TablePanel {
                title: "Bands"
                Layout.row: 1
                Layout.column: 0
                Layout.columnSpan: 6
                Layout.rowSpan: 5
                model: rigCreator.bandsModel
            }

            // ---- Spectrum (row=3 col=11)
            GroupBox {
                title: "Spectrum"
                Layout.row: 2
                Layout.column: 11
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 6

                    Label { text: "Num RX" }
                    TextField {
                        text: rigCreator.numReceiver ?? "1"
                        onEditingFinished: rigCreator.numReceiver = text
                        Layout.preferredWidth: 80
                    }

                    Label { text: "VFO per RX" }
                    TextField {
                        text: rigCreator.numVFO ?? "1"
                        onEditingFinished: rigCreator.numVFO = text
                        Layout.preferredWidth: 80
                    }

                    Label { text: "Seq Max" }
                    TextField {
                        inputMask: "00"
                        text: rigCreator.seqMax ?? ""
                        onEditingFinished: rigCreator.seqMax = text
                        Layout.preferredWidth: 80
                    }

                    Label { text: "Amp Max" }
                    TextField {
                        inputMask: "000"
                        text: rigCreator.ampMax ?? ""
                        onEditingFinished: rigCreator.ampMax = text
                        Layout.preferredWidth: 80
                    }

                    Label { text: "Len Max" }
                    TextField {
                        inputMask: "000"
                        text: rigCreator.lenMax ?? ""
                        onEditingFinished: rigCreator.lenMax = text
                        Layout.preferredWidth: 80
                    }
                }
            }

            // ---- Memories (row=4 col=11)
            GroupBox {
                title: "Memories"
                Layout.row: 3
                Layout.column: 11
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 6

                    Label { text: "Grp" }
                    TextField { inputMask: "999"; text: rigCreator.memGroups ?? ""; onEditingFinished: rigCreator.memGroups = text; Layout.preferredWidth: 80 }

                    Label { text: "Mem" }
                    TextField { inputMask: "999"; text: rigCreator.memories ?? ""; onEditingFinished: rigCreator.memories = text; Layout.preferredWidth: 80 }

                    Label { text: "Sat" }
                    TextField { inputMask: "999"; text: rigCreator.satMemories ?? ""; onEditingFinished: rigCreator.satMemories = text; Layout.preferredWidth: 80 }

                    Label { text: "Start" }
                    TextField { inputMask: "999"; text: rigCreator.memStart ?? ""; onEditingFinished: rigCreator.memStart = text; Layout.preferredWidth: 80 }
                }
            }

            // ---- Tuning Steps + Periodic Commands (row=4 col=7)
            RowLayout {
                Layout.row: 3
                Layout.column: 6
                Layout.columnSpan: 5
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                TablePanel {
                    title: "Tuning Steps"
                    Layout.preferredWidth: 320
                    model: rigCreator.tuningStepsModel
                }
                TablePanel {
                    title: "Periodic Commands"
                    Layout.fillWidth: true
                    model: rigCreator.periodicCommandsModel
                }
            }
        }

        // =========================
        // Big bottom strip of tables (row=7 in your .ui)
        // =========================
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            spacing: 8

            TablePanel { title: "Antennas"; model: rigCreator.antennasModel; Layout.fillWidth: true }
            TablePanel { title: "Atten"; model: rigCreator.attenuatorsModel; Layout.preferredWidth: 150 }
            TablePanel { title: "Flters"; model: rigCreator.filtersModel; Layout.fillWidth: true }
            TablePanel { title: "Spans"; model: rigCreator.spansModel; Layout.fillWidth: true }
            TablePanel { title: "Preamps"; model: rigCreator.preampsModel; Layout.preferredWidth: 200 }
            TablePanel { title: "CTCSS"; model: rigCreator.ctcssModel; Layout.preferredWidth: 240 }
            TablePanel { title: "DTCS"; model: rigCreator.dtcsModel; Layout.preferredWidth: 120 }
            TablePanel { title: "Meters"; model: rigCreator.metersModel; Layout.preferredWidth: 320 }
            TablePanel { title: "Roofing Filters"; model: rigCreator.roofingModel; Layout.preferredWidth: 220 }
        }

        // =========================
        // Memory format fields (row=9 in your .ui)
        // =========================
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Main Memory Format" }
                TextField {
                    Layout.fillWidth: true
                    text: rigCreator.memoryFormat ?? ""
                    onEditingFinished: rigCreator.memoryFormat = text
                    ToolTip.visible: hovered
                    ToolTip.text: "See your QWidget tooltip text (keep it in docs/help)."
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Satellite Memory Format" }
                TextField {
                    Layout.fillWidth: true
                    text: rigCreator.satelliteFormat ?? ""
                    onEditingFinished: rigCreator.satelliteFormat = text
                }
            }
        }

        // bottom buttons (Dialog already has default buttons, but your QWidget didn’t show them)
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "Close"; onClicked: dlg.close() }
        }
    }
}
