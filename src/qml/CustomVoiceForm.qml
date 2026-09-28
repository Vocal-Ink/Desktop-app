import QtQuick
import QtQuick.Layouts
import Ink.Core

// Add a provider voice by its ID (e.g. from the ElevenLabs or Fish Audio library).
ColumnLayout {
    id: form
    property string providerId
    spacing: Theme.s1

    Txt { text: qsTr("Add a voice by ID"); role: "label" }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        Field { id: vid; Layout.fillWidth: true; label: qsTr("Voice ID"); placeholderText: qsTr("Voice ID"); font.family: Theme.monoFont }
        Field { id: vname; Layout.preferredWidth: Math.round(180 * Theme.scale); label: qsTr("Name"); placeholderText: qsTr("Name it") }
        PillButton {
            small: true
            iconName: "plus"
            text: qsTr("Add")
            enabled: vid.text.trim() !== ""
            onClicked: {
                App.addCustomVoice(form.providerId, vid.text, vname.text || vid.text)
                App.notifyUser(qsTr("Added. Find it in Voices."), 0)
                vid.clear(); vname.clear()
            }
        }
    }
}
