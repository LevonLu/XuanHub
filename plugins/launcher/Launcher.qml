// Launcher.qml — 快捷启动器
import QtQuick

Item {
    id: root
    property var plugin        // LauncherViewModel*

    width: childrenRect.width
    height: 40

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12

        Repeater {
            model: plugin        // QAbstractListModel — roleNames: name/program/icon
            delegate: Text {
                text: model.name
                color: themeManager ? themeManager.colors.accent : "#89b4fa"
                font.pixelSize: 14

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton
                    onClicked: plugin.launch(index)
                }
            }
        }
    }
}
