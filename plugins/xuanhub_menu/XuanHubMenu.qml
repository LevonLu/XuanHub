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
                                                : ({
        "bg": "#1e1e2e", "panel": "#313244", "fg": "#cdd6f4", "muted": "#7f849c",
        "accent": "#89b4fa", "danger": "#f38ba8", "hover": "#45475a",
        "border": "#45475a", "onAccent": "#1e1e2e"
    })
    readonly property var actionService: pluginContext ? pluginContext.actions : null
    property bool menuOpenOnBarPress: false
    readonly property var declaredActions: {
        if (!root.actionService) return []
        // revision 建立动态状态依赖；actions() 返回当前本地化后的快照。
        const currentRevision = root.actionService.revision
        const filterRevision = root.plugin ? root.plugin.actionFilterRevision : 0
        if (root.plugin && !root.plugin.pluginActionsEnabled) return []
        const actions = root.actionService.actions("quickMenu")
        return actions.filter(function(action) {
            return action.pluginId !== "xuanhub_menu"
                && (!root.plugin
                    || root.plugin.isPluginActionEnabled(action.pluginId, action.actionId))
        })
    }

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
    }

    Connections {
        target: root.pluginContext || null
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
                                 : null
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
        height: subtitle.length > 0 ? 46 : 38
        opacity: enabled ? 1.0 : 0.45

        Rectangle {
            anchors.fill: parent
            radius: 5
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
            anchors.rightMargin: 0
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

        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: row.enabled
            cursorShape: row.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: row.triggered()
        }
    }

    Window {
        id: menuWin
        // taskbar 模式下 bar 是 Shell_TrayWnd 的子窗口，不能作为 transient parent。
        transientParent: null
        visible: false
        width: 240
        height: Math.min(520, menuColumn.implicitHeight + 26)
        color: "transparent"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint

        onActiveChanged: if (!active && visible) hide()
        Shortcut { sequence: "Escape"; onActivated: menuWin.hide() }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 6
            color: root.colors.bg
            border.width: 1
            border.color: root.colors.border
        }

        Flickable {
            anchors.fill: parent
            anchors.margins: 12
            contentWidth: width
            contentHeight: menuColumn.implicitHeight
            clip: true

        Column {
            id: menuColumn
            width: parent.width
            spacing: 1

            MenuRow {
                title: qsTranslate("plugin.xuanhub_menu.main", "设置")
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

            Rectangle {
                visible: root.declaredActions.length > 0
                width: parent.width
                height: visible ? 1 : 0
                color: root.colors.border
            }

            Repeater {
                model: root.declaredActions
                delegate: MenuRow {
                    required property var modelData
                    title: modelData.label
                    checkable: modelData.checkable
                    checked: modelData.checked
                    enabled: modelData.enabled
                    onTriggered: {
                        menuWin.hide()
                        if (root.actionService)
                            root.actionService.invoke(modelData.pluginId, modelData.actionId)
                    }
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
}
