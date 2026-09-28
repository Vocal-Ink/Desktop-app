import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Where messages are written. Enter speaks, Shift+Enter adds a line,
// Up/Down bring back earlier messages, Tab takes the first suggestion.
ColumnLayout {
    id: root
    property alias text: area.text
    property bool compact: false
    readonly property alias editor: area
    signal spoke(string text)

    spacing: Theme.s2

    function focusEditor() { area.forceActiveFocus() }
    function insert(t) {
        if (!t)
            return
        const before = area.text.length && !/\s$/.test(area.text) ? " " : ""
        area.insert(area.length, before + t)
        area.cursorPosition = area.length
        area.forceActiveFocus()
    }

    property var recall: []
    property int recallIndex: -1
    property var suggestions: []

    function refreshSuggestions() {
        suggestions = area.text.length === 0 ? [] : App.suggest(area.text.substring(0, area.cursorPosition))
    }
    function takeSuggestion(i) {
        if (i < 0 || i >= suggestions.length)
            return false
        const before = area.text.substring(0, area.cursorPosition)
        const after = area.text.substring(area.cursorPosition)
        const updated = App.applySuggestion(before, suggestions[i])
        area.text = updated + after
        area.cursorPosition = updated.length
        return true
    }

    function send() {
        const t = area.text.trim()
        if (t === "")
            return
        if (App.prefs["a11y/confirmSpeak"]) {
            confirm.pending = t
            confirm.open()
            return
        }
        commit(t)
    }
    function commit(t) {
        App.speak(t)
        root.spoke(t)
        recallIndex = -1
        if (App.prefs["ui/clearAfterSpeak"] !== false)
            area.clear()
        area.forceActiveFocus()
    }

    // Typing echo: say words or sentences back to you (your speakers only).
    property int echoedUpTo: 0
    function echoTyping() {
        const mode = App.prefs["a11y/echoTyping"]
        if (!mode || mode === "off")
            return
        const t = area.text
        if (t.length < echoedUpTo)
            echoedUpTo = 0
        const last = t.charAt(t.length - 1)
        if (mode === "words" && /[\s.,!?]/.test(last)) {
            const w = t.substring(echoedUpTo).trim().split(/\s+/).pop()
            if (w) App.echo(w)
            echoedUpTo = t.length
        } else if (mode === "sentences" && /[.!?]/.test(last)) {
            App.echo(t.substring(echoedUpTo).trim())
            echoedUpTo = t.length
        }
    }

    // --- Suggestions ------------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        spacing: Theme.s2
        visible: root.suggestions.length > 0 && area.activeFocus
        Repeater {
            model: root.suggestions
            Chip {
                required property string modelData
                required property int index
                text: modelData
                shortcut: index < 5 ? "Alt+" + (index + 1) : ""
                onClicked: { root.takeSuggestion(index); area.forceActiveFocus() }
            }
        }
    }

    // --- The paper -----------------------------------------------------------------
    Rectangle {
        id: paper
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight + Theme.s3 * 2
        radius: Theme.radiusLg
        color: Theme.raised
        border.width: area.activeFocus ? 2 : Theme.hairline
        border.color: App.listening ? Theme.accent : area.activeFocus ? Theme.accent : Theme.line
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }

        ColumnLayout {
            id: col
            anchors.fill: parent
            anchors.margins: Theme.s3
            anchors.leftMargin: Theme.s4
            spacing: Theme.s2

            Flickable {
                id: flick
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(area.implicitHeight, area.font.pixelSize * Theme.lineHeight * 1.45 * (root.compact ? 3 : 5) + 12)
                contentHeight: area.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: InkScrollBar {}

                TextArea.flickable: TextArea {
                    id: area
                    wrapMode: TextEdit.Wrap
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.composerSize
                    font.letterSpacing: Theme.tracking(Theme.composerSize)
                    color: Theme.text
                    selectionColor: Theme.accentWashStrong
                    selectedTextColor: Theme.text
                    placeholderTextColor: App.listening ? Theme.accentText : Theme.faint
                    placeholderText: App.transcribing ? qsTr("Writing down what you said…")
                                   : App.listening ? qsTr("Listening… speak now")
                                   : qsTr("Type what you want to say…")
                    padding: 0
                    topPadding: 4
                    bottomPadding: 4
                    background: null
                    focus: true
                    persistentSelection: false
                    Accessible.name: qsTr("Message")
                    Accessible.description: qsTr("Enter speaks. Shift+Enter adds a line.")

                    onTextChanged: { root.refreshSuggestions(); root.echoTyping() }
                    onCursorPositionChanged: root.refreshSuggestions()

                    Keys.onPressed: (event) => {
                        const plain = event.modifiers === Qt.NoModifier || event.modifiers === Qt.KeypadModifier
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && plain) {
                            root.send()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            if (App.listening) App.cancelListening()
                            else App.stop()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Tab && plain && root.suggestions.length > 0) {
                            root.takeSuggestion(0)
                            event.accepted = true
                        } else if ((event.modifiers & Qt.AltModifier) && event.key >= Qt.Key_1 && event.key <= Qt.Key_5) {
                            event.accepted = root.takeSuggestion(event.key - Qt.Key_1)
                        } else if (event.key === Qt.Key_Up && plain && (area.text === "" || root.recallIndex >= 0)) {
                            if (root.recallIndex < 0)
                                root.recall = App.recentTexts()
                            if (root.recallIndex + 1 < root.recall.length) {
                                root.recallIndex++
                                area.text = root.recall[root.recallIndex]
                                area.cursorPosition = area.length
                            }
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down && plain && root.recallIndex >= 0) {
                            root.recallIndex--
                            area.text = root.recallIndex >= 0 ? root.recall[root.recallIndex] : ""
                            area.cursorPosition = area.length
                            event.accepted = true
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2

                // The signature line: your voice, in ink.
                InkWave {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(40 * Math.min(1.3, Theme.scale))
                    visible: App.prefs["ui/waveStyle"] !== "off"
                    style: App.prefs["ui/waveStyle"] || "ink"
                    running: App.speaking || App.listening || App.micLive
                    level: App.listening ? App.micLevel : App.micLive && !App.speaking ? App.micLiveLevel : App.outputLevel
                    color: App.micLive && !App.speaking && !App.listening ? Theme.live : Theme.accent
                    sheen: App.micLive && !App.speaking && !App.listening ? Theme.live : Theme.sheen
                    thickness: height * 0.42
                    animated: !Theme.reducedMotion
                }
                Item { Layout.fillWidth: true; visible: App.prefs["ui/waveStyle"] === "off" }

                Txt {
                    visible: area.length > 0 && !root.compact
                    text: area.length
                    role: "caption"
                    color: Theme.faint
                    font.family: Theme.monoFont
                    Accessible.ignored: true
                }
                MicButton {}
                PillButton {
                    kind: "primary"
                    text: root.compact ? "" : qsTr("Speak")
                    iconName: "send-horizontal"
                    shortcut: root.compact ? "" : "Return"
                    enabled: area.text.trim().length > 0
                    tip: qsTr("Speak this message")
                    onClicked: root.send()
                }
            }
        }
    }

    Sheet {
        id: confirm
        property string pending: ""
        title: qsTr("Say this?")
        iconName: "megaphone"
        Txt {
            text: confirm.pending
            role: "lead"
            Layout.fillWidth: true
        }
        footer: [
            PillButton { text: qsTr("Speak"); kind: "primary"; iconName: "send-horizontal"; onClicked: { confirm.close(); root.commit(confirm.pending) } },
            PillButton { text: qsTr("Keep editing"); kind: "ghost"; onClicked: confirm.close() }
        ]
        onOpened: contentItem.forceActiveFocus()
    }
}
