import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    id: step
    title: qsTr("Dictate instead of typing?")
    lead: qsTr("If you can whisper, mouth or speak softly, Vocal Ink can write it down for you to check and send. It runs on this computer.")

    property string heard: ""
    Connections { target: App; function onTranscriptReady(text) { step.heard = text } }

    Card {
        Layout.fillWidth: true
        title: App.sttReady ? qsTr("Dictation is ready") : qsTr("Download a speech model")
        subtitle: App.sttReady ? App.sttStatus : qsTr("“Base” is a good start: about 60 MB, quick on most computers.")
        iconName: "mic-vocal"
        RowLayout {
            spacing: Theme.s3
            visible: !App.sttReady
            PillButton { kind: "primary"; iconName: "download"; text: qsTr("Download the recommended model"); onClicked: App.whisperModels.downloadRecommended() }
            Txt { text: App.whisperModels.status; role: "caption"; Layout.fillWidth: true }
        }
        SettingRow {
            title: qsTr("Microphone")
            DeviceChoice { kind: "input"; model: App.inputs }
        }
        SettingRow {
            title: qsTr("How to start")
            Segmented {
                label: qsTr("Dictation mode")
                value: App.prefs["stt/mode"] || "ptt"
                options: [{ value: "ptt", label: qsTr("Hold a key") }, { value: "toggle", label: qsTr("Tap on/off") }, { value: "vad", label: qsTr("Hands-free") }]
                onActivated: (v) => App.prefs["stt/mode"] = v
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            PillButton {
                enabled: App.sttReady || App.listening
                kind: App.listening ? "primary" : "secondary"
                iconName: App.listening ? "square" : "mic"
                text: App.listening ? qsTr("Done") : qsTr("Try: say something")
                onClicked: App.listening ? App.stopListening() : App.startListening()
            }
            LevelBar { Layout.fillWidth: true; level: App.listening ? App.micLevel : 0 }
        }
        Txt { visible: step.heard !== ""; text: "“" + step.heard + "”"; role: "lead"; Layout.fillWidth: true; font.family: Theme.stageFont }
    }
    Txt { Layout.fillWidth: true; role: "caption"; text: qsTr("Not for you? Just continue; typing works without it.") }
}
