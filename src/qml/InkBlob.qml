import QtQuick
import Ink.Core

// Vocal Ink's little ink drop, talking with the voice. It stands in for your
// avatar in the app's previews and is the built-in PNGtuber's default look.
// Sizes are animated rather than scaled (crisper in the software renderer).
Item {
    id: blob
    property real mouth: 0          // 0..1
    property string viseme: ""      // "" | A | I | U | E | O
    property bool micLive: false
    property color ink: Theme.accent
    property bool animate: !Theme.reducedMotion

    implicitWidth: Math.round(150 * Theme.scale)
    implicitHeight: Math.round(160 * Theme.scale)
    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("Mouth preview")

    readonly property real m: Math.max(0, Math.min(1, mouth))
    readonly property color face: "#140E24"
    // Mouth width and height per shape, as fractions of the body width.
    readonly property var shape: ({
        "A": [0.34, 0.24], "I": [0.42, 0.1], "U": [0.16, 0.16],
        "E": [0.38, 0.14], "O": [0.22, 0.22], "": [0.3, 0.2]
    })
    readonly property var sh: shape[viseme] || shape[""]

    // The live mic glow sits behind everything.
    Rectangle {
        anchors.centerIn: body
        width: body.width + Math.round(18 * Theme.scale)
        height: body.height + Math.round(18 * Theme.scale)
        radius: width * 0.47
        color: "transparent"
        border.width: Math.round(4 * Theme.scale)
        border.color: Theme.live
        opacity: blob.micLive ? 0.9 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    }

    Rectangle {
        id: body
        width: blob.width * (0.9 - 0.035 * blob.m)
        height: blob.height * (0.84 + 0.06 * blob.m)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: blob.height * 0.02
        radius: width * 0.46
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.lighter(blob.ink, 1.3) }
            GradientStop { position: 0.55; color: blob.ink }
            GradientStop { position: 1; color: Qt.darker(blob.ink, 1.35) }
        }
        Behavior on height { enabled: blob.animate; NumberAnimation { duration: 70 } }
    }
    // The drop's tip.
    Rectangle {
        width: body.width * 0.24
        height: width
        radius: width * 0.3
        rotation: 45
        x: body.x + body.width / 2 - width / 2
        y: body.y - height * 0.32
        color: Qt.lighter(blob.ink, 1.3)
    }
    // A highlight, like wet ink.
    Rectangle {
        x: body.x + body.width * 0.2
        y: body.y + body.height * 0.13
        width: body.width * 0.17
        height: width * 0.55
        radius: height / 2
        rotation: -28
        color: Qt.rgba(1, 1, 1, 0.3)
    }

    // Eyes (they blink now and then).
    property bool blinking: false
    Timer {
        running: blob.animate && blob.visible
        repeat: true
        interval: 3400
        onTriggered: { blob.blinking = true; unblink.restart(); interval = 2600 + Math.random() * 2600 }
    }
    Timer { id: unblink; interval: 130; onTriggered: blob.blinking = false }
    Row {
        anchors.horizontalCenter: body.horizontalCenter
        y: body.y + body.height * 0.34
        spacing: body.width * 0.2
        Repeater {
            model: 2
            Rectangle {
                width: body.width * 0.085
                height: blob.blinking ? Math.max(2, width * 0.25) : width * 1.45
                anchors.verticalCenter: parent.verticalCenter
                radius: width / 2
                color: blob.face
            }
        }
    }

    // Mouth.
    Rectangle {
        id: mouthShape
        anchors.horizontalCenter: body.horizontalCenter
        y: body.y + body.height * 0.6 - height / 2
        width: body.width * (0.16 + (blob.sh[0] - 0.16) * Math.min(1, blob.m * 1.6))
        height: Math.max(Math.round(3 * Theme.scale), body.width * (0.03 + blob.sh[1] * blob.m))
        radius: Math.min(width, height) / 2
        color: blob.face
        clip: true
        Behavior on width { enabled: blob.animate; NumberAnimation { duration: 60 } }
        Behavior on height { enabled: blob.animate; NumberAnimation { duration: 60 } }
        // Tongue, when the mouth is open enough to see it.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: parent.width * 0.6
            height: parent.height * 0.42
            radius: height / 2
            color: Qt.darker(blob.ink, 1.1)
            opacity: blob.m > 0.35 ? 0.9 : 0
        }
    }
}
