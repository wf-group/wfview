import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    property var controllerController: null
    property int controllerRevision: 0
    property color currentButtonOnColor: "lightgray"
    property color currentButtonOffColor: "blue"
    property bool editingPressedButtonColor: false
    property var controllerButtonPalette: [
        "#f8f9fa", "#ced4da", "#495057", "#111111", "#d90429", "#f77f00",
        "#fcbf49", "#2b9348", "#00b4d8", "#0077b6", "#5a189a", "#ff5d8f",
        "#80ed99", "#90e0ef", "#ffd166", "#b5179e", "#4361ee", "#6c757d"
    ]

    function applyCurrentButtonColor(pressedColor, color) {
        if (pressedColor)
            root.currentButtonOffColor = color
        else
            root.currentButtonOnColor = color
        if (root.controllerController)
            root.controllerController.setCurrentButtonColor(pressedColor, color)
    }

    function setComboByValue(combo, value) {
        if (!combo)
            return
        var idx = combo.indexOfValue(value)
        combo.currentIndex = idx >= 0 ? idx : 0
    }

    Connections {
        target: root.controllerController
        ignoreUnknownSignals: true
        function onDeviceAdded(index) {
            root.controllerRevision += 1
        }
        function onDeviceRemoved(index) {
            root.controllerRevision += 1
        }
        function onDeviceUpdated(path) {
            root.controllerRevision += 1
        }
        function onShowConfigDialog(title, isButton, isKnob, onCommand, offCommand, knobCommand, toggle, ledNumber, showLed, showColor, showIcon, onColor, offColor) {
            popupTitle.text = title
            onEventRow.visible = isButton
            offEventRow.visible = isButton
            toggleCheckbox.visible = isButton
            knobEventRow.visible = isKnob
            ledRow.visible = isButton && showLed
            colorButtonRow.visible = isButton && showColor
            iconRow.visible = isButton && showIcon
            root.setComboByValue(onEventCombo, onCommand)
            root.setComboByValue(offEventCombo, offCommand)
            root.setComboByValue(knobEventCombo, knobCommand)
            toggleCheckbox.checked = toggle
            ledSpinner.value = Math.max(ledSpinner.from, Math.min(ledSpinner.to, ledNumber))
            root.currentButtonOnColor = onColor
            root.currentButtonOffColor = offColor
            configPopup.open()
        }
    }

    Label {
        id: noControllersLabel
        anchors.centerIn: parent
        text: qsTr("No USB controller found")
        color: palette.mid
        font.pixelSize: 16
        visible: controllerTabBar.count === 0
    }

    TabBar {
        id: controllerTabBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 10
        visible: count > 0

        Repeater {
            model: root.controllerController ? root.controllerController.deviceModel : null

            TabButton {
                required property string deviceName
                text: deviceName
                width: implicitWidth
            }
        }
    }

    RowLayout {
        id: bottomButtons
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 10
        spacing: 10
        visible: controllerTabBar.count > 0

        Button {
            text: qsTr("Backup")
            onClicked: backupDialog.open()
        }

        Button {
            text: qsTr("Restore")
            onClicked: restoreDialog.open()
        }

        Item {
            Layout.fillWidth: true
        }
    }

    StackLayout {
        id: stackLayout
        anchors.top: controllerTabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomButtons.top
        anchors.topMargin: 8
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.bottomMargin: 8
        currentIndex: controllerTabBar.currentIndex
        visible: controllerTabBar.count > 0

        Repeater {
            model: root.controllerController ? root.controllerController.deviceModel : null

            Item {
                id: deviceTab

                required property int index
                required property string deviceName
                required property string devicePath
                required property bool connected
                required property int currentPage
                required property int totalPages
                required property bool disabled
                required property int sensitivity
                required property int brightness
                required property int speed
                required property int orientation
                required property int timeout
                required property bool showSensitivity
                required property bool showBrightness
                required property bool showSpeed
                required property bool showOrientation
                required property bool showColor
                required property bool showTimeout

                RowLayout {
                    id: topStatus
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 10

                    Label {
                        text: deviceTab.connected ? qsTr("Connected") : qsTr("Not Connected")
                        color: deviceTab.connected ? "green" : "red"
                        font.bold: true
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    CheckBox {
                        text: qsTr("Disable")
                        checked: deviceTab.disabled
                        onCheckedChanged: {
                            if (root.controllerController)
                                root.controllerController.setDeviceDisabled(deviceTab.devicePath, checked)
                        }
                    }
                }

                RowLayout {
                    id: controllerSettingsPanel
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: Math.min(implicitWidth, parent.width, controllerFrame.width)
                    spacing: 4

                    Label {
                        text: qsTr("Page")
                        visible: deviceTab.totalPages > 1
                    }

                    SpinBox {
                        Layout.preferredWidth: 68
                        visible: deviceTab.totalPages > 1
                        from: 1
                        to: deviceTab.totalPages
                        value: deviceTab.currentPage
                        onValueChanged: {
                            if (value !== deviceTab.currentPage && root.controllerController)
                                root.controllerController.setPage(deviceTab.devicePath, value)
                        }
                    }

                    Label {
                        text: qsTr("Pages")
                    }

                    SpinBox {
                        Layout.preferredWidth: 68
                        from: 1
                        to: 10
                        value: deviceTab.totalPages
                        onValueChanged: {
                            if (value !== deviceTab.totalPages && root.controllerController)
                                root.controllerController.setTotalPages(deviceTab.devicePath, value)
                        }
                    }

                    Label {
                        text: qsTr("Sensitivity")
                        visible: deviceTab.showSensitivity
                    }

                    Slider {
                        Layout.preferredWidth: 110
                        visible: deviceTab.showSensitivity
                        from: 0
                        to: 100
                        stepSize: 1
                        value: deviceTab.sensitivity
                        onMoved: {
                            if (root.controllerController)
                                root.controllerController.setSensitivity(deviceTab.devicePath, value)
                        }
                    }

                    Label {
                        text: qsTr("Brightness")
                        visible: deviceTab.showBrightness
                    }

                    ComboBox {
                        Layout.preferredWidth: 90
                        visible: deviceTab.showBrightness
                        model: [qsTr("Off"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
                        currentIndex: Math.max(0, Math.min(count - 1, deviceTab.brightness))
                        onActivated: {
                            if (root.controllerController)
                                root.controllerController.setBrightness(deviceTab.devicePath, currentIndex)
                        }
                    }

                    Label {
                        text: qsTr("Speed")
                        visible: deviceTab.showSpeed
                    }

                    ComboBox {
                        Layout.preferredWidth: 104
                        visible: deviceTab.showSpeed
                        model: [qsTr("Fastest"), qsTr("Faster"), qsTr("Normal"), qsTr("Slower"), qsTr("Slowest")]
                        currentIndex: Math.max(0, Math.min(count - 1, deviceTab.speed))
                        onActivated: {
                            if (root.controllerController)
                                root.controllerController.setSpeed(deviceTab.devicePath, currentIndex)
                        }
                    }

                    Label {
                        text: qsTr("Orientation")
                        visible: deviceTab.showOrientation
                    }

                    ComboBox {
                        Layout.preferredWidth: 108
                        visible: deviceTab.showOrientation
                        model: [qsTr("Rotate 0"), qsTr("Rotate 90"), qsTr("Rotate 180"), qsTr("Rotate 270")]
                        currentIndex: Math.max(0, Math.min(count - 1, deviceTab.orientation))
                        onActivated: {
                            if (root.controllerController)
                                root.controllerController.setOrientation(deviceTab.devicePath, currentIndex)
                        }
                    }

                    Button {
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 28
                        visible: deviceTab.showColor

                        Rectangle {
                            anchors.fill: parent
                            color: "white"
                            border.color: "#999"
                            border.width: 1
                        }

                        onClicked: colorDialog.open()

                        ColorDialog {
                            id: colorDialog
                            title: qsTr("Select Color")
                            onAccepted: {
                                if (root.controllerController)
                                    root.controllerController.setColor(deviceTab.devicePath, selectedColor)
                            }
                        }
                    }

                    Label {
                        text: qsTr("Timeout")
                        visible: deviceTab.showTimeout
                    }

                    SpinBox {
                        Layout.preferredWidth: 84
                        visible: deviceTab.showTimeout
                        from: 0
                        to: 60
                        value: deviceTab.timeout
                        onValueChanged: {
                            if (value !== deviceTab.timeout && root.controllerController)
                                root.controllerController.setTimeout(deviceTab.devicePath, value)
                        }
                    }
                }

                Item {
                    id: controllerPreview
                    anchors.top: topStatus.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: controllerSettingsPanel.top
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    clip: true
                    enabled: !deviceTab.disabled

                    Rectangle {
                        id: controllerFrame
                        property real nativeWidth: controllerImage.status === Image.Ready ? controllerImage.sourceSize.width : 600
                        property real nativeHeight: controllerImage.status === Image.Ready ? controllerImage.sourceSize.height : 400
                        property real fitScale: nativeWidth > 0 && nativeHeight > 0
                                                ? Math.max(0.1, Math.min((controllerPreview.width - 2) / nativeWidth,
                                                                         (controllerPreview.height - 2) / nativeHeight))
                                                : 1
                        anchors.centerIn: parent
                        width: nativeWidth * fitScale + 2
                        height: nativeHeight * fitScale + 2
                        color: palette.base
                        border.color: palette.mid
                        border.width: 1

                        Item {
                            id: controllerLayer
                            x: 1
                            y: 1
                            width: controllerFrame.nativeWidth
                            height: controllerFrame.nativeHeight
                            scale: controllerFrame.fitScale
                            transformOrigin: Item.TopLeft

                            Image {
                                id: controllerImage
                                anchors.fill: parent
                                fillMode: Image.Stretch
                                cache: true
                                source: root.controllerController ? root.controllerController.getControllerImagePath(deviceTab.devicePath) : ""
                            }

                            Repeater {
                                model: {
                                    root.controllerRevision
                                    return root.controllerController ? root.controllerController.getButtonsForPage(deviceTab.devicePath, deviceTab.currentPage) : []
                                }

                                delegate: Item {
                                    id: buttonOverlay
                                    property var buttonData: modelData || ({})
                                    property int buttonX: Number(buttonData.buttonX || 0)
                                    property int buttonY: Number(buttonData.buttonY || 0)
                                    property int buttonWidth: Number(buttonData.buttonWidth || 0)
                                    property int buttonHeight: Number(buttonData.buttonHeight || 0)
                                    property string buttonText: buttonData.buttonText !== undefined ? String(buttonData.buttonText) : "None"
                                    property string buttonOnText: buttonData.buttonOnText !== undefined ? String(buttonData.buttonOnText) : "None"
                                    property string buttonOffText: buttonData.buttonOffText !== undefined ? String(buttonData.buttonOffText) : "None"
                                    property int buttonNum: Number(buttonData.buttonNum || 0)
                                    property bool buttonIsOn: Boolean(buttonData.buttonIsOn)
                                    property color textColor: buttonData.textColor || "#ffffff"
                                    property color buttonOnColor: buttonData.buttonOnColor || "#333333"
                                    property color buttonOffColor: buttonData.buttonOffColor || "#111111"
                                    property bool buttonGraphics: Boolean(buttonData.buttonGraphics)

                                    x: buttonX
                                    y: buttonY
                                    width: buttonWidth
                                    height: buttonHeight

                                    Rectangle {
                                        anchors.fill: parent
                                        color: buttonOverlay.buttonGraphics
                                               ? (buttonOverlay.buttonIsOn ? buttonOverlay.buttonOffColor : buttonOverlay.buttonOnColor)
                                               : ((buttonOverlay.buttonOnText === "None" && buttonOverlay.buttonOffText === "None") ? "#44000000" : "#66000000")
                                        border.color: buttonOverlay.buttonIsOn ? "#ffff66" : "#ccffffff"
                                        border.width: 1
                                        radius: 2
                                    }

                                    Text {
                                        anchors.top: parent.top
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: buttonDivider.top
                                        anchors.margins: 2
                                        text: buttonOverlay.buttonOnText
                                        color: buttonOverlay.buttonOnText === "None" ? "#eeeeee" : buttonOverlay.textColor
                                        font.pixelSize: buttonOverlay.buttonOnText === "None" ? Math.max(8, Math.min(11, parent.height * 0.22))
                                                                                            : Math.max(8, Math.min(13, parent.height * 0.28))
                                        font.bold: buttonOverlay.buttonOnText !== "None"
                                        renderType: Text.NativeRendering
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        id: buttonDivider
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 2
                                        anchors.rightMargin: 2
                                        height: 1
                                        color: "#88ffffff"
                                        visible: buttonOverlay.buttonOffText !== "None"
                                    }

                                    Text {
                                        anchors.top: buttonDivider.bottom
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 2
                                        text: buttonOverlay.buttonOffText === "None" ? "" : buttonOverlay.buttonOffText
                                        color: buttonOverlay.textColor
                                        font.pixelSize: Math.max(8, Math.min(13, parent.height * 0.28))
                                        font.bold: true
                                        renderType: Text.NativeRendering
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.top: buttonOverlay.buttonIsOn ? buttonDivider.bottom : parent.top
                                        anchors.bottom: buttonOverlay.buttonIsOn ? parent.bottom : buttonDivider.top
                                        anchors.margins: 1
                                        color: "#22ffff66"
                                        visible: buttonOverlay.buttonOffText !== "None"
                                        z: -0.5
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        hoverEnabled: true
                                        onEntered: parent.opacity = 0.8
                                        onExited: parent.opacity = 1.0

                                        onPressed: (mouse) => {
                                            if (!root.controllerController)
                                                return
                                            if (mouse.button === Qt.RightButton) {
                                                root.controllerController.showContextMenu(
                                                    deviceTab.devicePath,
                                                    Qt.point(buttonOverlay.x + mouse.x, buttonOverlay.y + mouse.y))
                                            } else if (mouse.button === Qt.LeftButton) {
                                                root.controllerController.buttonPressed(
                                                    deviceTab.devicePath,
                                                    Qt.point(buttonOverlay.x + mouse.x, buttonOverlay.y + mouse.y),
                                                    true)
                                            }
                                        }

                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.LeftButton && root.controllerController) {
                                                root.controllerController.buttonPressed(
                                                    deviceTab.devicePath,
                                                    Qt.point(buttonOverlay.x + mouse.x, buttonOverlay.y + mouse.y),
                                                    false)
                                            }
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: {
                                    root.controllerRevision
                                    return root.controllerController ? root.controllerController.getKnobsForPage(deviceTab.devicePath, deviceTab.currentPage) : []
                                }

                                delegate: Item {
                                    id: knobOverlay
                                    property var knobData: modelData || ({})
                                    property int knobX: Number(knobData.knobX || 0)
                                    property int knobY: Number(knobData.knobY || 0)
                                    property int knobWidth: Number(knobData.knobWidth || 0)
                                    property int knobHeight: Number(knobData.knobHeight || 0)
                                    property string knobText: knobData.knobText !== undefined ? String(knobData.knobText) : "None"
                                    property int knobNum: Number(knobData.knobNum || 0)
                                    property color textColor: knobData.textColor || "#ffffff"

                                    x: knobX
                                    y: knobY
                                    width: knobWidth
                                    height: knobHeight

                                    Rectangle {
                                        anchors.fill: parent
                                        color: knobOverlay.knobText === "None" ? "#44000000" : "#66000000"
                                        border.color: knobOverlay.knobText === "None" ? "#77999999" : "#ccffffff"
                                        border.width: 1
                                        radius: 2
                                        visible: knobOverlay.knobText !== ""
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        width: parent.width - 4
                                        height: parent.height - 4
                                        text: knobOverlay.knobText
                                        color: knobOverlay.knobText === "None" ? "#eeeeee" : knobOverlay.textColor
                                        font.pixelSize: knobOverlay.knobText === "None" ? Math.max(8, Math.min(11, parent.height * 0.28))
                                                                                       : Math.max(9, Math.min(13, parent.height * 0.35))
                                        font.bold: knobOverlay.knobText !== "None"
                                        renderType: Text.NativeRendering
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        wrapMode: Text.WordWrap
                                        elide: Text.ElideRight
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.RightButton
                                        hoverEnabled: true
                                        onEntered: parent.opacity = 0.8
                                        onExited: parent.opacity = 1.0

                                        onPressed: (mouse) => {
                                            if (mouse.button === Qt.RightButton && root.controllerController) {
                                                root.controllerController.showContextMenu(
                                                    deviceTab.devicePath,
                                                    Qt.point(knobOverlay.x + mouse.x, knobOverlay.y + mouse.y))
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: backupDialog
        title: qsTr("Select Backup Filename")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Backup Files (*.ini)")]
        onAccepted: {
            if (!root.controllerController)
                return
            var path = stackLayout.currentIndex >= 0 ?
                       root.controllerController.deviceModel.get(stackLayout.currentIndex).devicePath : ""
            root.controllerController.backupSettings(path, selectedFile)
        }
    }

    FileDialog {
        id: restoreDialog
        title: qsTr("Select Backup Filename")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Backup Files (*.ini)")]
        onAccepted: {
            if (!root.controllerController)
                return
            var path = stackLayout.currentIndex >= 0 ?
                       root.controllerController.deviceModel.get(stackLayout.currentIndex).devicePath : ""
            root.controllerController.restoreSettings(path, selectedFile)
        }
    }

    Popup {
        id: configPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        width: 350
        height: contentColumn.implicitHeight + 40

        x: (parent.width - width) / 2
        y: (parent.height - height) / 2

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            Label {
                id: popupTitle
                text: qsTr("Configure Button")
                font.bold: true
                font.pixelSize: 14
            }

            RowLayout {
                id: onEventRow
                Layout.fillWidth: true
                visible: true

                Label {
                    text: qsTr("On:")
                    Layout.preferredWidth: 80
                }

                ComboBox {
                    id: onEventCombo
                    Layout.fillWidth: true
                    model: root.controllerController ? root.controllerController.commandModel : null
                    textRole: "text"
                    valueRole: "index"
                }
            }

            RowLayout {
                id: offEventRow
                Layout.fillWidth: true
                visible: true

                Label {
                    text: qsTr("Off:")
                    Layout.preferredWidth: 80
                }

                ComboBox {
                    id: offEventCombo
                    Layout.fillWidth: true
                    model: root.controllerController ? root.controllerController.commandModel : null
                    textRole: "text"
                    valueRole: "index"
                }
            }

            RowLayout {
                id: knobEventRow
                Layout.fillWidth: true
                visible: false

                Label {
                    text: qsTr("Knob:")
                    Layout.preferredWidth: 80
                }

                ComboBox {
                    id: knobEventCombo
                    Layout.fillWidth: true
                    model: root.controllerController ? root.controllerController.knobCommandModel : null
                    textRole: "text"
                    valueRole: "index"
                }
            }

            RowLayout {
                id: ledRow
                Layout.fillWidth: true
                visible: false

                Label {
                    text: qsTr("LED:")
                    Layout.preferredWidth: 80
                }

                SpinBox {
                    id: ledSpinner
                    from: 0
                    to: 16
                }
            }

            CheckBox {
                id: toggleCheckbox
                text: qsTr("Toggle")
                visible: true
            }

            RowLayout {
                id: colorButtonRow
                Layout.fillWidth: true
                visible: false
                spacing: 10

                Button {
                    id: onColorButton
                    text: qsTr("Color")
                    Layout.fillWidth: true

                    Rectangle {
                        id: onColorSwatch
                        anchors.fill: parent
                        anchors.margins: 2
                        color: root.currentButtonOnColor
                        z: -1
                    }

                    onClicked: {
                        root.editingPressedButtonColor = false
                        buttonColorPopup.open()
                    }
                }

                Button {
                    id: offColorButton
                    text: qsTr("Pressed")
                    Layout.fillWidth: true

                    Rectangle {
                        id: offColorSwatch
                        anchors.fill: parent
                        anchors.margins: 2
                        color: root.currentButtonOffColor
                        z: -1
                    }

                    onClicked: {
                        root.editingPressedButtonColor = true
                        buttonColorPopup.open()
                    }
                }
            }

            RowLayout {
                id: iconRow
                Layout.fillWidth: true
                visible: false
                spacing: 10

                Button {
                    id: iconButton
                    text: qsTr("Icon")
                    onClicked: iconDialog.open()
                }

                Label {
                    id: iconLabel
                    text: qsTr("<None>")
                    Layout.fillWidth: true
                }
            }

            Button {
                text: qsTr("Apply")
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    if (!root.controllerController)
                        return
                    root.controllerController.applyButtonConfig(
                        onEventCombo.currentValue,
                        offEventCombo.currentValue,
                        knobEventCombo.currentValue,
                        toggleCheckbox.checked,
                        ledSpinner.value)
                    configPopup.close()
                }
            }
        }

        ColorDialog {
            id: onColorDialog
            title: qsTr("Select On Color")
            options: ColorDialog.ShowAlphaChannel
            onAccepted: {
                root.applyCurrentButtonColor(false, onColorDialog.selectedColor)
            }
        }

        ColorDialog {
            id: offColorDialog
            title: qsTr("Select Off Color")
            options: ColorDialog.ShowAlphaChannel
            onAccepted: {
                root.applyCurrentButtonColor(true, offColorDialog.selectedColor)
            }
        }

        Popup {
            id: buttonColorPopup
            modal: true
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            width: 246
            height: colorPickerColumn.implicitHeight + 24
            x: (configPopup.width - width) / 2
            y: (configPopup.height - height) / 2

            ColumnLayout {
                id: colorPickerColumn
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Label {
                    text: root.editingPressedButtonColor ? qsTr("Pressed Color") : qsTr("Button Color")
                    font.bold: true
                }

                GridLayout {
                    columns: 6
                    rowSpacing: 6
                    columnSpacing: 6

                    Repeater {
                        model: root.controllerButtonPalette

                        Rectangle {
                            required property string modelData
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            color: modelData
                            border.color: palette.mid
                            border.width: 1
                            radius: 3

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    root.applyCurrentButtonColor(root.editingPressedButtonColor, parent.color)
                                    buttonColorPopup.close()
                                }
                            }
                        }
                    }
                }

                Button {
                    text: qsTr("More...")
                    Layout.alignment: Qt.AlignRight
                    onClicked: {
                        buttonColorPopup.close()
                        if (root.editingPressedButtonColor) {
                            offColorDialog.selectedColor = root.currentButtonOffColor
                            offColorDialog.open()
                        } else {
                            onColorDialog.selectedColor = root.currentButtonOnColor
                            onColorDialog.open()
                        }
                    }
                }
            }
        }

        FileDialog {
            id: iconDialog
            title: qsTr("Select Icon")
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Image Files (*.png *.jpg *.jpeg *.bmp)")]
        }
    }
}
