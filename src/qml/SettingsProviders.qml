import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Cloud voice providers: keys and per-provider options. Local ones need nothing.
ScrollPage {
    id: page
    title: qsTr("Voice providers")
    subtitle: qsTr("Local voices are free and private. Cloud voices sound richer and use your own account, so you pay the provider directly.")

    function provider(id) {
        const list = App.voices.providers
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i]
        return null
    }

    component ProviderCard: Card {
        id: pc
        property string providerId
        readonly property var info: page.provider(providerId)
        Layout.fillWidth: true
        title: info ? info.name : providerId
        iconName: info && info.local ? "lock" : "globe"
        headerExtra: StatusChip {
            tone: pc.info && pc.info.available ? "ok" : "warn"
            text: pc.info && pc.info.available ? qsTr("%n voice(s)", "", pc.info.count) : qsTr("Not set up")
            tip: pc.info && !pc.info.available ? pc.info.reason : ""
        }
    }

    ProviderCard {
        providerId: "piper"
        subtitle: qsTr("Natural neural voices that run offline. Download voices in Voices → Download.")
        SettingRow {
            title: qsTr("Custom Piper program")
            description: qsTr("Leave empty to use the one Vocal Ink downloads.")
            Field { label: qsTr("Piper program path"); text: App.prefs["tts/piper/executable"] || ""; placeholderText: qsTr("Automatic"); onEditingFinished: App.prefs["tts/piper/executable"] = text.trim() }
        }
    }
    ProviderCard {
        providerId: "azure"
        subtitle: qsTr("Hundreds of voices in 140+ languages, with speaking styles. Free tier available.")
        ProviderKeyForm { providerId: "azure"; Layout.fillWidth: true }
    }
    ProviderCard {
        providerId: "elevenlabs"
        subtitle: qsTr("Very expressive voices, and your own cloned voice. Add voices by ID from the Voice Library.")
        ProviderKeyForm { providerId: "elevenlabs"; Layout.fillWidth: true }
        SettingRow {
            title: qsTr("Model")
            description: qsTr("Flash is fastest; Multilingual sounds best.")
            Choice {
                label: qsTr("ElevenLabs model")
                readonly property var ids: ["eleven_flash_v2_5", "eleven_turbo_v2_5", "eleven_multilingual_v2", "eleven_v3"]
                model: [qsTr("Flash v2.5 (fastest)"), qsTr("Turbo v2.5"), qsTr("Multilingual v2 (best)"), qsTr("Eleven v3 (most expressive)")]
                currentIndex: Math.max(0, ids.indexOf(App.prefs["tts/elevenlabs/model"]))
                onActivated: App.prefs["tts/elevenlabs/model"] = ids[currentIndex]
            }
        }
        CustomVoiceForm { providerId: "elevenlabs"; Layout.fillWidth: true }
    }
    ProviderCard {
        providerId: "fishaudio"
        subtitle: qsTr("A huge community library of voices. Search it from Voices, or add a voice by its model ID.")
        ProviderKeyForm { providerId: "fishaudio"; Layout.fillWidth: true }
        SettingRow {
            title: qsTr("Model")
            Field { label: qsTr("Fish Audio model"); text: App.prefs["tts/fish/model"] || "s1"; onEditingFinished: App.prefs["tts/fish/model"] = text.trim() }
        }
        CustomVoiceForm { providerId: "fishaudio"; Layout.fillWidth: true }
    }
    ProviderCard {
        providerId: "openai"
        subtitle: qsTr("Natural voices you can direct (“speak warmly, a bit fast”). Works with compatible servers too.")
        ProviderKeyForm { providerId: "openai"; Layout.fillWidth: true }
        SettingRow {
            title: qsTr("Model")
            Field { label: qsTr("OpenAI voice model"); text: App.prefs["tts/openai/model"] || ""; onEditingFinished: App.prefs["tts/openai/model"] = text.trim() }
        }
        SettingRow {
            title: qsTr("Server address")
            Field { label: qsTr("OpenAI server address"); text: App.prefs["tts/openai/baseUrl"] || ""; onEditingFinished: App.prefs["tts/openai/baseUrl"] = text.trim() }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Txt { text: qsTr("How to speak"); role: "label" }
            Field {
                Layout.fillWidth: true
                label: qsTr("Delivery instructions")
                placeholderText: qsTr("e.g. Friendly and relaxed, a little quick.")
                text: App.prefs["tts/openai/instructions"] || ""
                onEditingFinished: App.prefs["tts/openai/instructions"] = text
            }
        }
    }
}
