import QtQuick
import QtQuick.Controls.Basic
import Ink.Core

// Dictation button. Push-to-talk holds; toggle and hands-free modes click.
AbstractButton {
    id: root
    readonly property string mode: App.prefs["stt/mode"] || "ptt"
    readonly property bool holdMode: mode === "ptt" && !App.prefs["a11y/latchPtt"]
    readonly property bool active: App.listening

    implicitWidth: Theme.control
    implicitHeight: Theme.control
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: active ? qsTr("Stop listening") : holdMode ? qsTr("Hold to dictate") : qsTr("Dictate")
    Accessible.description: App.sttReady ? "" : App.sttStatus

    onPressed: if (holdMode) App.startListening()
    onReleased: if (holdMode) App.stopListening()
    onCanceled: if (holdMode) App.stopListening()
    onClicked: if (!holdMode) App.toggleListening()
    Keys.onSpacePressed: (event) => { if (holdMode && !event.isAutoRepeat) App.startListening(); event.accepted = true }
    Keys.onReleased: (event) => { if (holdMode && event.key === Qt.Key_Space && !event.isAutoRepeat) { App.stopListening(); event.accepted = true } }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: App.transcribing ? "loader" : "mic"
            size: Math.round(20 * Theme.scale)
            color: root.active ? "#FFFFFF" : root.hovered ? Theme.text : Theme.muted
            RotationAnimation on rotation {
                running: App.transcribing && Theme.motionOn
                loops: Animation.Infinite
                from: 0; to: 360; duration: 1100
            }
        }
    }
    background: Rectangle {
        radius: Theme.corners === "sharp" ? 4 : width / 2
        color: root.active ? Theme.accent : root.down ? Theme.pressed : root.hovered ? Theme.hover : "transparent"
        border.color: root.active ? Theme.accent : Theme.line
        border.width: Theme.hairline

        // The ring swells with your voice while listening.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 6 + App.micLevel * 22
            height: width
            radius: width / 2
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            opacity: root.active ? 0.55 : 0
            visible: opacity > 0
            Behavior on width { NumberAnimation { duration: 80 } }
            Behavior on opacity { NumberAnimation { duration: Theme.fast } }
        }
        FocusFrame { shown: root.visualFocus; baseRadius: parent.radius }
    }

    InkTip {
        visible: root.hovered
        text: !App.sttReady ? App.sttStatus
            : root.holdMode ? qsTr("Hold to dictate (%1)").arg(App.shortcutFor("listen.ptt") || qsTr("no shortcut"))
            : root.active ? qsTr("Stop listening") : qsTr("Dictate")
    }
}
