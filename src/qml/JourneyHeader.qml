import QtQuick
import QtQuick.Controls.Basic
import Ink.Core

// The setup as one ink stroke: every step is a node on it. The stretch you've
// travelled is written in ink, the road ahead is dotted, and the stroke pans
// so the step you're on stays in view.
//
// One Tab stop: ←/→ move between the steps you've already reached, Enter or
// Space goes there.
FocusScope {
    id: jh
    property var labels: []    // one per step, in order
    property int current: 0    // the step on screen
    property int reached: 0    // furthest step visited so far
    property real position: current // fractional while travelling between steps
    signal jump(int index)

    readonly property int count: labels.length
    readonly property real s: Math.min(1.5, Theme.scale)
    readonly property real pad: Math.round(64 * s)
    readonly property real minGap: Math.round(118 * s)
    readonly property real gap: Math.max(minGap, (width - pad * 2) / Math.max(1, count - 1))
    readonly property real trackWidth: pad * 2 + gap * Math.max(0, count - 1)
    readonly property real lineY: Math.round(30 * s)
    function nodeX(i) { return pad + gap * i }
    // Camera: centre the travelling point once the stroke is wider than the window.
    readonly property real camera: trackWidth <= width ? (width - trackWidth) / 2
        : Math.max(width - trackWidth, Math.min(0, width / 2 - nodeX(position)))

    property int cursor: current
    onCurrentChanged: cursor = current

    implicitHeight: Math.round(74 * s) + Theme.fsSm
    activeFocusOnTab: true
    clip: true
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Setup steps")
    Accessible.description: qsTr("Step %1 of %2: %3. Use the left and right arrow keys to go back to a step you've done.")
        .arg(cursor + 1).arg(count).arg(labels[cursor] || "")

    Keys.onLeftPressed: cursor = Math.max(0, cursor - 1)
    Keys.onRightPressed: cursor = Math.min(reached, cursor + 1)
    Keys.onReturnPressed: jh.jump(cursor)
    Keys.onEnterPressed: jh.jump(cursor)
    Keys.onSpacePressed: jh.jump(cursor)
    onActiveFocusChanged: if (!activeFocus) cursor = current

    Item {
        id: track
        x: jh.camera
        width: jh.trackWidth
        height: jh.height

        // The stroke itself.
        Canvas {
            id: stroke
            anchors.fill: parent
            renderStrategy: Canvas.Cooperative
            readonly property real inkX: jh.nodeX(jh.position)
            readonly property color wet: Theme.accent
            readonly property color road: Theme.alpha(Theme.text, Theme.dark ? 0.26 : 0.3)
            onInkXChanged: requestPaint()
            onWetChanged: requestPaint()
            onRoadChanged: requestPaint()
            onWidthChanged: requestPaint()

            // A hand-drawn line wobbles a little; keep it the same every paint.
            function yAt(x) { return jh.lineY + Math.sin(x / (41 * jh.s)) * 1.6 * jh.s + Math.sin(x / (97 * jh.s) + 1.3) * 1.2 * jh.s }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const x0 = jh.nodeX(0)
                const x1 = jh.nodeX(Math.max(0, jh.count - 1))
                // Road ahead: small dots.
                ctx.fillStyle = road
                const step = 9 * jh.s
                for (let x = Math.max(x0, inkX + step); x <= x1; x += step) {
                    ctx.beginPath()
                    ctx.arc(x, yAt(x), 1.35 * jh.s, 0, Math.PI * 2)
                    ctx.fill()
                }
                if (inkX <= x0 + 0.5)
                    return
                // Written stretch: a brush stroke that swells slightly and
                // tapers where the pen is now.
                const base = 3.4 * jh.s
                const top = []
                const bottom = []
                for (let x = x0; x <= inkX; x += 3) {
                    const tip = Math.min(1, (inkX - x) / (30 * jh.s))
                    const w = base * (0.82 + 0.18 * Math.sin(x / (23 * jh.s))) * (0.3 + 0.7 * tip)
                    top.push([x, yAt(x) - w / 2])
                    bottom.push([x, yAt(x) + w / 2])
                }
                top.push([inkX, yAt(inkX)])
                ctx.fillStyle = wet
                ctx.beginPath()
                ctx.moveTo(top[0][0], top[0][1])
                for (let i = 1; i < top.length; ++i)
                    ctx.lineTo(top[i][0], top[i][1])
                for (let i = bottom.length - 1; i >= 0; --i)
                    ctx.lineTo(bottom[i][0], bottom[i][1])
                ctx.closePath()
                ctx.fill()
            }
        }

        Repeater {
            model: jh.count
            delegate: Item {
                id: node
                required property int index
                readonly property bool here: index === jh.current
                readonly property bool done: index < jh.current
                readonly property bool reachable: index <= jh.reached && !here
                readonly property bool cursorHere: jh.activeFocus && jh.cursor === index
                readonly property real dot: here ? Math.round(24 * jh.s) : Math.round(14 * jh.s)

                x: jh.nodeX(index) - jh.gap / 2
                width: jh.gap
                height: jh.height

                Rectangle {
                    id: ring
                    x: (parent.width - width) / 2
                    y: stroke.yAt(jh.nodeX(node.index)) - height / 2
                    width: node.dot
                    height: width
                    radius: width / 2
                    color: node.done ? Theme.accent : Theme.bg
                    border.width: node.here ? Math.round(3 * jh.s) : 2
                    border.color: node.here || node.done ? Theme.accent
                                : node.reachable ? Theme.accentText : Theme.alpha(Theme.text, Theme.dark ? 0.34 : 0.4)
                    Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack } }
                    Behavior on color { ColorAnimation { duration: Theme.normal } }

                    Icon {
                        anchors.centerIn: parent
                        visible: node.done
                        name: "check"
                        strokeWidth: 3.2
                        size: parent.width * 0.62
                        color: Theme.accentInk
                    }
                    // The pen resting on the page (size, not scale: the
                    // software renderer draws scaled round shapes badly).
                    Rectangle {
                        id: pen
                        anchors.centerIn: parent
                        visible: node.here
                        property real k: 0.36
                        width: Math.round(parent.width * k)
                        height: width
                        radius: width / 2
                        color: Theme.accent
                        SequentialAnimation on k {
                            running: node.here && !Theme.reducedMotion
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.36; to: 0.5; duration: 900; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.5; to: 0.36; duration: 900; easing.type: Easing.InOutSine }
                        }
                    }
                    FocusFrame { shown: node.cursorHere; baseRadius: parent.radius }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: jh.lineY + Math.round(22 * jh.s)
                    width: parent.width - Theme.s2
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: jh.labels[node.index] || ""
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.fsSm
                    font.weight: node.here ? Font.Bold : Font.Normal
                    font.letterSpacing: Theme.tracking(Theme.fsSm)
                    color: node.here ? Theme.text : node.done || node.reachable ? Theme.muted : Theme.faint
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: node.reachable
                    hoverEnabled: true
                    cursorShape: node.reachable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: jh.jump(node.index)
                }
            }
        }
    }

    // Soft edges where the stroke runs off-screen.
    Rectangle {
        visible: jh.trackWidth > jh.width && jh.camera < -1
        width: Math.round(48 * jh.s)
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.bg }
            GradientStop { position: 1; color: Theme.alpha(Theme.bg, 0) }
        }
    }
    Rectangle {
        visible: jh.trackWidth > jh.width && jh.camera > jh.width - jh.trackWidth + 1
        anchors.right: parent.right
        width: Math.round(48 * jh.s)
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.alpha(Theme.bg, 0) }
            GradientStop { position: 1; color: Theme.bg }
        }
    }
}
