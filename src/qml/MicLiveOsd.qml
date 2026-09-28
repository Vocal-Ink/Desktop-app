import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Ink.Core

// The on-screen "your real mic is live" badge. Floats over games, ignores
// clicks, stays while the mic is live, and says so briefly when it mutes.
Window {
    id: win
    width: badge.implicitWidth
    height: badge.implicitHeight
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    color: App.micLive ? Theme.live : Theme.raised
    title: qsTr("Microphone status")

    property bool live: false

    function flash(isLive) {
        live = isLive
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + 24)
        show()
        hideTimer.stop()
        if (!isLive)
            hideTimer.start()
    }

    Connections {
        target: App
        function onMicLiveChanged() {
            if (!App.micLive && win.visible && win.live)
                win.flash(false)
        }
    }

    Timer { id: hideTimer; interval: 1400; onTriggered: win.hide() }

    Rectangle {
        id: badge
        anchors.fill: parent
        implicitWidth: row.implicitWidth + Theme.s5 * 2
        implicitHeight: row.implicitHeight + Theme.s3 * 2
        color: win.live ? Theme.live : Theme.raised
        border.color: win.live ? Qt.darker(Theme.live, 1.3) : Theme.line
        border.width: 1

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: Theme.s3
            Rectangle {
                width: 12; height: 12; radius: 6
                color: win.live ? "#FFFFFF" : Theme.faint
                SequentialAnimation on opacity {
                    running: win.live && Theme.motionOn
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 600 }
                    NumberAnimation { to: 1; duration: 600 }
                }
            }
            Text {
                text: win.live ? qsTr("MIC LIVE") : qsTr("Mic muted")
                font.family: Theme.displayFont
                font.weight: Font.ExtraBold
                font.pixelSize: Math.round(20 * Theme.scale)
                color: win.live ? "#FFFFFF" : Theme.text
            }
            Text {
                visible: win.live
                text: qsTr("people can hear you")
                font.family: Theme.uiFont
                font.pixelSize: Theme.fsSm
                font.weight: Font.DemiBold
                color: "#FFFFFF"
                opacity: 0.9
            }
            // A tiny meter so you can see it's really picking you up.
            Rectangle {
                visible: win.live
                width: Math.round(60 * Theme.scale)
                height: 6
                radius: 3
                color: Qt.rgba(1, 1, 1, 0.3)
                Rectangle {
                    height: parent.height
                    radius: 3
                    width: parent.width * Math.min(1, Math.pow(App.micLiveLevel, 0.6))
                    color: "#FFFFFF"
                }
            }
        }
    }
}
