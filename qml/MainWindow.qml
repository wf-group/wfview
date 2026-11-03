// Main.qml
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

ApplicationWindow {
    id: win
    color: palette.window

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
    property int startupGeometryRestoreAttempts: 0
    property bool wasRadioConnected: false
    // Once a radio has been connected, the window keeps its size on disconnect
    // rather than shrinking to the logo, so the shrunken size is never saved.
    property bool hasConnectedOnce: false
    readonly property int contentHorizontalPadding: 8
    readonly property int contentTopPadding: 4
    readonly property int mainControlSpacing: 6
    readonly property int mainControlSliderHeight: 120
    readonly property int mainControlDialSize: mainControlSliderHeight
    readonly property int ritDialSize: 44
    readonly property bool showTuningDial: settingAsBool(MainController.settings.options["Interface.ShowTuningDial"], true)
    // Satellite-friendly RIT: wide 1 Hz slider plus exact entry, replacing the small RIT knob
    readonly property bool wideRitSlider: settingAsBool(MainController.settings.options["Interface.WideRitSlider"], false)
    readonly property int wideRitSliderWidth: 420
    // "Additional Meters Larger": same 1.5x size and larger font as the primary meter
    readonly property bool largeOptionalMeters: settingAsBool(MainController.settings.options["Interface.LargeOptionalMeters"], false)
    readonly property int optionalMeterWidth: largeOptionalMeters ? 450 : 300
    readonly property int optionalMeterHeight: largeOptionalMeters ? 60 : 40
    // 0 Modern (two additional meters), 1 Analog, 2 Analog Cream, 3 LCD, 4 LCD Arc
    // (one vintage meter showing Meter2Type as primary and Meter3Type as secondary)
    readonly property int meterStyle: Number(MainController.settings.options["Interface.MeterStyle"] || 0)
    // Optional meter slots showing audio (TX Mod = 10, RX Audio = 11, Audio = 12)
    // replace the vertical level bars next to the Vol / Mic sliders.
    readonly property var optionalMeterChoices: [
        Number(MainController.settings.options["Interface.Meter2Type"] || 0),
        Number(MainController.settings.options["Interface.Meter3Type"] || 0)
    ]
    readonly property bool rxAudioOnOptionalMeter: optionalMeterChoices.indexOf(11) !== -1
                                                   || optionalMeterChoices.indexOf(12) !== -1
    readonly property bool txAudioOnOptionalMeter: optionalMeterChoices.indexOf(10) !== -1
                                                   || optionalMeterChoices.indexOf(12) !== -1
    readonly property bool radioConnected: Number(MainController.connStatus) === 2
    readonly property bool waylandPlatform: String(MainController.platformName()).indexOf("wayland") !== -1
    readonly property var mainControlSpecs: MainController.uiSpecs["mainControls"] || ({})
    // The receiver the radio has selected (Main/Sub); the tuning dial follows it
    readonly property var dialReceiver: MainController.receiverCount > 0
                                        ? MainController.receiver(Math.min(MainController.currentReceiver, MainController.receiverCount - 1)) : null
    readonly property bool _sysTheme: MainController.settings.options["Interface.UseSystemTheme"] === true

    property int flowSpacing: mainControlSpacing
    property bool cmdTwoColumns: false
    readonly property var macroButtons: {
        const macros = MainController.settings ? MainController.settings.macros : []
        return macros.filter(m => m.showButton)
    }
    property int scopeFlowPaneWidth: 320
    property bool _flowLayoutInitialized: false

    Timer {
        id: flowLayoutTimer
        interval: 150
        onTriggered: win.applyFlowThresholds()
    }

    Timer {
        id: heightReleaseTimer
        interval: 300
        onTriggered: {
            mainGroup.targetHeight = Qt.binding(function() {
                return Math.max(leftControlsColumn.implicitHeight, mainControlsFlow.implicitHeight)
            })
        }
    }

    function applyFlowThresholds() {
        var flowW = mainControlsFlow.width
        var changed = false

        if (!cmdTwoColumns && flowW >= 930) {
            cmdTwoColumns = true
            changed = true
        } else if (cmdTwoColumns && flowW < 870) {
            cmdTwoColumns = false
            changed = true
        }

        if (scopeFlowPaneWidth === 320 && flowW >= 1680) {
            scopeFlowPaneWidth = 640
            changed = true
        } else if (scopeFlowPaneWidth === 640 && flowW < 1620) {
            scopeFlowPaneWidth = 320
            changed = true
        }

        if (changed) {
            mainGroup.targetHeight = mainGroup.height
            heightReleaseTimer.restart()
        }
    }

    function updateFlowLayout() {
        var w = win.width
        flowSpacing = w >= 1500 ? 12 : (w >= 1200 ? 8 : mainControlSpacing)

        if (!_flowLayoutInitialized) {
            _flowLayoutInitialized = true
            applyFlowThresholds()
        } else {
            flowLayoutTimer.restart()
        }
    }

    width: 946
    // No "height: minimumHeight" binding here: the window system resizing the
    // window does not break a QML binding on Window.height, so it stayed live
    // and snapped the window back to minimumHeight whenever the content reflowed
    // (seen on Wayland, where nothing else ever assigns height). The initial
    // height is set once in Component.onCompleted instead.
    visible: false

    minimumWidth:  radioConnected ? 360 : mainLayout.implicitWidth + contentHorizontalPadding * 2
    //minimumHeight: mainLayout.implicitHeight+30
    minimumHeight: mainLayout.implicitHeight + 8
                   + mainGroup.padding * 2
                   + scopeVFOGroup.padding * 2
                   + mainLayout.spacing * 2
    onMinimumWidthChanged: {
        if (!win.radioConnected && !win.hasConnectedOnce && !win.waylandPlatform)
            win.width = win.minimumWidth
    }
    onMinimumHeightChanged: {
        if (!win.radioConnected && !win.hasConnectedOnce && !win.waylandPlatform)
            win.height = win.minimumHeight
    }

    component OptionalMeterSlot: Item {
        id: optionalMeterSlot

        property int slot: 2
        readonly property string optionKey: slot === 2 ? "Interface.Meter2Type" : "Interface.Meter3Type"
        readonly property int meterChoice: Number(MainController.settings.options[optionKey] || 0)
        // Choice 1 is S+Power: the active receiver's primary meter (S on receive, Po on transmit).
        readonly property var activeRx: {
            const count = MainController.receiverCount
            for (let i = 0; i < count; ++i) {
                const r = MainController.receiver(i)
                if (r && r.active)
                    return r
            }
            return count > 0 ? MainController.receiver(0) : null
        }
        readonly property int effectiveType: meterChoice === 1 ? (activeRx ? Number(activeRx.meterType) : 1) : meterChoice
        readonly property double meterLevel: meterChoice === 1 ? (activeRx ? activeRx.meter : 0)
                                           : (slot === 2 ? MainController.optionalMeter2Level : MainController.optionalMeter3Level)

        width: 300
        height: 40

        function applyExtremities() {
            const ext = MainController.optionalMeterExtremities(effectiveType)
            if (ext.valid) {
                optionalMeter.setMeterExtremities(ext.low, ext.high, ext.red)
                optionalMeter.warningText = ""
            } else if (effectiveType === 1) {
                optionalMeter.setMeterExtremities(-54, 60, 0)
                optionalMeter.warningText = ""
            } else {
                optionalMeter.clearMeterExtremities()
                optionalMeter.warningText = ext.calibrationMissing ? qsTr("No calibration found") : ""
            }
        }

        Meter {
            id: optionalMeter
            anchors.fill: parent
            visible: optionalMeterSlot.meterChoice !== 0
            meterType: optionalMeterSlot.effectiveType
            current: optionalMeterSlot.meterLevel
            drawLabels: true
            fontSize: win.largeOptionalMeters ? 12 : 9
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
                text: qsTr("Double-click or right-click to add meter")
                color: win.palette.windowText
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton)
                    meterMenu.open()
            }
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

            RowLayout {
                spacing: 12

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
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

                // Same style picker as the vintage meter's menu. A vintage
                // style replaces both additional meters with one meter.
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 2

                    Label {
                        text: qsTr("Style")
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Repeater {
                        model: [
                            { text: qsTr("Modern"), value: 0 },
                            { text: qsTr("Analog"), value: 1 },
                            { text: qsTr("Analog Cream"), value: 2 },
                            { text: qsTr("LCD"), value: 3 },
                            { text: qsTr("LCD Arc"), value: 4 }
                        ]
                        Button {
                            Layout.fillWidth: true
                            text: modelData.text
                            checkable: true
                            checked: win.meterStyle === modelData.value
                            onClicked: {
                                MainController.settings.setOption("Interface.MeterStyle", modelData.value)
                                meterMenu.close()
                            }
                        }
                    }
                }
            }
        }

        onEffectiveTypeChanged: applyExtremities()
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

    MeterPopoutWindow {
        id: meterPopoutWindow
        radioConnected: win.radioConnected
        windowTextColor: win.palette.windowText
        windowColor: win.palette.window
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
        interval: 150
        repeat: false
        onTriggered: {
            if (win.waylandPlatform) {
                win.startupGeometryPending = false
                return
            }

            if (!win.startupGeometryPending || !win.radioConnected
                    || !win.savedStartupGeometry || !win.savedStartupGeometry.valid)
                return

            if (MainController.receiverCount <= 0 && win.startupGeometryRestoreAttempts < 20) {
                win.startupGeometryRestoreAttempts += 1
                win.scheduleStartupGeometryRestore()
                return
            }

            win.applyWindowGeometry(win.savedStartupGeometry)

            if (win.startupGeometryRestoreAttempts < 8) {
                win.startupGeometryRestoreAttempts += 1
                win.scheduleStartupGeometryRestore()
            } else {
                win.startupGeometryPending = false
                win.connectedGeometryRestored = true
            }
        }
    }

    Connections {
        target: MainController.settings
        function onSaved() {
            win.saveWindowGeometry()
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
        if (win.radioConnected)
            win.hasConnectedOnce = true
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
            win.startupGeometryRestoreAttempts = 0
        }
        win.wasRadioConnected = win.radioConnected
    }

    Dialog {
        id: unsavedSettingsDialog
        title: qsTr("Unsaved settings")
        width: 480
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
                    text: qsTr("Always save")
                    onClicked: {
                        unsavedSettingsDialog.visible = false
                        if (MainController && MainController.settings)
                            MainController.settings.setOption("Interface.AlwaysSaveOnExit", true)
                        win.finishQuit(true)
                    }
                }
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

    // Shown while on-disconnect commands and macros run before the radio
    // connection closes; MainController clears the text when they are done.
    Dialog {
        id: closingActionsDialog
        title: qsTr("Disconnecting")
        width: 420
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        modal: true
        closePolicy: Popup.NoAutoClose

        readonly property string actionsText: MainController.closingActionsText

        onActionsTextChanged: {
            if (actionsText !== "") {
                win.ensureWindowFitsPopup(closingActionsDialog)
                open()
            } else {
                close()
            }
        }
        onClosed: win.shrinkAfterDialog()

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 16

            BusyIndicator {
                running: closingActionsDialog.visible
                Layout.alignment: Qt.AlignTop
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Executing closing commands, please wait...")
                    color: win.palette.windowText
                    font.bold: true
                    wrapMode: Text.WordWrap
                }
                Label {
                    Layout.fillWidth: true
                    text: closingActionsDialog.actionsText
                    color: win.palette.windowText
                    wrapMode: Text.WordWrap
                }
            }
        }
    }


    Dialog {
        id: enterFrequencyDialog
        title: qsTr("Enter Frequency")
        width: 320
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        visible: false
        modal: true
        closePolicy: Popup.CloseOnEscape

        property var receiverController: null

        // Size from the layout so the buttons always fit inside the dialog.
        contentHeight: frequencyLayout.implicitHeight + 32

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

        function showDialog(receiver) {
            if (!win.radioConnected)
                return
            var i = Number(receiver)
            var rc = MainController.receiver(i >= 0 ? i : 0)
            if (!rc)
                return
            receiverController = rc
            frequencyField.text = (Number(rc.frequencyA) / 1e6).toFixed(6)
            win.show()
            win.raise()
            win.requestActivate()
            win.ensureWindowFitsPopup(enterFrequencyDialog)
            open()
        }

        // Text with a decimal point is MHz; whole numbers are kHz. An explicit
        // "MHz" or "kHz" suffix overrides that. A lone comma with no period is
        // treated as the decimal separator; otherwise commas are digit grouping.
        function parseFrequencyMHz(text) {
            var s = String(text).trim().replace(/\s+/g, "")
            var unit = ""
            var suffix = s.match(/(mhz|khz)$/i)
            if (suffix) {
                unit = suffix[1].toLowerCase()
                s = s.slice(0, s.length - suffix[1].length)
            }
            if (s.indexOf(".") < 0 && (s.match(/,/g) || []).length === 1)
                s = s.replace(",", ".")
            else
                s = s.replace(/,/g, "")
            if (!/^\d*\.?\d+$|^\d+\.$/.test(s))
                return NaN
            if (unit === "")
                unit = s.indexOf(".") < 0 ? "khz" : "mhz"
            return unit === "khz" ? Number(s) / 1000 : Number(s)
        }

        function submit() {
            var mhz = parseFrequencyMHz(frequencyField.text)
            if (!isFinite(mhz) || mhz <= 0) {
                frequencyField.selectAll()
                frequencyField.forceActiveFocus()
                return
            }
            if (receiverController)
                receiverController.setExactFrequencyMHz(mhz)
            close()
        }

        onOpened: {
            frequencyField.forceActiveFocus()
            frequencyField.selectAll()
        }
        onClosed: {
            receiverController = null
            win.shrinkAfterDialog()
        }

        ColumnLayout {
            id: frequencyLayout
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("Enter desired frequency")
                color: win.palette.windowText
            }

            TextField {
                id: frequencyField
                Layout.fillWidth: true
                selectByMouse: true
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onAccepted: enterFrequencyDialog.submit()
                Keys.onEscapePressed: enterFrequencyDialog.close()
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                Button {
                    text: qsTr("Cancel")
                    onClicked: enterFrequencyDialog.close()
                }
                Button {
                    text: qsTr("Go")
                    highlighted: true
                    onClicked: enterFrequencyDialog.submit()
                }
            }
        }
    }

    onClosing: function(close) {
        if (win.shutdownStarted) {
            close.accepted = true
            return
        }

        if (!win.quitConfirmed && shouldAutoSaveSettings()) {
            close.accepted = false
            win.finishQuit(true)
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
            // Leave hasRunSetup false so the welcome screen returns next launch.
            console.info("First time setup: user elected to exit the program.")
            win.finishQuit(false)
        }

        onShowSettings: function(isNetwork, manufacturer) {
            console.info("First time setup: showing Radio Access settings.")
            const s = MainController.settings
            if (s) {
                s.setOption("Radio.Manufacturer", manufacturer)
                s.setOption("LAN.EnableLAN", isNetwork)
                s.setOption("Program.HasRunSetup", true)
            }
            if (!settingsLoader.active)
                settingsLoader.active = true
            if (settingsLoader.item)
                settingsLoader.item.showRadioAccess(win)
        }

        onSkipSetup: {
            console.info("First time setup: user elected to skip the setup.")
            if (MainController.settings)
                MainController.settings.setOption("Program.HasRunSetup", true)
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
        id: repeaterLoader
        active: false
        sourceComponent: Component {
            RepeaterSetup {
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

    // Load RTTY window
    Loader {
        id: rttyLoader
        active: false
        source: "qrc:/qml/RttyWindow.qml"
        asynchronous: false
        onLoaded: {
            item.rtty = MainController.rtty
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
        if (repeaterLoader.item && repeaterLoader.item.visible && repeaterLoader.item.active) {
            repeaterLoader.item.close()
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

    function openRepeaterWindow() {
        if (!win.radioConnected || !(mainControlSpecs.canRepeater ?? false))
            return
        if (!repeaterLoader.active)
            repeaterLoader.active = true
        Qt.callLater(function() {
            if (repeaterLoader.item) {
                repeaterLoader.item.visible = true
                repeaterLoader.item.raise()
                repeaterLoader.item.requestActivate()
            }
        })
    }

    function openRttyWindow() {
        if (!win.radioConnected)
            return
        if (!rttyLoader.active)
            rttyLoader.active = true
        MainController.showRtty()
        Qt.callLater(function() {
            if (rttyLoader.item) {
                rttyLoader.item.raise()
                rttyLoader.item.requestActivate()
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
        if (command === "app.enterFrequency") {
            enterFrequencyDialog.showDialog(receiver)
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
        function onPttTimeoutExpired(seconds) {
            pttTimeoutDialog.showDialog()
        }
        function onAudioDeviceProblem(headline, detail) {
            audioDeviceProblemDialog.addProblem(headline, detail)
        }
        function onSerialPortProblem(headline, detail) {
            serialPortProblemDialog.showProblem(headline, detail)
        }
        function onRigFilesProblemChanged() {
            rigFilesProblemDialog.showIfNeeded()
        }
    }

    // Connection problem dialogs (audio device, serial port) can be raised
    // together when connecting. Show one at a time; the rest wait here.
    property var pendingProblemDialogs: []

    function problemDialogShowing() {
        return audioDeviceProblemDialog.visible || serialPortProblemDialog.visible
               || rigFilesProblemDialog.visible || legacyUpgradeDialog.visible
    }

    function presentProblemDialog(dlg) {
        if (dlg.visible)
            return
        if (problemDialogShowing()) {
            if (pendingProblemDialogs.indexOf(dlg) < 0) {
                var list = pendingProblemDialogs.slice()
                list.push(dlg)
                pendingProblemDialogs = list
            }
            return
        }
        dlg.present()
    }

    // Returns true when another problem dialog was opened.
    function presentNextProblemDialog() {
        if (pendingProblemDialogs.length === 0)
            return false
        var list = pendingProblemDialogs.slice()
        var dlg = list.shift()
        pendingProblemDialogs = list
        dlg.present()
        return true
    }

    // Shown when the radio serial port cannot be used on connect.
    Dialog {
        id: serialPortProblemDialog
        title: qsTr("Serial port problem")
        width: Math.min(560, win.width - 40)
        height: Math.round(implicitHeight * 1.2)
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        visible: false
        modal: true
        closePolicy: Popup.NoAutoClose

        property string headline: ""
        property string detail: ""

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

        function showProblem(newHeadline, newDetail) {
            headline = newHeadline
            detail = newDetail
            win.presentProblemDialog(serialPortProblemDialog)
        }

        function present() {
            win.ensureWindowFitsPopup(serialPortProblemDialog)
            open()
            forceActiveFocus()
        }

        onClosed: {
            if (!win.presentNextProblemDialog())
                win.shrinkAfterDialog()
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 12
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: serialPortProblemDialog.headline
                horizontalAlignment: Text.AlignHCenter
                font.bold: true
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: serialPortProblemDialog.detail
                horizontalAlignment: Text.AlignHCenter
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("OK")
                    onClicked: serialPortProblemDialog.close()
                }
            }
        }
    }

    // Shown at startup when the settings were last saved by wfview 1.x or 2.x.
    // MainController does not connect automatically in that case.
    Dialog {
        id: legacyUpgradeDialog
        title: qsTr("Welcome to wfview version 3")
        width: Math.min(560, win.width - 40)
        height: Math.round(implicitHeight * 1.2)
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

        function showIfNeeded() {
            if (MainController.settings && MainController.settings.upgradedFromLegacyVersion())
                win.presentProblemDialog(legacyUpgradeDialog)
        }

        function present() {
            win.ensureWindowFitsPopup(legacyUpgradeDialog)
            open()
            legacyUpgradeOkButton.forceActiveFocus()
        }

        onClosed: {
            if (!settingsLoader.active)
                settingsLoader.active = true
            if (settingsLoader.item)
                settingsLoader.item.showRadioAccess(win)
            if (!win.presentNextProblemDialog())
                win.shrinkAfterDialog()
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 12
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("Welcome to wfview version 3")
                horizontalAlignment: Text.AlignHCenter
                font.bold: true
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Please take a moment to review your audio, network and serial settings before connecting to your radio")
                horizontalAlignment: Text.AlignHCenter
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Once you have verified your settings, press 'Save Settings' to disable this note")
                horizontalAlignment: Text.AlignHCenter
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    id: legacyUpgradeOkButton
                    text: qsTr("Ok")
                    onClicked: legacyUpgradeDialog.close()
                    Keys.onReturnPressed: legacyUpgradeDialog.close()
                    Keys.onEnterPressed: legacyUpgradeDialog.close()
                    Keys.onEscapePressed: legacyUpgradeDialog.close()
                }
            }
        }
    }

    // Shown when no rig definition files were found at startup (or after a
    // manufacturer change).
    Dialog {
        id: rigFilesProblemDialog
        title: qsTr("Rig files not found")
        width: Math.min(560, win.width - 40)
        height: Math.round(implicitHeight * 1.2)
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

        function showIfNeeded() {
            if (MainController && MainController.rigFilesProblem !== "")
                win.presentProblemDialog(rigFilesProblemDialog)
        }

        function present() {
            win.ensureWindowFitsPopup(rigFilesProblemDialog)
            open()
            forceActiveFocus()
        }

        onClosed: {
            if (!win.presentNextProblemDialog())
                win.shrinkAfterDialog()
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 12
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("wfview could not find any rig files.")
                horizontalAlignment: Text.AlignHCenter
                font.bold: true
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: MainController ? MainController.rigFilesProblem : ""
                horizontalAlignment: Text.AlignHCenter
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("OK")
                    onClicked: rigFilesProblemDialog.close()
                }
            }
        }
    }

    // Shown when a configured audio device is blank, missing, or cannot be
    // opened. Several problems from one connection are listed together.
    Dialog {
        id: audioDeviceProblemDialog
        title: qsTr("Audio device problem")
        width: Math.min(560, win.width - 40)
        // 20% taller than the content needs, with the content centered.
        height: Math.round(implicitHeight * 1.2)
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        visible: false
        modal: true
        closePolicy: Popup.NoAutoClose

        property var problems: []

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

        function addProblem(headline, detail) {
            var list = problems.slice()
            list.push({ "headline": headline, "detail": detail })
            problems = list
            win.presentProblemDialog(audioDeviceProblemDialog)
        }

        function present() {
            suppressAudioWarningChk.checked = false
            win.ensureWindowFitsPopup(audioDeviceProblemDialog)
            open()
            forceActiveFocus()
        }

        function dismiss() {
            if (suppressAudioWarningChk.checked && MainController && MainController.settings)
                MainController.settings.setOption("Interface.WarnAudioDeviceProblems", false)
            close()
        }

        onClosed: {
            problems = []
            if (!win.presentNextProblemDialog())
                win.shrinkAfterDialog()
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 12
            spacing: 12

            Repeater {
                model: audioDeviceProblemDialog.problems
                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: modelData.headline
                        horizontalAlignment: Text.AlignHCenter
                        font.bold: true
                        color: win.palette.windowText
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: modelData.detail
                        horizontalAlignment: Text.AlignHCenter
                        color: win.palette.windowText
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Audio for this connection will not work until this is fixed. "
                           + "To fix it, disconnect from the radio, open Settings \u2192 Radio Access, "
                           + "and re-select the audio device. Then connect again.")
                horizontalAlignment: Text.AlignHCenter
                color: win.palette.windowText
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                CheckBox {
                    id: suppressAudioWarningChk
                    text: qsTr("Suppress this warning")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Can be turned back on in Settings \u2192 User Interface \u2192 Other.")
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("OK")
                    onClicked: audioDeviceProblemDialog.dismiss()
                }
            }
        }
    }

    Dialog {
        id: pttTimeoutDialog
        title: qsTr("PTT Timeout")
        width: 420
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        visible: false
        modal: true
        standardButtons: Dialog.Ok

        background: Rectangle {
            color: win.palette.window
            border.color: win.palette.highlight
            border.width: 1
            radius: 4
        }

        function showDialog() {
            win.ensureWindowFitsPopup(pttTimeoutDialog)
            open()
            forceActiveFocus()
        }

        onClosed: win.shrinkAfterDialog()

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("PTT Timeout")
                font.pointSize: Qt.application.font.pointSize * 1.8
                font.bold: true
                color: win.palette.windowText
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("The PTT timeout can be disabled in Settings \u2192 Radio Settings.")
                color: win.palette.windowText
                wrapMode: Text.WordWrap
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

    function shouldAutoSaveSettings() {
        if (!MainController || !MainController.settings)
            return false

        return win.settingAsBool(MainController.settings.dirty, false)
                && win.settingAsBool(MainController.settings.options["Interface.AlwaysSaveOnExit"], false)
    }

    function shouldConfirmUnsavedSettings() {
        if (!MainController || !MainController.settings)
            return false
        if (shouldAutoSaveSettings())
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
        if (win.radioConnected || win.hasConnectedOnce || win.waylandPlatform)
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
        if (g && g.valid) {
            win.savedStartupGeometry = g
            win.startupGeometryPending = true
            win.startupGeometryRestoreAttempts = 0
            win.scheduleStartupGeometryRestore()
        } else {
            win.connectedGeometryRestored = true
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
        // Before the first connection the window is shrunk to the logo, so
        // there is no real geometry to save yet.
        if ((!win.radioConnected && !win.hasConnectedOnce) || win.waylandPlatform)
            return
        MainController.saveMainWindowGeometry(win.x, win.y, win.width, win.height,
                                              win.visibility === Window.Maximized)
    }

    function requestQuit() {
        if (shouldAutoSaveSettings()) {
            win.finishQuit(true)
            return
        }

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
        if (repeaterLoader.item) {
            repeaterLoader.item.forceClose = true
            repeaterLoader.item.close()
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
            RigCreator {
                id: rigCreatorWin
                onWindowClosed: {
                    win.rigCreatorWindows = win.rigCreatorWindows.filter(function(w) { return w !== rigCreatorWin })
                    rigCreatorWin.destroy()
                }
            }
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
                                if (win._sysTheme) return
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
                                window: win._sysTheme ? undefined : MainController.settings.options["Color.Window"]
                                windowText: win._sysTheme ? undefined : MainController.settings.options["Color.WindowText"]
                                base: win._sysTheme ? undefined : MainController.settings.options["Color.Base"]
                                alternateBase: win._sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
                                text: win._sysTheme ? undefined : MainController.settings.options["Color.MainText"]
                                button: win._sysTheme ? undefined : MainController.settings.options["Color.Button"]
                                buttonText: win._sysTheme ? undefined : MainController.settings.options["Color.ButtonText"]
                                brightText: win._sysTheme ? undefined : MainController.settings.options["Color.BrightText"]
                                highlight: win._sysTheme ? undefined : MainController.settings.options["Color.Highlight"]
                                highlightedText: win._sysTheme ? undefined : MainController.settings.options["Color.HighlightedText"]
                                mid: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                dark: win._sysTheme ? undefined : MainController.settings.options["Color.Dark"]
                                light: win._sysTheme ? undefined : MainController.settings.options["Color.Light"]

                                disabled {
                                    window: win._sysTheme ? undefined : MainController.settings.options["Color.Window"]
                                    windowText: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                    base: win._sysTheme ? undefined : MainController.settings.options["Color.Base"]
                                    alternateBase: win._sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
                                    text: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                    button: win._sysTheme ? undefined : MainController.settings.options["Color.Button"]
                                    buttonText: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                    brightText: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                    highlight: win._sysTheme ? undefined : MainController.settings.options["Color.Dark"]
                                    highlightedText: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                    mid: win._sysTheme ? undefined : MainController.settings.options["Color.Mid"]
                                    dark: win._sysTheme ? undefined : MainController.settings.options["Color.Dark"]
                                    light: win._sysTheme ? undefined : MainController.settings.options["Color.Light"]
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
                                target: MainController.settings
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

        // -------- 1b) disconnected logo (replaces scopeVFOGroup) --------
        Item {
            id: disconnectedLogo
            visible: !win.radioConnected
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            implicitWidth: disconnectedLogoColumn.implicitWidth
            implicitHeight: disconnectedLogoColumn.implicitHeight

            Column {
                id: disconnectedLogoColumn
                anchors.centerIn: parent
                spacing: 8

                Image {
                    id: disconnectedLogoImage
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 256
                    height: 256
                    sourceSize.width: 512
                    sourceSize.height: 512
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    source: "qrc:/resources/wfview.svg"
                    // Fall back to the bitmap logo if the SVG image plugin is unavailable
                    onStatusChanged: {
                        if (status === Image.Error && source != "qrc:/resources/wfview.png")
                            source = "qrc:/resources/wfview.png"
                    }
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Disconnected")
                    font.pixelSize: 28
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // -------- 2) mainGroup (middle, fixed-ish) --------
        Frame {
            id: mainGroup
            Layout.fillWidth: true
            property real targetHeight: Math.max(leftControlsColumn.implicitHeight, mainControlsFlow.implicitHeight)
            Layout.preferredHeight: targetHeight
            clip: true
            padding: 3

            RowLayout {
                anchors.fill: parent
                spacing: win.mainControlSpacing

                ColumnLayout {
                    id: leftControlsColumn
                    Layout.preferredWidth: Math.max(320, win.optionalMeterWidth + 20)
                    Layout.maximumWidth: Math.max(320, win.optionalMeterWidth + 20)
                    Layout.fillHeight: true
                    spacing: win.mainControlSpacing

                    ColumnLayout {
                        spacing: 1
                        visible: win.radioConnected && win.meterStyle === 0

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

                    // Vintage style: one meter replaces both additional meters.
                    MeterPanel {
                        visible: win.radioConnected && win.meterStyle > 0
                        Layout.preferredWidth: win.optionalMeterWidth
                        Layout.preferredHeight: Math.round(win.optionalMeterWidth / 2.1)
                        panelStyle: win.meterStyle
                        primaryChoice: win.optionalMeterChoices[0]
                        secondaryChoice: win.optionalMeterChoices[1]
                        // Already polled through the Meter2Type/Meter3Type slots.
                        consumerId: ""
                        windowTextColor: win.palette.windowText
                        onPrimaryChosen: function(value) { MainController.setOptionalMeterType(2, value) }
                        onSecondaryChosen: function(value) { MainController.setOptionalMeterType(3, value) }
                        onStyleChosen: function(value) { MainController.settings.setOption("Interface.MeterStyle", value) }
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
                    spacing: win.flowSpacing
                    onWidthChanged: win.updateFlowLayout()

                // ---- tuningLayout ----
                ColumnLayout {
                    width: win.mainControlDialSize
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: win.mainControlDialSize
                    Layout.maximumWidth: win.mainControlDialSize
                    spacing: win.mainControlSpacing
                    visible: win.radioConnected && win.showTuningDial

                    TuningKnob {
                        id: freqDial
                        Layout.preferredWidth: win.mainControlDialSize
                        Layout.preferredHeight: win.mainControlDialSize
                        visible: win.radioConnected
                        enabled: dialReceiver !== null
                        onStepped: function(steps) {
                            if (dialReceiver)
                                dialReceiver.tuneSteps(steps, 0, true)
                        }
                        onWheeled: function(angleDelta, modifiers) {
                            if (dialReceiver)
                                dialReceiver.onWheelTune(angleDelta, modifiers)
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
                            visible: win.radioConnected && (mainControlSpecs.canRit ?? false) && !win.wideRitSlider

                            Rectangle {
                                Layout.preferredWidth: win.ritDialSize + 8
                                Layout.preferredHeight: win.ritDialSize + 8
                                radius: width / 2
                                color: Qt.rgba(win.palette.button.r, win.palette.button.g, win.palette.button.b, 0.35)
                                border.color: win.palette.mid

                                Dial {
                                    WheelArea { stepsPerRange: Math.max(1, (parent.to - parent.from) / Math.max(1, parent.stepSize)) }
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

                // ---- wide RIT slider (replaces the RIT knob when enabled) ----
                Frame {
                    id: wideRitGroup
                    width: win.wideRitSliderWidth
                    Layout.alignment: Qt.AlignTop
                    padding: 3
                    visible: win.radioConnected && win.wideRitSlider && (mainControlSpecs.canRit ?? false)
                    readonly property var spec: controlSpec("ritFrequency", -500, 500)

                    contentItem: ColumnLayout {
                        spacing: 4

                        RowLayout {
                            spacing: 8
                            CheckBox {
                                id: wideRitEnableChk
                                text: qsTr("RIT")
                                checked: MainController.ritEnabled
                                onToggled: MainController.ritEnabled = checked
                            }
                            SpinBox {
                                id: wideRitSpin
                                from: wideRitGroup.spec.min
                                to: wideRitGroup.spec.max
                                stepSize: 1
                                editable: true
                                value: MainController.ritFrequency
                                enabled: MainController.ritEnabled
                                Accessible.name: "RIT Offset Hz"
                                onValueModified: MainController.ritFrequency = value
                            }
                            Label { text: qsTr("Hz") }
                            Item { Layout.fillWidth: true }
                        }

                        Slider {
                            id: wideRitSlider
                            // 10 Hz per wheel notch; drag resolves to 1 Hz
                            WheelArea { stepsPerRange: Math.max(1, (parent.to - parent.from) / 10) }
                            Layout.fillWidth: true
                            from: wideRitGroup.spec.min
                            to: wideRitGroup.spec.max
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: MainController.ritFrequency
                            enabled: MainController.ritEnabled
                            onMoved: MainController.ritFrequency = Math.round(value)

                            HoverHandler { id: hoverWideRit }
                            ToolTip.visible: hoverWideRit.hovered || pressed
                            ToolTip.text: qsTr("RIT") + ": " + Math.round(value).toString() + " Hz"
                            ToolTip.delay: 300
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
                                    WheelArea {}
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
                                    visible: !win.rxAudioOnOptionalMeter
                                    enabled: visible
                                    value: visible ? MainController.rxAudioLevel : 0
                                    label: qsTr("RX audio")
                                    active: MainController.localAudioAvailable
                                }
                            }
                            Label { text: qsTr("Vol"); horizontalAlignment: Text.AlignHCenter }
                        }
                        ColumnLayout {
                            visible: rangeControlVisible("txPower")
                            Slider {
                                WheelArea {}
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
                                WheelArea {}
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
                                    WheelArea {}
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
                                    visible: !win.txAudioOnOptionalMeter
                                    enabled: visible
                                    value: visible ? MainController.txAudioLevel : 0
                                    label: qsTr("TX audio")
                                    active: micGainSlider.enabled
                                }
                            }
                            Label { text: MainController.modGainLabel; horizontalAlignment: Text.AlignHCenter }
                        }
                    }
                }

                // ---- command buttons (Transmit, ATU, etc.) ----
                // Fixed width so column changes don't cause the Flow to re-wrap.
                Item {
                    id: commandButtonsWrapper
                    width: 246
                    height: commandButtonsColumn.implicitHeight
                    clip: true
                    visible: win.radioConnected

                    Behavior on height {
                        enabled: win._flowLayoutInitialized
                        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
                    }

                GridLayout {
                    id: commandButtonsColumn
                    property bool twoColumns: win.cmdTwoColumns
                    columns: twoColumns ? 2 : 1
                    anchors.left: parent.left
                    rowSpacing: win.mainControlSpacing
                    columnSpacing: win.mainControlSpacing

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
                        text: qsTr("RTTY")
                        Layout.preferredWidth: 120
                        enabled: win.radioConnected
                        visible: win.radioConnected
                        onClicked: win.openRttyWindow()
                    }

                    Button {
                        text: qsTr("Rpt/Split")
                        Layout.preferredWidth: 120
                        enabled: win.radioConnected && (mainControlSpecs.canRepeater ?? false)
                        visible: win.radioConnected && (mainControlSpecs.canRepeater ?? false)
                        onClicked: win.openRepeaterWindow()
                    }
                    Button {
                        text: qsTr("Memories")
                        Layout.preferredWidth: 120
                        visible: win.radioConnected && (mainControlSpecs.canMemories ?? false)
                        enabled: win.radioConnected && (mainControlSpecs.canMemories ?? false)
                        onClicked: win.openMemoriesWindow()
                    }

                    Repeater {
                        model: win.macroButtons
                        delegate: Button {
                            text: String(modelData.name)
                            Layout.preferredWidth: 120
                            enabled: MainController.runningMacroId < 0
                            highlighted: MainController.runningMacroId === modelData.id
                            onClicked: MainController.runMacro(modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Run macro %1").arg(modelData.id)
                        }
                    }
                }
                } // commandButtonsWrapper

                Flow {
                    id: scopeAndOtherControlsFlow
                    readonly property bool hasReceiverScopeControls: (mainControlSpecs.canDualScope ?? false)
                                                                   || (mainControlSpecs.canDualWatch ?? false)
                                                                   || (mainControlSpecs.canMainSub ?? false)
                                                                   || (mainControlSpecs.canSwapMainSub ?? false)
                                                                   || (mainControlSpecs.canEqualMainSub ?? false)
                    readonly property bool showScopeControls: win.radioConnected && hasReceiverScopeControls
                    readonly property bool showOtherControls: win.radioConnected && anyControlVisible(["canCompressor", "canVox"])
                    property int scopePaneWidth: win.scopeFlowPaneWidth
                    readonly property int otherPaneWidth: 165
                    readonly property int rowWidthBeforeScope: (freqDial.visible ? win.mainControlDialSize : 0)
                                                            + (tuningControlsGroup.visible ? tuningControlsGroup.width : 0)
                                                            + (wideRitGroup.visible ? wideRitGroup.width + mainControlsFlow.spacing : 0)
                                                            + (levelsControlsGroup.visible ? levelsControlsGroup.implicitWidth : 0)
                                                            + (commandButtonsWrapper.visible ? commandButtonsWrapper.width : 0)
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
                                // Not checkable so the binding survives a click and follows the radio.
                                checkable: false
                                checked: MainController.splitEnabled
                                enabled: mainControlSpecs.canSplit ?? false
                                visible: (mainControlSpecs.canSplit ?? false) && scopeAndOtherControlsFlow.hasReceiverScopeControls
                                onClicked: MainController.splitEnabled = !MainController.splitEnabled
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
                // Stays visible after disconnect so the last connection statistics remain readable
                visible: text.length > 0
                Layout.preferredWidth: visible ? 360 : 0
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
        win.height = win.minimumHeight
        restoreWindowGeometry()
        // First run: show the welcome screen. MainController does not open
        // the radio connection until hasRunSetup is true.
        if (MainController.settings && MainController.settings.option("Program.HasRunSetup") !== true)
            Qt.callLater(firstTimeSetup.begin)
        else
            Qt.callLater(legacyUpgradeDialog.showIfNeeded)
        // The rig file scan runs before this window exists.
        Qt.callLater(rigFilesProblemDialog.showIfNeeded)
        // Serial/audio alerts raised by the startup connect, before this
        // window existed. They queue behind any dialog already showing.
        Qt.callLater(function() { MainController.windowReady() })
    }

}
