import QtQuick
import QtQuick.Layouts
import Ink.Core

// Enter an API key (and region, for Azure) for a cloud voice provider.
ColumnLayout {
    id: form
    property string providerId

    readonly property var info: ({
        "azure": { secret: "azure", url: "https://portal.azure.com/#create/Microsoft.CognitiveServicesSpeechServices", label: qsTr("Azure Speech key") },
        "elevenlabs": { secret: "elevenlabs", url: "https://elevenlabs.io/app/settings/api-keys", label: qsTr("ElevenLabs API key") },
        "fishaudio": { secret: "fishaudio", url: "https://fish.audio/app/api-keys/", label: qsTr("Fish Audio API key") },
        "openai": { secret: "openai", url: "https://platform.openai.com/api-keys", label: qsTr("OpenAI API key") }
    })
    readonly property var entry: info[providerId] || null
    readonly property bool hasKey: entry ? (App.secretsRevision >= 0 && App.hasSecret(entry.secret)) : false

    spacing: Theme.s2
    visible: entry !== null

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        Field {
            id: key
            Layout.fillWidth: true
            secret: true
            iconName: "lock"
            label: form.entry ? form.entry.label : ""
            placeholderText: form.hasKey ? qsTr("Saved (%1). Paste a new key to replace it.").arg(App.secretHint(form.entry.secret)) : (form.entry ? form.entry.label : "")
            onAccepted: save.clicked()
        }
        PillButton {
            id: save
            text: qsTr("Save key")
            kind: "primary"
            enabled: key.text.trim().length > 0
            onClicked: {
                App.setSecret(form.entry.secret, key.text)
                key.clear()
                App.notifyUser(qsTr("Key saved. Loading voices…"), 0)
                App.refreshVoices()
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: form.providerId === "azure"
        spacing: Theme.s2
        Txt { text: qsTr("Region"); role: "label" }
        Field {
            Layout.preferredWidth: Math.round(200 * Theme.scale)
            label: qsTr("Azure region")
            placeholderText: "westeurope"
            text: App.prefs["tts/azure/region"] || ""
            onEditingFinished: App.prefs["tts/azure/region"] = text.trim()
        }
    }
    RowLayout {
        spacing: Theme.s3
        PillButton {
            small: true
            kind: "ghost"
            iconName: "external-link"
            text: qsTr("Get a key")
            onClicked: App.openUrl(form.entry.url)
        }
        PillButton {
            small: true
            kind: "ghost"
            iconName: "trash-2"
            text: qsTr("Remove saved key")
            visible: form.hasKey
            onClicked: App.setSecret(form.entry.secret, "")
        }
        Txt {
            text: qsTr("Keys are kept in your system's password manager.")
            role: "caption"
            Layout.fillWidth: true
        }
    }
}
