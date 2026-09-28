import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Dictation: talk (or mouth, or whisper) and Vocal Ink writes it down.
ScrollPage {
    id: page
    title: qsTr("Speech input")
    subtitle: qsTr("Dictate instead of typing. Speech is turned into text on this computer unless you choose a cloud service.")

    readonly property string engine: App.prefs["stt/engine"] || "whisper"
    property string heard: ""

    Connections {
        target: App
        function onTranscriptReady(text) { page.heard = text }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Try it")
        iconName: "mic-vocal"
        headerExtra: StatusChip {
            tone: App.sttReady ? "ok" : "warn"
            text: App.sttReady ? qsTr("Ready") : qsTr("Needs setup")
            tip: App.sttStatus
        }
        Txt { Layout.fillWidth: true; text: App.sttStatus; color: Theme.muted }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            PillButton {
                kind: App.listening ? "primary" : "secondary"
                iconName: App.listening ? "square" : "mic"
                text: App.listening ? qsTr("Stop and write it down") : qsTr("Start listening")
                enabled: App.sttReady || App.listening
                onClicked: App.listening ? App.stopListening() : App.startListening()
            }
            LevelBar { Layout.fillWidth: true; level: App.listening ? App.micLevel : 0 }
        }
        Rectangle {
            visible: page.heard !== "" || App.transcribing
            Layout.fillWidth: true
            implicitHeight: heardText.implicitHeight + Theme.s4 * 2
            radius: Theme.radius
            color: Theme.sunken
            Txt {
                id: heardText
                x: Theme.s4; y: Theme.s4
                width: parent.width - Theme.s4 * 2
                text: App.transcribing ? qsTr("Writing it down…") : "“" + page.heard + "”"
                role: "lead"
                font.family: Theme.stageFont
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("How it listens")
        iconName: "ear"
        SettingRow {
            title: qsTr("Recognition")
            Segmented {
                label: qsTr("Recognition engine")
                value: page.engine
                options: [{ value: "whisper", label: qsTr("On this computer"), icon: "lock" }, { value: "openai", label: qsTr("Cloud (OpenAI-compatible)"), icon: "globe" }]
                onActivated: (v) => App.prefs["stt/engine"] = v
            }
        }
        SettingRow {
            title: qsTr("Mode")
            description: (App.prefs["stt/mode"] || "ptt") === "ptt" ? qsTr("Hold the dictation key (or the mic button) while you talk.")
                       : App.prefs["stt/mode"] === "toggle" ? qsTr("Press once to start and again to stop.")
                       : qsTr("Listens all the time and writes down whenever you speak.")
            Segmented {
                label: qsTr("Dictation mode")
                value: App.prefs["stt/mode"] || "ptt"
                //: Dictation modes: hold a key while speaking / press once to start and again to stop / listens on its own
                options: [{ value: "ptt", label: qsTr("Hold to dictate") }, { value: "toggle", label: qsTr("Tap on/off") }, { value: "vad", label: qsTr("Hands-free") }]
                onActivated: (v) => App.prefs["stt/mode"] = v
            }
        }
        SettingRow {
            visible: App.prefs["stt/mode"] === "vad"
            title: qsTr("Sensitivity")
            description: qsTr("Higher picks up quieter speech, and more background noise.")
            ValueSlider {
                label: qsTr("Sensitivity")
                from: 0; to: 100; suffix: "%"
                value: App.prefs["stt/vadSensitivity"]
                onMoved: App.prefs["stt/vadSensitivity"] = value
            }
        }
        SettingRow {
            title: qsTr("After dictating")
            description: qsTr("Reviewing first lets you fix a misheard word before anyone hears it.")
            Segmented {
                label: qsTr("After dictating")
                value: App.prefs["stt/autoSpeak"] === true ? "speak" : "review"
                options: [{ value: "review", label: qsTr("Let me review") }, { value: "speak", label: qsTr("Speak right away") }]
                onActivated: (v) => App.prefs["stt/autoSpeak"] = (v === "speak")
            }
        }
        SettingRow {
            title: qsTr("Language")
            Choice {
                label: qsTr("Spoken language")
                readonly property var codes: ["auto", "en", "es", "fr", "de", "it", "pt", "nl", "pl", "ru", "uk", "tr", "ar", "hi", "ja", "ko", "zh", "sv", "da", "no", "fi", "cs", "el", "he", "id", "vi", "th"]
                readonly property var names: [qsTr("Detect automatically"), "English", "Español", "Français", "Deutsch", "Italiano", "Português", "Nederlands", "Polski", "Русский", "Українська", "Türkçe", "العربية", "हिन्दी", "日本語", "한국어", "中文", "Svenska", "Dansk", "Norsk", "Suomi", "Čeština", "Ελληνικά", "עברית", "Bahasa Indonesia", "Tiếng Việt", "ไทย"]
                model: names
                currentIndex: Math.max(0, codes.indexOf(App.prefs["stt/language"] || "auto"))
                onActivated: App.prefs["stt/language"] = codes[currentIndex]
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Txt { text: qsTr("Words it should know"); role: "label" }
            Txt { text: qsTr("Names, game terms, slang. Separate with commas."); role: "caption"; Layout.fillWidth: true }
            Field {
                Layout.fillWidth: true
                label: qsTr("Custom vocabulary")
                placeholderText: qsTr("e.g. Vocal Ink, Valorant, Anya, GG")
                text: App.prefs["stt/prompt"] || ""
                onEditingFinished: App.prefs["stt/prompt"] = text
            }
        }
    }

    Card {
        visible: page.engine === "whisper"
        Layout.fillWidth: true
        title: qsTr("Speech models")
        subtitle: qsTr("Bigger models understand more but need a faster computer. “Base” suits most people.")
        iconName: "download"
        DownloadList {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(420 * Theme.scale)
            model: App.whisperModels
            kind: "whisper"
        }
    }

    Card {
        visible: page.engine === "openai"
        Layout.fillWidth: true
        title: qsTr("Cloud recognition")
        subtitle: qsTr("Works with OpenAI, Groq, or a local server such as Speaches. Audio is sent only while you dictate.")
        iconName: "globe"
        SettingRow {
            title: qsTr("API key")
            Field {
                secret: true
                label: qsTr("Speech recognition API key")
                placeholderText: App.secretsRevision >= 0 && App.hasSecret("openai-stt") ? qsTr("Saved (%1)").arg(App.secretHint("openai-stt")) : qsTr("Paste your key")
                onEditingFinished: if (text !== "") { App.setSecret("openai-stt", text); clear() }
            }
        }
        SettingRow {
            title: qsTr("Server address")
            Field { label: qsTr("Server address"); text: App.prefs["stt/openai/baseUrl"] || ""; onEditingFinished: App.prefs["stt/openai/baseUrl"] = text.trim() }
        }
        SettingRow {
            title: qsTr("Model")
            Field { label: qsTr("Recognition model"); text: App.prefs["stt/openai/model"] || ""; onEditingFinished: App.prefs["stt/openai/model"] = text.trim() }
        }
    }
}
