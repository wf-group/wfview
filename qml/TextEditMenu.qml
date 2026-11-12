import QtQuick 2.15
import QtQuick.Controls 2.15
import WFVIEW 1.0

// Right-click Undo/Cut/Copy/Paste menu for every text entry in a window.
// Qt Quick Controls before 6.9 have no built-in context menu for TextField,
// TextArea, or editable SpinBox/ComboBox, so place one of these as the last
// child of the window and point root at the item holding the fields:
//
//     ColumnLayout { id: pageRoot; ... }
//     TextEditMenu { root: pageRoot }
//
// A single field can also carry its own menu by placing one inside it:
//
//     TextField { id: hexField; TextEditMenu { field: hexField } }
//
// Only right presses are taken, and only when they land on an enabled text
// editor; anything else falls through to the items underneath.
//
// On Qt 6.9 and newer, TextField and TextArea (including an editable
// ComboBox's field) have Qt's own context menu, so those presses are left
// for Qt; plain TextInput/TextEdit editors, such as an editable SpinBox's,
// still get this menu.
MouseArea {
    id: textEditMenu

    property Item root: null
    property Item field: null
    readonly property bool nativeMenu: MainController.nativeTextContextMenu === true
    property Item target: null
    property int selStart: 0
    property int selEnd: 0
    readonly property bool hasSelection: selEnd > selStart
    readonly property bool editable: target !== null && !target.readOnly
    readonly property bool concealed: target !== null && target.echoMode !== undefined
                                      && target.echoMode !== TextInput.Normal

    anchors.fill: parent
    z: 1000
    acceptedButtons: Qt.RightButton

    // TextInput and TextEdit (and so TextField, TextArea, and the editors
    // inside SpinBox and ComboBox) all provide these.
    function isTextEditor(item) {
        return typeof item.cut === "function" && typeof item.paste === "function"
               && typeof item.selectAll === "function" && item.readOnly !== undefined
    }

    // Controls TextField/TextArea have placeholderText; the bare
    // TextInput/TextEdit types do not.
    function hasNativeMenu(item) {
        return nativeMenu && item.placeholderText !== undefined
    }

    // Deepest visible, enabled text editor under the point.
    function editorAt(x, y) {
        if (field)
            return field.enabled ? field : null
        if (!root)
            return null
        var found = null
        var item = root
        var p = mapToItem(root, x, y)
        while (item) {
            var child = item.childAt(p.x, p.y)
            if (!child)
                break
            p = item.mapToItem(child, p.x, p.y)
            item = child
            if (isTextEditor(item) && item.enabled)
                found = item
        }
        return found
    }

    function rememberSelection() {
        selStart = target.selectionStart
        selEnd = target.selectionEnd
    }

    // The menu takes focus, which can clear the field's selection, so put it
    // back before each edit.
    function restoreSelection() {
        target.forceActiveFocus()
        target.select(selStart, selEnd)
    }

    function edit(action) {
        restoreSelection()
        action()
        rememberSelection()
    }

    onPressed: function(mouse) {
        var t = editorAt(mouse.x, mouse.y)
        if (!t || hasNativeMenu(t)) {
            mouse.accepted = false
            return
        }
        target = t
        t.forceActiveFocus()
        // Like a desktop editor: clicking outside the selection moves the
        // cursor there, clicking inside keeps it.
        var local = mapToItem(t, mouse.x, mouse.y)
        var pos = t.positionAt(local.x, local.y)
        if (t.selectionStart === t.selectionEnd || pos < t.selectionStart || pos > t.selectionEnd)
            t.cursorPosition = pos
        rememberSelection()
        editMenu.popup()
    }

    Menu {
        id: editMenu

        onClosed: if (textEditMenu.target) textEditMenu.restoreSelection()

        MenuItem {
            text: qsTr("Undo")
            enabled: textEditMenu.editable && textEditMenu.target.canUndo
            onTriggered: textEditMenu.edit(function() { textEditMenu.target.undo() })
        }
        MenuItem {
            text: qsTr("Redo")
            enabled: textEditMenu.editable && textEditMenu.target.canRedo
            onTriggered: textEditMenu.edit(function() { textEditMenu.target.redo() })
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Cut")
            enabled: textEditMenu.editable && textEditMenu.hasSelection && !textEditMenu.concealed
            onTriggered: textEditMenu.edit(function() { textEditMenu.target.cut() })
        }
        MenuItem {
            text: qsTr("Copy")
            enabled: textEditMenu.hasSelection && !textEditMenu.concealed
            onTriggered: textEditMenu.edit(function() { textEditMenu.target.copy() })
        }
        MenuItem {
            text: qsTr("Paste")
            enabled: textEditMenu.editable && textEditMenu.target.canPaste
            onTriggered: textEditMenu.edit(function() { textEditMenu.target.paste() })
        }
        MenuItem {
            text: qsTr("Delete")
            enabled: textEditMenu.editable && textEditMenu.hasSelection
            onTriggered: textEditMenu.edit(function() {
                textEditMenu.target.remove(textEditMenu.selStart, textEditMenu.selEnd)
            })
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Select All")
            enabled: textEditMenu.target !== null && textEditMenu.target.length > 0
            onTriggered: textEditMenu.edit(function() { textEditMenu.target.selectAll() })
        }
    }
}
