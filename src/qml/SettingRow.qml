import QtQuick
import QtQuick.Layouts

// One setting: what it is and what it does on the left, the control on the right.
// Drops the control under the text when the row gets narrow.
Item {
    id: row
    property string title
    property string description: ""
    property bool stacked: width < Math.round(520 * Theme.scale)
    default property alias control: slot.data

    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight

    GridLayout {
        id: grid
        anchors.left: parent.left
        anchors.right: parent.right
        columns: row.stacked ? 1 : 2
        columnSpacing: Theme.s5
        rowSpacing: Theme.s2

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Txt { text: row.title; role: "label"; Layout.fillWidth: true }
            Txt { visible: row.description !== ""; text: row.description; role: "caption"; Layout.fillWidth: true }
        }
        Row {
            id: slot
            Layout.alignment: row.stacked ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
            spacing: Theme.s2
        }
    }
}
