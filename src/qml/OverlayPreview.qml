import QtQuick
import Ink.Core

// An in-app approximation of an overlay page on a 1920×1080 stream, drawn
// from the same effective style the page gets. Good for position, size,
// colours and fonts; "Open in browser" and "Send test caption" show the
// real thing.
Rectangle {
    id: pv
    property var style: ({})
    property string kind: "captions"
    property string sample: qsTr("Good game everyone, that last round was close!")
    property real progress: 0.62

    implicitWidth: 640
    implicitHeight: Math.round(width * 9 / 16)
    radius: Theme.radius
    clip: true
    color: "#1b1433"
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Overlay preview")

    readonly property real s: width / 1920
    function v(path, fallback) {
        let o = style
        const parts = path.split(".")
        for (let i = 0; i < parts.length; ++i) {
            if (o === undefined || o === null || typeof o !== "object")
                return fallback
            o = o[parts[i]]
        }
        return o === undefined || o === null ? fallback : o
    }
    function withAlpha(c, pct) {
        const col = Qt.color(c)
        return Qt.rgba(col.r, col.g, col.b, Math.max(0, Math.min(1, pct / 100)))
    }
    // Places a box of size w×h by a 9-point anchor, margin and offsets.
    function placeX(anchor, w, margin, offset) {
        const base = anchor.endsWith("left") ? margin : anchor.endsWith("right") ? width - w - margin : (width - w) / 2
        return base + offset / 100 * width
    }
    function placeY(anchor, h, margin, offset) {
        const base = anchor.startsWith("top") ? margin : anchor.startsWith("bottom") ? height - h - margin : (height - h) / 2
        return base + offset / 100 * height
    }

    // A stand-in for gameplay behind the overlay.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#2b1d55" }
            GradientStop { position: 0.55; color: "#173a4a" }
            GradientStop { position: 1; color: "#3b2a1f" }
        }
    }
    Rectangle {
        x: pv.width * 0.58; y: pv.height * 0.18
        width: pv.width * 0.3; height: width
        radius: width / 2
        color: Qt.rgba(1, 0.8, 0.5, 0.08)
    }
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: parent.height * 0.34
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0) }
            GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0.35) }
        }
    }

    NumberAnimation on progress {
        running: Theme.motionOn && pv.visible && pv.kind !== "avatar"
        from: 0; to: 1.25; duration: 3400; loops: Animation.Infinite
    }
    readonly property real shown: Math.min(1, progress)

    // --- Captions -----------------------------------------------------------
    Item {
        id: caption
        visible: pv.kind === "captions"
        readonly property real fontPx: Math.max(6, pv.v("font.size", 44) * pv.s)
        readonly property real pad: pv.v("box.padding", 18) * pv.s
        readonly property real maxW: pv.v("box.maxWidth", 70) / 100 * pv.width
        readonly property string anchor: pv.v("position.anchor", "bottom")
        readonly property bool inkLook: ["ink", "glow"].indexOf(pv.v("animation.word", "ink")) >= 0
                                       || ["ink", "karaoke"].indexOf(pv.v("preset", "")) >= 0
        readonly property string text: pv.v("font.uppercase", false) ? pv.sample.toUpperCase() : pv.sample

        width: Math.min(maxW, measure.implicitWidth + pad * 2)
        height: box.height + (nameLabel.visible && pv.v("name.position", "above") === "above" ? nameLabel.height + 4 * pv.s : 0)
        x: pv.placeX(anchor, width, pv.v("position.margin", 48) * pv.s, pv.v("position.offsetX", 0))
        y: pv.placeY(anchor, height, pv.v("position.margin", 48) * pv.s, pv.v("position.offsetY", 0))

        Text {
            id: measure
            visible: false
            text: caption.text
            font: base.font
        }
        Text {
            id: nameLabel
            visible: pv.v("name.show", false)
            text: pv.v("name.text", "") || qsTr("Vocal Ink voice")
            color: pv.v("colors.name", "#d9c6ff")
            font.family: base.font.family
            font.pixelSize: caption.fontPx * 0.5
            font.weight: Font.Bold
        }
        Rectangle {
            id: box
            y: nameLabel.visible && pv.v("name.position", "above") === "above" ? nameLabel.height + 4 * pv.s : 0
            width: parent.width
            height: base.height + caption.pad * 2
            radius: pv.v("box.radius", 14) * pv.s
            color: pv.withAlpha(pv.v("colors.background", "#120d1f"), pv.v("colors.backgroundOpacity", 72))
            border.width: pv.v("box.borderWidth", 0) * pv.s
            border.color: pv.withAlpha(pv.v("colors.border", "#ffffff"), pv.v("colors.borderOpacity", 12))

            // Bubble tail.
            Rectangle {
                readonly property string tail: pv.v("box.tail", "none")
                visible: tail !== "none" && parent.color.a > 0.01
                width: caption.pad * 1.2
                height: width
                rotation: 45
                color: parent.color
                y: parent.height - height / 2
                x: tail === "left" ? caption.pad * 1.5 : tail === "right" ? parent.width - caption.pad * 1.5 - width : (parent.width - width) / 2
                z: -1
            }

            // Not spoken yet (ink looks show it faintly).
            Text {
                id: base
                x: caption.pad
                y: caption.pad
                width: box.width - caption.pad * 2
                text: caption.text
                wrapMode: Text.WordWrap
                horizontalAlignment: pv.v("box.align", "center") === "left" ? Text.AlignLeft
                                   : pv.v("box.align", "center") === "right" ? Text.AlignRight : Text.AlignHCenter
                maximumLineCount: pv.v("history.lines", 1) > 1 ? 4 : 3
                font.family: pv.v("font.family", "Bricolage Grotesque")
                font.pixelSize: caption.fontPx
                font.weight: pv.v("font.weight", 700)
                font.italic: pv.v("font.italic", false)
                font.letterSpacing: pv.v("font.letterSpacing", 0) / 100 * caption.fontPx
                lineHeight: pv.v("font.lineHeight", 1.25)
                color: caption.inkLook ? pv.withAlpha(pv.v("colors.unspoken", "#ffffff"), pv.v("colors.unspokenOpacity", 35)) : "transparent"
                style: pv.v("effects.outline", 0) > 0 ? Text.Outline : Text.Normal
                styleColor: pv.v("colors.outline", "#000000")
            }
            // Spoken so far, revealed left to right.
            Item {
                x: base.x
                y: base.y
                width: base.width * pv.shown
                height: base.height
                clip: true
                Text {
                    width: base.width
                    text: base.text
                    wrapMode: base.wrapMode
                    horizontalAlignment: base.horizontalAlignment
                    maximumLineCount: base.maximumLineCount
                    font: base.font
                    lineHeight: base.lineHeight
                    color: pv.v("colors.text", "#ffffff")
                    style: base.style
                    styleColor: base.styleColor
                }
            }
            // The wet edge where the voice is now.
            Item {
                visible: caption.inkLook && pv.progress < 1
                x: base.x + base.width * Math.max(0, pv.shown - 0.1)
                y: base.y
                width: base.width * Math.min(0.1, pv.shown)
                height: base.height
                clip: true
                Text {
                    x: -parent.x + base.x
                    width: base.width
                    text: base.text
                    wrapMode: base.wrapMode
                    horizontalAlignment: base.horizontalAlignment
                    maximumLineCount: base.maximumLineCount
                    font: base.font
                    lineHeight: base.lineHeight
                    color: pv.v("colors.ink", "#b48cff")
                }
            }
            // Speaking indicator.
            Rectangle {
                visible: pv.v("indicator.show", false)
                width: caption.fontPx * 0.32
                height: width
                radius: width / 2
                color: pv.v("colors.ink", "#b48cff")
                x: pv.v("indicator.position", "before") === "after" ? parent.width - caption.pad * 0.7 - width / 2
                                                                    : caption.pad * 0.5 - width / 2
                y: caption.pad + caption.fontPx * 0.5 - height / 2
            }
        }
    }

    // --- Chat -------------------------------------------------------------------
    Column {
        id: chat
        visible: pv.kind === "chat"
        readonly property real fontPx: Math.max(6, pv.v("font.size", 30) * pv.s)
        readonly property string anchor: pv.v("position.anchor", "bottom-left")
        width: pv.v("box.maxWidth", 40) / 100 * pv.width
        spacing: 6 * pv.s
        x: pv.placeX(anchor, width, pv.v("position.margin", 48) * pv.s, pv.v("position.offsetX", 0))
        y: pv.placeY(anchor, height, pv.v("position.margin", 48) * pv.s, pv.v("position.offsetY", 0))
        Repeater {
            model: [
                { name: "inkling_42", color: "#35c2ff", text: qsTr("that clutch was insane") },
                { name: "moonpaw", color: "#ff8a3d", text: qsTr("hi from Brazil!") },
                { name: "vellum", color: "#3ddc97", text: qsTr("what voice is that? it sounds great") }
            ]
            Rectangle {
                required property var modelData
                required property int index
                width: chat.width
                height: line.implicitHeight + pv.v("box.padding", 12) * pv.s * 2
                radius: pv.v("box.radius", 12) * pv.s
                color: pv.withAlpha(pv.v("colors.background", "#120d1f"), pv.v("colors.backgroundOpacity", 72))
                border.width: index === 2 && pv.v("chat.highlightReading", true) ? Math.max(1, 2 * pv.s) : 0
                border.color: pv.v("colors.ink", "#b48cff")
                Text {
                    id: line
                    x: pv.v("box.padding", 12) * pv.s
                    y: x
                    width: parent.width - x * 2
                    wrapMode: Text.WordWrap
                    textFormat: Text.StyledText
                    font.family: pv.v("font.family", "Bricolage Grotesque")
                    font.pixelSize: chat.fontPx
                    font.weight: pv.v("font.weight", 600)
                    color: pv.v("colors.text", "#ffffff")
                    text: "<b><font color=\"" + (pv.v("chat.useNameColors", true) ? parent.modelData.color : pv.v("colors.name", "#d9c6ff"))
                          + "\">" + parent.modelData.name + "</font></b>  " + parent.modelData.text
                }
            }
        }
    }

    // --- PNGtuber -----------------------------------------------------------------
    Item {
        id: avatar
        visible: pv.kind === "avatar"
        readonly property string anchor: pv.v("avatar.anchor", "bottom-left")
        readonly property var images: pv.v("avatar.images", ({}))
        readonly property bool custom: !!images.idle
        property real mouth: 0
        SequentialAnimation on mouth {
            running: Theme.motionOn && pv.visible && pv.kind === "avatar"
            loops: Animation.Infinite
            NumberAnimation { to: 0.8; duration: 140 }
            NumberAnimation { to: 0.2; duration: 110 }
            NumberAnimation { to: 0.65; duration: 150 }
            NumberAnimation { to: 0; duration: 160 }
            PauseAnimation { duration: 700 }
        }
        readonly property real bounce: pv.v("avatar.motion", "bounce") === "bounce" ? mouth * pv.v("avatar.intensity", 60) / 100 * 18 * pv.s : 0
        height: pv.v("avatar.size", 60) / 100 * pv.height
        width: height * 0.9
        x: pv.placeX(anchor, width, pv.v("position.margin", 48) * pv.s, 0)
        y: pv.placeY(anchor, height, pv.v("position.margin", 48) * pv.s, 0) - bounce
        transform: Scale { origin.x: avatar.width / 2; xScale: pv.v("avatar.flip", false) ? -1 : 1 }

        InkBlob {
            anchors.fill: parent
            visible: !avatar.custom
            mouth: avatar.mouth
            animate: false
        }
        Image {
            anchors.fill: parent
            visible: avatar.custom
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: !avatar.custom ? ""
                  : App.avatarAssetUrl(avatar.mouth > 0.08 && avatar.images.talking ? avatar.images.talking : avatar.images.idle)
        }
    }

    Text {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Math.max(6, 16 * pv.s)
        text: qsTr("Preview")
        font.family: Theme.monoFont
        font.pixelSize: Math.max(10, 22 * pv.s)
        color: Qt.rgba(1, 1, 1, 0.5)
    }
}
