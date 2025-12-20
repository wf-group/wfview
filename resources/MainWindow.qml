// Main.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: win
    width: 946
    height: 361
    visible: true
    title: "wfmain"

    // QWidget had acceptDrops=true
    // In QML, you typically use DropArea
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            // TODO: forward to C++ if needed
            // console.log("Dropped:", drop.urls)
        }
    }

    Component {
            id: rigCreatorComponent
            RigCreator { }
        }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // -------- 1) scopeVFOGroup (top) --------
        Frame {
            id: scopeVFOGroup
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // This corresponds to your vfoLayout (currently empty in .ui)
                // Drop your converted receiver controls in here:
                //ReceiverControls {   // <- your QML component name
                //    Layout.fillWidth: true
                //    // Layout.preferredHeight: ...
                //}

                // If you also have scope/waterfall above/below, place it here too
                // ScopePanel { Layout.fillWidth: true; Layout.fillHeight: true }
            }
        }

        // -------- 2) mainGroup (middle, fixed-ish) --------
        Frame {
            id: mainGroup
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            padding: 3

            RowLayout {
                anchors.fill: parent
                spacing: 6

                // ---- meters + power buttons (left) ----
                ColumnLayout {
                    Layout.preferredWidth: 320
                    Layout.alignment: Qt.AlignTop

                    ColumnLayout {
                        spacing: 0

                        // Replace these with your QML meter items (you had a custom QWidget 'meter')
                        //MeterItem { id: meter1; Layout.fillWidth: true; Layout.preferredHeight: 30 }
                        //MeterItem { id: meter2; Layout.fillWidth: true; Layout.preferredHeight: 30 }
                        //MeterItem { id: meter3; Layout.fillWidth: true; Layout.preferredHeight: 30 }
                    }

                    RowLayout {
                        Button { text: "Power On"; onClicked: rig.powerOn() }
                        Button { text: "Power Off"; onClicked: rig.powerOff() }
                    }
                }

                // ---- tuningLayout (dial, step combo, lock, RIT) ----
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 6

                    Dial {
                        id: freqDial
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 80
                        from: 3000
                        to: 4000
                        stepSize: 10
                        wrap: true
                        // onMoved: rig.setVfoDial(value)   // if you want live tracking
                    }

                    ComboBox {
                        id: tuningStepCombo
                        Layout.fillWidth: true
                        // model: stepModel
                        // textRole: "text"
                        // valueRole: "value"
                        // onActivated: rig.setStep(currentValue)
                    }

                    CheckBox { id: tuneLockChk; text: "F Lock" }

                    RowLayout {
                        Dial {
                            id: ritTuneDial
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            from: -500
                            to: 500
                            stepSize: 10
                            wrap: false
                        }
                        CheckBox { id: ritEnableChk; text: "RIT" }
                    }
                }

                // ---- levelsHorizontalLayout (RF/AF/SQ/Mic/TX/Mon sliders) + OtherControls ----
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 6

                    RowLayout {
                        spacing: 10

                        function sliderCol(lbl, sliderId) {
                            // placeholder, QML doesn't support returning Items here;
                            // keep the explicit blocks below for clarity.
                            return null
                        }

                        ColumnLayout {
                            Slider { id: rfGainSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "RF"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: afGainSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "AF"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: sqlSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "SQ"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: micGainSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "Mic"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: txPowerSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "TX"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: monitorSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "Mon"; horizontalAlignment: Text.AlignHCenter }
                        }
                    }

                    GroupBox {
                        title: "Other Controls"
                        Layout.fillWidth: true

                        ColumnLayout {
                            spacing: 3

                            RowLayout {
                                CheckBox { text: "NB" }
                                CheckBox { text: "NR" }
                                CheckBox { text: "IP+" }
                            }
                            RowLayout {
                                CheckBox { text: "DS" }
                                CheckBox { text: "CMP" }
                                CheckBox { text: "VOX" }
                            }
                        }
                    }
                }

                // ---- buttonsVerticalLayout (Transmit, ATU, etc.) ----
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 6

                    Button {
                        text: "Transmit"
                        Layout.preferredHeight: 50
                        onClicked: rig.toggleTx()
                    }
                    CheckBox { text: "Enable ATU" }
                    Button { text: "Tune" }
                    Button { text: "CW" }
                    Button { text: "Rpt/Split" }
                    Button { text: "Memories" }
                }

                // ---- rightControlsLayout (Scope Settings, Preamp/Att, Antenna) ----
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 6

                    GroupBox {
                        title: "Scope Settings"
                        Layout.fillWidth: true

                        GridLayout {
                            columns: 3
                            rowSpacing: 6
                            columnSpacing: 6

                            Button { text: "Dual Scope"; checkable: true }
                            Button { text: "Dual Watch"; checkable: true }
                            Button { text: "Split"; checkable: true }

                            Button { text: "Main/Sub" }
                            Button { text: "Main<>Sub" }
                            Button { text: "Main=Sub" }
                        }
                    }

                    GroupBox {
                        title: "Preamp/Att"
                        Layout.fillWidth: true

                        ColumnLayout {
                            RowLayout {
                                Label { text: "Preamp:" }
                                ComboBox { id: preampSelCombo; Layout.fillWidth: true }
                            }
                            RowLayout {
                                Label { text: "Attenuator:" }
                                ComboBox { id: attSelCombo; Layout.fillWidth: true }
                            }
                        }
                    }

                    GroupBox {
                        title: "Antenna"
                        Layout.fillWidth: true

                        RowLayout {
                            ComboBox { id: antennaSelCombo; Layout.fillWidth: true }
                            CheckBox { id: rxAntennaCheck; text: "RX"; enabled: false }
                        }
                    }
                }

                Item { Layout.fillWidth: true } // spacer (your horizontalSpacer_3)
            }
        }

        // -------- 3) lowerButtonsFrame (bottom) --------
        Frame {
            id: lowerButtonsFrame
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            padding: 1

            RowLayout {
                anchors.fill: parent
                spacing: 6

                Button { text: "About" }
                Button { text: "Settings" }
                Button { text: "Save Settings" }
                Button { text: "Radio Status" }
                Button { text: "Log" }
                Button { text: "Bands" }
                Button { text: "Frequency" }
                Button {
                    text: "Rig Creator"
                    onClicked: {
                        const w = rigCreatorComponent.createObject(null)
                        if (w) w.show()
                    }
                }

                Item { Layout.fillWidth: true } // spacer (horizontalSpacer_32)

                Button { text: "Connect to Radio" }

                Item { Layout.fillWidth: true } // spacer (horizontalSpacer_9)

                Button { text: "Exit Program"; onClicked: Qt.quit() }
            }
        }
    }

    // If you want a QWidget-like status bar, do this:
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label { text: rig.statusText ?? "" }
            Item { Layout.fillWidth: true }
            Label { text: audio.statusText ?? "" }
        }
    }
}
