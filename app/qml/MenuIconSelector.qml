// MenuIconSelector.qml — 带真实图形预览的菜单图标草稿选择器
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.FluentWinUI3 as Fluent
import XuanHub

Item {
    id: root
    property string selectedIcon: "original"
    signal iconChosen(string iconStyle)
    implicitWidth: 288
    implicitHeight: choices.implicitHeight

    GridLayout {
        id: choices
        width: parent.width
        columns: width >= 260 ? 3 : 1
        columnSpacing: UiMetrics.space2
        rowSpacing: UiMetrics.space2
        Repeater {
            model: [
                { value: "original", label: qsTranslate("xuanhub.settings", "原版") },
                { value: "hub", label: qsTranslate("xuanhub.settings", "方框枢纽") },
                { value: "compact", label: qsTranslate("xuanhub.settings", "紧凑 H") }
            ]
            delegate: Fluent.Button {
                id: option
                required property var modelData
                objectName: "menuIconOption_" + modelData.value
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 88
                Layout.preferredHeight: 80
                text: modelData.label
                checked: root.selectedIcon === modelData.value
                checkable: false
                padding: 6
                Accessible.role: Accessible.RadioButton
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: root.iconChosen(modelData.value)
                // 三个图形选项使用同一 palette；选中态与键盘焦点都清晰可见。
                background: Rectangle {
                    radius: UiMetrics.radiusM
                    color: option.checked ? option.palette.highlight : option.palette.button
                    border.width: option.checked || option.activeFocus ? 2 : 1
                    border.color: option.checked || option.activeFocus
                                  ? option.palette.highlight : option.palette.mid
                }
                contentItem: ColumnLayout {
                    spacing: UiMetrics.space2
                    BrandIcon {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        iconStyle: option.modelData.value
                        glyphColor: option.checked ? option.palette.highlightedText
                                                   : option.palette.buttonText
                    }
                    Text {
                        Layout.fillWidth: true
                        text: option.text
                        font.pixelSize: UiMetrics.fontS
                        color: option.checked ? option.palette.highlightedText
                                              : option.palette.buttonText
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
