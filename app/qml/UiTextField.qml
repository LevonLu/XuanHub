// UiTextField.qml — FluentWinUI3 输入框，额外标示 schema 非法输入
import QtQuick
import QtQuick.Controls.FluentWinUI3 as Fluent
import QtQuick.Layouts
import XuanHub

Fluent.TextField {
    id: control
    property bool invalid: false
    readonly property color dangerColor: (typeof themeManager !== "undefined" && themeManager)
                                         ? themeManager.colors.danger : "#c42b1c"
    font.pixelSize: UiMetrics.fontM
    selectByMouse: true
    Layout.fillWidth: true

    // 保留样式自身的底部焦点线与背景，仅叠加错误边框。
    Rectangle {
        anchors.fill: parent
        visible: control.invalid
        color: "transparent"
        radius: UiMetrics.radiusS
        border.width: 1
        border.color: control.dangerColor
    }
}
