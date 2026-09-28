import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

OnbStep {
    title: qsTr("Pick your ink")
    lead: qsTr("Purple is ours, but it's your voice.")

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
    Row {
        spacing: Theme.s2
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
    InkLine {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s4
        text: qsTr("This is how your words will look while they're spoken.")
        progress: 0.6
    }
}
