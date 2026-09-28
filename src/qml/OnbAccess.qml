import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    title: qsTr("Make it comfortable")
    lead: qsTr("Everything changes as you touch it, so you can see what suits you.")

    Card {
        Layout.fillWidth: true
        SettingRow {
            title: qsTr("Text size")
            ValueSlider {
                label: qsTr("Text size")
                from: 80; to: 200; stepSize: 10; suffix: "%"
                value: App.prefs["ui/fontScale"]
                onPressedChanged: if (!pressed) App.prefs["ui/fontScale"] = value
                Keys.onReleased: App.prefs["ui/fontScale"] = value
            }
        }
        SettingRow {
            title: qsTr("Reading font")
            Segmented {
                label: qsTr("Font")
                value: App.prefs["ui/font"] || "atkinson"
                options: [{ value: "atkinson", label: "Atkinson" }, { value: "lexend", label: "Lexend" }, { value: "opendyslexic", label: "OpenDyslexic" }]
                onActivated: (v) => App.prefs["ui/font"] = v
            }
        }
        SettingRow {
            title: qsTr("Bigger buttons")
            Toggle { tip: qsTr("Bigger buttons"); checked: App.prefs["a11y/largeTargets"] === true; onToggled: App.prefs["a11y/largeTargets"] = checked }
        }
        SettingRow {
            title: qsTr("Bold focus ring")
            Toggle { tip: qsTr("Bold focus ring"); checked: App.prefs["a11y/focusRing"] === "bold"; onToggled: App.prefs["a11y/focusRing"] = checked ? "bold" : "normal" }
        }
        SettingRow {
            title: qsTr("Less motion")
            Toggle { tip: qsTr("Less motion"); checked: (App.prefs["ui/motion"] || "full") !== "full"; onToggled: App.prefs["ui/motion"] = checked ? "reduced" : "full" }
        }
        SettingRow {
            title: qsTr("Tap to dictate instead of holding")
            Toggle { tip: qsTr("Tap to dictate"); checked: App.prefs["a11y/latchPtt"] === true; onToggled: App.prefs["a11y/latchPtt"] = checked }
        }
        SettingRow {
            title: qsTr("Switch access scanning")
            description: qsTr("A highlight steps through your phrases; any key picks one.")
            Toggle { tip: qsTr("Switch access scanning"); checked: App.prefs["a11y/scanning"] === true; onToggled: App.prefs["a11y/scanning"] = checked }
        }
    }
    Txt { Layout.fillWidth: true; role: "caption"; text: qsTr("More in Settings → Accessibility: letter and line spacing, typing echo, repeat-press protection and more.") }
}
