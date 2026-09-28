import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    title: qsTr("Captions for your stream")
    lead: qsTr("Show what you say on screen, in the same ink. Add the overlay to OBS as a browser source; no plugin needed.")

    Card {
        Layout.fillWidth: true
        SettingRow {
            title: qsTr("Caption overlay")
            Toggle { tip: qsTr("Caption overlay"); checked: App.prefs["overlay/enabled"] === true; onToggled: App.prefs["overlay/enabled"] = checked }
        }
        RowLayout {
            visible: App.prefs["overlay/enabled"] === true
            Layout.fillWidth: true
            spacing: Theme.s2
            Field { Layout.fillWidth: true; readOnly: true; label: qsTr("Overlay address"); text: App.overlayUrl; font.family: Theme.monoFont }
            PillButton { text: qsTr("Copy"); iconName: "copy"; onClicked: { App.copy(App.overlayUrl); App.notifyUser(qsTr("Copied. In OBS: Sources → + → Browser → paste."), 0) } }
        }
        SettingRow {
            title: qsTr("Connect to OBS too")
            description: qsTr("For subtitles in a text source, closed captions, and a “talking” avatar. Needs OBS 28 or newer.")
            Toggle { tip: qsTr("Connect to OBS"); checked: App.prefs["obs/enabled"] === true; onToggled: App.prefs["obs/enabled"] = checked }
        }
        SettingRow {
            title: qsTr("Read my Twitch chat aloud")
            Toggle { tip: qsTr("Read Twitch chat"); checked: App.prefs["twitch/enabled"] === true; onToggled: App.prefs["twitch/enabled"] = checked }
        }
        Field {
            visible: App.prefs["twitch/enabled"] === true
            Layout.fillWidth: true
            label: qsTr("Twitch channel")
            placeholderText: qsTr("Your channel name")
            text: App.prefs["twitch/channel"] || ""
            onEditingFinished: App.prefs["twitch/channel"] = text.trim()
        }
    }
    Txt { Layout.fillWidth: true; role: "caption"; text: qsTr("Styles, positions and OBS details are in the Stream page.") }
}
