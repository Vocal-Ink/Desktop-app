import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    title: qsTr("Your real microphone")
    lead: qsTr("Sometimes you may want people to hear your actual mic too: a laugh, the room, your own voice. It's off unless you choose otherwise, and Vocal Ink always shows when it's live.")

    Segmented {
        label: qsTr("Real microphone mode")
        value: App.prefs["mic/mode"] || "off"
        options: [
            { value: "off", label: qsTr("Never"), icon: "mic-off" },
            { value: "hold", label: qsTr("While I hold a key"), icon: "hand" },
            { value: "toggle", label: qsTr("A key turns it on and off"), icon: "keyboard" }
        ]
        onActivated: (v) => App.prefs["mic/mode"] = v
    }

    Rectangle {
        visible: (App.prefs["mic/mode"] || "off") !== "off"
        Layout.fillWidth: true
        implicitHeight: w.implicitHeight + Theme.s4 * 2
        radius: Theme.radius
        color: Theme.liveWash
        border.color: Theme.alpha(Theme.live, 0.45)
        border.width: Theme.hairline
        RowLayout {
            id: w
            anchors.fill: parent
            anchors.margins: Theme.s4
            spacing: Theme.s3
            Icon { name: "shield-alert"; color: Theme.live; Layout.alignment: Qt.AlignTop }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                Txt { Layout.fillWidth: true; text: qsTr("While it's live, everyone in your call or stream hears your real microphone. A red MIC LIVE badge stays on top of everything and a tone plays, so it can't be left on by accident."); font.pixelSize: Theme.fsSm }
                RowLayout {
                    spacing: Theme.s2
                    Txt { text: App.prefs["mic/mode"] === "hold" ? qsTr("Hold:") : qsTr("Toggle:"); role: "label" }
                    ShortcutField {
                        readonly property string actionId: App.prefs["mic/mode"] === "hold" ? "mic.hold" : "mic.toggle"
                        label: qsTr("Real mic shortcut")
                        sequence: App.shortcuts[actionId] || ""
                        onRecorded: (seq) => App.keybinds.bind(actionId, seq, true)
                        onCleared: App.keybinds.clear(actionId)
                    }
                }
            }
        }
    }
    Txt { Layout.fillWidth: true; role: "caption"; text: qsTr("Change this any time in Audio & mic.") }
}
