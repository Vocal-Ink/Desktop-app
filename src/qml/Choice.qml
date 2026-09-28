import QtQuick
import QtQuick.Controls.Basic

// Drop-down. Use `model` with textRole/valueRole, or a plain string list.
ComboBox {
    id: root
    property string label: ""

    implicitHeight: Theme.control
    implicitWidth: 260
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.uiFont
    font.pixelSize: Theme.fsMd
    leftPadding: Theme.s3
    rightPadding: Theme.s3 + Math.round(18 * Theme.scale) + Theme.s2
    Accessible.name: label

    contentItem: Text {
        text: root.displayText
        font: root.font
        color: root.enabled ? Theme.text : Theme.faint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: root.width - width - Theme.s3
        y: (root.height - height) / 2
        name: "chevron-down"
        size: Math.round(18 * Theme.scale)
        color: Theme.muted
        rotation: root.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.fast } }
    }
    background: Rectangle {
        radius: Theme.radius
        color: root.hovered ? Theme.mix(Theme.raised, Theme.text, 0.04) : Theme.raised
        border.width: root.popup.visible ? 2 : Theme.hairline
        border.color: root.popup.visible ? Theme.accent : Theme.line
        FocusFrame { shown: root.visualFocus && !root.popup.visible; baseRadius: parent.radius }
    }

    delegate: ItemDelegate {
        id: item
        required property int index
        required property var model
        width: ListView.view ? ListView.view.width : root.width
        height: Theme.control
        highlighted: root.highlightedIndex === index
        leftPadding: Theme.s3
        contentItem: Row {
            spacing: Theme.s2
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: Math.round(16 * Theme.scale)
                color: Theme.accentText
                opacity: root.currentIndex === item.index ? 1 : 0
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    const m = item.model
                    if (!root.textRole)
                        return m.modelData
                    const v = m[root.textRole]
                    if (v !== undefined)
                        return v
                    return m.modelData ? m.modelData[root.textRole] : ""
                }
                font: root.font
                color: Theme.text
                elide: Text.ElideRight
                width: item.width - Theme.s6 - Theme.s3
            }
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: item.highlighted ? Theme.accentWash : item.hovered ? Theme.hover : "transparent"
        }
    }

    popup: Popup {
        y: root.height + 4
        width: Math.max(root.width, 200)
        implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, 360)
        padding: 4
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
            NumberAnimation { property: "y"; from: root.height + 4 - Theme.travel(6); to: root.height + 4; duration: Theme.normal; easing.type: Easing.OutCubic }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: Theme.fast } }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.raised
            border.color: Theme.line
            border.width: Theme.hairline
        }
    }
}
