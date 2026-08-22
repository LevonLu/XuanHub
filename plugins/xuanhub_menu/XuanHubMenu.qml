// XuanHubMenu.qml — bar 品牌图标与宿主快速设置弹窗
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

Item {
    id: root
    property var plugin
    property var pluginContext
    width: 40
    height: 40

    readonly property var colors: pluginContext ? pluginContext.colors
                                                : (themeManager ? themeManager.colors : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "hover": "#45475a",
        "border": "#45475a", "onAccent": "#1e1e2e"
    }))
    property bool menuOpenOnBarPress: false

    Rectangle {
        anchors.fill: parent
        radius: 7
        color: iconArea.containsMouse ? root.colors.hover : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    XuanHubGlyph {
        anchors.centerIn: parent
        width: 28
        height: 28
        glyphColor: root.colors.fg
        centerColor: root.colors.panel
    }

    Connections {
        target: pluginContext ? pluginContext
                              : (typeof barWindow !== "undefined" ? barWindow : null)
        function onBarPressed() {
            root.menuOpenOnBarPress = menuWin.visible
            menuWin.hide()
        }
    }

    MouseArea {
        id: iconArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (root.menuOpenOnBarPress) {
                root.menuOpenOnBarPress = false
                return
            }
            root.openMenu()
        }
    }

    function openMenu() {
        if (plugin) plugin.requestState()

        const gap = 7
        const anchorBelow = root.mapToGlobal(Qt.point(root.width / 2, root.height + gap))
        let x = Math.round(anchorBelow.x - menuWin.width / 2)
        let y = Math.round(anchorBelow.y)
        const sg = pluginContext ? pluginContext.screenGeometry
                                 : (typeof barScreenGeometry !== "undefined"
                                    ? barScreenGeometry : null)
        if (sg && sg.width > 0) {
            const right = sg.x + sg.width
            const bottom = sg.y + sg.height
            if (x + menuWin.width > right) x = Math.round(right - menuWin.width)
            if (x < sg.x) x = sg.x
            if (y + menuWin.height > bottom) {
                const anchorAbove = root.mapToGlobal(Qt.point(root.width / 2, -gap))
                y = Math.round(anchorAbove.y - menuWin.height)
            }
            if (y < sg.y) y = sg.y
        }
        menuWin.x = x
        menuWin.y = y
        menuWin.show()
        menuWin.raise()
        menuWin.requestActivate()
    }

    component MenuRow: Item {
        id: row
        property string title
        property string subtitle: ""
        property bool checkable: false
        property bool checked: false
        property bool dangerous: false
        signal triggered()

        width: parent ? parent.width : 248
        height: subtitle.length > 0 ? 52 : 44

        Rectangle {
            anchors.fill: parent
            radius: 7
            color: rowArea.containsMouse ? root.colors.hover : "transparent"
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                text: row.title
                color: row.dangerous ? root.colors.danger : root.colors.fg
                font.pixelSize: 14
            }
            Text {
                visible: row.subtitle.length > 0
                text: row.subtitle
                color: root.colors.muted
                font.pixelSize: 11
            }
        }

        Rectangle {
            visible: row.checkable
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 20
            radius: 10
            color: row.checked ? root.colors.accent : root.colors.border
            Rectangle {
                width: 16
                height: 16
                radius: 8
                y: 2
                x: row.checked ? 18 : 2
                color: row.checked ? root.colors.onAccent : root.colors.fg
                Behavior on x { NumberAnimation { duration: 100 } }
            }
        }

        Text {
            visible: !row.checkable
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "›"
            color: row.dangerous ? root.colors.danger : root.colors.muted
            font.pixelSize: 20
        }

        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.triggered()
        }
    }

    Window {
        id: menuWin
        // taskbar 模式下 bar 是 Shell_TrayWnd 的子窗口，不能作为 transient parent。
        transientParent: null
        visible: false
        width: 280
        height: 364
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        onActiveChanged: if (!active && visible) hide()
        Shortcut { sequence: "Escape"; onActivated: menuWin.hide() }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 10
            color: root.colors.bg
            border.width: 1
            border.color: root.colors.border
        }

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 2

            Item {
                width: parent.width
                height: 50
                XuanHubGlyph {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 34
                    height: 34
                    glyphColor: root.colors.fg
                    centerColor: root.colors.panel
                }
                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 44
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        text: qsTranslate("plugin.xuanhub_menu.main", "玄枢 XuanHub")
                        color: root.colors.fg; font.pixelSize: 15; font.bold: true
                    }
                    Text {
                        text: Qt.application.version.length > 0
                              ? qsTranslate("plugin.xuanhub_menu.main", "快速设置")
                                + " · v" + Qt.application.version
                              : qsTranslate("plugin.xuanhub_menu.main", "快速设置")
                        color: root.colors.muted
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle { width: parent.width; height: 1; color: root.colors.border }

            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "打开设置")
                subtitle: qsTranslate("plugin.xuanhub_menu.main", "管理主题、布局与插件")
                onTriggered: {
                    menuWin.hide()
                    if (root.plugin) root.plugin.invokeAction("settings")
                }
            }
            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "开机自启")
                checkable: true
                checked: root.plugin ? root.plugin.autoStartEnabled : false
                onTriggered: if (root.plugin) root.plugin.invokeAction("autostart", !checked)
            }
            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "任务栏模式")
                checkable: true
                checked: root.plugin ? root.plugin.taskbarMode : false
                onTriggered: {
                    menuWin.hide()
                    if (root.plugin) root.plugin.invokeAction("taskbarMode", !checked)
                }
            }
            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "浅色模式")
                checkable: true
                checked: root.plugin ? root.plugin.themeMode === "light" : false
                onTriggered: if (root.plugin) root.plugin.invokeAction("lightMode", !checked)
            }
            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "重新加载配置")
                onTriggered: {
                    menuWin.hide()
                    if (root.plugin) root.plugin.invokeAction("reload")
                }
            }

            Rectangle { width: parent.width; height: 1; color: root.colors.border }

            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "退出 XuanHub")
                dangerous: true
                onTriggered: {
                    menuWin.hide()
                    if (root.plugin) root.plugin.invokeAction("quit")
                }
            }
        }
    }
}
