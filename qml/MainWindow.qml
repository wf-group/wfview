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
    property var savedStartupGeometry: null
    property var lastConnectedGeometry: null
    property bool startupGeometryPending: false
    property bool connectedGeometryRestored: false
    property bool wasRadioConnected: false
    readonly property int contentHorizontalPadding: 8
    readonly property int contentTopPadding: 4
    readonly property int mainControlSpacing: 6
    readonly property int mainControlSliderHeight: 120
    readonly property int mainControlDialSize: 80
    readonly property int optionalMeterWidth: 300
    readonly property int optionalMeterHeight: 40
    readonly property bool radioConnected: Number(MainController.connStatus) === 2
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
            reverseCompMeter: Boolean(MainController.settings.options["Interface.CompMeterReverse"])
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

    Timer {
        id: startupGeometryTimer
        interval: 300
        repeat: false
        onTriggered: {
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
                startupGeometryTimer.restart()
        }
    }

    onRadioConnectedChanged: {
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
            open()
            forceActiveFocus()
        }

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
        if (!win.quitConfirmed && shouldConfirmUnsavedSettings()) {
            close.accepted = false
            unsavedSettingsDialog.showDialog()
            return
        }

        shutdownAndQuit()
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
        return Boolean(MainController.settings.dirty) && (confirm === undefined || Boolean(confirm))
    }

    function applyWindowGeometry(g) {
        if (g.valid) {
            win.x = g.x
            win.y = g.y
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

    function shrinkDisconnectedWindow() {
        if (win.radioConnected)
            return
        if (win.visibility === Window.Maximized || win.visibility === Window.FullScreen)
            win.visibility = Window.Windowed
        win.width = win.minimumWidth
        win.height = win.minimumHeight
    }

    function restoreConnectedWindowGeometry() {
        if (win.connectedGeometryRestored)
            return

        var g = win.lastConnectedGeometry && win.lastConnectedGeometry.valid ? win.lastConnectedGeometry
                                                                             : win.savedStartupGeometry
        win.connectedGeometryRestored = true
        if (g && g.valid) {
            win.savedStartupGeometry = g
            win.startupGeometryPending = true
            startupGeometryTimer.restart()
        }
    }

    function restoreWindowGeometry() {
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
        if (!win.radioConnected)
            return
        MainController.saveMainWindowGeometry(win.x, win.y, win.width, win.height,
                                              win.visibility === Window.Maximized)
    }

    function requestQuit() {
        if (shouldConfirmUnsavedSettings()) {
            unsavedSettingsDialog.showDialog()
            return
        }

        win.quitConfirmed = true
        win.close()
    }

    function finishQuit(saveFirst) {
        if (saveFirst && MainController && MainController.settings)
            MainController.settings.save()

        win.quitConfirmed = true
        shutdownAndQuit()
        win.close()
    }

    function shutdownAndQuit() {
        saveWindowGeometry()
        unsavedSettingsDialog.visible = false
        if (settings)
            settings.visible = false
        if (txAudioProcessingWindow)
            txAudioProcessingWindow.visible = false
        if (rxAudioProcessingWindow)
            rxAudioProcessingWindow.visible = false

        MainController.quitApplication()
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
                                detachedWin.visible = row.detached
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
                                detachedWin.visible = d
                                row.detached = d
                            }
                        }

                        ApplicationWindow {
                            id: detachedWin
                            visible: row.detached
                            title: qsTr("Receiver %1").arg(index + 1)
                            width: Number(MainController.settings.receiverSetting(index, "DetachedWidth", 900))
                            height: Number(MainController.settings.receiverSetting(index, "DetachedHeight", 500))
                            color: palette.window
                            property bool restoringGeometry: true
                            property bool fullScreen: Boolean(MainController.settings.receiverSetting(index, "DetachedFullScreen", false))

                            function saveDetachedGeometry() {
                                if (restoringGeometry || !visible || fullScreen)
                                    return
                                MainController.settings.saveReceiverSetting(index, "DetachedX", Math.round(x))
                                MainController.settings.saveReceiverSetting(index, "DetachedY", Math.round(y))
                                MainController.settings.saveReceiverSetting(index, "DetachedWidth", Math.round(width))
                                MainController.settings.saveReceiverSetting(index, "DetachedHeight", Math.round(height))
                            }

                            function applyWindowMode() {
                                if (!visible)
                                    return
                                visibility = fullScreen ? Window.FullScreen : Window.Windowed
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
                                Qt.callLater(detachedWin.applyWindowMode)

                                if (visible && row.havePendingPos) {
                                    Qt.callLater(function() {
                                        detachedWin.x = row.pendingX
                                        detachedWin.y = row.pendingY
                                        row.havePendingPos = false
                                    })
                                }
                            }

                            Component.onCompleted: {
                                x = Number(MainController.settings.receiverSetting(index, "DetachedX", x))
                                y = Number(MainController.settings.receiverSetting(index, "DetachedY", y))
                                restoringGeometry = false
                                Qt.callLater(detachedWin.applyWindowMode)
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

                            var steps = 0
                            if (Math.abs(delta) < stepSize)
                                steps = delta > 0 ? 1 : -1
                            else
                                steps = Math.round(delta / stepSize)

                            if (steps !== 0) {
                                const now = Date.now()
                                const elapsed = lastMoveTime > 0 ? Math.max(1, now - lastMoveTime) : 0
                                const rate = elapsed > 0 ? Math.abs(steps) * 1000 / elapsed : 0
                                var multiplier = 1
                                if (rate >= 45)
                                    multiplier = 25
                                else if (rate >= 25)
                                    multiplier = 10
                                else if (rate >= 12)
                                    multiplier = 5
                                else if (rate >= 6)
                                    multiplier = 2

                                firstReceiver.tuneSteps(steps * multiplier, 0, true)
                                lastMoveTime = now
                            }

                            lastValue = value
                        }
                        Component.onCompleted: {
                            lastValue = value
                            lastMoveTime = 0
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
                    readonly property bool twoColumns: mainControlsFlow.width >= 1180
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
                        onClicked: MainController.showCWSender()
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
                        visible: win.radioConnected
                        enabled: win.radioConnected
                        onClicked: {
                            MainController.openMemories()
                            memoriesLoader.item.visible = true
                        }
                    }
                }

                GroupBox {
                    id: scopeSettingsGroup
                    title: qsTr("Scope Settings")
                    width: mainControlsFlow.width >= 1320 ? 640 : 320
                    readonly property bool hasReceiverScopeControls: (mainControlSpecs.canDualScope ?? false)
                                                                    || (mainControlSpecs.canDualWatch ?? false)
                                                                    || (mainControlSpecs.canMainSub ?? false)
                                                                    || (mainControlSpecs.canSwapMainSub ?? false)
                                                                    || (mainControlSpecs.canEqualMainSub ?? false)
                    visible: win.radioConnected && hasReceiverScopeControls

                    contentItem: GridLayout {
                        columns: scopeSettingsGroup.width >= 600 ? 6 : 3
                        rowSpacing: scopeSettingsGroup.width >= 600 ? 10 : 6
                        columnSpacing: scopeSettingsGroup.width >= 600 ? 10 : 6

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
                            visible: (mainControlSpecs.canSplit ?? false) && scopeSettingsGroup.hasReceiverScopeControls
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
                    width: 165
                    padding: 3
                    visible: win.radioConnected && anyControlVisible(["canCompressor", "canVox"])

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
                    onClicked: aboutDialog.open()
                }
                Button {
                    text: qsTr("Settings")
                    onClicked: settings.show()
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
                    onClicked: MainController.selectRadio.visible = true
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

        // Optional: Add a close button at the bottom
        standardButtons: Dialog.Close
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
