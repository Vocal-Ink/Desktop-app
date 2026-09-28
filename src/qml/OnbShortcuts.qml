import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    title: qsTr("Shortcuts that work everywhere")
    lead: App.hotkeysSupported ? qsTr("These work even while a game or another app has focus. Click one and press new keys to change it.")
                               : App.hotkeysUnsupportedReason

    Card {
        Layout.fillWidth: true
        pad: Theme.s4
        Repeater {
            model: [
                { id: "window.quickType", text: qsTr("Pop up a box to type over any app") },
                { id: "listen.ptt", text: qsTr("Hold to dictate") },
                { id: "speak.stop", text: qsTr("Stop speaking") },
                { id: "speak.repeat", text: qsTr("Say the last message again") },
                { id: "panic.mute", text: qsTr("Stop everything and mute my mic") },
                { id: "window.toggle", text: qsTr("Show or hide Vocal Ink") }
            ]
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: Theme.s3
                Txt { text: modelData.text; Layout.fillWidth: true }
                ShortcutField {
                    label: modelData.text
                    sequence: App.shortcuts[modelData.id] || ""
                    onRecorded: (seq) => App.keybinds.bind(modelData.id, seq, true)
                    onCleared: App.keybinds.clear(modelData.id)
                }
            }
        }
    }
    Txt { Layout.fillWidth: true; role: "caption"; text: qsTr("Phrases and sounds can have shortcuts too. There are 26 more actions in Settings → Shortcuts.") }
}
