import QtQuick
import QtQuick.Controls.Basic

// A theme choice drawn as a miniature of the app in that palette.
AbstractButton {
    id: card
    property string themeId
    property string label
    property bool selected: false

    readonly property var pal: themeId === "system" ? null : Theme.palettes[themeId]
    readonly property color ink: pal && pal.accent ? pal.accent : Theme.accent

    implicitWidth: Math.round(150 * Math.min(1.4, Theme.scale))
    implicitHeight: preview.height + name.implicitHeight + Theme.s2 * 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    checkable: true
    checked: selected
    Accessible.role: Accessible.RadioButton
    Accessible.name: label
    Accessible.checked: selected

    contentItem: Item {}
    background: Item {
        Rectangle {
            id: preview
            width: parent.width
            height: Math.round(width * 0.62)
            radius: Theme.radius
            clip: true
            border.width: card.selected ? 3 : Theme.hairline
            border.color: card.selected ? Theme.accent : card.hovered ? Theme.muted : Theme.line
            color: card.pal ? card.pal.bg : "transparent"

            // "System" is half light, half dark.
            Row {
                visible: !card.pal
                anchors.fill: parent
                anchors.margins: parent.border.width
                Rectangle { width: parent.width / 2; height: parent.height; color: Theme.palettes.vellum.bg }
                Rectangle { width: parent.width / 2; height: parent.height; color: Theme.palettes.midnight.bg }
            }
            // Sidebar
            Rectangle {
                visible: !!card.pal
                x: parent.border.width; y: parent.border.width
                width: parent.width * 0.24
                height: parent.height - parent.border.width * 2
                color: card.pal ? card.pal.surface : "transparent"
                Column {
                    x: 6; y: 8
                    spacing: 5
                    Repeater {
                        model: 4
                        Rectangle { width: preview.width * 0.14; height: 3; radius: 1.5; color: index === 0 ? card.ink : (card.pal ? card.pal.faint : "gray") }
                    }
                }
            }
            // A line of ink half written, and the composer.
            Column {
                visible: !!card.pal
                x: preview.width * 0.3
                y: preview.height * 0.28
                spacing: 5
                Row {
                    spacing: 3
                    Rectangle { width: preview.width * 0.18; height: 6; radius: 3; color: card.pal ? card.pal.text : "gray" }
                    Rectangle { width: preview.width * 0.12; height: 6; radius: 3; color: card.ink }
                    Rectangle { width: preview.width * 0.16; height: 6; radius: 3; color: card.pal ? card.pal.faint : "gray"; opacity: 0.7 }
                }
                Rectangle {
                    width: preview.width * 0.62
                    height: preview.height * 0.24
                    radius: 4
                    color: card.pal ? card.pal.raised : "gray"
                    border.color: card.pal ? card.pal.line : "gray"
                    border.width: 1
                    Rectangle { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; width: parent.height * 0.9; height: parent.height * 0.6; radius: 3; color: card.ink }
                }
            }
            Rectangle {
                visible: card.selected
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                width: 20; height: 20; radius: 10
                color: Theme.accent
                Icon { anchors.centerIn: parent; name: "check"; size: 13; strokeWidth: 3; color: Theme.accentInk }
            }
        }
        Text {
            id: name
            anchors.top: preview.bottom
            anchors.topMargin: Theme.s2
            text: card.label
            font.family: Theme.uiFont
            font.pixelSize: Theme.fsSm
            font.weight: card.selected ? Font.Bold : Font.DemiBold
            color: card.selected ? Theme.text : Theme.muted
        }
        FocusFrame { shown: card.visualFocus; baseRadius: Theme.radius }
    }
}
