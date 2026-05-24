import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import WFVIEW 1.0

ApplicationWindow {
    id: firstTimeSetupWindow
    title: qsTr("First Time Setup")
    width: 518
    height: 443

    modality: Qt.ApplicationModal
    flags: Qt.Dialog | Qt.FramelessWindowHint
    
    // Signals to emit
    signal exitProgram()
    signal showSettings(bool isNetwork)
    signal skipSetup()
    
    // Internal state
    property int setupState: 0  // 0 = initial, 1 = step2
    property bool isNetwork: true
    
    // Text content
    readonly property string serialText1: qsTr("Serial Port Name")
    readonly property string serialText2: qsTr("Baud Rate")
    readonly property string serialText3: ""
    
    readonly property string networkText1: qsTr("Radio IP address, UDP Port Numbers")
    readonly property string networkText2: qsTr("Radio Username, Radio Password")
    readonly property string networkText3: qsTr("Mic and Speaker on THIS PC")
    
    Rectangle {
        anchors.fill: parent
        color: palette.window
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20
            
            // Title
            Label {
                Layout.fillWidth: true
                text: qsTr("Welcome to wfview!")
                font.pixelSize: 24
                horizontalAlignment: Text.AlignHCenter
            }
            
            // Step 1: Connection type
            GroupBox {
                id: step1GroupBox
                Layout.fillWidth: true
                title: qsTr("How is your radio connected?")
                visible: setupState === 0
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    
                    RadioButton {
                        id: serialPortThisPC
                        text: qsTr("Serial Port on this PC")
                        onCheckedChanged: if (checked) isNetwork = false
                    }
                    
                    RadioButton {
                        id: usbPortThisPC
                        text: qsTr("USB Port on This PC")
                        onCheckedChanged: if (checked) isNetwork = false
                    }
                    
                    RadioButton {
                        id: ethernetNetwork
                        text: qsTr("Ethernet Network")
                        onCheckedChanged: if (checked) isNetwork = true
                    }
                    
                    RadioButton {
                        id: wifiNetwork
                        text: qsTr("WiFi Network")
                        checked: true
                        onCheckedChanged: if (checked) isNetwork = true
                    }
                }
            }
            
            // Step 2: What you'll need
            GroupBox {
                id: step2GroupBox
                Layout.fillWidth: true
                title: qsTr("You will need the following information:")
                visible: setupState === 1
                
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 15
                    
                    Label {
                        id: neededDetailsLabel1
                        Layout.fillWidth: true
                        text: isNetwork ? networkText1 : serialText1
                        wrapMode: Text.WordWrap
                        font.pixelSize: 14
                    }
                    
                    Label {
                        id: neededDetailsLabel2
                        Layout.fillWidth: true
                        text: isNetwork ? networkText2 : serialText2
                        wrapMode: Text.WordWrap
                        font.pixelSize: 14
                        visible: text !== ""
                    }
                    
                    Label {
                        id: neededDetailsLabel3
                        Layout.fillWidth: true
                        text: isNetwork ? networkText3 : serialText3
                        wrapMode: Text.WordWrap
                        font.pixelSize: 14
                        visible: text !== ""
                    }
                }
            }
            
            // Spacer
            Item {
                Layout.fillHeight: true
            }
            
            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                
                Button {
                    text: qsTr("Exit Program")
                    onClicked: {
                        exitProgram()
                        firstTimeSetupWindow.close()
                    }
                }
                
                Button {
                    text: qsTr("I'll Do This On My Own")
                    onClicked: {
                        skipSetup()
                        firstTimeSetupWindow.close()
                    }
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                Button {
                    id: backBtn
                    text: qsTr("Back")
                    visible: setupState === 1
                    onClicked: {
                        setupState = 0
                    }
                }
                
                Button {
                    id: nextBtn
                    text: setupState === 0 ? qsTr("Next") : qsTr("Finish")
                    highlighted: true
                    onClicked: {
                        if (setupState === 0) {
                            // Go to step 2
                            setupState = 1
                        } else {
                            // Finish
                            showSettings(isNetwork)
                            firstTimeSetupWindow.close()
                        }
                    }
                }
            }
        }
    }
}
