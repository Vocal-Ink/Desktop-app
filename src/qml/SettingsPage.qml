import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Ink.Core

// Settings: a section list on the left, the section on the right.
Item {
    id: page
    property string section: "appearance"
    signal runOnboarding()

    readonly property var sections: [
        { id: "appearance", title: qsTr("Appearance"), icon: "palette" },
        { id: "access", title: qsTr("Accessibility"), icon: "accessibility" },
        { id: "shortcuts", title: qsTr("Shortcuts"), icon: "keyboard" },
        { id: "speech", title: qsTr("Speech input"), icon: "mic-vocal" },
        { id: "typing", title: qsTr("Typing & text"), icon: "type" },
        { id: "providers", title: qsTr("Voice providers"), icon: "globe" },
        { id: "backup", title: qsTr("Backup & updates"), icon: "file-down" },
        { id: "about", title: qsTr("About & help"), icon: "circle-help" }
    ]
    readonly property bool narrow: width < Math.round(760 * Theme.scale)

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Section list (a row of chips on narrow windows).
        ColumnLayout {
            visible: !page.narrow
            // Children fill this column; don't let that make the column fill the row.
            Layout.fillWidth: false
            Layout.preferredWidth: Math.round(220 * Math.min(1.3, Theme.scale))
            Layout.maximumWidth: Layout.preferredWidth
            Layout.fillHeight: true
            Layout.topMargin: Theme.s8
            Layout.leftMargin: Theme.s4
            spacing: Theme.s1
            Txt {
                text: qsTr("Settings")
                role: "heading"
                Layout.leftMargin: Theme.s3
                Layout.bottomMargin: Theme.s4
                Accessible.role: Accessible.Heading
            }
            Repeater {
                model: page.sections
                NavItem {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.title
                    iconName: modelData.icon
                    current: page.section === modelData.id
                    onClicked: page.section = modelData.id
                }
            }
            Item { Layout.fillHeight: true }
        }
        Rectangle { visible: !page.narrow; Layout.fillHeight: true; width: Theme.hairline; color: Theme.line }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Flickable {
                visible: page.narrow
                Layout.fillWidth: true
                Layout.preferredHeight: chips.implicitHeight + Theme.s4 * 2
                contentWidth: chips.implicitWidth + Theme.s6 * 2
                clip: true
                Row {
                    id: chips
                    x: Theme.s6
                    y: Theme.s4
                    spacing: Theme.s2
                    Repeater {
                        model: page.sections
                        Chip {
                            required property var modelData
                            text: modelData.title
                            iconName: modelData.icon
                            selected: page.section === modelData.id
                            onClicked: page.section = modelData.id
                        }
                    }
                }
            }

            Loader {
                id: body
                Layout.fillWidth: true
                Layout.fillHeight: true
                asynchronous: false
                sourceComponent: {
                    switch (page.section) {
                    case "access": return accessComp
                    case "shortcuts": return shortcutsComp
                    case "speech": return speechComp
                    case "typing": return typingComp
                    case "providers": return providersComp
                    case "backup": return backupComp
                    case "about": return aboutComp
                    default: return appearanceComp
                    }
                }
                opacity: status === Loader.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.fast } }
            }
        }
    }

    Component { id: appearanceComp; SettingsAppearance {} }
    Component { id: accessComp; SettingsAccess {} }
    Component { id: shortcutsComp; SettingsShortcuts {} }
    Component { id: speechComp; SettingsSpeech {} }
    Component { id: typingComp; SettingsTyping {} }
    Component { id: providersComp; SettingsProviders {} }
    Component { id: backupComp; SettingsBackup { onRunOnboarding: page.runOnboarding() } }
    Component { id: aboutComp; SettingsAbout { onRunOnboarding: page.runOnboarding() } }
}
