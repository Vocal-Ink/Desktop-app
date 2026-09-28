import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import Ink.Core

// Ctrl+K: find any page, setting, action, phrase or voice and run it.
Popup {
    id: pal
    signal navigate(string page)
    signal runOnboarding()

    parent: T.Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)
    width: Math.min(Math.round(640 * Math.min(1.3, Theme.scale)), parent.width - Theme.s6 * 2)
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    T.Overlay.modal: Rectangle { color: Theme.scrim }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        NumberAnimation { property: "y"; from: pal.parent.height * 0.12 - Theme.travel(8); to: pal.parent.height * 0.12; duration: Theme.normal; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.raised
        border.color: Theme.line
        border.width: Theme.hairline
    }

    readonly property var pages: [
        { kind: "page", id: "talk", title: qsTr("Go to Talk"), icon: "message-square-text", detail: "" },
        { kind: "page", id: "board", title: qsTr("Go to Board"), icon: "layout-grid", detail: qsTr("Phrases and sounds") },
        { kind: "page", id: "voices", title: qsTr("Go to Voices"), icon: "audio-lines", detail: "" },
        { kind: "page", id: "audio", title: qsTr("Go to Audio & mic"), icon: "cable", detail: qsTr("Virtual mic, output, real microphone") },
        { kind: "page", id: "stream", title: qsTr("Go to Stream"), icon: "radio", detail: qsTr("Captions, OBS, Twitch") },
        { kind: "page", id: "settings:appearance", title: qsTr("Appearance settings"), icon: "palette", detail: qsTr("Theme, ink colour, fonts, sizes") },
        // "Language" stays findable in English whatever the interface language is.
        { kind: "page", id: "settings:appearance", title: qsTr("Change language"), icon: "languages", detail: "Language" },
        { kind: "page", id: "settings:access", title: qsTr("Accessibility settings"), icon: "accessibility", detail: qsTr("Focus ring, bigger buttons, switch access") },
        { kind: "page", id: "settings:shortcuts", title: qsTr("Shortcut settings"), icon: "keyboard", detail: "" },
        { kind: "page", id: "settings:speech", title: qsTr("Speech input settings"), icon: "mic-vocal", detail: qsTr("Dictation models and languages") },
        { kind: "page", id: "settings:typing", title: qsTr("Typing & text settings"), icon: "type", detail: qsTr("Suggestions, abbreviations, variables") },
        { kind: "page", id: "settings:providers", title: qsTr("Voice provider keys"), icon: "globe", detail: "Azure, ElevenLabs, Fish Audio, OpenAI" },
        { kind: "page", id: "settings:backup", title: qsTr("Backup & updates"), icon: "file-down", detail: "" },
        { kind: "setup", id: "setup", title: qsTr("Run the setup again"), icon: "wand-sparkles", detail: "" }
    ]

    property var results: []
    property int current: 0

    function score(text, q) {
        text = text.toLowerCase()
        if (text.startsWith(q)) return 3
        if (text.indexOf(" " + q) >= 0) return 2
        return text.indexOf(q) >= 0 ? 1 : 0
    }
    function refresh() {
        const q = search.text.trim().toLowerCase()
        const out = []
        const add = (item, hay) => {
            const s = q === "" ? 1 : score(hay, q)
            if (s > 0) out.push(Object.assign({ score: s, order: out.length }, item))
        }
        pages.forEach(p => add(p, p.title + " " + p.detail))
        App.commands().forEach(c => add({ kind: "action", id: c.id, title: c.title, detail: c.detail, icon: c.id.startsWith("phrase:") ? "message-square-text" : "zap", shortcut: c.shortcut, hold: c.hold }, c.title + " " + c.detail + " " + c.category))
        if (q.length >= 2 && App.language) {
            for (const l of App.language.available)
                add({ kind: "language", id: l.code, title: l.name, detail: l.english, icon: "languages" }, l.name + " " + l.english + " " + l.code)
        }
        if (q.length >= 2) {
            for (let i = 0; i < App.voices.count && out.length < 200; ++i) {
                const v = App.voices.get(i)
                add({ kind: "voice", id: v.key, title: qsTr("Use voice: %1").arg(v.name), detail: v.provider + " · " + v.languageLabel, icon: "audio-lines" }, v.name + " " + v.languageLabel + " " + v.provider)
            }
        }
        out.sort((a, b) => b.score - a.score || a.order - b.order)
        results = out.slice(0, 60)
        current = 0
    }
    function run(item) {
        if (!item)
            return
        close()
        if (item.kind === "page") pal.navigate(item.id)
        else if (item.kind === "setup") pal.runOnboarding()
        else if (item.kind === "voice") App.setVoice(item.id)
        else if (item.kind === "language") App.language.choose(item.id)
        else App.trigger(item.id)
    }

    onOpened: { search.text = ""; refresh(); search.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s3
            spacing: Theme.s2
            Icon { name: "search"; color: Theme.muted }
            TextField {
                id: search
                Layout.fillWidth: true
                background: null
                font.family: Theme.uiFont
                font.pixelSize: Theme.fsLg
                color: Theme.text
                placeholderText: qsTr("Search pages, settings, actions, phrases, voices…")
                placeholderTextColor: Theme.faint
                Accessible.name: qsTr("Search commands")
                onTextChanged: pal.refresh()
                Keys.onDownPressed: { pal.current = Math.min(pal.results.length - 1, pal.current + 1); list.positionViewAtIndex(pal.current, ListView.Contain) }
                Keys.onUpPressed: { pal.current = Math.max(0, pal.current - 1); list.positionViewAtIndex(pal.current, ListView.Contain) }
                Keys.onReturnPressed: pal.run(pal.results[pal.current])
                Keys.onEnterPressed: pal.run(pal.results[pal.current])
            }
            KeyCombo { sequence: "Esc" }
        }
        Rectangle { Layout.fillWidth: true; height: Theme.hairline; color: Theme.line }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Math.round(420 * Theme.scale))
            Layout.margins: Theme.s2
            clip: true
            model: pal.results
            currentIndex: pal.current
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: InkScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Results")
            delegate: ItemDelegate {
                id: d
                required property var modelData
                required property int index
                width: ListView.view.width - 8
                height: Math.round(Theme.control * 1.15)
                highlighted: index === pal.current
                hoverEnabled: true
                onHoveredChanged: if (hovered) pal.current = index
                onClicked: pal.run(modelData)
                Accessible.name: modelData.title
                contentItem: RowLayout {
                    spacing: Theme.s3
                    Icon { name: d.modelData.icon || "zap"; color: d.highlighted ? Theme.accentText : Theme.muted }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Txt { text: d.modelData.title; role: "label"; wrapMode: Text.NoWrap; elide: Text.ElideRight; Layout.fillWidth: true }
                        Txt { visible: !!d.modelData.detail; text: d.modelData.detail || ""; role: "caption"; wrapMode: Text.NoWrap; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    KeyCombo { visible: !!d.modelData.shortcut; sequence: d.modelData.shortcut || ""; compact: true }
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: d.highlighted ? Theme.accentWash : "transparent"
                }
            }
            Txt {
                visible: pal.results.length === 0
                anchors.centerIn: parent
                text: qsTr("Nothing matches “%1”.").arg(search.text)
                color: Theme.muted
            }
        }
    }
}
