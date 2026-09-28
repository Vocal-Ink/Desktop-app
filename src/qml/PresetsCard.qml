import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Saved voice setups.
Card {
    id: card
    title: qsTr("Presets")
    subtitle: qsTr("The first three have shortcuts. Apply one and the voice, speed, pitch and effect all switch at once.")
    iconName: "layers"

    Txt {
        visible: App.presets.count === 0
        text: qsTr("No presets yet. Tune your voice, then choose “Save as preset”.")
        color: Theme.muted
        Layout.fillWidth: true
    }

    Repeater {
        model: App.presets
        delegate: Rectangle {
            id: item
            required property int index
            required property string presetId
            required property string name
            required property string voiceKey
            required property int rate
            required property int pitch
            required property string effect
            Layout.fillWidth: true
            implicitHeight: row.implicitHeight + Theme.s3 * 2
            radius: Theme.radius
            color: Theme.raised
            border.color: Theme.line
            border.width: Theme.hairline

            RowLayout {
                id: row
                anchors.fill: parent
                anchors.margins: Theme.s3
                spacing: Theme.s3
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Txt { text: item.name; role: "label"; Layout.fillWidth: true }
                    Txt {
                        Layout.fillWidth: true
                        role: "caption"
                        text: [App.voiceInfo(item.voiceKey).name, qsTr("speed %1%").arg(item.rate), item.effect && item.effect !== "none" ? item.effect : ""].filter(s => s).join(" · ")
                    }
                }
                KeyCombo { visible: item.index < 3; sequence: App.prefs["keybinds/preset." + (item.index + 1)] || "" }
                PillButton { small: true; text: qsTr("Apply"); onClicked: App.applyPreset(item.presetId) }
                IconButton { small: true; iconName: "trash-2"; tip: qsTr("Delete preset “%1”").arg(item.name); onClicked: App.presets.remove(item.presetId) }
            }
        }
    }
}
