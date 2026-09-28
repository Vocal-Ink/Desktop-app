import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Captions for your stream, OBS control, and reading Twitch chat aloud.
ScrollPage {
    id: page
    title: qsTr("Stream")
    subtitle: qsTr("Put what you say on screen as captions, drive OBS, and let Vocal Ink read your Twitch chat.")

    actions: [
        PillButton {
            kind: App.captionsPaused ? "primary" : "secondary"
            iconName: App.captionsPaused ? "play" : "pause"
            text: App.captionsPaused ? qsTr("Resume captions") : qsTr("Pause captions")
            onClicked: App.setCaptionsPaused(!App.captionsPaused)
        },
        PillButton { kind: "ghost"; iconName: "x"; text: qsTr("Clear"); onClicked: App.clearCaptions(); tip: qsTr("Remove the caption on screen now") }
    ]

    // Overlay settings live in one query string; edit it as a map.
    function queryMap() {
        const out = {}
        const q = App.prefs["overlay/query"] || ""
        q.split("&").forEach(p => { if (p) { const kv = p.split("="); out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || "") } })
        return out
    }
    function setQuery(key, value, fallback) {
        const m = queryMap()
        if (value === fallback || value === "" || value === undefined) delete m[key]
        else m[key] = value
        App.prefs["overlay/query"] = Object.keys(m).map(k => encodeURIComponent(k) + "=" + encodeURIComponent(m[k])).join("&")
    }

    // --- Caption overlay --------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Caption overlay")
        subtitle: qsTr("A browser source for OBS, Streamlabs or any streaming app. No OBS plugin needed.")
        iconName: "captions"
        headerExtra: Toggle {
            tip: qsTr("Caption overlay")
            checked: App.prefs["overlay/enabled"] === true
            onToggled: App.prefs["overlay/enabled"] = checked
        }

        ColumnLayout {
            visible: App.prefs["overlay/enabled"] === true
            Layout.fillWidth: true
            spacing: Theme.s4

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                Field {
                    Layout.fillWidth: true
                    readOnly: true
                    iconName: "link"
                    label: qsTr("Overlay address")
                    text: App.overlayUrl !== "" ? App.overlayUrl : qsTr("Starting…")
                    font.family: Theme.monoFont
                }
                PillButton { text: qsTr("Copy"); iconName: "copy"; enabled: App.overlayUrl !== ""; onClicked: { App.copy(App.overlayUrl); App.notifyUser(qsTr("Overlay address copied. Add it as a Browser source in OBS."), 0) } }
                IconButton { iconName: "external-link"; tip: qsTr("Open in your browser"); enabled: App.overlayUrl !== ""; onClicked: App.openUrl(App.overlayUrl + (App.overlayUrl.indexOf("?") >= 0 ? "&" : "?") + "demo=1") }
            }
            Txt {
                Layout.fillWidth: true
                role: "caption"
                text: App.overlayClients > 0 ? qsTr("%n overlay(s) connected.", "", App.overlayClients)
                                             : qsTr("Nothing connected yet. In OBS: Sources → + → Browser, paste the address, width 1920, height 1080.")
                color: App.overlayClients > 0 ? Theme.ok : Theme.muted
            }

            SettingRow {
                title: qsTr("Style")
                Segmented {
                    label: qsTr("Caption style")
                    value: page.queryMap()["style"] || "subtitles"
                    options: [
                        { value: "subtitles", label: qsTr("Subtitles") },
                        { value: "ink", label: qsTr("Ink") },
                        { value: "bubble", label: qsTr("Bubble") },
                        { value: "plain", label: qsTr("Outline") }
                    ]
                    onActivated: (v) => page.setQuery("style", v, "subtitles")
                }
            }
            SettingRow {
                title: qsTr("Position")
                Segmented {
                    label: qsTr("Caption position")
                    value: page.queryMap()["position"] || "bottom"
                    options: [
                        { value: "top", label: qsTr("Top") },
                        { value: "middle", label: qsTr("Middle") },
                        { value: "bottom", label: qsTr("Bottom") }
                    ]
                    onActivated: (v) => page.setQuery("position", v, "bottom")
                }
            }
            SettingRow {
                title: qsTr("Text size")
                ValueSlider {
                    label: qsTr("Caption text size")
                    from: 20; to: 120; suffix: " px"
                    value: parseInt(page.queryMap()["size"] || "42")
                    onMoved: page.setQuery("size", String(Math.round(value)), "42")
                }
            }
            SettingRow {
                title: qsTr("Stay on screen")
                description: qsTr("How long a caption stays after you finish speaking.")
                ValueSlider {
                    label: qsTr("Caption hold time")
                    from: 1; to: 30
                    format: (v) => qsTr("%1 s", "seconds").arg(Number(Math.round(v)).toLocaleString(Qt.locale(), "f", 0))
                    value: parseFloat(page.queryMap()["hold"] || "4")
                    onMoved: page.setQuery("hold", String(Math.round(value)), "4")
                }
            }
            SettingRow {
                title: qsTr("Reveal word by word")
                description: qsTr("Words appear in time with the voice.")
                Toggle {
                    tip: qsTr("Reveal word by word")
                    checked: (page.queryMap()["reveal"] || "word") === "word"
                    onToggled: page.setQuery("reveal", checked ? "word" : "instant", "word")
                }
            }
            SettingRow {
                title: qsTr("Show the voice's name")
                Toggle {
                    tip: qsTr("Show the voice's name")
                    checked: page.queryMap()["name"] === "1"
                    onToggled: page.setQuery("name", checked ? "1" : "", "")
                }
            }
            SettingRow {
                title: qsTr("Let other computers on my network load it")
                description: qsTr("For a separate streaming PC. Leave off otherwise.")
                Toggle {
                    tip: qsTr("Allow other computers on my network")
                    checked: App.prefs["overlay/allowLan"] === true
                    onToggled: App.prefs["overlay/allowLan"] = checked
                }
            }
        }
    }

    // --- OBS ---------------------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("OBS")
        subtitle: qsTr("Connects to OBS 28+ (Tools → WebSocket Server Settings) to write subtitles, send closed captions and show a “talking” source.")
        iconName: "tv"
        headerExtra: Toggle {
            tip: qsTr("Connect to OBS")
            checked: App.prefs["obs/enabled"] === true
            onToggled: App.prefs["obs/enabled"] = checked
        }

        readonly property bool connected: App.obsStatus === 2

        ColumnLayout {
            visible: App.prefs["obs/enabled"] === true
            Layout.fillWidth: true
            spacing: Theme.s4

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                StatusChip {
                    tone: App.obsStatus === 2 ? "ok" : App.obsStatus === 1 ? "accent" : App.obsStatus >= 3 ? "live" : "idle"
                    pulse: App.obsStatus === 1
                    text: App.obsStatusText || qsTr("Not connected")
                    onClicked: App.obsReconnect()
                    tip: qsTr("Click to reconnect")
                }
                Item { Layout.fillWidth: true }
                PillButton { small: true; kind: "ghost"; iconName: "refresh-cw"; text: qsTr("Reconnect"); onClicked: App.obsReconnect() }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > Math.round(760 * Theme.scale) ? 3 : 1
                columnSpacing: Theme.s3
                rowSpacing: Theme.s2
                ColumnLayout {
                    spacing: Theme.s1
                    Layout.fillWidth: true
                    Txt { text: qsTr("Host"); role: "label" }
                    Field { Layout.fillWidth: true; label: qsTr("OBS host"); text: App.prefs["obs/host"] || "127.0.0.1"; onEditingFinished: App.prefs["obs/host"] = text.trim() }
                }
                ColumnLayout {
                    spacing: Theme.s1
                    Txt { text: qsTr("Port"); role: "label" }
                    Field {
                        implicitWidth: Math.round(110 * Theme.scale)
                        label: qsTr("OBS port")
                        text: String(App.prefs["obs/port"] || 4455)
                        validator: IntValidator { bottom: 1; top: 65535 }
                        onEditingFinished: App.prefs["obs/port"] = parseInt(text)
                    }
                }
                ColumnLayout {
                    spacing: Theme.s1
                    Layout.fillWidth: true
                    Txt { text: qsTr("Password"); role: "label" }
                    Field {
                        Layout.fillWidth: true
                        secret: true
                        label: qsTr("OBS WebSocket password")
                        placeholderText: App.secretsRevision >= 0 && App.hasSecret("obs") ? qsTr("Saved (%1)").arg(App.secretHint("obs")) : qsTr("From OBS → Show connect info")
                        onEditingFinished: if (text !== "") { App.setSecret("obs", text); clear() }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }

            SettingRow {
                title: qsTr("Add the caption overlay to my current scene")
                description: qsTr("Creates a browser source with your overlay address.")
                PillButton { text: qsTr("Add to scene"); iconName: "plus"; enabled: App.obsStatus === 2; onClicked: App.obsAddOverlay() }
            }
            SettingRow {
                title: qsTr("Write subtitles into a text source")
                description: qsTr("Uses a “Text (GDI+/FreeType)” source you style yourself in OBS.")
                Toggle {
                    tip: qsTr("Write subtitles into a text source")
                    checked: App.prefs["obs/subtitles/enabled"] === true
                    onToggled: App.prefs["obs/subtitles/enabled"] = checked
                }
            }
            RowLayout {
                visible: App.prefs["obs/subtitles/enabled"] === true
                Layout.fillWidth: true
                spacing: Theme.s2
                Choice {
                    id: textSources
                    Layout.fillWidth: true
                    label: qsTr("Text source")
                    property var names: []
                    model: names.length ? names : [App.prefs["obs/subtitles/source"] || qsTr("Pick a text source")]
                    currentIndex: Math.max(0, names.indexOf(App.prefs["obs/subtitles/source"] || ""))
                    onActivated: if (names.length) App.prefs["obs/subtitles/source"] = currentText
                    Component.onCompleted: if (App.obsStatus === 2) App.obsFetchSources("text")
                }
                IconButton { iconName: "refresh-cw"; tip: qsTr("Reload the list from OBS"); onClicked: App.obsFetchSources("text") }
                PillButton { text: qsTr("Create one"); onClicked: App.obsCreateTextSource("Vocal Ink subtitles"); enabled: App.obsStatus === 2 }
                PillButton { text: qsTr("Test"); onClicked: App.obsTest(); enabled: App.obsStatus === 2 }
            }
            SettingRow {
                title: qsTr("Send closed captions with the stream")
                description: qsTr("Viewers can turn them on in the Twitch or YouTube player.")
                Toggle {
                    tip: qsTr("Send closed captions")
                    checked: App.prefs["obs/captions/enabled"] === true
                    onToggled: App.prefs["obs/captions/enabled"] = checked
                }
            }
            SettingRow {
                title: qsTr("Show a source while I'm talking")
                description: qsTr("For a PNGtuber-style avatar or a “speaking” badge.")
                Toggle {
                    tip: qsTr("Show a source while I'm talking")
                    checked: App.prefs["obs/indicator/enabled"] === true
                    onToggled: App.prefs["obs/indicator/enabled"] = checked
                }
            }
            RowLayout {
                visible: App.prefs["obs/indicator/enabled"] === true
                Layout.fillWidth: true
                spacing: Theme.s2
                Choice {
                    id: allSources
                    Layout.fillWidth: true
                    label: qsTr("Talking source")
                    property var names: []
                    model: names.length ? names : [App.prefs["obs/indicator/source"] || qsTr("Pick a source")]
                    currentIndex: Math.max(0, names.indexOf(App.prefs["obs/indicator/source"] || ""))
                    onActivated: if (names.length) App.prefs["obs/indicator/source"] = currentText
                    Component.onCompleted: if (App.obsStatus === 2) App.obsFetchSources("all")
                }
                IconButton { iconName: "refresh-cw"; tip: qsTr("Reload the list from OBS"); onClicked: App.obsFetchSources("all") }
            }
        }

        Connections {
            target: App
            function onObsSources(kind, names, error) {
                if (error !== "") { App.notifyUser(error, 1); return }
                if (kind === "text") textSources.names = names
                else allSources.names = names
            }
            function onObsResult(ok, message) { App.notifyUser(message, ok ? 0 : 2) }
            function onObsChanged() {
                if (App.obsStatus === 2) { App.obsFetchSources("text"); App.obsFetchSources("all") }
            }
        }
    }

    // --- Twitch --------------------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Read Twitch chat aloud")
        subtitle: qsTr("Chat messages are spoken in a voice of your choice. No login needed: Vocal Ink only reads.")
        iconName: "message-circle"
        headerExtra: Toggle {
            tip: qsTr("Read Twitch chat aloud")
            checked: App.prefs["twitch/enabled"] === true
            onToggled: App.prefs["twitch/enabled"] = checked
        }

        ColumnLayout {
            visible: App.prefs["twitch/enabled"] === true
            Layout.fillWidth: true
            spacing: Theme.s4

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                Field {
                    Layout.fillWidth: true
                    iconName: "users-round"
                    label: qsTr("Twitch channel")
                    placeholderText: qsTr("Channel name, e.g. yourname")
                    text: App.prefs["twitch/channel"] || ""
                    onEditingFinished: App.prefs["twitch/channel"] = text.trim()
                }
                StatusChip {
                    tone: App.twitchConnected ? "ok" : "idle"
                    text: App.twitchConnected ? qsTr("Reading chat") : (App.twitchStatus || qsTr("Not connected"))
                }
            }
            SettingRow {
                title: qsTr("Chat voice")
                description: qsTr("Use a different voice from yours so viewers can tell who's talking.")
                Choice {
                    label: qsTr("Chat voice")
                    readonly property var keys: [""].concat(App.prefs["tts/favorites"] || [])
                    model: keys.map(k => k === "" ? qsTr("Same as mine") : App.voiceInfo(k).name)
                    currentIndex: Math.max(0, keys.indexOf(App.prefs["twitch/voice"] || ""))
                    onActivated: App.prefs["twitch/voice"] = keys[currentIndex]
                }
            }
            SettingRow {
                title: qsTr("Say who wrote it")
                description: qsTr("“alice says: hello”")
                Toggle { tip: qsTr("Say who wrote it"); checked: App.prefs["twitch/readNames"] !== false; onToggled: App.prefs["twitch/readNames"] = checked }
            }
            SettingRow {
                title: qsTr("Skip commands")
                description: qsTr("Messages starting with “!”")
                Toggle { tip: qsTr("Skip commands"); checked: App.prefs["twitch/skipCommands"] !== false; onToggled: App.prefs["twitch/skipCommands"] = checked }
            }
            SettingRow {
                title: qsTr("Skip messages with links")
                Toggle { tip: qsTr("Skip messages with links"); checked: App.prefs["twitch/skipLinks"] !== false; onToggled: App.prefs["twitch/skipLinks"] = checked }
            }
            SettingRow {
                title: qsTr("Only subscribers, VIPs and mods")
                Toggle { tip: qsTr("Only subscribers, VIPs and mods"); checked: App.prefs["twitch/subsOnly"] === true; onToggled: App.prefs["twitch/subsOnly"] = checked }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s1
                Txt { text: qsTr("Never read these users"); role: "label" }
                Field { Layout.fillWidth: true; label: qsTr("Ignored users"); text: App.prefs["twitch/ignoredUsers"] || ""; placeholderText: qsTr("Comma separated, e.g. nightbot, streamelements"); onEditingFinished: App.prefs["twitch/ignoredUsers"] = text }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s1
                Txt { text: qsTr("Skip messages containing"); role: "label" }
                Field { Layout.fillWidth: true; label: qsTr("Blocked words"); text: App.prefs["twitch/blockedWords"] || ""; placeholderText: qsTr("Comma separated words"); onEditingFinished: App.prefs["twitch/blockedWords"] = text }
            }
        }
    }
}
