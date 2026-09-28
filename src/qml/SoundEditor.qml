import QtQuick
import QtQuick.Layouts
import Ink.Core

// Rename a sound, colour it, give it a shortcut and a volume.
Sheet {
    id: sheet
    property string soundId: ""
    property string tint: ""
    property string hotkey: ""

    title: qsTr("Edit sound")
    iconName: "music"

    function openFor(id, name, tint_, hotkey_, gain) {
        soundId = id
        nameField.text = name
        tint = tint_ || ""
        hotkey = hotkey_ || ""
        gainSlider.value = Math.round((gain || 1) * 100)
        open()
        nameField.forceActiveFocus()
    }

    Txt { text: qsTr("Name"); role: "label" }
    Field { id: nameField; Layout.fillWidth: true; label: qsTr("Sound name") }
    Txt { text: qsTr("Colour"); role: "label" }
    Row {
        spacing: Theme.s1
        Repeater {
            model: ["", "#8c52ff", "#FF6FB5", "#FF8A5B", "#FFC857", "#3FD6A6", "#48B8FF"]
            Swatch { required property string modelData; swatch: modelData; selected: sheet.tint === modelData; onClicked: sheet.tint = modelData }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        ColumnLayout {
            spacing: Theme.s1
            Txt { text: qsTr("Shortcut"); role: "label" }
            ShortcutField { label: qsTr("Sound shortcut"); sequence: sheet.hotkey; onRecorded: (s) => sheet.hotkey = s; onCleared: sheet.hotkey = "" }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Txt { text: qsTr("Volume"); role: "label" }
            ValueSlider { id: gainSlider; Layout.fillWidth: true; label: qsTr("Sound volume"); from: 0; to: 200; suffix: "%" }
        }
    }
    footer: [
        PillButton { text: qsTr("Save"); kind: "primary"; onClicked: { App.sounds.update(sheet.soundId, nameField.text, sheet.tint, sheet.hotkey, gainSlider.value / 100); sheet.close() } },
        PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: sheet.close() },
        Item { Layout.fillWidth: true },
        PillButton { text: qsTr("Delete"); kind: "danger"; iconName: "trash-2"; onClicked: { App.sounds.remove(sheet.soundId); sheet.close() } }
    ]
}
