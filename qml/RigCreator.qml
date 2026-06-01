// RigCreator.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs
import WFVIEW 1.0


ApplicationWindow {
    id: rigCreator
    transientParent: null
    title: qsTr("Rig Creator")
    RigCreatorController { id: rig }

    width: 1200
    height: 880
    color: palette.window
    property bool forceClose: false
    property bool closeAfterSaveDialog: false

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
        placeholderText: MainController.settings.options["Color.PlaceholderText"]

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
            placeholderText: MainController.settings.options["Color.Mid"]
        }
    }

    Component.onCompleted: {
        MainController.updateApplicationPalette()
        applyDisabledPalette()
    }

    Connections {
        target: MainController
        function onColChanged(items) {
            rigCreator.applyDisabledPalette()
        }
    }

    onClosing: function(event) {
        if (forceClose)
            return

        if (rig.dirty) {
            event.accepted = false
            unsavedDialog.open()
        }
    }

    // File dialog to open a file
    FileDialog {
        id: loadRigDialog
        title: qsTr("Open Rig File")
        currentFolder: rig.userRigsFolder()
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
        currentFolder: rig.userRigsFolder()
        selectedFile: rig.suggestedSaveFile()
        nameFilters: [qsTr("Rig Files (*.rig)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (rig.saveFile(selectedFile) && closeAfterSaveDialog) {
                closeAfterSaveDialog = false
                forceClose = true
                rigCreator.close()
            }
        }
        onRejected: {
            closeAfterSaveDialog = false
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

    Dialog {
        id: unsavedDialog
        title: qsTr("Unsaved Changes")
        modal: true
        x: Math.round((rigCreator.width - width) / 2)
        y: Math.round((rigCreator.height - height) / 2)
        width: Math.min(420, rigCreator.width - 48)
        closePolicy: Popup.CloseOnEscape

        background: Rectangle {
            color: rigCreator.palette.window
            border.color: rigCreator.palette.highlight
            border.width: 1
            radius: 3
        }

        contentItem: Label {
            text: qsTr("This rig file has unsaved changes. Save before closing?")
            wrapMode: Text.WordWrap
            color: rigCreator.palette.windowText
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Save")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Discard")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: {
            if (rig.hasCurrentFile() && rig.saveCurrentFile()) {
                forceClose = true
                rigCreator.close()
            } else {
                closeAfterSaveDialog = true
                saveRigDialog.open()
            }
        }

        onDiscarded: {
            forceClose = true
            rigCreator.close()
        }

        onRejected: closeAfterSaveDialog = false
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
        columns: ["Priority", "Command", "VFO", "Modes"]
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

    function modeChoicesFromTable() {
        var choices = []
        var rows = modesModel.rowCount()
        for (var row = 0; row < rows; ++row) {
            var name = String(modesModel.cell(row, 4) ?? "").trim()
            if (name.length > 0)
                choices.push({ text: name, value: name })
        }
        return choices
    }

    component PanelFrame : Item {
        id: root

        property string title: ""
        property int padding: 8
        property int radius: 4

        property color borderColor: rigCreator.palette.mid
        property color bgColor: "transparent"
        property color titleBg: rigCreator.palette.window
        property color textColor: rigCreator.palette.windowText

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

        property color gridColor: rigCreator.palette.mid
        property color headerBg: rigCreator.palette.button
        property color rowBgA: rigCreator.palette.base
        property color rowBgB: rigCreator.palette.alternateBase
        property color textColor: rigCreator.palette.text
        readonly property int verticalScrollBarWidth: 14
        readonly property int contentColumnsWidth: {
            var total = 0
            if (!columns)
                return total
            for (var i = 0; i < columns.length; ++i)
                total += columns[i].width ?? 140
            return total
        }
        implicitWidth: contentColumnsWidth + padding * 2 + verticalScrollBarWidth + 2

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
                    Item {
                        id: tableHost
                        width: parent.width
                        height: parent.height - headerBar.height

                        TableView {
                            id: tv
                            anchors.fill: parent
                            clip: true
                        model: panel.model

                        rowSpacing: 0
                        columnSpacing: 0

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AlwaysOn
                                interactive: true
                                implicitWidth: panel.verticalScrollBarWidth
                                contentItem: Rectangle {
                                    implicitWidth: panel.verticalScrollBarWidth
                                    radius: 3
                                    color: parent.pressed ? rigCreator.palette.highlight : rigCreator.palette.mid
                                }
                                background: Rectangle {
                                    color: rigCreator.palette.dark
                                    border.color: rigCreator.palette.mid
                                    border.width: 1
                                }
                            }
                            ScrollBar.horizontal: ScrollBar {
                                policy: ScrollBar.AlwaysOff
                            }

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

                            function closeEditorPopup() {
                                if (editorLoader.item && editorLoader.item.closeModesPopup)
                                    editorLoader.item.closeModesPopup()
                            }

                            TableView.onPooled: closeEditorPopup()
                            TableView.onReused: closeEditorPopup()

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
                                        if (ed === "modes") return modesEditor
                                        if (ed === "bool") return boolEditor
                                        if (ed === "color") return colorEditor
                                        if (ed === "int") return intEditor
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
                                id: intEditor
                                TextField {
                                    text: String(editorLoader.cellValueRef ?? "")
                                    color: panel.textColor
                                    background: Rectangle { color: "transparent" }
                                    selectByMouse: true
                                    validator: IntValidator {
                                        bottom: editorLoader.colDef.from ?? -2147483648
                                        top: editorLoader.colDef.to ?? 2147483647
                                    }
                                    onEditingFinished: {
                                        if (acceptableInput || text.length === 0)
                                            editorLoader.cellDataRef.display = text
                                    }
                                }
                            }

                            Component {
                                id: comboEditor
                                Item {
                                    anchors.fill: parent

                                    property var choicesRef: editorLoader.colDef.choices || []
                                    property var valueRef: editorLoader.cellValueRef
                                    property bool searchable: editorLoader.colDef.searchable === true
                                    property string choiceFilter: ""

                                    function filterChoices() {
                                        if (!searchable || choiceFilter.trim().length === 0)
                                            return choicesRef

                                        var out = []
                                        var terms = choiceFilter.toLowerCase().trim().split(/\s+/)
                                        for (var i = 0; i < choicesRef.length; ++i) {
                                            var item = choicesRef[i]
                                            var text = String((item && item.text !== undefined) ? item.text : item).toLowerCase()
                                            var ok = true
                                            for (var t = 0; t < terms.length; ++t) {
                                                if (text.indexOf(terms[t]) < 0) {
                                                    ok = false
                                                    break
                                                }
                                            }
                                            if (ok)
                                                out.push(item)
                                        }
                                        return out
                                    }

                                    function syncCurrent() {
                                        if (!cb || cb.count <= 0) return

                                        var v = valueRef

                                        for (var i = 0; i < cb.count; ++i) {
                                            var it = cb.model[i]
                                            if (it && it.value === v) {
                                                cb.currentIndex = i
                                                return
                                            }
                                        }

                                        var sv = String((v !== undefined && v !== null) ? v : "")

                                        for (var j = 0; j < cb.count; ++j) {
                                            var it2 = cb.model[j]
                                            var t = (it2 && it2.text !== undefined && it2.text !== null) ? it2.text : ""
                                            if (it2 && String(t) === sv) {
                                                cb.currentIndex = j
                                                return
                                            }
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
                                        onActivated: {
                                            editorLoader.cellDataRef.display = currentValue
                                            choiceFilter = ""
                                        }

                                        popup: Popup {
                                            id: comboPopup
                                            y: cb.height
                                            width: searchable ? Math.max(cb.width, 260) : cb.width
                                            height: Math.min(360, popupColumn.implicitHeight + topPadding + bottomPadding)
                                            padding: 4
                                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

                                            onOpened: {
                                                if (searchable) {
                                                    choiceFilter = ""
                                                    searchField.text = ""
                                                    searchField.forceActiveFocus()
                                                }
                                            }

                                            contentItem: Column {
                                                id: popupColumn
                                                width: comboPopup.availableWidth
                                                spacing: 4

                                                TextField {
                                                    id: searchField
                                                    width: parent.width
                                                    visible: searchable
                                                    placeholderText: qsTr("Search commands")
                                                    selectByMouse: true
                                                    onTextChanged: choiceFilter = text
                                                }

                                                ListView {
                                                    width: parent.width
                                                    height: Math.min(300, Math.max(1, count) * 28)
                                                    clip: true
                                                    model: searchable ? filterChoices() : choicesRef

                                                    delegate: ItemDelegate {
                                                        width: ListView.view.width
                                                        height: 28
                                                        text: {
                                                            var item = modelData
                                                            return String((item && item.text !== undefined) ? item.text : item)
                                                        }
                                                        highlighted: {
                                                            var item = modelData
                                                            return item && item.value === valueRef
                                                        }
                                                        onClicked: {
                                                            var item = modelData
                                                            editorLoader.cellDataRef.display = item && item.value !== undefined ? item.value : item
                                                            choiceFilter = ""
                                                            comboPopup.close()
                                                            Qt.callLater(syncCurrent)
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    onChoicesRefChanged: Qt.callLater(syncCurrent)
                                    onValueRefChanged: Qt.callLater(syncCurrent)
                                }
                            }

                            Component {
                                id: modesEditor
                                Item {
                                    id: modesEditorRoot
                                    anchors.fill: parent

                                    property var choicesRef: []
                                    readonly property int popupMargin: 10
                                    readonly property int popupMinHeight: 96
                                    readonly property int popupMaxHeight: 320
                                    readonly property int modeRowHeight: 30

                                    function popupWidth() {
                                        return width
                                    }

                                    function cellPoint() {
                                        return mapToItem(rigCreator.contentItem, 0, 0)
                                    }

                                    function popupX() {
                                        var p = cellPoint()
                                        return Math.max(popupMargin, Math.min(p.x, rigCreator.contentItem.width - popupWidth() - popupMargin))
                                    }

                                    function spaceBelow() {
                                        var p = cellPoint()
                                        return rigCreator.contentItem.height - (p.y + height) - popupMargin
                                    }

                                    function spaceAbove() {
                                        return Math.max(0, cellPoint().y - popupMargin)
                                    }

                                    function popupHeight() {
                                        var available = Math.max(spaceBelow(), spaceAbove())
                                        var wanted = 78 + Math.min(choicesRef.length * modeRowHeight, popupMaxHeight - 78)
                                        return Math.max(popupMinHeight, Math.min(wanted, popupMaxHeight, available))
                                    }

                                    function popupY() {
                                        var p = cellPoint()
                                        if (spaceBelow() >= popupMinHeight || spaceBelow() >= spaceAbove())
                                            return p.y + height
                                        return Math.max(popupMargin, p.y - popupHeight())
                                    }

                                    function positionPopup() {
                                        modesPopup.width = popupWidth()
                                        modesPopup.height = popupHeight()
                                        modesPopup.x = popupX()
                                        modesPopup.y = popupY()
                                    }

                                    function openModesPopup() {
                                        choicesRef = rigCreator.modeChoicesFromTable()
                                        positionPopup()
                                        modesPopup.open()
                                    }

                                    function closeModesPopup() {
                                        if (modesPopup.opened)
                                            modesPopup.close()
                                    }

                                    function selectedList() {
                                        var parts = String(editorLoader.cellValueRef ?? "").split(",")
                                        var out = []
                                        for (var i = 0; i < parts.length; ++i) {
                                            var part = parts[i].trim()
                                            if (part.length > 0)
                                                out.push(part)
                                        }
                                        return out
                                    }

                                    function containsMode(modeName) {
                                        var list = selectedList()
                                        for (var i = 0; i < list.length; ++i) {
                                            if (list[i].toUpperCase() === String(modeName).toUpperCase())
                                                return true
                                        }
                                        return false
                                    }

                                    function setMode(modeName, enabled) {
                                        var list = selectedList()
                                        var out = []
                                        var found = false
                                        for (var i = 0; i < list.length; ++i) {
                                            if (list[i].toUpperCase() === String(modeName).toUpperCase()) {
                                                found = true
                                                if (enabled)
                                                    out.push(list[i])
                                            } else {
                                                out.push(list[i])
                                            }
                                        }
                                        if (enabled && !found)
                                            out.push(modeName)
                                        editorLoader.cellDataRef.display = out.join(",")
                                    }

                                    Text {
                                        anchors.fill: parent
                                        anchors.leftMargin: 6
                                        anchors.rightMargin: 6
                                        verticalAlignment: Text.AlignVCenter
                                        text: String(editorLoader.cellValueRef ?? "").length > 0 ? String(editorLoader.cellValueRef ?? "") : qsTr("All modes")
                                        color: String(editorLoader.cellValueRef ?? "").length > 0 ? panel.textColor : rigCreator.palette.placeholderText
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: openModesPopup()
                                    }

                                    Popup {
                                        id: modesPopup
                                        parent: rigCreator.contentItem
                                        padding: 8
                                        modal: false
                                        dim: false
                                        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                                        background: Rectangle {
                                            color: rigCreator.palette.window
                                            border.color: rigCreator.palette.highlight
                                            border.width: 1
                                            radius: 3
                                        }

                                        contentItem: Column {
                                            id: modesColumn
                                            width: modesPopup.availableWidth
                                            spacing: 6

                                            Label {
                                                id: modesPopupHeader
                                                width: parent.width
                                                text: qsTr("Empty means all modes")
                                                color: rigCreator.palette.placeholderText
                                                elide: Text.ElideRight
                                            }

                                            ListView {
                                                id: modesList
                                                width: parent.width
                                                height: Math.max(1, modesPopup.availableHeight - modesPopupHeader.implicitHeight - modesPopupFooter.implicitHeight - parent.spacing * 2)
                                                clip: true
                                                model: choicesRef
                                                delegate: CheckBox {
                                                    width: modesList.width - (modesList.contentHeight > modesList.height ? panel.verticalScrollBarWidth : 0)
                                                    height: modesEditorRoot.modeRowHeight
                                                    text: modelData.text
                                                    checked: containsMode(modelData.value)
                                                    onClicked: setMode(modelData.value, checked)
                                                }
                                                boundsBehavior: Flickable.StopAtBounds
                                                ScrollBar.vertical: ScrollBar {
                                                    policy: modesList.contentHeight > modesList.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                                                    interactive: true
                                                    implicitWidth: panel.verticalScrollBarWidth
                                                    contentItem: Rectangle {
                                                        implicitWidth: panel.verticalScrollBarWidth
                                                        radius: 3
                                                        color: parent.pressed ? rigCreator.palette.highlight : rigCreator.palette.mid
                                                    }
                                                    background: Rectangle {
                                                        color: rigCreator.palette.dark
                                                        border.color: rigCreator.palette.mid
                                                        border.width: 1
                                                    }
                                                }
                                            }

                                            RowLayout {
                                                id: modesPopupFooter
                                                width: parent.width
                                                spacing: 6

                                                Button {
                                                    text: qsTr("Clear")
                                                    Layout.preferredWidth: 72
                                                    onClicked: {
                                                        editorLoader.cellDataRef.display = ""
                                                    }
                                                }

                                                Item { Layout.fillWidth: true }

                                                Button {
                                                    text: qsTr("Done")
                                                    Layout.preferredWidth: 72
                                                    onClicked: modesPopup.close()
                                                }
                                            }
                                        }
                                    }

                                    Connections {
                                        target: rigCreator
                                        function onWidthChanged() { modesEditorRoot.closeModesPopup() }
                                        function onHeightChanged() { modesEditorRoot.closeModesPopup() }
                                    }

                                    Connections {
                                        target: flick
                                        function onContentYChanged() { modesEditorRoot.closeModesPopup() }
                                    }
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

                        MouseArea {
                            id: emptyTableMouse
                            anchors.fill: parent
                            visible: panel.model && panel.model.visibleRows === 0
                            acceptedButtons: Qt.RightButton

                            onPressed: function(mouse) {
                                if (mouse.button !== Qt.RightButton)
                                    return

                                panel.ctxRow = -1
                                panel.ctxCol = -1
                                panel.ctxCellData = null
                                panel.ctxCellText = ""

                                var p = panel.mapFromItem(emptyTableMouse, mouse.x, mouse.y)
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






    // Put this near your TablePanel component (same file)
    // Use this wrapper to control sizing inside Flow.
    component FlowPanel : Item {
        id: fp
        property int prefW: 320
        property int prefH: 240
        property bool fullRow: false

        // The actual content goes here
        default property alias content: host.data

        readonly property int contentImplicitWidth: host.children.length > 0 ? host.children[0].implicitWidth : prefW

        width: fullRow ? flow.width : Math.min(Math.max(prefW, contentImplicitWidth), flow.width)
        height: prefH
        implicitHeight: prefH

        Item { id: host; anchors.fill: parent }
    }

    component SettingsPanel : PanelFrame {
        id: sp

        // same visual defaults as TablePanel
        borderColor: rigCreator.palette.mid
        bgColor: "transparent"
        titleBg: rigCreator.palette.window
        textColor: rigCreator.palette.windowText
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
                                    { title: qsTr("Type"),    name: "Type",       width: 180, editor: "combo", choices: rig.commandTypeChoices, searchable: true },
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

                    FlowPanel { prefW: 590; prefH: 260
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
                                    { title: qsTr("Command"), name: "Command", width: 180, editor: "combo", choices: rig.commandTypeChoices, searchable: true },
                                    { title: qsTr("VFO"),     name: "VFO",     width: 40, editor: "int", from: -1 },
                                    { title: qsTr("Modes"),   name: "Modes",   width: 220, editor: "modes" }
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
