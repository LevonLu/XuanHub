// XuanHubMenu.qml — bar 品牌图标与宿主快速设置弹窗
pragma ComponentBehavior: Bound
import QtQuick
import XuanHub

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

    MouseArea {
        id: iconArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.openMenu()
    }

    function openMenu() {
        if (plugin) plugin.requestState()

        menuWin.toggleFromBar()
    }

    function selectMode(taskbar) {
        // 使用明确的目标值，避免“关闭任务栏模式”隐藏另一个模式的含义。
        if (!plugin || plugin.taskbarMode === taskbar) return
        menuWin.hide()
        plugin.invokeAction("taskbarMode", taskbar)
    }

    function selectTheme(light) {
        if (!plugin || plugin.themeMode === (light ? "light" : "dark")) return
        plugin.invokeAction("lightMode", light)
    }

    component SegmentedSelector: Rectangle {
        id: chooser
        property string firstText
        property string secondText
        property string firstObjectName
        property string secondObjectName
        property bool secondSelected: false
        signal optionChosen(bool second)
        width: parent ? parent.width : 256
        height: 38; radius: 10; color: Qt.alpha(root.colors.fg, 0.045)
        enabled: !!root.plugin
        Row {
            anchors.fill: parent; anchors.margins: 3; spacing: 4
            Repeater {
                model: 2
                delegate: FocusScope {
                    id: segment
                    required property int index
                    readonly property bool second: index === 1
                    readonly property bool checked: chooser.secondSelected === second
                    objectName: second ? chooser.secondObjectName : chooser.firstObjectName
                    width: (parent.width - parent.spacing) / 2; height: parent.height
                    activeFocusOnTab: true
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: second ? chooser.secondText : chooser.firstText
                    Accessible.checkable: true
                    Accessible.checked: checked
                    Accessible.onPressAction: chooser.optionChosen(second)
                    Keys.onSpacePressed: chooser.optionChosen(second)
                    Keys.onReturnPressed: chooser.optionChosen(second)
                    Rectangle {
                        anchors.fill: parent; radius: 8
                        color: segmentMouse.pressed ? Qt.alpha(root.colors.accent, 0.16)
                            : segment.checked ? root.colors.bg
                            : segmentMouse.containsMouse ? Qt.alpha(root.colors.accent, 0.08) : "transparent"
                        border.color: segment.activeFocus ? root.colors.accent
                            : segment.checked ? Qt.alpha(root.colors.border, 0.7) : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } }
                    }
                    Text {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                        text: segment.second ? chooser.secondText : chooser.firstText
                        color: segment.checked || segmentMouse.containsMouse ? root.colors.accent : root.colors.fg
                        font.pixelSize: 12; font.weight: segment.checked ? Font.DemiBold : Font.Medium
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: segmentMouse
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: chooser.optionChosen(segment.second)
                    }
                }
            }
        }
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

    PluginPopup {
        id: menuWin
        objectName: "xuanhubMenuPopup"
        anchorItem: root
        pluginContext: root.pluginContext
        gap: 7
        width: 280
        // 首次布局与动作列表更新都会改变高度，由宿主按最终尺寸重定位后显现。
        height: Math.min(520, menuColumn.implicitHeight + 26)

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
            Column {
                width: parent.width; spacing: 6
                Item {
                    width: parent.width; height: 20
                    Text {
                        x: 11; anchors.verticalCenter: parent.verticalCenter
                        text: qsTranslate("plugin.xuanhub_menu.main", "显示模式")
                        color: root.colors.muted; font.pixelSize: 11
                    }
                }
                SegmentedSelector {
                    firstObjectName: "menuTaskbarMode"; secondObjectName: "menuAppbarMode"
                    firstText: qsTranslate("plugin.xuanhub_menu.main", "任务栏模式")
                    secondText: qsTranslate("plugin.xuanhub_menu.main", "独立状态栏")
                    secondSelected: root.plugin ? !root.plugin.taskbarMode : true
                    onOptionChosen: function(second) { root.selectMode(!second) }
                }
                Item { width: parent.width; height: 4 }
            }
            Column {
                width: parent.width; spacing: 6
                Item {
                    width: parent.width; height: 20
                    Text {
                        x: 11; anchors.verticalCenter: parent.verticalCenter
                        text: qsTranslate("plugin.xuanhub_menu.main", "外观")
                        color: root.colors.muted; font.pixelSize: 11
                    }
                }
                SegmentedSelector {
                    firstObjectName: "menuLightMode"; secondObjectName: "menuDarkMode"
                    firstText: qsTranslate("plugin.xuanhub_menu.main", "浅色")
                    secondText: qsTranslate("plugin.xuanhub_menu.main", "深色")
                    secondSelected: root.plugin ? root.plugin.themeMode !== "light" : true
                    onOptionChosen: function(second) { root.selectTheme(!second) }
                }
                Item { width: parent.width; height: 8 }
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
