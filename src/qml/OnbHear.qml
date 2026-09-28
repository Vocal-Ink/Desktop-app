import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    title: qsTr("Do you want to hear it too?")
    lead: qsTr("Hearing your own voice helps you know what others heard. Use headphones if you're in a call, so it doesn't echo.")

    Card {
        Layout.fillWidth: true
        SettingRow {
            title: qsTr("Play my voice on my headphones too")
            Toggle { tip: qsTr("Hear my voice too"); checked: App.prefs["audio/monitorEnabled"] === true; onToggled: App.prefs["audio/monitorEnabled"] = checked }
        }
        SettingRow {
            visible: App.prefs["audio/monitorEnabled"] === true
            title: qsTr("Headphones")
            DeviceChoice { kind: "monitor"; model: App.outputs }
        }
        SettingRow {
            visible: App.prefs["audio/monitorEnabled"] === true
            title: qsTr("Volume")
            ValueSlider { label: qsTr("Headphone volume"); from: 0; to: 100; suffix: "%"; value: App.prefs["audio/monitorVolume"]; onMoved: App.prefs["audio/monitorVolume"] = value }
        }
        SettingRow {
            title: qsTr("Soft sound cues")
            description: qsTr("Tones when listening starts and stops or a message is sent. Only you hear them.")
            Toggle { tip: qsTr("Sound cues"); checked: App.prefs["a11y/soundCues"] !== false; onToggled: App.prefs["a11y/soundCues"] = checked }
        }
        PillButton { iconName: "play"; text: qsTr("Test"); onClicked: App.testOutput() }
    }
}
