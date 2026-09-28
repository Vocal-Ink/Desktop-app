import QtQuick
import QtQuick.Controls.Basic

// A small set of mutually exclusive options shown side by side.
// options: [{ value, label, icon? }]
Rectangle {
    id: root
    property var options: []
    property var value
    property string label: ""
    signal activated(var value)

    implicitHeight: Theme.control
    implicitWidth: row.implicitWidth + 8
    radius: Theme.corners === "round" ? height / 2 : Theme.radius
    color: Theme.sunken
    border.color: Theme.line
    border.width: Theme.hairline
    Accessible.role: Accessible.Grouping
    Accessible.name: label

    readonly property int currentIndex: {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === value)
                return i
        return -1
    }

    // The sliding ink behind the selected option.
    Rectangle {
        id: pill
        readonly property Item target: currentIndex >= 0 && rep.count > currentIndex ? rep.itemAt(currentIndex) : null
        visible: target !== null
        x: target ? row.x + target.x : 0
        y: 4
        width: target ? target.width : 0
        height: parent.height - 8
        radius: Theme.corners === "round" ? height / 2 : Theme.radiusSm
        color: Theme.accent
        Behavior on x { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
    }

    Row {
        id: row
        x: 4
        y: 4
        height: parent.height - 8
        Repeater {
            id: rep
            model: root.options
            AbstractButton {
                id: opt
                required property var modelData
                required property int index
                readonly property bool selected: root.currentIndex === index
                height: row.height
                width: Math.max(implicitContentWidth + Theme.s4 * 2, height * 1.4)
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                checkable: true
                checked: selected
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label || modelData.tip || ""
                Accessible.checked: selected
                onClicked: { root.value = modelData.value; root.activated(modelData.value) }
                contentItem: Row {
                    spacing: Theme.s1 + 2
                    anchors.centerIn: parent
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !!opt.modelData.icon
                        name: opt.modelData.icon || ""
                        size: Math.round(16 * Theme.scale)
                        color: opt.selected ? Theme.accentInk : opt.hovered ? Theme.text : Theme.muted
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !!opt.modelData.label
                        text: opt.modelData.label || ""
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.fsSm
                        font.weight: Font.DemiBold
                        color: opt.selected ? Theme.accentInk : opt.hovered ? Theme.text : Theme.muted
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                }
                background: Item {
                    FocusFrame { shown: opt.visualFocus; baseRadius: Theme.radiusSm }
                }
                InkTip { visible: !!opt.modelData.tip && opt.hovered; text: opt.modelData.tip || "" }
            }
        }
    }
}
