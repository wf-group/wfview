// RigCreator.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs
import WFVIEW 1.0


ApplicationWindow {
    id: rigCreator
    title: qsTr("Rig Creator")
    RigCreatorController { id: rig }

    width: 1200
    height: 880

    Component.onCompleted: {
        rigCreator.closing.connect(function(event) {
            rigCreator.destroy()
        })
    }

    // File dialog to open a file
    FileDialog {
        id: loadRigDialog
        title: qsTr("Open Rig File")
        currentFolder: rig.defaultRigsFolder()
        nameFilters: [qsTr("Rig Files (*.rig)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            pendingFile = selectedFile
            rig.loading = true        // if writable, or via a method
            deferredLoad.start()
        }

        onRejected: {
            console.log("File dialog was canceled")
        }
    }

    FileDialog {
        id: saveRigDialog
        title: qsTr("Save Rig File")
        currentFolder: rig.defaultRigsFolder()
        nameFilters: [qsTr("Rig Files (*.rig)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            rig.saveFile(selectedFile) // Pass selected file URL to the controller
        }
        onRejected: {
            console.log("Save dialog was canceled")
        }
    }

    FileDialog {
        id: defaultRigDialog
        currentFolder: rig.defaultRigsFolder()
        title: qsTr("Select Rig Filename")
        nameFilters: [qsTr("Rig Files (*.rig)")]
        fileMode: FileDialog.OpenFile

        onAccepted: {
            pendingFile = selectedFile
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


    MessageDialog {
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
                text: qsTr("Copy")
                enabled: panel.ctxCellText.length > 0
                onTriggered: Clipboard.text = panel.ctxCellText
            }
            MenuItem {
                text: qsTr("Paste")
                enabled: Clipboard.text.length > 0
                onTriggered: panel.ctxCellData.display = Clipboard.text
            }
            MenuSeparator {}

            MenuItem {
                text: qsTr("Add row")
                onTriggered: panel.sourceModel.appendRow()
            }

            MenuItem {
                text: qsTr("Delete row")
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

        ColorDialog {
            id: colorDlg
            selectedColor: panel.parseColor(panel._colorCurrentString)
            options: ColorDialog.ShowAlphaChannel
            onAccepted: {
                if (!panel._colorTargetCellData) return
                panel._colorTargetCellData.display = panel.toAARRGGBB(colorDlg.selectedColor)
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

            Button { text: qsTr("Load File"); onClicked: loadRigDialog.open() }
            Button { text: qsTr("Save File"); onClicked: saveRigDialog.open() }

            Item { Layout.fillWidth: true } // spacer (like your horizontalSpacer_2)

            Button { text: qsTr("Default Rigs"); onClicked: defaultRigDialog.open() }

            Label { text: qsTr("Manufacturer") }
            ComboBox {
                id: manufacturer
                Layout.preferredWidth: 180

                model: [
                    { text: qsTr("Icom"),    value: 0 },
                    { text: qsTr("Kenwood"), value: 1 },
                    { text: qsTr("Yaesu"),   value: 2 }
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


            Label { text: qsTr("Model") }
            TextField {
                id: modelField
                Layout.preferredWidth: 220
                text: rig.settings["Rig/Model"] ?? ""
                onEditingFinished: rig.settings["Rig/Model"] = text
            }

            Label { text: qsTr("C-IV Address") }
            TextField {
                id: civAddress
                Layout.preferredWidth: 120
                inputMask: "hhhh"
                text: rig.settings["Rig/CIVAddress"] ?? ""
                onEditingFinished: rig.settings["Rig/CIVAddress"] = text
            }

            Label { text: qsTr("RigCtlD Model") }
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
                                title: qsTr("Inputs")
                                model: inputsProxy
                                sourceModel: inputsModel
                                columns: [
                                  { title: qsTr("Num"),  name: "Num",  width: 40 },
                                  { title: qsTr("Reg"),  name: "Reg",  width: 40 },
                                  { title: qsTr("Name"), name: "Name", width: 70 }
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
                                title: qsTr("Scope Modes")
                                model: scopeModesProxy
                                sourceModel: scopeModesModel
                                columns: [
                                  { title: qsTr("Num"),  name: "Num",  width: 40 },
                                  { title: qsTr("Name"), name: "Name", width: 120 }
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
                                title: qsTr("Widths")
                                model: widthsProxy
                                sourceModel: widthsModel
                                columns: [
                                  { title: qsTr("Bands"), name: "Bands", width: 40 },
                                  { title: qsTr("Num"),   name: "Num",   width: 40 },
                                  { title: qsTr("Hz"),    name: "Hz",    width: 80 }
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
                                title: qsTr("Modes")
                                model: modesProxy
                                sourceModel: modesModel
                                columns: [
                                  { title: qsTr("Num"),  name: "Num",  width: 40 },
                                  { title: qsTr("Reg"),  name: "Reg",  width: 40 },
                                  { title: qsTr("Min"),  name: "Min",  width: 50 },
                                  { title: qsTr("Max"),  name: "Max",  width: 50 },
                                  { title: qsTr("Name"), name: "Name", width: 80 }
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
                                title: qsTr("Commands")
                                model: commandsProxy
                                sourceModel: commandsModel
                                columns: [
                                    { title: qsTr("Type"),    name: "Type",       width: 180, editor: "combo", choices: rig.commandTypeChoices },
                                    { title: qsTr("Command"), name: "String",     width: 140 },
                                    { title: qsTr("Min"),     name: "Min",        width: 60  },
                                    { title: qsTr("Max"),     name: "Max",        width: 60  },
                                    { title: qsTr("Bytes"),   name: "Bytes",      width: 50  },
                                    { title: qsTr("PadR"),    name: "PadRight",   width: 50, editor: "bool" },
                                    { title: qsTr("Cmd29"),   name: "Command29",  width: 60, editor: "bool" },
                                    { title: qsTr("Get"),     name: "GetCommand", width: 40, editor: "bool" },
                                    { title: qsTr("Set"),     name: "SetCommand", width: 40, editor: "bool" },
                                    { title: qsTr("Admin"),   name: "Admin",      width: 60, editor: "bool" }
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
                                title: qsTr("Bands")
                                model: bandsProxy
                                sourceModel: bandsModel
                                columns: [
                                  { title: qsTr("Region"), name: "Region",      width: 60 },
                                  { title: qsTr("Num"),    name: "Num",         width: 40 },
                                  { title: qsTr("BSR"),    name: "BSR",         width: 40 },
                                  { title: qsTr("Start"),  name: "Start",       width: 100 },
                                  { title: qsTr("End"),    name: "End",         width: 100 },
                                  { title: qsTr("Range"),  name: "Range",       width: 60 },
                                  { title: qsTr("MemGrp"), name: "MemoryGroup", width: 80 },
                                  { title: qsTr("Name"),   name: "Name",        width: 80 },
                                  { title: qsTr("Bytes"),  name: "Bytes",       width: 60 },
                                  { title: qsTr("Power"),  name: "Power",       width: 60 },
                                  { title: qsTr("Ant"),    name: "Antennas",    width: 40, editor: "bool" },
                                  { title: qsTr("Offset"), name: "Offset",      width: 80 },
                                  { title: qsTr("Color"),  name: "Color",       width: 100, editor: "color" }
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
                                title: qsTr("Tuning Steps")
                                model: tuningStepsProxy
                                sourceModel: tuningStepsModel
                                columns: [
                                     { title: qsTr("Num"),  name: "Num",  width: 40 },
                                     { title: qsTr("Name"), name: "Name", width: 100 },
                                     { title: qsTr("Hz"),   name: "Hz",   width: 80 }
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
                                title: qsTr("Periodic Commands")
                                model: periodicProxy
                                sourceModel: periodicModel
                                columns: [
                                    { title: qsTr("Priority"), name: "Priority", width: 140, editor: "combo", choices: [
                                        { text: qsTr("Highest"), value: "Highest" },
                                        { text: qsTr("High"), value: "High" },
                                        { text: qsTr("Medium High"), value: "Medium High" },
                                        { text: qsTr("Medium"), value: "Medium" },
                                        { text: qsTr("Medium Low"), value: "Medium Low" },
                                        { text: qsTr("Low"), value: "Low" }
                                    ]},
                                    { title: qsTr("Type"),    name: "Type",       width: 180, editor: "combo", choices: rig.commandTypeChoices },
                                    { title: qsTr("VFO"),     name: "VFO",     width: 40 }
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
                                title: qsTr("Antennas");
                                model: antennasProxy;
                                sourceModel: antennasModel
                                columns: [{ title: qsTr("Num"), name: "Num", width: 40 }, { title: qsTr("Name"), name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 120; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("Attenuators");
                                model: attenuatorsProxy;
                                sourceModel: attenuatorsModel
                                columns: [{ title: qsTr("Num"), name: "Num", width: 40 }, { title: qsTr("Name"), name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 170; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("Filters");
                                model: filtersProxy
                                sourceModel: filtersModel
                                columns: [{ title: qsTr("Num"), name: "Num", width: 40 }, { title: qsTr("Modes"), name: "Modes", width: 50 }, { title: qsTr("Name"), name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 200; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("Spans")
                                model: spansProxy
                                sourceModel: spansModel
                                columns: [{ title: qsTr("Num"), name: "Num", width: 40 }, { title: qsTr("Name"), name: "Name", width: 80 }, { title: qsTr("Freq"), name: "Freq", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 120; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("Preamps")
                                model: preampsProxy
                                sourceModel: preampsModel
                                columns: [{ title: qsTr("Num"), name: "Num", width: 40 }, { title: qsTr("Name"), name: "Name", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 110; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("CTCSS");
                                model: ctcssProxy
                                sourceModel: ctcssModel
                                columns: [
                                { title: qsTr("Reg"), name: "Reg", width: 40 }, { title: qsTr("Tone"), name: "Tone", width: 70, format: "fixed1" }]
                            }
                        }
                    }

                    FlowPanel { prefW: 90; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("DTCS");
                                model: dtcsProxy
                                sourceModel: dtcsModel
                                columns: [{ title: qsTr("Reg"), name: "Reg", width: 80 }]
                            }
                        }
                    }

                    FlowPanel { prefW: 250; prefH: 260
                        Loader {
                            anchors.fill: parent
                            active: rigCreator.visible && !rig.loading
                            sourceComponent: TablePanel {
                                anchors.fill: parent
                                title: qsTr("Meters");
                                model: metersProxy
                                sourceModel: metersModel
                                columns: [
                                    { title: qsTr("Meter"), name: "Meter", width: 80 },
                                    { title: qsTr("Rig"), name: "RigVal", width: 60 },
                                    { title: qsTr("Act"), name: "ActualVal", width: 60 },
                                    { title: qsTr("Red"), name: "RedLine", width: 40, editor: "bool" }
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
                                title: qsTr("Roofing");
                                model: roofingProxy
                                sourceModel: roofingModel
                                columns: [{ title: qsTr("Num"), name: "Num", width: 40 }, { title: qsTr("Name"), name: "Name", width: 80 }]
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
                        title: qsTr("Features")
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 4
                            CheckBox { text: qsTr("Has Spectrum"); checked: (rig.settings["Rig/HasSpectrum"] ?? false); onToggled: rig.settings["Rig/HasSpectrum"] = checked }
                            CheckBox { text: qsTr("Has LAN");      checked: (rig.settings["Rig/HasLAN"] ?? false);      onToggled: rig.settings["Rig/HasLAN"] = checked }
                            CheckBox { text: qsTr("Has Ethernet"); checked: (rig.settings["Rig/HasEthernet"] ?? false); onToggled: rig.settings["Rig/HasEthernet"] = checked }
                            CheckBox { text: qsTr("Has WiFi");     checked: (rig.settings["Rig/HasWiFi"] ?? false);     onToggled: rig.settings["Rig/HasWiFi"] = checked }
                            CheckBox { text: qsTr("Has Transmit"); checked: (rig.settings["Rig/HasTransmit"] ?? false); onToggled: rig.settings["Rig/HasTransmit"] = checked }
                            CheckBox { text: qsTr("Has FD Comms"); checked: (rig.settings["Rig/HasFDComms"] ?? false);  onToggled: rig.settings["Rig/HasFDComms"] = checked }
                            CheckBox { text: qsTr("Has Cmd 29");   checked: (rig.settings["Rig/HasCommand29"] ?? false);onToggled: rig.settings["Rig/HasCommand29"] = checked }
                            Item { Layout.fillHeight: true }
                        }
                    }

                    SettingsPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 190
                        title: qsTr("Spectrum")
                        GridLayout {
                            anchors.fill: parent
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            Label { text: qsTr("Num RX") }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/NumberOfReceivers"] ?? "0"; onEditingFinished: rig.settings["Rig/NumberOfReceivers"] = text }
                            Label { text: qsTr("VFO/RX") }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/NumberOfVFOs"] ?? "0";      onEditingFinished: rig.settings["Rig/NumberOfVFOs"] = text }
                            Label { text: qsTr("Seq Max") }  TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SpectrumSeqMax"] ?? "0";     onEditingFinished: rig.settings["Rig/SpectrumSeqMax"] = text }
                            Label { text: qsTr("Amp Max") }  TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SpectrumAmpMax"] ?? "0";     onEditingFinished: rig.settings["Rig/SpectrumAmpMax"] = text }
                            Label { text: qsTr("Len Max") }  TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SpectrumLenMax"] ?? "0";     onEditingFinished: rig.settings["Rig/SpectrumLenMax"] = text }
                        }
                    }

                    SettingsPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 170
                        title: qsTr("Memories")
                        GridLayout {
                            anchors.fill: parent
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 6
                            Label { text: qsTr("Grp") }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/MemGroups"] ?? "0";   onEditingFinished: rig.settings["Rig/MemGroups"] = text }
                            Label { text: qsTr("Mem") }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/Memories"] ?? "0";    onEditingFinished: rig.settings["Rig/Memories"] = text }
                            Label { text: qsTr("Sat") }   TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/SatMemories"] ?? "0"; onEditingFinished: rig.settings["Rig/SatMemories"] = text }
                            Label { text: qsTr("Start") } TextField { Layout.preferredWidth: 80; text: rig.settings["Rig/MemStart"] ?? "0";    onEditingFinished: rig.settings["Rig/MemStart"] = text }
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
                Label { text: qsTr("Main Memory Format") }
                TextField {
                    Layout.fillWidth: true
                    text: rig.settings["Rig/MemFormat"] ?? "0"
                    onEditingFinished: rig.settings["Rig/MemFormat"] = text
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("See your QWidget tooltip text (keep it in docs/help).")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Satellite Memory Format") }
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
            Button { text: qsTr("Close"); onClicked: rigCreator.close() }
        }
    }
}
