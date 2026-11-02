// SettingField.qml — 自绘设置页复用的受控输入项
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

RowLayout {
    id: root
    property string label: ""
    property string kind: "string"
    property var value
    property var options: []
    property int minimum: 0
    property int maximum: 86400
    property bool secret: false
    signal edited(var next)
    Layout.fillWidth: true
    Label { text: root.label; Layout.preferredWidth: 115; wrapMode: Text.Wrap; color: palette.windowText }
    Loader {
        Layout.fillWidth: true
        sourceComponent: root.kind === "bool" ? booleanInput : root.kind === "enum" ? enumInput : root.kind === "int" ? integerInput : textInput
    }
    Component { id: booleanInput; CheckBox { checked: root.value === true; onClicked: root.edited(checked) } }
    Component {
        id: enumInput
        UiComboBox {
            model: root.options; textRole: "label"; valueRole: "value"
            currentIndex: Math.max(0, root.options.findIndex(function(o) { return o.value === root.value }))
            onActivated: if (currentIndex >= 0) root.edited(root.options[currentIndex].value)
        }
    }
    Component { id: integerInput; UiSpinBox { from: root.minimum; to: root.maximum; value: Number(root.value || 0); onValueModified: root.edited(value) } }
    Component {
        id: textInput
        UiTextField {
            text: root.value === undefined || root.value === null ? "" : String(root.value)
            echoMode: root.secret ? TextInput.Password : TextInput.Normal
            selectByMouse: true
            onTextEdited: root.edited(text)
        }
    }
}
