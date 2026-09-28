import QtQuick

// A voice's monogram. Local voices are solid ink; cloud voices are outlined.
Rectangle {
    id: av
    property string name: ""
    property string initials: ""
    property bool local: true
    property real size: Math.round(36 * Theme.scale)
    property bool speaking: false

    implicitWidth: size
    implicitHeight: size
    radius: Theme.corners === "sharp" ? 4 : size / 2
    color: local ? Theme.accent : "transparent"
    border.color: Theme.accent
    border.width: local ? 0 : 2
    Accessible.ignored: true

    Text {
        anchors.centerIn: parent
        text: av.initials !== "" ? av.initials : av.name.substring(0, 1).toUpperCase()
        font.family: Theme.fontChoice === "atkinson" ? "Bricolage Grotesque" : Theme.uiFont
        font.weight: Font.Bold
        font.pixelSize: Math.round(av.size * 0.38)
        color: av.local ? Theme.accentInk : Theme.accentText
    }

    // A ring that breathes while this voice is talking.
    Rectangle {
        anchors.centerIn: parent
        width: parent.width + 8
        height: width
        radius: av.radius + 4
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
        visible: av.speaking
        opacity: 0.6
        SequentialAnimation on scale {
            running: av.speaking && Theme.motionOn
            loops: Animation.Infinite
            NumberAnimation { from: 1; to: 1.12; duration: 700; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.12; to: 1; duration: 700; easing.type: Easing.InOutSine }
        }
    }
}
