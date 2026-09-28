import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Ink.Core

// A small always-on-top box for typing over a game. Enter speaks and closes,
// Esc closes. Opened with the Quick type shortcut from anywhere.
Window {
    id: win
    width: Math.round(680 * Math.min(1.3, Theme.scale))
    height: box.implicitHeight + 2
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: Theme.line
    title: qsTr("Quick type")

    function open() {
        const s = Screen
        x = Math.round(s.virtualX + (s.width - width) / 2)
        y = Math.round(s.virtualY + s.height * 0.62)
        show()
        raise()
        requestActivate()
        composer.focusEditor()
    }

    onActiveChanged: if (!active && visible && composer.text === "") hide()

    Shortcut { sequence: "Esc"; onActivated: win.hide() }

    Rectangle {
        id: box
        x: 1; y: 1
        width: parent.width - 2
        implicitHeight: col.implicitHeight + Theme.s4 * 2
        color: Theme.bg

        ColumnLayout {
            id: col
            x: Theme.s4; y: Theme.s4
            width: parent.width - Theme.s4 * 2
            spacing: Theme.s2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                VoiceAvatar { size: Math.round(24 * Theme.scale); name: App.voiceName; initials: App.voiceInfo(App.voiceKey).initials || ""; local: App.voiceInfo(App.voiceKey).local || false }
                Txt { text: App.voiceName; role: "caption"; Layout.fillWidth: true; elide: Text.ElideRight; wrapMode: Text.NoWrap }
                KeyCombo { sequence: "Return" }
                Txt { text: qsTr("speak"); role: "caption" }
                KeyCombo { sequence: "Esc" }
                Txt { text: qsTr("close"); role: "caption" }
            }
            Composer {
                id: composer
                Layout.fillWidth: true
                compact: true
                onSpoke: win.hide()
            }
        }
    }

    Connections {
        target: App
        function onTranscriptReady(text) { if (win.visible) composer.insert(text) }
    }
}
