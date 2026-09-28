import QtQuick
import QtQuick.Layouts
import Ink.Core

// App navigation. Collapses to an icon rail when labels are off or the window is narrow.
Rectangle {
    id: bar
    property string current: "talk"
    property bool labels: true
    signal navigate(string page)
    signal openPalette()

    readonly property var pages: [
        { id: "talk", title: qsTr("Talk"), icon: "message-square-text", key: "Ctrl+1" },
        { id: "board", title: qsTr("Board"), icon: "layout-grid", key: "Ctrl+2" },
        { id: "voices", title: qsTr("Voices"), icon: "audio-lines", key: "Ctrl+3" },
        { id: "audio", title: qsTr("Audio & mic"), icon: "cable", key: "Ctrl+4" },
        { id: "stream", title: qsTr("Stream"), icon: "radio", key: "Ctrl+5" },
        { id: "avatar", title: qsTr("Avatar"), icon: "smile", key: "Ctrl+6" },
        { id: "settings", title: qsTr("Settings"), icon: "settings", key: "Ctrl+," }
    ]

    implicitWidth: labels ? Math.round(224 * Math.min(1.3, Theme.scale)) : Math.round(76 * Math.min(1.3, Theme.scale))
    color: Theme.surface
    Behavior on implicitWidth { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Sections")

    Rectangle {
        anchors.right: parent.right
        width: Theme.hairline
        height: parent.height
        color: Theme.line
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s3
        anchors.topMargin: Theme.s5
        spacing: Theme.s1

        // Wordmark
        Row {
            Layout.leftMargin: bar.labels ? Theme.s2 : 0
            Layout.alignment: bar.labels ? Qt.AlignLeft : Qt.AlignHCenter
            Layout.bottomMargin: Theme.s5
            spacing: Theme.s2
            Image {
                source: "qrc:/icons/tray.png"
                width: Math.round(30 * Theme.scale)
                height: width
                sourceSize: Qt.size(64, 64)
                smooth: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: bar.labels
                anchors.verticalCenter: parent.verticalCenter
                text: "Vocal Ink"
                font.family: Theme.displayFont
                font.weight: Font.ExtraBold
                font.pixelSize: Math.round(22 * Theme.scale)
                color: Theme.text
            }
        }

        Repeater {
            model: bar.pages
            NavItem {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.title
                iconName: modelData.icon
                labels: bar.labels
                current: bar.current === modelData.id
                badge: modelData.id === "audio" && App.micLive ? 1 : 0
                onClicked: bar.navigate(modelData.id)
            }
        }

        Item { Layout.fillHeight: true }

        // Search & commands
        NavItem {
            Layout.fillWidth: true
            text: qsTr("Search & commands")
            iconName: "command"
            labels: bar.labels
            onClicked: bar.openPalette()
            KeyCombo {
                visible: bar.labels && parent.width > Math.round(200 * Theme.scale)
                anchors.right: parent.right
                anchors.rightMargin: Theme.s2
                anchors.verticalCenter: parent.verticalCenter
                sequence: "Ctrl+K"
            }
        }
        NavItem {
            Layout.fillWidth: true
            text: qsTr("Compact bar")
            iconName: "picture-in-picture-2"
            labels: bar.labels
            onClicked: App.trigger("window.compact")
        }
        NavItem {
            Layout.fillWidth: true
            text: bar.labels ? qsTr("Collapse") : qsTr("Expand")
            iconName: "panel-left"
            labels: bar.labels
            onClicked: App.prefs["ui/sidebarLabels"] = !bar.labels
        }
    }
}
