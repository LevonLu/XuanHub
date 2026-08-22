// Demo.qml — 第三方 V2 示例的最小 bar 视图
import QtQuick

Item {
    property var plugin
    width: label.implicitWidth + 16
    height: label.implicitHeight + 8

    Text {
        id: label
        anchors.centerIn: parent
        text: plugin ? plugin.text : "SDK V2"
        color: themeManager.colors.fg
    }
}
