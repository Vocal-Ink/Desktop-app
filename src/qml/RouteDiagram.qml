import QtQuick
import Ink.Core

// You → voice → virtual mic → your apps. A hop is solid ink when sound can
// flow through it and dotted when it can't yet.
Item {
    id: diagram
    implicitHeight: Math.round(86 * Math.min(1.4, Theme.scale))

    readonly property var nodes: [
        { icon: "keyboard", title: qsTr("You type or talk"), ok: true },
        { icon: "audio-lines", title: App.voiceName || qsTr("Your voice"), ok: App.voiceName !== "" },
        { icon: "cable", title: App.routeState === "virtual" ? App.routeName : qsTr("Virtual mic"), ok: App.routeState === "virtual" },
        { icon: "users-round", title: qsTr("Discord, games, OBS"), ok: App.routeState === "virtual" }
    ]
    readonly property real slot: width / nodes.length
    readonly property real disc: Math.round(44 * Math.min(1.3, Theme.scale))
    Accessible.role: Accessible.StaticText
    Accessible.name: nodes.map(n => n.ok ? n.title : qsTr("%1 (not connected)").arg(n.title))
                          .reduce((path, next) => qsTr("%1, then %2").arg(path).arg(next))

    // Hops between nodes.
    Repeater {
        model: diagram.nodes.length - 1
        Item {
            id: hop
            required property int index
            readonly property bool flowing: diagram.nodes[index + 1].ok
            x: diagram.slot * index + diagram.slot / 2 + diagram.disc / 2 + Theme.s2
            width: diagram.slot - diagram.disc - Theme.s2 * 2
            y: diagram.disc / 2 - 1.5
            height: 3
            clip: true

            Rectangle {
                visible: hop.flowing
                anchors.fill: parent
                radius: 1.5
                color: Theme.accent
            }
            Row {
                visible: !hop.flowing
                spacing: 5
                Repeater {
                    model: Math.max(1, Math.floor(hop.width / 9))
                    Rectangle { width: 4; height: 3; radius: 1.5; color: Theme.faint }
                }
            }
            // A drop of ink travelling along while the voice is speaking.
            Rectangle {
                visible: hop.flowing && App.speaking && Theme.motionOn
                width: 10
                height: 3
                radius: 1.5
                color: Theme.sheen
                NumberAnimation on x {
                    running: hop.flowing && App.speaking && Theme.motionOn
                    loops: Animation.Infinite
                    from: -10; to: hop.width; duration: 700
                }
            }
        }
    }

    // Nodes.
    Repeater {
        model: diagram.nodes
        Item {
            id: node
            required property var modelData
            required property int index
            x: diagram.slot * index
            width: diagram.slot
            height: diagram.height

            Rectangle {
                id: disc
                anchors.horizontalCenter: parent.horizontalCenter
                width: diagram.disc
                height: width
                radius: Theme.corners === "sharp" ? 4 : width / 2
                color: node.modelData.ok ? Theme.accentWash : "transparent"
                border.color: node.modelData.ok ? Theme.accent : Theme.line
                border.width: node.modelData.ok ? 2 : Theme.hairline
                Icon { anchors.centerIn: parent; name: node.modelData.icon; color: node.modelData.ok ? Theme.accentText : Theme.faint }
            }
            Txt {
                anchors.top: disc.bottom
                anchors.topMargin: Theme.s2
                width: parent.width - Theme.s2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: node.modelData.title
                role: "caption"
                color: node.modelData.ok ? Theme.text : Theme.faint
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
        }
    }
}
