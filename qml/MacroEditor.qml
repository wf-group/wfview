// MacroEditor.qml
// Edits one macro: its name, its main-window button, and up to 20 ordered steps.
// Steps are edited in a local copy and only written back to settings on OK.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import WFVIEW 1.0

Dialog {
    id: editor

    property var controller
    property int macroId: -1
    property var commandOptions: []
    readonly property int maxSteps: controller ? controller.macroMaxSteps : 20

    title: qsTr("Edit Macro %1").arg(macroId)
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Overlay.overlay ? Math.min(980, Overlay.overlay.width - 40) : 980
    height: Overlay.overlay ? Math.min(640, Overlay.overlay.height - 40) : 640
    standardButtons: Dialog.Ok | Dialog.Cancel

    background: Rectangle {
        color: editor.palette.window
        border.color: editor.palette.highlight
        border.width: 1
        radius: 4
    }

    ListModel { id: stepsModel }
    ListModel { id: filteredOptions }

    function openMacro(id) {
        if (!controller)
            return
        const macro = controller.macro(id)
        if (!macro || macro.id === undefined)
            return

        macroId = id
        nameField.text = macro.name
        showButtonCheck.checked = Boolean(macro.showButton)
        commandOptions = MainController.macroCommandOptions()
        stepsModel.clear()
        const steps = macro.steps || []
        for (let i = 0; i < steps.length; ++i) {
            stepsModel.append({
                command: String(steps[i].command),
                parameter: String(steps[i].parameter),
                delayMs: Number(steps[i].delayMs)
            })
        }
        open()
    }

    function optionFor(value) {
        for (let i = 0; i < commandOptions.length; ++i) {
            if (commandOptions[i].value === value)
                return commandOptions[i]
        }
        return null
    }

    function addStep() {
        if (stepsModel.count >= maxSteps)
            return
        stepsModel.append({ command: "None", parameter: "", delayMs: 200 })
        stepsView.positionViewAtEnd()
    }

    function moveStep(row, direction) {
        const target = row + direction
        if (target < 0 || target >= stepsModel.count)
            return
        stepsModel.move(row, target, 1)
    }

    function setStepCommand(row, value) {
        if (row < 0 || row >= stepsModel.count)
            return
        if (stepsModel.get(row).command !== value)
            stepsModel.setProperty(row, "parameter", "")
        stepsModel.setProperty(row, "command", value)
        if (value === "raw" && controller && !controller.macroRawWarningShown)
            rawWarning.open()
    }

    // Abbreviations used in command names, expanded so a search for the full word finds them.
    readonly property var searchAliases: ({
        "freq": "frequency", "tx": "transmit", "transceiver": "ptt transmit receive", "ptt": "transmit receive", "rx": "receive receiver", "trx": "transceiver",
        "att": "attenuator", "atu": "tuner antenna", "ant": "antenna", "pwr": "power",
        "lvl": "level", "nb": "noise blanker", "nr": "noise reduction", "agc": "gain control",
        "mod": "modulation", "vfo": "frequency", "pbt": "passband twin", "apf": "audio peak filter",
        "rit": "receive incremental tuning", "xit": "transmit incremental tuning",
        "tsql": "tone squelch", "sql": "squelch", "af": "audio", "rf": "radio",
        "mic": "microphone", "comp": "compressor", "cw": "morse", "bsr": "band stack",
        "mem": "memory", "sel": "selected", "unsel": "unselected", "dtcs": "digital squelch"
    })

    function searchTextFor(option) {
        const base = (String(option.text) + " " + String(option.category)).toLowerCase()
        const words = base.split(/[^a-z0-9]+/).filter(w => w.length > 0)
        let extra = ""
        for (let i = 0; i < words.length; ++i) {
            const alias = searchAliases[words[i]]
            if (alias !== undefined)
                extra += " " + alias
        }
        return { text: base + extra, words: words }
    }

    // A term matches when it appears anywhere in the name (or its expanded abbreviations),
    // or when a word of the name is an abbreviation of it ("frequency" finds "Freq").
    function termMatches(term, entry) {
        if (entry.text.indexOf(term) >= 0)
            return true
        for (let i = 0; i < entry.words.length; ++i) {
            const word = entry.words[i]
            if (word.length >= 3 && term.startsWith(word))
                return true
        }
        return false
    }

    property var searchEntries: []

    onCommandOptionsChanged: {
        const entries = []
        for (let i = 0; i < commandOptions.length; ++i)
            entries.push(searchTextFor(commandOptions[i]))
        searchEntries = entries
    }

    function filterOptions(text) {
        filteredOptions.clear()
        const terms = String(text).toLowerCase().split(/[^a-z0-9%]+/).filter(t => t.length > 0)
        for (let i = 0; i < commandOptions.length; ++i) {
            const option = commandOptions[i]
            const entry = searchEntries[i] || searchTextFor(option)
            let match = true
            for (let t = 0; t < terms.length; ++t) {
                if (!termMatches(terms[t], entry)) {
                    match = false
                    break
                }
            }
            if (match)
                filteredOptions.append({ text: option.text, value: option.value, category: option.category })
        }
    }

    onAccepted: {
        if (!controller)
            return
        const steps = []
        for (let i = 0; i < stepsModel.count; ++i) {
            const step = stepsModel.get(i)
            steps.push({ command: step.command, parameter: step.parameter, delayMs: step.delayMs })
        }
        controller.updateMacro(macroId, nameField.text, showButtonCheck.checked, steps)
    }

    contentItem: ColumnLayout {
        id: editorColumn
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label { text: qsTr("Name") }
            TextField {
                id: nameField
                Layout.preferredWidth: 160
                maximumLength: editor.controller ? editor.controller.macroMaxNameLength : 12
                selectByMouse: true
                placeholderText: qsTr("Macro name")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Up to %1 characters. Used for the main window button.").arg(maximumLength)
            }
            CheckBox {
                id: showButtonCheck
                text: qsTr("Show button on main window")
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Steps run from top to bottom. After each step wfview waits for its delay, or for the minimum time between commands if that is longer.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("#"); Layout.preferredWidth: 28 }
            Label { text: qsTr("Command"); Layout.preferredWidth: 260 }
            Label { text: qsTr("Parameter"); Layout.fillWidth: true }
            Label { text: qsTr("Delay (ms)"); Layout.preferredWidth: 120 }
            Item { Layout.preferredWidth: 108 }
        }

        ListView {
            id: stepsView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: stepsModel
            ScrollBar.vertical: ScrollBar {}

            delegate: RowLayout {
                id: stepRow
                width: ListView.view.width
                spacing: 8

                readonly property int row: index
                readonly property var option: editor.optionFor(model.command)

                Label {
                    Layout.preferredWidth: 28
                    text: String(stepRow.row + 1)
                }

                Button {
                    id: commandButton
                    Layout.preferredWidth: 260
                    text: stepRow.option ? stepRow.option.text : model.command
                    contentItem: Text {
                        text: commandButton.text
                        color: commandButton.palette.buttonText
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }
                    ToolTip.visible: hovered && !stepRow.option && model.command !== "None"
                    ToolTip.text: qsTr("The connected radio does not offer this command.")
                    onClicked: commandPopup.openFor(stepRow.row, commandButton)
                }

                TextField {
                    Layout.fillWidth: true
                    text: model.parameter
                    enabled: stepRow.option ? Boolean(stepRow.option.hasParameter) : model.command !== "None"
                    placeholderText: stepRow.option ? String(stepRow.option.hint) : ""
                    selectByMouse: true
                    onTextEdited: stepsModel.setProperty(stepRow.row, "parameter", text)
                    ToolTip.visible: hovered && placeholderText.length > 0
                    ToolTip.text: placeholderText
                }

                SpinBox {
                    Layout.preferredWidth: 120
                    from: 50
                    to: 5000
                    stepSize: 10
                    editable: true
                    value: model.delayMs
                    onValueModified: stepsModel.setProperty(stepRow.row, "delayMs", value)
                }

                ToolButton {
                    Layout.preferredWidth: 32
                    text: "▲"
                    enabled: stepRow.row > 0
                    onClicked: editor.moveStep(stepRow.row, -1)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Move up")
                }
                ToolButton {
                    Layout.preferredWidth: 32
                    text: "▼"
                    enabled: stepRow.row < stepsModel.count - 1
                    onClicked: editor.moveStep(stepRow.row, 1)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Move down")
                }
                ToolButton {
                    Layout.preferredWidth: 32
                    text: "✕"
                    onClicked: stepsModel.remove(stepRow.row)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Remove step")
                }
            }

            Label {
                anchors.centerIn: parent
                visible: stepsModel.count === 0
                text: qsTr("No steps yet. Use Add Step to add one.")
                opacity: 0.7
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: qsTr("Add Step")
                enabled: stepsModel.count < editor.maxSteps
                onClicked: editor.addStep()
            }
            Label {
                text: qsTr("%1 of %2 steps").arg(stepsModel.count).arg(editor.maxSteps)
                opacity: 0.8
            }
            Item { Layout.fillWidth: true }
        }

        // One shared, searchable command chooser for all rows (the list has hundreds of entries).
        Popup {
            id: commandPopup
            property int row: -1
            width: 340
            height: Math.min(400, editorColumn.height)
            padding: 4
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

            function openFor(row, anchorItem) {
                commandPopup.row = row
                const p = anchorItem.mapToItem(editorColumn, 0, anchorItem.height)
                x = Math.max(0, Math.min(p.x, editorColumn.width - width))
                y = Math.max(0, Math.min(p.y, editorColumn.height - height))
                searchField.text = ""
                editor.filterOptions("")
                open()
                searchField.forceActiveFocus()
            }

            background: Rectangle {
                color: editor.palette.base
                border.color: editor.palette.highlight
                border.width: 1
                radius: 3
            }

            contentItem: ColumnLayout {
                spacing: 4

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search commands")
                    selectByMouse: true
                    onTextChanged: editor.filterOptions(text)
                }

                ListView {
                    id: optionsView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: filteredOptions
                    ScrollBar.vertical: ScrollBar {}
                    section.property: "category"
                    section.delegate: Label {
                        width: ListView.view.width
                        text: section
                        font.bold: true
                        padding: 4
                    }
                    delegate: ItemDelegate {
                        width: ListView.view.width
                        height: 28
                        text: model.text
                        highlighted: commandPopup.row >= 0 && commandPopup.row < stepsModel.count
                                     && stepsModel.get(commandPopup.row).command === model.value
                        onClicked: {
                            editor.setStepCommand(commandPopup.row, model.value)
                            commandPopup.close()
                        }
                    }
                }
            }
        }

        Dialog {
            id: rawWarning
            title: qsTr("Raw radio commands")
            modal: true
            anchors.centerIn: Overlay.overlay
            width: 480
            standardButtons: Dialog.Ok

            background: Rectangle {
                color: editor.palette.window
                border.color: editor.palette.highlight
                border.width: 1
                radius: 4
            }

            contentItem: Label {
                width: rawWarning.availableWidth
                wrapMode: Text.WordWrap
                text: qsTr("Raw radio commands are highly experimental. wfview sends them to the radio without "
                           + "checking them, and the radio's replies may confuse wfview's view of the radio.\n\n"
                           + "For Icom radios, enter the command bytes in hex, for example 0x14 0x0A 0x01 0x28 "
                           + "or 140A0128; wfview adds the CI-V address framing. For Kenwood and Yaesu radios, "
                           + "enter the CAT command text, for example FA00007215000;")
            }

            onClosed: if (editor.controller) editor.controller.macroRawWarningShown = true
        }
    }
}
