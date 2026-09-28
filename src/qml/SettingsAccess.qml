import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

ScrollPage {
    title: qsTr("Accessibility")
    subtitle: qsTr("Vocal Ink works with a keyboard only, with screen readers, with switch access and with shaky hands. Tune it here.")

    Card {
        Layout.fillWidth: true
        title: qsTr("Seeing")
        iconName: "eye"
        SettingRow {
            title: qsTr("Bold focus ring")
            description: qsTr("A thick, bright outline around whatever the keyboard is on.")
            Toggle { tip: qsTr("Bold focus ring"); checked: App.prefs["a11y/focusRing"] === "bold"; onToggled: App.prefs["a11y/focusRing"] = checked ? "bold" : "normal" }
        }
        SettingRow {
            title: qsTr("Letter spacing")
            ValueSlider {
                label: qsTr("Letter spacing")
                from: 0; to: 20; suffix: "%"
                value: App.prefs["a11y/letterSpacing"]
                onMoved: App.prefs["a11y/letterSpacing"] = value
            }
        }
        SettingRow {
            title: qsTr("Line spacing")
            ValueSlider {
                label: qsTr("Line spacing")
                from: 100; to: 200; stepSize: 10; suffix: "%"
                value: App.prefs["a11y/lineSpacing"]
                onMoved: App.prefs["a11y/lineSpacing"] = value
            }
        }
        SettingRow {
            title: qsTr("High contrast theme")
            description: qsTr("Pure black and white with a yellow focus ring.")
            Toggle {
                tip: qsTr("High contrast theme")
                checked: App.prefs["ui/theme"] === "contrast"
                onToggled: App.prefs["ui/theme"] = checked ? "contrast" : "midnight"
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Moving & pressing")
        iconName: "hand"
        SettingRow {
            title: qsTr("Bigger buttons")
            description: qsTr("Every control grows to at least 52 pixels.")
            Toggle { tip: qsTr("Bigger buttons"); checked: App.prefs["a11y/largeTargets"] === true; onToggled: App.prefs["a11y/largeTargets"] = checked }
        }
        SettingRow {
            title: qsTr("Tap to talk instead of holding")
            description: qsTr("Press the dictation key once to start and again to stop.")
            Toggle { tip: qsTr("Tap to talk instead of holding"); checked: App.prefs["a11y/latchPtt"] === true; onToggled: App.prefs["a11y/latchPtt"] = checked }
        }
        SettingRow {
            title: qsTr("Ignore repeated key presses")
            description: qsTr("Presses of the same shortcut closer together than this are ignored. Helps with tremor.")
            ValueSlider {
                label: qsTr("Ignore repeated presses within")
                from: 0; to: 1500; stepSize: 50
                format: (v) => v === 0 ? qsTr("Off") : qsTr("%1 ms", "milliseconds").arg(Number(Math.round(v)).toLocaleString(Qt.locale(), "f", 0))
                value: App.prefs["a11y/debounceMs"]
                onMoved: App.prefs["a11y/debounceMs"] = value
            }
        }
        SettingRow {
            title: qsTr("Ask before speaking")
            description: qsTr("Shows what will be said and waits for a second press.")
            Toggle { tip: qsTr("Ask before speaking"); checked: App.prefs["a11y/confirmSpeak"] === true; onToggled: App.prefs["a11y/confirmSpeak"] = checked }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Switch access")
        subtitle: qsTr("For one or two switches: a highlight steps through your phrases and any key picks the highlighted one.")
        iconName: "scan-line"
        SettingRow {
            title: qsTr("Step through phrases on the Board")
            Toggle { tip: qsTr("Switch scanning"); checked: App.prefs["a11y/scanning"] === true; onToggled: App.prefs["a11y/scanning"] = checked }
        }
        SettingRow {
            visible: App.prefs["a11y/scanning"] === true
            title: qsTr("Time on each phrase")
            ValueSlider {
                label: qsTr("Scanning speed")
                from: 400; to: 4000; stepSize: 100
                format: (v) => qsTr("%1 s", "seconds").arg(Number(v / 1000).toLocaleString(Qt.locale(), "f", 1))
                value: App.prefs["a11y/scanIntervalMs"]
                onMoved: App.prefs["a11y/scanIntervalMs"] = value
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Hearing & feedback")
        iconName: "ear"
        SettingRow {
            title: qsTr("Sound cues")
            description: qsTr("Soft tones when listening starts and stops, a message is sent, or something fails. Only you hear them.")
            Toggle { tip: qsTr("Sound cues"); checked: App.prefs["a11y/soundCues"] !== false; onToggled: App.prefs["a11y/soundCues"] = checked }
        }
        SettingRow {
            visible: App.prefs["a11y/soundCues"] !== false
            title: qsTr("Cue volume")
            ValueSlider {
                label: qsTr("Cue volume")
                from: 0; to: 100; suffix: "%"
                value: App.prefs["a11y/soundCueVolume"]
                onMoved: App.prefs["a11y/soundCueVolume"] = value
            }
        }
        SettingRow {
            title: qsTr("Read back what I type")
            description: qsTr("Hear each word or sentence as you finish it, on your own speakers only.")
            Segmented {
                label: qsTr("Read back what I type")
                value: App.prefs["a11y/echoTyping"] || "off"
                options: [{ value: "off", label: qsTr("Off") }, { value: "words", label: qsTr("Words") }, { value: "sentences", label: qsTr("Sentences") }]
                onActivated: (v) => App.prefs["a11y/echoTyping"] = v
            }
        }
        SettingRow {
            title: qsTr("Screen reader announcements")
            description: qsTr("Notices and status changes are announced to NVDA, JAWS, Narrator, VoiceOver and Orca.")
            Toggle { tip: qsTr("Screen reader announcements"); checked: App.prefs["a11y/announce"] !== false; onToggled: App.prefs["a11y/announce"] = checked }
        }
    }
}
