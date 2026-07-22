// Launcher.qml — 快捷启动器
import QtQuick

Item {
    id: root
    property var plugin        // LauncherViewModel*

    width: childrenRect.width
    height: 40

    // hover 反馈用色（字符串 → color 属性转换后才能拆通道）
    property color hoverColor: themeManager ? themeManager.colors.hover : "#45475a"

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        Repeater {
            model: plugin        // QAbstractListModel — roleNames: name/program/icon
            delegate: Item {
                implicitWidth: label.implicitWidth + 16
                implicitHeight: 26

                // hover 圆角底色：提示可点击
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: Qt.rgba(root.hoverColor.r, root.hoverColor.g, root.hoverColor.b,
                                   ma.containsMouse ? 0.35 : 0)
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: model.name
                    color: themeManager ? themeManager.colors.accent : "#89b4fa"
                    font.pixelSize: 14
                }
                MouseArea {
                    id: ma
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: plugin.launch(index)
                }
            }
        }
    }
}
