import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

ScrollPage {
    title: qsTr("Appearance")
    subtitle: qsTr("Make Vocal Ink look and feel the way you like. Changes apply as you make them.")

    Card {
        Layout.fillWidth: true
        title: qsTr("Theme")
        iconName: "palette"
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s3
            Repeater {
                model: [
                    { id: "midnight", label: qsTr("Midnight ink") },
                    { id: "vellum", label: qsTr("Vellum (light)") },
                    { id: "amethyst", label: qsTr("Amethyst (true black)") },
                    { id: "contrast", label: qsTr("High contrast") },
                    { id: "system", label: qsTr("Match my system") }
                ]
                ThemeCard {
                    required property var modelData
                    themeId: modelData.id
                    label: modelData.label
                    selected: (App.prefs["ui/theme"] || "midnight") === modelData.id
                    onClicked: App.prefs["ui/theme"] = modelData.id
                }
            }
        }
        SettingRow {
            title: qsTr("Ink colour")
            description: Theme.highContrast ? qsTr("High contrast uses its own colours.") : qsTr("Used for the words being spoken, buttons and highlights.")
            Row {
                spacing: Theme.s1 + 2
                Repeater {
                    model: Theme.accents
                    Swatch {
                        required property var modelData
                        swatch: modelData.color
                        name: modelData.name
                        enabled: !Theme.highContrast
                        selected: (App.prefs["ui/accent"] || "#8c52ff").toLowerCase() === modelData.color.toLowerCase()
                        onClicked: App.prefs["ui/accent"] = modelData.color
                    }
                }
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Text")
        iconName: "type"
        SettingRow {
            title: qsTr("Font")
            description: qsTr("Atkinson Hyperlegible was designed for low vision. OpenDyslexic and Lexend can make reading easier with dyslexia.")
            stacked: true
            Segmented {
                label: qsTr("Font")
                value: App.prefs["ui/font"] || "atkinson"
                options: [
                    { value: "atkinson", label: "Atkinson Hyperlegible" },
                    { value: "lexend", label: "Lexend" },
                    { value: "opendyslexic", label: "OpenDyslexic" },
                    { value: "system", label: qsTr("System") }
                ]
                onActivated: (v) => App.prefs["ui/font"] = v
            }
        }
        SettingRow {
            title: qsTr("Text size")
            ValueSlider {
                label: qsTr("Text size")
                from: 80; to: 250; stepSize: 5; suffix: "%"
                value: App.prefs["ui/fontScale"]
                // Apply on release: resizing everything while dragging is jumpy.
                onPressedChanged: if (!pressed) App.prefs["ui/fontScale"] = value
                Keys.onReleased: App.prefs["ui/fontScale"] = value
            }
        }
        SettingRow {
            title: qsTr("Size of the line being spoken")
            ValueSlider {
                label: qsTr("Spoken line size")
                from: 60; to: 200; stepSize: 5; suffix: "%"
                value: App.prefs["ui/stageScale"]
                onMoved: App.prefs["ui/stageScale"] = value
            }
        }
        SettingRow {
            title: qsTr("Message box text size")
            ValueSlider {
                label: qsTr("Message box text size")
                from: 12; to: 40; suffix: " pt"
                value: App.prefs["ui/composerSize"]
                onMoved: App.prefs["ui/composerSize"] = value
            }
        }
        // A live sample, so the choices above are judged on real words.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: sample.implicitHeight + Theme.s4 * 2
            radius: Theme.radius
            color: Theme.sunken
            InkLine {
                id: sample
                x: Theme.s4
                y: Theme.s4
                width: parent.width - Theme.s4 * 2
                text: qsTr("The quick brown fox jumps over the lazy dog.")
                progress: 0.55
                animate: true
            }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Layout")
        iconName: "layout-grid"
        SettingRow {
            title: qsTr("Spacing")
            Segmented {
                label: qsTr("Spacing")
                value: App.prefs["ui/density"] || "comfortable"
                options: [{ value: "compact", label: qsTr("Compact") }, { value: "comfortable", label: qsTr("Comfortable") }, { value: "spacious", label: qsTr("Spacious") }]
                onActivated: (v) => App.prefs["ui/density"] = v
            }
        }
        SettingRow {
            title: qsTr("Corners")
            Segmented {
                label: qsTr("Corners")
                value: App.prefs["ui/corners"] || "soft"
                options: [{ value: "sharp", label: qsTr("Sharp") }, { value: "soft", label: qsTr("Soft") }, { value: "round", label: qsTr("Round") }]
                onActivated: (v) => App.prefs["ui/corners"] = v
            }
        }
        SettingRow {
            title: qsTr("Sidebar labels")
            Toggle { tip: qsTr("Sidebar labels"); checked: App.prefs["ui/sidebarLabels"] !== false; onToggled: App.prefs["ui/sidebarLabels"] = checked }
        }
        SettingRow {
            title: qsTr("Quick phrases under the message box")
            Toggle { tip: qsTr("Quick phrases under the message box"); checked: App.prefs["ui/showPhraseTray"] !== false; onToggled: App.prefs["ui/showPhraseTray"] = checked }
        }
        SettingRow {
            title: qsTr("Voice drawing")
            description: qsTr("How your voice is drawn under the message box.")
            Segmented {
                label: qsTr("Voice drawing")
                value: App.prefs["ui/waveStyle"] || "ink"
                options: [{ value: "ink", label: qsTr("Brush stroke") }, { value: "bars", label: qsTr("Bars") }, { value: "off", label: qsTr("Off") }]
                onActivated: (v) => App.prefs["ui/waveStyle"] = v
            }
        }
        SettingRow {
            title: qsTr("Fill words with ink as they're spoken")
            description: qsTr("Helps you and the people around you follow along.")
            Toggle { tip: qsTr("Fill words with ink as they're spoken"); checked: App.prefs["ui/inkEffect"] !== false; onToggled: App.prefs["ui/inkEffect"] = checked }
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Motion & windows")
        iconName: "app-window"
        SettingRow {
            title: qsTr("Animation")
            description: qsTr("Reduced keeps fades but drops movement.")
            Segmented {
                label: qsTr("Animation")
                value: App.prefs["ui/motion"] || "full"
                options: [{ value: "full", label: qsTr("Full") }, { value: "reduced", label: qsTr("Reduced") }, { value: "off", label: qsTr("Off") }]
                onActivated: (v) => App.prefs["ui/motion"] = v
            }
        }
        SettingRow {
            title: qsTr("Keep Vocal Ink above other windows")
            Toggle { tip: qsTr("Keep above other windows"); checked: App.prefs["ui/alwaysOnTop"] === true; onToggled: App.prefs["ui/alwaysOnTop"] = checked }
        }
        SettingRow {
            title: qsTr("Closing the window keeps it running in the tray")
            description: qsTr("So your shortcuts keep working.")
            Toggle { tip: qsTr("Keep running in the tray"); checked: App.prefs["ui/minimizeToTray"] !== false; onToggled: App.prefs["ui/minimizeToTray"] = checked }
        }
        SettingRow {
            title: qsTr("Compact bar opacity")
            ValueSlider {
                label: qsTr("Compact bar opacity")
                from: 40; to: 100; suffix: "%"
                value: App.prefs["ui/compactOpacity"]
                onMoved: App.prefs["ui/compactOpacity"] = value
            }
        }
    }
}
