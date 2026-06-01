// Main.qml
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

ApplicationWindow {
    id: win

    title: radioConnected ? MainController.windowTitle : qsTr("wfview")

    property int connStatus: Number(MainController.connStatus)
    property var txAudioProcessingWindow: null
    property var rxAudioProcessingWindow: null
    property var rigCreatorWindows: []
    property bool quitConfirmed: false
    property bool shutdownStarted: false
    property var savedStartupGeometry: null
    property var lastConnectedGeometry: null
    property bool startupGeometryPending: false
    property bool connectedGeometryRestored: false
    property bool wasRadioConnected: false
    readonly property int contentHorizontalPadding: 8
    readonly property int contentTopPadding: 4
    readonly property int mainControlSpacing: 6
    readonly property int mainControlSliderHeight: 120
    readonly property int mainControlDialSize: mainControlSliderHeight
    readonly property int ritDialSize: 44
    readonly property int optionalMeterWidth: 300
    readonly property int optionalMeterHeight: 40
    readonly property bool radioConnected: Number(MainController.connStatus) === 2
    readonly property bool waylandPlatform: String(MainController.platformName()).indexOf("wayland") !== -1
    readonly property var mainControlSpecs: MainController.uiSpecs["mainControls"] || ({})
    readonly property var firstReceiver: MainController.receiverCount > 0 ? MainController.receiver(0) : null

    width: 946
    visible: false

    minimumWidth:  radioConnected ? 360 : mainLayout.implicitWidth + contentHorizontalPadding * 2
    //minimumHeight: mainLayout.implicitHeight+30
    minimumHeight: mainLayout.implicitHeight + 8
                   + mainGroup.padding * 2
                   + scopeVFOGroup.padding * 2
                   + mainLayout.spacing * 2
    height: minimumHeight
    onMinimumWidthChanged: {
        if (!win.radioConnected && !win.waylandPlatform)
            win.width = win.minimumWidth
    }
    onMinimumHeightChanged: {
        if (!win.radioConnected && !win.waylandPlatform)
            win.height = win.minimumHeight
    }

    component OptionalMeterSlot: Item {
        id: optionalMeterSlot

        property int slot: 2
        readonly property string optionKey: slot === 2 ? "Interface.Meter2Type" : "Interface.Meter3Type"
        readonly property int meterChoice: Number(MainController.settings.options[optionKey] || 0)
        readonly property double meterLevel: slot === 2 ? MainController.optionalMeter2Level : MainController.optionalMeter3Level

        width: 300
        height: 40

        function applyExtremities() {
            const ext = MainController.optionalMeterExtremities(meterChoice)
            if (ext.valid)
                optionalMeter.setMeterExtremities(ext.low, ext.high, ext.red)
            else
                optionalMeter.clearMeterExtremities()
        }

        Meter {
            id: optionalMeter
            anchors.fill: parent
            visible: optionalMeterSlot.meterChoice !== 0
            meterType: optionalMeterSlot.meterChoice
            current: optionalMeterSlot.meterLevel
            drawLabels: true
            reverseCompMeter: win.settingAsBool(MainController.settings.options["Interface.CompMeterReverse"], false)
            scaleTextColor: win.palette.windowText
            scaleLineColor: win.palette.windowText
            scaleHighTextColor: MainController.settings.options["Color.MeterHighScale"]
            scaleHighLineColor: MainController.settings.options["Color.MeterHighScale"]
            onConfigureMeterRequested: meterMenu.open()
            Component.onCompleted: optionalMeterSlot.applyExtremities()
        }

        Rectangle {
            anchors.fill: parent
            visible: optionalMeterSlot.meterChoice === 0
            color: Qt.rgba(win.palette.button.r, win.palette.button.g, win.palette.button.b, 0.35)
            border.color: win.palette.mid

            Label {
                anchors.centerIn: parent
                text: qsTr("Double-click to add meter")
                color: win.palette.windowText
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onDoubleClicked: meterMenu.open()
        }

        Popup {
            id: meterMenu
            x: 0
            y: -implicitHeight - win.mainControlSpacing
            modal: false
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            padding: 4

            ColumnLayout {
                spacing: 2
                Repeater {
                    model: MainController.optionalMeterOptions()
                    Button {
                        Layout.fillWidth: true
                        text: modelData.text === "" ? qsTr("None") : modelData.text
                        enabled: modelData.available
                        checkable: true
                        checked: optionalMeterSlot.meterChoice === modelData.value
                        onClicked: {
                            MainController.setOptionalMeterType(optionalMeterSlot.slot, modelData.value)
                            meterMenu.close()
                        }
                    }
                }
            }
        }

        onMeterChoiceChanged: applyExtremities()
        Connections {
            target: MainController
            function onUiSpecsChanged() { optionalMeterSlot.applyExtremities() }
        }
    }

    component AudioLevelBar: Item {
        id: audioLevelBar

        property int value: 0
        property string label: ""
        property bool active: true
        readonly property real normalizedValue: Math.max(0, Math.min(255, value)) / 255

        Layout.preferredWidth: 14
        Layout.preferredHeight: win.mainControlSliderHeight
        opacity: active ? 1.0 : 0.35

        Rectangle {
            anchors.fill: parent
            color: win.palette.base
            border.color: win.palette.mid
            radius: 2

            Rectangle {
                id: levelClip
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 2
                height: Math.max(0, parent.height - 4) * audioLevelBar.normalizedValue
                radius: 1
                color: "transparent"
                clip: true

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Math.max(0, audioLevelBar.height - 4)
                    radius: 1
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: "#d32f2f" }
                        GradientStop { position: 0.05; color: "#d32f2f" }
                        GradientStop { position: 0.20; color: "#fbc02d" }
                        GradientStop { position: 0.80; color: "#2e7d32" }
                        GradientStop { position: 1.0; color: "#2e7d32" }
                    }
                }
            }
        }

        HoverHandler { id: audioLevelHover }
        ToolTip.visible: audioLevelHover.hovered
        ToolTip.text: audioLevelBar.label + ": " + audioLevelBar.value
        ToolTip.delay: 300
    }

    LoggingWindow {
        id: loggingWindow
    }

    function scheduleStartupGeometryRestore() {
        if (win.waylandPlatform) {
            win.startupGeometryPending = false
            return
        }
        startupGeometryTimer.restart()
    }

    Timer {
        id: startupGeometryTimer
        interval: 300
        repeat: false
        onTriggered: {
            if (win.waylandPlatform) {
                win.startupGeometryPending = false
                return
            }
            if (win.startupGeometryPending && win.radioConnected && win.savedStartupGeometry && win.savedStartupGeometry.valid) {
                win.applyWindowGeometry(win.savedStartupGeometry)
                win.startupGeometryPending = false
            }
        }
    }

    Connections {
        target: MainController
        function onReceiverCountChanged() {
            if (win.startupGeometryPending)
                win.scheduleStartupGeometryRestore()
        }
    }

    onRadioConnectedChanged: {
        if (win.waylandPlatform) {
            win.wasRadioConnected = win.radioConnected
            return
        }
        if (win.radioConnected) {
            win.restoreConnectedWindowGeometry()
        } else {
            if (win.wasRadioConnected)
                win.rememberConnectedWindowGeometry()
            win.connectedGeometryRestored = false
            win.startupGeometryPending = Boolean(win.savedStartupGeometry && win.savedStartupGeometry.valid)
            Qt.callLater(win.shrinkDisconnectedWindow)
        }
        win.wasRadioConnected = win.radioConnected
    }

    Dialog {
        id: unsavedSettingsDialog
        title: qsTr("Unsaved settings")
        width: 420
        height: 150
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        visible: false
        modal: true
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

        function showDialog() {
            win.ensureWindowFitsPopup(unsavedSettingsDialog)
            open()
            forceActiveFocus()
        }

        onClosed: win.shrinkAfterDialog()
        onRejected: close()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            Label {
                Layout.fillWidth: true
                text: qsTr("Settings have changed since the last save.")
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                Button {
                    text: qsTr("Save")
                    onClicked: {
                        unsavedSettingsDialog.visible = false
                        win.finishQuit(true)
                    }
                }
                Button {
                    text: qsTr("Discard")
                    onClicked: {
                        unsavedSettingsDialog.visible = false
                        win.finishQuit(false)
                    }
                }
                Button {
                    text: qsTr("Cancel")
                    onClicked: unsavedSettingsDialog.visible = false
                }
            }
        }
    }


    onClosing: function(close) {
        if (win.shutdownStarted) {
            close.accepted = true
            return
        }

        if (!win.quitConfirmed && shouldConfirmUnsavedSettings()) {
            close.accepted = false
            unsavedSettingsDialog.showDialog()
            return
        }

        close.accepted = false
        shutdownAndQuit()
    }

    FirstTimeSetup {
        id: firstTimeSetup
        visible: false

        onExitProgram: {
            Qt.quit()
        }

        onShowSettings: function(isNetwork) {
            showSettingsWindow()
            win.visible = true
        }

        onSkipSetup: {
            win.visible = true
        }
    }

    Loader {
        id: settingsLoader
        active: false
        sourceComponent: Component {
            Settings {
                controller: MainController.settings
            }
        }
    }

    // Create memories window on demand
    Loader {
        id: memoriesLoader
        active: false
        sourceComponent: Component {
            Memories {
                memoriesModel: MainController.memoriesModel
            }
        }
    }

    Loader {
        id: selectRadioLoader
        active: false
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
        active: false
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

    function showDebugWindow() {
        if (!debugLoader.active) {
            debugLoader.active = true
        } else {
            debugLoader.item.visible = true
            debugLoader.item.raise()
            debugLoader.item.requestActivate()
        }
    }

    function showSettingsWindow() {
        if (!settingsLoader.active)
            settingsLoader.active = true
        if (settingsLoader.item)
            settingsLoader.item.showOnScreen(win)
    }

    function showSelectRadioWindow() {
        if (!selectRadioLoader.active)
            selectRadioLoader.active = true
        MainController.showSelectRadio()
        Qt.callLater(function() {
            if (selectRadioLoader.item) {
                selectRadioLoader.item.raise()
                selectRadioLoader.item.requestActivate()
            }
        })
    }

    function closeTopWindow() {
        if (settingsLoader.item && settingsLoader.item.visible && settingsLoader.item.active) {
            settingsLoader.item.close()
            return
        }
        if (memoriesLoader.item && memoriesLoader.item.visible && memoriesLoader.item.active) {
            memoriesLoader.item.close()
            return
        }
        if (selectRadioLoader.item && selectRadioLoader.item.visible && selectRadioLoader.item.active) {
            MainController.selectRadio.visible = false
            return
        }
        if (cwSenderLoader.item && cwSenderLoader.item.visible && cwSenderLoader.item.active) {
            MainController.cwSender.visible = false
            return
        }
        if (debugLoader.item && debugLoader.item.visible && debugLoader.item.active) {
            debugLoader.item.close()
            return
        }
        for (var i = 0; i < receiversRepeater.count; ++i) {
            var row = receiversRepeater.itemAt(i)
            if (row && MainController.isReceiverDetached(i) && row.detachedWindowActive) {
                MainController.setReceiverDetached(i, false)
                return
            }
        }
        if (win.active)
            win.close()
    }

    function openMemoriesWindow() {
        if (!win.radioConnected || !MainController.canOpenMemories())
            return
        MainController.openMemories()
        if (!memoriesLoader.active)
            memoriesLoader.active = true
        Qt.callLater(function() {
            if (memoriesLoader.item) {
                memoriesLoader.item.visible = true
                memoriesLoader.item.raise()
                memoriesLoader.item.requestActivate()
            }
        })
    }

    function openCwSenderWindow() {
        if (!(mainControlSpecs.canSendCW ?? false))
            return
        if (!cwSenderLoader.active)
            cwSenderLoader.active = true
        MainController.showCWSender()
        Qt.callLater(function() {
            if (cwSenderLoader.item) {
                cwSenderLoader.item.raise()
                cwSenderLoader.item.requestActivate()
            }
        })
    }

    function popoutReceiver(receiver) {
        if (!win.radioConnected)
            return
        var i = Number(receiver)
        var row = i >= 0 && i < receiversRepeater.count ? receiversRepeater.itemAt(i) : null
        if (row && row.receiverController && row.receiverController.active && !MainController.isReceiverDetached(i))
            MainController.setReceiverDetached(i, true)
    }

    function popinReceiver(receiver) {
        var i = Number(receiver)
        if (i >= 0 && i < receiversRepeater.count && MainController.isReceiverDetached(i))
            MainController.setReceiverDetached(i, false)
    }

    function runConfiguredAppShortcut(commandName, receiver) {
        const command = String(commandName)
        if (command === "app.debug") {
            showDebugWindow()
            return
        }
        if (command === "app.raiseMainWindow") {
            win.show()
            win.raise()
            win.requestActivate()
            return
        }
        if (command === "app.openSettings") {
            showSettingsWindow()
            return
        }
        if (command === "app.toggleFullscreen") {
            win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
            return
        }
        if (command === "app.closeWindow") {
            closeTopWindow()
            return
        }
        if (command === "app.openMemories") {
            openMemoriesWindow()
            return
        }
        if (command === "app.openCwSender") {
            openCwSenderWindow()
            return
        }
        if (command === "app.popoutReceiver") {
            popoutReceiver(receiver)
            return
        }
        if (command === "app.popinReceiver") {
            popinReceiver(receiver)
            return
        }
        if (command.indexOf("app.") === 0) {
            MainController.runShortcutAppAction(command)
            return
        }
    }

    Connections {
        target: MainController
        function onAppShortcutActivated(commandName, receiver) {
            win.runConfiguredAppShortcut(commandName, receiver)
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

    function settingAsBool(value, fallback) {
        if (typeof value === "boolean")
            return value
        if (typeof value === "number")
            return value !== 0
        if (typeof value === "string") {
            const s = value.trim().toLowerCase()
            if (s === "true" || s === "1" || s === "yes" || s === "on")
                return true
            if (s === "false" || s === "0" || s === "no" || s === "off" || s === "")
                return false
        }
        return fallback === undefined ? false : settingAsBool(fallback, false)
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

    function controlAvailable(name) {
        return Boolean(mainControlSpecs[name] ?? false)
    }

    function rangeControlVisible(name) {
        var spec = mainControlSpecs[name]
        return Boolean(spec && (spec.available ?? false) && (spec.visible ?? true))
    }

    function anyControlVisible(names) {
        for (var i = 0; i < names.length; ++i) {
            if (controlAvailable(names[i]))
                return true
        }
        return false
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

    function shouldConfirmUnsavedSettings() {
        if (!MainController || !MainController.settings)
            return false

        var confirm = MainController.settings.options["Interface.ConfirmSettingsChanged"]
        return win.settingAsBool(MainController.settings.dirty, false)
                && (confirm === undefined || win.settingAsBool(confirm, true))
    }

    function applyWindowGeometry(g) {
        if (g.valid) {
            if (!win.waylandPlatform) {
                win.x = g.x
                win.y = g.y
            }
            win.width = Math.max(g.width, win.minimumWidth)
            win.height = Math.max(g.height, win.minimumHeight)
            if (g.maximized)
                win.visibility = Window.Maximized
        }
    }

    function rememberConnectedWindowGeometry() {
        win.lastConnectedGeometry = {
            valid: true,
            x: win.x,
            y: win.y,
            width: win.width,
            height: win.height,
            maximized: win.visibility === Window.Maximized
        }
    }

    property bool dialogExpandedWindow: false

    function ensureWindowFitsPopup(popup) {
        var pad = 40
        var needW = popup.width + pad
        var needH = popup.height + pad
        if (win.width < needW || win.height < needH) {
            win.dialogExpandedWindow = true
            win.width  = Math.max(win.width,  needW)
            win.height = Math.max(win.height, needH)
        }
    }

    function shrinkAfterDialog() {
        if (win.dialogExpandedWindow) {
            win.dialogExpandedWindow = false
            if (!win.radioConnected)
                shrinkDisconnectedWindow()
        }
    }

    function shrinkDisconnectedWindow() {
        if (win.radioConnected || win.waylandPlatform)
            return
        if (win.visibility === Window.Maximized || win.visibility === Window.FullScreen)
            win.visibility = Window.Windowed
        win.width = win.minimumWidth
        win.height = win.minimumHeight
    }

    function restoreConnectedWindowGeometry() {
        if (win.waylandPlatform) {
            win.connectedGeometryRestored = true
            win.startupGeometryPending = false
            return
        }
        if (win.connectedGeometryRestored)
            return

        var g = win.lastConnectedGeometry && win.lastConnectedGeometry.valid ? win.lastConnectedGeometry
                                                                             : win.savedStartupGeometry
        win.connectedGeometryRestored = true
        if (g && g.valid) {
            win.savedStartupGeometry = g
            win.startupGeometryPending = true
            win.scheduleStartupGeometryRestore()
        }
    }

    function restoreWindowGeometry() {
        if (win.waylandPlatform) {
            win.visible = true
            win.connectedGeometryRestored = true
            win.startupGeometryPending = false
            win.wasRadioConnected = win.radioConnected
            return
        }

        var g = MainController.restoredMainWindowGeometry()
        win.savedStartupGeometry = g
        win.visible = true
        if (win.radioConnected)
            win.restoreConnectedWindowGeometry()
        else
            Qt.callLater(win.shrinkDisconnectedWindow)
        win.wasRadioConnected = win.radioConnected
    }

    function saveWindowGeometry() {
        if (!win.radioConnected || win.waylandPlatform)
            return
        MainController.saveMainWindowGeometry(win.x, win.y, win.width, win.height,
                                              win.visibility === Window.Maximized)
    }

    function requestQuit() {
        if (shouldConfirmUnsavedSettings()) {
            unsavedSettingsDialog.showDialog()
            return
        }

        win.close()
    }

    function finishQuit(saveFirst) {
        if (saveFirst && MainController && MainController.settings)
            MainController.settings.save()

        win.quitConfirmed = true
        win.close()
    }

    function shutdownAndQuit() {
        if (win.shutdownStarted)
            return

        win.shutdownStarted = true
        win.quitConfirmed = true
        saveWindowGeometry()
        unsavedSettingsDialog.visible = false
        if (settingsLoader.item)
            settingsLoader.item.close()
        if (memoriesLoader.item) {
            memoriesLoader.item.forceClose = true
            memoriesLoader.item.close()
        }
        if (selectRadioLoader.item) {
            MainController.selectRadio.visible = false
            selectRadioLoader.item.close()
        }
        if (cwSenderLoader.item) {
            MainController.cwSender.visible = false
            cwSenderLoader.item.close()
        }
        for (var i = 0; i < rigCreatorWindows.length; ++i) {
            if (rigCreatorWindows[i]) {
                rigCreatorWindows[i].forceClose = true
                rigCreatorWindows[i].close()
            }
        }
        if (txAudioProcessingWindow)
            txAudioProcessingWindow.close()
        if (rxAudioProcessingWindow)
            rxAudioProcessingWindow.close()

        Qt.callLater(function() {
            MainController.quitApplication()
        })
    }

    Component {
            id: rigCreatorComponent
            RigCreator { }
    }

    Component {
        id: txAudioProcessingComponent
        AudioProcessing {
            mode: "tx"
            controller: MainController.settings
            onClosing: function(close) {
                win.txAudioProcessingWindow = null
                destroy()
            }
        }
    }

    Component {
        id: rxAudioProcessingComponent
        AudioProcessing {
            mode: "rx"
            controller: MainController.settings
            onClosing: function(close) {
                win.rxAudioProcessingWindow = null
                destroy()
            }
        }
    }

    function showTxAudioProcessing() {
        MainController.ensureAudioProcessors()
        if (!win.txAudioProcessingWindow)
            win.txAudioProcessingWindow = txAudioProcessingComponent.createObject(null)
        if (win.txAudioProcessingWindow) {
            win.txAudioProcessingWindow.show()
            win.txAudioProcessingWindow.raise()
            win.txAudioProcessingWindow.requestActivate()
        }
    }

    function showRxAudioProcessing() {
        MainController.ensureAudioProcessors()
        if (!win.rxAudioProcessingWindow)
            win.rxAudioProcessingWindow = rxAudioProcessingComponent.createObject(null)
        if (win.rxAudioProcessingWindow) {
            win.rxAudioProcessingWindow.show()
            win.rxAudioProcessingWindow.raise()
            win.rxAudioProcessingWindow.requestActivate()
        }
    }

    ColumnLayout {
        id: mainLayout
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: win.contentTopPadding
        anchors.leftMargin: win.contentHorizontalPadding
        anchors.rightMargin: win.contentHorizontalPadding
        spacing: 6

        // -------- 1) scopeVFOGroup (top) --------
        Frame {
            id: scopeVFOGroup
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: win.radioConnected
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
                        property bool detached: MainController.isReceiverDetached(index)
                        property bool detachedWindowActive: false
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
                                rxLoader.item.parent = row.detached ? detachedHost : attachedHost
                                rxLoader.item.anchors.fill = row.detached ? detachedHost : attachedHost
                                rxLoader.item.anchors.margins = 1
                                detachedWin.applyWindowMode()
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
                            function onFullScreenRequested(enabled) {
                                detachedWin.fullScreen = enabled
                                MainController.settings.saveReceiverSetting(index, "DetachedFullScreen", enabled)
                                if (enabled && !MainController.isReceiverDetached(index))
                                    MainController.setReceiverDetached(index, true)
                                detachedWin.applyWindowMode()
                            }
                            function onSpectrumProcessingTime(ms) {
                                if (index === 0)
                                    MainController.selectRadio.spectrumTime(ms)
                            }
                            function onWaterfallProcessingTime(ms) {
                                if (index === 0)
                                    MainController.selectRadio.waterfallTime(ms)
                            }
                        }

                        Connections {
                            target: MainController
                            function onReceiverDetachedChanged(i, d) {
                                if (i !== index) return
                                row.detached = d
                                detachedWin.applyWindowMode()
                            }
                        }

                        ApplicationWindow {
                            id: detachedWin
                            transientParent: null
                            title: qsTr("Receiver %1").arg(index + 1)
                            width: Number(MainController.settings.receiverSetting(index, "DetachedWidth", 900))
                            height: Number(MainController.settings.receiverSetting(index, "DetachedHeight", 500))
                            color: palette.window
                            property bool restoringGeometry: true
                            property bool fullScreen: win.settingAsBool(MainController.settings.receiverSetting(index, "DetachedFullScreen", false), false)
                            onActiveChanged: row.detachedWindowActive = active

                            function saveDetachedGeometry() {
                                if (restoringGeometry || !row.detached || fullScreen)
                                    return
                                MainController.settings.saveReceiverSetting(index, "DetachedX", Math.round(x))
                                MainController.settings.saveReceiverSetting(index, "DetachedY", Math.round(y))
                                MainController.settings.saveReceiverSetting(index, "DetachedWidth", Math.round(width))
                                MainController.settings.saveReceiverSetting(index, "DetachedHeight", Math.round(height))
                            }

                            function applyWindowMode() {
                                visibility = row.detached ? (fullScreen ? Window.FullScreen : Window.Windowed) : Window.Hidden
                            }

                            function applyDisabledPalette() {
                                try {
                                    palette.disabled.window = MainController.settings.options["Color.Window"]
                                    palette.disabled.windowText = Qt.darker(MainController.settings.options["Color.WindowText"], 2.5)
                                    palette.disabled.base = Qt.darker(MainController.settings.options["Color.Base"], 1.1)
                                    palette.disabled.alternateBase = Qt.darker(MainController.settings.options["Color.AlternateBase"], 1.1)
                                    palette.disabled.text = Qt.darker(MainController.settings.options["Color.MainText"], 2.5)
                                    palette.disabled.button = Qt.darker(MainController.settings.options["Color.Button"], 1.3)
                                    palette.disabled.buttonText = Qt.darker(MainController.settings.options["Color.ButtonText"], 2.5)
                                    palette.disabled.brightText = Qt.darker(MainController.settings.options["Color.BrightText"], 2.5)
                                    palette.disabled.highlight = Qt.darker(MainController.settings.options["Color.Highlight"], 1.5)
                                    palette.disabled.highlightedText = Qt.darker(MainController.settings.options["Color.MainText"], 2.5)
                                    palette.disabled.mid = MainController.settings.options["Color.Mid"]
                                    palette.disabled.dark = MainController.settings.options["Color.Dark"]
                                    palette.disabled.light = Qt.darker(MainController.settings.options["Color.Light"], 1.2)
                                    palette.disabled.placeholderText = Qt.darker(MainController.settings.options["Color.PlaceholderText"], 1.5)
                                } catch (e) {
                                    // Qt 5 does not expose palette disabled groups to QML.
                                }
                            }

                            palette {
                                window: MainController.settings.options["Color.Window"]
                                windowText: MainController.settings.options["Color.WindowText"]
                                base: MainController.settings.options["Color.Base"]
                                alternateBase: MainController.settings.options["Color.AlternateBase"]
                                text: MainController.settings.options["Color.MainText"]
                                button: MainController.settings.options["Color.Button"]
                                buttonText: MainController.settings.options["Color.ButtonText"]
                                brightText: MainController.settings.options["Color.BrightText"]
                                highlight: MainController.settings.options["Color.Highlight"]
                                highlightedText: MainController.settings.options["Color.HighlightedText"]
                                mid: MainController.settings.options["Color.Mid"]
                                dark: MainController.settings.options["Color.Dark"]
                                light: MainController.settings.options["Color.Light"]

                                disabled {
                                    window: MainController.settings.options["Color.Window"]
                                    windowText: MainController.settings.options["Color.Mid"]
                                    base: MainController.settings.options["Color.Base"]
                                    alternateBase: MainController.settings.options["Color.AlternateBase"]
                                    text: MainController.settings.options["Color.Mid"]
                                    button: MainController.settings.options["Color.Button"]
                                    buttonText: MainController.settings.options["Color.Mid"]
                                    brightText: MainController.settings.options["Color.Mid"]
                                    highlight: MainController.settings.options["Color.Dark"]
                                    highlightedText: MainController.settings.options["Color.Mid"]
                                    mid: MainController.settings.options["Color.Mid"]
                                    dark: MainController.settings.options["Color.Dark"]
                                    light: MainController.settings.options["Color.Light"]
                                }
                            }

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
                                Qt.callLater(detachedWin.applyDisabledPalette)
                                Qt.callLater(detachedWin.applyWindowMode)

                                if (visible && row.havePendingPos) {
                                    Qt.callLater(function() {
                                        if (!win.waylandPlatform) {
                                            detachedWin.x = row.pendingX
                                            detachedWin.y = row.pendingY
                                        }
                                        row.havePendingPos = false
                                    })
                                }
                            }

                            Component.onCompleted: {
                                if (!win.waylandPlatform) {
                                    x = Number(MainController.settings.receiverSetting(index, "DetachedX", x))
                                    y = Number(MainController.settings.receiverSetting(index, "DetachedY", y))
                                }
                                restoringGeometry = false
                                applyDisabledPalette()
                                Qt.callLater(detachedWin.applyWindowMode)
                            }

                            Connections {
                                target: MainController
                                function onColChanged(items) {
                                    detachedWin.applyDisabledPalette()
                                }
                            }

                            onXChanged: saveDetachedGeometry()
                            onYChanged: saveDetachedGeometry()
                            onWidthChanged: saveDetachedGeometry()
                            onHeightChanged: saveDetachedGeometry()

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
            Layout.preferredHeight: Math.max(leftControlsColumn.implicitHeight, mainControlsFlow.implicitHeight)
            padding: 3

            RowLayout {
                anchors.fill: parent
                spacing: win.mainControlSpacing

                ColumnLayout {
                    id: leftControlsColumn
                    Layout.preferredWidth: 320
                    Layout.maximumWidth: 320
                    Layout.fillHeight: true
                    spacing: win.mainControlSpacing

                    ColumnLayout {
                        spacing: 1
                        visible: win.radioConnected

                        Repeater {
                            model: [2, 3]
                            OptionalMeterSlot {
                                slot: modelData
                                width: win.optionalMeterWidth
                                height: win.optionalMeterHeight
                                Layout.preferredWidth: win.optionalMeterWidth
                                Layout.preferredHeight: win.optionalMeterHeight
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }

                    RowLayout {
                        id: powerButtonsRow
                        spacing: win.mainControlSpacing
                        Button { text: qsTr("Power On"); onClicked: MainController.powerOn() }
                        Button { text: qsTr("Power Off"); onClicked: MainController.powerOff() }
                    }
                }

                Flow {
                    id: mainControlsFlow
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: win.width >= 1500 ? 12 : (win.width >= 1200 ? 8 : win.mainControlSpacing)

                // ---- tuningLayout ----
                ColumnLayout {
                    width: win.mainControlDialSize
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: win.mainControlDialSize
                    Layout.maximumWidth: win.mainControlDialSize
                    spacing: win.mainControlSpacing
                    visible: win.radioConnected

                    Dial {
                        id: freqDial
                        property real lastValue: value
                        property bool syncing: false
                        property double lastMoveTime: 0
                        property int lastDirection: 0
                        property int fastMoveCount: 0
                        Layout.preferredWidth: win.mainControlDialSize
                        Layout.preferredHeight: win.mainControlDialSize
                        from: 3000
                        to: 4000
                        stepSize: 10
                        wrap: true
                        visible: win.radioConnected
                        enabled: firstReceiver !== null
                        onMoved: {
                            if (syncing || !firstReceiver)
                                return

                            const fullSweep = to - from
                            var delta = value - lastValue
                            if (delta > fullSweep / 2)
                                delta -= fullSweep
                            else if (delta < -fullSweep / 2)
                                delta += fullSweep

                            var direction = 0
                            if (Math.abs(delta) < stepSize)
                                direction = delta > 0 ? 1 : -1
                            else
                                direction = Math.round(delta / stepSize) > 0 ? 1 : -1

                            if (direction !== 0) {
                                const now = Date.now()
                                const elapsed = lastMoveTime > 0 ? Math.max(1, now - lastMoveTime) : 0
                                var multiplier = 1

                                if (direction === lastDirection && elapsed > 0 && elapsed <= 90)
                                    fastMoveCount += 1
                                else
                                    fastMoveCount = 0

                                if (fastMoveCount >= 5 && elapsed <= 35)
                                    multiplier = 10
                                else if (fastMoveCount >= 3 && elapsed <= 60)
                                    multiplier = 5
                                else if (fastMoveCount >= 2 && elapsed <= 90)
                                    multiplier = 2

                                firstReceiver.tuneSteps(direction * multiplier, 0, true)
                                lastDirection = direction
                                lastMoveTime = now
                            }

                            lastValue = value
                        }
                        Component.onCompleted: {
                            lastValue = value
                            lastMoveTime = 0
                            lastDirection = 0
                            fastMoveCount = 0
                        }
                    }
                }

                Frame {
                    id: tuningControlsGroup
                    width: 120
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 120
                    Layout.maximumWidth: 120
                    padding: 3
                    visible: win.radioConnected

                    contentItem: ColumnLayout {
                        spacing: 4

                        ComboBox {
                            id: tuningStepCombo
                            Layout.fillWidth: true
                            readonly property var spec: MainController ? MainController.uiSpecs["tuningSteps"] : null
                            model: spec ? spec.model : []
                            textRole: spec ? spec.textRole : "text"
                            valueRole: spec ? spec.valueRole : "value"
                            visible: win.radioConnected && (spec ? (spec.visible ?? true) : false)
                            currentIndex: MainController ? indexFromValue(tuningStepCombo, MainController.stepSize) : -1
                            onActivated: MainController.stepSize = currentValue
                        }

                        CheckBox {
                            id: tuneLockChk
                            text: qsTr("F Lock")
                            visible: win.radioConnected
                            onToggled: MainController.setFrequencyLock(checked)
                        }

                        RowLayout {
                            id: ritControlsRow
                            visible: win.radioConnected && (mainControlSpecs.canRit ?? false)

                            Rectangle {
                                Layout.preferredWidth: win.ritDialSize + 8
                                Layout.preferredHeight: win.ritDialSize + 8
                                radius: width / 2
                                color: Qt.rgba(win.palette.button.r, win.palette.button.g, win.palette.button.b, 0.35)
                                border.color: win.palette.mid

                                Dial {
                                    id: ritTuneDial
                                    anchors.centerIn: parent
                                    width: win.ritDialSize
                                    height: win.ritDialSize
                                    readonly property var spec: controlSpec("ritFrequency", -500, 500)
                                    from: spec.min
                                    to: spec.max
                                    value: MainController.ritFrequency
                                    enabled: (mainControlSpecs.canRit ?? false) && MainController.ritEnabled
                                    stepSize: 10
                                    wrap: false
                                    onMoved: MainController.ritFrequency = Math.round(value)

                                    HoverHandler { id: hoverRitTune }
                                    ToolTip.visible: hoverRitTune.hovered
                                    ToolTip.text: qsTr("RIT") + ": " + Math.round(value).toString() + " Hz"
                                    ToolTip.delay: 300
                                }
                            }
                            CheckBox {
                                id: ritEnableChk
                                text: qsTr("RIT")
                                checked: MainController.ritEnabled
                                enabled: mainControlSpecs.canRit ?? false
                                onToggled: MainController.ritEnabled = checked
                            }
                        }
                    }
                }

                // ---- levelsHorizontalLayout (RF/AF/SQ/Mic/TX/Mon sliders) + OtherControls ----
                ColumnLayout {
                    id: levelsControlsGroup
                    width: implicitWidth
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: false
                    Layout.preferredWidth: implicitWidth
                    spacing: win.mainControlSpacing
                    visible: win.radioConnected

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            visible: win.radioConnected && MainController.localAudioAvailable
                            enabled: MainController.localAudioAvailable
                            RowLayout {
                                Layout.preferredHeight: win.mainControlSliderHeight
                                spacing: 4

                                Slider {
                                    id: volumeSlider
                                    from: 0
                                    to: 255
                                    value: MainController.localAfGain
                                    enabled: MainController.localAudioAvailable
                                    orientation: Qt.Vertical
                                    Layout.preferredHeight: win.mainControlSliderHeight
                                    onMoved: MainController.localAfGain = Math.round(value)

                                    HoverHandler { id: hoverVolume }
                                    ToolTip.visible: hoverVolume.hovered
                                    ToolTip.text: Math.round((value - from) / Math.max(1, to - from) * 100).toString() + " %"
                                    ToolTip.delay: 300
                                }

                                AudioLevelBar {
                                    value: MainController.rxAudioLevel
                                    label: qsTr("RX audio")
                                    active: MainController.localAudioAvailable
                                }
                            }
                            Label { text: qsTr("Vol"); horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            visible: rangeControlVisible("txPower")
                            Slider {
                                id: txPowerSlider
                                readonly property var spec: controlSpec("txPower", 0, 255)
                                from: spec.min
                                to: spec.max
                                value: MainController.txPower
                                enabled: spec.available ?? false
                                orientation: Qt.Vertical
                                Layout.preferredHeight: win.mainControlSliderHeight
                                onMoved: MainController.txPower = Math.round(value)

                                HoverHandler { id: hoverTxPower }
                                ToolTip.visible: hoverTxPower.hovered
                                ToolTip.text: Math.round((value - from) / Math.max(1, to - from) * 100).toString() + " %"
                                ToolTip.delay: 300
                            }
                            Label { text: qsTr("TX"); horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            visible: rangeControlVisible("monitorGain") || (mainControlSpecs.canMonitor ?? false)
                            Slider {
                                id: monitorSlider
                                readonly property var spec: controlSpec("monitorGain", 0, 255)
                                from: spec.min
                                to: spec.max
                                value: MainController.monitorGain
                                enabled: spec.available ?? false
                                orientation: Qt.Vertical
                                Layout.preferredHeight: win.mainControlSliderHeight
                                onMoved: MainController.monitorGain = Math.round(value)

                                HoverHandler { id: hoverMonitor }
                                ToolTip.visible: hoverMonitor.hovered
                                ToolTip.text: Math.round((value - from) / Math.max(1, to - from) * 100).toString() + " %"
                                ToolTip.delay: 300
                            }
                            Label {
                                id: monitorLabel
                                text: qsTr("Mon")
                                horizontalAlignment: Text.AlignHCenter
                                enabled: mainControlSpecs.canMonitor ?? false
                                font.bold: MainController.monitorEnabled
                                font.strikeout: !MainController.monitorEnabled
                                opacity: enabled ? 1.0 : 0.45

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: monitorLabel.enabled
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: MainController.monitorEnabled = !MainController.monitorEnabled
                                }
                            }
                        }
                        ColumnLayout {
                            visible: rangeControlVisible("micGain")
                            RowLayout {
                                Layout.preferredHeight: win.mainControlSliderHeight
                                spacing: 4

                                Slider {
                                    id: micGainSlider
                                    readonly property var spec: controlSpec("micGain", 0, 255)
                                    from: MainController.modGainMin
                                    to: MainController.modGainMax
                                    value: MainController.micGain
                                    enabled: spec.available ?? false
                                    orientation: Qt.Vertical
                                    Layout.preferredHeight: win.mainControlSliderHeight
                                    onMoved: MainController.micGain = Math.round(value)

                                    HoverHandler { id: hoverMicGain }
                                    ToolTip.visible: hoverMicGain.hovered
                                    ToolTip.text: Math.round((value - from) / Math.max(1, to - from) * 100).toString() + " %"
                                    ToolTip.delay: 300
                                }

                                AudioLevelBar {
                                    value: MainController.txAudioLevel
                                    label: qsTr("TX audio")
                                    active: micGainSlider.enabled
                                }
                            }
                            Label { text: MainController.modGainLabel; horizontalAlignment: Text.AlignHCenter }
                        }
                    }
                }

                // ---- command buttons (Transmit, ATU, etc.) ----
                GridLayout {
                    id: commandButtonsColumn
                    readonly property bool twoColumns: mainControlsFlow.width >= 900
                    columns: twoColumns ? 2 : 1
                    width: twoColumns ? 246 : 120
                    rowSpacing: win.mainControlSpacing
                    columnSpacing: win.mainControlSpacing
                    visible: win.radioConnected

                    Button {
                        text: MainController.transmitting ? qsTr("Receive") : qsTr("Transmit")
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 50
                        checkable: true
                        checked: MainController.transmitting
                        enabled: mainControlSpecs.canTransmit ?? false
                        visible: mainControlSpecs.canTransmit ?? false
                        onClicked: MainController.toggleTransmit()
                    }
                    CheckBox {
                        text: qsTr("Enable ATU")
                        Layout.preferredWidth: 120
                        checked: MainController.tunerEnabled
                        enabled: mainControlSpecs.canTune ?? false
                        visible: mainControlSpecs.canTune ?? false
                        onToggled: MainController.tunerEnabled = checked
                    }
                    Button {
                        text: qsTr("Tune")
                        Layout.preferredWidth: 120
                        enabled: mainControlSpecs.canTune ?? false
                        visible: mainControlSpecs.canTune ?? false
                        onClicked: MainController.tuneNow()
                    }
                    Button {
                        text: qsTr("CW")
                        Layout.preferredWidth: 120
                        enabled: mainControlSpecs.canSendCW ?? false
                        visible: mainControlSpecs.canSendCW ?? false
                        onClicked: win.openCwSenderWindow()
                    }

                    Button {
                        text: qsTr("Rpt/Split")
                        Layout.preferredWidth: 120
                        enabled: false
                        visible: false
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Repeater setup has not been ported to QML yet.")
                    }
                    Button {
                        text: qsTr("Memories")
                        Layout.preferredWidth: 120
                        visible: win.radioConnected && (mainControlSpecs.canMemories ?? false)
                        enabled: win.radioConnected && (mainControlSpecs.canMemories ?? false)
                        onClicked: win.openMemoriesWindow()
                    }
                }

                Flow {
                    id: scopeAndOtherControlsFlow
                    readonly property bool hasReceiverScopeControls: (mainControlSpecs.canDualScope ?? false)
                                                                   || (mainControlSpecs.canDualWatch ?? false)
                                                                   || (mainControlSpecs.canMainSub ?? false)
                                                                   || (mainControlSpecs.canSwapMainSub ?? false)
                                                                   || (mainControlSpecs.canEqualMainSub ?? false)
                    readonly property bool showScopeControls: win.radioConnected && hasReceiverScopeControls
                    readonly property bool showOtherControls: win.radioConnected && anyControlVisible(["canCompressor", "canVox"])
                    readonly property int scopePaneWidth: mainControlsFlow.width >= 1650 ? 640 : 320
                    readonly property int otherPaneWidth: 165
                    readonly property int rowWidthBeforeScope: (freqDial.visible ? win.mainControlDialSize : 0)
                                                            + (tuningControlsGroup.visible ? tuningControlsGroup.width : 0)
                                                            + (levelsControlsGroup.visible ? levelsControlsGroup.implicitWidth : 0)
                                                            + (commandButtonsColumn.visible ? commandButtonsColumn.width : 0)
                                                            + (4 * mainControlsFlow.spacing)
                    readonly property bool scopeFitsOnCurrentRow: mainControlsFlow.width >= (rowWidthBeforeScope + scopePaneWidth)
                    readonly property bool otherFitsBesideScopeOnCurrentRow: mainControlsFlow.width >= (rowWidthBeforeScope + scopePaneWidth + otherPaneWidth + spacing)
                    readonly property bool otherFitsBesideScopeOnWrappedRow: !scopeFitsOnCurrentRow
                                                                             && mainControlsFlow.width >= (scopePaneWidth + otherPaneWidth + spacing)
                    readonly property bool canPlaceOtherBesideScope: showScopeControls
                                                                     && showOtherControls
                                                                     && (otherFitsBesideScopeOnCurrentRow || otherFitsBesideScopeOnWrappedRow)
                    width: showScopeControls
                           ? (canPlaceOtherBesideScope ? scopePaneWidth + otherPaneWidth + spacing : scopePaneWidth)
                           : otherPaneWidth
                    height: implicitHeight
                    spacing: win.mainControlSpacing
                    visible: showScopeControls || showOtherControls

                    GroupBox {
                        id: scopeSettingsGroup
                        title: qsTr("Scope Settings")
                        width: scopeAndOtherControlsFlow.scopePaneWidth
                        visible: scopeAndOtherControlsFlow.showScopeControls

                        contentItem: GridLayout {
                            columns: scopeAndOtherControlsFlow.scopePaneWidth >= 600 ? 6 : 3
                            rowSpacing: scopeAndOtherControlsFlow.scopePaneWidth >= 600 ? 10 : 6
                            columnSpacing: scopeAndOtherControlsFlow.scopePaneWidth >= 600 ? 10 : 6

                            Button {
                                text: qsTr("Dual Scope")
                                checkable: true
                                checked: MainController.dualScope
                                enabled: mainControlSpecs.canDualScope ?? false
                                visible: mainControlSpecs.canDualScope ?? false
                                onToggled: MainController.dualScope = checked
                            }
                            Button {
                                text: qsTr("Dual Watch")
                                checkable: true
                                checked: MainController.dualWatch
                                enabled: mainControlSpecs.canDualWatch ?? false
                                visible: mainControlSpecs.canDualWatch ?? false
                                onToggled: MainController.dualWatch = checked
                            }
                            Button {
                                text: qsTr("Split")
                                checkable: true
                                checked: MainController.splitEnabled
                                enabled: mainControlSpecs.canSplit ?? false
                                visible: (mainControlSpecs.canSplit ?? false) && scopeAndOtherControlsFlow.hasReceiverScopeControls
                                onToggled: MainController.splitEnabled = checked
                            }

                            Button {
                                text: qsTr("Main/Sub")
                                enabled: mainControlSpecs.canMainSub ?? false
                                visible: mainControlSpecs.canMainSub ?? false
                                onClicked: MainController.selectMainSub()
                            }
                            Button {
                                text: qsTr("Main<>Sub")
                                enabled: mainControlSpecs.canSwapMainSub ?? false
                                visible: mainControlSpecs.canSwapMainSub ?? false
                                onClicked: MainController.swapMainSub()
                            }
                            Button {
                                text: qsTr("Main=Sub")
                                enabled: mainControlSpecs.canEqualMainSub ?? false
                                visible: mainControlSpecs.canEqualMainSub ?? false
                                onClicked: MainController.equalizeMainSub()
                            }
                        }
                    }

                    Frame {
                        id: otherControlsFrame
                        width: scopeAndOtherControlsFlow.otherPaneWidth
                        padding: 3
                        visible: scopeAndOtherControlsFlow.showOtherControls

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 3
                            RowLayout {
                                CheckBox {
                                    text: qsTr("CMP")
                                    checked: MainController.compressorEnabled
                                    enabled: mainControlSpecs.canCompressor ?? false
                                    visible: mainControlSpecs.canCompressor ?? false
                                    onToggled: MainController.compressorEnabled = checked
                                }
                                CheckBox {
                                    text: qsTr("VOX")
                                    checked: MainController.voxEnabled
                                    enabled: mainControlSpecs.canVox ?? false
                                    visible: mainControlSpecs.canVox ?? false
                                    onToggled: MainController.voxEnabled = checked
                                }
                            }
                        }
                    }
                }

                Item {
                    width: win.radioConnected ? 0 : 1
                    height: 1
                    visible: !win.radioConnected
                }
                }
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
                    text: qsTr("About")
                    onClicked: {
                        win.ensureWindowFitsPopup(aboutDialog)
                        aboutDialog.open()
                    }
                }
                Button {
                    text: qsTr("Settings")
                    onClicked: showSettingsWindow()
                }
                Button {
                    text: qsTr("TX Audio Proc")
                    visible: win.radioConnected
                    onClicked: showTxAudioProcessing()
                }
                Button {
                    text: qsTr("RX Audio Proc")
                    visible: win.radioConnected
                    onClicked: showRxAudioProcessing()
                }

                Button {
                    text: qsTr("Radio Status")
                    onClicked: showSelectRadioWindow()
                }
                Button {
                    text: qsTr("Log")
                    onClicked: {
                        loggingWindow.show()
                        loggingWindow.raise()
                        loggingWindow.requestActivate()
                    }
                }
                Button {
                    text: qsTr("Rig Creator")
                    onClicked: {
                        const w = rigCreatorComponent.createObject(win)
                        if (w) {
                            const windows = rigCreatorWindows.slice()
                            windows.push(w)
                            rigCreatorWindows = windows

                            if (!win.waylandPlatform) {
                                const screenRect = win.screen ? win.screen.availableGeometry : null
                                const margin = 24
                                const offset = Math.min(40 + (windows.length - 1) * 24, 160)
                                if (screenRect) {
                                    w.x = Math.max(screenRect.x + margin,
                                                   Math.min(win.x + offset,
                                                            screenRect.x + screenRect.width - w.width - margin))
                                    w.y = Math.max(screenRect.y + margin,
                                                   Math.min(win.y + offset,
                                                            screenRect.y + screenRect.height - w.height - margin))
                                } else {
                                    w.x = Math.max(0, win.x + offset)
                                    w.y = Math.max(0, win.y + offset)
                                }
                            }

                            w.show()
                            w.requestActivate()
                        }
                    }
                }

                Item { Layout.fillWidth: true } // spacer (horizontalSpacer_32)

                Button {
                    text: (connStatus === 0) ?
                              qsTr("Connect to Radio") :
                              (connStatus === 1) ?
                              qsTr("Cancel Connection") :
                              (connStatus === 2) ?
                              qsTr("Disconnect from Radio") : qsTr("Unknown status!")
                    onClicked: MainController.connectionHandler()
                }

                Item { Layout.fillWidth: true } // spacer (horizontalSpacer_9)

                Button { text: qsTr("Exit Program"); onClicked: win.requestQuit() }
            }
        }
    }

    // If you want a QWidget-like status bar, do this:
    footer: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: win.contentHorizontalPadding
            anchors.rightMargin: win.contentHorizontalPadding
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: MainController ? MainController.footerMessageText : ""
                color: win.palette.windowText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
            }

            Label {
                visible: win.radioConnected
                Layout.preferredWidth: win.radioConnected ? 360 : 0
                text: MainController ? MainController.radioStatusText : ""
                color: win.palette.windowText
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
            }

            Label {
                visible: win.radioConnected
                Layout.preferredWidth: win.radioConnected ? 90 : 0
                text: MainController && MainController.rigModelName.length > 0 ? MainController.rigModelName : qsTr("NONE")
                color: win.palette.windowText
                font.bold: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignRight
            }
        }
    }


    Dialog {
        id: aboutDialog
        width: 700
        height: 621
        title: qsTr("About wfview")
        modal: true
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)

        // Remove default padding to let AboutBox fill the dialog
        padding: 0

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

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

        standardButtons: Dialog.Close
        onClosed: win.shrinkAfterDialog()
    }

    Component.onCompleted: {
        MainController.updateApplicationPalette();
        restoreWindowGeometry()
        // Check if this is the first run
        //if (MainController.isFirstRun()) {
        //    firstTimeSetup.visible = true
        //}
    }

}
