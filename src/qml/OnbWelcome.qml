import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Slide 1: the app introduces itself by writing in ink, then everything that
// makes the rest of the setup comfortable to use: language, text size, a
// readable font, contrast, motion, bigger buttons, focus ring, and how the
// keyboard and screen readers work here. Every change applies immediately.
Item {
    id: step
    property bool canContinue: true
    property string continueLabel: ""
    property Item firstFocus: language
    function commit() {}

    readonly property bool wide: width >= Math.round(900 * Math.min(1.3, Theme.scale))
    readonly property real gutter: Math.round(64 * Math.min(1.3, Theme.scale))
    implicitHeight: wide ? Math.max(hello.implicitHeight, comfort.implicitHeight)
                         : hello.implicitHeight + Theme.s6 + comfort.implicitHeight

    // Remembered so turning high contrast off returns to the theme you had.
    property string themeBeforeContrast: ""

    // --- Left: hello ------------------------------------------------------------
    ColumnLayout {
        id: hello
        width: step.wide ? Math.round(step.width * 0.44) : step.width
        spacing: Theme.s5

        InkLine {
            id: line
            Layout.fillWidth: true
            text: qsTr("Hello. From now on, I'll say what you write.")
            fontFamily: Theme.displayFont
            fontWeight: Font.ExtraBold
            fontSize: Math.round(Theme.fsHero * (step.wide ? 1.2 : 1.0))
            progress: 0
            NumberAnimation on progress { from: 0; to: 1; duration: Theme.motionOn ? 2600 : 0; easing.type: Easing.InOutSine }
        }
        InkWave {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(48 * Theme.scale)
            running: line.progress < 1 && Theme.motionOn
            level: running ? 0.35 + 0.35 * Math.abs(Math.sin(line.progress * 19)) : 0
            color: Theme.accent
            sheen: Theme.sheen
            thickness: height * 0.42
            animated: !Theme.reducedMotion
        }
        Txt {
            Layout.fillWidth: true
            Layout.maximumWidth: Math.round(560 * Theme.scale)
            role: "lead"
            color: Theme.muted
            text: qsTr("Type or dictate, and Vocal Ink speaks for you: in calls, in games, on stream, or across the table. Setup takes about two minutes, and you can change everything later.")
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            spacing: Theme.s2
            Txt { text: qsTr("Language"); role: "label" }
            LanguagePicker {
                id: language
                Layout.fillWidth: true
                Layout.maximumWidth: Math.round(420 * Theme.scale)
            }
            Txt {
                Layout.fillWidth: true
                visible: App.language && App.language.current !== "en"
                role: "caption"
                text: qsTr("Translated with the help of AI; not yet checked by a native speaker.")
            }
        }

        // How to get around, for keyboards and screen readers.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            implicitHeight: keys.implicitHeight + Theme.s4 * 2
            radius: Theme.radiusLg
            color: App.assistiveTech ? Theme.accentWash : "transparent"
            border.color: App.assistiveTech ? Theme.accent : Theme.line
            border.width: Theme.hairline
            Accessible.role: Accessible.Note
            Accessible.name: keysTitle.text

            ColumnLayout {
                id: keys
                x: Theme.s4
                y: Theme.s4
                width: parent.width - Theme.s4 * 2
                spacing: Theme.s2
                RowLayout {
                    spacing: Theme.s2
                    Icon { name: App.assistiveTech ? "accessibility" : "keyboard"; color: Theme.accentText; size: Math.round(18 * Theme.scale) }
                    Txt {
                        id: keysTitle
                        Layout.fillWidth: true
                        role: "label"
                        text: App.assistiveTech ? qsTr("A screen reader is running. Vocal Ink announces each step and everything it says.")
                                                : qsTr("Everything works from the keyboard.")
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.s4
                    Repeater {
                        model: [
                            { keys: "Tab", what: qsTr("next control") },
                            { keys: "Ctrl+Return", what: qsTr("continue") },
                            { keys: "Alt+Left", what: qsTr("back") }
                        ]
                        Row {
                            required property var modelData
                            spacing: Theme.s2
                            KeyCombo { sequence: modelData.keys; anchors.verticalCenter: parent.verticalCenter }
                            Txt { text: modelData.what; role: "caption"; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                }
            }
        }
    }

    // --- Right: comfort -----------------------------------------------------------
    Card {
        id: comfort
        x: step.wide ? hello.width + step.gutter : 0
        y: step.wide ? 0 : hello.implicitHeight + Theme.s6
        width: step.wide ? step.width - hello.width - step.gutter : step.width
        title: qsTr("Make it comfortable")
        subtitle: qsTr("Changes apply as you make them.")
        iconName: "accessibility"

        SettingRow {
            title: qsTr("Text size")
            ValueSlider {
                id: size
                label: qsTr("Text size")
                from: 80; to: 200; stepSize: 10; suffix: "%"
                value: App.prefs["ui/fontScale"] || 100
                // Applied on release: resizing the whole window while dragging is hard to aim.
                onPressedChanged: if (!pressed) App.prefs["ui/fontScale"] = value
                Keys.onReleased: App.prefs["ui/fontScale"] = value
            }
        }
        SettingRow {
            title: qsTr("Reading font")
            description: qsTr("Made for low vision, or for dyslexia.")
            Segmented {
                label: qsTr("Reading font")
                value: App.prefs["ui/font"] || "atkinson"
                options: [{ value: "atkinson", label: "Atkinson" }, { value: "lexend", label: "Lexend" }, { value: "opendyslexic", label: "OpenDyslexic" }]
                onActivated: (v) => App.prefs["ui/font"] = v
            }
        }
        SettingRow {
            title: qsTr("High contrast")
            Toggle {
                tip: qsTr("High contrast")
                checked: Theme.highContrast
                onToggled: {
                    if (checked) {
                        step.themeBeforeContrast = App.prefs["ui/theme"] || "midnight"
                        App.prefs["ui/theme"] = "contrast"
                    } else {
                        App.prefs["ui/theme"] = step.themeBeforeContrast !== "" && step.themeBeforeContrast !== "contrast"
                            ? step.themeBeforeContrast : "midnight"
                    }
                }
            }
        }
        SettingRow {
            title: qsTr("Less motion")
            description: qsTr("Slides and ink appear without moving.")
            Toggle { tip: qsTr("Less motion"); checked: (App.prefs["ui/motion"] || "full") !== "full"; onToggled: App.prefs["ui/motion"] = checked ? "reduced" : "full" }
        }
        SettingRow {
            title: qsTr("Bigger buttons")
            Toggle { tip: qsTr("Bigger buttons"); checked: App.prefs["a11y/largeTargets"] === true; onToggled: App.prefs["a11y/largeTargets"] = checked }
        }
        SettingRow {
            title: qsTr("Bold focus ring")
            description: qsTr("A thick outline shows where the keyboard is.")
            Toggle { tip: qsTr("Bold focus ring"); checked: App.prefs["a11y/focusRing"] === "bold"; onToggled: App.prefs["a11y/focusRing"] = checked ? "bold" : "normal" }
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
        Txt {
            Layout.fillWidth: true
            role: "caption"
            text: qsTr("More in Settings → Accessibility: letter and line spacing, typing echo, repeat-press protection and more.")
        }
    }
}
