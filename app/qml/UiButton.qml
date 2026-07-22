// UiButton.qml — 自绘按钮：primary（accent 实心）/ outline（描边）/ ghost（文字）三形态
// 颜色全走 palette（hover/press 用 Qt.lighter/Qt.rgba 派生），120ms 过渡。
import QtQuick
import QtQuick.Controls
import XuanHub

Button {
    id: control

    // 形态：primary = accent 实心 + highlightedText(onAccent) 字；outline = 描边；ghost = 纯文字
    property string variant: "outline"

    font.pixelSize: UiMetrics.fontM
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0
    opacity: enabled ? 1.0 : 0.5

    background: Rectangle {
        implicitHeight: UiMetrics.controlHeightM
        radius: UiMetrics.radiusS
        readonly property color acc: control.palette.highlight
        readonly property color fg: control.palette.buttonText
        color: {
            if (control.variant === "primary")
                return control.pressed ? Qt.darker(acc, 1.12)
                     : control.hovered ? Qt.lighter(acc, 1.08) : acc
            if (control.variant === "outline")
                return control.pressed ? Qt.rgba(acc.r, acc.g, acc.b, 0.18)
                     : control.hovered ? Qt.rgba(acc.r, acc.g, acc.b, 0.10) : "transparent"
            // ghost：hover 底色从前景色低 alpha 派生，dark/light 两套模式都成立
            return control.pressed ? Qt.rgba(fg.r, fg.g, fg.b, 0.16)
                 : control.hovered ? Qt.rgba(fg.r, fg.g, fg.b, 0.08) : "transparent"
        }
        border.width: control.variant === "outline" ? 1 : 0
        border.color: control.variant === "outline"
                      ? (control.hovered || control.activeFocus ? acc : control.palette.mid)
                      : "transparent"
        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
        Behavior on border.color { ColorAnimation { duration: UiMetrics.animFast } }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.variant === "primary" ? control.palette.highlightedText
             : control.variant === "outline" && (control.hovered || control.activeFocus)
               ? control.palette.highlight
             : control.palette.buttonText
        Behavior on color { ColorAnimation { duration: UiMetrics.animFast } }
    }
}
