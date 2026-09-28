import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Add or edit a phrase: words, category, colour, shortcut, voice.
Sheet {
    id: sheet
    property int index: -1
    property string hotkey: ""
    property string tint: ""
    property string voiceKey: ""

    title: index < 0 ? qsTr("New phrase") : qsTr("Edit phrase")
    iconName: "message-square-text"
    preferredWidth: Math.round(560 * Math.min(1.4, Theme.scale))

    readonly property var colors: ["", "#8c52ff", "#FF6FB5", "#FF8A5B", "#FFC857", "#3FD6A6", "#48B8FF"]

    function openFor(i) {
        index = i
        const p = i >= 0 ? App.phrases.get(i) : { text: "", category: "", tint: "", hotkey: "", voiceKey: "" }
        textField.text = p.text
        categoryField.text = p.category || ""
        tint = p.tint || ""
        hotkey = p.hotkey || ""
        voiceKey = p.voiceKey || ""
        open()
        textField.forceActiveFocus()
    }
    function save() {
        if (textField.text.trim() === "")
            return
        if (index < 0)
            App.phrases.add(textField.text, categoryField.text, tint, hotkey, voiceKey)
        else
            App.phrases.update(index, textField.text, categoryField.text, tint, hotkey, voiceKey)
        close()
    }

    Txt { text: qsTr("What to say"); role: "label" }
    Field {
        id: textField
        Layout.fillWidth: true
        label: qsTr("Phrase text")
        placeholderText: qsTr("e.g. Be right back, grabbing water!")
        onAccepted: sheet.save()
    }
    Txt { text: qsTr("You can use {time}, {date}, {clipboard} and your own {variables}."); role: "caption"; Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Txt { text: qsTr("Category"); role: "label" }
            Field { id: categoryField; Layout.fillWidth: true; label: qsTr("Category"); placeholderText: qsTr("e.g. Stream") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s1
                Repeater {
                    model: App.phrases.categories
                    Chip { required property string modelData; text: modelData; selected: categoryField.text === modelData; onClicked: categoryField.text = modelData }
                }
            }
        }
        ColumnLayout {
            spacing: Theme.s1
            Layout.alignment: Qt.AlignTop
            Txt { text: qsTr("Colour"); role: "label" }
            Row {
                spacing: Theme.s1
                Repeater {
                    model: sheet.colors
                    Swatch {
                        required property string modelData
                        swatch: modelData
                        selected: sheet.tint === modelData
                        onClicked: sheet.tint = modelData
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        ColumnLayout {
            spacing: Theme.s1
            Txt { text: qsTr("Shortcut"); role: "label" }
            ShortcutField {
                label: qsTr("Phrase shortcut")
                sequence: sheet.hotkey
                onRecorded: (seq) => sheet.hotkey = seq
                onCleared: sheet.hotkey = ""
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Txt { text: qsTr("Voice"); role: "label" }
            Choice {
                Layout.fillWidth: true
                label: qsTr("Voice for this phrase")
                readonly property var keys: [""].concat(App.prefs["tts/favorites"] || [])
                model: keys.map(k => k === "" ? qsTr("My current voice") : App.voiceInfo(k).name)
                currentIndex: Math.max(0, keys.indexOf(sheet.voiceKey))
                onActivated: sheet.voiceKey = keys[currentIndex]
            }
        }
    }

    footer: [
        PillButton { text: sheet.index < 0 ? qsTr("Add phrase") : qsTr("Save"); kind: "primary"; enabled: textField.text.trim() !== ""; onClicked: sheet.save() },
        PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: sheet.close() },
        Item { Layout.fillWidth: true },
        PillButton {
            visible: sheet.index >= 0
            text: qsTr("Delete")
            kind: "danger"
            iconName: "trash-2"
            onClicked: { App.phrases.remove(sheet.index); sheet.close() }
        },
        PillButton {
            visible: sheet.index > 0
            small: true
            kind: "ghost"
            iconName: "arrow-left"
            tip: qsTr("Move earlier")
            onClicked: { App.phrases.move(sheet.index, sheet.index - 1); sheet.index-- }
        }
    ]
}
