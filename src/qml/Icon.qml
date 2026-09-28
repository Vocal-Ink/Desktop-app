import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

// A Lucide icon, stroked in the current theme colour.
Item {
    id: root
    property string name
    property color color: Theme.text
    property real size: Math.round(20 * Theme.scale)
    property real strokeWidth: 2
    property bool filled: false

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: root.size / 24
        visible: root.name !== ""
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.paths[root.name] || "" }
        }
    }
}
