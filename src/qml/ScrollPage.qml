import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A scrolling page with a readable column width, a title and optional actions.
Flickable {
    id: page
    property string title: ""
    property string subtitle: ""
    property real maxWidth: Math.round(900 * Math.min(1.35, Theme.scale))
    property alias actions: actionRow.data
    default property alias content: column.data

    clip: true
    contentWidth: width
    contentHeight: outer.implicitHeight + Theme.s8 * 2
    boundsBehavior: Flickable.StopAtBounds
    flickDeceleration: 4000
    maximumFlickVelocity: 5000
    ScrollBar.vertical: InkScrollBar {}
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_PageDown) { contentY = Math.min(contentHeight - height, contentY + height * 0.85); event.accepted = true }
        else if (event.key === Qt.Key_PageUp) { contentY = Math.max(0, contentY - height * 0.85); event.accepted = true }
    }

    ColumnLayout {
        id: outer
        width: Math.min(page.width - Theme.s6 * 2, page.maxWidth)
        x: (page.width - width) / 2
        y: Theme.s8
        spacing: Theme.s5

        RowLayout {
            visible: page.title !== ""
            Layout.fillWidth: true
            spacing: Theme.s4
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                Txt {
                    text: page.title
                    role: "heading"
                    Layout.fillWidth: true
                    Accessible.role: Accessible.Heading
                }
                Txt {
                    visible: page.subtitle !== ""
                    text: page.subtitle
                    role: "lead"
                    color: Theme.muted
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.round(640 * Theme.scale)
                }
            }
            Row { id: actionRow; spacing: Theme.s2; Layout.alignment: Qt.AlignBottom }
        }

        ColumnLayout {
            id: column
            Layout.fillWidth: true
            spacing: Theme.s5
        }
    }
}
