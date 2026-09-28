import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// A colour: a swatch that opens a palette, and the hex code for exact values.
// Only #rrggbb is accepted (transparency is a separate setting).
RowLayout {
    id: root
    property string value: "#ffffff"
    property string label: ""
    signal picked(string color)

    spacing: Theme.s2

    readonly property var palette: [
        "#ffffff", "#f4efe6", "#d9c6ff", "#b48cff", "#8c52ff", "#5b2bd6", "#2a1a5e", "#120d1f",
        "#000000", "#ff5c8a", "#ff8a3d", "#ffd23f", "#3ddc97", "#35c2ff", "#4d7cff", "#9aa0b4"
    ]
    function valid(c) { return /^#[0-9a-fA-F]{6}$/.test(c) }

    AbstractButton {
        id: swatch
        implicitWidth: Theme.controlSm
        implicitHeight: Theme.controlSm
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("%1: %2. Choose a colour").arg(root.label).arg(root.value)
        onClicked: pop.open()
        background: Rectangle {
            radius: Theme.radiusSm
            color: root.valid(root.value) ? root.value : "transparent"
            border.color: swatch.hovered ? Theme.text : Theme.line
            border.width: Theme.hairline + (swatch.hovered ? 1 : 0)
            FocusFrame { shown: swatch.visualFocus; baseRadius: parent.radius }
        }

        Popup {
            id: pop
            y: swatch.height + 4
            padding: Theme.s3
            focus: true
            background: Rectangle {
                radius: Theme.radius
                color: Theme.raised
                border.color: Theme.line
                border.width: Theme.hairline
            }
            contentItem: Grid {
                columns: 8
                spacing: Theme.s1 + 2
                Repeater {
                    model: root.palette
                    AbstractButton {
                        required property string modelData
                        width: Math.round(24 * Theme.scale)
                        height: width
                        focusPolicy: Qt.StrongFocus
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData
                        onClicked: { root.picked(modelData); pop.close() }
                        background: Rectangle {
                            radius: width / 2
                            color: parent.modelData
                            border.color: root.value.toLowerCase() === parent.modelData ? Theme.accent : Theme.line
                            border.width: root.value.toLowerCase() === parent.modelData ? 3 : 1
                            FocusFrame { shown: parent.parent.visualFocus; baseRadius: parent.radius }
                        }
                    }
                }
            }
        }
    }
    Field {
        id: hex
        label: root.label
        implicitWidth: Math.round(104 * Theme.scale)
        font.family: Theme.monoFont
        text: root.value
        maximumLength: 7
        validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{0,6}/ }
        onEditingFinished: {
            const c = (text.startsWith("#") ? text : "#" + text).toLowerCase()
            if (root.valid(c))
                root.picked(c)
            text = Qt.binding(() => root.value) // typing replaced the binding
        }
    }
}
