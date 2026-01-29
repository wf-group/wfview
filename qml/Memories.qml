// Memories.qml
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 2.15
import Qt.labs.platform 1.1 as PLATFORM
import WFVIEW 1.0

Window {
    id: memoriesWindow

    width: 1113
    height: 520
    minimumWidth: 800
    minimumHeight: 400

    maximumWidth: {
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

    Component.onCompleted: {
        if (memoriesModel) {
            memoriesModel.populate()
        }
    }

    Connections {
        target: MainController

        function onMemoryReceived(mem) {
            memoriesModel.receiveMemory(mem)
        }

        function onMemoryNameReceived(tag) {
            memoriesModel.receiveMemoryName(tag)
        }

        function onMemorySplitReceived(split) {
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
                    model: memoriesModel.groupNames
                    visible: memoriesModel.hasMemoryGroups
                    currentIndex: memoriesModel.currentGroup
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0) {
                            memoriesModel.currentGroup = currentIndex
                        }
                    }
                    Layout.preferredWidth: 150
                }

                Item { Layout.fillWidth: true }

                CheckBox {
                    text: qsTr("Disable Editing")
                    checked: !memoriesModel.canEdit
                    onCheckedChanged: memoriesModel.canEdit = !checked
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: memoriesModel.isScanning ? qsTr("Stop Scan") : qsTr("Start Scan")
                    visible: memoriesModel.canScan
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
                    enabled: memoriesModel.canEdit
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
                    text: memoriesModel.memoryModeActive ?
                          qsTr("Select VFO Mode") : qsTr("Select Memory Mode")
                    checkable: true
                    checked: memoriesModel.memoryModeActive
                    onCheckedChanged: memoriesModel.memoryModeActive = checked
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            HorizontalHeaderView {
                id: headerView
                syncView: tableView
                Layout.fillWidth: true
                Layout.preferredHeight: 30

                delegate: Rectangle {
                    implicitHeight: 30
                    color: palette.button
                    border.color: palette.mid

                    Text {
                        anchors.centerIn: parent
                        text: memoriesModel.getColumnName(index)
                        font.bold: true
                        color: palette.buttonText
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
                    //alternatingRows: true
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

                    Component.onCompleted: {
                        updateColumnWidths()
                    }

                    function updateColumnWidths() {
                        for (var i = 0; i < memoriesModel.columnCount(); i++) {
                            if (!memoriesModel.isColumnVisible(i)) {
                                tableView.setColumnWidth(i, 0)
                            }
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
                            if (delegateRow >= 0 && delegateRow < memoriesModel.rowCount()) {
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

                        // Cell dimensions
                        implicitWidth: {
                            if (!memoriesModel.isColumnVisible(delegateColumn)) {
                                return 0
                            }

                            switch(delegateColumn) {
                                case 0: return 80
                                case 1: return 60
                                case 2: return 120
                                case 7: return 100
                                default:
                                    if (memoriesModel.visibleColumnCount > 15) {
                                        return 60
                                    }
                                    return 80
                            }
                        }
                        implicitHeight: 35

                        // Cell color
                        color: {
                            if (tableView.alternatingRows && delegateRow % 2) {
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
                                    text: "Saving..."
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
                        text: memoriesModel.statusText
                        Layout.fillWidth: true
                    }

                    ProgressBar {
                        visible: memoriesModel.loading
                        from: 0
                        to: memoriesModel.maxProgress
                        value: memoriesModel.progress
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
            enabled: memoriesModel.canEdit && cellColumn !== 1
            readOnly: !memoriesModel.canEdit || cellColumn === 1
            selectByMouse: true

            inputMask: memoriesModel.getInputMask(cellColumn)

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

            model: memoriesModel.getComboOptions(cellColumn)
            currentIndex: {
                var options = memoriesModel.getComboOptions(cellColumn)
                var value = cellData || ""
                return Math.max(0, options.indexOf(value))
            }
            enabled: memoriesModel.canEdit
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

    PLATFORM.FileDialog {
        id: csvImportDialog
        title: qsTr("Select CSV file to import")
        nameFilters: ["CSV files (*.csv)"]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            memoriesModel.importCSV(selectedFile.toString().replace("file:///", ""),
                                   allFieldsCheck.checked)
        }
    }

    PLATFORM.FileDialog {
        id: csvExportDialog
        title: qsTr("Select CSV file to export")
        nameFilters: ["CSV files (*.csv)"]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            memoriesModel.exportCSV(selectedFile.toString().replace("file:///", ""),
                                   allFieldsCheck.checked)
        }
    }
}
