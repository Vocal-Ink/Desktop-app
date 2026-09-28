import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

ScrollPage {
    title: qsTr("Typing & text")
    subtitle: qsTr("Type less and sound right: word suggestions, abbreviations that expand, and how emoji and links are read.")

    Card {
        Layout.fillWidth: true
        title: qsTr("Word suggestions")
        subtitle: qsTr("Learned from what you say, on this computer only. Tab takes the first one, Alt+1–5 any of them.")
        iconName: "sparkles"
        SettingRow {
            title: qsTr("Suggestions to show")
            ValueSlider {
                label: qsTr("Suggestions to show")
                from: 0; to: 8
                format: (v) => v === 0 ? qsTr("Off") : Math.round(v)
                value: App.prefs["text/predictions"]
                onMoved: App.prefs["text/predictions"] = value
            }
        }
        SettingRow {
            title: qsTr("Words learned from you")
            description: qsTr("%n word(s) so far.", "", App.learnedWords)
            PillButton { small: true; kind: "ghost"; iconName: "trash-2"; text: qsTr("Forget them"); enabled: App.learnedWords > 0; onClicked: forget.open() }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("How messages are read")
        iconName: "type"
        SettingRow {
            title: qsTr("Capitalise sentences")
            description: qsTr("Helps voices get the rhythm right when you type in lower case.")
            Toggle { tip: qsTr("Capitalise sentences"); checked: App.prefs["text/autoCapitalize"] !== false; onToggled: App.prefs["text/autoCapitalize"] = checked }
        }
        SettingRow {
            title: qsTr("Emoji")
            Segmented {
                label: qsTr("Emoji")
                value: App.prefs["text/emoji"] || "speak"
                options: [{ value: "speak", label: qsTr("Say them") }, { value: "remove", label: qsTr("Skip them") }, { value: "keep", label: qsTr("Leave to the voice") }]
                onActivated: (v) => App.prefs["text/emoji"] = v
            }
        }
        SettingRow {
            title: qsTr("Links")
            Segmented {
                label: qsTr("Links")
                value: App.prefs["text/urls"] || "say"
                options: [{ value: "say", label: qsTr("Say “link”") }, { value: "remove", label: qsTr("Skip them") }, { value: "keep", label: qsTr("Read in full") }]
                onActivated: (v) => App.prefs["text/urls"] = v
            }
        }
        SettingRow {
            title: qsTr("A new message cuts off the current one")
            description: qsTr("Off: messages wait their turn.")
            Toggle { tip: qsTr("A new message cuts off the current one"); checked: App.prefs["text/interrupt"] === true; onToggled: App.prefs["text/interrupt"] = checked }
        }
        SettingRow {
            title: qsTr("Start speaking after the first sentence")
            description: qsTr("Long messages start sooner.")
            Toggle { tip: qsTr("Start speaking after the first sentence"); checked: App.prefs["tts/splitSentences"] !== false; onToggled: App.prefs["tts/splitSentences"] = checked }
        }
        SettingRow {
            title: qsTr("Clear the message box after speaking")
            Toggle { tip: qsTr("Clear the message box after speaking"); checked: App.prefs["ui/clearAfterSpeak"] !== false; onToggled: App.prefs["ui/clearAfterSpeak"] = checked }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Abbreviations")
        subtitle: qsTr("Type the short form, hear the long one. Matched as whole words.")
        iconName: "whole-word"
        MapEditor {
            Layout.fillWidth: true
            prefKey: "text/replacements"
            keyLabel: qsTr("Abbreviation")
            valueLabel: qsTr("Spoken as")
            keyPlaceholder: qsTr("brb")
            valuePlaceholder: qsTr("be right back")
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Variables")
        subtitle: qsTr("Write {name} in any message or phrase and it's replaced when spoken. Built in: {time}, {date}, {day}, {clipboard}, {voice}.")
        iconName: "sparkle"
        MapEditor {
            Layout.fillWidth: true
            prefKey: "text/variables"
            keyLabel: qsTr("Variable")
            valueLabel: qsTr("Value")
            keyPlaceholder: qsTr("name")
            valuePlaceholder: qsTr("e.g. Sam")
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: preview.implicitHeight + Theme.s3 * 2
            radius: Theme.radius
            color: Theme.sunken
            ColumnLayout {
                id: preview
                x: Theme.s3; y: Theme.s3
                width: parent.width - Theme.s3 * 2
                spacing: Theme.s1
                Field { id: tryIt; Layout.fillWidth: true; label: qsTr("Try a message"); placeholderText: qsTr("Try: hi, I'm {name}, it's {time} brb 😂") }
                Txt { Layout.fillWidth: true; role: "caption"; text: tryIt.text === "" ? qsTr("You'll see exactly what will be spoken.") : "→ " + App.prepare(tryIt.text) }
            }
        }
    }

    Sheet {
        id: forget
        title: qsTr("Forget learned words?")
        message: qsTr("Suggestions go back to the built-in dictionary. Your messages and phrases aren't touched.")
        iconName: "trash-2"
        footer: [
            PillButton { text: qsTr("Forget"); kind: "danger"; onClicked: { App.forgetLearnedWords(); forget.close() } },
            PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: forget.close() }
        ]
    }
}
