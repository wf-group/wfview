// scope.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import RadioScope 1.0

Item {
    id: root
    anchors.fill: parent

    Drawer {
        id: sideDrawer
        edge: Qt.RightEdge
        height: parent.height

        // Width driven by (scaled) content width, capped to 90% of window
        implicitWidth: Math.min(grid.implicitWidth * settingsContainer.scaleFactor + 24,
                                parent.width * 0.9)
        width: implicitWidth

        Flickable {
            id: flick
            anchors.fill: parent
            clip: true

            contentWidth: settingsContainer.width
            contentHeight: sideDrawer.height

            // Wrapper that handles scaling
            Item {
                id: settingsContainer

                // Scale so full content fits vertically
                property real scaleFactor: {
                    if (grid.implicitHeight <= 0 || sideDrawer.height <= 0)
                        return 1.0;
                    return Math.min(1.0, sideDrawer.height / grid.implicitHeight);
                }

                width: grid.implicitWidth * scaleFactor
                height: grid.implicitHeight * scaleFactor

                transform: Scale {
                    origin.x: 0
                    origin.y: 0
                    xScale: settingsContainer.scaleFactor
                    yScale: settingsContainer.scaleFactor
                }

                GridLayout {
                    id: grid
                    columns: 2
                    rowSpacing: 8
                    columnSpacing: 8

                    // Title row
                    Label {
                        text: "Scope settings"
                        font.bold: true
                        Layout.columnSpan: 2
                    }

                    // === Ref slider ===
                    Label {
                        text: "Ref"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: refSlider
                        from: -160
                        to: 20
                        value: -80
                        Layout.fillWidth: true
                        // onValueChanged: spectrum.refLevel = value
                    }

                    // === Length slider ===
                    Label {
                        text: "Length"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: lengthSlider
                        from: 16
                        to: 1024
                        stepSize: 16
                        value: waterfall.length
                        Layout.fillWidth: true
                        onValueChanged: waterfall.length = value
                    }

                    // === Ceiling slider ===
                    Label {
                        text: "Ceiling"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: ceilingSlider
                        from: -160
                        to: 0
                        value: -20
                        Layout.fillWidth: true
                        // onValueChanged: spectrum.ceiling = value
                    }

                    // === Floor slider ===
                    Label {
                        text: "Floor"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: floorSlider
                        from: -200
                        to: 0
                        value: -100
                        Layout.fillWidth: true
                        // onValueChanged: spectrum.floor = value
                    }

                    // === Speed combobox ===
                    Label {
                        text: "Speed"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: speedCombo
                        model: [ "Very slow", "Slow", "Normal", "Fast", "Very fast" ]
                        currentIndex: 2
                        Layout.fillWidth: true
                        // onCurrentIndexChanged: waterfall.speedMode = currentIndex
                    }

                    // === Theme combobox ===
                    Label {
                        text: "Theme"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    ComboBox {
                        id: themeCombo
                        model: [ "Dark", "Light", "High contrast" ]
                        currentIndex: 0
                        Layout.fillWidth: true
                        // onCurrentTextChanged: backend.theme = currentText
                    }

                    // === PBT Inner slider ===
                    Label {
                        text: "PBT Inner"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: pbtInnerSlider
                        from: -3000
                        to: 3000
                        stepSize: 10
                        value: 0
                        Layout.fillWidth: true
                        // onValueChanged: backend.pbtInner = value
                    }

                    // === PBT Outer slider ===
                    Label {
                        text: "PBT Outer"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: pbtOuterSlider
                        from: -3000
                        to: 3000
                        stepSize: 10
                        value: 0
                        Layout.fillWidth: true
                        // onValueChanged: backend.pbtOuter = value
                    }

                    // === IF Shift slider + reset button ===
                    Label {
                        text: "IF Shift"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Slider {
                            id: ifShiftSlider
                            from: -5000
                            to: 5000
                            stepSize: 10
                            value: 0
                            Layout.fillWidth: true
                            // onValueChanged: backend.ifShift = value
                        }

                        Button {
                            text: "R"
                            Layout.preferredWidth: 32
                            onClicked: ifShiftSlider.value = 0
                        }
                    }

                    // === Filter width slider ===
                    Label {
                        text: "Filter width"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    Slider {
                        id: filterWidthSlider
                        from: 100
                        to: 12000
                        stepSize: 50
                        value: 2400
                        Layout.fillWidth: true
                        // onValueChanged: backend.filterWidth = value
                    }

                    // === Scope enabled checkbox ===
                    Label {
                        text: "Scope enabled"
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    }
                    CheckBox {
                        id: scopeEnabledCheckBox
                        text: ""
                        checked: true
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        onCheckedChanged: {
                            splitView.visible = checked
                            splitView.enabled = checked
                        }
                    }
                }
            }
        }
    }


    // MAIN CONTENT
    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Vertical

        background: null

        handle: Rectangle {
            implicitHeight: 5
            width: parent.width
            color: "#202020"
        }

        SpectrumItem {
            id: spectrum
            objectName: "spectrum"
            SplitView.minimumHeight: 60
            SplitView.preferredHeight: root.height * 0.5
        }

        WaterfallItem {
            id: waterfall
            objectName: "waterfall"
            SplitView.minimumHeight: 60
            SplitView.preferredHeight: root.height * 0.5
            length: 128
        }
    }

    // Toggle button for drawer (top-right)
    ToolButton {
        id: drawerButton
        text: "\u2630"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        onClicked: sideDrawer.open()
    }
}
