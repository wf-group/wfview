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
    property var audioProcessingWindow: null
    readonly property var mainControlSpecs: MainController.uiSpecs["mainControls"] || ({})
    readonly property var firstReceiver: MainController.receiverCount > 0 ? MainController.receiver(0) : null

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
            settings.show()
            win.visible = true
        }

        onSkipSetup: {
            win.visible = true
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

    function controlSpec(name, fallbackMin, fallbackMax) {
        var spec = mainControlSpecs[name]
        return spec ? spec : { "available": false, "min": fallbackMin, "max": fallbackMax }
    }

    function activeReceiverItem() {
        for (var i = 0; i < receiversRepeater.count; ++i) {
            var row = receiversRepeater.itemAt(i)
            if (row && row.receiverController && row.receiverController.active && row.receiverItem)
                return row.receiverItem
        }
        var firstRow = receiversRepeater.count > 0 ? receiversRepeater.itemAt(0) : null
        return firstRow ? firstRow.receiverItem : null
    }

    function openActiveReceiverBands() {
        var receiver = activeReceiverItem()
        if (receiver)
            receiver.bandPanelOpen = true
    }

    Component {
            id: rigCreatorComponent
            RigCreator { }
    }

    Component {
        id: audioProcessingComponent
        AudioProcessing {
            controller: MainController.settings
            onClosing: function(close) {
                win.audioProcessingWindow = null
                destroy()
            }
        }
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
                    id: receiversRepeater
                    model: MainController.receiverCount

                    delegate: Item {
                        id: row
                        Layout.fillWidth: true

                        Item { id: attachedHost; anchors.fill: parent }

                        Rectangle {
                            anchors.fill: attachedHost
                            anchors.margins: 1
                            color: "transparent"
                            border.width: 1
                            border.color: "red"
                            enabled: false
                            visible: row.receiverController && row.receiverController.active && !row.detached
                            z: 10
                        }

                        property int pendingX: 0
                        property int pendingY: 0
                        property bool havePendingPos: false
                        property bool detached: false
                        readonly property var receiverController: MainController.receiver(index)
                        readonly property var receiverItem: rxLoader.item
                        readonly property bool receiverVisible: receiverController
                                                               && (receiverController.active
                                                                   || MainController.dualScope)

                        visible: !detached && receiverVisible
                        Layout.fillHeight: !detached && receiverVisible
                        Layout.preferredHeight: (detached || !receiverVisible) ? 0 : -1
                        Layout.minimumHeight: (detached || !receiverVisible) ? 0 : 240

                        clip: true

                        Behavior on Layout.preferredHeight {
                            NumberAnimation { duration: 120 }   // tweak to taste
                        }

                        Loader {
                            id: rxLoader
                            active: true
                            sourceComponent: Receiver {
                                receiverIndex: index
                                controller: row.receiverController
                            }
                            onLoaded: {
                                rxLoader.item.parent = attachedHost
                                rxLoader.item.anchors.fill = attachedHost
                                rxLoader.item.anchors.margins = 1
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

                            Rectangle {
                                anchors.fill: detachedHost
                                anchors.margins: 1
                                color: "transparent"
                                border.width: 1
                                border.color: "red"
                                enabled: false
                                visible: row.receiverController && row.receiverController.active
                                z: 10
                            }

                            onVisibleChanged: function() {
                                if (!rxLoader.item) return

                                rxLoader.item.anchors.fill = undefined
                                rxLoader.item.parent = visible ? detachedHost : attachedHost
                                rxLoader.item.anchors.fill = visible ? detachedHost : attachedHost
                                rxLoader.item.anchors.margins = 1
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
                        Button { text: "Power On"; onClicked: MainController.powerOn() }
                        Button { text: "Power Off"; onClicked: MainController.powerOff() }
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

                    CheckBox {
                        id: tuneLockChk
                        text: "F Lock"
                        onToggled: MainController.setFrequencyLock(checked)
                    }

                    RowLayout {
                        Dial {
                            id: ritTuneDial
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 30
                            readonly property var spec: controlSpec("ritFrequency", -500, 500)
                            from: spec.min
                            to: spec.max
                            value: MainController.ritFrequency
                            enabled: mainControlSpecs.canRit ?? false
                            stepSize: 10
                            wrap: false
                            onMoved: MainController.ritFrequency = Math.round(value)
                        }
                        CheckBox {
                            id: ritEnableChk
                            text: "RIT"
                            checked: MainController.ritEnabled
                            enabled: mainControlSpecs.canRit ?? false
                            onToggled: MainController.ritEnabled = checked
                        }
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
                                id: volumeSlider
                                from: 0
                                to: 255
                                value: firstReceiver ? firstReceiver.afGain : 0
                                enabled: firstReceiver !== null
                                orientation: Qt.Vertical
                                Layout.preferredHeight: 120
                                onMoved: if (firstReceiver) firstReceiver.afGain = Math.round(value)
                            }
                            Label { text: "Vol"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider {
                                id: txPowerSlider
                                readonly property var spec: controlSpec("txPower", 0, 255)
                                from: spec.min
                                to: spec.max
                                value: MainController.txPower
                                enabled: spec.available ?? false
                                orientation: Qt.Vertical
                                Layout.preferredHeight: 120
                                onMoved: MainController.txPower = Math.round(value)
                            }
                            Label { text: "TX"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider {
                                id: monitorSlider
                                readonly property var spec: controlSpec("monitorGain", 0, 255)
                                from: spec.min
                                to: spec.max
                                value: MainController.monitorGain
                                enabled: spec.available ?? false
                                orientation: Qt.Vertical
                                Layout.preferredHeight: 120
                                onMoved: MainController.monitorGain = Math.round(value)
                            }
                            Label { text: "Mon"; horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            Slider {
                                id: micGainSlider
                                readonly property var spec: controlSpec("micGain", 0, 255)
                                from: spec.min
                                to: spec.max
                                value: MainController.micGain
                                enabled: spec.available ?? false
                                orientation: Qt.Vertical
                                Layout.preferredHeight: 120
                                onMoved: MainController.micGain = Math.round(value)
                            }
                            Label { text: "Mic"; horizontalAlignment: Text.AlignHCenter }
                        }
                    }
                }

                // ---- buttonsVerticalLayout (Transmit, ATU, etc.) ----
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 6

                    Button {
                        text: MainController.transmitting ? "Receive" : "Transmit"
                        Layout.preferredHeight: 50
                        checkable: true
                        checked: MainController.transmitting
                        enabled: mainControlSpecs.canTransmit ?? false
                        onClicked: MainController.toggleTransmit()
                    }
                    CheckBox {
                        text: "Enable ATU"
                        checked: MainController.tunerEnabled
                        enabled: mainControlSpecs.canTune ?? false
                        onToggled: MainController.tunerEnabled = checked
                    }
                    Button {
                        text: "Tune"
                        enabled: mainControlSpecs.canTune ?? false
                        onClicked: MainController.tuneNow()
                    }
                    Button {
                        text: "CW"
                        enabled: (connStatus === 2) // Only enable button if connected
                        onClicked: MainController.cwSender.visible = true
                    }

                    Button {
                        text: "Rpt/Split"
                        enabled: false
                        ToolTip.visible: hovered
                        ToolTip.text: "Repeater setup has not been ported to QML yet."
                    }
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

                            Button {
                                text: "Dual Scope"
                                checkable: true
                                checked: MainController.dualScope
                                enabled: mainControlSpecs.canDualScope ?? false
                                onToggled: MainController.dualScope = checked
                            }
                            Button {
                                text: "Dual Watch"
                                checkable: true
                                checked: MainController.dualWatch
                                enabled: mainControlSpecs.canDualWatch ?? false
                                onToggled: MainController.dualWatch = checked
                            }
                            Button {
                                text: "Split"
                                checkable: true
                                checked: MainController.splitEnabled
                                enabled: mainControlSpecs.canSplit ?? false
                                onToggled: MainController.splitEnabled = checked
                            }

                            Button {
                                text: "Main/Sub"
                                enabled: mainControlSpecs.canMainSub ?? false
                                onClicked: MainController.selectMainSub()
                            }
                            Button {
                                text: "Main<>Sub"
                                enabled: mainControlSpecs.canSwapMainSub ?? false
                                onClicked: MainController.swapMainSub()
                            }
                            Button {
                                text: "Main=Sub"
                                enabled: mainControlSpecs.canEqualMainSub ?? false
                                onClicked: MainController.equalizeMainSub()
                            }
                        }
                    }

                    GroupBox {
                        title: "Other Controls"
                        Layout.fillWidth: true

                        ColumnLayout {
                            spacing: 3
                            RowLayout {
                                CheckBox {
                                    text: "CMP"
                                    checked: MainController.compressorEnabled
                                    enabled: mainControlSpecs.canCompressor ?? false
                                    onToggled: MainController.compressorEnabled = checked
                                }
                                CheckBox {
                                    text: "VOX"
                                    checked: MainController.voxEnabled
                                    enabled: mainControlSpecs.canVox ?? false
                                    onToggled: MainController.voxEnabled = checked
                                }
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
                    text: "Audio Processing"
                    onClicked: {
                        MainController.ensureAudioProcessors()
                        if (!win.audioProcessingWindow) {
                            win.audioProcessingWindow = audioProcessingComponent.createObject(null)
                        }
                        if (win.audioProcessingWindow) {
                            win.audioProcessingWindow.show()
                            win.audioProcessingWindow.raise()
                            win.audioProcessingWindow.requestActivate()
                        }
                    }
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
                Button {
                    text: "Bands"
                    onClicked: openActiveReceiverBands()
                }
                Button {
                    text: "Frequency"
                    enabled: false
                    ToolTip.visible: hovered
                    ToolTip.text: "Frequency input has not been ported to QML yet."
                }
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
