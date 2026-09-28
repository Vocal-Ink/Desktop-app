import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// The voice in use and how it's tuned.
Card {
    id: card
    property bool showTuning: true

    title: qsTr("Your voice")
    iconName: "audio-lines"

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        VoiceAvatar {
            size: Math.round(52 * Theme.scale)
            name: App.voiceName
            initials: App.voiceInfo(App.voiceKey).initials || ""
            local: App.voiceInfo(App.voiceKey).local || false
            speaking: App.speaking
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Txt { text: App.voiceName || qsTr("No voice yet"); role: "title"; font.pixelSize: Theme.fsLg; Layout.fillWidth: true; elide: Text.ElideRight; wrapMode: Text.NoWrap }
            Txt { text: [App.voiceProviderName, App.voiceLanguage].filter(s => s).join(" · "); role: "caption"; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        visible: card.showTuning
        Layout.fillWidth: true
        spacing: Theme.s3

        component Labeled: ColumnLayout {
            property string label
            property string hint: ""
            default property alias control: holder.data
            spacing: Theme.s1
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                Txt { text: parent.parent.label; role: "label" }
                Item { Layout.fillWidth: true }
                Txt { text: parent.parent.hint; role: "caption"; visible: text !== "" }
            }
            Item { id: holder; Layout.fillWidth: true; implicitHeight: childrenRect.height }
        }

        Labeled {
            label: qsTr("Speed")
            ValueSlider {
                width: parent.width
                label: qsTr("Speed")
                from: 50; to: 200; stepSize: 5; suffix: "%"
                value: App.prefs["tts/rate"]
                onMoved: App.prefs["tts/rate"] = value
            }
        }
        Labeled {
            label: qsTr("Pitch")
            hint: qsTr("Not every voice can change pitch")
            ValueSlider {
                width: parent.width
                label: qsTr("Pitch")
                from: -50; to: 50; stepSize: 5
                format: (v) => (v > 0 ? "+" : "") + Math.round(v)
                value: App.prefs["tts/pitch"]
                onMoved: App.prefs["tts/pitch"] = value
            }
        }
        Labeled {
            label: qsTr("Volume into the virtual mic")
            ValueSlider {
                width: parent.width
                label: qsTr("Voice volume")
                from: 0; to: 100; suffix: "%"
                value: App.prefs["audio/outputVolume"]
                onMoved: App.prefs["audio/outputVolume"] = value
            }
        }
        Labeled {
            label: qsTr("Effect")
            Choice {
                width: parent.width
                label: qsTr("Voice effect")
                model: App.effects
                textRole: "name"
                valueRole: "id"
                currentIndex: indexOfValue(App.prefs["fx/effect"])
                onActivated: App.prefs["fx/effect"] = currentValue
            }
        }
        Labeled {
            visible: (App.prefs["fx/effect"] || "none") !== "none"
            label: qsTr("Effect strength")
            ValueSlider {
                width: parent.width
                label: qsTr("Effect strength")
                from: 0; to: 100; suffix: "%"
                value: App.prefs["fx/intensity"]
                onMoved: App.prefs["fx/intensity"] = value
            }
        }
        Txt {
            Layout.fillWidth: true
            role: "caption"
            text: {
                const list = App.effects
                for (let i = 0; i < list.length; ++i)
                    if (list[i].id === App.prefs["fx/effect"]) return list[i].description
                return ""
            }
            visible: text !== ""
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        PillButton { text: qsTr("Test"); iconName: "play"; onClicked: App.testOutput(); tip: qsTr("Speak a test line through your output") }
        PillButton { text: qsTr("Save as preset"); iconName: "plus"; kind: "ghost"; onClicked: presetSheet.open() }
    }

    Sheet {
        id: presetSheet
        title: qsTr("Save as preset")
        message: qsTr("A preset remembers the voice, speed, pitch and effect. Switch presets from Talk or with Ctrl+Alt+F1–F3.")
        iconName: "sliders-horizontal"
        Field { id: presetName; Layout.fillWidth: true; placeholderText: qsTr("e.g. Chill stream, Work calls"); label: qsTr("Preset name") }
        footer: [
            PillButton { text: qsTr("Save preset"); kind: "primary"; onClicked: { App.saveCurrentAsPreset(presetName.text); presetName.clear(); presetSheet.close() } },
            PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: presetSheet.close() }
        ]
        onOpened: presetName.forceActiveFocus()
    }
}
