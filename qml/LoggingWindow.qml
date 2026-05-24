// LoggingWindow.qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: win
    width: 1037
    height: 300


    visible: false
    title: qsTr("Logging")

    Shortcut {
        sequence: "Ctrl+C"
        enabled: logView.hasSelection()
        onActivated: Clipboard.text = logView.selectedText()
    }

    Shortcut {
        sequence: "Escape"
        enabled: logView.hasSelection()
        onActivated: logView.clearSelection()
    }


    function scrollToBottomNow() {
        logView.positionViewAtEnd()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // ─────────────────────────────────────────────
        // Log output (ListView, fast + scrollable)
        // ─────────────────────────────────────────────
        ListView {
            id: logView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Logging.model

            property bool autoFollow: true

            // Selection
            property int selStart: -1
            property int selEnd: -1
            property bool selecting: false
            property int ctxRow: -1

            function clearSelection() { selStart = -1; selEnd = -1 }
            function hasSelection() { return selStart >= 0 && selEnd >= selStart }

            function clampIndex(i) {
                return Math.max(0, Math.min(count - 1, i))
            }

            function setSelection(a, b) {
                if (count <= 0) { clearSelection(); return; }
                a = clampIndex(a); b = clampIndex(b)
                selStart = Math.min(a, b)
                selEnd   = Math.max(a, b)
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                width: logView.width
                height: logText.implicitHeight + 4

                color: (index >= logView.selStart && index <= logView.selEnd)
                       ? Qt.rgba(0.2, 0.35, 0.6, 0.35)
                       : "transparent"


                Text {
                    id: logText
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    anchors.verticalCenter: parent.verticalCenter

                    // IMPORTANT: role names injected directly (no model.xxx)
                    text: line
                    font.family: "monospace"
                    wrapMode: Text.NoWrap
                    elide: Text.ElideNone

                    color: {
                        switch (type) {
                        case Logging.debugMsg:    return "#b0b0b0"
                        case Logging.infoMsg:     return win.palette.text
                        case Logging.warningMsg:  return "#ffd966"
                        case Logging.criticalMsg: return "#ff6b6b"
                        case Logging.fatalMsg:    return "#ff0000"
                        default:                  return win.palette.text
                        }
                    }
                }
            }

            Menu {
                id: logCtxMenu

                MenuItem {
                    text: qsTr("Copy selection")
                    enabled: logView.hasSelection()
                    onTriggered: Clipboard.text = Logging.model.rangeText(logView.selStart, logView.selEnd)
                }

                MenuItem {
                    text: qsTr("Copy line")
                    enabled: logView.ctxRow >= 0
                    onTriggered: Clipboard.text = Logging.model.lineAt(logView.ctxRow)
                }

                MenuSeparator {}

                MenuItem {
                    text: qsTr("Copy all (visible)")
                    enabled: logView.count > 0
                    onTriggered: Clipboard.text = Logging.model.allVisibleText()
                }

                MenuSeparator {}

                MenuItem {
                    text: qsTr("Clear selection")
                    enabled: logView.hasSelection()
                    onTriggered: logView.clearSelection()
                }
            }

            // Overlay to handle drag-select + right click menu.
            // This MUST prevent stealing; otherwise ListView eats drags as flicks.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                preventStealing: true

                function idxFromMouse(mouse) {
                    // Convert mouse coords (viewport) -> content coords
                    var p = logView.mapToItem(logView.contentItem, mouse.x, mouse.y)
                    return logView.indexAt(p.x, p.y)
                }

                function clampIndexFromMouse(mouse) {
                    var idx = idxFromMouse(mouse)
                    if (idx >= 0) return idx
                    // If dragging above or below, clamp to ends
                    if (mouse.y < 0) return 0
                    if (mouse.y > height) return Math.max(0, logView.count - 1)
                    return -1
                }

                onPressed: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        logView.ctxRow = clampIndexFromMouse(mouse)
                        logCtxMenu.x = mouse.x
                        logCtxMenu.y = mouse.y
                        logCtxMenu.open()
                        mouse.accepted = true
                        return
                    }

                    if (mouse.button === Qt.LeftButton) {
                        const idx = clampIndexFromMouse(mouse)
                        if (idx >= 0) {
                            logView.selecting = true
                            logView.interactive = false   // stop flick stealing the drag
                            logView.autoFollow = false
                            logView.setSelection(idx, idx)
                        }
                        mouse.accepted = true
                    }
                }

                onPositionChanged: (mouse) => {
                    if (!logView.selecting) return
                    const idx = clampIndexFromMouse(mouse)
                    if (idx >= 0)
                        logView.setSelection(logView.selStart, idx)
                    mouse.accepted = true
                }

                onReleased: (mouse) => {
                    if (mouse.button === Qt.LeftButton && logView.selecting) {
                        logView.selecting = false
                        logView.interactive = true
                        mouse.accepted = true
                    }
                }

                onDoubleClicked: (mouse) => {
                    const idx = clampIndexFromMouse(mouse)
                    if (idx >= 0)
                        Clipboard.text = Logging.model.lineAt(idx)
                }

            }

        }


        // ─────────────────────────────────────────────
        // Annotation row
        // ─────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: qsTr("Annotation:")
                Layout.minimumWidth: 75
                Layout.maximumWidth: 75
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: 290
                height: annotationField.implicitHeight

                TextField {
                    id: annotationField
                    Menu {
                        id: annotationMenu

                        MenuItem {
                            text: qsTr("Cut")
                            enabled: annotationField.selectedText.length > 0
                            onTriggered: {
                                Clipboard.text = annotationField.selectedText
                                annotationField.remove(annotationField.selectionStart, annotationField.selectionEnd)
                            }
                        }

                        MenuItem {
                            text: qsTr("Copy")
                            enabled: annotationField.selectedText.length > 0
                            onTriggered: Clipboard.text = annotationField.selectedText
                        }

                        MenuItem {
                            text: qsTr("Paste")
                            enabled: Clipboard.text.length > 0
                            onTriggered: annotationField.insert(annotationField.cursorPosition, Clipboard.text)
                        }

                        MenuSeparator {}

                        MenuItem {
                            text: qsTr("Select All")
                            onTriggered: annotationField.selectAll()
                        }
                    }

                    MouseArea {
                        anchors.fill: annotationField
                        acceptedButtons: Qt.RightButton
                        propagateComposedEvents: true

                        onPressed: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                annotationMenu.popup()
                                mouse.accepted = true
                            }
                        }
                    }

                    anchors.fill: parent
                    rightPadding: 26
                    onAccepted: Logging.annotateRequested(text)
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24
                    height: 24
                    text: "✕"
                    visible: annotationField.text.length > 0
                    onClicked: annotationField.clear()
                    focusPolicy: Qt.NoFocus
                }
            }

            Button {
                text: qsTr("Annotate")
                Layout.minimumWidth: 85
                Layout.maximumWidth: 85
                onClicked: Logging.annotateRequested(annotationField.text)
            }
        }

        // ─────────────────────────────────────────────
        // Controls row
        // ─────────────────────────────────────────────
        // ---- Controls area (wraps) ----
        // ---- Controls area (wraps, and reports height correctly to ColumnLayout) ----
        Item {
            id: controlsContainer
            Layout.fillWidth: true
            Layout.preferredHeight: controlsFlow.implicitHeight
            // Optional: if you want some bottom padding
            // Layout.bottomMargin: 2

            Flow {
                id: controlsFlow
                width: parent.width
                spacing: 10

                // Filters
                CheckBox {
                    text: qsTr("Debug")
                    checked: Logging.debugEnabled
                    onToggled: Logging.debugEnabled = checked
                }

                CheckBox {
                    text: qsTr("CommDebug")
                    checked: Logging.commDebugEnabled
                    onToggled: Logging.commDebugEnabled = checked
                }

                CheckBox {
                    text: qsTr("RigCtl Debug")
                    checked: Logging.rigctlDebugEnabled
                    onToggled: Logging.rigctlDebugEnabled = checked
                }

                // Filter field + clear
                Item {
                    width: 240
                    height: filterField.implicitHeight

                    TextField {
                        id: filterField
                        anchors.fill: parent
                        rightPadding: 26
                        placeholderText: qsTr("Filter…")
                        onTextChanged: Logging.model.filter = text
                    }

                    ToolButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24; height: 24
                        text: "✕"
                        visible: filterField.text.length > 0
                        onClicked: {
                            filterField.clear()
                            Logging.model.filter = ""
                        }
                        focusPolicy: Qt.NoFocus
                    }
                }

                CheckBox {
                    text: qsTr("Pause")
                    checked: Logging.model.paused
                    onToggled: Logging.model.paused = checked
                }

                CheckBox {
                    text: qsTr("Follow")
                    checked: logView.autoFollow
                    onToggled: {
                        logView.autoFollow = checked
                        if (checked)
                            Qt.callLater(scrollToBottomNow)
                    }
                }

                Button {
                    text: qsTr("Scroll Down")
                    onClicked: {
                        logView.autoFollow = true
                        Qt.callLater(scrollToBottomNow)
                    }
                }

                Button { text: qsTr("Clear"); onClicked: Logging.model.clear() }
                Button { text: qsTr("Open Log Directory"); onClicked: Logging.openLogDirectoryRequested() }
                Button { text: qsTr("Open Log"); onClicked: Logging.openLogFileRequested() }
                Button { text: qsTr("Copy Path"); onClicked: Logging.copyPathRequested() }
                Button { text: qsTr("Send to termbin.com"); onClicked: Logging.sendToTermbinRequested() }
            }
        }
    }

    // Auto-follow on new rows
    Connections {
        target: Logging.model
        function onRowsInserted() {
            if (logView.autoFollow)
                Qt.callLater(scrollToBottomNow)
        }
        function onModelReset() {
            if (logView.autoFollow)
                Qt.callLater(scrollToBottomNow)
        }
    }
}
