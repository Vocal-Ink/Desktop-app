import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    id: step
    title: qsTr("Choose your voice")
    lead: qsTr("Listen to a few and pick the one that feels like you. Previews play only on your speakers.")

    readonly property var dl: App.piperVoices
    readonly property bool downloading: dl.runtimeDownloading || busy
    property bool busy: false

    // A natural offline voice, one click.
    Card {
        Layout.fillWidth: true
        title: qsTr("Get a natural voice (free, offline)")
        subtitle: qsTr("Piper voices sound human and run on this computer. About 90 MB for the engine and a first voice.")
        iconName: "sparkles"
        RowLayout {
            spacing: Theme.s3
            PillButton {
                kind: "primary"
                iconName: "download"
                text: step.downloading ? qsTr("Downloading…") : dl.runtimeInstalled ? qsTr("Download the recommended voice") : qsTr("Download Piper and a voice")
                enabled: !step.downloading
                onClicked: {
                    step.busy = true
                    if (!dl.runtimeInstalled) dl.downloadRuntime()
                    dl.downloadRecommended()
                }
            }
            LevelBar { visible: dl.runtimeDownloading; Layout.fillWidth: true; level: dl.runtimeProgress }
            Txt { visible: dl.status !== ""; text: dl.status; role: "caption"; Layout.fillWidth: true }
        }
    }

    // What's already here.
    Card {
        Layout.fillWidth: true
        title: qsTr("Voices on this computer")
        iconName: "audio-lines"
        pad: Theme.s4
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(300 * Theme.scale)
            clip: true
            spacing: 2
            model: App.voices
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: InkScrollBar {}
            delegate: VoiceRow { width: ListView.view.width - 10 }
            Component.onCompleted: { App.voices.provider = ""; App.voices.search = "" }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        Txt { text: qsTr("Speed"); role: "label" }
        ValueSlider {
            Layout.fillWidth: true
            label: qsTr("Speed")
            from: 50; to: 200; stepSize: 5; suffix: "%"
            value: App.prefs["tts/rate"]
            onMoved: App.prefs["tts/rate"] = value
        }
        PillButton { text: qsTr("Hear it"); iconName: "play"; onClicked: App.previewVoice(App.voiceKey, qsTr("Hi! This is how I'll sound from now on.")) }
    }
    Txt {
        Layout.fillWidth: true
        role: "caption"
        text: qsTr("Want Azure, ElevenLabs, Fish Audio or OpenAI voices? Add your key later in Settings → Voice providers.")
    }
}
