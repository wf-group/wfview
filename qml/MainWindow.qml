// Main.qml
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

ApplicationWindow {
    id: win

    title: MainController.windowTitle

    property int connStatus: Number(MainController.connStatus)

    width: 946
    visible: true

    minimumWidth:  mainLayout.implicitWidth
    //minimumHeight: mainLayout.implicitHeight+30
    minimumHeight: mainLayout.implicitHeight + 8
                   + mainGroup.padding * 2
                   + scopeVFOGroup.padding * 2
                   + mainLayout.spacing * 2
    height: minimumHeight

    LoggingWindow {
        id: loggingWindow
    }


    onClosing: function(close) {
        MainController.shutdown()
        close.accepted = true
    }

    FirstTimeSetup {
        id: firstTimeSetup
        visible: false

        onExitProgram: {
            Qt.quit()
        }

        onShowSettings: function(isNetwork) {
            // Mark first run as complete
            MainController.setFirstRun(false)

            // Show settings with appropriate mode
            MainController.showSettings(isNetwork)

            // Show main window
            mainWindow.visible = true
        }

        onSkipSetup: {
            // Mark first run as complete
            MainController.setFirstRun(false)

            // Show main window
            mainWindow.visible = true
        }
    }

    Settings {
        id: settings
        controller: MainController.settings
    }

    // Create memories window on demand
    Loader {
        id: memoriesLoader
        active: MainController.memoriesModel !== null
        sourceComponent: Component {
            Memories {
                memoriesModel: MainController.memoriesModel
            }
        }
    }

    Loader {
        id: selectRadioLoader
        source: "qrc:/qml/SelectRadio.qml"
        asynchronous: false

        onLoaded: {
            // Connect the window to MainController's radio controller
            item.selectRadioController = MainController.selectRadio
        }
    }

    // Load CWSender window
    Loader {
        id: cwSenderLoader
        source: "qrc:/qml/CWSender.qml"
        asynchronous: false
        onLoaded: {
            item.cwSenderController = MainController.cwSender
        }
    }

    Loader {
        id: debugLoader
        active: false
        source: "qrc:/qml/Debug.qml"

        onLoaded: {
            // show + activate
            item.visible = true
            item.raise()
            item.requestActivate()

            // when closed, destroy it (frees controller/timers/models too)
            item.closing.connect(function() {
                debugLoader.active = false
            })
        }
    }

    Shortcut {
        sequences: ["Ctrl+D"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!debugLoader.active) {
                debugLoader.active = true
            } else {
                debugLoader.item.visible = true
                debugLoader.item.raise()
                debugLoader.item.requestActivate()
            }
        }
    }

    // QWidget had acceptDrops=true
    // In QML, you typically use DropArea
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            // TODO: forward to C++ if needed
            // console.log("Dropped:", drop.urls)
        }
    }

    function isPointOutsideWindow(p) {
        return (
            p.x < 0 ||
            p.y < 0 ||
            p.x > win.contentItem.width ||
            p.y > win.contentItem.height
        )
    }

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




    Component {
            id: rigCreatorComponent
            RigCreator { }
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        spacing: 6

        // -------- 1) scopeVFOGroup (top) --------
        Frame {
            id: scopeVFOGroup
            Layout.fillWidth: true
            Layout.fillHeight: true
            //Layout.preferredHeight: implicitHeight   // content drives height
            padding: 0

            contentItem: ColumnLayout {
                spacing: 0

                Repeater {
                    model: MainController.receiverCount

                    delegate: Item {
                        id: row
                        Layout.fillWidth: true

                        Item { id: attachedHost; anchors.fill: parent }

                        property int pendingX: 0
                        property int pendingY: 0
                        property bool havePendingPos: false
                        property bool detached: false

                        visible: !detached
                        Layout.fillHeight: !detached
                        Layout.preferredHeight: detached ? 0 : -1
                        Layout.minimumHeight: detached ? 0 : 240

                        clip: true

                        Behavior on Layout.preferredHeight {
                            NumberAnimation { duration: 120 }   // tweak to taste
                        }

                        Loader {
                            id: rxLoader
                            active: true
                            sourceComponent: Receiver {
                                receiverIndex: index
                                controller: MainController.receiver(index)
                            }
                            onLoaded: {
                                rxLoader.item.parent = attachedHost
                                rxLoader.item.anchors.fill = attachedHost
                            }
                        }

                        Connections {
                            target: rxLoader.item
                            function onRequestDetach(p) {
                                Logging.info("onRequestDetach(p) " + p.x + "," + p.y)
                                if (MainController.isReceiverDetached(index))
                                    return

                                if (win.isPointOutsideWindow(p)) {
                                    const g = win.contentItem.mapToGlobal(p.x, p.y)
                                    row.pendingX = Math.round(g.x - 40)
                                    row.pendingY = Math.round(g.y - 16)
                                    row.havePendingPos = true

                                    Logging.info("detach gpos " + g.x + "," + g.y)
                                    MainController.setReceiverDetached(index, true)
                                }
                            }
                        }

                        Connections {
                            target: MainController
                            function onReceiverDetachedChanged(i, d) {
                                if (i !== index) return
                                detachedWin.visible = d
                                row.detached = d
                            }
                        }

                        ApplicationWindow {
                            id: detachedWin
                            visible: false
                            title: "Receiver " + (index + 1)
                            width: 900
                            height: 500

                            Item { id: detachedHost; anchors.fill: parent }

                            onVisibleChanged: function() {
                                if (!rxLoader.item) return

                                rxLoader.item.anchors.fill = undefined
                                rxLoader.item.parent = visible ? detachedHost : attachedHost
                                rxLoader.item.anchors.fill = visible ? detachedHost : attachedHost
                                Qt.callLater(MainController.updateApplicationPalette)

                                if (visible && row.havePendingPos) {
                                    Qt.callLater(function() {
                                        detachedWin.x = row.pendingX
                                        detachedWin.y = row.pendingY
                                        row.havePendingPos = false
                                    })
                                }
                            }


                            onClosing: function(close) {
                                MainController.setReceiverDetached(index, false)
                                close.accepted = true
                            }
                        }
                    }

                }

            }
        }

        // -------- 2) mainGroup (middle, fixed-ish) --------
        Frame {
            id: mainGroup
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            padding: 3

            RowLayout {
                spacing: 6

                // ---- meters + power buttons (left) ----
                ColumnLayout {
                    Layout.preferredWidth: 320
                    Layout.alignment: Qt.AlignTop

                    ColumnLayout {
                        spacing: 0

                        Meter {
                            id: meter1
                            width: 300
                            height: 40
                            //meterType: MainController ? MainController.meter1Type : 0
                            // only set the "current" level; MeterItem does peak/avg itself
                            //current: MainController ? MainController.meter1 : 0.0

                            drawLabels: true
                            Component.onCompleted: meter1.setMeterExtremities(-54, 60, 0)
                        }
                        Meter {
                            id: meter2
                            width: 300
                            height: 40
                            //meterType: MainController ? MainController.meter2Type : 0
                            // only set the "current" level; MeterItem does peak/avg itself
                            //current: MainController ? MainController.meter2 : 0.0

                            drawLabels: true
                            Component.onCompleted: meter2.setMeterExtremities(-54, 60, 0)
                        }
                    }

                    Item { Layout.fillHeight: true }

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
                        readonly property var spec: MainController ? MainController.uiSpecs["tuningSteps"] : null
                        model: spec ? spec.model : []
                        textRole: spec ? spec.textRole : "text"
                        valueRole: spec ? spec.valueRole : "value"
                        visible: spec ? (spec.visible ?? true) : false
                        currentIndex: MainController ? indexFromValue(tuningStepCombo, MainController.stepSize) : -1
                        onActivated: MainController.stepSize = currentValue
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

                        ColumnLayout {
                            Slider {
                                id: volumeSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "Vol"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: txPowerSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "TX"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: monitorSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "Mon"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider { id: micGainSlider; from: 0; to: 255; orientation: Qt.Vertical; Layout.preferredHeight: 120 }
                            Label { text: "Mic"; horizontalAlignment: Text.AlignHCenter }
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
                    Button {
                        text: "CW"
                        enabled: (connStatus === 2) // Only enable button if connected
                        onClicked: MainController.cwSender.visible = true
                    }

                    Button { text: "Rpt/Split" }
                    Button {
                        text: "Memories"
                        enabled: (connStatus === 2) // Only enable button if connected
                        onClicked: {
                            MainController.openMemories()
                            memoriesLoader.item.visible = true
                        }
                    }
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
                        title: "Other Controls"
                        Layout.fillWidth: true

                        ColumnLayout {
                            spacing: 3
                            RowLayout {
                                CheckBox { text: "CMP" }
                                CheckBox { text: "VOX" }
                            }
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

                Button {
                    text: "About"
                    onClicked: aboutDialog.open()
                }
                Button {
                    text: "Settings"
                    onClicked: settings.show()
                }
                Button {
                    text: "Save Settings"
                    onClicked: MainController.settings.save()
                }

                Button {
                    text: "Radio Status"
                    onClicked: MainController.selectRadio.visible = true
                }
                Button {
                    text: "Log"
                    onClicked: {
                        loggingWindow.show()
                        loggingWindow.raise()
                        loggingWindow.requestActivate()
                    }
                }
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

                Button {
                    text: (connStatus === 0) ?
                              "Connect to Radio" :
                              (connStatus === 1) ?
                              "Cancel Connection" :
                              (connStatus === 2) ?
                              "Disconnect from Radio" : "Unknown status!"
                    onClicked: MainController.connectionHandler()
                }

                Item { Layout.fillWidth: true } // spacer (horizontalSpacer_9)

                Button { text: "Exit Program"; onClicked: Qt.quit() }
            }
        }
    }

    // If you want a QWidget-like status bar, do this:
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label { text: "memoriesActive: " + (MainController && MainController.memoriesModel ? MainController.memoriesModel.memoryModeActive : false) }
            //Label { text: rig.statusText ?? "" }
            Item { Layout.fillWidth: true }
            //Label { text: audio.statusText ?? "" }
        }
    }


    Dialog {
        id: aboutDialog
        width: 700
        height: 621
        title: "About wfview"
        modal: true
        anchors.centerIn: parent

        // Remove default padding to let AboutBox fill the dialog
        padding: 0

        contentItem: AboutBox {
            // Set your build information here
            wfviewVersion: MainController ? MainController.uiSpecs["program"].wfviewVersion : ""
            gitShort: MainController ? MainController.uiSpecs["program"].gitShort : ""
            buildDate: MainController ? MainController.uiSpecs["program"].buildDate : ""
            buildTime: MainController ? MainController.uiSpecs["program"].buildTime : ""
            buildUser: MainController ? MainController.uiSpecs["program"].buildUser : ""
            buildHost: MainController ? MainController.uiSpecs["program"].buildHost : ""
            qtVersion: Qt.version ? Qt.version : ""
        }

        // Optional: Add a close button at the bottom
        standardButtons: Dialog.Close
    }

    Component.onCompleted: {
        MainController.updateApplicationPalette();
        // Check if this is the first run
        //if (MainController.isFirstRun()) {
        //    firstTimeSetup.visible = true
        //}
    }

}

