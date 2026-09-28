import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Make a VTuber or PNGtuber avatar talk with the voice: any lip-sync app
// through the virtual mic, VTube Studio, VMC apps and veadotube directly, the
// built-in PNGtuber overlay, and Streamer.bot for everything else.
ScrollPage {
    id: page
    signal navigate(string page)
    title: qsTr("Avatar")
    subtitle: qsTr("Make your VTuber or PNGtuber move its mouth with your voice, and react when you talk.")

    readonly property var avatar: App.avatar
    readonly property var vts: avatar ? avatar.vts : null
    readonly property var vmc: avatar ? avatar.vmc : null
    readonly property var veado: avatar ? avatar.veado : null

    actions: [
        PillButton {
            kind: "secondary"
            iconName: "play"
            text: qsTr("Test the mouth")
            tip: qsTr("Moves the mouth like a short sentence on everything that's connected")
            onClicked: page.avatar.test(1800)
        }
    ]

    // A choice of "nothing" or one of `items` ([{value, text}]), saved in `key`.
    component TargetChoice: Choice {
        property string key
        property var items: []
        readonly property string saved: App.prefs[key] || ""
        readonly property var entries: {
            const list = [{ value: "", text: qsTr("Nothing") }].concat(items)
            if (saved !== "" && !list.some(e => e.value === saved))
                list.push({ value: saved, text: saved })
            return list
        }
        textRole: "text"
        valueRole: "value"
        model: entries
        currentIndex: Math.max(0, entries.findIndex(e => e.value === saved))
        onActivated: (i) => App.prefs[key] = entries[i].value
    }
    component StatusLine: StatusChip { focusPolicy: Qt.NoFocus }

    function vtsText() {
        if (!vts) return ""
        switch (vts.status) {
        case VtsClient.Off: return qsTr("Off")
        case VtsClient.Searching: return qsTr("Looking for VTube Studio…")
        case VtsClient.Connecting: return qsTr("Connecting…")
        case VtsClient.NotRunning: return qsTr("VTube Studio isn't running")
        case VtsClient.ApiOff: return qsTr("Turn on “Allow Plugin API access” in VTube Studio")
        case VtsClient.WaitingForAllow: return qsTr("Click “Allow” in VTube Studio")
        case VtsClient.Denied: return qsTr("VTube Studio didn't allow Vocal Ink")
        case VtsClient.Connected: return vts.modelName ? qsTr("Connected · %1").arg(vts.modelName) : qsTr("Connected")
        }
        return vts.detail || qsTr("Something went wrong")
    }
    function vtsTone() {
        if (!vts) return "idle"
        switch (vts.status) {
        case VtsClient.Connected: return "ok"
        case VtsClient.Searching: case VtsClient.Connecting: case VtsClient.WaitingForAllow: return "accent"
        case VtsClient.Off: return "idle"
        }
        return "warn"
    }
    function veadoText() {
        if (!veado) return ""
        switch (veado.status) {
        case VeadotubeClient.Off: return qsTr("Off")
        case VeadotubeClient.Searching: return qsTr("Looking for veadotube…")
        case VeadotubeClient.NotRunning: return qsTr("veadotube isn't running, or its WebSocket server is off")
        case VeadotubeClient.Connected: return veado.instanceName ? qsTr("Connected · %1").arg(veado.instanceName) : qsTr("Connected")
        }
        return veado.detail || qsTr("Something went wrong")
    }

    // --- The mouth -------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Mouth")
        subtitle: qsTr("One mouth movement, sent to everything you connect below.")
        iconName: "smile"

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s6

            Rectangle {
                Layout.preferredWidth: Math.round(200 * Theme.scale)
                Layout.preferredHeight: Math.round(200 * Theme.scale)
                Layout.alignment: Qt.AlignTop
                radius: Theme.radiusLg
                color: Theme.sunken
                border.color: Theme.line
                border.width: Theme.hairline
                InkBlob {
                    anchors.centerIn: parent
                    width: parent.width * 0.62
                    height: parent.height * 0.66
                    mouth: page.avatar ? page.avatar.mouth : 0
                    viseme: page.avatar ? page.avatar.viseme : ""
                    micLive: App.micLive
                }
                Txt {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.s2
                    role: "caption"
                    font.family: Theme.monoFont
                    text: page.avatar && page.avatar.talking ? qsTr("talking") : qsTr("quiet")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s4
                SettingRow {
                    title: qsTr("What moves the mouth")
                    Segmented {
                        label: qsTr("What moves the mouth")
                        value: App.prefs["avatar/source"] || "voice"
                        options: [{ value: "voice", label: qsTr("Voice") },
                                  { value: "voiceAndSounds", label: qsTr("Voice + sounds") },
                                  { value: "everything", label: qsTr("Also my real mic") }]
                        onActivated: (v) => App.prefs["avatar/source"] = v
                    }
                }
                SettingRow {
                    title: qsTr("Sensitivity")
                    description: qsTr("Higher opens the mouth wider for quiet voices.")
                    ValueSlider {
                        label: qsTr("Sensitivity")
                        from: 20; to: 300; stepSize: 10; suffix: "%"
                        value: App.prefs["avatar/sensitivity"] || 100
                        onMoved: App.prefs["avatar/sensitivity"] = value
                    }
                }
                SettingRow {
                    title: qsTr("Smoothing")
                    description: qsTr("Less is snappier, more is calmer.")
                    ValueSlider {
                        label: qsTr("Smoothing")
                        from: 0; to: 100; suffix: "%"
                        value: App.prefs["avatar/smoothing"] !== undefined ? App.prefs["avatar/smoothing"] : 40
                        onMoved: App.prefs["avatar/smoothing"] = value
                    }
                }
                SettingRow {
                    title: qsTr("Mouth shapes from the words")
                    description: qsTr("A, I, U, E and O shapes follow what's being said, for models that have them.")
                    Toggle { tip: qsTr("Mouth shapes from the words"); checked: App.prefs["avatar/visemes"] !== false; onToggled: App.prefs["avatar/visemes"] = checked }
                }
            }
        }
    }

    // --- Any app ----------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Works with any avatar app")
        subtitle: qsTr("No setup here: pick Vocal Ink's microphone in your app.")
        iconName: "mic"
        Txt {
            Layout.fillWidth: true
            text: qsTr("In your avatar app's lip-sync or microphone settings, choose “Vocal Ink Mic”. The mouth follows what Vocal Ink says, like it would follow your voice. This works with VTube Studio, VSeeFace, Warudo, VNyan, veadotube, PNGTuber Plus, Animaze and most others.")
        }
        RowLayout {
            spacing: Theme.s2
            PillButton { small: true; iconName: "book-open"; text: qsTr("Setup guides"); onClicked: App.openUrl("https://github.com/Vocal-Ink/Desktop-app/blob/main/docs/VTUBING.md") }
            PillButton { small: true; kind: "ghost"; iconName: "cable"; text: qsTr("Virtual mic settings"); onClicked: page.navigate("audio") }
        }
    }

    // --- VTube Studio -------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: "VTube Studio"
        subtitle: qsTr("Mouth, expressions and hotkeys through VTube Studio's plugin API.")
        iconName: "smile"
        headerExtra: Toggle {
            tip: qsTr("Connect to VTube Studio")
            checked: App.prefs["vts/enabled"] === true
            onToggled: App.prefs["vts/enabled"] = checked
        }

        RowLayout {
            Layout.fillWidth: true
            visible: App.prefs["vts/enabled"] === true
            spacing: Theme.s2
            StatusLine {
                tone: page.vtsTone()
                pulse: page.vts && (page.vts.status === VtsClient.WaitingForAllow || page.vts.status === VtsClient.Connecting)
                text: page.vtsText()
            }
            Item { Layout.fillWidth: true }
            PillButton {
                visible: page.vts && page.vts.status === VtsClient.Denied
                small: true
                kind: "primary"
                text: qsTr("Ask again")
                onClicked: page.vts.requestAccess()
            }
            PillButton {
                visible: page.vts && page.vts.status !== VtsClient.Connected && page.vts.status !== VtsClient.Denied
                small: true
                kind: "ghost"
                iconName: "refresh-cw"
                text: qsTr("Retry")
                onClicked: page.vts.reconnect()
            }
            PillButton {
                visible: page.vts && page.vts.status === VtsClient.Connected
                small: true
                kind: "ghost"
                text: qsTr("Forget access")
                tip: qsTr("VTube Studio will ask to allow Vocal Ink again next time")
                onClicked: page.vts.forgetAccess()
            }
        }
        Txt {
            Layout.fillWidth: true
            visible: App.prefs["vts/enabled"] !== true
            role: "caption"
            text: qsTr("In VTube Studio: Settings → “Allow Plugin API access”. The first time, VTube Studio asks you to allow Vocal Ink.")
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: App.prefs["vts/enabled"] === true
            spacing: Theme.s4

            SettingRow {
                title: qsTr("Mouth parameter")
                description: qsTr("The input parameter the mouth drives. Almost every model uses MouthOpen.")
                Field {
                    label: qsTr("Mouth parameter")
                    implicitWidth: Math.round(220 * Theme.scale)
                    text: App.prefs["vts/mouthParam"] || "MouthOpen"
                    onEditingFinished: App.prefs["vts/mouthParam"] = text.trim() || "MouthOpen"
                }
            }
            SettingRow {
                title: qsTr("Smile parameter")
                description: qsTr("A little smile while talking. Leave empty to keep your own.")
                Field {
                    label: qsTr("Smile parameter")
                    implicitWidth: Math.round(220 * Theme.scale)
                    placeholderText: qsTr("Off")
                    text: App.prefs["vts/mouthFormParam"] || ""
                    onEditingFinished: App.prefs["vts/mouthFormParam"] = text.trim()
                }
            }
            SettingRow {
                title: qsTr("No face tracking")
                description: qsTr("Tell VTube Studio your face is found while you talk, so the model doesn't play its “tracking lost” pose.")
                Toggle { tip: qsTr("No face tracking"); checked: App.prefs["vts/faceFound"] === true; onToggled: App.prefs["vts/faceFound"] = checked }
            }
            SettingRow {
                title: qsTr("Extra parameters for riggers")
                description: qsTr("Also send VocalInkVolume and VocalInkSpeaking, to map to anything in your model.")
                Toggle { tip: qsTr("Extra parameters for riggers"); checked: App.prefs["vts/customParams"] === true; onToggled: App.prefs["vts/customParams"] = checked }
            }

            Txt { text: qsTr("When things happen"); role: "label"; Layout.topMargin: Theme.s2 }
            Txt {
                Layout.fillWidth: true
                visible: !page.vts || !page.vts.connected
                role: "caption"
                text: qsTr("Connect to see your model's expressions and hotkeys.")
            }
            SettingRow {
                title: qsTr("Expression while speaking")
                TargetChoice {
                    key: "vts/expressionWhileSpeaking"
                    label: qsTr("Expression while speaking")
                    items: page.vts ? page.vts.expressions.map(e => ({ value: e.file, text: e.name })) : []
                }
            }
            Repeater {
                model: [
                    { key: "vts/hotkeyOnStart", title: qsTr("Hotkey when you start speaking") },
                    { key: "vts/hotkeyOnStop", title: qsTr("Hotkey when you finish") },
                    { key: "vts/hotkeyOnMicLive", title: qsTr("Hotkey when your real mic goes live") },
                    { key: "vts/hotkeyOnMicMuted", title: qsTr("Hotkey when your real mic is muted") }
                ]
                SettingRow {
                    required property var modelData
                    title: modelData.title
                    TargetChoice {
                        key: modelData.key
                        label: modelData.title
                        items: page.vts ? page.vts.hotkeys.map(h => ({ value: h.id, text: h.name })) : []
                    }
                }
            }
        }
    }

    // --- VMC: VSeeFace, Warudo, VNyan, VirtualMotionCapture --------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("VSeeFace, Warudo, VNyan")
        subtitle: qsTr("Mouth shapes over the VMC protocol, for 3D models.")
        iconName: "user-round"
        headerExtra: Toggle {
            tip: qsTr("Send mouth shapes over VMC")
            checked: App.prefs["vmc/enabled"] === true
            onToggled: App.prefs["vmc/enabled"] = checked
        }
        Txt {
            Layout.fillWidth: true
            role: "caption"
            text: qsTr("Turn on the VMC receiver in your app, with the port shown here.")
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: App.prefs["vmc/enabled"] === true
            spacing: Theme.s4
            StatusLine {
                tone: page.vmc && page.vmc.sending ? "ok" : "idle"
                text: page.vmc && page.vmc.sending ? qsTr("Sending to %1:%2").arg(page.vmc.host).arg(page.vmc.port)
                                                   : qsTr("Ready; nothing sent yet")
            }
            SettingRow {
                title: qsTr("App")
                Segmented {
                    label: qsTr("VMC app")
                    value: App.prefs["vmc/preset"] || "vseeface"
                    options: [{ value: "vseeface", label: "VSeeFace" }, { value: "warudo", label: "Warudo" },
                              { value: "vnyan", label: "VNyan" }, { value: "vmc", label: "VMC" },
                              { value: "custom", label: qsTr("Other") }]
                    onActivated: (v) => {
                        App.prefs["vmc/preset"] = v
                        if (v !== "custom")
                            App.prefs["vmc/port"] = v === "vmc" ? 39540 : 39539
                    }
                }
            }
            SettingRow {
                title: qsTr("Address")
                description: qsTr("This computer is 127.0.0.1. Use another PC's address if the app runs there.")
                RowLayout {
                    spacing: Theme.s2
                    Field {
                        label: qsTr("Host")
                        implicitWidth: Math.round(170 * Theme.scale)
                        text: App.prefs["vmc/host"] || "127.0.0.1"
                        onEditingFinished: App.prefs["vmc/host"] = text.trim() || "127.0.0.1"
                    }
                    Field {
                        label: qsTr("Port")
                        implicitWidth: Math.round(96 * Theme.scale)
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1; top: 65535 }
                        text: String(App.prefs["vmc/port"] || 39539)
                        onEditingFinished: App.prefs["vmc/port"] = parseInt(text) || 39539
                    }
                }
            }
            SettingRow {
                title: qsTr("Model format")
                description: qsTr("VRM 0.x models use A I U E O; VRM 1.0 models use aa ih ou ee oh.")
                Segmented {
                    label: qsTr("Model format")
                    value: App.prefs["vmc/blendset"] || "vrm0"
                    options: [{ value: "vrm0", label: "VRM 0.x" }, { value: "vrm1", label: "VRM 1.0" }]
                    onActivated: (v) => App.prefs["vmc/blendset"] = v
                }
            }
            SettingRow {
                title: qsTr("Expression while speaking")
                description: qsTr("A blendshape held while you talk, like Joy or Fun. Leave empty for none.")
                Field {
                    label: qsTr("Expression while speaking")
                    implicitWidth: Math.round(170 * Theme.scale)
                    placeholderText: qsTr("None")
                    text: App.prefs["vmc/expression"] || ""
                    onEditingFinished: App.prefs["vmc/expression"] = text.trim()
                }
            }
            SettingRow {
                title: qsTr("Mouth size")
                ValueSlider {
                    label: qsTr("Mouth size")
                    from: 20; to: 200; stepSize: 5; suffix: "%"
                    value: App.prefs["vmc/gain"] || 100
                    onMoved: App.prefs["vmc/gain"] = value
                }
            }
        }
    }

    // --- veadotube -----------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: "veadotube mini"
        subtitle: qsTr("Opens the mouth only while Vocal Ink speaks, and switches states.")
        iconName: "image-plus"
        headerExtra: Toggle {
            tip: qsTr("Connect to veadotube mini")
            checked: App.prefs["veado/enabled"] === true
            onToggled: App.prefs["veado/enabled"] = checked
        }
        Txt {
            Layout.fillWidth: true
            role: "caption"
            text: qsTr("In veadotube mini: turn on the WebSocket server in its program settings, pick “Vocal Ink Mic” as the microphone, and turn on “use websocket” for push-to-talk.")
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: App.prefs["veado/enabled"] === true
            spacing: Theme.s4
            RowLayout {
                spacing: Theme.s2
                StatusLine {
                    tone: page.veado && page.veado.status === VeadotubeClient.Connected ? "ok"
                        : page.veado && page.veado.status === VeadotubeClient.Searching ? "accent" : "warn"
                    text: page.veadoText()
                }
                PillButton { small: true; kind: "ghost"; iconName: "refresh-cw"; text: qsTr("Refresh"); onClicked: { page.veado.reconnect(); page.veado.refreshStates() } }
            }
            SettingRow {
                title: qsTr("Mouth only while speaking")
                description: qsTr("Uses veadotube's push-to-talk, so the room around you doesn't move the mouth.")
                Toggle { tip: qsTr("Mouth only while speaking"); checked: App.prefs["veado/pushToTalk"] !== false; onToggled: App.prefs["veado/pushToTalk"] = checked }
            }
            Repeater {
                model: [
                    { key: "veado/talkingState", title: qsTr("State while speaking") },
                    { key: "veado/idleState", title: qsTr("State when quiet") },
                    { key: "veado/micLiveState", title: qsTr("State while your real mic is live") }
                ]
                SettingRow {
                    required property var modelData
                    title: modelData.title
                    TargetChoice {
                        key: modelData.key
                        label: modelData.title
                        items: page.veado ? page.veado.states.map(s => ({ value: s.id, text: s.name })) : []
                    }
                }
            }
        }
    }

    // --- Built-in PNGtuber ------------------------------------------------------------
    Card {
        id: pngCard
        Layout.fillWidth: true
        title: qsTr("Built-in PNGtuber")
        subtitle: qsTr("No avatar app? Show your own pictures on stream, talking and blinking.")
        iconName: "radio"
        readonly property var profile: {
            const list = App.overlayProfiles
            for (let i = 0; i < list.length; ++i)
                if (list[i].kind === "avatar")
                    return list[i]
            return null
        }
        Txt {
            Layout.fillWidth: true
            text: qsTr("An OBS browser source with a mouth-closed and a mouth-open picture (plus blinking, if you like). Without pictures it shows Vocal Ink's ink drop.")
        }
        RowLayout {
            spacing: Theme.s2
            PillButton {
                kind: "primary"
                iconName: pngCard.profile ? "pencil" : "plus"
                text: pngCard.profile ? qsTr("Edit PNGtuber") : qsTr("Create PNGtuber overlay")
                onClicked: {
                    if (!pngCard.profile)
                        App.addOverlayProfile("avatar", qsTr("PNGtuber"))
                    page.navigate("stream")
                }
            }
        }
    }

    // --- Streamer.bot -------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: "Streamer.bot"
        subtitle: qsTr("Run your own actions when you start or stop speaking: lights, scenes, T.I.T.S., anything Streamer.bot controls.")
        iconName: "zap"
        headerExtra: Toggle {
            tip: qsTr("Run Streamer.bot actions")
            checked: App.prefs["streamerbot/enabled"] === true
            onToggled: App.prefs["streamerbot/enabled"] = checked
        }
        Txt {
            Layout.fillWidth: true
            role: "caption"
            text: qsTr("Turn on Streamer.bot's UDP server (Servers/Clients → UDP Server) and create actions with these names.")
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: App.prefs["streamerbot/enabled"] === true
            spacing: Theme.s4
            SettingRow {
                title: qsTr("Address")
                RowLayout {
                    spacing: Theme.s2
                    Field {
                        label: qsTr("Host")
                        implicitWidth: Math.round(170 * Theme.scale)
                        text: App.prefs["streamerbot/host"] || "127.0.0.1"
                        onEditingFinished: App.prefs["streamerbot/host"] = text.trim() || "127.0.0.1"
                    }
                    Field {
                        label: qsTr("Port")
                        implicitWidth: Math.round(96 * Theme.scale)
                        validator: IntValidator { bottom: 1; top: 65535 }
                        text: String(App.prefs["streamerbot/port"] || 4242)
                        onEditingFinished: App.prefs["streamerbot/port"] = parseInt(text) || 4242
                    }
                }
            }
            Repeater {
                model: [
                    { key: "streamerbot/actionStart", title: qsTr("When you start speaking") },
                    { key: "streamerbot/actionStop", title: qsTr("When you finish") },
                    { key: "streamerbot/actionMicLive", title: qsTr("When your real mic goes live") },
                    { key: "streamerbot/actionMicMuted", title: qsTr("When your real mic is muted") }
                ]
                SettingRow {
                    required property var modelData
                    title: modelData.title
                    Field {
                        label: modelData.title
                        implicitWidth: Math.round(240 * Theme.scale)
                        placeholderText: qsTr("Action name (empty = nothing)")
                        text: App.prefs[modelData.key] || ""
                        onEditingFinished: App.prefs[modelData.key] = text.trim()
                    }
                }
            }
        }
    }
}
