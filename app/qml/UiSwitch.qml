// UiSwitch.qml — 自绘开关（替代 CheckBox）：滑轨 + 滑块，位置/颜色 120ms 过渡，颜色全走 palette
import QtQuick
import QtQuick.Controls
import XuanHub

Switch {
    id: control

    font.pixelSize: UiMetrics.fontM
    spacing: UiMetrics.space2
    opacity: enabled ? 1.0 : 0.5

    indicator: Rectangle {
        x: control.leftPadding
        y: control.height / 2 - height / 2
        implicitWidth: 42
        implicitHeight: 24
        radius: 12
        color: control.checked ? control.palette.highlight : control.palette.button
        border.width: 1
        border.color: control.checked ? control.palette.highlight : control.palette.mid
        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
        Behavior on border.color { ColorAnimation { duration: UiMetrics.animFast } }

        Rectangle {   // 滑块
            y: 3
            width: 18
            height: 18
            radius: 9
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? control.palette.highlightedText : control.palette.buttonText
            Behavior on x { NumberAnimation { duration: UiMetrics.animFast; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.palette.buttonText
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
