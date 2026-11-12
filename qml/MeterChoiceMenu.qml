import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

// Meter picker: Primary and Secondary columns, plus the meter style.
// Choosing anything closes the menu.
// Optionally carries a "Show secondary VFO meter" toggle (pop-out window).
Popup {
    id: menu

    property int primaryChoice: 0
    property int secondaryChoice: 0
    // 0 Modern, 1 Analog, 2 Analog Cream, 3 LCD, 4 LCD Arc
    property int styleChoice: 1
    property bool showVfoOption: false
    property bool vfoChecked: false

    signal primaryChosen(int value)
    signal secondaryChosen(int value)
    signal styleChosen(int value)
    signal vfoToggled(bool checked)

    // Meter types with a printed scale on the vintage faces.
    // 1 (meterS) is S+Power: S on receive, Po on transmit.
    // Placeholders, to add once the faces have scales for them:
    //   { text: qsTr("Center"), value: 2 }, { text: qsTr("Tx/Rx Audio"), value: 12 },
    //   { text: qsTr("Tx Audio"), value: 10 }, { text: qsTr("Rx Audio"), value: 11 },
    //   { text: qsTr("dBu"), value: 14 }, { text: qsTr("dBu EMF"), value: 15 }, { text: qsTr("dBm"), value: 16 }
    readonly property var choices: [
        { text: qsTr("None"), value: 0 },
        { text: qsTr("S+Power"), value: 1 },
        { text: qsTr("SWR"), value: 3 },
        { text: qsTr("ALC"), value: 5 },
        { text: qsTr("Compression"), value: 6 },
        { text: qsTr("Voltage"), value: 7 },
        { text: qsTr("Current"), value: 8 }
    ]

    readonly property var styles: [
        { text: qsTr("Modern"), value: 0 },
        { text: qsTr("Analog"), value: 1 },
        { text: qsTr("Analog Cream"), value: 2 },
        { text: qsTr("LCD"), value: 3 },
        { text: qsTr("LCD Arc"), value: 4 }
    ]

    // Emitted when the window is too small for the menu; the owner may grow it.
    signal needsSize(int width, int height)

    function openAt(item, px, py) {
        if (parent.width < implicitWidth || parent.height < implicitHeight)
            needsSize(implicitWidth, implicitHeight)
        const p = item.mapToItem(parent, px, py)
        x = Math.max(0, Math.min(p.x, parent.width - implicitWidth))
        y = Math.max(0, Math.min(p.y, parent.height - implicitHeight))
        open()
    }

    parent: Overlay.overlay
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 6

    ColumnLayout {
        spacing: 6

        RowLayout {
            spacing: 12

            Repeater {
                model: [
                    { title: qsTr("Primary"), secondary: false },
                    { title: qsTr("Secondary"), secondary: true }
                ]

                ColumnLayout {
                    id: column
                    readonly property bool secondary: modelData.secondary
                    Layout.alignment: Qt.AlignTop
                    spacing: 2

                    Label {
                        text: modelData.title
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Repeater {
                        model: menu.choices
                        Button {
                            Layout.fillWidth: true
                            text: modelData.text
                            enabled: {
                                MainController.uiSpecs
                                // Guarded: MainController goes away first at shutdown.
                                return typeof MainController.isOptionalMeterAvailable !== "function"
                                        || MainController.isOptionalMeterAvailable(modelData.value)
                            }
                            checkable: true
                            checked: (column.secondary ? menu.secondaryChoice : menu.primaryChoice) === modelData.value
                            onClicked: {
                                if (column.secondary)
                                    menu.secondaryChosen(modelData.value)
                                else
                                    menu.primaryChosen(modelData.value)
                                menu.close()
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 2

                Label {
                    text: qsTr("Style")
                    font.bold: true
                    Layout.alignment: Qt.AlignHCenter
                }

                Repeater {
                    model: menu.styles
                    Button {
                        Layout.fillWidth: true
                        text: modelData.text
                        checkable: true
                        checked: menu.styleChoice === modelData.value
                        onClicked: {
                            menu.styleChosen(modelData.value)
                            menu.close()
                        }
                    }
                }
            }
        }

        CheckBox {
            visible: menu.showVfoOption
            text: qsTr("Show secondary VFO meter")
            checked: menu.vfoChecked
            onClicked: {
                menu.vfoToggled(checked)
                menu.close()
            }
        }
    }
}
