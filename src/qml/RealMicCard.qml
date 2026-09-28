import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Real-microphone passthrough: optional, off by default, loud about being on.
Card {
    id: card
    title: qsTr("Your real microphone")
    subtitle: qsTr("Optionally mix your actual mic into the virtual mic too: for a laugh, background sound, or when you do want to talk. Off by default.")
    iconName: "mic"

    readonly property string mode: App.prefs["mic/mode"] || "off"

    headerExtra: StatusChip {
        visible: card.mode !== "off"
        tone: App.micLive ? "live" : "idle"
        pulse: App.micLive
        iconName: App.micLive ? "mic" : "mic-off"
        text: App.micLive ? qsTr("LIVE") : qsTr("Muted")
        onClicked: App.setMicLive(!App.micLive)
        tip: App.micLive ? qsTr("Click to mute") : qsTr("Click to go live")
    }

    SettingRow {
        title: qsTr("How it turns on")
        stacked: true
        Segmented {
            label: qsTr("Real microphone mode")
            value: card.mode
            options: [
                { value: "off", label: qsTr("Off"), icon: "mic-off" },
                { value: "hold", label: qsTr("While I hold a key"), icon: "hand" },
                { value: "toggle", label: qsTr("Key toggles it"), icon: "keyboard" },
                { value: "always", label: qsTr("Always on"), icon: "mic" }
            ]
            onActivated: (v) => {
                if (v === "always" && card.mode !== "always")
                    alwaysSheet.open()
                else
                    App.prefs["mic/mode"] = v
            }
        }
    }

    // The warning is part of the setting, not hidden in a tooltip.
    Rectangle {
        visible: card.mode !== "off"
        Layout.fillWidth: true
        implicitHeight: warnRow.implicitHeight + Theme.s3 * 2
        radius: Theme.radius
        color: Theme.liveWash
        border.color: Theme.alpha(Theme.live, 0.45)
        border.width: Theme.hairline
        RowLayout {
            id: warnRow
            anchors.fill: parent
            anchors.margins: Theme.s3
            spacing: Theme.s3
            Icon { name: "shield-alert"; color: Theme.live; Layout.alignment: Qt.AlignTop }
            Txt {
                Layout.fillWidth: true
                font.pixelSize: Theme.fsSm
                text: card.mode === "always" ? qsTr("Your real mic is sent all the time. Anyone in your call or stream hears you, including background noise. Use the panic key (%1) to mute everything at once.").arg(App.shortcutFor("panic.mute") || qsTr("unset"))
                    : card.mode === "hold" ? qsTr("While the key is held, people in your call or stream hear your real microphone. A red badge stays on screen while it's live.")
                    : qsTr("Pressing the key sends your real microphone until you press it again. A red badge stays on screen while it's live, so it can't be left on by accident.")
            }
        }
    }

    SettingRow {
        visible: card.mode === "hold" || card.mode === "toggle"
        title: card.mode === "hold" ? qsTr("Hold this key to talk") : qsTr("Press this key to switch")
        description: App.hotkeysSupported ? qsTr("Works in any app, even while a game has focus.") : App.hotkeysUnsupportedReason
        ShortcutField {
            readonly property string actionId: card.mode === "hold" ? "mic.hold" : "mic.toggle"
            label: card.mode === "hold" ? qsTr("Hold to talk") : qsTr("Toggle real mic")
            sequence: App.shortcuts[actionId] || ""
            onRecorded: (seq) => {
                const clash = App.keybinds.bind(actionId, seq, false)
                if (clash.length > 0) { conflict.seq = seq; conflict.action = actionId; conflict.names = clash; conflict.open() }
            }
            onCleared: App.keybinds.clear(actionId)
        }
    }

    SettingRow {
        visible: card.mode !== "off"
        title: qsTr("Microphone")
        DeviceChoice { kind: "mic"; model: App.inputs }
    }
    SettingRow {
        visible: card.mode !== "off"
        title: qsTr("Mic volume")
        ValueSlider {
            label: qsTr("Mic gain")
            from: -24; to: 24
            format: (v) => qsTr("%1 dB", "decibels").arg((v > 0 ? "+" : "") + Number(Math.round(v)).toLocaleString(Qt.locale(), "f", 0))
            value: App.prefs["mic/gainDb"]
            onMoved: App.prefs["mic/gainDb"] = value
        }
    }
    SettingRow {
        visible: card.mode !== "off"
        title: qsTr("Noise gate")
        description: qsTr("Silences the mic below this level, so keyboard and fan noise stay out.")
        ValueSlider {
            label: qsTr("Noise gate")
            from: -90; to: -20
            format: (v) => v <= -89 ? qsTr("Off") : qsTr("%1 dB", "decibels").arg(Number(Math.round(v)).toLocaleString(Qt.locale(), "f", 0))
            value: App.prefs["mic/gateDb"]
            onMoved: App.prefs["mic/gateDb"] = value
        }
    }
    SettingRow {
        visible: card.mode !== "off"
        title: qsTr("Lower my mic while the voice speaks")
        description: qsTr("Keeps your typed messages clear over background sound.")
        Toggle {
            tip: qsTr("Lower my mic while the voice speaks")
            checked: App.prefs["mic/duck"] === true
            onToggled: App.prefs["mic/duck"] = checked
        }
    }
    SettingRow {
        visible: card.mode !== "off"
        title: qsTr("Show a red LIVE badge on screen")
        description: qsTr("Stays on top of games and other apps while your mic is live.")
        Toggle {
            tip: qsTr("Show a red LIVE badge on screen")
            checked: App.prefs["mic/warnOverlay"] !== false
            onToggled: App.prefs["mic/warnOverlay"] = checked
        }
    }
    SettingRow {
        visible: card.mode !== "off"
        title: qsTr("Play a sound when it goes live or mutes")
        Toggle {
            tip: qsTr("Play a sound when it goes live or mutes")
            checked: App.prefs["mic/warnSound"] !== false
            onToggled: App.prefs["mic/warnSound"] = checked
        }
    }
    RowLayout {
        visible: card.mode !== "off"
        Layout.fillWidth: true
        spacing: Theme.s3
        PillButton {
            kind: App.micLive ? "live" : "secondary"
            iconName: App.micLive ? "mic" : "mic-off"
            text: App.micLive ? qsTr("Mute now") : qsTr("Test: go live")
            onClicked: App.setMicLive(!App.micLive)
        }
        LevelBar {
            Layout.fillWidth: true
            level: App.micLiveLevel
            tint: App.micLive ? Theme.live : Theme.faint
        }
    }

    Sheet {
        id: alwaysSheet
        title: qsTr("Keep your real mic on all the time?")
        message: qsTr("Everyone in your calls and streams will hear your microphone whenever Vocal Ink is running. You can mute it any time from the tray, this page, or the panic key.")
        iconName: "shield-alert"
        iconTint: Theme.live
        footer: [
            PillButton { text: qsTr("Turn it on"); kind: "live"; onClicked: { App.prefs["mic/mode"] = "always"; alwaysSheet.close() } },
            PillButton { text: qsTr("Cancel"); kind: "ghost"; onClicked: alwaysSheet.close() }
        ]
    }

    ConflictSheet { id: conflict }
}
