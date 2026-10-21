// Settings.qml
// Conversion of your QWidget .ui to QML (Qt Quick Controls 2 + Layouts).
//
// Notes (tell-it-like-it-is):
// - This is a *UI structure* conversion. Your C++ wiring (models, signals, validators, persistence)
//   still needs to be connected.
// - QWidget-only custom controls (QLedLabel, tableWidget) don’t exist in QML, so I’ve provided
//   QML stand-ins: LedSwatch + a basic users TableView.
// - The huge color editor grid is included, but it’s best maintained as data-driven (Repeater)
//   later. For now it’s a faithful “static” layout.
//
// Requires: QtQuick 2.15+, QtQuick.Controls 2.15+, QtQuick.Layouts 1.15+
// If you use Qt6, change imports accordingly.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs
import WFVIEW 1.0

ApplicationWindow {
    id: window
    color: palette.window
    transientParent: null
    title: qsTr("Settings")

    width: 1193
    height: 625

    readonly property int contentMargin: 8
    readonly property bool _sysTheme: controller && controller.options["Interface.UseSystemTheme"] === true

    property var controller  // 'required' keyword is Qt 6 only

    // Meter faces for Interface.MeterStyle and Interface.PopoutMeterStyle.
    readonly property var meterStyleChoices: [
        { text: qsTr("Modern"), value: 0 },
        { text: qsTr("Analog"), value: 1 },
        { text: qsTr("Analog Cream"), value: 2 },
        { text: qsTr("LCD"), value: 3 },
        { text: qsTr("LCD Arc"), value: 4 }
    ]

    palette {
        window: _sysTheme ? undefined : (controller ? controller.options["Color.Window"] : undefined)
        windowText: _sysTheme ? undefined : (controller ? controller.options["Color.WindowText"] : undefined)
        base: _sysTheme ? undefined : (controller ? controller.options["Color.Base"] : undefined)
        alternateBase: _sysTheme ? undefined : (controller ? controller.options["Color.AlternateBase"] : undefined)
        text: _sysTheme ? undefined : (controller ? controller.options["Color.MainText"] : undefined)
        button: _sysTheme ? undefined : (controller ? controller.options["Color.Button"] : undefined)
        buttonText: _sysTheme ? undefined : (controller ? controller.options["Color.ButtonText"] : undefined)
        brightText: _sysTheme ? undefined : (controller ? controller.options["Color.BrightText"] : undefined)
        highlight: _sysTheme ? undefined : (controller ? controller.options["Color.Highlight"] : undefined)
        highlightedText: _sysTheme ? undefined : (controller ? controller.options["Color.HighlightedText"] : undefined)
        mid: _sysTheme ? undefined : (controller ? controller.options["Color.Mid"] : undefined)
        dark: _sysTheme ? undefined : (controller ? controller.options["Color.Dark"] : undefined)
        light: _sysTheme ? undefined : (controller ? controller.options["Color.Light"] : undefined)
    }

    property int connStatus: Number(MainController.connStatus)

    onControllerChanged: applyDisabledPalette()

    // ---- Helpers ----

    function applyDisabledPalette() {
        if (!controller || !controller.options || _sysTheme)
            return

        try {
            palette.disabled.window = controller.options["Color.Window"]
            palette.disabled.windowText = Qt.darker(controller.options["Color.WindowText"], 2.5)
            palette.disabled.base = Qt.darker(controller.options["Color.Base"], 1.1)
            palette.disabled.alternateBase = Qt.darker(controller.options["Color.AlternateBase"], 1.1)
            palette.disabled.text = Qt.darker(controller.options["Color.MainText"], 2.5)
            palette.disabled.button = Qt.darker(controller.options["Color.Button"], 1.3)
            palette.disabled.buttonText = Qt.darker(controller.options["Color.ButtonText"], 2.5)
            palette.disabled.brightText = Qt.darker(controller.options["Color.BrightText"], 2.5)
            palette.disabled.highlight = Qt.darker(controller.options["Color.Highlight"], 1.5)
            palette.disabled.highlightedText = Qt.darker(controller.options["Color.MainText"], 2.5)
            palette.disabled.mid = controller.options["Color.Mid"]
            palette.disabled.dark = controller.options["Color.Dark"]
            palette.disabled.light = Qt.darker(controller.options["Color.Light"], 1.2)
            palette.disabled.placeholderText = Qt.darker(controller.options["Color.PlaceholderText"], 1.5)
        } catch (e) {
            // Qt 5 does not expose palette disabled groups to QML.
        }
    }

    Component.onCompleted: applyDisabledPalette()

    Connections {
        target: MainController.settings
        function onColChanged(items) {
            window.applyDisabledPalette()
        }
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

    // Model for an audio device ComboBox. When the configured device is blank
    // or not on this system, a placeholder for it is listed first, so the box
    // never shows an empty selection and choosing a real device is a change.
    function audioDeviceModel(spec, configured) {
        var devices = (spec && spec.model) ? spec.model : []
        var name = (configured === undefined || configured === null) ? "" : String(configured)
        var list = []
        var found = false
        for (var i = 0; i < devices.length; ++i) {
            list.push(devices[i])
            if (String(devices[i].value) === name)
                found = true
        }
        if (!found)
            list.unshift(name === ""
                         ? { "text": qsTr("(no device selected)"), "value": "" }
                         : { "text": qsTr("%1 (not found)").arg(name), "value": name })
        return list
    }

    // Model for the serial port ComboBox, built like audioDeviceModel(). A
    // configured port that is not enumerated but does exist (for example a
    // /dev/serial/by-id link) is listed by name, without "(not found)".
    function serialPortModel(spec, configured) {
        var ports = (spec && spec.model) ? spec.model : []
        var name = (configured === undefined || configured === null) ? "" : String(configured)
        var list = []
        var found = false
        for (var i = 0; i < ports.length; ++i) {
            list.push(ports[i])
            if (String(ports[i].value) === name)
                found = true
        }
        if (!found) {
            var text
            if (name === "")
                text = qsTr("(no port selected)")
            else if (controller && controller.serialPortExists(name))
                text = name
            else
                text = qsTr("%1 (not found)").arg(name)
            list.unshift({ "text": text, "value": name })
        }
        return list
    }

    // Store a device choice, then re-bind the box to the stored value
    // so it always shows what the backend accepted.
    function selectDeviceOption(cb, key) {
        if (!controller)
            return
        controller.setOption(key, cb.currentValue)
        cb.currentIndex = Qt.binding(function() {
            return controller ? indexFromValue(cb, controller.options[key]) : -1
        })
    }

    function boolValue(value, fallback) {
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
        return fallback === undefined ? false : boolValue(fallback, false)
    }

    function optBool(key, fallback) {
        if (!controller || !controller.options || controller.options[key] === undefined)
            return boolValue(fallback, false)
        return boolValue(controller.options[key], fallback)
    }

    function showRadioAccess(ownerWindow) {
        settingsStack.currentIndex = 0
        showOnScreen(ownerWindow)
    }

    function showAudioProcessing() {
        settingsStack.currentIndex = 11
        showOnScreen()
    }

    // Fit the window height to the selected page. Pages are fill-height
    // Items/Loaders, so measure their content's implicit height instead.
    property rect _fitArea: Qt.rect(0, 0, 0, 0)
    property bool _fitAreaWayland: false

    function contentFitHeight() {
        var page = settingsStack.children[settingsStack.currentIndex]
        if (!page)
            return 0
        // A page (or the item a page Loader holds) may ask for a specific
        // height, e.g. scrolling pages that should not open at full length.
        const content = page.item !== undefined && page.item ? page.item : page
        var pageHeight = 0
        if (content.preferredFitHeight !== undefined && content.preferredFitHeight > 0) {
            pageHeight = content.preferredFitHeight
        } else {
            pageHeight = page.implicitHeight
            if (page.children.length > 0)
                pageHeight = Math.max(pageHeight, page.children[0].implicitHeight)
        }
        // Never cut off the category list on short pages.
        pageHeight = Math.max(pageHeight, settingsList.contentHeight)
        if (pageHeight <= 0)
            return 0
        return Math.ceil(pageHeight + settingsContent.spacing
                         + bottomButtonsLayout.implicitHeight
                         + window.contentMargin * 2)
    }

    // A page's implicit height is often wrong until its first layout pass
    // (wrapped text, combo boxes waiting for their models), so follow changes
    // to it. Stop once the user has resized the window themselves.
    property real _appliedFitHeight: -1
    readonly property real pageFitHeight: contentFitHeight()
    onPageFitHeightChanged: {
        if (visible && Math.abs(height - _appliedFitHeight) < 1)
            Qt.callLater(sizeToContent)
    }

    function sizeToContent() {
        var fit = contentFitHeight()
        if (fit <= 0)
            return
        const area = _fitArea
        if (area.height > 0)
            fit = Math.min(fit, Math.max(240, area.height - 48))
        height = fit
        _appliedFitHeight = height

        // Keep the bottom edge on screen when the window grows (X11 only;
        // Wayland clients cannot position their windows).
        if (visible && area.height > 0 && !_fitAreaWayland) {
            const bottom = area.y + area.height - 24
            if (y + height > bottom)
                y = Math.max(area.y + 24, bottom - height)
        }
    }

    function showOnScreen(ownerWindow) {
        const platform = String(MainController.platformName())
        const wayland = platform.indexOf("wayland") !== -1
        const targetScreen = ownerWindow && ownerWindow.screen ? ownerWindow.screen : screen
        const rect = targetScreen ? targetScreen.availableGeometry : null

        _fitAreaWayland = wayland
        if (rect)
            _fitArea = Qt.rect(rect.x, rect.y, rect.width, rect.height)
        sizeToContent()

        if (rect && !wayland) {
            const margin = 24
            const maxWidth = Math.max(320, rect.width - margin * 2)
            const maxHeight = Math.max(240, rect.height - margin * 2)
            width = Math.min(width, maxWidth)
            height = Math.min(height, maxHeight)
            x = rect.x + Math.max(margin, Math.round((rect.width - width) / 2))
            y = rect.y + Math.max(margin, Math.round((rect.height - height) / 2))
        }

        show()
        raise()
        requestActivate()

        // Layout implicit sizes may only settle after the first polish.
        Qt.callLater(sizeToContent)
    }

    function commitIntegerOption(key, field, minimum, maximum) {
        if (!controller)
            return

        var value = parseInt(field.text)
        if (isNaN(value) || value < minimum || value > maximum) {
            field.text = String(controller.options[key])
            return
        }
        controller.setOption(key, value)
    }


    component ColorRow : RowLayout {
        spacing: 8
        Layout.fillWidth: true

        property string key: "Color.SpectrumLine"
        property string label: qsTr("Spectrum line")
        property string tooltip: ""  // NEW: tooltip property

        // Can be QColor-ish OR a string in some cases; we handle both.
        readonly property var cur: controller ? controller.options[key] : null

        function clamp01(x) { return Math.max(0, Math.min(1, x)) }

        function qmlColor(v) {
            // Accept QColor, QML color, or strings "#RRGGBB" / "#AARRGGBB"
            if (v === null || v === undefined) return Qt.rgba(0.266, 0.266, 0.266, 1) // "#444444"
            if (typeof v === "string") return parseHexToColor(v) ?? Qt.rgba(0.266, 0.266, 0.266, 1)
            // If it's already a color/QColor, just return it (QML can consume it directly)
            return v
        }

        function parseHexToColor(s) {
            if (!s) return null
            var t = ("" + s).trim()
            if (t.length === 0) return null
            if (t[0] !== "#") t = "#" + t

            // #RRGGBB
            if (t.length === 7) {
                var r = parseInt(t.slice(1,3), 16)
                var g = parseInt(t.slice(3,5), 16)
                var b = parseInt(t.slice(5,7), 16)
                if (isNaN(r) || isNaN(g) || isNaN(b)) return null
                return Qt.rgba(r/255, g/255, b/255, 1)
            }

            // #AARRGGBB
            if (t.length === 9) {
                var a = parseInt(t.slice(1,3), 16)
                var r2 = parseInt(t.slice(3,5), 16)
                var g2 = parseInt(t.slice(5,7), 16)
                var b2 = parseInt(t.slice(7,9), 16)
                if (isNaN(a) || isNaN(r2) || isNaN(g2) || isNaN(b2)) return null
                return Qt.rgba(r2/255, g2/255, b2/255, a/255)
            }

            return null
        }

        function byteHex(n) {
            var h = Math.round(n).toString(16).toUpperCase()
            return (h.length === 1) ? ("0" + h) : h
        }

        function colorToHexArgb(c) {
            // Works for QML colors (r,g,b,a exist). Also works for QColor-ish values in QML.
            if (!c) return ""
            var cc = qmlColor(c)
            // QML color has r/g/b/a in 0..1
            var a = byteHex(clamp01(cc.a) * 255)
            var r = byteHex(clamp01(cc.r) * 255)
            var g = byteHex(clamp01(cc.g) * 255)
            var b = byteHex(clamp01(cc.b) * 255)
            return "#" + a + r + g + b
        }

        // ---------- UI ----------
        Button {
            text: label
            Layout.preferredWidth: 150
            ToolTip.text: tooltip  // NEW: tooltip on button
            ToolTip.visible: tooltip && hovered  // NEW: show tooltip on hover if provided
            ToolTip.delay: 500  // NEW: optional delay before showing

            onClicked: {
                if (!controller) return
                dlg.selectedColor = qmlColor(cur)
                dlg.open()
            }
        }

        TextField {
            id: hexField
            Layout.preferredWidth: 110
            placeholderText: qsTr("#AARRGGBB")

            TextEditMenu { field: hexField }

            // Don't bind text permanently. We update it when cur changes.
            onEditingFinished: {
                if (!controller) return
                var c = parseHexToColor(text)
                if (!c) {
                    text = colorToHexArgb(cur)   // revert
                    return
                }
                controller.setOption(key, c)
            }
        }

        Rectangle {
            // QLedLabel stand-in
            width: 10
            height: 18
            radius: 2
            border.width: 1
            color: qmlColor(cur)
        }

        // Keep textbox synced when controller changes the color (including preset switches),
        // but don't overwrite while user is typing.
        onCurChanged: {
            if (!hexField.activeFocus)
                hexField.text = colorToHexArgb(cur)
        }

        Component.onCompleted: {
            hexField.text = colorToHexArgb(cur)
        }

        ColorDialog {
            id: dlg
            title: qsTr("Select ") + label
            options: ColorDialog.ShowAlphaChannel

            onAccepted: {
                if (!controller) return
                controller.setOption(key, dlg.selectedColor)
            }
        }
    }


    component LedSwatch: Rectangle {
        // QLedLabel stand-in
        width: 10
        height: 18
        radius: 2
        border.width: 1
        color: "#444444"
    }

    component LabeledField: RowLayout {
        // Small helper for Label + TextField
        property string label: ""
        property alias text: field.text
        property alias placeholderText: field.placeholderText
        property int fieldWidth: 150
        spacing: 6
        Label { text: parent.label }
        TextField {
            id: field
            Layout.preferredWidth: parent.fieldWidth
        }
    }

    component ProcSwitch: CheckBox {
        property string key: ""
        checked: optBool(key, false)
        onToggled: {
            if (!controller) return
            controller.setOption(key, checked)
            MainController.applyAudioProcessingSettings()
        }
    }

    component ProcCombo: RowLayout {
        property string key: ""
        property string label: ""
        property var values: []
        spacing: 8
        Label { text: parent.label; Layout.preferredWidth: 130 }
        ComboBox {
            id: combo
            Layout.preferredWidth: 180
            textRole: "text"
            valueRole: "value"
            model: values
            currentIndex: controller && controller.options ? indexFromValue(combo, controller.options[key]) : -1
            onActivated: {
                if (!controller) return
                controller.setOption(key, currentValue)
                MainController.applyAudioProcessingSettings()
            }
        }
    }

    // Label + combo for a stored transmit modulation source (rigInput reg).
    // The choices come from rigCaps, so it is hidden until a radio is connected.
    component ModSourcePref: RowLayout {
        id: modSourcePref
        property string key: ""
        property string label: ""
        property int dataMode: 0
        property bool active: true
        readonly property bool supported: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(dataMode)
        visible: supported
        spacing: 8
        Label { text: modSourcePref.label; enabled: modSourcePref.active && window.connStatus === 2 }
        ComboBox {
            id: modSourcePrefCombo
            Accessible.name: modSourcePref.label
            enabled: modSourcePref.active && window.connStatus === 2
            textRole: "text"
            valueRole: "value"
            model: modSourcePref.supported ? MainController.modSourceOptions(modSourcePref.dataMode) : []
            currentIndex: count > 0 && controller && controller.options ? indexFromValue(modSourcePrefCombo, controller.options[modSourcePref.key]) : -1
            onActivated: if (controller) controller.setOption(modSourcePref.key, currentValue)
        }
    }

    // Checkbox + combo choosing a user macro (stored by id) to run.
    component MacroPref: RowLayout {
        id: macroPref
        property string enableKey: ""
        property string idKey: ""
        property string label: ""
        readonly property var macroOptions: {
            var list = controller ? controller.macros : []
            var out = []
            for (var i = 0; i < list.length; ++i)
                out.push({ text: String(list[i].name), value: list[i].id })
            return out
        }
        readonly property bool haveMacros: macroOptions.length > 0
        spacing: 8
        CheckBox {
            id: macroPrefChk
            text: macroPref.label
            enabled: macroPref.haveMacros || checked
            checked: optBool(macroPref.enableKey, false)
            onClicked: {
                if (!controller)
                    return
                // Store a macro straight away if none valid is selected yet.
                if (checked && macroPrefCombo.currentIndex < 0 && macroPref.haveMacros)
                    controller.setOption(macroPref.idKey, macroPref.macroOptions[0].value)
                controller.setOption(macroPref.enableKey, checked)
            }
        }
        ComboBox {
            id: macroPrefCombo
            Accessible.name: macroPref.label
            Layout.preferredWidth: 180
            enabled: macroPref.haveMacros && macroPrefChk.checked
            textRole: "text"
            valueRole: "value"
            model: macroPref.macroOptions
            currentIndex: count > 0 && controller && controller.options ? indexFromValue(macroPrefCombo, controller.options[macroPref.idKey]) : -1
            displayText: !macroPref.haveMacros ? qsTr("No macros defined")
                         : currentIndex < 0 ? qsTr("Select a macro") : currentText
            onActivated: if (controller) controller.setOption(macroPref.idKey, currentValue)
        }
    }

    component ProcSlider: RowLayout {
        property string key: ""
        property string label: ""
        property real from: 0
        property real to: 1
        property real step: 1
        property int decimals: 0

        function currentValue() {
            if (!controller || !controller.options || controller.options[key] === undefined)
                return from
            return Number(controller.options[key])
        }

        function commit(v) {
            if (!controller || isNaN(v)) return
            var bounded = Math.max(from, Math.min(to, v))
            controller.setOption(key, bounded)
            MainController.applyAudioProcessingSettings()
        }

        spacing: 8
        Label {
            text: parent.label
            Layout.preferredWidth: 150
            elide: Text.ElideRight
        }
        Slider {
            WheelArea {}
            id: slider
            Layout.fillWidth: true
            from: parent.from
            to: parent.to
            stepSize: parent.step
            value: parent.currentValue()
            onMoved: parent.commit(value)
        }
        TextField {
            Layout.preferredWidth: 72
            text: Number(slider.value).toFixed(decimals)
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onEditingFinished: parent.commit(Number(text))
        }
    }
    // ---- Main Layout ----
    ColumnLayout {
        id: settingsContent
        anchors.fill: parent
        anchors.margins: window.contentMargin
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // Left category list
            ListView {
                id: settingsList
                Layout.preferredWidth: 150
                Layout.minimumWidth: 150
                Layout.maximumWidth: 150
                Layout.fillHeight: true
                clip: true

                Accessible.name: "Setting Category"
                Accessible.description: "Select the settings category you wish to edit. Selection may also be performed with keyboard shortcuts: Shift F1 is Radio Access, Shift F2 is User Interface, Shift F3 is Radio Settings, Shift F4 is Radio Server, Shift F5 is External Control, Shift F6 is Controllers, Shift F7 is DX Cluster, Shift F8 is Shortcuts, Shift F9 is Experimental"

                model: [
                    { title: qsTr("Radio Access") },
                    { title: qsTr("User Interface") },
                    { title: qsTr("UI Colors") },
                    { title: qsTr("Radio Settings") },
                    { title: qsTr("Radio Server") },
                    { title: qsTr("External Control") },
                    { title: qsTr("Controllers") },
                    { title: qsTr("DX Cluster") },
                    { title: qsTr("Shortcuts") },
                    { title: qsTr("Macros") },
                    { title: qsTr("Experimental") }
                ]

                currentIndex: settingsStack.currentIndex

                delegate: ItemDelegate {
                    width: ListView.view.width
                    text: modelData.title
                    highlighted: ListView.isCurrentItem
                    onClicked: settingsStack.currentIndex = index
                }
            }

            // Stacked pages
            StackLayout {
                id: settingsStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0
                onCurrentIndexChanged: {
                    if (window.visible)
                        Qt.callLater(window.sizeToContent)
                }

                // =========================
                // Page 0: Radio Access
                // =========================
                Item {
                    id: radioAccess
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            enabled: controller && connStatus === 0
                            spacing: 8

                            Label {
                                text: qsTr("Radio")
                            }

                            Item {
                                Layout.preferredWidth: 280
                                Layout.maximumWidth: 360
                                Layout.fillWidth: true
                                Layout.preferredHeight: radioProfileCombo.implicitHeight

                                ComboBox {
                                    id: radioProfileCombo
                                    anchors.fill: parent
                                    editable: true
                                    textRole: "description"
                                    model: controller ? controller.radioProfiles : []
                                    currentIndex: controller ? controller.currentRadioProfileIndex : -1
                                    rightPadding: 82
                                    ToolTip.visible: hovered && displayText.length > 0
                                    ToolTip.text: displayText

                                    function syncCurrentProfile() {
                                        if (!controller)
                                            return
                                        const selected = controller.currentRadioProfileIndex
                                        if (currentIndex !== selected)
                                            currentIndex = selected
                                    }

                                    Component.onCompleted: syncCurrentProfile()

                                    onActivated: if (controller) controller.selectRadioProfile(index)
                                    onAccepted: {
                                        if (controller && currentIndex >= 0)
                                            controller.renameRadioProfile(currentIndex, editText)
                                    }
                                }

                                Row {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 28
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 0

                                    ToolButton {
                                        width: 24
                                        height: radioProfileCombo.height - 4
                                        text: "\u25B2"
                                        enabled: controller && radioProfileCombo.currentIndex > 0
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Move the selected radio profile up.")
                                        onClicked: if (controller) controller.moveRadioProfile(radioProfileCombo.currentIndex, -1)
                                    }

                                    ToolButton {
                                        width: 24
                                        height: radioProfileCombo.height - 4
                                        text: "\u25BC"
                                        enabled: controller
                                                 && radioProfileCombo.currentIndex >= 0
                                                 && radioProfileCombo.currentIndex < controller.radioProfiles.length - 1
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Move the selected radio profile down.")
                                        onClicked: if (controller) controller.moveRadioProfile(radioProfileCombo.currentIndex, 1)
                                    }
                                }
                            }

                            Connections {
                                target: controller
                                function onRadioProfilesChanged() { radioProfileCombo.syncCurrentProfile() }
                                function onCurrentRadioProfileIndexChanged() { radioProfileCombo.syncCurrentProfile() }
                            }

                            Button {
                                text: qsTr("Update")
                                enabled: controller && radioProfileCombo.currentIndex >= 0
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Store the current radio settings in the selected radio profile.")
                                onClicked: if (controller) controller.updateRadioProfile(radioProfileCombo.currentIndex, radioProfileCombo.editText)
                            }

                            Button {
                                text: qsTr("Add")
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Create a new radio profile from the current radio settings.")
                                onClicked: if (controller) controller.addRadioProfile(radioProfileCombo.editText)
                            }

                            Button {
                                text: qsTr("Delete")
                                enabled: controller && radioProfileCombo.currentIndex >= 0 && controller.radioProfiles.length > 1
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Delete the selected radio profile.")
                                onClicked: if (controller) controller.deleteRadioProfile(radioProfileCombo.currentIndex)
                            }

                            Item { Layout.fillWidth: true }
                        }

                        // Top row: Connection / CIV+Model / Info
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            GroupBox {
                                id: groupConnection
                                title: qsTr("Radio Connection")
                                Layout.preferredWidth: 140

                                ColumnLayout {
                                    enabled: !connStatus
                                    spacing: 6

                                    ComboBox {
                                        id: manufacturerCombo
                                        Accessible.name: "Radio Manufacturer Select"
                                        textRole: "text"
                                        valueRole: "value"
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(manufacturerCombo,controller.options["Radio.Manufacturer"])
                                                      : -1

                                        onActivated: if (controller) controller.setOption("Radio.Manufacturer", currentValue)
                                        model: [
                                            { text: qsTr("Icom"), value: 0 },
                                            { text: qsTr("Kenwood"), value: 1 },
                                            { text: qsTr("Yaesu"), value: 2 }
                                        ]
                                    }

                                    ButtonGroup { id: connGroup; exclusive: true ; }

                                    RadioButton {
                                        id: serialEnableBtn
                                        text: qsTr("Serial (USB)")
                                        ButtonGroup.group: connGroup
                                        checked: controller ? !optBool("LAN.EnableLAN", false) : true
                                        onToggled: {
                                            if (checked && controller && optBool("LAN.EnableLAN", false))
                                                controller.setOption("LAN.EnableLAN", false)
                                        }
                                    }

                                    RadioButton {
                                        id: lanEnableBtn
                                        text: qsTr("Network")
                                        ButtonGroup.group: connGroup
                                        checked: optBool("LAN.EnableLAN", false)
                                        onToggled: {
                                            if (checked && controller && !optBool("LAN.EnableLAN", false))
                                                controller.setOption("LAN.EnableLAN", true)
                                        }
                                    }

                                }
                            }

                            GroupBox {
                                title: qsTr("CI-V and Model")
                                enabled: !connStatus
                                Layout.preferredWidth: 300
                                Layout.maximumWidth: 300

                                GridLayout {
                                    columns: 2
                                    columnSpacing: 8
                                    rowSpacing: 8

                                    CheckBox {
                                        id: rigCIVManualAddrChk
                                        text: qsTr("Manual Radio CI-V Address:")
                                        Accessible.name: "Manual CI-V address Checkbox"
                                        ToolTip.visible: hovered
                                        checked: controller ? Number(controller.options["Radio.CIVAddr"]) !== 0 : false
                                        onClicked: {
                                            if (!controller)
                                                return

                                            if (!checked)
                                                controller.setOption("Radio.CIVAddr", 0)
                                            else if (Number(controller.options["Radio.CIVAddr"]) === 0)
                                                controller.setOption("Radio.CIVAddr", 0x94)
                                        }
                                        ToolTip.text: qsTr("If you are using an older (year 2010) radio, you may need to enable this option to manually specify the CI-V address. This option is also useful for radios that do not have CI-V Transceive enabled and thus will not answer our broadcast query for connected rigs on the CI-V bus.\n\nIf you have a modern radio with CI-V Transceive enabled, you should not need to check this box.\n\nYou will need to Save Settings and re-launch wfview for this to take effect.")
                                    }

                                    TextField {
                                        id: rigCIVaddrHexLine
                                        enabled: rigCIVManualAddrChk.checked
                                        Layout.preferredWidth: 50
                                        Layout.maximumWidth: 50
                                        Accessible.name: "Manual CI-V Textbox"
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Enter the address in hexadecimal, no prefix. Examples: IC-706:58, IC-756:50, IC-7300:94, IC-7100:88, etc. After changing, press Save Settings and re-launch wfview.")
                                        text: (controller && controller.options && controller.options["Radio.CIVAddr"] !== undefined)
                                              ? (Number(controller.options["Radio.CIVAddr"]) === 0 ? qsTr("auto") : controller.options["Radio.CIVAddr"].toString(16).toUpperCase())
                                              : ""


                                        onEditingFinished: {
                                            const v = parseInt(text, 16)
                                            if (!isNaN(v) && v > 0 && v < 0xe0)
                                                controller.setOption("Radio.CIVAddr", v)
                                            else
                                                text = Number(controller.options["Radio.CIVAddr"]) === 0
                                                       ? qsTr("auto")
                                                       : controller.options["Radio.CIVAddr"].toString(16).toUpperCase()
                                        }
                                    }

                                    CheckBox {
                                        id: useCIVasRigIDChk
                                        enabled: rigCIVManualAddrChk.checked
                                        Layout.columnSpan: 2
                                        text: qsTr("Use CI-V address as Model ID too")
                                        Accessible.name: "Use CI-V address as Model ID checkbox"
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Only check for older radios! Forces wfview to trust the CI-V address is also the model number. Do not check unless you have an older radio.")

                                        checked: optBool("Radio.CIVisRadioModel", false)
                                        onClicked: if (controller) controller.setOption("Radio.CIVisRadioModel", checked)
                                    }
                                }
                            }

                            GroupBox {
                                title: qsTr("Information")
                                Layout.fillWidth: true

                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("Local audio controls apply to network and USB audio paths.\n") +
                                          qsTr("Please use the \"Radio Server\" page to select server audio.\n") +
                                          qsTr("ONLY use Manual CI-V when Transceive mode is not supported\n\n")+
                                          qsTr("You MUST disconnect from the radio before making any changes.\n\n")+
                                          qsTr("Please use the Connect/Disconnect button below")
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        GroupBox {
                            id: groupLocalAudio
                            title: qsTr("Local Audio")
                            Layout.fillWidth: true
                            enabled: controller && connStatus === 0

                            ColumnLayout {
                                spacing: 8

                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("Audio System") }
                                    ComboBox {
                                        id: audioSystemCombo
                                        Accessible.name: "Audio System Selection Combo"
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("Qt Audio"), value: 0 },
                                            { text: qsTr("PortAudio"), value: 1 },
                                            { text: qsTr("RT Audio"), value: 2 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioSystemCombo,controller.options["Radio.AudioSystem"])
                                                      : -1
                                        onActivated: controller.setOption("Radio.AudioSystem",currentValue)
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("Audio Output") }
                                    ComboBox {
                                        id: audioOutputCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioOutputs"] : null
                                        model: audioDeviceModel(spec, controller ? controller.options["UDP.RxAudio"] : "")
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        visible: spec ? (spec.visible ?? true) : false
                                        currentIndex: controller ? indexFromValue(audioOutputCombo,controller.options["UDP.RxAudio"]) : -1
                                        onActivated: selectDeviceOption(audioOutputCombo, "UDP.RxAudio")
                                        readonly property real widestText: {
                                            let max = 0
                                            for (let i = 0; i < count; ++i)
                                                max = Math.max(max, audioOutMetrics.advanceWidth(textAt(i)))
                                            max = Math.max(max, audioOutMetrics.advanceWidth(displayText))
                                            return max
                                        }
                                        implicitWidth: widestText + leftPadding + rightPadding + indicator.width + 12
                                        Layout.preferredWidth: implicitWidth
                                        Layout.maximumWidth: implicitWidth
                                        FontMetrics { id: audioOutMetrics; font: audioOutputCombo.font }
                                        Accessible.name: "Audio Output Selector"
                                    }

                                    Label { text: qsTr("Audio Input") }
                                    ComboBox {
                                        id: audioInputCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioInputs"] : null
                                        model: audioDeviceModel(spec, controller ? controller.options["UDP.TxAudio"] : "")
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        visible: spec ? (spec.visible ?? true) : false
                                        currentIndex: controller ? indexFromValue(audioInputCombo,controller.options["UDP.TxAudio"]) : -1
                                        onActivated: selectDeviceOption(audioInputCombo, "UDP.TxAudio")
                                        readonly property real widestText: {
                                            let max = 0
                                            for (let i = 0; i < count; ++i)
                                                max = Math.max(max, audioInMetrics.advanceWidth(textAt(i)))
                                            max = Math.max(max, audioInMetrics.advanceWidth(displayText))
                                            return max
                                        }
                                        implicitWidth: widestText + leftPadding + rightPadding + indicator.width + 12
                                        Layout.preferredWidth: implicitWidth
                                        Layout.maximumWidth: implicitWidth
                                        FontMetrics { id: audioInMetrics; font: audioInputCombo.font }
                                        Accessible.name: "Audio Input Selector"
                                    }

                                    Button {
                                        text: qsTr("Rescan")
                                        onClicked: if (controller) controller.rescanAudioDevices()
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Look for audio devices again, for example after plugging one in.")
                                        Accessible.name: "Rescan audio devices"
                                    }

                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        // Serial Connected Radios
                        GroupBox {
                            id: groupSerial
                            title: qsTr("Serial Connected Radios")
                            Layout.fillWidth: true
                            enabled: controller
                                     && connStatus === 0
                                     && !optBool("LAN.EnableLAN", false)
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Label { text: qsTr("Serial Device:") }
                                    ComboBox {
                                        id: serialDeviceListCombo
                                        Layout.preferredWidth: 180
                                        Layout.maximumWidth: 180
                                        Accessible.name: "Serial (USB) Port Selection Combo"
                                        readonly property var spec: controller ? controller.uiSpecs["SerialPorts"] : null
                                        model: serialPortModel(spec, controller ? controller.options["Radio.SerialPortRadio"] : "")
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        visible: spec ? (spec.visible ?? true) : false
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(serialDeviceListCombo,controller.options["Radio.SerialPortRadio"])
                                                      : -1
                                        onActivated: {
                                            if (String(currentValue) === "manual")
                                                manualSerialPortDialog.open()
                                            else
                                                selectDeviceOption(serialDeviceListCombo, "Radio.SerialPortRadio")
                                        }
                                    }

                                    Button {
                                        text: qsTr("Rescan")
                                        onClicked: if (controller) controller.rescanSerialPorts()
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Look for serial ports again, for example after plugging in a radio.")
                                        Accessible.name: "Rescan serial ports"
                                    }

                                    Label { text: qsTr("Baud Rate") }
                                    ComboBox {
                                        id: baudRateCombo
                                        Layout.preferredWidth: 120
                                        Layout.maximumWidth: 120
                                        Accessible.name: "Serial Baud Rate Combo"
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "115,200", value: 115200 },
                                            { text: "57,600", value: 57600 },
                                            { text: "38,400", value: 38400 },
                                            { text: "28,800", value: 28800 },
                                            { text: "19,200", value: 19200 },
                                            { text: "9,600", value: 9600 },
                                            { text: "4,800", value: 4800 },
                                            { text: "2,400", value: 2400 },
                                            { text: "1,200", value: 1200 },
                                            { text: "600", value: 600 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(baudRateCombo, controller.options["Radio.SerialPortBaud"])
                                                      : -1
                                        onActivated: if (controller) controller.setOption("Radio.SerialPortBaud", currentValue)
                                    }

                                    Label { id: pttTypeLabel; text: qsTr("PTT Type") }
                                    ComboBox {
                                        id: pttTypeCombo
                                        Accessible.name: "PTT Type Combo"
                                        model: [
                                            // Icom calls its command protocol CI-V; Kenwood and Yaesu call it CAT.
                                            { text: (controller && Number(controller.options["Radio.Manufacturer"]) !== 0)
                                                    ? qsTr("CAT") : qsTr("CI-V"), value: 0 },
                                            { text: qsTr("RTS"), value: 1 },
                                            { text: qsTr("DTR"), value: 2 }
                                        ]
                                        textRole: "text"
                                        valueRole: "value"

                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(pttTypeCombo,controller.options["Radio.PTTType"])
                                                      : -1

                                        onActivated: controller.setOption("Radio.PTTType",currentValue)
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    CheckBox {
                                        id: usbAudioCheck
                                        text: qsTr("USB Audio")
                                        checked: optBool("Radio.EnableUSBAudio", false)
                                        onClicked: if (controller) controller.setOption("Radio.EnableUSBAudio", checked)
                                    }

                                    Label { text: qsTr("Radio RX Audio") }
                                    ComboBox {
                                        id: usbRadioRxAudioCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioInputs"] : null
                                        model: audioDeviceModel(spec, controller ? controller.options["Radio.USBAudioRXInput"] : "")
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        enabled: optBool("Radio.EnableUSBAudio", false)
                                        currentIndex: controller ? indexFromValue(usbRadioRxAudioCombo, controller.options["Radio.USBAudioRXInput"]) : -1
                                        onActivated: selectDeviceOption(usbRadioRxAudioCombo, "Radio.USBAudioRXInput")
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        Layout.minimumWidth: 160
                                        ToolTip.visible: hovered && displayText.length > 0
                                        ToolTip.text: displayText
                                        ToolTip.delay: 300
                                    }

                                    Label { text: qsTr("Radio TX Audio") }
                                    ComboBox {
                                        id: usbRadioTxAudioCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioOutputs"] : null
                                        model: audioDeviceModel(spec, controller ? controller.options["Radio.USBAudioTXOutput"] : "")
                                        textRole: spec ? spec.textRole : "text"
                                        valueRole: spec ? spec.valueRole : "value"
                                        enabled: optBool("Radio.EnableUSBAudio", false)
                                        currentIndex: controller ? indexFromValue(usbRadioTxAudioCombo, controller.options["Radio.USBAudioTXOutput"]) : -1
                                        onActivated: selectDeviceOption(usbRadioTxAudioCombo, "Radio.USBAudioTXOutput")
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        Layout.minimumWidth: 160
                                        ToolTip.visible: hovered && displayText.length > 0
                                        ToolTip.text: displayText
                                        ToolTip.delay: 300
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        // Network Connected Radios
                        GroupBox {
                            id: groupLan
                            enabled: controller
                                     && connStatus === 0
                                     && optBool("LAN.EnableLAN", false)

                            title: qsTr("Network Connected Radios")
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                spacing: 8

                                // Row: host + ports
                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("Hostname") }
                                    TextField {
                                        id: ipAddressTxt
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        Accessible.name: "Hostname Textbox"
                                        text: (controller && controller.options && controller.options["UDP.IPAddress"] !== undefined)
                                              ? controller.options["UDP.IPAddress"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["UDP.IPAddress"])
                                                controller.setOption("UDP.IPAddress", text)
                                        }
                                    }

                                    Label { id: controlPortLabel; text: qsTr("Control Port") }
                                    TextField {
                                        id: controlPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        Accessible.name: "Control Port Textbox"
                                        text: (controller && controller.options && controller.options["UDP.ControlLANPort"] !== undefined)
                                              ? String(controller.options["UDP.ControlLANPort"])
                                              : "50001"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.ControlLANPort", v)
                                        }
                                    }

                                    Label { text: qsTr("Connection Type"); visible: manufacturerCombo.currentValue === 1 }
                                    ComboBox {
                                        id: networkConnectionTypeCombo
                                        visible: manufacturerCombo.currentValue === 1
                                        Accessible.name: "Connection Type Combo"
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("LAN"), value: 1 },
                                            { text: qsTr("WiFi"), value: 2 },
                                            { text: qsTr("WAN"), value: 3 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(networkConnectionTypeCombo,controller.options["UDP.ConnectionType"])
                                                      : -1

                                        onActivated: controller.setOption("UDP.ConnectionType",currentValue)
                                    }

                                    Label { id: catPortLabel; text: qsTr("CAT"); visible: manufacturerCombo.currentValue === 2 }
                                    TextField {
                                        id: catPortTxt
                                        visible: manufacturerCombo.currentValue === 2
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: (controller && controller.options && controller.options["UDP.SerialLANPort"] !== undefined)
                                              ? String(controller.options["UDP.SerialLANPort"])
                                              : "50002"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.SerialLANPort", v)
                                        }
                                    }

                                    Label { id: audioPortLabel; text: qsTr("Audio");  visible: manufacturerCombo.currentValue === 2 }
                                    TextField {
                                        visible: manufacturerCombo.currentValue === 2
                                        id: audioPortTxt
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: (controller && controller.options && controller.options["UDP.AudioLANPort"] !== undefined)
                                              ? String(controller.options["UDP.AudioLANPort"])
                                              : "50003"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.AudioLANPort", v)
                                        }
                                    }

                                    Label { id: scopePortLabel; text: qsTr("Scope"); visible: manufacturerCombo.currentValue === 2}
                                    TextField {
                                        id: scopePortTxt
                                        visible: manufacturerCombo.currentValue === 2
                                        Layout.preferredWidth: 60
                                        Layout.maximumWidth: 60
                                        text: (controller && controller.options && controller.options["UDP.ScopeLANPort"] !== undefined)
                                              ? String(controller.options["UDP.ScopeLANPort"])
                                              : "50004"

                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: {
                                            const v = parseInt(text)
                                            if (!isNaN(v))
                                                controller.setOption("UDP.ScopeLANPort", v)
                                        }
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: username/pass
                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("Username") }
                                    TextField {
                                        id: usernameTxt
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        Accessible.name: "Username Textbox"
                                        text: (controller && controller.options && controller.options["UDP.Username"] !== undefined)
                                              ? controller.options["UDP.Username"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["UDP.Username"])
                                                controller.setOption("UDP.Username", text)
                                        }

                                    }

                                    Label { text: qsTr("Password") }
                                    TextField {
                                        id: passwordTxt
                                        echoMode: TextInput.Password
                                        Layout.preferredWidth: 150
                                        Layout.maximumWidth: 150
                                        Accessible.name: "Password Textbox"
                                        text: (controller && controller.options && controller.options["UDP.Password"] !== undefined)
                                              ? controller.options["UDP.Password"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["UDP.Password"])
                                                controller.setOption("UDP.Password", text)
                                        }

                                    }

                                    CheckBox {
                                        id: adminLoginChk
                                        visible: manufacturerCombo.currentValue === 1;
                                        text: qsTr("Admin Login")
                                        Accessible.name: "Admin Login Checkbox"
                                        checked: optBool("UDP.AdminLogin", false)
                                        onClicked: if (controller) controller.setOption("UDP.AdminLogin", checked)

                                        Accessible.description: "Check this box if you are using the admin login (Kenwood radios only)"
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                // Row: connection and audio format combos
                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("RX Codec") }
                                    ComboBox {
                                        id: audioRXCodecCombo
                                        enabled: manufacturerCombo.currentValue !== 1
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("LPCM 1ch 16bit"), value: 4 },
                                            { text: qsTr("PCM 1ch 8bit"), value: 2 },
                                            { text: qsTr("uLaw 1ch 8bit"), value: 1 },
                                            { text: qsTr("LPCM 2ch 16bit"), value: 16 },
                                            { text: qsTr("uLaw 2ch 8bit"), value: 32 },
                                            { text: qsTr("PCM 2ch 8bit"), value: 8 },
                                            { text: qsTr("Opus 1ch"), value: 64 },
                                            { text: qsTr("Opus 2ch"), value: 65 },
                                            { text: qsTr("ADPCM 1ch"), value: 128 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioRXCodecCombo,controller.options["UDP.RxCodec"])
                                                      : -1
                                        onActivated: controller.setOption("UDP.RxCodec",currentValue)
                                        Accessible.name: "Receive Audio Codec Selector"
                                    }

                                    Label { text: qsTr("TX Codec") }
                                    ComboBox {
                                        id: audioTXCodecCombo
                                        enabled: manufacturerCombo.currentValue !== 1
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("LPCM 1ch 16bit"), value: 4 },
                                            { text: qsTr("PCM 1ch 8bit"), value: 2 },
                                            { text: qsTr("uLaw 1ch 8bit"), value: 1 },
                                            { text: qsTr("Opus 1ch"), value: 64 },
                                            { text: qsTr("ADPCM 1ch"), value: 128 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioTXCodecCombo,controller.options["UDP.TxCodec"])
                                                      : -1

                                        onActivated: controller.setOption("UDP.TxCodec",currentValue)
                                        Accessible.name: "Transmit Audio Codec Selector"
                                    }

                                    Label { text: qsTr("Sample Rate") }
                                    ComboBox {
                                        id: audioSampleRateCombo
                                        Accessible.name: "Audio Sample Rate Selector"
                                        enabled: manufacturerCombo.currentValue !== 1
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioSampleRateCombo,controller.options["UDP.SampleRate"])
                                                      : -1

                                        onActivated: controller.setOption("UDP.SampleRate",currentValue)
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("48 kHz"), value: 48000 },
                                            { text: qsTr("24 kHz"), value: 24000 },
                                            { text: qsTr("16 kHz"), value: 16000 },
                                            { text: qsTr("8 kHz"), value: 8000 }
                                        ]
                                    }

                                    Label { text: qsTr("Duplex") }
                                    ComboBox {
                                        id: audioDuplexCombo
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("Full Duplex"), value: 0 },
                                            { text: qsTr("Half Duplex"), value: 1 }
                                        ]
                                        Accessible.name: "Full or Half Duplex Combo"
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(audioDuplexCombo, optBool("UDP.HalfDuplex", false) ? 1 : 0)
                                                      : -1
                                        onActivated: controller.setOption("UDP.HalfDuplex",currentValue)
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }
                        GroupBox {
                            id: groupLatency
                            enabled: controller
                                     && optBool("LAN.EnableLAN", false)

                            title: qsTr("Network latency settings")
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                spacing: 8
                                // Row: latency sliders + codecs
                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("RX Latency (ms)") }
                                    Slider {
                                        WheelArea {}
                                        id: rxLatencySlider
                                        from: 30
                                        to: 500
                                        stepSize: 1
                                        Accessible.name: "RX Latency Buffer Size"
                                        Layout.preferredWidth: 220
                                        value: controller ? controller.options["UDP.RxLatency"] : 0
                                        onMoved:controller.setOption("UDP.RxLatency", value)
                                    }
                                    Label { id: rxLatencyValue; text: rxLatencySlider.value }

                                    Label { text: qsTr("TX Latency (ms)") }
                                    Slider {
                                        WheelArea {}
                                        id: txLatencySlider
                                        from: 30
                                        to: 500
                                        stepSize: 1
                                        Accessible.name: "TX Latency Buffer Size"
                                        Layout.preferredWidth: 220
                                        value: controller ? controller.options["UDP.TxLatency"] : 0
                                        onMoved:controller.setOption("UDP.TxLatency", value)
                                    }
                                    Label { id: txLatencyValue; text: txLatencySlider.value }

                                }
                            }


                        }
                    }
                }

                // =========================
                // Page 1: User Interface
                // =========================
                Item {
                    id: userInterface
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        GroupBox {
                            title: qsTr("Waterfall and Spectrum")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    spacing: 12
                                    CheckBox {
                                        id: wfInterpolateChk
                                        text: qsTr("Interpolate Waterfall")
                                        checked: optBool("Interface.WFInterpolate", false)
                                        onClicked: if (controller) controller.setOption("Interface.WFInterpolate", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Enables interpolation between pixels. Note that this will increase CPU usage.")
                                    }
                                    CheckBox {
                                        id: wfAntiAliasChk;
                                        checked: optBool("Interface.WFAntiAlias", false)
                                        onClicked: if (controller) controller.setOption("Interface.WFAntiAlias", checked)
                                        text: qsTr("Anti-Alias Waterfall")
                                    }
                                    CheckBox {
                                        id: clickDragTuningEnableChk;
                                        checked: optBool("Interface.ClickDragTuningEnable", false)
                                        onClicked: if (controller) controller.setOption("Interface.ClickDragTuningEnable", checked)
                                        text: qsTr("Allow tuning via click and drag (experimental)")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 8
                                    Label { id: underlayLabel; text: qsTr("Underlay Mode") }
                                    ButtonGroup { id: underlayGroup }

                                    property int currentMode: {
                                        if (!controller) return 2
                                        var v = Number(controller.options["Interface.UnderlayMode"])
                                        return (v >= 0 && v <= 2) ? v : 2
                                    }

                                    RadioButton { id: underlayNoneBtn; text: qsTr("None"); checked: parent.currentMode === 0; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: qsTr("No underlay graphics"); onClicked: if (controller) controller.setOption("Interface.UnderlayMode", 0) }
                                    RadioButton { id: underlayPeakBtn; text: qsTr("Peak"); checked: parent.currentMode === 1; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: qsTr("Peak hold with configurable decay"); onClicked: if (controller) controller.setOption("Interface.UnderlayMode", 1) }
                                    RadioButton { id: underlayAverageBtn; text: qsTr("Average"); checked: parent.currentMode === 2; ButtonGroup.group: underlayGroup; ToolTip.visible: hovered; ToolTip.text: qsTr("Average of recent spectrum data"); onClicked: if (controller) controller.setOption("Interface.UnderlayMode", 2) }

                                    Label {
                                        text: parent.currentMode === 1 ? qsTr("Peak Lifetime") : qsTr("Average Count")
                                        visible: parent.currentMode !== 0
                                    }
                                    Slider {
                                        WheelArea {}
                                        id: underlaySlider
                                        visible: parent.currentMode !== 0
                                        enabled: parent.currentMode !== 0
                                        from: parent.currentMode === 1 ? 0 : 2
                                        to: parent.currentMode === 1 ? 100 : 160
                                        value: parent.currentMode === 1
                                            ? (controller ? Number(controller.options["Interface.PeakDecay"]) : 50)
                                            : (controller ? Number(controller.options["Interface.UnderlayBufferSize"]) : 80)
                                        stepSize: 1
                                        Layout.preferredWidth: 100
                                        ToolTip.visible: hovered
                                        ToolTip.text: parent.currentMode === 1
                                            ? qsTr("Lower values decay rapidly; Higher values decay slowly. Maximum setting does not decay.")
                                            : qsTr("Number of spectral frames to average (2-160)")
                                        onMoved: {
                                            if (!controller) return
                                            if (parent.currentMode === 1)
                                                controller.setOption("Interface.PeakDecay", Math.round(value))
                                            else
                                                controller.setOption("Interface.UnderlayBufferSize", Math.round(value))
                                        }
                                    }
                                    CheckBox {
                                        id: showBandsChk
                                        text: qsTr("Show band limits")
                                        checked: optBool("Interface.ShowBands", true)
                                        onClicked: if (controller) controller.setOption("Interface.ShowBands", checked)
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 12
                                    CheckBox {
                                        id: spectrumZeroDbBottomChk
                                        text: qsTr("0dB is bottom")
                                        checked: optBool("Interface.SpectrumZeroDbBottom", false)
                                        onClicked: if (controller) controller.setOption("Interface.SpectrumZeroDbBottom", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Label the spectrum amplitude scale with 0 dB at the bottom. When unchecked, 0 dB is the top of the radio's range and lower values are negative.")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        GroupBox {
                            title: qsTr("Metering")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    spacing: 8
                                    Label { id: secondaryMeterSelectionLabel; text: meterStyleRow.currentStyle > 0 ? qsTr("Meter Primary / Secondary:") : qsTr("Additional Meter Selection:") }
                                    ComboBox {
                                        id: meter2selectionCombo;
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("None"), value: 0 },
                                            { text: qsTr("S+Power"), value: 1 },
                                            { text: qsTr("SWR"), value: 3 },
                                            { text: qsTr("ALC"), value: 5 },
                                            { text: qsTr("Compression"), value: 6 },
                                            { text: qsTr("Voltage"), value: 7 },
                                            { text: qsTr("Current"), value: 8 },
                                            { text: qsTr("Center"), value: 2 },
                                            { text: qsTr("Tx/Rx Audio"), value: 12 },
                                            { text: qsTr("Tx Audio"), value: 10 },
                                            { text: qsTr("Rx Audio"), value: 11 }
                                        ]
                                        currentIndex: controller ? indexFromValue(meter2selectionCombo, controller.options["Interface.Meter2Type"]) : -1
                                        onActivated: if (controller) controller.setOption("Interface.Meter2Type", currentValue)
                                    }
                                    ComboBox {
                                        id: meter3selectionCombo;
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("None"), value: 0 },
                                            { text: qsTr("S+Power"), value: 1 },
                                            { text: qsTr("SWR"), value: 3 },
                                            { text: qsTr("ALC"), value: 5 },
                                            { text: qsTr("Compression"), value: 6 },
                                            { text: qsTr("Voltage"), value: 7 },
                                            { text: qsTr("Current"), value: 8 },
                                            { text: qsTr("Center"), value: 2 },
                                            { text: qsTr("Tx/Rx Audio"), value: 12 },
                                            { text: qsTr("Tx Audio"), value: 10 },
                                            { text: qsTr("Rx Audio"), value: 11 }
                                        ]
                                        currentIndex: controller ? indexFromValue(meter3selectionCombo, controller.options["Interface.Meter3Type"]) : -1
                                        onActivated: if (controller) controller.setOption("Interface.Meter3Type", currentValue)
                                    }
                                    CheckBox {
                                        id: revCompMeterBtn
                                        text: qsTr("Reverse Comp Meter")
                                        checked: optBool("Interface.CompMeterReverse", false)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Broadcast-style reduction meter")
                                        onClicked: if (controller) controller.setOption("Interface.CompMeterReverse", checked)
                                    }
                                    CheckBox {
                                        id: sMeterOnTopChk
                                        text: qsTr("S-Meter on top")
                                        checked: optBool("Interface.SMeterOnTop", false)
                                        onClicked: if (controller) controller.setOption("Interface.SMeterOnTop", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show the S-meter in the receiver title bar instead of beside the frequency display")
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    spacing: 8
                                    CheckBox {
                                        id: largeOptionalMetersChk
                                        text: qsTr("Additional Meters Larger")
                                        checked: optBool("Interface.LargeOptionalMeters", false)
                                        onClicked: if (controller) controller.setOption("Interface.LargeOptionalMeters", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Draw the additional meters larger, with a larger font")
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    id: meterStyleRow
                                    spacing: 8
                                    // 0 Modern, 1 Analog, 2 Analog Cream, 3 LCD, 4 LCD Arc
                                    readonly property int currentStyle: controller ? (Number(controller.options["Interface.MeterStyle"]) || 0) : 0
                                    Label { text: qsTr("Additional meter style:") }
                                    ButtonGroup { id: meterStyleGroup }
                                    Repeater {
                                        model: window.meterStyleChoices
                                        RadioButton {
                                            text: modelData.text
                                            checked: meterStyleRow.currentStyle === modelData.value
                                            ButtonGroup.group: meterStyleGroup
                                            ToolTip.visible: hovered
                                            ToolTip.text: modelData.value === 0 ? qsTr("Two separate additional meters")
                                                                                : qsTr("One vintage meter replaces both additional meters; right-click it to choose what it shows")
                                            onClicked: if (controller) controller.setOption("Interface.MeterStyle", modelData.value)
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    id: popoutMeterRow
                                    spacing: 8
                                    readonly property int currentStyle: {
                                        if (!controller) return 1
                                        const v = controller.options["Interface.PopoutMeterStyle"]
                                        return (v === undefined || v === null || v === "") ? 1 : Number(v)
                                    }
                                    CheckBox {
                                        id: popoutMeterChk
                                        text: qsTr("Pop-out meter")
                                        checked: optBool("Interface.PopoutMeter", false)
                                        onClicked: if (controller) controller.setOption("Interface.PopoutMeter", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show a meter in its own window; right-click it to choose what it shows")
                                    }
                                    ButtonGroup { id: popoutMeterStyleGroup }
                                    Repeater {
                                        model: window.meterStyleChoices
                                        RadioButton {
                                            text: modelData.text
                                            enabled: popoutMeterChk.checked
                                            checked: popoutMeterRow.currentStyle === modelData.value
                                            ButtonGroup.group: popoutMeterStyleGroup
                                            onClicked: if (controller) controller.setOption("Interface.PopoutMeterStyle", modelData.value)
                                        }
                                    }
                                    CheckBox {
                                        text: qsTr("Show secondary VFO meter")
                                        // Receivers beyond the first only exist once a dual-receiver rig's caps are known.
                                        enabled: popoutMeterChk.checked && MainController.receiverCount > 1
                                        checked: optBool("Interface.PopoutSecondaryVfo", false)
                                        onClicked: if (controller) controller.setOption("Interface.PopoutSecondaryVfo", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Add a meter for the second receiver (dual-receiver radios only)")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        GroupBox {
                            title: qsTr("Frequency and Region")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    spacing: 8
                                    Label { text: qsTr("Frequency Display:") }
                                    Label { text: qsTr("Units") }
                                    ComboBox {
                                        id: frequencyUnitsCombo
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: qsTr("None"), value: 0 },
                                            { text: qsTr("Hz"), value: 1 },
                                            { text: qsTr("kHz"), value: 2 },
                                            { text: qsTr("MHz"), value: 3 },
                                            { text: qsTr("GHz"), value: 4 }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(frequencyUnitsCombo,controller.options["Interface.FrequencyUnits"])
                                                      : -1
                                        onActivated: controller.setOption("Interface.FrequencyUnits",currentValue)
                                    }
                                    Label { text: qsTr("Separators:") }
                                    Label { text: qsTr("Decimal") }
                                    ComboBox {
                                        id: decimalSeparatorsCombo;
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "\",\"", value: "," },
                                            { text: "\".\"", value: "." },
                                            { text: "\":\"", value: ":" },
                                            { text: "\" \"", value: " " }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(decimalSeparatorsCombo,controller.options["Interface.DecimalSeparator"])
                                                      : -1

                                        onActivated: controller.setOption("Interface.DecimalSeparator",currentValue)
                                    }
                                    Label { text: qsTr("Groups") }
                                    ComboBox {
                                        id: groupSeparatorsCombo;
                                        textRole: "text"
                                        valueRole: "value"
                                        model: [
                                            { text: "\",\"", value: "," },
                                            { text: "\".\"", value: "." },
                                            { text: "\":\"", value: ":" },
                                            { text: "\" \"", value: " " }
                                        ]
                                        currentIndex: (controller && controller.options)
                                                      ? indexFromValue(groupSeparatorsCombo,controller.options["Interface.GroupSeparator"])
                                                      : -1

                                        onActivated: controller.setOption("Interface.GroupSeparator",currentValue)
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 8
                                    Label { text: qsTr("Font") }
                                    ComboBox {
                                        id: freqFontCombo
                                        Layout.preferredWidth: 200

                                        readonly property string customEntry: qsTr("Custom...")
                                        readonly property string curFont: (controller && controller.options && controller.options["Interface.FreqFont"] !== undefined)
                                                                          ? String(controller.options["Interface.FreqFont"])
                                                                          : ""
                                        readonly property var builtinFonts: controller ? controller.freqFontNames() : []

                                        // Built-in fonts, then the current custom font (if any), then "Custom..."
                                        model: {
                                            var m = builtinFonts.slice()
                                            if (curFont.length > 0 && m.indexOf(curFont) < 0)
                                                m.push(curFont)
                                            m.push(customEntry)
                                            return m
                                        }
                                        currentIndex: model.indexOf(curFont)

                                        onActivated: function(index) {
                                            if (index === model.length - 1)
                                                freqFontDialog.open()
                                            else if (controller)
                                                controller.setOption("Interface.FreqFont", model[index])
                                            // Re-sync with the stored setting (the dialog may be cancelled)
                                            currentIndex = Qt.binding(function() { return freqFontCombo.model.indexOf(freqFontCombo.curFont) })
                                        }

                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Font used for the frequency display. Choose Custom... to pick any installed font.")
                                    }
                                    Item { Layout.fillWidth: true }

                                    FontDialog {
                                        id: freqFontDialog
                                        title: qsTr("Select frequency display font")
                                        onAccepted: if (controller) controller.setOption("Interface.FreqFont", selectedFont.family)
                                    }
                                }
                                RowLayout {
                                    spacing: 8
                                    Label { text: qsTr("Region:") }
                                    TextField {
                                        id: regionTxt
                                        Layout.preferredWidth: 35
                                        Layout.maximumWidth: 35
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        placeholderText: "1"
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("ITU Region. Used to display band limits. (See the QWidget tooltip text in your .ui.)")
                                        text: (controller && controller.options && controller.options["Interface.Region"] !== undefined)
                                              ? controller.options["Interface.Region"]
                                              : ""

                                        onEditingFinished: {
                                            if (text !== controller.options["Interface.Region"])
                                                controller.setOption("Interface.Region", text)
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        GroupBox {
                            title: qsTr("Other")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    spacing: 12
                                    CheckBox {
                                        id: fullScreenChk;
                                        checked: optBool("Interface.UseFullScreen", false)
                                        onClicked: if (controller) controller.setOption("Interface.UseFullScreen", checked)
                                        text: qsTr("Show full screen (F11)")
                                    }
                                    CheckBox {
                                        id: tuningFloorZerosChk
                                        checked: optBool("Controls.NiceTS", true)
                                        onClicked: if (controller) controller.setOption("Controls.NiceTS", checked)

                                        text: qsTr("Digits below tuning step are zero")
                                    }
                                    CheckBox {
                                        id: autoSSBchk
                                        text: qsTr("Auto SSB")
                                        checked: optBool("Controls.AutomaticSidebandSwitching", false)
                                        onClicked: if (controller) controller.setOption("Controls.AutomaticSidebandSwitching", checked)
                                        Accessible.name: "Auto SSB Switching"
                                        Accessible.description: "When using SSB, automatically switch to the standard sideband for a given band."
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("When using SSB, automatically switch to the standard sideband for a given band.")
                                    }
                                    CheckBox {
                                        id: pttEnableChk;
                                        text: qsTr("Enable PTT Controls")
                                        checked: optBool("Controls.EnablePTT", false)
                                        onClicked: if (controller) controller.setOption("Controls.EnablePTT", checked)
                                    }
                                    CheckBox {
                                        id: miniRxButtonsChk
                                        text: qsTr("Mini RX Buttons")
                                        checked: optBool("Interface.MiniRxButtons", true)
                                        onClicked: if (controller) controller.setOption("Interface.MiniRxButtons", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Use compact menu buttons to open the Bands and RX Settings panels")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 12
                                    CheckBox {
                                        id: showTuningDialChk
                                        text: qsTr("Show tuning dial")
                                        checked: optBool("Interface.ShowTuningDial", true)
                                        onClicked: if (controller) controller.setOption("Interface.ShowTuningDial", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show the main tuning knob. Uncheck to recover space in the controls area.")
                                    }
                                    CheckBox {
                                        id: wideRitSliderChk
                                        text: qsTr("Wide RIT slider")
                                        checked: optBool("Interface.WideRitSlider", false)
                                        onClicked: if (controller) controller.setOption("Interface.WideRitSlider", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Replace the RIT knob with a wide, fine-step slider and an exact entry box. Useful for satellite operation.")
                                    }
                                    CheckBox {
                                        id: alwaysSaveOnExitChk
                                        text: qsTr("Always save settings on exit")
                                        checked: optBool("Interface.AlwaysSaveOnExit", false)
                                        onClicked: if (controller) controller.setOption("Interface.AlwaysSaveOnExit", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Save changed settings automatically when wfview exits, without asking.")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 8
                                    ButtonGroup { id: pollingButtonGroup }
                                    RadioButton {
                                        id: autoPollBtn
                                        text: qsTr("AutoPolling")
                                        checked: controller ? Number(controller.options["Radio.PollingMS"]) === 0 : true
                                        ButtonGroup.group: pollingButtonGroup
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("wfview will automatically calculate command polling. Recommended.")
                                        onClicked: if (controller) controller.setOption("Radio.PollingMS", 0)
                                    }
                                    RadioButton {
                                        id: manualPollBtn
                                        text: qsTr("Manual Polling Interval:")
                                        checked: controller ? Number(controller.options["Radio.PollingMS"]) !== 0 : false
                                        ButtonGroup.group: pollingButtonGroup
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Manual (user-defined) command polling")
                                        onClicked: if (controller && Number(controller.options["Radio.PollingMS"]) === 0) controller.setOption("Radio.PollingMS", pollTimeMsSpin.value)
                                    }
                                    SpinBox {
                                        id: pollTimeMsSpin
                                        from: 1
                                        to: 250
                                        value: controller ? Math.max(1, Number(controller.options["Radio.PollingMS"]) || 25) : 25
                                        editable: true
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Sets the polling interval, in ms. Automatic polling is recommended. Serial/USB radios should not poll quicker than about 75ms.")
                                        enabled: manualPollBtn.checked
                                        onValueModified: if (controller && manualPollBtn.checked) controller.setOption("Radio.PollingMS", value)
                                    }
                                    Label { text: qsTr("ms") }
                                    CheckBox {
                                        Layout.leftMargin: 16
                                        id: reverseWheelChk
                                        text: qsTr("Reverse scroll wheel")
                                        checked: optBool("Interface.ReverseWheel", false)
                                        onClicked: if (controller) controller.setOption("Interface.ReverseWheel", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Reverse the mouse-wheel direction when adjusting sliders and dials")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    spacing: 12
                                    CheckBox {
                                        id: largeFreqDisplayChk
                                        text: qsTr("Larger digits and primary meter")
                                        checked: optBool("Interface.LargeFreqDisplay", false)
                                        onClicked: if (controller) controller.setOption("Interface.LargeFreqDisplay", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Make the frequency display and the S/power meter 50% larger")
                                    }
                                    CheckBox {
                                        id: warnAudioDeviceProblemsChk
                                        text: qsTr("Warn about audio device problems")
                                        checked: optBool("Interface.WarnAudioDeviceProblems", true)
                                        onClicked: if (controller) controller.setOption("Interface.WarnAudioDeviceProblems", checked)
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Show a warning when the selected audio input or output device is blank, cannot be found, or cannot be opened.")
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 2: UI Colors
                // =========================
                Item {
                    id: uiColors
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 8
                            CheckBox {
                                id: useSystemThemeChk;
                                checked: optBool("Interface.UseSystemTheme", false)
                                onClicked: if (controller) controller.setOption("Interface.UseSystemTheme", checked)
                                text: qsTr("Use System Theme")
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Color scheme / preset controls
                        RowLayout {
                            spacing: 8
                            Label { text: qsTr("Color scheme") }
                            Label { id: colorPresetLabel; text: qsTr("Preset:") }
                            ComboBox {
                                id: colorPresetCombo
                                Layout.preferredWidth: 90
                                Layout.maximumWidth: 90

                                // show 1..N, but store 0..N-1
                                model: [
                                    { text: "1", value: 0 },
                                    { text: "2", value: 1 },
                                    { text: "3", value: 2 },
                                    { text: "4", value: 3 },
                                    { text: "5", value: 4 }
                                ]
                                textRole: "text"
                                valueRole: "value"

                                // display current selection
                                currentIndex: (controller && controller.options)
                                              ? indexFromValue(colorPresetCombo,controller.options["Interface.CurrentColorPresetNumber"])
                                              : -1

                                // commit user change
                                onActivated: if (controller) controller.setOption("Interface.CurrentColorPresetNumber", model[currentIndex].value)

                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Select a color preset here.")
                            }

                            Label { text: qsTr("Name:") }
                            TextField {
                                id: colorPresetNameField
                                Layout.preferredWidth: 120
                                maximumLength: 10
                                text: controller ? String(controller.options["Color.PresetName"]) : ""
                                selectByMouse: true
                                onEditingFinished: {
                                    if (controller && text.length > 0)
                                        controller.setOption("Color.PresetName", text)
                                    else if (controller)
                                        text = String(controller.options["Color.PresetName"])
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Color editor (scrollable)

                        GroupBox {
                            id: colorEditorGroupBox
                            title: qsTr("User-defined Color Editor")
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 250

                            ScrollView {
                                id: colorEditorScrollArea
                                anchors.fill: parent
                                clip: true

                                GridLayout {
                                    id: gridLayoutColors
                                    columns: 3
                                    columnSpacing: 40
                                    rowSpacing: 6

                                    ColorRow { key: "Color.Window"; label: qsTr("Window"); tooltip: qsTr("General window background color") }
                                    ColorRow { key: "Color.WindowText"; label: qsTr("Window Text"); tooltip: qsTr("General text color on Window background") }
                                    ColorRow { key: "Color.Base"; label: qsTr("Base"); tooltip: qsTr("Background for text input fields and item views") }

                                    ColorRow { key: "Color.AlternateBase"; label: qsTr("Alternate Base"); tooltip: qsTr("Alternate background (e.g., striped table rows)") }
                                    ColorRow { key: "Color.MainText"; label: qsTr("Main Text"); tooltip: qsTr("Text color for Base background (input fields)") }
                                    ColorRow { key: "Color.BrightText"; label: qsTr("Bright Text"); tooltip: qsTr("High-contrast text for dark backgrounds") }

                                    ColorRow { key: "Color.Button"; label: qsTr("Button"); tooltip: qsTr("Button background color") }
                                    ColorRow { key: "Color.ButtonText"; label: qsTr("Button Text"); tooltip: qsTr("Button text color") }
                                    ColorRow { key: "Color.Light"; label: qsTr("Light"); tooltip: qsTr("Lighter edge color for 3D effects") }

                                    ColorRow { key: "Color.MidLight"; label: qsTr("Mid Light"); tooltip: qsTr("Medium-light edge color for 3D effects") }
                                    ColorRow { key: "Color.Dark"; label: qsTr("Dark"); tooltip: qsTr("Darker edge color for 3D effects") }
                                    ColorRow { key: "Color.Mid"; label: qsTr("Mid"); tooltip: qsTr("Medium-dark edge color for 3D effects") }

                                    ColorRow { key: "Color.Shadow"; label: qsTr("Shadow"); tooltip: qsTr("Shadow color (darkest, typically black)") }
                                    ColorRow { key: "Color.Highlight"; label: qsTr("Highlight"); tooltip: qsTr("Selected item background color") }
                                    ColorRow { key: "Color.HighlightedText"; label: qsTr("Highlighted Text"); tooltip: qsTr("Selected item text color") }

                                    ColorRow { key: "Color.Link"; label: qsTr("Link"); tooltip: qsTr("Unvisited hyperlink color") }
                                    ColorRow { key: "Color.LinkVisited"; label: qsTr("Link Visited"); tooltip: qsTr("Visited hyperlink color") }
                                    Item {}

                                    ColorRow { key: "Color.ToolTipBase"; label: qsTr("Tooltip Base"); tooltip: qsTr("Tooltip background color") }
                                    ColorRow { key: "Color.ToolTipText"; label: qsTr("Tooltip Text"); tooltip: qsTr("Tooltip text color") }
                                    ColorRow { key: "Color.Placeholder"; label: qsTr("Placeholder"); tooltip: qsTr("Placeholder text in empty input fields") }

                                    Item { height: 20}
                                    Item {}
                                    Item {}

                                    ColorRow { key: "Color.Grid"; label: qsTr("Grid") }
                                    ColorRow { key: "Color.TuningLine"; label: qsTr("Tuning Line") }
                                    ColorRow { key: "Color.MeterScale"; label: qsTr("Meter Scale") }

                                    ColorRow { key: "Color.Axis"; label: qsTr("Axis") }
                                    ColorRow { key: "Color.Passband"; label: qsTr("Passband") }
                                    ColorRow { key: "Color.MeterText"; label: qsTr("Meter Text") }

                                    ColorRow { key: "Color.Text"; label: qsTr("Text") }
                                    ColorRow { key: "Color.PbtIndicator"; label: qsTr("PBT Indicator") }
                                    ColorRow { key: "Color.WaterfallBack"; label: qsTr("Waterfall Back") }

                                    ColorRow { key: "Color.PlotBackground"; label: qsTr("Plot Background") }
                                    ColorRow { key: "Color.MeterLevel"; label: qsTr("Meter Level") }
                                    ColorRow { key: "Color.WaterfallGrid"; label: qsTr("Waterfall Grid") }

                                    ColorRow { key: "Color.SpectrumLine"; label: qsTr("Spectrum Line") }
                                    ColorRow { key: "Color.MeterAverage"; label: qsTr("Meter Average") }
                                    ColorRow { key: "Color.WaterfallAxis"; label: qsTr("Waterfall Axis") }

                                    ColorRow { key: "Color.SpectrumFill"; label: qsTr("Spectrum Fill") }
                                    ColorRow { key: "Color.MeterPeakLevel"; label: qsTr("Meter Peak Level") }
                                    ColorRow { key: "Color.WaterfallText"; label: qsTr("Waterfall Text") }

                                    CheckBox {
                                        id: useSpectrumFillGradientChk;
                                        text: qsTr("Spectrum Gradient")
                                        checked: optBool("Color.UseSpectrumFillGradient", false)
                                        onClicked: if (controller) controller.setOption("Color.UseSpectrumFillGradient", checked)

                                    }
                                    ColorRow { key: "Color.MeterHighScale"; label: qsTr("Meter High Scale") }
                                    CheckBox {
                                        id: useUnderlayFillGradientChk;
                                        text: qsTr("Underlay Gradient")
                                        checked: optBool("Color.UseUnderlayFillGradient", false)
                                        onClicked: if (controller) controller.setOption("Color.UseUnderlayFillGradient", checked)
                                    }

                                    ColorRow { key: "Color.SpectrumFillTop"; label: qsTr("Spectrum Fill Top") }
                                    ColorRow { key: "Color.UnderlayLine"; label: qsTr("Underlay Line") }
                                    ColorRow { key: "Color.UnderlayFillTop"; label: qsTr("Underlay Fill Top") }

                                    ColorRow { key: "Color.SpectrumFillBot"; label: qsTr("Spectrum Fill Bot") }
                                    ColorRow { key: "Color.UnderlayFill"; label: qsTr("Underlay Fill") }
                                    ColorRow { key: "Color.UnderlayFillBot"; label: qsTr("Underlay Fill Bot") }

                                    ColorRow { key: "Color.ClusterSpots"; label: qsTr("Cluster Spots") }
                                    ColorRow { key: "Color.ClusterSpotLine"; label: qsTr("Cluster Spot Line"); tooltip: qsTr("Line from a cluster spot label down to its frequency") }
                                    ColorRow { key: "Color.ButtonOff"; label: qsTr("Button Off") }
                                    ColorRow { key: "Color.ButtonOn"; label: qsTr("Button On") }
                                    Item {}
                                    Item {}

                                    // Frequency display colors, grouped in a single column
                                    Item { height: 20; Layout.columnSpan: 3 }

                                    ColorRow { key: "Color.FreqBackground"; label: qsTr("Freq Background"); tooltip: qsTr("Frequency display background color") }
                                    Item {}
                                    Item {}

                                    ColorRow { key: "Color.FreqForeground"; label: qsTr("Freq Digits"); tooltip: qsTr("Frequency display digit color") }
                                    Item {}
                                    Item {}

                                    ColorRow { key: "Color.FreqUnits"; label: qsTr("Freq Units"); tooltip: qsTr("Frequency display units text color (e.g. MHz)") }
                                    Item {}
                                    Item {}

                                    ColorRow { key: "Color.FreqInactive"; label: qsTr("Freq Unused Digits"); tooltip: qsTr("Frequency display color for unused leading zeros") }
                                    Item {}
                                    Item {}

                                    ColorRow { key: "Color.FreqHighlight"; label: qsTr("Freq Highlight"); tooltip: qsTr("Frequency display highlight behind the hovered digit and typed entry") }
                                    Item {}
                                    Item {}
                                }
                            }
                        }
                    }
                }

                // =========================
                // Page 3: Radio Settings
                // =========================
                Item {
                    id: radioSettings
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        GroupBox {
                            id: currentModSourceGroup
                            title: qsTr("Current transmit modulation source")
                            Layout.fillWidth: true

                            RowLayout {
                                spacing: 8
                                Label {
                                    id: modInputDataOffComboText
                                    text: qsTr("Data Off Modulation Input:")
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(0)
                                    enabled: window.connStatus === 2
                                }
                                ComboBox {
                                    id: modInputCombo
                                    Accessible.name: "Modulation Input"
                                    Accessible.description: "Transmit modulation source"
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(0)
                                    enabled: visible && window.connStatus === 2
                                    textRole: "text"
                                    valueRole: "value"
                                    model: MainController.modSourceRevision >= 0 ? MainController.modSourceOptions(0) : []
                                    currentIndex: MainController.modSourceRevision >= 0 ? indexFromValue(modInputCombo, MainController.modSourceReg(0)) : -1
                                    onActivated: MainController.setModSource(0, currentValue)
                                }
                                Label {
                                    id: modInputData1ComboText
                                    text: qsTr("(Data Mod Inputs) DATA1:")
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(1)
                                    enabled: window.connStatus === 2
                                }
                                ComboBox {
                                    id: modInputData1Combo
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(1)
                                    enabled: visible && window.connStatus === 2
                                    textRole: "text"
                                    valueRole: "value"
                                    model: MainController.modSourceRevision >= 0 ? MainController.modSourceOptions(1) : []
                                    currentIndex: MainController.modSourceRevision >= 0 ? indexFromValue(modInputData1Combo, MainController.modSourceReg(1)) : -1
                                    onActivated: MainController.setModSource(1, currentValue)
                                }
                                Label {
                                    id: modInputData2ComboText
                                    text: qsTr("DATA2:")
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(2)
                                    enabled: window.connStatus === 2
                                }
                                ComboBox {
                                    id: modInputData2Combo
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(2)
                                    enabled: visible && window.connStatus === 2
                                    textRole: "text"
                                    valueRole: "value"
                                    model: MainController.modSourceRevision >= 0 ? MainController.modSourceOptions(2) : []
                                    currentIndex: MainController.modSourceRevision >= 0 ? indexFromValue(modInputData2Combo, MainController.modSourceReg(2)) : -1
                                    onActivated: MainController.setModSource(2, currentValue)
                                }
                                Label {
                                    id: modInputData3ComboText
                                    text: qsTr("DATA3:")
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(3)
                                    enabled: window.connStatus === 2
                                }
                                ComboBox {
                                    id: modInputData3Combo
                                    visible: MainController.modSourceRevision >= 0 && MainController.modSourceSupported(3)
                                    enabled: visible && window.connStatus === 2
                                    textRole: "text"
                                    valueRole: "value"
                                    model: MainController.modSourceRevision >= 0 ? MainController.modSourceOptions(3) : []
                                    currentIndex: MainController.modSourceRevision >= 0 ? indexFromValue(modInputData3Combo, MainController.modSourceReg(3)) : -1
                                    onActivated: MainController.setModSource(3, currentValue)
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        GroupBox {
                            id: onConnectGroup
                            title: qsTr("On Connect")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    spacing: 8
                                    CheckBox {
                                        id: connectModSrcChk
                                        text: qsTr("Set transmit mod input sources on connect")
                                        checked: optBool("Radio.SetModSrcOnConnect", false)
                                        onClicked: if (controller) controller.setOption("Radio.SetModSrcOnConnect", checked)
                                    }
                                    ModSourcePref {
                                        label: qsTr("Data Off:")
                                        key: "Radio.ConnectDataOffModSrc"
                                        dataMode: 0
                                        active: connectModSrcChk.checked
                                    }
                                    ModSourcePref {
                                        label: qsTr("Data On:")
                                        key: "Radio.ConnectDataOnModSrc"
                                        dataMode: 1
                                        active: connectModSrcChk.checked
                                    }
                                    Label {
                                        text: qsTr("(connect to a radio to choose sources)")
                                        visible: window.connStatus !== 2
                                        opacity: 0.7
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                CheckBox {
                                    id: setRadioTimeChk
                                    text: qsTr("Set radio time on connect (takes up to a minute)")
                                    checked: optBool("Radio.SetRadioTime", false)
                                    onClicked: if (controller) controller.setOption("Radio.SetRadioTime", checked)
                                }

                                MacroPref {
                                    label: qsTr("Execute macro")
                                    enableKey: "Radio.RunMacroOnConnect"
                                    idKey: "Radio.ConnectMacroId"
                                }
                            }
                        }

                        GroupBox {
                            id: onDisconnectGroup
                            title: qsTr("On Disconnect")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    spacing: 8
                                    CheckBox {
                                        id: disconnectModSrcChk
                                        text: qsTr("Set transmit mod input sources on disconnect")
                                        checked: optBool("Radio.SetModSrcOnDisconnect", false)
                                        onClicked: if (controller) controller.setOption("Radio.SetModSrcOnDisconnect", checked)
                                    }
                                    ModSourcePref {
                                        label: qsTr("Data Off:")
                                        key: "Radio.DisconnectDataOffModSrc"
                                        dataMode: 0
                                        active: disconnectModSrcChk.checked
                                    }
                                    ModSourcePref {
                                        label: qsTr("Data On:")
                                        key: "Radio.DisconnectDataOnModSrc"
                                        dataMode: 1
                                        active: disconnectModSrcChk.checked
                                    }
                                    Label {
                                        text: qsTr("(connect to a radio to choose sources)")
                                        visible: window.connStatus !== 2
                                        opacity: 0.7
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                MacroPref {
                                    label: qsTr("Execute macro")
                                    enableKey: "Radio.RunMacroOnDisconnect"
                                    idKey: "Radio.DisconnectMacroId"
                                }
                            }
                        }

                        RowLayout {
                            spacing: 8
                            Button {
                                id: setClockBtn
                                text: qsTr("Set Clock")
                                enabled: window.connStatus === 2
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Set the radio clock at the next minute boundary.")
                                onClicked: MainController.syncRadioClock()
                            }
                            CheckBox {
                                id: useUTCChk
                                text: qsTr("Use UTC")
                                checked: optBool("Radio.UseUTC", false)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Set radio clock to UTC; otherwise uses local timezone.")
                                onClicked: {
                                    if (controller)
                                        controller.setOption("Radio.UseUTC", checked)
                                    if (window.connStatus === 2)
                                        MainController.syncRadioClock()
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                id: satOpsBtn
                                text: qsTr("Satellite Ops")
                                visible: false
                            }
                            Button {
                                id: adjRefBtn
                                text: qsTr("Adjust Reference")
                                visible: false
                            }
                        }

                        RowLayout {
                            spacing: 12
                            CheckBox {
                                id: forceVfoModeChk;
                                text: qsTr("Force VFO Mode");
                                checked: optBool("Interface.ForceVfoMode", true)
                                onClicked: if (controller) controller.setOption("Interface.ForceVfoMode", checked)
                            }
                            CheckBox {
                                id: autoPowerOnChk;
                                text: qsTr("Auto Power-on radio");
                                checked: optBool("Interface.AutoPowerOn", true)
                                onClicked: if (controller) controller.setOption("Interface.AutoPowerOn", checked)
                            }
                            Item { Layout.fillWidth: true }
                        }

                        GroupBox {
                            id: pttGroup
                            title: qsTr("PTT")
                            Layout.fillWidth: true

                            RowLayout {
                                anchors.fill: parent
                                spacing: 8
                                Label { text: qsTr("Manual PTT Toggle"); enabled: window.connStatus === 2 }
                                Button { id: pttOnBtn; text: qsTr("PTT On"); enabled: window.connStatus === 2; onClicked: MainController.setTransmit(true) }
                                Button { id: pttOffBtn; text: qsTr("PTT Off"); enabled: window.connStatus === 2; onClicked: MainController.setTransmit(false) }
                                CheckBox {
                                    id: pttTimeoutChk
                                    Layout.leftMargin: 16
                                    text: qsTr("PTT Timeout")
                                    checked: optBool("Controls.PTTTimeoutEnabled", true)
                                    onClicked: if (controller) controller.setOption("Controls.PTTTimeoutEnabled", checked)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Sets a timer (within wfview) for the maximum transmission length.\nNot actually a radio setting.")
                                }
                                SpinBox {
                                    id: pttTimeoutSpin
                                    from: 30
                                    to: 999
                                    value: controller ? (Number(controller.options["Controls.PTTTimeoutSeconds"]) || 300) : 300
                                    editable: true
                                    enabled: pttTimeoutChk.checked
                                    Accessible.name: "PTT Timeout Seconds"
                                    onValueModified: if (controller) controller.setOption("Controls.PTTTimeoutSeconds", value)
                                }
                                Label { text: qsTr("seconds"); enabled: pttTimeoutChk.checked }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 4: Radio Server
                // =========================
                Item {
                    id: radioServer
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 12
                            CheckBox {
                                id: serverEnableCheckbox
                                text: qsTr("Enable")
                                checked: optBool("Server.Enabled", false)
                                onClicked: if (controller) controller.setOption("Server.Enabled", checked)
                            }
                            Item { Layout.preferredWidth: 20 }
                            CheckBox {
                                id: serverDisableUIChk
                                text: qsTr("Disable local user controls when in use (restart required)")
                                checked: optBool("Server.DisableUI", false)
                                onClicked: if (controller) controller.setOption("Server.DisableUI", checked)
                            }
                            Item { Layout.preferredWidth: 20 }
                            CheckBox {
                                id: serverSuppressSerialWarningsChk
                                text: qsTr("Suppress serial port warnings")
                                enabled: optBool("Server.Enabled", false)
                                checked: optBool("Server.SuppressSerialWarnings", false)
                                onClicked: if (controller) controller.setOption("Server.SuppressSerialWarnings", checked)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("While the server is enabled, do not show a warning when the radio's serial port cannot be opened. "
                                                   + "wfview keeps retrying the port, for headless or unattended servers.")
                            }
                            Item { Layout.fillWidth: true }
                        }

                        GroupBox {
                            id: serverSetupGroup
                            title: qsTr("Server Setup")
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                spacing: 10

                                // ports row
                                RowLayout {
                                    spacing: 8

                                    Label { id: serverControlPortLabel; text: qsTr("Control Port") }
                                    TextField {
                                        id: serverControlPortText
                                        text: controller ? String(controller.options["Server.ControlPort"]) : "50001"
                                        Layout.preferredWidth: 90
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                        onEditingFinished: commitIntegerOption("Server.ControlPort", serverControlPortText, 1, 65535)
                                    }

                                    Label {
                                        id: serverCATPortLabel
                                        text: manufacturerCombo.currentValue === 0 ? qsTr("CI-V Port") : qsTr("CAT Port")
                                        visible: manufacturerCombo.currentValue !== 1
                                    }
                                    TextField {
                                        id: serverCivPortText
                                        visible: manufacturerCombo.currentValue !== 1
                                        text: controller ? String(controller.options["Server.CivPort"]) : "50002"
                                        Layout.preferredWidth: 90
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                        onEditingFinished: commitIntegerOption("Server.CivPort", serverCivPortText, 1, 65535)
                                    }

                                    Label { id: serverAudioPortLabel; text: qsTr("Audio Port") }
                                    TextField {
                                        id: serverAudioPortText
                                        text: controller ? String(controller.options["Server.AudioPort"]) : "50003"
                                        Layout.preferredWidth: 90
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                        onEditingFinished: commitIntegerOption("Server.AudioPort", serverAudioPortText, 1, 65535)
                                    }

                                    Label {
                                        id: serverScopePortLabel
                                        text: qsTr("Scope Port")
                                        visible: manufacturerCombo.currentValue === 2
                                    }
                                    TextField {
                                        id: serverScopePortText
                                        visible: manufacturerCombo.currentValue === 2
                                        text: controller ? String(controller.options["Server.ScopePort"]) : "50004"
                                        Layout.preferredWidth: 90
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                        onEditingFinished: commitIntegerOption("Server.ScopePort", serverScopePortText, 1, 65535)
                                    }

                                    Item { Layout.fillWidth: true }
                                }

                                RowLayout {
                                    spacing: 8

                                    Label { text: qsTr("Listen Interface") }
                                    ComboBox {
                                        id: serverListenAddressCombo
                                        model: controller ? controller.serverListenAddressOptions() : []
                                        textRole: "text"
                                        valueRole: "value"
                                        currentIndex: controller ? indexFromValue(serverListenAddressCombo, controller.options["Server.ListenAddress"]) : -1
                                        enabled: window.connStatus !== 2
                                        Layout.preferredWidth: 340
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Select the interface used by the built-in UDP server.")
                                        onActivated: if (controller) controller.setOption("Server.ListenAddress", currentValue)
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Automatic prefers a non-loopback IPv4 address, then falls back to IPv6.")
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                // server audio routing row
                                RowLayout {
                                    spacing: 8
                                    Label { text: qsTr("RX Audio Input") }
                                    ComboBox {
                                        id: serverRXAudioInputCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioInputs"] : null
                                        model: audioDeviceModel(spec, controller ? controller.options["Server.AudioInput"] : "")
                                        textRole: spec ? spec["textRole"] : "text"
                                        valueRole: spec ? spec["valueRole"] : "value"
                                        currentIndex: controller ? indexFromValue(serverRXAudioInputCombo, controller.options["Server.AudioInput"]) : -1
                                        enabled: window.connStatus !== 2
                                        Layout.preferredWidth: 160
                                        onActivated: selectDeviceOption(serverRXAudioInputCombo, "Server.AudioInput")
                                    }
                                    Label { text: qsTr("TX Audio Output") }
                                    ComboBox {
                                        id: serverTXAudioOutputCombo
                                        readonly property var spec: controller ? controller.uiSpecs["audioOutputs"] : null
                                        model: audioDeviceModel(spec, controller ? controller.options["Server.AudioOutput"] : "")
                                        textRole: spec ? spec["textRole"] : "text"
                                        valueRole: spec ? spec["valueRole"] : "value"
                                        currentIndex: controller ? indexFromValue(serverTXAudioOutputCombo, controller.options["Server.AudioOutput"]) : -1
                                        enabled: window.connStatus !== 2
                                        Layout.preferredWidth: 160
                                        onActivated: selectDeviceOption(serverTXAudioOutputCombo, "Server.AudioOutput")
                                    }
                                    Item { width: 20 }
                                    Label { text: qsTr("Audio System") }
                                    ComboBox {
                                        id: audioSystemServerCombo
                                        model: [
                                            { text: qsTr("Qt Audio"), value: 0 },
                                            { text: qsTr("PortAudio"), value: 1 },
                                            { text: qsTr("RT Audio"), value: 2 }
                                        ]
                                        textRole: "text"
                                        valueRole: "value"
                                        currentIndex: controller ? indexFromValue(audioSystemServerCombo, controller.options["Radio.AudioSystem"]) : -1
                                        enabled: window.connStatus !== 2
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Server uses the global audio system setting.")
                                        onActivated: if (controller) controller.setOption("Radio.AudioSystem", currentValue)
                                    }
                                    Item { Layout.fillWidth: true }
                                }

                                // users table
                                GroupBox {
                                    title: qsTr("Users")
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 260

                                    Item {
                                        id: serverUsersRoot
                                        anchors.fill: parent
                                        readonly property var usersModel: controller ? controller.serverUsersModel : null
                                        property int currentRow: -1
                                        readonly property int wUser: 180
                                        readonly property int wPass: 180
                                        readonly property int wType: 180
                                        readonly property int wStatus: 120
                                        readonly property int rowH: 34
                                        readonly property int pad: 6

                                        ColumnLayout {
                                            anchors.fill: parent
                                            spacing: 8

                                            RowLayout {
                                                Layout.fillWidth: true
                                                Button {
                                                    text: qsTr("Add")
                                                    enabled: serverUsersRoot.usersModel !== null
                                                    onClicked: serverUsersRoot.usersModel.addEntry("", "", 1)
                                                }
                                                Button {
                                                    text: qsTr("Remove")
                                                    enabled: serverUsersRoot.usersModel !== null && serverUsersRoot.currentRow >= 0
                                                    onClicked: {
                                                        serverUsersRoot.usersModel.removeEntry(serverUsersRoot.currentRow)
                                                        serverUsersRoot.currentRow = -1
                                                    }
                                                }
                                                Item { Layout.fillWidth: true }
                                            }

                                            Rectangle {
                                                Layout.fillWidth: true
                                                height: serverUsersRoot.rowH
                                                color: palette.mid
                                                border.color: palette.dark

                                                Row {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: serverUsersRoot.pad
                                                    anchors.rightMargin: serverUsersRoot.pad
                                                    spacing: serverUsersRoot.pad

                                                    Label { width: serverUsersRoot.wUser; height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Username"); font.bold: true }
                                                    Label { width: serverUsersRoot.wPass; height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Password"); font.bold: true }
                                                    Label { width: serverUsersRoot.wType; height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Access"); font.bold: true }
                                                    Label { width: serverUsersRoot.wStatus; height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Status"); font.bold: true }
                                                }
                                            }

                                            ScrollView {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                clip: true

                                                contentItem: ListView {
                                                    id: serverUsersTable
                                                    width: parent.width
                                                    model: serverUsersRoot.usersModel
                                                    clip: true
                                                    boundsBehavior: Flickable.StopAtBounds

                                                    delegate: Rectangle {
                                                        width: serverUsersTable.width
                                                        height: serverUsersRoot.rowH
                                                        color: (index === serverUsersRoot.currentRow) ? palette.highlight : ((index % 2) ? palette.alternateBase : palette.base)
                                                        border.color: palette.mid

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            onClicked: serverUsersRoot.currentRow = index
                                                        }

                                                        Row {
                                                            anchors.fill: parent
                                                            anchors.leftMargin: serverUsersRoot.pad
                                                            anchors.rightMargin: serverUsersRoot.pad
                                                            spacing: serverUsersRoot.pad

                                                            TextField {
                                                                width: serverUsersRoot.wUser
                                                                height: parent.height - 6
                                                                anchors.verticalCenter: parent.verticalCenter
                                                                text: model.username
                                                                selectByMouse: true
                                                                onPressed: serverUsersRoot.currentRow = index
                                                                onActiveFocusChanged: if (activeFocus) serverUsersRoot.currentRow = index
                                                                onTextEdited: serverUsersRoot.usersModel.setUsername(index, text)
                                                                onEditingFinished: serverUsersRoot.usersModel.setUsername(index, text)
                                                            }

                                                            TextField {
                                                                width: serverUsersRoot.wPass
                                                                height: parent.height - 6
                                                                anchors.verticalCenter: parent.verticalCenter
                                                                echoMode: TextInput.PasswordEchoOnEdit
                                                                text: model.password
                                                                // Stored passwords are encoded, so they are not shown.
                                                                // Typing here replaces the stored password.
                                                                placeholderText: model.hasSavedPassword ? qsTr("(saved)") : ""
                                                                selectByMouse: true
                                                                onPressed: serverUsersRoot.currentRow = index
                                                                onActiveFocusChanged: if (activeFocus) serverUsersRoot.currentRow = index
                                                                onTextEdited: serverUsersRoot.usersModel.setPassword(index, text)
                                                            }

                                                            ComboBox {
                                                                id: serverUserTypeCombo
                                                                width: serverUsersRoot.wType
                                                                height: parent.height - 6
                                                                anchors.verticalCenter: parent.verticalCenter
                                                                textRole: "text"
                                                                valueRole: "value"
                                                                model: [
                                                                    { text: qsTr("Admin User"), value: 0 },
                                                                    { text: qsTr("Normal User"), value: 1 },
                                                                    { text: qsTr("Normal with no TX"), value: 2 },
                                                                    { text: qsTr("Disabled"), value: 3 }
                                                                ]
                                                                currentIndex: indexFromValue(serverUserTypeCombo, userType)
                                                                onActiveFocusChanged: if (activeFocus) serverUsersRoot.currentRow = index
                                                                onActivated: {
                                                                    serverUsersRoot.currentRow = index
                                                                    serverUsersRoot.usersModel.setUserType(index, currentValue)
                                                                }
                                                            }

                                                            Label {
                                                                width: serverUsersRoot.wStatus
                                                                height: parent.height
                                                                verticalAlignment: Text.AlignVCenter
                                                                elide: Text.ElideRight
                                                                text: model.userStatusName
                                                                font.bold: model.userStatus === 1
                                                                Accessible.name: qsTr("Status: %1").arg(text)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }


                                Label {
                                    id: serverDisconnectLabel
                                    text: window.connStatus === 0
                                          ? qsTr("Press \"Connect to Radio\" before starting the server")
                                          : qsTr("To make changes to the server settings, first press \"Disconnect from Radio\"")
                                    wrapMode: Text.WordWrap
                                }

                                Item { Layout.fillHeight: true }
                            }
                        }

                        GroupBox {
                            id: serverStatusGroup
                            title: qsTr("Server Status")
                            Layout.fillWidth: true

                            GridLayout {
                                columns: 2
                                columnSpacing: 12
                                rowSpacing: 4
                                width: parent.width

                                Label { text: qsTr("Status:") }
                                Label {
                                    id: serverStatusLabel
                                    Layout.fillWidth: true
                                    text: MainController.serverStatusText
                                    font.bold: MainController.serverRunning
                                    wrapMode: Text.WordWrap
                                }

                                Label { text: qsTr("Users logged in:") }
                                Label { text: String(MainController.serverUsersLoggedIn) }

                                Label { text: qsTr("Logins since start:") }
                                Label { text: String(MainController.serverTotalLogins) }
                            }
                        }
                    }
                }

                // =========================
                // Page 5: External Control
                // =========================
                Item {
                    id: externalControl
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        RowLayout {
                            spacing: 8
                            CheckBox {
                                id: enableRigctldChk
                                text: qsTr("Enable RigCtld")
                                checked: optBool("LAN.EnableRigCtlD", false)
                                onClicked: if (controller) controller.setOption("LAN.EnableRigCtlD", checked)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("wfview can run a rigctld-compatible server, which provides basic information to third-party tools such as loggers and digital mode applications.\nwfview's rigctld-compatible server can accept multiple connections at the same time.")
                                Accessible.name: "Enable RIGCTLD checkbox"
                            }
                            Item { width: 20 }
                            Label {
                                text: qsTr("Port")
                                HoverHandler { id: rigctldPortLabelHover }
                                ToolTip.visible: rigctldPortLabelHover.hovered
                                ToolTip.text: rigctldPortTxt.ToolTip.text
                            }
                            TextField {
                                id: rigctldPortTxt
                                text: controller ? String(controller.options["LAN.RigCtlPort"]) : ""
                                Layout.preferredWidth: 75
                                Layout.maximumWidth: 75
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 1; top: 65535 }
                                onEditingFinished: commitIntegerOption("LAN.RigCtlPort", rigctldPortTxt, 1, 65535)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("The default rigctld port for most applications is 4532.\nwfview recommends using 4533 so as not to conflict with any other rigctld you may already be using.")
                                Accessible.name: "RIGCTLD server port"
                            }
                            Item { width: 20 }
                            CheckBox {
                                id: rigctldIntegerFreqsChk
                                text: qsTr("Report frequencies as integers")
                                checked: optBool("LAN.RigCtlIntegerFreqs", false)
                                onClicked: if (controller) controller.setOption("LAN.RigCtlIntegerFreqs", checked)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Alters the response of wfview's rigctld server to the 'f', 'i' and 'k' commands, reporting frequencies as integers rather than as numbers with decimals")
                                Accessible.name: "Report rigctld frequencies as integers checkbox"
                            }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { id: vspLabel; text: qsTr("Virtual Serial Port") }
                            ComboBox {
                                id: vspCombo
                                Layout.preferredWidth: 250
                                Layout.maximumWidth: 250
                                Accessible.name: "Virtual Serial Port Selector"
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Define a virtual serial port. On Windows: loopback device for other programs. On Linux/macOS: pseudo-terminal.")
                                readonly property var spec: controller ? controller.uiSpecs["VirtualSerialPorts"] : null
                                readonly property string savedValue: controller ? String(controller.options["Radio.VirtualSerialPort"]) : "none"
                                editable: spec && spec.editable !== undefined ? spec.editable : true
                                model: spec ? spec.model : []
                                textRole: spec ? spec.textRole : "text"
                                valueRole: spec ? spec.valueRole : "value"
                                currentIndex: controller ? indexFromValue(vspCombo, savedValue) : -1
                                editText: currentIndex >= 0 ? currentText : savedValue
                                onAccepted: if (controller) controller.setOption("Radio.VirtualSerialPort", editText.length ? editText : "none")
                                onActivated: if (controller) controller.setOption("Radio.VirtualSerialPort", currentValue !== undefined && currentValue !== null ? currentValue : "none")
                            }
                            Label {
                                id: ptyDeviceLabel
                                text: vspCombo.spec && vspCombo.spec.hint ? vspCombo.spec.hint : qsTr("Use \"none\" to disable the virtual serial bridge.")
                            }
                            CheckBox {
                                id: vspQueueCheck
                                text: qsTr("Use command queue")
                                checked: optBool("Radio.VirtualSerialPortUseQueue", false)
                                enabled: vspCombo.savedValue.toLowerCase() !== "none"
                                onClicked: if (controller) controller.setOption("Radio.VirtualSerialPortUseQueue", checked)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Route virtual serial CAT commands through wfview's command queue where possible.")
                            }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: qsTr("TCP Server Port") }
                            TextField {
                                id: tcpServerPortTxt
                                text: controller ? String(controller.options["LAN.TCPPort"]) : ""
                                Layout.preferredWidth: 80
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 0; top: 65535 }
                                onEditingFinished: commitIntegerOption("LAN.TCPPort", tcpServerPortTxt, 0, 65535)
                            }
                            Label { text: qsTr("Enter port for TCP server, 0 = disabled (restart required if changed)") }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: qsTr("TCI Server Port") }
                            TextField {
                                id: tciServerPortTxt
                                text: controller ? String(controller.options["LAN.TCIPort"]) : ""
                                Layout.preferredWidth: 80
                                inputMethodHints: Qt.ImhDigitsOnly
                                validator: IntValidator { bottom: 0; top: 65535 }
                                onEditingFinished: commitIntegerOption("LAN.TCIPort", tciServerPortTxt, 0, 65535)
                            }
                            Label { text: qsTr("Enter port for TCI server, 0 = disabled") }
                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            spacing: 8
                            Label { text: qsTr("Waterfall Format") }
                            ComboBox {
                                id: waterfallFormatCombo
                                model: [
                                    { text: qsTr("Default"), value: 0 },
                                    { text: qsTr("Single (network)"), value: 1 },
                                    { text: qsTr("Multi (serial)"), value: 2 }
                                ]
                                textRole: "text"
                                valueRole: "value"
                                currentIndex: controller ? indexFromValue(waterfallFormatCombo, controller.options["LAN.WaterfallFormat"]) : -1
                                onActivated: if (controller) controller.setOption("LAN.WaterfallFormat", currentValue)
                            }
                            Label { text: qsTr("Only change this if you are absolutely sure you need it (connecting to N1MM+ or similar)") }
                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 6: Controllers
                // =========================
                Item {
                    id: controllers
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        RowLayout {
                            spacing: 8
                            CheckBox {
                                id: enableUsbChk
                                text: qsTr("Enable USB Controllers")
                                checked: optBool("Controls.EnableUSBControllers", false)
                                onClicked: if (controller) controller.setOption("Controls.EnableUSBControllers", checked)
                                Accessible.name: "Enable USB Controllers Checkbox"
                            }
                            Item { width: 20 }
                            Button {
                                id: usbControllersReset
                                text: qsTr("Reset Buttons")
                                enabled: enableUsbChk.checked
                                Accessible.name: "Reset USB Controllers Button"
                                onClicked: usbResetConfirm.open()
                            }
                            Label {
                                id: usbResetLbl
                                visible: enableUsbChk.checked
                                text: qsTr("Only reset buttons/commands if you have issues.")
                            }
                            Item { Layout.fillWidth: true }
                        }
                        // Add the controller settings
                        ControllerSettings {
                            enabled: enableUsbChk.checked
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            id: controllerSettings
                            controllerController: controller ? controller.controllerController : null
                        }
                    }
                }
                // =========================
                // Page 7: DX Cluster
                // =========================
                Item {
                    id: clusterPage
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10

                        Label {
                            text: qsTr("This page contains configuration for DX Cluster, either UDP style broadcast (from N1MM+/DXlog) or TCP connection to your favourite cluster")
                            wrapMode: Text.WordWrap
                        }

                        GroupBox {
                            id: clusterTcpGroup
                            title: qsTr("Enable Cluster Connections (TCP)")
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            // Don't let the UDP section below squash the server table.
                            Layout.minimumHeight: implicitHeight - root.rowH

                            label: CheckBox {
                                id: clusterTcpEnable
                                text: qsTr("Enable Cluster Connections (TCP)")
                                checked: optBool("Cluster.TcpEnabled", false)
                                onClicked: {
                                    if (!controller)
                                        return
                                    var row = root.currentRow >= 0 ? root.currentRow : root.clusterModel.defaultRow()
                                    if (row >= 0)
                                        controller.selectClusterRow(row)
                                    controller.setOption("Cluster.TcpEnabled", checked)
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 12

                                Item {
                                    id: root
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    // The table is anchored inside this Item, so pass its size up.
                                    implicitHeight: clusterTableColumn.implicitHeight
                                    // Preferred shows 4 server rows; allow shrinking to 3.
                                    Layout.minimumHeight: clusterTableColumn.implicitHeight - rowH
                                    readonly property var clusterModel: controller ? controller.clusterModel : null
                                    property int currentRow: -1

                                    function selectRow(r) { root.currentRow = r }

                                    // Column widths (easy to tweak)
                                    readonly property int wServer: 220
                                    readonly property int wPort: 90
                                    readonly property int wUser: 130
                                    readonly property int wPass: 130
                                    readonly property int wTimeout: 140
                                    readonly property int wDef: 70
                                    readonly property int wStatus: 190
                                    readonly property int rowH: 34
                                    readonly property int pad: 6

                                    ColumnLayout {
                                        id: clusterTableColumn
                                        anchors.fill: parent
                                        spacing: 8

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Button {
                                                text: qsTr("Add")
                                                enabled: root.clusterModel !== null
                                                onClicked: {
                                                    root.clusterModel.addEntry("localhost", 7300, "", "", 60, false)
                                                    root.currentRow = root.clusterModel.count() - 1
                                                }
                                            }
                                            Button {
                                                text: qsTr("Remove")
                                                enabled: root.clusterModel !== null && root.currentRow >= 0
                                                onClicked: {
                                                    root.clusterModel.removeEntry(root.currentRow)
                                                    root.currentRow = -1
                                                }
                                            }
                                            Button {
                                                text: qsTr("Connect")
                                                enabled: root.clusterModel !== null && root.clusterModel.count() > 0
                                                onClicked: {
                                                    var row = root.currentRow >= 0 ? root.currentRow : root.clusterModel.defaultRow()
                                                    if (row >= 0) {
                                                        root.currentRow = row
                                                        controller.selectClusterRow(row)
                                                        MainController.connectCluster()
                                                    }
                                                }
                                            }
                                            Button {
                                                text: qsTr("Disconnect")
                                                onClicked: MainController.disconnectCluster()
                                            }
                                            Item { Layout.fillWidth: true }
                                        }

                                        // Header (DON'T use RowLayout here; use Row + fixed widths for predictability)
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.minimumHeight: root.rowH
                                            height: root.rowH
                                            color: "#111"
                                            border.color: "#333"

                                            Row {
                                                anchors.fill: parent
                                                anchors.leftMargin: root.pad
                                                anchors.rightMargin: root.pad
                                                spacing: root.pad

                                                Label { width: root.wServer;  height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Server");  font.bold: true; color: "white" }
                                                Label { width: root.wPort;    height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Port");    font.bold: true; color: "white" }
                                                Label { width: root.wUser;    height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("User");    font.bold: true; color: "white" }
                                                Label { width: root.wPass;    height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Password");font.bold: true; color: "white" }
                                                Label {
                                                    width: root.wTimeout; height: parent.height; verticalAlignment: Text.AlignVCenter
                                                    text: qsTr("Spot Lifetime (sec)"); font.bold: true; color: "white"
                                                    HoverHandler { id: clusterLifetimeHeaderHover }
                                                    FitToolTip {
                                                        visible: clusterLifetimeHeaderHover.hovered
                                                        text: qsTr("How many seconds a spot is displayed")
                                                    }
                                                }
                                                Label {
                                                    width: root.wDef; height: parent.height; verticalAlignment: Text.AlignVCenter
                                                    text: qsTr("Activate"); font.bold: true; color: "white"
                                                    HoverHandler { id: clusterActivateHeaderHover }
                                                    FitToolTip {
                                                        visible: clusterActivateHeaderHover.hovered
                                                        text: qsTr("Check the one server you wish to connect to\nAll other servers listed are not connected to,\nbut are recorded here for your convenience")
                                                    }
                                                }
                                                Label { width: root.wStatus;  height: parent.height; verticalAlignment: Text.AlignVCenter; text: qsTr("Status");  font.bold: true; color: "white" }
                                            }
                                        }

                                        ScrollView {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            // Always show a few servers; more scroll.
                                            Layout.minimumHeight: root.rowH * 3
                                            Layout.preferredHeight: root.rowH * 4
                                            clip: true

                                            // Critical for Qt5: give the content item a width
                                            contentItem: ListView {
                                                id: list
                                                width: parent.width   // forces stable column layout
                                                model: root.clusterModel
                                                clip: true
                                                boundsBehavior: Flickable.StopAtBounds

                                                delegate: Rectangle {
                                                    width: list.width
                                                    height: root.rowH
                                                    color: (index === root.currentRow) ? "#2a2a2a" : ((index % 2) ? "#1f1f1f" : "#171717")
                                                    border.color: "#333"

                                                    MouseArea {
                                                        anchors.fill: parent
                                                        onClicked: root.currentRow = index
                                                    }

                                                    Row {
                                                        anchors.fill: parent
                                                        anchors.leftMargin: root.pad
                                                        anchors.rightMargin: root.pad
                                                        spacing: root.pad

                                                        TextField {
                                                            width: root.wServer
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            text: model.server
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onTextEdited: root.clusterModel.setServer(index, text)
                                                            onEditingFinished: root.clusterModel.setServer(index, text)
                                                        }

                                                        TextField {
                                                            width: root.wPort
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            inputMethodHints: Qt.ImhDigitsOnly
                                                            validator: IntValidator { bottom: 1; top: 65535 }
                                                            text: String(model.port)
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onTextEdited: if (acceptableInput) root.clusterModel.setPort(index, parseInt(text))
                                                            onEditingFinished: root.clusterModel.setPort(index, parseInt(text))
                                                        }

                                                        TextField {
                                                            width: root.wUser
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            text: model.userName
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onTextEdited: root.clusterModel.setUserName(index, text)
                                                            onEditingFinished: root.clusterModel.setUserName(index, text)
                                                        }

                                                        TextField {
                                                            width: root.wPass
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            echoMode: TextInput.Password
                                                            text: model.password
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onTextEdited: root.clusterModel.setPassword(index, text)
                                                            onEditingFinished: root.clusterModel.setPassword(index, text)
                                                        }

                                                        TextField {
                                                            width: root.wTimeout
                                                            height: parent.height - 6
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            inputMethodHints: Qt.ImhDigitsOnly
                                                            validator: IntValidator { bottom: 10; top: 999 }
                                                            text: String(model.timeout)
                                                            selectByMouse: true
                                                            onPressed: root.currentRow = index
                                                            onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            onTextEdited: if (acceptableInput) root.clusterModel.setTimeout(index, parseInt(text))
                                                            onEditingFinished: root.clusterModel.setTimeout(index, parseInt(text))
                                                        }

                                                        Item {
                                                            width: root.wDef
                                                            height: parent.height
                                                            CheckBox {
                                                                anchors.centerIn: parent
                                                                checked: !!model.isdefault
                                                                onToggled: root.clusterModel.setDefaultRow(index, checked)
                                                                onPressed: root.currentRow = index
                                                                onActiveFocusChanged: if (activeFocus) root.currentRow = index
                                                            }
                                                        }

                                                        // Only the activated server is ever connected.
                                                        Label {
                                                            id: clusterStatusLabel
                                                            width: root.wStatus
                                                            height: parent.height
                                                            verticalAlignment: Text.AlignVCenter
                                                            elide: Text.ElideRight
                                                            text: model.isdefault ? MainController.clusterStatusText : qsTr("Not connected")
                                                            HoverHandler { id: clusterStatusHover }
                                                            ToolTip.visible: clusterStatusHover.hovered && clusterStatusLabel.truncated
                                                            ToolTip.text: text
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        GroupBox {
                            id: clusterUdpGroup
                            title: qsTr("Receive spots over UDP Broadcast")
                            Layout.fillWidth: true

                            label: CheckBox {
                                    id: clusterUdpEnable
                                    text: qsTr("Receive spots over UDP Broadcast")
                                    checked: optBool("Cluster.UdpEnabled", false)
                                    onClicked: if (controller) controller.setOption("Cluster.UdpEnabled", checked)
                                    FitToolTip {
                                        visible: clusterUdpEnable.hovered
                                        text: qsTr("Receives spots over broadcasted UDP data\nFormat is XML using <spot>")
                                    }
                                }


                            RowLayout {
                                spacing: 8
                                Label { text: qsTr("UDP Port") }
                                TextField {
                                    id: clusterUdpPortLineEdit
                                    text: controller ? String(controller.options["Cluster.UdpPort"]) : ""
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    validator: IntValidator { bottom: 1; top: 65535 }
                                    Layout.preferredWidth: 120
                                    onEditingFinished: commitIntegerOption("Cluster.UdpPort", clusterUdpPortLineEdit, 1, 65535)
                                }
                                CheckBox {
                                    id: clusterUdpLifetimeEnable
                                    Layout.leftMargin: 16
                                    text: qsTr("UDP Spot Lifetime (sec)")
                                    checked: optBool("Cluster.UdpLifetimeEnabled", false)
                                    onClicked: if (controller) controller.setOption("Cluster.UdpLifetimeEnabled", checked)
                                    FitToolTip {
                                        id: clusterUdpLifetimeTip
                                        visible: clusterUdpLifetimeEnable.hovered
                                        text: qsTr("Expire spots received over UDP in this many seconds\nWhen unchecked, only the UDP packets may indicate a spot expiration")
                                    }
                                }
                                SpinBox {
                                    id: clusterUdpLifetimeSpin
                                    from: 10
                                    to: 999
                                    value: controller ? (Number(controller.options["Cluster.UdpLifetime"]) || 300) : 300
                                    editable: true
                                    enabled: clusterUdpLifetimeEnable.checked
                                    FitToolTip {
                                        visible: clusterUdpLifetimeSpin.hovered
                                        text: clusterUdpLifetimeTip.text
                                    }
                                    onValueModified: if (controller) controller.setOption("Cluster.UdpLifetime", value)
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        ScrollView {
                            id: clusterOutputScroll
                            Layout.fillWidth: true
                            // Don't size the layout from the wrapped text, or from
                            // it to the text: either way loops on implicitWidth.
                            Layout.preferredWidth: 200
                            contentWidth: availableWidth
                            Layout.preferredHeight: 160
                            Layout.minimumHeight: 120
                            Layout.maximumHeight: 180
                            clip: true
                            // Text wraps, so never scroll sideways. ScrollView sizes a
                            // TextArea child itself.
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded

                            TextArea {
                                id: clusterOutputTextEdit
                                text: MainController.clusterOutputText
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextArea.Wrap
                                persistentSelection: true
                                font.family: "monospace"
                                background: Rectangle {
                                    color: palette.base
                                    border.color: palette.mid
                                }
                            }
                        }

                        Connections {
                            target: MainController
                            function onClusterOutputTextChanged() {
                                clusterOutputTextEdit.cursorPosition = clusterOutputTextEdit.text.length
                            }
                        }

                        RowLayout {
                            spacing: 8
                            CheckBox {
                                id: clusterSkimmerSpotsEnable
                                text: qsTr("Show Skimmer Spots")
                                checked: optBool("Cluster.SkimmerSpotsEnable", false)
                                onClicked: if (controller) controller.setOption("Cluster.SkimmerSpotsEnable", checked)
                                FitToolTip {
                                    visible: clusterSkimmerSpotsEnable.hovered
                                    text: qsTr("Ask the cluster server to also send CW Skimmer and Reverse Beacon Network spots\n(sends set/skimmer). Not every cluster server supports this.")
                                }
                            }
                            Button {
                                text: qsTr("Clear Output")
                                onClicked: MainController.clearClusterOutput()
                            }
                            Label {
                                Layout.leftMargin: 16
                                text: qsTr("Spots begin to fade at this percent lifetime")
                                HoverHandler { id: clusterFadeLabelHover }
                                FitToolTip {
                                    visible: clusterFadeLabelHover.hovered
                                    text: clusterSpotFadeTip.text
                                }
                            }
                            SpinBox {
                                id: clusterSpotFadeSpin
                                from: 0
                                to: 100
                                value: controller && controller.options["Cluster.SpotFadePercent"] !== undefined ? Number(controller.options["Cluster.SpotFadePercent"]) : 50
                                editable: true
                                textFromValue: function(value, locale) { return value + "%" }
                                valueFromText: function(text, locale) { var v = parseInt(text); return isNaN(v) ? clusterSpotFadeSpin.value : v }
                                validator: RegularExpressionValidator { regularExpression: /\s*\d{1,3}\s*%?\s*/ }
                                FitToolTip {
                                    id: clusterSpotFadeTip
                                    visible: clusterSpotFadeSpin.hovered
                                    text: qsTr("Spots begin to linearly fade out at this percent of total spot lifetime\nZero percent = begin to fade over entire lifetime\nFifty percent = begin to fade when half way to expiration\nNinety percent = begin to fade when just ten percent life left")
                                }
                                onValueModified: if (controller) controller.setOption("Cluster.SpotFadePercent", value)
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }

                // =========================
                // Page 8: Shortcuts
                // =========================
                Loader {
                    id: shortcutsPageLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: settingsStack.currentIndex === 8
                    sourceComponent: shortcutsPageComponent
                }

                Component {
                    id: shortcutsPageComponent

                ScrollView {
                    id: shortcutsPage
                    clip: true

                    // Size the window to show about a dozen shortcuts; the rest scroll.
                    readonly property int fitRows: 12
                    readonly property real preferredFitHeight: {
                        var extraRows = shortcutsRepeater.count - fitRows
                        var first = shortcutsRepeater.count > 0 ? shortcutsRepeater.itemAt(0) : null
                        var rowStep = first ? first.implicitHeight + 6 : 0
                        var h = shortcutsContent.implicitHeight
                        if (extraRows > 0)
                            h -= extraRows * rowStep
                        return h + topPadding + bottomPadding
                    }

                    ColumnLayout {
                        id: shortcutsContent
                        width: shortcutsPage.availableWidth
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Configure keyboard shortcuts. wfview commands affect the application; radio commands send rig function commands.")
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                text: qsTr("Add Shortcut")
                                onClicked: if (controller) controller.addShortcut()
                            }
                            Button {
                                text: qsTr("Reset Defaults")
                                onClicked: if (controller) controller.resetShortcutsToDefault()
                            }
                            Item { Layout.fillWidth: true }
                        }

                        GroupBox {
                            title: qsTr("Shortcut Bindings")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: qsTr("On"); Layout.preferredWidth: 34 }
                                    Label { text: qsTr("Shortcut"); Layout.preferredWidth: 120 }
                                    Label { text: qsTr("wfview Command"); Layout.fillWidth: true }
                                    Label { text: qsTr("Radio Command"); Layout.fillWidth: true }
                                    Label { text: qsTr("Action"); Layout.preferredWidth: 110 }
                                    Label { text: qsTr("Value"); Layout.preferredWidth: 90 }
                                    Label { text: qsTr("Receiver"); Layout.preferredWidth: 100 }
                                    Item { Layout.preferredWidth: 32 }
                                }

                                Repeater {
                                    id: shortcutsRepeater
                                    model: controller ? controller.shortcutsModel : null

                                    delegate: RowLayout {
                                        id: shortcutRow
                                        Layout.fillWidth: true
                                        spacing: 8

                                        readonly property int rowIndex: index
                                        readonly property bool appCommand: String(shortcutCommand).indexOf("app.") === 0
                                        readonly property bool radioCommand: String(shortcutCommand) !== "None" && !appCommand
                                        readonly property bool receiverAppCommand: String(shortcutCommand) === "app.popoutReceiver" || String(shortcutCommand) === "app.popinReceiver" || String(shortcutCommand) === "app.enterFrequency"

                                        CheckBox {
                                            Layout.preferredWidth: 34
                                            checked: Boolean(shortcutEnabled)
                                            onClicked: if (controller) controller.updateShortcut(shortcutRow.rowIndex, "enabled", checked)
                                        }

                                        TextField {
                                            Layout.preferredWidth: 120
                                            text: String(shortcutSequence)
                                            placeholderText: qsTr("Ctrl+D")
                                            selectByMouse: true
                                            onEditingFinished: if (controller) controller.updateShortcut(shortcutRow.rowIndex, "sequence", text.trim())
                                            ToolTip.visible: hovered
                                            ToolTip.text: qsTr("Qt shortcut text, for example Ctrl+D, Alt+F, or Shift+F10.")
                                        }

                                        ComboBox {
                                            id: shortcutAppCommandCombo
                                            Layout.fillWidth: true
                                            textRole: "text"
                                            valueRole: "value"
                                            enabled: !shortcutRow.radioCommand
                                            model: controller ? controller.shortcutAppCommandModel : null
                                            currentIndex: controller ? controller.shortcutAppCommandIndex(shortcutRow.appCommand ? shortcutCommand : "None") : -1
                                            onActivated: {
                                                if (!controller)
                                                    return
                                                controller.updateShortcut(shortcutRow.rowIndex, "command", currentValue)
                                                controller.updateShortcut(shortcutRow.rowIndex, "action", 0)
                                                controller.updateShortcut(shortcutRow.rowIndex, "value", 0)
                                            }
                                        }

                                        ComboBox {
                                            id: shortcutRadioCommandCombo
                                            Layout.fillWidth: true
                                            textRole: "text"
                                            valueRole: "value"
                                            enabled: !shortcutRow.appCommand
                                            model: controller ? controller.shortcutRadioCommandModel : null
                                            currentIndex: controller ? controller.shortcutRadioCommandIndex(shortcutRow.radioCommand ? shortcutCommand : "None") : -1
                                            onActivated: if (controller) controller.updateShortcut(shortcutRow.rowIndex, "command", currentValue)
                                        }

                                        ComboBox {
                                            id: shortcutActionCombo
                                            Layout.preferredWidth: 110
                                            textRole: "text"
                                            valueRole: "value"
                                            model: [
                                                { text: qsTr("Trigger"), value: 0 },
                                                { text: qsTr("Absolute"), value: 1 },
                                                { text: qsTr("Increase"), value: 2 },
                                                { text: qsTr("Decrease"), value: 3 }
                                            ]
                                            enabled: !shortcutRow.appCommand
                                            currentIndex: indexFromValue(shortcutActionCombo, shortcutAction)
                                            onActivated: if (controller) controller.updateShortcut(shortcutRow.rowIndex, "action", currentValue)
                                        }

                                        SpinBox {
                                            Layout.preferredWidth: 90
                                            from: -100000000
                                            to: 100000000
                                            value: Number(shortcutValue)
                                            enabled: !shortcutRow.appCommand
                                            editable: true
                                            onValueModified: if (controller) controller.updateShortcut(shortcutRow.rowIndex, "value", value)
                                        }

                                        ComboBox {
                                            id: shortcutReceiverCombo
                                            Layout.preferredWidth: 100
                                            textRole: "text"
                                            valueRole: "value"
                                            model: [
                                                { text: qsTr("Current"), value: -1 },
                                                { text: qsTr("Main"), value: 0 },
                                                { text: qsTr("Sub"), value: 1 }
                                            ]
                                            enabled: !shortcutRow.appCommand || shortcutRow.receiverAppCommand
                                            currentIndex: indexFromValue(shortcutReceiverCombo, shortcutReceiver)
                                            onActivated: if (controller) controller.updateShortcut(shortcutRow.rowIndex, "receiver", currentValue)
                                        }

                                        ToolButton {
                                            Layout.preferredWidth: 32
                                            text: "..."
                                            onClicked: shortcutMenu.popup()
                                            Menu {
                                                id: shortcutMenu
                                                MenuItem {
                                                    text: qsTr("Add Shortcut")
                                                    onTriggered: if (controller) controller.addShortcut()
                                                }
                                                MenuItem {
                                                    text: qsTr("Duplicate Shortcut")
                                                    onTriggered: if (controller) controller.duplicateShortcut(shortcutRow.rowIndex)
                                                }
                                                MenuItem {
                                                    text: qsTr("Delete Shortcut")
                                                    onTriggered: if (controller) controller.deleteShortcut(shortcutRow.rowIndex)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("For radio shortcuts, use Absolute for fixed values and Increase or Decrease for step changes.")
                            wrapMode: Text.WordWrap
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
                }

                // =========================
                // Page 9: Macros
                // =========================
                Loader {
                    id: macrosPageLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: settingsStack.currentIndex === 9
                    sourceComponent: macrosPageComponent
                }

                Component {
                    id: macrosPageComponent

                ScrollView {
                    id: macrosPage
                    clip: true

                    ColumnLayout {
                        width: macrosPage.availableWidth
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("A macro sends up to %1 commands in order. Each macro is also a wfview command on the Shortcuts page, and can show a button on the main window.").arg(controller ? controller.macroMaxSteps : 20)
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Button {
                                text: qsTr("Add Macro")
                                onClicked: {
                                    if (!controller)
                                        return
                                    macroEditor.openMacro(controller.addMacro())
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Label { text: qsTr("Minimum time between commands (ms)") }
                            SpinBox {
                                from: 20
                                to: 1000
                                stepSize: 10
                                editable: true
                                value: controller ? controller.macroSpacingMs : 100
                                onValueModified: if (controller) controller.macroSpacingMs = value
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("No step is followed by a shorter wait than this, whatever its own delay.")
                            }
                        }

                        GroupBox {
                            title: qsTr("Macros")
                            Layout.fillWidth: true

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: qsTr("#"); Layout.preferredWidth: 40 }
                                    Label { text: qsTr("Name"); Layout.fillWidth: true }
                                    Label { text: qsTr("Steps"); Layout.preferredWidth: 50 }
                                    Label { text: qsTr("Show Button"); Layout.preferredWidth: 100 }
                                    Item { Layout.preferredWidth: 80 }
                                    Item { Layout.preferredWidth: 80 }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: !controller || controller.macros.length === 0
                                    text: qsTr("No macros yet. Use Add Macro to create one.")
                                    opacity: 0.7
                                }

                                Repeater {
                                    model: controller ? controller.macros : []

                                    delegate: RowLayout {
                                        id: macroRow
                                        Layout.fillWidth: true
                                        spacing: 8

                                        readonly property var macro: modelData

                                        Label {
                                            Layout.preferredWidth: 40
                                            text: String(macroRow.macro.id)
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: String(macroRow.macro.name)
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            Layout.preferredWidth: 50
                                            text: String(macroRow.macro.steps ? macroRow.macro.steps.length : 0)
                                        }
                                        CheckBox {
                                            Layout.preferredWidth: 100
                                            checked: Boolean(macroRow.macro.showButton)
                                            onClicked: if (controller) controller.setMacroShowButton(macroRow.macro.id, checked)
                                        }
                                        Button {
                                            Layout.preferredWidth: 80
                                            text: qsTr("Edit")
                                            onClicked: macroEditor.openMacro(macroRow.macro.id)
                                        }
                                        Button {
                                            Layout.preferredWidth: 80
                                            text: qsTr("Delete")
                                            onClicked: {
                                                deleteMacroConfirm.macroId = macroRow.macro.id
                                                deleteMacroConfirm.macroName = String(macroRow.macro.name)
                                                deleteMacroConfirm.open()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
                }

                // =========================
                // Page 10: Experimental
                // =========================
                ScrollView {
                    id: experimental
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: experimental.availableWidth
                        spacing: 10

                        Label {
                            text: qsTr("This page contains experimental features. Use at your own risk.")
                            wrapMode: Text.WordWrap
                        }

                        CheckBox {
                            id: rigCreatorChk
                            text: qsTr("Enable Rig Creator Feature (use with care)")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Rig creator allows changing of all rig features and adding new rig profiles")
                            checked: optBool("Interface.RigCreatorEnable", false)
                            onClicked: if (controller) controller.setOption("Interface.RigCreatorEnable", checked)
                        }

                        GroupBox {
                            title: qsTr("wfshare Remote Access")
                            Layout.fillWidth: true

                            GridLayout {
                                anchors.fill: parent
                                columns: 3
                                columnSpacing: 10
                                rowSpacing: 8

                                CheckBox {
                                    id: wfShareEnabledCheck
                                    text: qsTr("Enable wfshare")
                                    checked: optBool("Experimental.WfShareEnabled", false)
                                    onClicked: if (controller) controller.setOption("Experimental.WfShareEnabled", checked)
                                    Layout.columnSpan: 3
                                }

                                CheckBox {
                                    id: wfShareDirectCheck
                                    text: qsTr("Direct point-to-point mode")
                                    checked: optBool("Experimental.WfShareDirectMode", false)
                                    enabled: wfShareEnabledCheck.checked
                                    onClicked: if (controller) controller.setOption("Experimental.WfShareDirectMode", checked)
                                    Layout.columnSpan: 3
                                }

                                Label { text: qsTr("Remote host") }
                                TextField {
                                    id: wfShareHostField
                                    Layout.fillWidth: true
                                    text: controller ? String(controller.options["Experimental.WfShareHost"]) : "pbx.wfshare.org"
                                    placeholderText: wfShareDirectCheck.checked ? qsTr("server hostname or IP") : qsTr("pbx.wfshare.org")
                                    enabled: wfShareEnabledCheck.checked
                                    onEditingFinished: if (controller) controller.setOption("Experimental.WfShareHost", text.trim())
                                }
                                Label { text: wfShareDirectCheck.checked ? qsTr("Remote wfserver address") : qsTr("wfshare relay hostname") }

                                Label { text: qsTr("Port") }
                                SpinBox {
                                    id: wfSharePortSpin
                                    from: 1
                                    to: 65535
                                    value: controller ? Number(controller.options["Experimental.WfSharePort"]) : 4569
                                    editable: true
                                    enabled: wfShareEnabledCheck.checked
                                    onValueModified: if (controller) controller.setOption("Experimental.WfSharePort", value)
                                }
                                Label { text: qsTr("Default wfshare port is 4569") }

                                Label { text: qsTr("Username") }
                                TextField {
                                    id: wfShareUserField
                                    Layout.fillWidth: true
                                    text: controller ? String(controller.options["Experimental.WfShareUsername"]) : ""
                                    enabled: wfShareEnabledCheck.checked
                                    onEditingFinished: if (controller) controller.setOption("Experimental.WfShareUsername", text.trim())
                                }
                                Label { text: qsTr("wfshare account username") }

                                Label { text: qsTr("Password") }
                                TextField {
                                    id: wfSharePasswordField
                                    Layout.fillWidth: true
                                    text: controller ? String(controller.options["Experimental.WfSharePassword"]) : ""
                                    echoMode: TextInput.Password
                                    enabled: wfShareEnabledCheck.checked
                                    onEditingFinished: if (controller) controller.setOption("Experimental.WfSharePassword", text)
                                }
                                Label { text: qsTr("wfshare account password") }

                                Label { text: qsTr("Station ID") }
                                TextField {
                                    id: wfShareStationField
                                    Layout.fillWidth: true
                                    text: controller ? String(controller.options["Experimental.WfShareCalledNumber"]) : ""
                                    placeholderText: qsTr("remote station")
                                    enabled: wfShareEnabledCheck.checked && !wfShareDirectCheck.checked
                                    onEditingFinished: if (controller) controller.setOption("Experimental.WfShareCalledNumber", text.trim())
                                }
                                Label { text: wfShareDirectCheck.checked ? qsTr("Not used in direct mode") : qsTr("Remote station or PBX route to call") }

                                RowLayout {
                                    Layout.columnSpan: 3
                                    Layout.fillWidth: true
                                    Button {
                                        text: qsTr("Test wfshare Connection")
                                        enabled: wfShareEnabledCheck.checked
                                        onClicked: {
                                            if (controller) {
                                                controller.setOption("Experimental.WfShareHost", wfShareHostField.text.trim())
                                                controller.setOption("Experimental.WfShareDirectMode", wfShareDirectCheck.checked)
                                                controller.setOption("Experimental.WfSharePort", wfSharePortSpin.value)
                                                controller.setOption("Experimental.WfShareUsername", wfShareUserField.text.trim())
                                                controller.setOption("Experimental.WfSharePassword", wfSharePasswordField.text)
                                                controller.setOption("Experimental.WfShareCalledNumber", wfShareStationField.text.trim())
                                            }
                                            MainController.testWfShareConnection()
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("Handshake progress is written to the application log.")
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }

                        // MCP options are not saved; they start from the command line
                        // (--use-mcp etc.) and every change restarts the server.
                        GroupBox {
                            title: qsTr("MCP Diagnostics Server")
                            Layout.fillWidth: true

                            GridLayout {
                                anchors.fill: parent
                                columns: 3
                                columnSpacing: 10
                                rowSpacing: 8

                                Label {
                                    text: qsTr("MCP Disabled in this build")
                                    visible: !Mcp.available
                                    font.bold: true
                                    Layout.columnSpan: 3
                                }

                                CheckBox {
                                    id: mcpEnabledCheck
                                    text: qsTr("Enable MCP")
                                    enabled: Mcp.available
                                    checked: Mcp.serverEnabled
                                    onClicked: Mcp.serverEnabled = checked
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Runs a Model Context Protocol (MCP) server inside wfview, so an AI assistant can read live diagnostics: connection state, rig capabilities, the command queue and cache, audio configuration and levels, meters, spectrum, radio and network traffic, the log, settings and main-thread health. Not saved; use --use-mcp to start with it on.")
                                    Layout.columnSpan: 3
                                }

                                CheckBox {
                                    id: mcpWriteCheck
                                    text: qsTr("MCP Write Access")
                                    enabled: Mcp.available && mcpEnabledCheck.checked
                                    checked: Mcp.allowWrite
                                    onClicked: Mcp.allowWrite = checked
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Also lets MCP clients change the radio and wfview: frequency, mode, settings, raw commands and PTT (transmit). Leave off for read-only diagnostics.")
                                    Layout.columnSpan: 3
                                }

                                Label { text: qsTr("Port") }
                                TextField {
                                    id: mcpPortField
                                    Layout.preferredWidth: 100
                                    enabled: Mcp.available && mcpEnabledCheck.checked
                                    text: String(Mcp.port)
                                    validator: IntValidator { bottom: 1; top: 65535 }
                                    onEditingFinished: Mcp.port = Number(text)
                                }
                                Label { text: qsTr("Default MCP port is 8765") }

                                Label {
                                    text: qsTr("Bind address")
                                    Layout.alignment: Qt.AlignTop
                                    topPadding: 8
                                }
                                ColumnLayout {
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true
                                    enabled: Mcp.available && mcpEnabledCheck.checked
                                    spacing: 0

                                    RadioButton {
                                        text: qsTr("Local only (127.0.0.1)")
                                        checked: Mcp.bindMode === 0
                                        onClicked: Mcp.bindMode = 0
                                    }
                                    RadioButton {
                                        text: qsTr("All (0.0.0.0, needs a token)")
                                        checked: Mcp.bindMode === 1
                                        onClicked: Mcp.bindMode = 1
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        RadioButton {
                                            id: mcpBindCustomRadio
                                            text: qsTr("Specific address")
                                            checked: Mcp.bindMode === 2
                                            onClicked: {
                                                Mcp.customBindAddress = mcpBindField.text.trim()
                                                Mcp.bindMode = 2
                                            }
                                        }
                                        TextField {
                                            id: mcpBindField
                                            Layout.fillWidth: true
                                            enabled: mcpBindCustomRadio.checked
                                            text: Mcp.customBindAddress
                                            placeholderText: qsTr("IP address of a local interface")
                                            onEditingFinished: Mcp.customBindAddress = text.trim()
                                        }
                                    }
                                }

                                Label { text: qsTr("Token") }
                                TextField {
                                    id: mcpTokenField
                                    Layout.fillWidth: true
                                    enabled: Mcp.available && mcpEnabledCheck.checked
                                    text: Mcp.token
                                    echoMode: TextInput.Password
                                    placeholderText: qsTr("optional for local only")
                                    onEditingFinished: Mcp.token = text
                                }
                                Label { text: qsTr("Clients send it as a Bearer token") }

                                Label {
                                    text: qsTr("Status")
                                    Layout.alignment: Qt.AlignTop
                                }
                                Label {
                                    Layout.columnSpan: 2
                                    Layout.fillWidth: true
                                    text: Mcp.status
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                }

                                Label {
                                    Layout.columnSpan: 3
                                    Layout.fillWidth: true
                                    visible: Mcp.available
                                    text: qsTr("Check the log for detailed information.")
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        RowLayout {
                            spacing: 8
                            Button {
                                id: debugBtn
                                text: qsTr("Debug")
                                visible: false
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }

                // =========================
                // Page 11: Audio Processing
                // =========================
                ScrollView {
                    id: audioProcessing
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    RowLayout {
                        width: audioProcessing.availableWidth
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop

                            GroupBox {
                                title: qsTr("Transmit")
                                Layout.fillWidth: true
                                GridLayout {
                                    columns: 2
                                    anchors.fill: parent
                                    ProcSwitch { text: qsTr("Bypass"); key: "AudioProc.Tx.Bypass" }
                                    ProcSwitch { text: qsTr("EQ before compressor"); key: "AudioProc.Tx.EqFirst" }
                                    ProcSwitch { text: qsTr("Compressor"); key: "AudioProc.Tx.CompEnabled" }
                                    ProcSwitch { text: qsTr("Equalizer"); key: "AudioProc.Tx.EqEnabled" }
                                    ProcSwitch { text: qsTr("Noise gate"); key: "AudioProc.Tx.GateEnabled" }
                                    ProcSwitch { text: qsTr("Sidetone"); key: "AudioProc.Tx.SidetoneEnabled" }
                                    ProcSwitch { text: qsTr("Spectrum"); key: "AudioProc.Tx.SpectrumEnabled" }
                                }
                            }

                            GroupBox {
                                title: qsTr("Transmit Levels")
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    ProcSlider { label: qsTr("Input gain dB"); key: "AudioProc.Tx.InputGainDB"; from: -30; to: 30; step: 0.5; decimals: 1 }
                                    ProcSlider { label: qsTr("Output gain dB"); key: "AudioProc.Tx.OutputGainDB"; from: -30; to: 30; step: 0.5; decimals: 1 }
                                    ProcSlider { label: qsTr("Comp peak dB"); key: "AudioProc.Tx.CompPeakLimit"; from: -30; to: 0; step: 0.5; decimals: 1 }
                                    ProcSlider { label: qsTr("Comp release s"); key: "AudioProc.Tx.CompRelease"; from: 0; to: 1; step: 0.01; decimals: 2 }
                                    ProcSlider { label: qsTr("Fast ratio"); key: "AudioProc.Tx.CompFastRatio"; from: 0; to: 1; step: 0.01; decimals: 2 }
                                    ProcSlider { label: qsTr("Slow ratio"); key: "AudioProc.Tx.CompSlowRatio"; from: 0; to: 1; step: 0.01; decimals: 2 }
                                    ProcSlider { label: qsTr("Sidetone level"); key: "AudioProc.Tx.SidetoneLevel"; from: 0; to: 1; step: 0.01; decimals: 2 }
                                }
                            }

                            GroupBox {
                                title: qsTr("Transmit Gate")
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    ProcSlider { label: qsTr("Threshold dB"); key: "AudioProc.Tx.GateThreshold"; from: -90; to: 0; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("Attack ms"); key: "AudioProc.Tx.GateAttack"; from: 0; to: 1000; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("Hold ms"); key: "AudioProc.Tx.GateHold"; from: 0; to: 2000; step: 5; decimals: 0 }
                                    ProcSlider { label: qsTr("Decay ms"); key: "AudioProc.Tx.GateDecay"; from: 0; to: 4000; step: 5; decimals: 0 }
                                    ProcSlider { label: qsTr("Range dB"); key: "AudioProc.Tx.GateRange"; from: -90; to: 0; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("LF cutoff Hz"); key: "AudioProc.Tx.GateLfCutoff"; from: 20; to: 4000; step: 10; decimals: 0 }
                                    ProcSlider { label: qsTr("HF cutoff Hz"); key: "AudioProc.Tx.GateHfCutoff"; from: 200; to: 20000; step: 10; decimals: 0 }
                                    ProcSlider { label: qsTr("Spectrum FPS"); key: "AudioProc.Tx.SpectrumFPS"; from: 1; to: 60; step: 1; decimals: 0 }
                                }
                            }

                            GroupBox {
                                title: qsTr("Transmit EQ")
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Repeater {
                                        model: 15
                                        ProcSlider {
                                            label: qsTr("Band ") + (index + 1) + " dB"
                                            key: "AudioProc.Tx.EqBand" + index
                                            from: -70
                                            to: 30
                                            step: 0.5
                                            decimals: 1
                                        }
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop

                            GroupBox {
                                title: qsTr("Receive")
                                Layout.fillWidth: true
                                GridLayout {
                                    columns: 2
                                    anchors.fill: parent
                                    ProcSwitch { text: qsTr("Bypass"); key: "AudioProc.Rx.Bypass" }
                                    ProcSwitch { text: qsTr("Noise reduction"); key: "AudioProc.Rx.NrEnabled" }
                                    ProcSwitch { text: qsTr("Equalizer"); key: "AudioProc.Rx.EqEnabled" }
                                    ProcSwitch { text: qsTr("Spectrum"); key: "AudioProc.Rx.SpectrumEnabled" }
                                    ProcSwitch { text: qsTr("Speex AGC"); key: "AudioProc.Rx.SpeexAgc" }
                                    ProcSwitch { text: qsTr("Speex VAD"); key: "AudioProc.Rx.SpeexVad" }
                                }
                            }

                            GroupBox {
                                title: qsTr("Receive Noise Reduction")
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    ProcCombo {
                                        label: qsTr("NR mode")
                                        key: "AudioProc.Rx.NrMode"
                                        values: [
                                            { text: qsTr("None"), value: 0 },
                                            { text: qsTr("Speex"), value: 1 },
                                            { text: qsTr("ANR"), value: 2 }
                                        ]
                                    }
                                    ProcCombo {
                                        label: qsTr("Channel")
                                        key: "AudioProc.Rx.ChannelSelect"
                                        values: [
                                            { text: qsTr("Auto"), value: 0 },
                                            { text: qsTr("Left"), value: 1 },
                                            { text: qsTr("Right"), value: 2 },
                                            { text: qsTr("Mono sum"), value: 3 }
                                        ]
                                    }
                                    RowLayout {
                                        Button { text: qsTr("Collect ANR"); onClicked: MainController.startAnrNoiseProfile() }
                                        Button { text: qsTr("Stop Collect"); onClicked: MainController.stopAnrNoiseProfile() }
                                    }
                                    ProcSlider { label: qsTr("Speex suppression"); key: "AudioProc.Rx.SpeexSuppression"; from: -60; to: 0; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("Speex bands"); key: "AudioProc.Rx.SpeexBandsPreset"; from: 0; to: 8; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("Frame ms"); key: "AudioProc.Rx.SpeexFrameMs"; from: 10; to: 60; step: 10; decimals: 0 }
                                    ProcSlider { label: qsTr("AGC level"); key: "AudioProc.Rx.SpeexAgcLevel"; from: 1000; to: 30000; step: 100; decimals: 0 }
                                    ProcSlider { label: qsTr("AGC max dB"); key: "AudioProc.Rx.SpeexAgcMaxGain"; from: 0; to: 60; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("VAD start %"); key: "AudioProc.Rx.SpeexVadProbStart"; from: 0; to: 100; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("VAD continue %"); key: "AudioProc.Rx.SpeexVadProbCont"; from: 0; to: 100; step: 1; decimals: 0 }
                                    ProcSlider { label: qsTr("SNR decay"); key: "AudioProc.Rx.SpeexSnrDecay"; from: 0; to: 0.95; step: 0.01; decimals: 2 }
                                    ProcSlider { label: qsTr("Noise update"); key: "AudioProc.Rx.SpeexNoiseUpdateRate"; from: 0.01; to: 0.5; step: 0.01; decimals: 2 }
                                    ProcSlider { label: qsTr("Prior base"); key: "AudioProc.Rx.SpeexPriorBase"; from: 0.05; to: 0.5; step: 0.01; decimals: 2 }
                                    ProcSlider { label: qsTr("ANR reduction dB"); key: "AudioProc.Rx.AnrNoiseReductionDb"; from: 0; to: 40; step: 0.5; decimals: 1 }
                                    ProcSlider { label: qsTr("ANR sensitivity"); key: "AudioProc.Rx.AnrSensitivity"; from: 0; to: 12; step: 0.1; decimals: 1 }
                                    ProcSlider { label: qsTr("ANR smoothing"); key: "AudioProc.Rx.AnrFreqSmoothing"; from: 0; to: 12; step: 1; decimals: 0 }
                                }
                            }

                            GroupBox {
                                title: qsTr("Receive Output and EQ")
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    ProcSlider { label: qsTr("Output gain dB"); key: "AudioProc.Rx.OutputGainDB"; from: -30; to: 30; step: 0.5; decimals: 1 }
                                    ProcSlider { label: qsTr("Spectrum FPS"); key: "AudioProc.Rx.SpectrumFPS"; from: 1; to: 60; step: 1; decimals: 0 }
                                    Repeater {
                                        model: 4
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Label { text: qsTr("EQ band ") + (index + 1) }
                                            ProcSlider { label: qsTr("Gain dB"); key: "AudioProc.Rx.EqGain" + index; from: -24; to: 24; step: 0.5; decimals: 1 }
                                            ProcSlider { label: qsTr("Freq Hz"); key: "AudioProc.Rx.EqFreq" + index; from: 20; to: 8000; step: 10; decimals: 0 }
                                            ProcSlider { label: qsTr("Q"); key: "AudioProc.Rx.EqQ" + index; from: 0.1; to: 10; step: 0.1; decimals: 1 }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Bottom buttons
        RowLayout {
            id: bottomButtonsLayout
            Layout.fillWidth: true
            spacing: 8

            Button {
                id: saveSettingsBtn
                text: qsTr("Save Settings")
                Accessible.name: "Save Settings"
                onClicked: controller && controller.save()
            }

            Button {
                id: revertSettingsBtn
                text: qsTr("Revert to Default")
                Accessible.name: "Revert to Default"
                onClicked: revertSettingsConfirm.open()
            }

            Item { Layout.fillWidth: true }

            Button {
                id: connectBtn
                text: (connStatus === 0) ?
                          qsTr("Connect to Radio") :
                          (connStatus === 1) ?
                          qsTr("Cancel Connection") :
                          (connStatus === 2) ?
                          qsTr("Disconnect from Radio") : qsTr("Unknown status!")
                onClicked: MainController.connectionHandler()

                Accessible.name: "Connect to Radio"
            }

            Item { width: 20 } // fixed spacer (matches the .ui)
        }
    }

    // Right-click Cut/Copy/Paste for every text field above.
    TextEditMenu { root: settingsContent }

    Dialog {
        id: usbResetConfirm
        title: qsTr("Reset USB controllers")
        width: 420
        height: 140
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        background: Rectangle {
            color: window.palette.window
            border.color: window.palette.highlight
            border.width: 1
            radius: 4
        }

        contentItem: Label {
            width: usbResetConfirm.availableWidth
            text: qsTr("Are you sure you wish to reset the USB controllers?")
            color: window.palette.text
            wrapMode: Text.WordWrap
        }

        onAccepted: MainController.resetUsbControllers()
    }

    // Entry for a serial port that is not enumerated, such as a udev symlink.
    Dialog {
        id: manualSerialPortDialog
        title: qsTr("Manual serial port")
        width: 420
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        background: Rectangle {
            color: window.palette.window
            border.color: window.palette.highlight
            border.width: 1
            radius: 4
        }

        contentItem: ColumnLayout {
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter the full path of the serial port, for example /dev/ttyUSB0:")
                color: window.palette.text
                wrapMode: Text.WordWrap
            }
            TextField {
                id: manualSerialPortField
                Layout.fillWidth: true
                placeholderText: "/dev/ttyUSB0"
                Accessible.name: "Manual serial port path"
                onAccepted: manualSerialPortDialog.accept()
            }
        }

        onAboutToShow: {
            var current = controller ? String(controller.options["Radio.SerialPortRadio"] ?? "") : ""
            manualSerialPortField.text = (current === "auto") ? "" : current
            manualSerialPortField.forceActiveFocus()
            manualSerialPortField.selectAll()
        }

        onAccepted: {
            var port = manualSerialPortField.text.trim()
            if (controller && port !== "")
                controller.setOption("Radio.SerialPortRadio", port)
            restoreSerialSelection()
        }
        onRejected: restoreSerialSelection()

        // Point the box back at the stored port instead of "Manual ...".
        function restoreSerialSelection() {
            serialDeviceListCombo.currentIndex = Qt.binding(function() {
                return controller ? indexFromValue(serialDeviceListCombo, controller.options["Radio.SerialPortRadio"]) : -1
            })
        }
    }

    MacroEditor {
        id: macroEditor
        controller: window.controller
    }

    Dialog {
        id: deleteMacroConfirm
        property int macroId: -1
        property string macroName: ""
        title: qsTr("Delete macro")
        width: 420
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        modal: true
        standardButtons: Dialog.Yes | Dialog.No

        background: Rectangle {
            color: window.palette.window
            border.color: window.palette.highlight
            border.width: 1
            radius: 4
        }

        contentItem: Label {
            width: deleteMacroConfirm.availableWidth
            text: qsTr("Delete macro %1, \"%2\"?").arg(deleteMacroConfirm.macroId).arg(deleteMacroConfirm.macroName)
            color: window.palette.text
            wrapMode: Text.WordWrap
        }

        onAccepted: if (controller) controller.deleteMacro(macroId)
    }

    Dialog {
        id: revertSettingsConfirm
        title: qsTr("Revert settings")
        width: 460
        height: 170
        x: Math.round((window.width - width) / 2)
        y: Math.round((window.height - height) / 2)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        background: Rectangle {
            color: window.palette.window
            border.color: window.palette.highlight
            border.width: 1
            radius: 4
        }

        contentItem: Label {
            width: revertSettingsConfirm.availableWidth
            text: qsTr("Are you sure you wish to reset all wfview settings?\nIf so, wfview will exit and you will need to start the program again.")
            color: window.palette.text
            wrapMode: Text.WordWrap
        }

        onAccepted: MainController.revertSettingsToDefault()
    }

    // ---- Keyboard shortcuts from the .ui accessibleDescription ----
    Shortcut { sequence: "Shift+F1"; onActivated: settingsStack.currentIndex = 0 }
    Shortcut { sequence: "Shift+F2"; onActivated: settingsStack.currentIndex = 1 }
    Shortcut { sequence: "Shift+F3"; onActivated: settingsStack.currentIndex = 3 }
    Shortcut { sequence: "Shift+F4"; onActivated: settingsStack.currentIndex = 4 }
    Shortcut { sequence: "Shift+F5"; onActivated: settingsStack.currentIndex = 5 }
    Shortcut { sequence: "Shift+F6"; onActivated: settingsStack.currentIndex = 6 }
    Shortcut { sequence: "Shift+F7"; onActivated: settingsStack.currentIndex = 7 }
    Shortcut { sequence: "Shift+F8"; onActivated: settingsStack.currentIndex = 8 }
    Shortcut { sequence: "Shift+F9"; onActivated: settingsStack.currentIndex = 10 }
}
