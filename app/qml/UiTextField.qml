// UiTextField.qml — 自绘文本框：圆角描边、焦点 accent 边、invalid 红框，颜色全走 palette
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts   // 根上的 Layout.fillWidth 附件属性需要本模块
import XuanHub

TextField {
    id: control

    // schema 表单的非法输入红框（double/color/json 字段经 setFieldInvalid 驱动）
    property bool invalid: false
    // danger 不在 palette 角色内：优先主题色板（设置窗口上下文注入 themeManager），
    // typeof 守卫防在非设置窗口上下文中断链；硬编码仅作兜底
    readonly property color dangerColor: (typeof themeManager !== "undefined" && themeManager)
                                         ? themeManager.colors.danger : "#f38ba8"

    font.pixelSize: UiMetrics.fontM
    color: control.palette.text
    placeholderTextColor: control.palette.placeholderText
    selectionColor: control.palette.highlight
    selectedTextColor: control.palette.highlightedText
    selectByMouse: true
    leftPadding: UiMetrics.space3 - 2
    rightPadding: UiMetrics.space3 - 2
    Layout.fillWidth: true
    opacity: enabled ? 1.0 : 0.5

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: UiMetrics.controlHeightM
        radius: UiMetrics.radiusS
        color: Qt.rgba(control.palette.base.r, control.palette.base.g, control.palette.base.b, 0.86)
        border.width: 1
        border.color: control.invalid ? control.dangerColor
                    : control.activeFocus ? control.palette.highlight
                    : Qt.rgba(control.palette.mid.r, control.palette.mid.g, control.palette.mid.b, 0.72)
        Behavior on border.color { ColorAnimation { duration: UiMetrics.animFast } }
    }
}
