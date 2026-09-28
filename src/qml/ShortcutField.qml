import QtQuick
import QtQuick.Controls.Basic
import Ink.Core

// Shows a shortcut; click (or Enter) and press keys to record a new one.
// Esc cancels, Backspace/Delete clears. Global shortcuts are paused while
// recording so the keys you press don't trigger actions.
AbstractButton {
    id: root
    property string sequence: ""
    property string label: ""
    readonly property bool recording: activeFocus && armed
    property bool armed: false
    property string pending: ""
    signal recorded(string sequence)
    signal cleared()

    implicitHeight: Theme.controlSm
    implicitWidth: Math.max(Math.round(150 * Theme.scale), content.implicitWidth + Theme.s3 * 2)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: label + ": " + (sequence === "" ? qsTr("not set") : App.nativeShortcut(sequence))
    Accessible.description: qsTr("Press Enter, then the new keys. Escape cancels, Backspace clears.")

    onClicked: { armed = true; forceActiveFocus() }
    onActiveFocusChanged: if (!activeFocus) stopRecording()
    onRecordingChanged: App.suspendHotkeys(recording)

    function stopRecording() { armed = false; pending = "" }

    Keys.onPressed: (event) => {
        if (!armed) {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                armed = true
                event.accepted = true
            }
            return
        }
        event.accepted = true
        if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) {
            stopRecording()
            return
        }
        if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) && event.modifiers === Qt.NoModifier) {
            stopRecording()
            root.cleared()
            return
        }
        const seq = App.sequenceFromKey(event.key, event.modifiers)
        if (seq === "") {
            // Only modifiers so far: show them as a hint.
            pending = App.sequenceFromKey(Qt.Key_A, event.modifiers).replace(/\+A$/, "+…")
            return
        }
        stopRecording()
        root.recorded(seq)
    }

    contentItem: Item {
        Row {
            id: content
            anchors.centerIn: parent
            spacing: Theme.s2
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.recording
                name: "keyboard"
                size: Math.round(15 * Theme.scale)
                color: Theme.accentText
                SequentialAnimation on opacity {
                    running: root.recording && Theme.motionOn
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 520 }
                    NumberAnimation { to: 1; duration: 520 }
                }
            }
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.recording
                text: root.pending !== "" ? root.pending : qsTr("Press keys…")
                role: "caption"
                color: Theme.accentText
            }
            KeyCombo {
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.recording
                sequence: root.sequence
                placeholder: qsTr("Not set")
            }
        }
    }
    background: Rectangle {
        radius: Theme.radius
        color: root.recording ? Theme.accentWash : root.hovered ? Theme.hover : Theme.sunken
        border.width: root.recording ? 2 : Theme.hairline
        border.color: root.recording ? Theme.accent : Theme.line
        FocusFrame { shown: root.visualFocus && !root.recording; baseRadius: parent.radius }
    }
}
