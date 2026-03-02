import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    
    // Main content
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        
        // No controllers message (shown when empty)
        Label {
            id: noControllersLabel
            text: "No USB controller found"
            color: "gray"
            font.pixelSize: 16
            Layout.alignment: Qt.AlignHCenter
            visible: controllerTabBar.count === 0
        }
        
        // Tab bar for multiple controllers
        TabBar {
            id: controllerTabBar
            Layout.fillWidth: true
            visible: count > 0
            
            Repeater {
                model: controllerController.deviceModel
                
                TabButton {
                    text: model.deviceName
                    width: implicitWidth
                }
            }
        }
        
        // Stack layout for tab content
        StackLayout {
            id: stackLayout
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: controllerTabBar.currentIndex
            visible: controllerTabBar.count > 0
            
            Repeater {
                model: controllerController.deviceModel
                
                // Individual controller tab content
                Item {
                    id: deviceTab
                    
                    required property int index
                    required property string deviceName
                    required property string devicePath
                    required property bool connected
                    required property int currentPage
                    required property int totalPages
                    required property bool disabled
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        
                        // Top bar with connection status and disable checkbox
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            
                            Label {
                                id: connectionStatus
                                text: deviceTab.connected ? "Connected" : "Not Connected"
                                color: deviceTab.connected ? "green" : "red"
                                font.bold: true
                            }
                            
                            Item { Layout.fillWidth: true }
                            
                            CheckBox {
                                id: disableCheckbox
                                text: "Disable"
                                checked: deviceTab.disabled
                                onCheckedChanged: {
                                    controllerController.setDeviceDisabled(deviceTab.devicePath, checked)
                                }
                            }
                        }
                        
                        // Main widget container
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            enabled: !deviceTab.disabled
                            
                            ColumnLayout {
                                width: parent.width
                                spacing: 15
                                
                                // Controller image view with clickable overlays
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 400
                                    color: "#f0f0f0"
                                    border.color: "#ccc"
                                    border.width: 1
                                    
                                    // Background image showing the controller
                                    Image {
                                        id: controllerImage
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        fillMode: Image.PreserveAspectFit
                                        cache: true
                                        source: controllerController.getControllerImagePath(deviceTab.devicePath)
                                        
                                        // Transparent overlay with text labels for buttons/knobs
                                        Repeater {
                                            model: controllerController.getButtonsForPage(deviceTab.devicePath, deviceTab.currentPage)
                                            
                                            delegate: Item {
                                                required property int buttonX
                                                required property int buttonY
                                                required property int buttonWidth
                                                required property int buttonHeight
                                                required property string buttonText
                                                required property int buttonNum
                                                required property bool buttonIsOn
                                                required property color textColor
                                                
                                                x: buttonX
                                                y: buttonY
                                                width: buttonWidth
                                                height: buttonHeight
                                                
                                                // Text label overlay
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: buttonText
                                                    color: textColor
                                                    font.pixelSize: 10
                                                    font.bold: true
                                                    style: Text.Outline
                                                    styleColor: "white"
                                                }
                                                
                                                // Clickable area
                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                                    
                                                    onPressed: (mouse) => {
                                                        if (mouse.button === Qt.RightButton) {
                                                            controllerController.showContextMenu(
                                                                deviceTab.devicePath,
                                                                Qt.point(x + mouse.x, y + mouse.y)
                                                            )
                                                        } else if (mouse.button === Qt.LeftButton) {
                                                            controllerController.buttonPressed(
                                                                deviceTab.devicePath,
                                                                Qt.point(x + mouse.x, y + mouse.y),
                                                                true
                                                            )
                                                        }
                                                    }
                                                    
                                                    onReleased: (mouse) => {
                                                        if (mouse.button === Qt.LeftButton) {
                                                            controllerController.buttonPressed(
                                                                deviceTab.devicePath,
                                                                Qt.point(x + mouse.x, y + mouse.y),
                                                                false
                                                            )
                                                        }
                                                    }
                                                    
                                                    // Visual feedback on hover
                                                    hoverEnabled: true
                                                    onEntered: parent.opacity = 0.8
                                                    onExited: parent.opacity = 1.0
                                                }
                                            }
                                        }
                                        
                                        // Knobs overlay
                                        Repeater {
                                            model: controllerController.getKnobsForPage(deviceTab.devicePath, deviceTab.currentPage)
                                            
                                            delegate: Item {
                                                required property int knobX
                                                required property int knobY
                                                required property int knobWidth
                                                required property int knobHeight
                                                required property string knobText
                                                required property int knobNum
                                                required property color textColor
                                                
                                                x: knobX
                                                y: knobY
                                                width: knobWidth
                                                height: knobHeight
                                                
                                                // Text label
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: knobText
                                                    color: textColor
                                                    font.pixelSize: 9
                                                    font.bold: true
                                                    style: Text.Outline
                                                    styleColor: "white"
                                                }
                                                
                                                // Clickable area for configuration
                                                MouseArea {
                                                    anchors.fill: parent
                                                    acceptedButtons: Qt.RightButton
                                                    
                                                    onPressed: (mouse) => {
                                                        if (mouse.button === Qt.RightButton) {
                                                            controllerController.showContextMenu(
                                                                deviceTab.devicePath,
                                                                Qt.point(x + mouse.x, y + mouse.y)
                                                            )
                                                        }
                                                    }
                                                    
                                                    hoverEnabled: true
                                                    onEntered: parent.opacity = 0.8
                                                    onExited: parent.opacity = 1.0
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // Page selector
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10
                                    visible: deviceTab.totalPages > 1
                                    
                                    Label {
                                        text: "Page:"
                                    }
                                    
                                    SpinBox {
                                        id: pageSpinner
                                        from: 1
                                        to: deviceTab.totalPages
                                        value: deviceTab.currentPage
                                        onValueChanged: {
                                            if (value !== deviceTab.currentPage) {
                                                controllerController.setPage(deviceTab.devicePath, value)
                                            }
                                        }
                                    }
                                    
                                    Item { Layout.fillWidth: true }
                                    
                                    Label {
                                        text: "Total Pages:"
                                    }
                                    
                                    SpinBox {
                                        id: pagesSpinner
                                        from: 1
                                        to: 10
                                        value: deviceTab.totalPages
                                        onValueChanged: {
                                            if (value !== deviceTab.totalPages) {
                                                controllerController.setTotalPages(deviceTab.devicePath, value)
                                            }
                                        }
                                    }
                                }
                                
                                // Device-specific settings grid
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 10
                                    rowSpacing: 10
                                    
                                    // Sensitivity slider
                                    Label {
                                        text: "Sensitivity:"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    
                                    Slider {
                                        id: sensitivitySlider
                                        Layout.fillWidth: true
                                        from: 0
                                        to: 100
                                        stepSize: 1
                                        value: 50
                                        onMoved: {
                                            controllerController.setSensitivity(deviceTab.devicePath, value)
                                        }
                                    }
                                    
                                    // Brightness
                                    Label {
                                        text: "Brightness:"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    
                                    ComboBox {
                                        id: brightnessCombo
                                        Layout.fillWidth: true
                                        model: ["Low", "Medium", "High"]
                                        onCurrentIndexChanged: {
                                            controllerController.setBrightness(deviceTab.devicePath, currentIndex)
                                        }
                                    }
                                    
                                    // Speed
                                    Label {
                                        text: "Speed:"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    
                                    ComboBox {
                                        id: speedCombo
                                        Layout.fillWidth: true
                                        model: ["Slow", "Normal", "Fast"]
                                        onCurrentIndexChanged: {
                                            controllerController.setSpeed(deviceTab.devicePath, currentIndex)
                                        }
                                    }
                                    
                                    // Orientation
                                    Label {
                                        text: "Orientation:"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    
                                    ComboBox {
                                        id: orientationCombo
                                        Layout.fillWidth: true
                                        model: ["0°", "90°", "180°", "270°"]
                                        onCurrentIndexChanged: {
                                            controllerController.setOrientation(deviceTab.devicePath, currentIndex)
                                        }
                                    }
                                    
                                    // Color picker
                                    Label {
                                        text: "Color:"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    
                                    Button {
                                        id: colorButton
                                        Layout.preferredWidth: 100
                                        Layout.preferredHeight: 30
                                        
                                        Rectangle {
                                            anchors.fill: parent
                                            color: "white"
                                            border.color: "#999"
                                            border.width: 1
                                        }
                                        
                                        onClicked: colorDialog.open()
                                        
                                        ColorDialog {
                                            id: colorDialog
                                            title: "Select Color"
                                            onAccepted: {
                                                controllerController.setColor(deviceTab.devicePath, selectedColor)
                                            }
                                        }
                                    }
                                    
                                    // Timeout
                                    Label {
                                        text: "Timeout (minutes):"
                                        Layout.alignment: Qt.AlignRight
                                    }
                                    
                                    SpinBox {
                                        id: timeoutSpinner
                                        from: 0
                                        to: 60
                                        value: 5
                                        onValueChanged: {
                                            controllerController.setTimeout(deviceTab.devicePath, value)
                                        }
                                    }
                                }
                                
                                // Help text
                                Label {
                                    Layout.fillWidth: true
                                    text: "Right-click buttons or knobs to configure. Left-click to simulate button press."
                                    wrapMode: Text.WordWrap
                                    color: "#666"
                                    font.italic: true
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Bottom buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            Button {
                text: "Backup"
                onClicked: {
                    backupDialog.open()
                }
            }
            
            Button {
                text: "Restore"
                onClicked: {
                    restoreDialog.open()
                }
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // File dialogs
    FileDialog {
        id: backupDialog
        title: "Select Backup Filename"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Backup Files (*.ini)"]
        onAccepted: {
            var path = stackLayout.currentIndex >= 0 ? 
                       controllerController.deviceModel.get(stackLayout.currentIndex).devicePath : ""
            controllerController.backupSettings(path, selectedFile)
        }
    }
    
    FileDialog {
        id: restoreDialog
        title: "Select Backup Filename"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Backup Files (*.ini)"]
        onAccepted: {
            var path = stackLayout.currentIndex >= 0 ? 
                       controllerController.deviceModel.get(stackLayout.currentIndex).devicePath : ""
            controllerController.restoreSettings(path, selectedFile)
        }
    }
    
    // Context menu popup for button/knob configuration
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
                text: "Configure Button"
                font.bold: true
                font.pixelSize: 14
            }
            
            // On Event
            RowLayout {
                id: onEventRow
                Layout.fillWidth: true
                visible: true
                
                Label {
                    text: "On:"
                    Layout.preferredWidth: 80
                }
                
                ComboBox {
                    id: onEventCombo
                    Layout.fillWidth: true
                    model: controllerController.commandModel
                    textRole: "text"
                    valueRole: "index"
                }
            }
            
            // Off Event
            RowLayout {
                id: offEventRow
                Layout.fillWidth: true
                visible: true
                
                Label {
                    text: "Off:"
                    Layout.preferredWidth: 80
                }
                
                ComboBox {
                    id: offEventCombo
                    Layout.fillWidth: true
                    model: controllerController.commandModel
                    textRole: "text"
                    valueRole: "index"
                }
            }
            
            // Knob Event
            RowLayout {
                id: knobEventRow
                Layout.fillWidth: true
                visible: false
                
                Label {
                    text: "Knob:"
                    Layout.preferredWidth: 80
                }
                
                ComboBox {
                    id: knobEventCombo
                    Layout.fillWidth: true
                    model: controllerController.knobCommandModel
                    textRole: "text"
                    valueRole: "index"
                }
            }
            
            // LED Number
            RowLayout {
                id: ledRow
                Layout.fillWidth: true
                visible: false
                
                Label {
                    text: "LED:"
                    Layout.preferredWidth: 80
                }
                
                SpinBox {
                    id: ledSpinner
                    from: 0
                    to: 16
                }
            }
            
            // Toggle checkbox
            CheckBox {
                id: toggleCheckbox
                text: "Toggle"
                visible: true
            }
            
            // Color buttons
            RowLayout {
                id: colorButtonRow
                Layout.fillWidth: true
                visible: false
                spacing: 10
                
                Button {
                    id: onColorButton
                    text: "Color"
                    Layout.fillWidth: true
                    
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        color: "white"
                        z: -1
                    }
                    
                    onClicked: onColorDialog.open()
                }
                
                Button {
                    id: offColorButton
                    text: "Pressed"
                    Layout.fillWidth: true
                    
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        color: "gray"
                        z: -1
                    }
                    
                    onClicked: offColorDialog.open()
                }
            }
            
            // Icon button
            RowLayout {
                id: iconRow
                Layout.fillWidth: true
                visible: false
                spacing: 10
                
                Button {
                    id: iconButton
                    text: "Icon"
                    onClicked: iconDialog.open()
                }
                
                Label {
                    id: iconLabel
                    text: "<None>"
                    Layout.fillWidth: true
                }
            }
            
            // Apply button
            Button {
                text: "Apply"
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    controllerController.applyButtonConfig(
                        onEventCombo.currentValue,
                        offEventCombo.currentValue,
                        knobEventCombo.currentValue,
                        toggleCheckbox.checked,
                        ledSpinner.value
                    )
                    configPopup.close()
                }
            }
        }
        
        ColorDialog {
            id: onColorDialog
            title: "Select On Color"
        }
        
        ColorDialog {
            id: offColorDialog
            title: "Select Off Color"
        }
        
        FileDialog {
            id: iconDialog
            title: "Select Icon"
            fileMode: FileDialog.OpenFile
            nameFilters: ["Image Files (*.png *.jpg *.jpeg *.bmp)"]
        }
    }
}
