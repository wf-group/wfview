// RigCreator.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import Qt.labs.platform 1.1 as PLATFORM
import WFVIEW 1.0


Window {
    id: rigCreator
    title: "Rig Creator"
    RigCreatorController { id: rig }

    width: 1200
    height: 880

    Component.onCompleted: {
        rigCreator.closing.connect(function(event) {
            rigCreator.destroy()
        })
    }

    // File dialog to open a file
    PLATFORM.FileDialog {
        id: loadRigDialog
        title: "Open Rig File"
        folder: PLATFORM.StandardPaths.writableLocation(PLATFORM.StandardPaths.AppDataLocation)+"/rigs"
        nameFilters: ["Rig Files (*.rig)"]
        fileMode: PLATFORM.FileDialog.OpenFile
        options: PLATFORM.FileDialog.DontUseNativeDialog
        onAccepted: {
            pendingFile = file
            rig.loading = true        // if writable, or via a method
            deferredLoad.start()
        }

        onRejected: {
            console.log("File dialog was canceled")
        }
    }

    PLATFORM.FileDialog {
        id: saveRigDialog
        title: "Save Rig File"
        folder: PLATFORM.StandardPaths.writableLocation(PLATFORM.StandardPaths.AppDataLocation)+"/rigs"
        nameFilters: ["Rig Files (*.rig)"]
        fileMode: PLATFORM.FileDialog.SaveFile
        options: PLATFORM.FileDialog.DontUseNativeDialog
        onAccepted: {
            rig.saveFile(file) // Pass selected file URL to the controller
        }
        onRejected: {
            console.log("Save dialog was canceled")
        }
    }

    PLATFORM.FileDialog {
        id: defaultRigDialog
        folder: rig.defaultRigsFolder()
        title: "Select Rig Filename"
        nameFilters: ["Rig Files (*.rig)"]
        fileMode: PLATFORM.FileDialog.OpenFile
        options: PLATFORM.FileDialog.DontUseNativeDialog

        onAccepted: {
            pendingFile = file
            rig.loading = true        // if writable, or via a method
            deferredLoad.start()
        }
    }

    Timer {
        id: deferredLoad
        interval: 32 // At least two frames!
        repeat: false
        onTriggered: rig.loadFile(pendingFile)
    }

    property string pendingFile
    Rectangle {
        x: 0; y: 0
        width: rigCreator.width
        height: rigCreator.height
        z: 99999
        visible: rig.loading
        color: "#80000000"

        BusyIndicator { anchors.centerIn: parent; running: rig.loading }
        MouseArea { anchors.fill: parent }
    }


    PLATFORM.MessageDialog {
        id: msg;
    }

    IniTableModel {
        id: commandsModel
        store: rig.store
        group: "Rig/Commands"
        columns: ["Type","String","Min","Max","Bytes","PadRight","Command29","GetCommand","SetCommand","Admin"]
    }

    IniSortProxy {
        id: commandsProxy
        sourceModel: commandsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: inputsModel
        store: rig.store
        group: "Rig/Inputs"
        columns: ["Num", "Reg", "Name"]
    }

    IniSortProxy {
        id: inputsProxy
        sourceModel: inputsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: scopeModesModel
        store: rig.store
        group: "Rig/ScopeModes"
        columns: ["Num", "Name"]
    }

    IniSortProxy {
        id: scopeModesProxy
        sourceModel: scopeModesModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: widthsModel
        store: rig.store
        group: "Rig/Widths"
        columns: ["Num", "Name", "Hz"]   // adjust if your Widths rows use different keys
    }

    IniSortProxy {
        id: widthsProxy
        sourceModel: widthsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: modesModel
        store: rig.store
        group: "Rig/Modes"
        columns: ["Num", "Reg", "Min", "Max", "Name"]
    }

    IniSortProxy {
        id: modesProxy
        sourceModel: modesModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: bandsModel
        store: rig.store
        group: "Rig/Bands"
        columns: [
            "Region","Num","BSR","Start","End","Range","MemoryGroup",
            "Name","Bytes","Power","Antennas","Offset","Color"
        ]
    }

    IniSortProxy {
        id: bandsProxy
        sourceModel: bandsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: tuningStepsModel
        store: rig.store
        group: "Rig/Tuning Steps"
        columns: ["Num", "Name", "Hz"]
    }

    IniSortProxy {
        id: tuningStepsProxy
        sourceModel: tuningStepsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: periodicModel
        store: rig.store
        group: "Rig/Periodic"
        columns: ["Priority", "Command", "VFO"]
    }

    IniSortProxy {
        id: periodicProxy
        sourceModel: periodicModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: spansModel
        store: rig.store
        group: "Rig/Spans"
        columns: ["Num", "Name", "Freq"]
    }

    IniSortProxy {
        id: spansProxy
        sourceModel: spansModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: filtersModel
        store: rig.store
        group: "Rig/Filters"
        columns: ["Num", "Modes", "Name"]
    }

    IniSortProxy {
        id: filtersProxy
        sourceModel: filtersModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: preampsModel;
        store: rig.store;
        group: "Rig/Preamps";
        columns: ["Num","Name"]
    }

    IniSortProxy {
        id: preampsProxy
        sourceModel: preampsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: attenModel;
        store: rig.store;
        group: "Rig/Attenuators";
        columns: ["Num","Name"]
    }

    IniSortProxy {
        id: attenProxy
        sourceModel: attenModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: metersModel
        store: rig.store
        group: "Rig/Meters"
        columns: ["Meter","RigVal","ActualVal","RedLine"]
    }

    IniSortProxy {
        id: metersProxy
        sourceModel: metersModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: antennasModel
        store: rig.store
        group: "Rig/Antennas"
        columns: ["Num", "Name"]
    }

    IniSortProxy {
        id: antennasProxy
        sourceModel: antennasModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }


    IniTableModel {
        id: attenuatorsModel
        store: rig.store
        group: "Rig/Attenuators"
        columns: ["Num", "Name"]
    }

    IniSortProxy {
        id: attenuatorsProxy
        sourceModel: attenuatorsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    IniTableModel {
        id: ctcssModel
        store: rig.store
        group: "Rig/CTCSS"
        columns: ["Reg", "Tone"]
    }

    IniSortProxy {
        id: ctcssProxy
        sourceModel: ctcssModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }


    IniTableModel {
        id: dtcsModel
        store: rig.store
        group: "Rig/DTCS"
        columns: ["Reg"]
    }

    IniSortProxy {
        id: dtcsProxy
        sourceModel: dtcsModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }


    IniTableModel {
        id: roofingModel
        store: rig.store
        group: "Rig/Roofing"
        columns: ["Num", "Name", "Hz"]   // adjust once you show Roofing\1\... keys
    }

    IniSortProxy {
        id: roofingProxy
        sourceModel: roofingModel
        dynamicSortFilter: true
        // sortRole: Qt.UserRole
    }

    Connections {
        target: rig
        function onShowMessage(title,text) {
            msg.title = title;
            msg.text = text;
            msg.open();
        }
    }

    component PanelFrame : Item {
        id: root

        property string title: ""
        property int padding: 8
        property int radius: 4

        property color borderColor: "#404040"
        property color bgColor: "transparent"
        property color titleBg: "#1e1e1e"
        property color textColor: "#e0e0e0"

        // Everything you place inside PanelFrame goes into contentItem
        default property alias content: contentItem.data

        Rectangle {
            anchors.fill: parent
            radius: root.radius
            color: root.bgColor
            border.color: root.borderColor
            border.width: 1
        }

        Rectangle {
            id: titleChip
            visible: root.title.length > 0
            color: root.titleBg
            radius: 2
            anchors.left: parent.left
            anchors.leftMargin: root.padding + 6
            anchors.verticalCenter: parent.top
            implicitWidth: titleText.implicitWidth + 10
            implicitHeight: titleText.implicitHeight + 2

            Text {
                id: titleText
                anchors.centerIn: parent
                text: root.title
                font.bold: true
                color: root.textColor
                elide: Text.ElideRight
            }
        }

        Item {
            id: contentItem
            anchors.fill: parent
            anchors.margins: root.padding
            anchors.topMargin: root.padding + (titleChip.visible ? (titleChip.height / 2) : 0)
        }
    }

    component TablePanel : PanelFrame {
        id: panel

        // IMPORTANT: do NOT redeclare "title" here (we use PanelFrame.title)

        property var model: null
        property var sourceModel: null
        property var columns: []
        property bool alternating: true

        property int rowHeight: 28
        property int headerHeight: 30

        property color gridColor: "#404040"
        property color headerBg: "#2b2b2b"
        property color rowBgA: "#1e1e1e"
        property color rowBgB: "#232323"
        property color textColor: "#e0e0e0"

        // Context state
        property int ctxRow: -1
        property int ctxCol: -1
        property var ctxCellData: null
        property string ctxCellText: ""

        // sort state lives on panel (or root)
        property int sortColumn: -1
        property int sortOrder: Qt.AscendingOrder


        Menu {
            id: ctxMenu
            MenuItem {
                text: "Copy"
                enabled: panel.ctxCellText.length > 0
                onTriggered: Clipboard.text = panel.ctxCellText
            }
            MenuItem {
                text: "Paste"
                enabled: Clipboard.text.length > 0
                onTriggered: panel.ctxCellData.display = Clipboard.text
            }
            MenuSeparator {}

            MenuItem {
                text: "Add row"
                onTriggered: panel.sourceModel.appendRow()
            }

            MenuItem {
                text: "Delete row"
                enabled: panel.ctxRow >= 0
                onTriggered: {
                    var srcRow = panel.model.mapRowToSource(panel.ctxRow)
                    if (srcRow >= 0)
                        panel.sourceModel.removeRowAt(srcRow)
                }

            }
        }
        function isBoolLike(v) {
            if (typeof v === "boolean") return true
            if (typeof v === "number") return (v === 0 || v === 1)
            if (typeof v === "string") {
                var s = v.trim().toLowerCase()
                return (s === "true" || s === "false" || s === "0" || s === "1" ||
                        s === "yes" || s === "no" || s === "on" || s === "off")
            }
            return false
        }

        function toBool(v) {
            if (typeof v === "boolean") return v
            if (typeof v === "number") return v !== 0
            if (typeof v === "string") {
                var s = v.trim().toLowerCase()
                return (s === "true" || s === "1" || s === "yes" || s === "on")
            }
            return false
        }


        function toAARRGGBB(c) {
            function hexByte(x) {
                var v = Math.max(0, Math.min(255, Math.round(x))).toString(16)
                return (v.length === 1) ? "0" + v : v
            }
            var a = hexByte(c.a * 255)
            var r = hexByte(c.r * 255)
            var g = hexByte(c.g * 255)
            var b = hexByte(c.b * 255)
            return "#" + a + r + g + b
        }


        function parseColor(cs) {
            if (!cs) return "#000000"
            var str = String(cs).trim()
            if (str.charAt(0) === "#") return str
            return "#000000"
        }

        // dialog state
        property var _colorTargetCellData: null
        property string _colorCurrentString: "#ff000000"

        PLATFORM.ColorDialog {
            id: colorDlg
            color: panel.parseColor(panel._colorCurrentString)
            options: ColorDialog.ShowAlphaChannel
            onAccepted: {
                if (!panel._colorTargetCellData) return
                panel._colorTargetCellData.display = panel.toAARRGGBB(colorDlg.color)
            }
        }

        // ---- Everything below is content INSIDE PanelFrame ----
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.fill: parent
                    spacing: 0

                    // --- SINGLE header row (click to sort) ---
                    Rectangle {
                        id: headerBar
                        width: parent.width
                        height: panel.headerHeight
                        color: panel.headerBg
                        border.color: panel.gridColor
                        border.width: 1
                        clip: true

                        Row {
                            anchors.fill: parent
                            spacing: 0

                            Repeater {
                                model: panel.columns ? panel.columns.length : 0

                                Rectangle {
                                    width: tv.columnWidthProvider(index)
                                    height: parent.height
                                    color: panel.headerBg
                                    border.color: panel.gridColor
                                    border.width: 1
                                    clip: true

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 6
                                        anchors.right: sortArrow.left
                                        anchors.rightMargin: 6
                                        elide: Text.ElideRight
                                        text: panel.columns[index].title ?? ""
                                        color: panel.textColor
                                        font.bold: true
                                    }

                                    Text {
                                        id: sortArrow
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.right: parent.right
                                        anchors.rightMargin: 6
                                        text: (panel.sortColumn === index)
                                              ? (panel.sortOrder === Qt.AscendingOrder ? "▲" : "▼")
                                              : ""
                                        color: panel.textColor
                                        opacity: 0.8
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (!panel.model) return

                                            if (panel.sortColumn === index) {
                                                panel.sortOrder = (panel.sortOrder === Qt.AscendingOrder)
                                                                 ? Qt.DescendingOrder
                                                                 : Qt.AscendingOrder
                                            } else {
                                                panel.sortColumn = index
                                                panel.sortOrder = Qt.AscendingOrder
                                            }

                                            // This MUST be the proxy (IniSortProxy / QSortFilterProxyModel)
                                            panel.model.sort(panel.sortColumn, panel.sortOrder)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // --- Table ---
                    TableView {
                        id: tv
                        width: parent.width
                        height: parent.height - headerBar.height
                        clip: true
                        model: panel.model

                        rowSpacing: 0
                        columnSpacing: 0

                        ScrollBar.vertical: ScrollBar {}
                        ScrollBar.horizontal: ScrollBar {}

                        columnWidthProvider: function(col) {
                            if (!panel.columns || col < 0 || col >= panel.columns.length) return 0
                            return panel.columns[col].width ?? 140
                        }

                        delegate: Rectangle {
                            implicitHeight: panel.rowHeight
                            visible: column < panel.columns.length

                            property var cellData: model
                            property var cellValue: (model.display !== undefined) ? model.display : ""

                            readonly property var colDef: panel.columns[column] || ({})
                            readonly property string editorType: String(colDef.editor || "").toLowerCase()

                            color: (editorType === "color")
                                   ? panel.parseColor(cellValue)
                                   : (panel.alternating ? ((row % 2) ? panel.rowBgB : panel.rowBgA) : panel.rowBgA)

                            border.color: panel.gridColor
                            border.width: 1

                            Item {
                                anchors.fill: parent
                                anchors.margins: 2

                                Loader {
                                    id: editorLoader
                                    anchors.fill: parent

                                    property var colDef: parent.parent.colDef
                                    property var cellDataRef: parent.parent.cellData
                                    property var cellValueRef: parent.parent.cellValue

                                    sourceComponent: {
                                        var ed = (colDef.editor || "").toLowerCase()
                                        if (ed === "combo" && colDef.choices) return comboEditor
                                        if (ed === "bool") return boolEditor
                                        if (ed === "color") return colorEditor
                                        return textEditor
                                    }
                                }
                            }

                            Component {
                                id: textEditor
                                TextField {
                                    text: {
                                        var v = editorLoader.cellValueRef
                                        var fmt = editorLoader.colDef.format || ""
                                        if (fmt === "fixed1" && v !== undefined && v !== null)
                                            return Number(v).toFixed(1)
                                        return String(v ?? "")
                                    }
                                    color: panel.textColor
                                    background: Rectangle { color: "transparent" }
                                    selectByMouse: true
                                    onEditingFinished: editorLoader.cellDataRef.display = text
                                }
                            }

                            Component {
                                id: boolEditor
                                CheckBox {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: ""
                                    checked: panel.toBool(editorLoader.cellValueRef)
                                    onToggled: editorLoader.cellDataRef.display = checked
                                }
                            }

                            Component {
                                id: comboEditor
                                Item {
                                    anchors.fill: parent

                                    property var choicesRef: editorLoader.colDef.choices || []
                                    property var valueRef: editorLoader.cellValueRef

                                    function syncCurrent() {
                                        if (!cb || cb.count <= 0) return

                                        var v = valueRef

                                        for (var i = 0; i < cb.count; ++i) {
                                            var it = cb.model[i]
                                            if (it && it.value === v) { cb.currentIndex = i; return }
                                        }

                                        var sv = String((v !== undefined && v !== null) ? v : "")

                                        for (var j = 0; j < cb.count; ++j) {
                                            var it2 = cb.model[j]
                                            var t = (it2 && it2.text !== undefined && it2.text !== null) ? it2.text : ""
                                            if (it2 && String(t) === sv) { cb.currentIndex = j; return }
                                        }

                                        cb.currentIndex = -1
                                    }


                                    ComboBox {
                                        id: cb
                                        anchors.fill: parent
                                        model: choicesRef
                                        textRole: "text"
                                        valueRole: "value"

                                        Component.onCompleted: Qt.callLater(syncCurrent)
                                        onCountChanged: Qt.callLater(syncCurrent)
                                        onActivated: editorLoader.cellDataRef.display = currentValue
                                    }

                                    onChoicesRefChanged: Qt.callLater(syncCurrent)
                                    onValueRefChanged: Qt.callLater(syncCurrent)
                                }
                            }

                            // ---- COLOR EDITOR ----
                            Component {
                                id: colorEditor
                                Item {
                                    anchors.fill: parent

                                    Text {
                                        anchors.centerIn: parent
                                        text: String(editorLoader.cellValueRef ?? "")
                                        color: panel.textColor
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            panel._colorTargetCellData = editorLoader.cellDataRef
                                            panel._colorCurrentString = String(editorLoader.cellValueRef ?? "#ff000000")
                                            colorDlg.open()
                                        }
                                    }
                                }
                            }
                            MouseArea {
                                id: cellMouse
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton

                                onPressed: function(mouse) {
                                    if (mouse.button !== Qt.RightButton)
                                        return
                                    panel.ctxRow = row
                                    panel.ctxCol = column
                                    panel.ctxCellData = cellData
                                    panel.ctxCellText = String(cellValue ?? "")

                                    // Convert from this MouseArea's local coords -> panel coords
                                    var p = panel.mapFromItem(cellMouse, mouse.x, mouse.y)

                                    ctxMenu.x = p.x
                                    ctxMenu.y = p.y
                                    ctxMenu.open()
                                    mouse.accepted = true
                                }
                            }
                        }
                    }
                }
            }
        }
    }






    // Put this near your TablePanel component (same file)
    // Use this wrapper to control sizing inside Flow.
    component FlowPanel : Item {
        id: fp
        property int prefW: 320
        property int prefH: 240
        property bool fullRow: false

        // The actual content goes here
        default property alias content: host.data

        width: fullRow ? flow.width : Math.min(prefW, flow.width)
        height: prefH
        implicitHeight: prefH

        Item { id: host; anchors.fill: parent }
    }

    component SettingsPanel : PanelFrame {
        id: sp

        // same visual defaults as TablePanel
        borderColor: "#404040"
        bgColor: "transparent"
        titleBg: "#1e1e1e"
        textColor: "#e0e0e0"
        padding: 8
        radius: 4

        // Make it behave like TablePanel in layouts
        Layout.fillWidth: true
        Layout.fillHeight: true

        // Users will put content inside this
    }


    // ---- main layout ----
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // =========================
        // Header row (Load/Save/Defaults + meta fields)
        // =========================
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            spacing: 8

            Button { text: "Load File"; onClicked: loadRigDialog.open() }
            Button { text: "Save File"; onClicked: saveRigDialog.open() }

            Item { Layout.fillWidth: true } // spacer (like your horizontalSpacer_2)

            Button { text: "Default Rigs"; onClicked: defaultRigDialog.open() }

            Label { text: "Manufacturer" }
            ComboBox {
                id: manufacturer
                Layout.preferredWidth: 180

                model: [
                    { text: "Icom",    value: 0 },
                    { text: "Kenwood", value: 1 },
                    { text: "Yaesu",   value: 2 }
                ]
                textRole: "text"
                valueRole: "value"

                function applySetting() {
                    // Coerce to int so it matches your model values
                    var v = parseInt(rig.settings["Rig/Manufacturer"] ?? 0)
                    for (let i = 0; i < count; ++i) {
                        if (parseInt(model[i].value) === v) {
                            currentIndex = i
                            return
                        }
                    }
                    currentIndex = 0
                }

                Component.onCompleted: applySetting()

                // When user changes the combo
                onActivated: rig.settings["Rig/Manufacturer"] = model[currentIndex].value

                // If settings are loaded/reloaded later, re-apply
                Connections {
                    target: rig
                    function onLoadingChanged() {
                        if (!rig.loading) manufacturer.applySetting()
                    }
                }
            }


            Label { text: "Model" }
            TextField {
                id: modelField
                Layout.preferredWidth: 220
                text: rig.settings["Rig/Model"] ?? ""
                onEditingFinished: rig.settings["Rig/Model"] = text
            }

            Label { text: "C-IV Address" }
            TextField {
                id: civAddress
                Layout.preferredWidth: 120
                inputMask: "hhhh"
                text: rig.settings["Rig/CIVAddress"] ?? ""
                onEditingFinished: rig.settings["Rig/CIVAddress"] = text
            }

            Label { text: "RigCtlD Model" }
            TextField {
                id: rigctldModel
                Layout.preferredWidth: 90
                inputMask: "0000"
                text: rig.settings["Rig/RigCtlDModel"] ?? ""
                onEditingFinished: rig.settings["Rig/RigCtlDModel"] = text
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // Put this where your GridLayout + bottom RowLayout currently are
            Flickable {
                id: flick
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                property int topPad: 14   // tweak 12–18 depending on your title font

                contentWidth: width
                contentHeight: flow.implicitHeight + topPad

                Flow {
                    id: flow
                    width: flick.width
                    y: flick.topPad          // <-- critical
                    spacing: 8

                    // IMPORTANT: FlowPanel items must be HERE (children of Flow)

                    // =========================
                    // Auto dashboard region
                    // =========================

                    // Small tables row (will wrap as needed)
                    FlowPanel { prefW: 160; prefH: 220
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Inputs"
                                model: inputsProxy
                                sourceModel: inputsModel
                                columns: [
                                  { title: "Num",  name: "Num",  width: 40 },
                                  { title: "Reg",  name: "Reg",  width: 40 },
                                  { title: "Name", name: "Name", width: 70 }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 160; prefH: 220
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Scope Modes"
                                model: scopeModesProxy
                                sourceModel: scopeModesModel
                                columns: [
                                  { title: "Num",  name: "Num",  width: 40 },
                                  { title: "Name", name: "Name", width: 120 }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 160; prefH: 220
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Widths"
                                model: widthsProxy
                                sourceModel: widthsModel
                                columns: [
                                  { title: "Bands", name: "Bands", width: 40 },
                                  { title: "Num",   name: "Num",   width: 40 },
                                  { title: "Hz",    name: "Hz",    width: 80 }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 260; prefH: 220
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Modes"
                                model: modesProxy
                                sourceModel: modesModel
                                columns: [
                                  { title: "Num",  name: "Num",  width: 40 },
                                  { title: "Reg",  name: "Reg",  width: 40 },
                                  { title: "Min",  name: "Min",  width: 50 },
                                  { title: "Max",  name: "Max",  width: 50 },
                                  { title: "Name", name: "Name", width: 80 }
                                ]
                            }
                        }
                    }



                    // Big tables become "full row" so they always use all available width
                    FlowPanel { prefW: 750; prefH: 320
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                title: "Commands"
                                model: commandsProxy
                                sourceModel: commandsModel
                                columns: [
                                    { title: "Type",    name: "Type",       width: 180, editor: "combo", choices: rig.commandTypeChoices },
                                    { title: "Command", name: "String",     width: 140 },
                                    { title: "Min",     name: "Min",        width: 60  },
                                    { title: "Max",     name: "Max",        width: 60  },
                                    { title: "Bytes",   name: "Bytes",      width: 50  },
                                    { title: "PadR",    name: "PadRight",   width: 50, editor: "bool" },
                                    { title: "Cmd29",   name: "Command29",  width: 60, editor: "bool" },
                                    { title: "Get",     name: "GetCommand", width: 40, editor: "bool" },
                                    { title: "Set",     name: "SetCommand", width: 40, editor: "bool" },
                                    { title: "Admin",   name: "Admin",      width: 60, editor: "bool" }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 900 ; prefH: 360
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Bands"
                                model: bandsProxy
                                sourceModel: bandsModel
                                columns: [
                                  { title: "Region", name: "Region",      width: 60 },
                                  { title: "Num",    name: "Num",         width: 40 },
                                  { title: "BSR",    name: "BSR",         width: 40 },
                                  { title: "Start",  name: "Start",       width: 100 },
                                  { title: "End",    name: "End",         width: 100 },
                                  { title: "Range",  name: "Range",       width: 60 },
                                  { title: "MemGrp", name: "MemoryGroup", width: 80 },
                                  { title: "Name",   name: "Name",        width: 80 },
                                  { title: "Bytes",  name: "Bytes",       width: 60 },
                                  { title: "Power",  name: "Power",       width: 60 },
                                  { title: "Ant",    name: "Antennas",    width: 40, editor: "bool" },
                                  { title: "Offset", name: "Offset",      width: 80 },
                                  { title: "Color",  name: "Color",       width: 100, editor: "color" }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 220; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Tuning Steps"
                                model: tuningStepsProxy
                                sourceModel: tuningStepsModel
                                columns: [
                                     { title: "Num",  name: "Num",  width: 40 },
                                     { title: "Name", name: "Name", width: 100 },
                                     { title: "Hz",   name: "Hz",   width: 80 }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 370; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Periodic Commands"
                                model: periodicProxy
                                sourceModel: periodicModel
                                columns: [
                                    { title: "Priority", name: "Priority", width: 140, editor: "combo", choices: [
                                        { text: "Highest", value: "Highest" },
                                        { text: "High", value: "High" },
                                        { text: "Medium High", value: "Medium High" },
                                        { text: "Medium", value: "Medium" },
                                        { text: "Medium Low", value: "Medium Low" },
                                        { text: "Low", value: "Low" }
                                    ]},
                                    { title: "Type",    name: "Type",       width: 180, editor: "combo", choices: rig.commandTypeChoices },
                                    { title: "VFO",     name: "VFO",     width: 40 }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 120; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Antennas";
                                model: antennasProxy;
                                sourceModel: antennasModel
                                columns: [{ title: "Num", name: "Num", width: 40 }, { title: "Name", name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 120; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Attenuators";
                                model: attenuatorsProxy;
                                sourceModel: attenuatorsModel
                                columns: [{ title: "Num", name: "Num", width: 40 }, { title: "Name", name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 170; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Filters";
                                model: filtersProxy
                                sourceModel: filtersModel
                                columns: [{ title: "Num", name: "Num", width: 40 }, { title: "Modes", name: "Modes", width: 50 }, { title: "Name", name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 200; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Spans"
                                model: spansProxy
                                sourceModel: spansModel
                                columns: [{ title: "Num", name: "Num", width: 40 }, { title: "Name", name: "Name", width: 80 }, { title: "Freq", name: "Freq", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 120; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Preamps"
                                model: preampsProxy
                                sourceModel: preampsModel
                                columns: [{ title: "Num", name: "Num", width: 40 }, { title: "Name", name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 110; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "CTCSS";
                                model: ctcssProxy
                                sourceModel: ctcssModel
                                columns: [
                                { title: "Reg", name: "Reg", width: 40 }, { title: "Tone", name: "Tone", width: 70, format: "fixed1" }]
                            }
                        }
                    }

                    FlowPanel { prefW: 90; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "DTCS";
                                model: dtcsProxy
                                sourceModel: dtcsModel
                                columns: [{ title: "Reg", name: "Reg", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 250; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Meters";
                                model: metersProxy
                                sourceModel: metersModel
                                columns: [
                                    { title: "Meter", name: "Meter", width: 80 },
                                    { title: "Rig", name: "RigVal", width: 60 },
                                    { title: "Act", name: "ActualVal", width: 60 },
                                    { title: "Red", name: "RedLine", width: 40, editor: "bool" }
                                ]
                            }
                        }
                    }

                    FlowPanel { prefW: 120; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: "Roofing";
                                model: roofingProxy
                                sourceModel: roofingModel
                                columns: [{ title: "Num", name: "Num", width: 40 }, { title: "Name", name: "Name", width: 80 }]
                            }
                        }
                    }
                }
            }

            Item {
                Layout.preferredWidth: 160
                Layout.minimumWidth: 140
                Layout.maximumWidth: 180
                Layout.fillHeight: true

                ColumnLayout {
                    id: rhs
                    anchors.fill: parent;
                    spacing: 8

                    SettingsPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 220
                        title: "Features"
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 4
                            CheckBox { text: "Has Spectrum"; checked: (rig.settings["Rig/HasSpectrum"] ?? false); onToggled: rig.settings["Rig/HasSpectrum"] = checked }
                            CheckBox { text: "Has LAN";      checked: (rig.settings["Rig/HasLAN"] ?? false);      onToggled: rig.settings["Rig/HasLAN"] = checked }
                            CheckBox { text: "Has Ethernet"; checked: (rig.settings["Rig/HasEthernet"] ?? false); onToggled: rig.settings["Rig/HasEthernet"] = checked }
                            CheckBox { text: "Has WiFi";     checked: (rig.settings["Rig/HasWiFi"] ?? false);     onToggled: rig.settings["Rig/HasWiFi"] = checked }
                            CheckBox { text: "Has Transmit"; checked: (rig.settings["Rig/HasTransmit"] ?? false); onToggled: rig.settings["Rig/HasTransmit"] = checked }
                            CheckBox { text: "Has FD Comms"; checked: (rig.settings["Rig/HasFDComms"] ?? false);  onToggled: rig.settings["Rig/HasFDComms"] = checked }
                            CheckBox { text: "Has Cmd 29";   checked: (rig.settings["Rig/HasCommand29"] ?? false);onToggled: rig.settings["Rig/HasCommand29"] = checked }
                            Item { Layout.fillHeight: true }
                        }
                    }

                    SettingsPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 190
                        title: "Spectrum"
                        GridLayout {
                            anchors.fill: parent
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            Label { text: "Num RX" }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/NumberOfReceivers"] ?? "0"; onEditingFinished: rig.settings["Rig/NumberOfReceivers"] = text }
                            Label { text: "VFO/RX" }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/NumberOfVFOs"] ?? "0";      onEditingFinished: rig.settings["Rig/NumberOfVFOs"] = text }
                            Label { text: "Seq Max" }  TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SpectrumSeqMax"] ?? "0";     onEditingFinished: rig.settings["Rig/SpectrumSeqMax"] = text }
                            Label { text: "Amp Max" }  TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SpectrumAmpMax"] ?? "0";     onEditingFinished: rig.settings["Rig/SpectrumAmpMax"] = text }
                            Label { text: "Len Max" }  TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SpectrumLenMax"] ?? "0";     onEditingFinished: rig.settings["Rig/SpectrumLenMax"] = text }
                        }
                    }

                    SettingsPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 170
                        title: "Memories"
                        GridLayout {
                            anchors.fill: parent
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            Label { text: "Grp" }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/MemGroups"] ?? "0";   onEditingFinished: rig.settings["Rig/MemGroups"] = text }
                            Label { text: "Mem" }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/Memories"] ?? "0";    onEditingFinished: rig.settings["Rig/Memories"] = text }
                            Label { text: "Sat" }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SatMemories"] ?? "0"; onEditingFinished: rig.settings["Rig/SatMemories"] = text }
                            Label { text: "Start" } TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/MemStart"] ?? "0";    onEditingFinished: rig.settings["Rig/MemStart"] = text }
                        }
                    }

                    Item { Layout.fillHeight: true } // spacer pushes panels to top
                }
            }
        }
        // =========================
        // Memory format fields (row=9 in your .ui)
        // =========================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 88
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Main Memory Format" }
                TextField {
                    Layout.fillWidth: true
                    text: rig.settings["Rig/MemFormat"] ?? "0"
                    onEditingFinished: rig.settings["Rig/MemFormat"] = text
                    ToolTip.visible: hovered
                    ToolTip.text: "See your QWidget tooltip text (keep it in docs/help)."
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Satellite Memory Format" }
                TextField {
                    Layout.fillWidth: true
                    text: rig.settings["Rig/SatFormat"] ?? "0"
                    onEditingFinished: rig.settings["Rig/SatFormat"] = text
                }
            }
        }

        // bottom buttons (Dialog already has default buttons, but your QWidget didn’t show them)
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            spacing: 8
            Item { Layout.fillWidth: true }
            Button { text: "Close"; onClicked: rigCreator.close() }
        }
    }
}
