import QtQuick
import QtQuick.Layouts

// A titled group of settings or content.
Rectangle {
    id: card
    property string title: ""
    property string subtitle: ""
    property string iconName: ""
    property alias headerExtra: extra.data
    default property alias content: body.data
    property real pad: Theme.s5

    implicitWidth: 520
    implicitHeight: col.implicitHeight + pad * 2
    radius: Theme.radiusLg
    color: Theme.cardColor
    border.color: Theme.cardBorder
    border.width: Theme.hairline

    // A soft shadow from stacked translucent layers (no shader effects needed).
    Repeater {
        model: Theme.shadowDepth > 0 ? 3 : 0
        Rectangle {
            required property int index
            z: -1
            x: -index
            y: 2 + index * 2
            width: card.width + index * 2
            height: card.height + index
            radius: card.radius + index
            color: Qt.rgba(0, 0, 0, (Theme.dark ? 0.16 : 0.05) * Theme.shadowDepth / (index + 1))
        }
    }
    Accessible.role: Accessible.Grouping
    Accessible.name: title

    // Width-only binding: a taller card never spreads its rows apart.
    ColumnLayout {
        id: col
        x: card.pad
        y: card.pad
        width: card.width - card.pad * 2
        spacing: Theme.s4

        RowLayout {
            visible: card.title !== ""
            Layout.fillWidth: true
            spacing: Theme.s3
            Rectangle {
                visible: card.iconName !== ""
                Layout.alignment: Qt.AlignTop
                implicitWidth: Math.round(34 * Theme.scale)
                implicitHeight: implicitWidth
                radius: Theme.radius
                color: Theme.accentWash
                Icon {
                    anchors.centerIn: parent
                    name: card.iconName
                    color: Theme.accentText
                    size: Math.round(18 * Theme.scale)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Txt { text: card.title; role: "title"; Layout.fillWidth: true; font.pixelSize: Theme.fsLg }
                Txt { visible: card.subtitle !== ""; text: card.subtitle; role: "caption"; Layout.fillWidth: true }
            }
            Row { id: extra; Layout.alignment: Qt.AlignTop; spacing: Theme.s2 }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Theme.s4
        }
    }
}
