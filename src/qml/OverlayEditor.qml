import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Ink.Core

// Everything about one overlay profile: its address, a live preview, a look
// to start from, then every detail (docs/overlay-style.md). Changes reach
// connected OBS sources immediately.
ColumnLayout {
    id: ed
    property string profileId: "main"
    signal removed()

    readonly property var profile: {
        const list = App.overlayProfiles
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === profileId)
                return list[i]
        return null
    }
    readonly property string kind: profile ? profile.kind : "captions"
    // Re-read whenever any profile changes.
    readonly property var eff: { App.overlayProfiles; return App.overlayEffectiveStyle(profileId) }
    readonly property string url: { App.overlayProfiles; App.overlayRunning; return App.overlayProfileUrl(profileId) }

    function v(path, fallback) {
        let o = eff
        const parts = path.split(".")
        for (let i = 0; i < parts.length; ++i) {
            if (o === undefined || o === null || typeof o !== "object")
                return fallback
            o = o[parts[i]]
        }
        return o === undefined || o === null ? fallback : o
    }
    function set(path, value) { App.setOverlayStyleValue(profileId, path, value) }

    spacing: Theme.s5

    // --- The overlay ---------------------------------------------------------
    Card {
        Layout.fillWidth: true
        pad: Theme.s5

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            Icon {
                name: ed.kind === "chat" ? "message-circle" : ed.kind === "avatar" ? "smile" : "captions"
                color: Theme.accentText
            }
            Field {
                id: nameField
                Layout.fillWidth: true
                label: qsTr("Overlay name")
                text: ed.profile ? ed.profile.name : ""
                onEditingFinished: {
                    App.renameOverlayProfile(ed.profileId, text)
                    text = Qt.binding(() => ed.profile ? ed.profile.name : "")
                }
            }
            IconButton { iconName: "copy"; tip: qsTr("Duplicate this overlay"); onClicked: App.duplicateOverlayProfile(ed.profileId) }
            IconButton {
                visible: ed.profileId !== "main"
                iconName: "trash-2"
                tip: qsTr("Delete this overlay")
                onClicked: { App.removeOverlayProfile(ed.profileId); ed.removed() }
            }
        }

        OverlayPreview {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            style: ed.eff
            kind: ed.kind
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            Field {
                Layout.fillWidth: true
                readOnly: true
                iconName: "link"
                label: qsTr("Overlay address")
                text: ed.url !== "" ? ed.url : qsTr("Turn on the overlay server above")
                font.family: Theme.monoFont
            }
            PillButton {
                text: qsTr("Copy")
                iconName: "copy"
                enabled: ed.url !== ""
                onClicked: { App.copy(ed.url); App.notifyUser(qsTr("Address copied. In OBS: Sources → + → Browser, paste it, 1920 × 1080."), 0) }
            }
            IconButton {
                iconName: "external-link"
                tip: qsTr("Open in your browser (with sample text)")
                enabled: ed.url !== ""
                onClicked: App.openUrl(ed.url + (ed.url.indexOf("?") >= 0 ? "&" : "?") + "demo=1")
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            PillButton {
                kind: "primary"
                small: true
                iconName: "plus"
                text: qsTr("Add to OBS scene")
                enabled: App.obsStatus === 2 && ed.url !== ""
                tip: App.obsStatus === 2 ? "" : qsTr("Connect to OBS in the OBS tab first")
                onClicked: App.obsAddOverlayProfile(ed.profileId)
            }
            PillButton {
                small: true
                iconName: "play"
                text: qsTr("Send a test caption")
                enabled: ed.url !== ""
                tip: qsTr("Shows sample text on every connected overlay without speaking")
                onClicked: App.sendTestCaption()
            }
            Item { Layout.fillWidth: true }
            PillButton {
                small: true
                kind: "ghost"
                iconName: "rotate-ccw"
                text: qsTr("Reset style")
                onClicked: App.resetOverlayStyle(ed.profileId)
            }
        }
    }

    // --- Looks ---------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        visible: ed.kind !== "avatar"
        title: qsTr("Look")
        subtitle: qsTr("Start from a look, then change anything below.")
        iconName: "palette"
        Flow {
            Layout.fillWidth: true
            spacing: Theme.s2
            Repeater {
                model: App.overlayPresetNames
                AbstractButton {
                    id: look
                    required property string modelData
                    readonly property var p: App.overlayPreset(modelData)
                    readonly property bool on: ed.v("preset", "subtitles") === modelData
                    function pv(path, fb) {
                        let o = p
                        for (const k of path.split(".")) { if (!o || typeof o !== "object") return fb; o = o[k] }
                        return o === undefined ? fb : o
                    }
                    width: Math.round(128 * Theme.scale)
                    height: Math.round(76 * Theme.scale)
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    checkable: true
                    checked: on
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: ed.presetName(modelData)
                    Accessible.checked: on
                    onClicked: App.applyOverlayPreset(ed.profileId, modelData)
                    background: Rectangle {
                        radius: Theme.radius
                        color: "#231a40"
                        border.width: look.on ? 3 : Theme.hairline
                        border.color: look.on ? Theme.accent : look.hovered ? Theme.text : Theme.line
                        FocusFrame { shown: look.visualFocus; baseRadius: parent.radius }
                        Rectangle {
                            anchors.centerIn: parent
                            anchors.verticalCenterOffset: -Math.round(8 * Theme.scale)
                            width: sampleText.implicitWidth + Theme.s3
                            height: sampleText.implicitHeight + Theme.s1
                            radius: look.pv("box.radius", 8) / 3
                            color: Qt.rgba(Qt.color(look.pv("colors.background", "#120d1f")).r, Qt.color(look.pv("colors.background", "#120d1f")).g,
                                           Qt.color(look.pv("colors.background", "#120d1f")).b, look.pv("colors.backgroundOpacity", 72) / 100)
                            Text {
                                id: sampleText
                                anchors.centerIn: parent
                                text: look.pv("font.uppercase", false) ? "HELLO" : "Hello"
                                font.family: look.pv("font.family", "Bricolage Grotesque")
                                font.pixelSize: Math.round(20 * Theme.scale)
                                font.weight: look.pv("font.weight", 700)
                                color: look.modelData === "ink" || look.modelData === "neon" ? look.pv("colors.ink", "#b48cff") : look.pv("colors.text", "#ffffff")
                                style: look.pv("effects.outline", 0) > 0 ? Text.Outline : Text.Normal
                                styleColor: look.pv("colors.outline", "#000000")
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: Theme.s1 + 2
                            text: ed.presetName(look.modelData)
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.fsXs
                            font.weight: look.on ? Font.Bold : Font.Normal
                            color: "#e8e2f7"
                        }
                    }
                }
            }
        }
    }
    function presetName(id) {
        switch (id) {
        case "subtitles": return qsTr("Subtitles")
        case "ink": return qsTr("Ink")
        case "bubble": return qsTr("Speech bubble")
        case "outline": return qsTr("Outline")
        case "karaoke": return qsTr("Karaoke")
        case "typewriter": return qsTr("Typewriter")
        case "neon": return qsTr("Neon")
        case "lowerthird": return qsTr("Lower third")
        case "minimal": return qsTr("Minimal")
        }
        return id
    }

    // --- Text ------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        visible: ed.kind !== "avatar"
        title: qsTr("Text")
        iconName: "type"
        SettingRow {
            title: qsTr("Font")
            description: qsTr("Fonts that come with Vocal Ink work everywhere. Any font installed on the streaming PC works too.")
            Choice {
                id: fontChoice
                label: qsTr("Font")
                implicitWidth: Math.round(240 * Theme.scale)
                readonly property var fonts: ["Bricolage Grotesque", "Vocal Ink Display", "Atkinson Hyperlegible Next", "Lexend", "OpenDyslexic"]
                readonly property string current: ed.v("font.family", "Bricolage Grotesque")
                model: fonts.indexOf(current) >= 0 ? fonts.concat([qsTr("Another font…")]) : fonts.concat([current])
                currentIndex: fonts.indexOf(current) >= 0 ? fonts.indexOf(current) : fonts.length
                onActivated: (i) => { if (i < fonts.length) ed.set("font.family", fonts[i]); else otherFont.forceActiveFocus() }
            }
        }
        SettingRow {
            title: qsTr("Another font")
            description: qsTr("Its exact name, as your system lists it.")
            Field {
                id: otherFont
                label: qsTr("Another font")
                implicitWidth: Math.round(240 * Theme.scale)
                placeholderText: "Comic Neue"
                text: fontChoice.fonts.indexOf(fontChoice.current) >= 0 ? "" : fontChoice.current
                onEditingFinished: if (text.trim() !== "") ed.set("font.family", text.trim())
            }
        }
        SettingRow {
            title: qsTr("Size")
            ValueSlider { label: qsTr("Text size"); from: 12; to: 160; suffix: " px"; value: ed.v("font.size", 44); onMoved: ed.set("font.size", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Weight")
            Segmented {
                label: qsTr("Weight")
                value: ed.v("font.weight", 700) >= 850 ? 900 : ed.v("font.weight", 700) >= 600 ? 700 : 400
                options: [{ value: 400, label: qsTr("Regular") }, { value: 700, label: qsTr("Bold") }, { value: 900, label: qsTr("Black") }]
                onActivated: (w) => ed.set("font.weight", w)
            }
        }
        SettingRow {
            title: qsTr("Letter spacing")
            ValueSlider { label: qsTr("Letter spacing"); from: -5; to: 30; suffix: "%"; value: ed.v("font.letterSpacing", 0); onMoved: ed.set("font.letterSpacing", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Line height")
            ValueSlider { label: qsTr("Line height"); from: 90; to: 200; stepSize: 5; suffix: "%"; value: ed.v("font.lineHeight", 1.25) * 100; onMoved: ed.set("font.lineHeight", Math.round(value) / 100) }
        }
        SettingRow {
            title: qsTr("Capital letters")
            Toggle { tip: qsTr("Capital letters"); checked: ed.v("font.uppercase", false); onToggled: ed.set("font.uppercase", checked) }
        }
        SettingRow {
            title: qsTr("Italic")
            Toggle { tip: qsTr("Italic"); checked: ed.v("font.italic", false); onToggled: ed.set("font.italic", checked) }
        }
    }

    // --- Colours ----------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        visible: ed.kind !== "avatar"
        title: qsTr("Colours")
        iconName: "palette"
        Repeater {
            model: [
                { key: "text", title: qsTr("Words"), fallback: "#ffffff", opacity: "" },
                { key: "ink", title: qsTr("Ink"), description: qsTr("Words being spoken, the indicator, neon glow."), fallback: "#b48cff", opacity: "" },
                { key: "unspoken", title: qsTr("Words not spoken yet"), fallback: "#ffffff", opacity: "unspokenOpacity" },
                { key: "background", title: qsTr("Background"), fallback: "#120d1f", opacity: "backgroundOpacity" },
                { key: "border", title: qsTr("Border"), fallback: "#ffffff", opacity: "borderOpacity" },
                { key: "outline", title: qsTr("Outline"), fallback: "#000000", opacity: "" },
                { key: "shadow", title: qsTr("Shadow"), fallback: "#000000", opacity: "" },
                { key: "name", title: qsTr("Names"), fallback: "#d9c6ff", opacity: "" }
            ]
            SettingRow {
                id: colourRow
                required property var modelData
                title: modelData.title
                description: modelData.description || ""
                RowLayout {
                    spacing: Theme.s3
                    ValueSlider {
                        visible: colourRow.modelData.opacity !== ""
                        label: qsTr("Opacity")
                        implicitWidth: Math.round(150 * Theme.scale)
                        from: 0; to: 100; suffix: "%"
                        value: ed.v("colors." + (colourRow.modelData.opacity || "x"), 100)
                        onMoved: ed.set("colors." + colourRow.modelData.opacity, Math.round(value))
                    }
                    ColorPick {
                        label: colourRow.modelData.title
                        value: ed.v("colors." + colourRow.modelData.key, colourRow.modelData.fallback)
                        onPicked: (c) => ed.set("colors." + colourRow.modelData.key, c)
                    }
                }
            }
        }
    }

    // --- Box and effects ------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        visible: ed.kind !== "avatar"
        title: qsTr("Box and effects")
        iconName: "square"
        SettingRow {
            title: qsTr("Width")
            description: qsTr("How much of the screen's width the text may use.")
            ValueSlider { label: qsTr("Width"); from: 20; to: 100; suffix: "%"; value: ed.v("box.maxWidth", 70); onMoved: ed.set("box.maxWidth", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Text alignment")
            Segmented {
                label: qsTr("Text alignment")
                value: ed.v("box.align", "center")
                options: [{ value: "left", label: qsTr("Left") }, { value: "center", label: qsTr("Centre") }, { value: "right", label: qsTr("Right") }]
                onActivated: (a) => ed.set("box.align", a)
            }
        }
        SettingRow {
            title: qsTr("Padding")
            ValueSlider { label: qsTr("Padding"); from: 0; to: 80; suffix: " px"; value: ed.v("box.padding", 18); onMoved: ed.set("box.padding", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Corner roundness")
            ValueSlider { label: qsTr("Corner roundness"); from: 0; to: 60; suffix: " px"; value: ed.v("box.radius", 14); onMoved: ed.set("box.radius", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Border")
            ValueSlider { label: qsTr("Border width"); from: 0; to: 12; suffix: " px"; value: ed.v("box.borderWidth", 0); onMoved: ed.set("box.borderWidth", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Box shadow")
            ValueSlider { label: qsTr("Box shadow"); from: 0; to: 100; suffix: "%"; value: ed.v("box.shadow", 30); onMoved: ed.set("box.shadow", Math.round(value)) }
        }
        SettingRow {
            visible: ed.kind === "captions"
            title: qsTr("Speech bubble tail")
            Segmented {
                label: qsTr("Speech bubble tail")
                value: ed.v("box.tail", "none")
                options: [{ value: "none", label: qsTr("None") }, { value: "left", label: qsTr("Left") },
                          { value: "center", label: qsTr("Centre") }, { value: "right", label: qsTr("Right") }]
                onActivated: (t) => ed.set("box.tail", t)
            }
        }
        SettingRow {
            title: qsTr("Text outline")
            description: qsTr("Keeps words readable on any background.")
            ValueSlider { label: qsTr("Text outline"); from: 0; to: 16; suffix: " px"; value: ed.v("effects.outline", 0); onMoved: ed.set("effects.outline", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Text shadow")
            ValueSlider { label: qsTr("Text shadow"); from: 0; to: 100; suffix: "%"; value: ed.v("effects.textShadow", 40); onMoved: ed.set("effects.textShadow", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Glow")
            ValueSlider { label: qsTr("Glow"); from: 0; to: 100; suffix: "%"; value: ed.v("effects.glow", 0); onMoved: ed.set("effects.glow", Math.round(value)) }
        }
    }

    // --- Position -------------------------------------------------------------------
    Card {
        id: positionCard
        Layout.fillWidth: true
        title: qsTr("Position")
        iconName: "move"
        readonly property string anchorKey: ed.kind === "avatar" ? "avatar.anchor" : "position.anchor"
        SettingRow {
            title: qsTr("Place on screen")
            AnchorPicker {
                label: qsTr("Place on screen")
                value: ed.v(positionCard.anchorKey, ed.kind === "captions" ? "bottom" : "bottom-left")
                onPicked: (a) => ed.set(positionCard.anchorKey, a)
            }
        }
        SettingRow {
            visible: ed.kind !== "avatar"
            title: qsTr("Move sideways")
            ValueSlider { label: qsTr("Move sideways"); from: -50; to: 50; suffix: "%"; value: ed.v("position.offsetX", 0); onMoved: ed.set("position.offsetX", Math.round(value)) }
        }
        SettingRow {
            visible: ed.kind !== "avatar"
            title: qsTr("Move up or down")
            ValueSlider { label: qsTr("Move up or down"); from: -50; to: 50; suffix: "%"; value: ed.v("position.offsetY", 0); onMoved: ed.set("position.offsetY", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Distance from the edge")
            ValueSlider { label: qsTr("Distance from the edge"); from: 0; to: 200; suffix: " px"; value: ed.v("position.margin", 48); onMoved: ed.set("position.margin", Math.round(value)) }
        }
    }

    // --- Motion and timing (captions) ----------------------------------------------------
    Card {
        Layout.fillWidth: true
        visible: ed.kind === "captions"
        title: qsTr("Motion and timing")
        iconName: "sparkles"
        SettingRow {
            title: qsTr("How words appear")
            Segmented {
                label: qsTr("How words appear")
                value: ed.v("timing.reveal", "audio")
                options: [{ value: "audio", label: qsTr("With the voice") }, { value: "estimate", label: qsTr("Steady pace") }, { value: "instant", label: qsTr("All at once") }]
                onActivated: (r) => ed.set("timing.reveal", r)
            }
        }
        SettingRow {
            visible: ed.v("timing.reveal", "audio") === "estimate"
            title: qsTr("Words per second")
            ValueSlider {
                label: qsTr("Words per second")
                from: 10; to: 80
                format: (x) => (x / 10).toLocaleString(Qt.locale(), "f", 1)
                value: ed.v("timing.wps", 2.6) * 10
                onMoved: ed.set("timing.wps", Math.round(value) / 10)
            }
        }
        Repeater {
            model: [
                { key: "enter", title: qsTr("Caption appears"), options: ["none", "fade", "rise", "pop", "slide"] },
                { key: "word", title: qsTr("Each word"), options: ["none", "fade", "rise", "pop", "ink", "glow", "bounce", "type"] },
                { key: "exit", title: qsTr("Caption leaves"), options: ["none", "fade", "sink", "shrink"] }
            ]
            SettingRow {
                id: motionRow
                required property var modelData
                title: modelData.title
                Choice {
                    label: motionRow.modelData.title
                    implicitWidth: Math.round(200 * Theme.scale)
                    textRole: "text"
                    readonly property var entries: motionRow.modelData.options.map(o => ({ value: o, text: ed.motionName(o) }))
                    model: entries
                    currentIndex: Math.max(0, motionRow.modelData.options.indexOf(ed.v("animation." + motionRow.modelData.key, "")))
                    onActivated: (i) => ed.set("animation." + motionRow.modelData.key, entries[i].value)
                }
            }
        }
        SettingRow {
            title: qsTr("Animation speed")
            ValueSlider { label: qsTr("Animation speed"); from: 25; to: 300; stepSize: 5; suffix: "%"; value: ed.v("animation.speed", 100); onMoved: ed.set("animation.speed", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Stay on screen")
            description: qsTr("After you finish speaking.")
            ValueSlider {
                label: qsTr("Stay on screen")
                from: 0; to: 61
                format: (x) => x > 60 ? qsTr("Until the next") : qsTr("%1 s", "seconds").arg(Number(Math.round(x)).toLocaleString(Qt.locale(), "f", 0))
                value: ed.v("timing.hold", 4) < 0 ? 61 : ed.v("timing.hold", 4)
                onMoved: ed.set("timing.hold", value > 60 ? -1 : Math.round(value))
            }
        }
        SettingRow {
            title: qsTr("Lines of history")
            description: qsTr("Earlier captions stay above the newest one.")
            ValueSlider { label: qsTr("Lines of history"); from: 1; to: 6; value: ed.v("history.lines", 1); onMoved: ed.set("history.lines", Math.round(value)) }
        }
        SettingRow {
            visible: ed.v("history.lines", 1) > 1
            title: qsTr("Roll upwards")
            Toggle { tip: qsTr("Roll upwards"); checked: ed.v("history.roll", false); onToggled: ed.set("history.roll", checked) }
        }
        SettingRow {
            title: qsTr("Show the voice's name")
            Toggle { tip: qsTr("Show the voice's name"); checked: ed.v("name.show", false); onToggled: ed.set("name.show", checked) }
        }
        SettingRow {
            visible: ed.v("name.show", false)
            title: qsTr("Name to show")
            description: qsTr("Empty uses the voice's own name.")
            Field {
                label: qsTr("Name to show")
                implicitWidth: Math.round(200 * Theme.scale)
                text: ed.v("name.text", "")
                onEditingFinished: ed.set("name.text", text.trim())
            }
        }
        SettingRow {
            title: qsTr("Speaking indicator")
            Segmented {
                label: qsTr("Speaking indicator")
                value: ed.v("indicator.show", false) ? ed.v("indicator.style", "dot") : "off"
                options: [{ value: "off", label: qsTr("Off") }, { value: "dot", label: qsTr("Dot") },
                          { value: "bars", label: qsTr("Bars") }, { value: "wave", label: qsTr("Wave") }]
                onActivated: (x) => {
                    ed.set("indicator.show", x !== "off")
                    if (x !== "off") ed.set("indicator.style", x)
                }
            }
        }
    }
    function motionName(id) {
        switch (id) {
        case "none": return qsTr("No animation")
        case "fade": return qsTr("Fade")
        case "rise": return qsTr("Rise")
        case "pop": return qsTr("Pop")
        case "slide": return qsTr("Slide in")
        case "ink": return qsTr("Fill with ink")
        case "glow": return qsTr("Glow")
        case "bounce": return qsTr("Bounce")
        case "type": return qsTr("Type out")
        case "sink": return qsTr("Sink")
        case "shrink": return qsTr("Shrink")
        }
        return id
    }

    // --- Chat ------------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        visible: ed.kind === "chat"
        title: qsTr("Chat")
        subtitle: qsTr("Shows the Twitch messages Vocal Ink reads aloud (only those that pass your chat filters).")
        iconName: "message-circle"
        SettingRow {
            title: qsTr("Messages on screen")
            ValueSlider { label: qsTr("Messages on screen"); from: 1; to: 20; value: ed.v("chat.maxMessages", 5); onMoved: ed.set("chat.maxMessages", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Fade out after")
            ValueSlider {
                label: qsTr("Fade out after")
                from: 0; to: 300; stepSize: 5
                format: (x) => x === 0 ? qsTr("Never") : qsTr("%1 s", "seconds").arg(Number(Math.round(x)).toLocaleString(Qt.locale(), "f", 0))
                value: ed.v("chat.fadeAfter", 30)
                onMoved: ed.set("chat.fadeAfter", Math.round(value))
            }
        }
        SettingRow {
            title: qsTr("New messages")
            Segmented {
                label: qsTr("New messages")
                value: ed.v("chat.direction", "up")
                options: [{ value: "up", label: qsTr("At the bottom") }, { value: "down", label: qsTr("At the top") }]
                onActivated: (d) => ed.set("chat.direction", d)
            }
        }
        SettingRow {
            title: qsTr("Chatters' own colours")
            Toggle { tip: qsTr("Chatters' own colours"); checked: ed.v("chat.useNameColors", true); onToggled: ed.set("chat.useNameColors", checked) }
        }
        SettingRow {
            title: qsTr("Badges")
            description: qsTr("Mod, VIP, subscriber and broadcaster.")
            Toggle { tip: qsTr("Badges"); checked: ed.v("chat.showBadges", true); onToggled: ed.set("chat.showBadges", checked) }
        }
        SettingRow {
            title: qsTr("Highlight the message being read")
            Toggle { tip: qsTr("Highlight the message being read"); checked: ed.v("chat.highlightReading", true); onToggled: ed.set("chat.highlightReading", checked) }
        }
    }

    // --- PNGtuber ----------------------------------------------------------------------
    Card {
        id: pngCard
        Layout.fillWidth: true
        visible: ed.kind === "avatar"
        title: qsTr("Pictures")
        subtitle: qsTr("PNG, GIF, WebP or JPEG, up to 10 MB. Transparent PNGs look best.")
        iconName: "image"
        property string picking: ""

        Flow {
            Layout.fillWidth: true
            spacing: Theme.s3
            Repeater {
                model: [
                    { key: "idle", title: qsTr("Mouth closed") },
                    { key: "talking", title: qsTr("Mouth open") },
                    { key: "blink", title: qsTr("Blinking") },
                    { key: "talkingBlink", title: qsTr("Blinking, mouth open") },
                    { key: "micLive", title: qsTr("Real mic live") }
                ]
                ColumnLayout {
                    id: slotCol
                    required property var modelData
                    readonly property string asset: ed.v("avatar.images." + modelData.key, "")
                    spacing: Theme.s1
                    Rectangle {
                        Layout.preferredWidth: Math.round(116 * Theme.scale)
                        Layout.preferredHeight: Layout.preferredWidth
                        radius: Theme.radius
                        color: Theme.sunken
                        border.color: Theme.line
                        border.width: Theme.hairline
                        Image {
                            anchors.fill: parent
                            anchors.margins: Theme.s2
                            fillMode: Image.PreserveAspectFit
                            source: slotCol.asset !== "" ? App.avatarAssetUrl(slotCol.asset) : ""
                            sourceSize: Qt.size(232, 232)
                        }
                        Icon {
                            anchors.centerIn: parent
                            visible: slotCol.asset === ""
                            name: "image-plus"
                            color: Theme.faint
                        }
                    }
                    Txt { text: slotCol.modelData.title; role: "caption"; Layout.maximumWidth: Math.round(116 * Theme.scale); elide: Text.ElideRight; wrapMode: Text.NoWrap }
                    RowLayout {
                        spacing: 2
                        PillButton {
                            small: true
                            text: slotCol.asset === "" ? qsTr("Choose…") : qsTr("Change…")
                            onClicked: { pngCard.picking = slotCol.modelData.key; imageDialog.open() }
                        }
                        IconButton {
                            visible: slotCol.asset !== ""
                            small: true
                            iconName: "x"
                            tip: qsTr("Remove this picture")
                            onClicked: ed.set("avatar.images." + slotCol.modelData.key, undefined)
                        }
                    }
                }
            }
        }
        FileDialog {
            id: imageDialog
            title: qsTr("Choose a picture")
            nameFilters: [qsTr("Pictures (*.png *.gif *.webp *.jpg *.jpeg)")]
            onAccepted: {
                const id = App.importAvatarImage(selectedFile)
                if (id !== "")
                    ed.set("avatar.images." + pngCard.picking, id)
            }
        }
    }
    Card {
        Layout.fillWidth: true
        visible: ed.kind === "avatar"
        title: qsTr("Movement")
        iconName: "smile"
        SettingRow {
            title: qsTr("When talking")
            Segmented {
                label: qsTr("When talking")
                value: ed.v("avatar.motion", "bounce")
                options: [{ value: "none", label: qsTr("Still") }, { value: "bounce", label: qsTr("Bounce") }, { value: "squash", label: qsTr("Squash") },
                          { value: "shake", label: qsTr("Shake") }, { value: "float", label: qsTr("Float") }]
                onActivated: (m) => ed.set("avatar.motion", m)
            }
        }
        SettingRow {
            title: qsTr("How much")
            ValueSlider { label: qsTr("How much"); from: 0; to: 100; suffix: "%"; value: ed.v("avatar.intensity", 60); onMoved: ed.set("avatar.intensity", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Only while talking")
            description: qsTr("Otherwise it also moves gently when quiet.")
            Toggle { tip: qsTr("Only while talking"); checked: ed.v("avatar.motionOnlyWhileTalking", true); onToggled: ed.set("avatar.motionOnlyWhileTalking", checked) }
        }
        SettingRow {
            title: qsTr("Mouth opens at")
            description: qsTr("How loud the voice must be before the mouth-open picture shows.")
            ValueSlider { label: qsTr("Mouth opens at"); from: 1; to: 50; suffix: "%"; value: ed.v("avatar.threshold", 8); onMoved: ed.set("avatar.threshold", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Blink every")
            ValueSlider {
                label: qsTr("Blink every")
                from: 0; to: 20
                format: (x) => x === 0 ? qsTr("Never") : qsTr("%1 s", "seconds").arg(Number(Math.round(x)).toLocaleString(Qt.locale(), "f", 0))
                value: ed.v("avatar.blinkEvery", 4)
                onMoved: ed.set("avatar.blinkEvery", Math.round(value))
            }
        }
        SettingRow {
            title: qsTr("Size")
            ValueSlider { label: qsTr("Size"); from: 10; to: 100; suffix: "%"; value: ed.v("avatar.size", 60); onMoved: ed.set("avatar.size", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Mirror")
            Toggle { tip: qsTr("Mirror"); checked: ed.v("avatar.flip", false); onToggled: ed.set("avatar.flip", checked) }
        }
        SettingRow {
            title: qsTr("Shadow")
            ValueSlider { label: qsTr("Shadow"); from: 0; to: 100; suffix: "%"; value: ed.v("avatar.shadow", 0); onMoved: ed.set("avatar.shadow", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Darker when quiet")
            ValueSlider { label: qsTr("Darker when quiet"); from: 0; to: 100; suffix: "%"; value: ed.v("avatar.dimWhenIdle", 0); onMoved: ed.set("avatar.dimWhenIdle", Math.round(value)) }
        }
        SettingRow {
            title: qsTr("Glow while the real mic is live")
            Toggle { tip: qsTr("Glow while the real mic is live"); checked: ed.v("avatar.micLiveGlow", true); onToggled: ed.set("avatar.micLiveGlow", checked) }
        }
    }

    // --- Custom CSS ---------------------------------------------------------------------
    Card {
        Layout.fillWidth: true
        title: qsTr("Custom CSS")
        subtitle: qsTr("For fine control. Selectors like .vi-word or .vi-chat-message; see the overlay guide. Nothing outside Vocal Ink can be loaded.")
        iconName: "square-pen"
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(120 * Theme.scale)
            radius: Theme.radius
            color: Theme.sunken
            border.color: css.activeFocus ? Theme.accent : Theme.line
            border.width: css.activeFocus ? 2 : Theme.hairline
            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.s2
                TextArea {
                    id: css
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fsSm
                    color: Theme.text
                    wrapMode: TextEdit.Wrap
                    placeholderText: ".vi-word.spoken { color: gold; }"
                    placeholderTextColor: Theme.faint
                    text: ed.v("customCss", "")
                    background: null
                    Accessible.name: qsTr("Custom CSS")
                    onActiveFocusChanged: if (!activeFocus && text !== ed.v("customCss", "")) ed.set("customCss", text)
                }
            }
        }
        RowLayout {
            PillButton { small: true; text: qsTr("Apply"); iconName: "check"; onClicked: ed.set("customCss", css.text) }
            PillButton { small: true; kind: "ghost"; iconName: "book-open"; text: qsTr("Overlay guide"); onClicked: App.openUrl("https://github.com/Vocal-Ink/Desktop-app/blob/main/docs/overlay-style.md") }
        }
    }
}
