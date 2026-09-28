import QtQuick
import QtQuick.Layouts
import Ink.Core

// Edits a settings map of short text -> text (abbreviations, variables).
ColumnLayout {
    id: editor
    property string prefKey
    property string keyLabel: qsTr("Type")
    property string valueLabel: qsTr("Say")
    property string keyPlaceholder: ""
    property string valuePlaceholder: ""
    property string keyPrefix: ""
    property string keySuffix: ""

    readonly property var map: App.prefs[prefKey] || ({})
    readonly property var keys: Object.keys(map).sort()

    spacing: Theme.s2

    function write(m) { App.prefs[prefKey] = m }
    function setEntry(oldKey, key, value) {
        const m = Object.assign({}, map)
        if (oldKey !== "" && oldKey !== key)
            delete m[oldKey]
        if (key.trim() !== "")
            m[key.trim()] = value
        write(m)
    }
    function removeEntry(key) {
        const m = Object.assign({}, map)
        delete m[key]
        write(m)
    }

    Repeater {
        model: editor.keys
        RowLayout {
            required property string modelData
            Layout.fillWidth: true
            spacing: Theme.s2
            Field {
                Layout.preferredWidth: Math.round(170 * Theme.scale)
                label: editor.keyLabel
                text: modelData
                font.family: Theme.monoFont
                leftPadding: Theme.s3
                onEditingFinished: if (text !== modelData) editor.setEntry(modelData, text, editor.map[modelData])
            }
            Icon { name: "arrow-right"; color: Theme.faint; size: Math.round(16 * Theme.scale) }
            Field {
                Layout.fillWidth: true
                label: editor.valueLabel
                text: editor.map[modelData] || ""
                placeholderText: editor.valuePlaceholder
                onEditingFinished: if (text !== editor.map[modelData]) editor.setEntry(modelData, modelData, text)
            }
            IconButton { small: true; iconName: "trash-2"; tip: qsTr("Remove %1").arg(modelData); onClicked: editor.removeEntry(modelData) }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        Field {
            id: newKey
            Layout.preferredWidth: Math.round(170 * Theme.scale)
            label: editor.keyLabel
            placeholderText: editor.keyPlaceholder
            font.family: Theme.monoFont
        }
        Icon { name: "arrow-right"; color: Theme.faint; size: Math.round(16 * Theme.scale) }
        Field {
            id: newValue
            Layout.fillWidth: true
            label: editor.valueLabel
            placeholderText: editor.valuePlaceholder
            onAccepted: add.clicked()
        }
        PillButton {
            id: add
            small: true
            iconName: "plus"
            text: qsTr("Add")
            enabled: newKey.text.trim() !== ""
            onClicked: { editor.setEntry("", newKey.text, newValue.text); newKey.clear(); newValue.clear(); newKey.forceActiveFocus() }
        }
    }
}
