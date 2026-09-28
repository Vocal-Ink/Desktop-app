import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts

// Modal dialog. Put content inside; buttons go in `footer`.
Popup {
    id: sheet
    property string title: ""
    property string message: ""
    property string iconName: ""
    property color iconTint: Theme.accentText
    property alias footer: footerRow.data
    default property alias body: bodyCol.data
    property real preferredWidth: Math.round(480 * Math.min(1.4, Theme.scale))

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(preferredWidth, (parent ? parent.width : 600) - Theme.s6 * 2)
    modal: true
    focus: true
    padding: Theme.s6
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    T.Overlay.modal: Rectangle {
        color: Theme.scrim
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: Theme.reducedMotion ? 1 : 0.96; to: 1; duration: Theme.normal; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.raised
        border.color: Theme.line
        border.width: Theme.hairline
    }

    contentItem: ColumnLayout {
        spacing: Theme.s4
        RowLayout {
            spacing: Theme.s3
            visible: sheet.title !== ""
            Layout.fillWidth: true
            Icon {
                visible: sheet.iconName !== ""
                name: sheet.iconName
                color: sheet.iconTint
                size: Math.round(24 * Theme.scale)
                Layout.alignment: Qt.AlignTop
            }
            Txt {
                text: sheet.title
                role: "title"
                Layout.fillWidth: true
                Accessible.role: Accessible.Heading
            }
        }
        Txt {
            visible: sheet.message !== ""
            text: sheet.message
            color: Theme.muted
            Layout.fillWidth: true
        }
        ColumnLayout {
            id: bodyCol
            Layout.fillWidth: true
            spacing: Theme.s3
        }
        RowLayout {
            id: footerRow
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            spacing: Theme.s2
            layoutDirection: Qt.RightToLeft
        }
    }
}
