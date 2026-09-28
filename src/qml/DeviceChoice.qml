import QtQuick
import Ink.Core

// Picks an audio device and saves it. kind: output | monitor | input | mic
Choice {
    id: root
    property string kind: "output"
    readonly property string key: kind === "output" ? "audio/outputDevice"
                                : kind === "monitor" ? "audio/monitorDevice"
                                : kind === "input" ? "audio/inputDevice" : "mic/device"

    label: kind === "output" ? qsTr("Voice output device") : kind === "monitor" ? qsTr("Headphones") : qsTr("Microphone")
    implicitWidth: Math.round(300 * Math.min(1.3, Theme.scale))
    textRole: "name"
    valueRole: "deviceId"
    currentIndex: Math.max(0, model.indexOfId(App.prefs[key] || ""))
    onActivated: App.useDevice(kind, currentValue)

    Connections {
        target: root.model
        function onCountChanged() { root.currentIndex = Math.max(0, root.model.indexOfId(App.prefs[root.key] || "")) }
    }
}
