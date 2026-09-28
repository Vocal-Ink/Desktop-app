import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Avatar step (only when "VTubing" was picked): the one thing that works with
// every avatar app, then direct connections for the popular ones.
OnbStep {
    id: step
    title: qsTr("Make your avatar talk")
    lead: qsTr("Your avatar's mouth can move with your voice. Pick what you use; the Avatar page has the rest (expressions, hotkeys, a built-in PNGtuber for OBS).")
    firstFocus: vtsToggle

    readonly property var avatar: App.avatar

    Card {
        Layout.fillWidth: true
        title: qsTr("Works with any avatar app")
        iconName: "mic"
        Txt {
            Layout.fillWidth: true
            text: qsTr("In your avatar app's lip-sync or microphone settings, choose “Vocal Ink Mic”. The mouth then follows what Vocal Ink says, like it would follow a real voice.")
        }
    }

    Card {
        Layout.fillWidth: true
        title: qsTr("Connect directly")
        subtitle: qsTr("Smoother mouth shapes and reactions while you talk.")
        iconName: "smile"
        SettingRow {
            title: "VTube Studio"
            description: qsTr("Turn on “Allow Plugin API access” in VTube Studio's settings. It will ask you to allow Vocal Ink once.")
            Toggle {
                id: vtsToggle
                tip: "VTube Studio"
                checked: App.prefs["vts/enabled"] === true
                onToggled: App.prefs["vts/enabled"] = checked
            }
        }
        SettingRow {
            title: qsTr("VSeeFace, Warudo, VNyan (VMC)")
            description: qsTr("Turn on the VMC receiver in your app. Vocal Ink sends mouth shapes to it.")
            Toggle {
                tip: qsTr("VMC protocol")
                checked: App.prefs["vmc/enabled"] === true
                onToggled: App.prefs["vmc/enabled"] = checked
            }
        }
        SettingRow {
            visible: App.prefs["vmc/enabled"] === true
            title: qsTr("Which app?")
            Segmented {
                label: qsTr("VMC app")
                value: App.prefs["vmc/preset"] || "vseeface"
                options: [{ value: "vseeface", label: "VSeeFace" }, { value: "warudo", label: "Warudo" },
                          { value: "vnyan", label: "VNyan" }, { value: "vmc", label: "VMC" }]
                onActivated: (v) => {
                    App.prefs["vmc/preset"] = v
                    App.prefs["vmc/port"] = v === "vmc" ? 39540 : 39539
                }
            }
        }
        SettingRow {
            title: "veadotube mini"
            description: qsTr("Turn on its WebSocket server. Vocal Ink opens its mouth only while speaking and can switch states.")
            Toggle {
                tip: "veadotube mini"
                checked: App.prefs["veado/enabled"] === true
                onToggled: App.prefs["veado/enabled"] = checked
            }
        }
    }
}
