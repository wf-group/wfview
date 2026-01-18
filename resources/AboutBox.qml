import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: aboutBox
    width: 700
    height: 621
    
    // Properties that would be set from outside (like version info)
    property string wfviewVersion: "1.0.0"  // Set this from your application
    property string gitShort: "abc1234"     // Set this from your build system
    property string buildDate: "Jan 12 2026"
    property string buildTime: "12:00:00"
    property string buildUser: "builder"
    property string buildHost: "buildhost"
    property string qtVersion: "6.5.0"      // Set from Qt.application.version or similar
    
    // Helper function to build the about text
    function buildAboutText() {
        let head = "<html><head></head><body>";
        let copyright = "Copyright 2017-2024 Elliott H. Liggett, W6EL and Phil E. Taylor, M0VSE. All rights reserved.<br/>wfview source code is <a href=\"https://gitlab.com/eliggett/wfview/-/blob/master/LICENSE\">licensed</a> under the GNU GPLv3.";
        let scm = "<br/><br/>Source code and issues managed by Roeland Jansen, PA3MET";
        let doctest = "<br/><br/>Testing and development mentorship from Jim Nijkamp, PA8E.";
        
        let dedication = "<br/><br/><b>This version of wfview is dedicated to the natural pursuit of freedom by people everywhere.</b> " +
                        "<br/><br/>Special thanks to Tony Collen, N0RUA/AE0KW (SK), for his work on open890, which was the inspiration for our support of the Kenwood TS-890. " +
                        "<br/><br/>Special thanks to our translators:<br/>Siwij Cat TA1YEP (Turkish)<br/>OK2HAM (Czech)<br/>JG3HLX (Japanese)<br/>Dawid SQ6EMM (Polish)<br/>Jim PA8E (Dutch)";
        
        let ssCredit = "<br/><br/>Stylesheet <a href=\"https://github.com/ColinDuquesnoy/QDarkStyleSheet/tree/master/qdarkstyle\"  style=\"color: cyan;\">qdarkstyle</a> used under MIT license.";
        
        let website = "<br/><br/>Please visit <a href=\"https://wfview.org/\"  style=\"color: cyan;\">https://wfview.org/</a> for the latest information.";
        
        let donate = "<br/><br/>Join us on <a href=\"https://www.patreon.com/wfview\">Patreon</a> for a behind-the-scenes look at wfview development, nightly builds, and to support the software you love.";
        
        let docs = "<br/><br/>Be sure to check the <a href=\"https://wfview.org/wfview-user-manual/\"  style=\"color: cyan;\">User Manual</a> and <a href=\"https://forum.wfview.org/\"  style=\"color: cyan;\">the Forum</a> if you have any questions.";
        let support = "<br/><br/>For support, please visit <a href=\"https://forum.wfview.org/\">the official wfview support forum</a>.";
        
        // Short credit strings
        let rsCredit = "<br/><br/><a href=\"https://www.speex.org/\"  style=\"color: cyan;\">Speex</a> Resample library Copyright 2003-2008 Jean-Marc Valin";
        let rtaudiocredit = "<br/><br/>RT Audio, from <a href=\"https://www.music.mcgill.ca/~gary/rtaudio/index.html\">Gary P. Scavone</a>";
        let portaudiocredit = "<br/><br/>Port Audio, from <a href=\"http://portaudio.com\">The Port Audio Community</a>";
        let qcpcredit = "<br/><br/>The waterfall and spectrum plot graphics use QCustomPlot, from  <a href=\"https://www.qcustomplot.com/\">Emanuel Eichhammer</a>";
        let qtcredit = "<br/><br/>This copy of wfview was built against Qt version " + qtVersion;
        let hamlibcredit = "<br/><br/>wfview contains our own implementation of the Hamlib rigctl protocol which uses portions of code from <a href=\"https://hamlib.github.io/\">Hamlib</a><br/>Copyright (C) 2000,2001,2002,2003,2004,2005,2006,2007,2008,2009,2010,2011,2012 The Hamlib Group";
        let adpcmcredit = "<br/><br/>wfview contains the adpcm-xq audio encoder/decoder - Copyright (c) David Bryant All rights reserved.";
        
        // Acknowledgement
        let wfviewcommunityack = "<br/><br/>The developers of wfview wish to thank the many contributions from the wfview community at-large, including ideas, bug reports, and fixes.";
        let kappanhangack = "<br/><br/>Special thanks to Norbert Varga, and the <a href=\"https://github.com/nonoo/kappanhang\">nonoo/kappanhang team</a> for their initial work on the OEM protocol.";
        
        let kb3mmwCredit = "<br/><br/>Many thanks to KB3MMW who assisted with the reverse enginering of the Yaesu LAN protocol. Portions of his code which he has released under LGPL/GPL have been integrated within wfview <a href=\"https://forum.wfview.org/t/off-topic-yaesu-ft710-and-others/3275/46\">Forum post</a>";
        
        let sxcreditcopyright = "Speex copyright notice:\n" +
            "Copyright (C) 2003 Jean-Marc Valin\n" +
            "Redistribution and use in source and binary forms, with or without\n" +
            "modification, are permitted provided that the following conditions\n" +
            "are met:\n" +
            "- Redistributions of source code must retain the above copyright\n" +
            "notice, this list of conditions and the following disclaimer.\n" +
            "- Redistributions in binary form must reproduce the above copyright\n" +
            "notice, this list of conditions and the following disclaimer in the\n" +
            "documentation and/or other materials provided with the distribution.\n" +
            "- Neither the name of the Xiph.org Foundation nor the names of its\n" +
            "contributors may be used to endorse or promote products derived from\n" +
            "this software without specific prior written permission.\n" +
            "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n" +
            "``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n" +
            "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n" +
            "A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR\n" +
            "CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,\n" +
            "EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,\n" +
            "PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR\n" +
            "PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF\n" +
            "LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING\n" +
            "NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n" +
            "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\n";
        
        let freqCtlCredit = "/*" +
            "* Frequency controller widget (originally from CuteSDR)\n" +
            "*\n" +
            "* This code is used within wfview and was modified\n" +
            "* You can download the source code from here: \n" +
            "* https://gitlab.com/eliggett/wfview/\n" +
            "*\n" +
            "* Copyright 2010 Moe Wheatley AE4JY \n" +
            "* Copyright 2012-2017 Alexandru Csete OZ9AEC\n" +
            "* Copyright 2024 Phil Taylor M0VSE\n" +
            "* All rights reserved.\n" +
            "*\n" +
            "* This software is released under the \"Simplified BSD License\".\n" +
            "*\n" +
            "* Redistribution and use in source and binary forms, with or without\n" +
            "* modification, are permitted provided that the following conditions are met:\n" +
            "*\n" +
            "* 1. Redistributions of source code must retain the above copyright notice,\n" +
            "*    this list of conditions and the following disclaimer.\n" +
            "*\n" +
            "* 2. Redistributions in binary form must reproduce the above copyright notice,\n" +
            "*    this list of conditions and the following disclaimer in the documentation\n" +
            "*    and/or other materials provided with the distribution.\n" +
            "*\n" +
            "* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n" +
            "* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n" +
            "* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\n" +
            "* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE\n" +
            "* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR\n" +
            "* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF\n" +
            "* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n" +
            "* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN\n" +
            "* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\n" +
            "* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE\n" +
            "* POSSIBILITY OF SUCH DAMAGE.\n" +
            "*/\n";
        
        let ftdiCredit = "\n/**\n" +
            "* FT4222 support library (for FT-710 SPI support)\n" +
            "*\n" +
            "* Copyright (c) 2001-2015 Future Technology Devices International Limited\n" +
            "*\n" +
            "* THIS SOFTWARE IS PROVIDED BY FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED \"AS IS\"\n" +
            "* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n" +
            "* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL\n" +
            "* FUTURE TECHNOLOGY DEVICES INTERNATIONAL LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n" +
            "* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT\n" +
            "* OF SUBSTITUTE GOODS OR SERVICES LOSS OF USE, DATA, OR PROFITS OR BUSINESS INTERRUPTION)\n" +
            "* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR\n" +
            "* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,\n" +
            "* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n" +
            "*\n" +
            "* FTDI DRIVERS MAY BE USED ONLY IN CONJUNCTION WITH PRODUCTS BASED ON FTDI PARTS.\n" +
            "*\n" +
            "* FTDI DRIVERS MAY BE DISTRIBUTED IN ANY FORM AS LONG AS LICENSE INFORMATION IS NOT MODIFIED.\n" +
            "*/\n\n";
        
        let end = "</body></html>";
        
        // String it all together
        let aboutText = head + copyright + "\n" + "\n" + scm + "\n" + doctest + dedication + wfviewcommunityack;
        aboutText += website + "\n" + donate + "\n" + docs + support + "\n";
        aboutText += "\n" + ssCredit + "\n" + rsCredit + "\n";
        aboutText += rtaudiocredit;
        aboutText += portaudiocredit;
        aboutText += kappanhangack + kb3mmwCredit + qcpcredit + qtcredit + hamlibcredit + adpcmcredit;
        aboutText += "<br/><br/>";
        aboutText += "<pre>" + sxcreditcopyright + freqCtlCredit + ftdiCredit + "</pre>";
        aboutText += "<br/><br/>";
        aboutText += end;
        
        return aboutText;
    }
    
    function buildInfoText() {
        let gitcodelink = "<a href=\"https://gitlab.com/eliggett/wfview/-/tree/" + gitShort + "\"  style=\"color: cyan;\">";
        return "<br/><br/>Build " + gitcodelink + gitShort + "</a> on " + buildDate + " at " + buildTime + " by " + buildUser + "@" + buildHost;
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Logo Button
        Button {
            id: logoBtn
            Layout.fillWidth: true
            Layout.minimumHeight: 128
            Layout.preferredHeight: 128
            flat: true
            
            // Set the icon - you'll need to ensure the resource is accessible
            icon.source: "qrc:/resources/wfview.png"  // or use Image if resource path differs
            icon.width: 128
            icon.height: 128
            
            background: Rectangle {
                color: "transparent"
            }
            
            // Alternative if icon doesn't work - use Image directly
            /*
            contentItem: Item {
                Image {
                    anchors.centerIn: parent
                    source: "qrc:/resources/wfview.png"
                    width: 128
                    height: 128
                    fillMode: Image.PreserveAspectFit
                }
            }
            */
            
            onClicked: {
                Qt.openUrlExternally("https://www.wfview.org/")
            }
        }
        
        // Top Text Label
        Label {
            id: topText
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            text: "wfview version " + wfviewVersion
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
        }
        
        // Middle Text Box (ScrollView with TextArea for rich text)
        ScrollView {
            id: midTextBox
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            
            clip: true
            
            TextArea {
                id: textArea
                textFormat: TextEdit.RichText
                text: aboutBox.buildAboutText()
                readOnly: true
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                focus: true
                
                // Enable opening external links
                onLinkActivated: function(link) {
                    Qt.openUrlExternally(link)
                }
                
                // Change mouse cursor when hovering over links
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
        }
        
        // Bottom Text Label
        Label {
            id: bottomText
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            text: aboutBox.buildInfoText()
            textFormat: Text.RichText
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            
            // Enable clicking links in bottom text
            onLinkActivated: function(link) {
                Qt.openUrlExternally(link)
            }
            
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }
    }
    
    // Set window properties if this is used as a window
    Component.onCompleted: {
        // If this component is used in a Window or ApplicationWindow,
        // you can set the title like this (requires parent to be a Window):
        // if (parent && parent.title !== undefined) {
        //     parent.title = "About wfview"
        // }
    }
}
