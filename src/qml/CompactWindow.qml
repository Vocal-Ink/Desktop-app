import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Ink.Core

// A slim bar that floats over everything: type, speak, stop, dictate.
Window {
    id: win
    signal openMain()

    width: Math.round(620 * Math.min(1.3, Theme.scale))
    height: bar.implicitHeight
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.bg
    opacity: (App.prefs["ui/compactOpacity"] || 94) / 100
    title: qsTr("Vocal Ink compact bar")

    function toggle() {
        if (visible) { hide(); return }
        if (x === 0 && y === 0) {
            x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
            y = Math.round(Screen.virtualY + Screen.height - height - 96)
        }
        show()
        raise()
        requestActivate()
        field.forceActiveFocus()
    }

    Rectangle {
        id: bar
        anchors.fill: parent
        implicitHeight: row.implicitHeight + Theme.s2 * 2
        color: Theme.surface
        border.color: Theme.line
        border.width: 1

        RowLayout {
            id: row
            anchors.fill: parent
            anchors.margins: Theme.s2
            spacing: Theme.s2

            // Drag handle: move the bar anywhere.
            Item {
                Layout.preferredWidth: Math.round(18 * Theme.scale)
                Layout.fillHeight: true
                Icon { anchors.centerIn: parent; name: "grip-vertical"; color: Theme.faint; size: Math.round(16 * Theme.scale) }
                DragHandler { target: null; onActiveChanged: if (active) win.startSystemMove() }
                HoverHandler { cursorShape: Qt.SizeAllCursor }
            }
            VoiceAvatar {
                size: Math.round(28 * Theme.scale)
                name: App.voiceName
                initials: App.voiceInfo(App.voiceKey).initials || ""
                local: App.voiceInfo(App.voiceKey).local || false
                speaking: App.speaking
            }
            Field {
                id: field
                Layout.fillWidth: true
                label: qsTr("Message")
                placeholderText: App.listening ? qsTr("Listening…") : qsTr("Type and press Enter")
                onAccepted: { if (text.trim() !== "") { App.speak(text); clear() } }
                Keys.onEscapePressed: App.stop()
            }
            InkWave {
                Layout.preferredWidth: Math.round(90 * Theme.scale)
                Layout.fillHeight: true
                visible: App.prefs["ui/waveStyle"] !== "off"
                style: App.prefs["ui/waveStyle"] || "ink"
                running: App.speaking || App.listening
                level: App.listening ? App.micLevel : App.outputLevel
                color: Theme.accent
                sheen: Theme.sheen
                thickness: height * 0.4
                animated: !Theme.reducedMotion
            }
            MicButton {}
            IconButton { iconName: "square"; tip: qsTr("Stop speaking"); enabled: App.speaking; onClicked: App.stop() }
            IconButton { iconName: "maximize-2"; tip: qsTr("Open Vocal Ink"); onClicked: win.openMain() }
            IconButton { iconName: "x"; tip: qsTr("Close the compact bar"); onClicked: win.hide() }
        }
    }

    Connections {
        target: App
        function onTranscriptReady(text) { if (win.visible && win.active) field.insert(field.length, (field.length ? " " : "") + text) }
    }
}
