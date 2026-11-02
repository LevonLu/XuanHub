// PopupAction.qml — 弹窗内随宿主主题变化的轻量操作按钮
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Button {
    id: control
    property var colors
    property bool selected: false
    property bool quiet: false
    property string iconText: ""
    implicitWidth: Math.max(32, label.implicitWidth + 24)
    implicitHeight: 32
    padding: 10
    hoverEnabled: true
    font.pixelSize: 12
    font.weight: selected ? Font.DemiBold : Font.Medium
    Accessible.name: text
    contentItem: Text {
        id: label
        text: (control.iconText ? control.iconText + "  " : "") + control.text
        font: control.font
        color: control.selected || control.hovered ? control.colors.accent : control.colors.fg
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: 8
        color: control.down ? Qt.alpha(control.colors.accent, 0.16)
            : control.selected ? control.colors.bg
            : control.hovered ? Qt.alpha(control.colors.accent, 0.08)
            : control.quiet ? "transparent" : Qt.alpha(control.colors.fg, 0.045)
        border.color: control.visualFocus ? control.colors.accent
            : control.selected ? Qt.alpha(control.colors.border, 0.7) : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
