// DebugWindow.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: win
    transientParent: null
    width: 896
    height: 575

    title: qsTr("Debug Window")
    visible: false

    DebugController { id: debugController }
    property var controller: debugController

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        //padding: 8

        // Top labels
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: controller ? controller.cacheTitle : "Current cache items"
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: controller ? controller.queueTitle : "Current queue items"
            }
        }

        // Two tables
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Cache table


            Item {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.preferredWidth: win.width / 2

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4


                    // header
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [
                                { t: qsTr("ID"),   w:50 },
                                { t: qsTr("Function"), w:160 },
                                { t: qsTr("Value"), w:260 },
                                { t: qsTr("RX"), w:50 },
                                { t: qsTr("Retries"), w:50 },
                                { t: qsTr("Request"), w:120 },
                                { t: qsTr("Reply"), w:120 }
                            ]
                            delegate: Label {
                                text: modelData.t
                                font.bold: true
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                Layout.preferredWidth: modelData.w
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; opacity: 0.25; color: "black" }

                    ListView {
                        id: cacheList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: controller ? controller.cacheModel : []
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Rectangle {
                            width: cacheList.width
                            height: 26
                            color: (index % 2) ? Qt.rgba(0,0,0,0.03) : "transparent"

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.leftMargin: 2
                                Layout.rightMargin: 2
                                spacing: 6

                                Label { Layout.preferredWidth: 50;  text: modelData.id; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 160; text: modelData.func; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 260; text: modelData.value; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 50;  text: modelData.rx; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 50;  text: modelData.retries; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 120; text: modelData.req; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 120; text: modelData.reply; elide: Text.ElideRight }

                                Item { Layout.fillWidth: true } // eat remaining space
                            }
                        }
                    }
                }
            }

            // Queue table
            Item {
                SplitView.fillWidth: true
                SplitView.fillHeight: true
                SplitView.preferredWidth: win.width / 2


                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [
                                { t: qsTr("ID"), w:50 },
                                { t: qsTr("Function"), w:160 },
                                { t: qsTr("Priority"), w:70 },
                                { t: qsTr("Get/Set"), w:70 },
                                { t: qsTr("RX"), w:50 },
                                { t: qsTr("Value"), w:220 },
                                { t: qsTr("Recurring"), w:80 }
                            ]
                            delegate: Label {
                                text: modelData.t
                                font.bold: true
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                Layout.preferredWidth: modelData.w
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; opacity: 0.25; color: "black" }

                    ListView {
                        id: queueList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: controller ? controller.queueModel : []
                        boundsBehavior: Flickable.StopAtBounds

                        delegate: Rectangle {
                            width: queueList.width
                            height: 26
                            color: (index % 2) ? Qt.rgba(0,0,0,0.03) : "transparent"

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.leftMargin: 2
                                Layout.rightMargin: 2
                                spacing: 6

                                Label { Layout.preferredWidth: 50;  text: modelData.id; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 160; text: modelData.func; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 70;  text: modelData.pri; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 70;  text: modelData.getset; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 50;  text: modelData.rx; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 220; text: modelData.value; elide: Text.ElideRight }
                                Label { Layout.preferredWidth: 80;  text: modelData.rec; elide: Text.ElideRight }

                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
        }

        // Pause + intervals row
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            CheckBox {
                text: qsTr("Pause refresh")
                checked: controller ? controller.cachePaused : false
                onToggled: if (controller) controller.cachePaused = checked
            }

            Item { Layout.fillWidth: true }

            Label { text: qsTr("Refresh Interval (ms)") }
            TextField {
                width: 90
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 99999 }
                text: controller ? String(controller.cacheIntervalMs) : "500"
                onEditingFinished: if (controller) controller.cacheIntervalMs = parseInt(text)
            }

            CheckBox {
                text: qsTr("Pause refresh")
                checked: controller ? controller.queuePaused : false
                onToggled: if (controller) controller.queuePaused = checked
            }

            Item { Layout.fillWidth: true }

            Label { text: qsTr("Refresh Interval (ms)") }
            TextField {
                width: 90
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 99999 }
                text: controller ? String(controller.queueIntervalMs) : "1000"
                onEditingFinished: if (controller) controller.queueIntervalMs = parseInt(text)
            }
        }

        // Yaesu grid (4 rows x 75 cols like your widget)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            // header-ish label row is already in your widget (numbers on rows 0 and 2)
            GridLayout {
                id: yaesuGrid
                columns: 75
                rowSpacing: 2
                columnSpacing: 2
                Layout.fillWidth: true

                Repeater {
                    // 4 rows * 75 cols
                    model: 300

                    delegate: Rectangle {
                        readonly property int r: Math.floor(index / 75)
                        readonly property int c: index % 75
                        readonly property int byteIndex: (r === 1 ? c : (r === 3 ? (75 + c) : -1))
                        readonly property bool isNumberRow: (r === 0 || r === 2)
                        readonly property bool isDataRow: (r === 1 || r === 3)

                        implicitWidth: 18
                        implicitHeight: 18
                        radius: 2
                        border.width: 0

                        color: isDataRow ? Qt.rgba(0.5,0.5,0.5,1) : "transparent"

                        Text {
                            anchors.centerIn: parent
                            font.pixelSize: 11
                            text: {
                                if (isNumberRow) {
                                    // match your numbering style: row0 shows 000..074, row2 shows 075..149
                                    var base = (r === 2) ? 75 : 0
                                    return String(base + c).padStart(3, "0")
                                }
                                if (isDataRow && controller) {
                                    return controller.yaesuHexAt(byteIndex)
                                }
                                return ""
                            }
                            color: {
                                if (!isDataRow || !controller) return "black"
                                return controller.yaesuChangedAt(byteIndex) ? "red" : "black"
                            }
                        }
                    }
                }
            }
        }
    }
}
