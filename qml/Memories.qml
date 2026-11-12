// Memories.qml
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import WFVIEW 1.0

ApplicationWindow {
    id: memoriesWindow
    color: palette.window
    transientParent: null

    readonly property bool _sysTheme: MainController.settings.options["Interface.UseSystemTheme"] === true

    palette {
        window: _sysTheme ? undefined : MainController.settings.options["Color.Window"]
        windowText: _sysTheme ? undefined : MainController.settings.options["Color.WindowText"]
        base: _sysTheme ? undefined : MainController.settings.options["Color.Base"]
        alternateBase: _sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
        text: _sysTheme ? undefined : MainController.settings.options["Color.MainText"]
        button: _sysTheme ? undefined : MainController.settings.options["Color.Button"]
        buttonText: _sysTheme ? undefined : MainController.settings.options["Color.ButtonText"]

        disabled {
            window: _sysTheme ? undefined : MainController.settings.options["Color.Window"]
            windowText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            base: _sysTheme ? undefined : MainController.settings.options["Color.Base"]
            alternateBase: _sysTheme ? undefined : MainController.settings.options["Color.AlternateBase"]
            text: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
            button: _sysTheme ? undefined : MainController.settings.options["Color.Button"]
            buttonText: _sysTheme ? undefined : MainController.settings.options["Color.Mid"]
        }
    }

    onClosing: function(close) {
        if (forceClose)
            return

        close.accepted = false  // Don't actually close
        visible = false         // Just hide instead
    }

    width: 1113
    height: 520

    minimumWidth: 800
    minimumHeight: 400

    maximumWidth: {
        if (!memoriesModel)
            return 16777215
        var totalWidth = 0
        for (var i = 0; i < memoriesModel.columnCount(); i++) {
            if (memoriesModel.isColumnVisible(i)) {
                // Add up column widths
                switch(i) {
                    case 0: totalWidth += 80; break   // Recall
                    case 1: totalWidth += 60; break   // Num
                    case 2: totalWidth += 120; break  // Name
                    case 7: totalWidth += 100; break  // Freq
                    default: totalWidth += 80; break
                }
            }
        }
        // Add spacing and margins
        var visibleCount = memoriesModel.visibleColumnCount
        totalWidth += (visibleCount - 1) * 1  // columnSpacing
        totalWidth += 20  // margins/padding
        return totalWidth
    }

    title: qsTr("Memory Management - wfview")
    visible: true

    property MemoriesModel memoriesModel
    property bool forceClose: false

    function applyDisabledPalette() {
        if (_sysTheme) return
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

    onVisibleChanged: {
        if (visible) {
            // Apply palette when window becomes visible
            MainController.updateApplicationPalette()
            applyDisabledPalette()
            // Retry if an earlier load failed (for example, opened right after connecting)
            if (memoriesModel)
                memoriesModel.populate()
        }
    }

    Component.onCompleted: {
        if (memoriesModel) {
            memoriesModel.populate()
        }
        applyDisabledPalette()
    }

    Connections {
        target: MainController.settings

        function onColChanged(items) {
            memoriesWindow.applyDisabledPalette()
        }
    }

    Connections {
        target: MainController

        function onMemoryReceived(mem) {
            if (memoriesModel)
                memoriesModel.receiveMemory(mem)
        }

        function onMemoryNameReceived(tag) {
            if (memoriesModel)
                memoriesModel.receiveMemoryName(tag)
        }

        function onMemorySplitReceived(split) {
            if (memoriesModel)
                memoriesModel.receiveMemorySplit(split)
        }
    }

    // The UI content
    Page {
        anchors.fill: parent

        header: ToolBar {
            RowLayout {
                anchors.fill: parent
                spacing: 10

                ComboBox {
                    id: groupCombo
                    model: memoriesModel ? memoriesModel.groupNames : []
                    visible: memoriesModel ? memoriesModel.hasMemoryGroups : false
                    currentIndex: memoriesModel ? memoriesModel.currentGroup : -1
                    onCurrentIndexChanged: {
                        if (memoriesModel && currentIndex >= 0) {
                            memoriesModel.currentGroup = currentIndex
                        }
                    }
                    Layout.preferredWidth: 150
                }

                Button {
                    text: qsTr("Reload")
                    enabled: memoriesModel ? !memoriesModel.loading : false
                    onClicked: memoriesModel.reload()
                }

                Item { Layout.fillWidth: true }

                CheckBox {
                    text: qsTr("Disable Editing")
                    checked: memoriesModel ? !memoriesModel.canEdit : false
                    onCheckedChanged: if (memoriesModel) memoriesModel.canEdit = !checked
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: (memoriesModel && memoriesModel.isScanning) ? qsTr("Stop Scan") : qsTr("Start Scan")
                    visible: memoriesModel ? memoriesModel.canScan : false
                    onClicked: {
                        if (memoriesModel.isScanning) {
                            memoriesModel.stopScan()
                        } else {
                            memoriesModel.startScan()
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr(".csv Import")
                    enabled: memoriesModel ? memoriesModel.canEdit : false
                    onClicked: csvImportDialog.open()
                }

                CheckBox {
                    id: allFieldsCheck
                    text: qsTr("All fields")
                    checked: true
                }

                Button {
                    text: qsTr(".csv Export")
                    onClicked: csvExportDialog.open()
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: (memoriesModel && memoriesModel.memoryModeActive) ?
                          qsTr("Select VFO Mode") : qsTr("Select Memory Mode")
                    checkable: true
                    checked: memoriesModel ? memoriesModel.memoryModeActive : false
                    onCheckedChanged: if (memoriesModel) memoriesModel.memoryModeActive = checked
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // HorizontalHeaderView is Qt 6.1+ only; use a Repeater row for Qt 5 compatibility
            Row {
                id: headerView
                Layout.fillWidth: true
                Layout.preferredHeight: 30

                Repeater {
                    model: memoriesModel ? memoriesModel.columnCount() : 0
                    Rectangle {
                        height: 30
                        // Reading visibleColumnCount makes this re-evaluate when columns are shown or hidden
                        width: (memoriesModel && memoriesModel.visibleColumnCount >= 0)
                               ? tableView.columnWidthProvider(index) : 0
                        // Hidden columns have zero width, but their labels would still be drawn on top of each other
                        visible: width > 0
                        clip: true
                        color: palette.button
                        border.color: palette.mid

                        Text {
                            anchors.centerIn: parent
                            text: memoriesModel ? memoriesModel.getColumnName(index) : ""
                            font.bold: true
                            color: palette.buttonText
                        }
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                clip: true

                // Show scrollbars when needed
                ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TableView {
                    id: tableView
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    clip: true
                    // alternatingRows is Qt 6.2+ only; enabled conditionally below
                    columnSpacing: 1
                    rowSpacing: 1

                    model: memoriesModel

                    Connections {
                        target: memoriesModel
                        function onVisibleColumnsChanged() {
                            //console.log("Visible columns changed, updating...")
                            // Force table to recalculate column widths
                            tableView.forceLayout()
                        }
                    }

                    // A width of 0 hides the column. setColumnWidth() is Qt 6.5+ only, so use the provider.
                    columnWidthProvider: function(column) {
                        if (!memoriesModel || !memoriesModel.isColumnVisible(column))
                            return 0
                        switch (column) {
                            case 0: return 80
                            case 1: return 60
                            case 2: return 120
                            case 7: return 100
                            default: return memoriesModel.visibleColumnCount > 15 ? 60 : 80
                        }
                    }

                    Component.onCompleted: {
                        // alternatingRows is Qt 6.2+ only; Qt.version is not available in QML
                        if (typeof alternatingRows !== "undefined") {
                            alternatingRows = true
                        }
                    }

                    delegate: Rectangle {
                        id: cellDelegate

                        // Store row as stable property
                        property int delegateRow: row
                        property int delegateColumn: column
                        property bool isSaving: false

                        // Initialize saving state when delegate is created
                        Component.onCompleted: {
                            if (memoriesModel && delegateRow >= 0 && delegateRow < memoriesModel.rowCount()) {
                                isSaving = memoriesModel.isRowSaving(delegateRow)
                            }
                        }

                        // Listen for saving state changes
                        Connections {
                            target: memoriesModel
                            function onRowSavingChanged(changedRow, saving) {
                                if (changedRow === cellDelegate.delegateRow) {
                                    cellDelegate.isSaving = saving
                                }
                            }
                        }

                        // Cell width comes from tableView.columnWidthProvider
                        implicitHeight: 35

                        // Cell color (alternatingRows is Qt 6.2+ only, use typeof guard)
                        color: {
                            if (typeof tableView.alternatingRows !== "undefined"
                                    && tableView.alternatingRows && delegateRow % 2) {
                                return Qt.lighter(palette.alternateBase, 1.02)
                            }
                            return palette.base
                        }

                        border.color: palette.mid

                        // Context menu
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    contextMenu.contextRow = delegateRow
                                    contextMenu.contextColumn = delegateColumn
                                    contextMenu.popup()
                                }
                            }
                        }

                        // Cell content
                        Loader {
                            anchors.fill: parent
                            anchors.margins: 2

                            property var cellData: model.display
                            property int cellRow: delegateRow
                            property int cellColumn: delegateColumn

                            sourceComponent: {
                                if (delegateColumn === 0) {
                                    return recallButtonComponent
                                }

                                if (!memoriesModel)
                                    return textDisplayComponent
                                var colType = memoriesModel.getColumnType(delegateColumn)
                                switch(colType) {
                                    case 0: return textInputComponent  // TypeText
                                    case 1: return comboBoxComponent   // TypeCombo
                                    case 2: return textInputComponent  // TypeFrequency
                                    case 3: return textInputComponent  // TypeNumeric
                                    default: return textDisplayComponent
                                }
                            }

                            onLoaded: {
                                if (item) {
                                    item.cellData = Qt.binding(function() { return model.display })
                                    item.cellRow = Qt.binding(function() { return delegateRow })
                                    item.cellColumn = Qt.binding(function() { return delegateColumn })
                                }
                            }
                        }

                        // Saving indicator - only show in first column
                        Rectangle {
                            anchors.fill: parent
                            visible: cellDelegate.isSaving && delegateColumn === 0
                            color: "transparent"
                            z: 100  // Ensure it's on top

                            // Semi-transparent highlight background
                            Rectangle {
                                anchors.fill: parent
                                color: palette.highlight
                                opacity: 0.2
                            }

                            // Spinner and text
                            Row {
                                anchors.centerIn: parent
                                spacing: 6

                                BusyIndicator {
                                    width: 24
                                    height: 24
                                    running: true
                                }

                                Label {
                                    text: qsTr("Saving...")
                                    font.italic: true
                                    font.bold: true
                                    color: palette.highlightedText
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }
            // Status bar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30  // Fixed height
                color: palette.window
                border.color: palette.mid
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 10

                    Label {
                        text: memoriesModel ? memoriesModel.statusText : ""
                        Layout.fillWidth: true
                    }

                    ProgressBar {
                        visible: memoriesModel ? memoriesModel.loading : false
                        from: 0
                        to: memoriesModel ? memoriesModel.maxProgress : 1
                        value: memoriesModel ? memoriesModel.progress : 0
                        Layout.preferredWidth: 200
                    }
                }
            }
        }
    }

    Component {
        id: recallButtonComponent
        Button {
            property var cellData
            property int cellRow
            property int cellColumn

            text: qsTr("Recall")
            anchors.centerIn: parent
            onClicked: memoriesModel.recallMemory(cellRow)
        }
    }

    Component {
        id: textDisplayComponent
        Label {
            property var cellData
            property int cellRow
            property int cellColumn

            text: cellData || ""
            anchors.fill: parent
            anchors.leftMargin: 4
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component {
        id: textInputComponent
        TextField {
            property var cellData
            property int cellRow
            property int cellColumn
            property string originalValue: cellData || ""

            text: cellData || ""
            enabled: memoriesModel ? (memoriesModel.canEdit && cellColumn !== 1) : false
            readOnly: memoriesModel ? (!memoriesModel.canEdit || cellColumn === 1) : true
            selectByMouse: true

            inputMask: memoriesModel ? memoriesModel.getInputMask(cellColumn) : ""

            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter

            // Update originalValue when cellData changes
            onCellDataChanged: {
                originalValue = cellData || ""
            }

            onEditingFinished: {
                // Only save if value actually changed
                if (text !== originalValue) {
                    memoriesModel.setData(memoriesModel.index(cellRow, cellColumn), text, Qt.EditRole)
                    originalValue = text  // Update after saving
                }
            }

            background: Rectangle {
                color: activeFocus ? palette.base : "transparent"
                border.color: activeFocus ? palette.highlight : "transparent"
                border.width: 1
            }
        }
    }

    Component {
        id: comboBoxComponent
        ComboBox {
            property var cellData
            property int cellRow
            property int cellColumn
            property string originalValue: cellData || ""

            model: memoriesModel ? memoriesModel.getComboOptions(cellColumn) : []
            currentIndex: {
                if (!memoriesModel)
                    return -1
                var options = memoriesModel.getComboOptions(cellColumn)
                var value = cellData || ""
                return Math.max(0, options.indexOf(value))
            }
            enabled: memoriesModel ? memoriesModel.canEdit : false
            anchors.fill: parent

            // Update originalValue when cellData changes
            onCellDataChanged: {
                originalValue = cellData || ""
            }

            onActivated: {
                // Only save if value actually changed
                if (currentText !== originalValue) {
                    memoriesModel.setData(memoriesModel.index(cellRow, cellColumn), currentText, Qt.EditRole)
                    originalValue = currentText  // Update after saving
                }
            }

            background: Rectangle { color: "transparent" }
        }
    }

    // Add this after the table delegates, before the FileDialog components

    Menu {
        id: contextMenu

        property int contextRow: -1
        property int contextColumn: -1

        MenuItem {
            text: qsTr("Use Current")
            onTriggered: {
                if (contextMenu.contextRow >= 0) {
                    memoriesModel.updateFromCurrent(contextMenu.contextRow)
                }
            }
        }

        MenuItem {
            text: qsTr("Add Current")
            onTriggered: {
                if (contextMenu.contextRow >= 0) {
                    memoriesModel.addCurrentToMemory(contextMenu.contextRow)
                }
            }
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Delete Memory")
            onTriggered: {
                if (contextMenu.contextRow >= 0) {
                    memoriesModel.deleteMemory(contextMenu.contextRow)
                }
            }
        }
    }

    FileDialog {
        id: csvImportDialog
        title: qsTr("Select CSV file to import")
        nameFilters: [qsTr("CSV files (*.csv)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            memoriesModel.importCSV(selectedFile.toString().replace("file:///", ""),
                                   allFieldsCheck.checked)
        }
    }

    FileDialog {
        id: csvExportDialog
        title: qsTr("Select CSV file to export")
        nameFilters: [qsTr("CSV files (*.csv)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            memoriesModel.exportCSV(selectedFile.toString().replace("file:///", ""),
                                   allFieldsCheck.checked)
        }
    }
}
