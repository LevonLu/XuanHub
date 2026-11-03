// XuanHubMenu.qml — bar 品牌图标与宿主快速设置弹窗
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.FluentWinUI3 as Fluent
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext
    width: 40
    height: 40

    readonly property var colors: pluginContext ? pluginContext.colors : ({
            "bg": "#202020",
            "panel": "#2b2b2b",
            "fg": "#ffffff",
            "muted": "#cecece",
            "accent": "#60cdff",
            "danger": "#ff99a4",
            "hover": "#383838",
            "border": "#484848",
            "onAccent": "#202020"
        })
    readonly property var actionService: pluginContext ? pluginContext.actions : null
    readonly property var declaredActions: {
        if (!root.actionService)
            return [];
        // revision 建立动态状态依赖；actions() 返回当前本地化后的快照。
        const currentRevision = root.actionService.revision;
        const filterRevision = root.plugin ? root.plugin.actionFilterRevision : 0;
        if (root.plugin && !root.plugin.pluginActionsEnabled)
            return [];
        const actions = root.actionService.actions("quickMenu");
        return actions.filter(function (action) {
            return action.pluginId !== "xuanhub_menu" && (!root.plugin || root.plugin.isPluginActionEnabled(action.pluginId, action.actionId));
        });
    }

    Rectangle {
        anchors.fill: parent
        radius: 7
        color: iconArea.containsMouse ? root.colors.hover : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
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
        if (plugin)
            plugin.requestState();

        menuWin.toggleFromBar();
    }

    function selectMode(taskbar) {
        // 使用明确的目标值，避免“关闭任务栏模式”隐藏另一个模式的含义。
        if (!plugin || plugin.taskbarMode === taskbar)
            return;
        menuWin.hide();
        plugin.invokeAction("taskbarMode", taskbar);
    }

    function selectTheme(light) {
        if (!plugin || plugin.themeMode === (light ? "light" : "dark"))
            return;
        plugin.invokeAction("lightMode", light);
    }

    component MenuGlyph: Text {
        font.family: "Segoe MDL2 Assets"
        font.pixelSize: 16
        color: root.colors.fg
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Accessible.ignored: true
    }

    component SegmentedSelector: Row {
        id: chooser
        property string firstText
        property string secondText
        property string firstObjectName
        property string secondObjectName
        property bool secondSelected: false
        signal optionChosen(bool second)
        width: parent ? parent.width : 256
        height: 32
        spacing: 4
        enabled: !!root.plugin
        Repeater {
            model: 2
            delegate: Fluent.Button {
                id: segment
                required property int index
                readonly property bool second: index === 1
                // 选中状态只跟随宿主回执；按钮不自行切换，避免覆盖绑定。
                checked: chooser.secondSelected === second
                checkable: false
                objectName: second ? chooser.secondObjectName : chooser.firstObjectName
                width: (chooser.width - chooser.spacing) / 2
                height: chooser.height
                text: second ? chooser.secondText : chooser.firstText
                font.pixelSize: 12
                Accessible.role: Accessible.RadioButton
                Accessible.checkable: true
                Accessible.checked: checked
                onClicked: chooser.optionChosen(second)
            }
        }
    }

    component MenuRow: Fluent.ItemDelegate {
        id: row
        property string title
        property string subtitle: ""
        property string glyph: ""
        property bool showSwitch: false
        property bool valueChecked: false
        property bool dangerous: false
        signal triggered

        width: parent ? parent.width : 248
        height: Math.max(32, implicitContentHeight + 8)
        leftPadding: 8
        rightPadding: 8
        topPadding: 4
        bottomPadding: 4
        text: title
        font.pixelSize: 14
        Accessible.checkable: showSwitch
        Accessible.checked: valueChecked
        onClicked: triggered()

        contentItem: RowLayout {
            spacing: 8
            opacity: row.enabled ? 1 : 0.45
            MenuGlyph {
                Layout.preferredWidth: 16
                text: row.glyph
                visible: text.length > 0
                color: row.dangerous ? root.colors.danger : root.colors.fg
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: row.title
                    color: row.dangerous ? root.colors.danger : root.colors.fg
                    font.family: row.font.family
                    font.pixelSize: row.font.pixelSize
                    font.weight: row.subtitle.length > 0 ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: row.subtitle.length > 0
                    text: row.subtitle
                    color: root.colors.muted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
            Fluent.Switch {
                visible: row.showSwitch
                checked: row.valueChecked
                checkable: false
                focusPolicy: Qt.NoFocus
                Accessible.ignored: true
                onClicked: row.triggered()
            }
        }
    }

    component SectionLabel: Text {
        width: parent ? parent.width : 256
        leftPadding: 8
        topPadding: 4
        bottomPadding: 4
        color: root.colors.muted
        font.pixelSize: 12
    }

    component MenuSeparator: Item {
        width: parent ? parent.width : 256
        height: 5
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 1
            color: Qt.alpha(root.colors.border, 0.65)
        }
    }

    PluginPopup {
        id: menuWin
        objectName: "xuanhubMenuPopup"
        anchorItem: root
        pluginContext: root.pluginContext
        gap: 7
        width: 248
        // 首次布局与动作列表更新都会改变高度，由宿主按最终尺寸重定位后显现。
        height: Math.min(520, menuColumn.implicitHeight + menuSurface.topPadding + menuSurface.bottomPadding + 2)

        Fluent.Pane {
            id: menuSurface
            anchors.fill: parent
            anchors.margins: 1
            padding: 8
            // PluginPopup 是独立窗口，显式注入与设置窗口一致的色板，避免继承系统色。
            palette.window: root.colors.bg
            palette.windowText: root.colors.fg
            palette.base: root.colors.bg
            palette.text: root.colors.fg
            palette.button: root.colors.panel
            palette.buttonText: root.colors.fg
            palette.accent: root.colors.accent
            palette.highlight: root.colors.accent
            palette.highlightedText: root.colors.onAccent
            palette.placeholderText: root.colors.muted
            palette.mid: root.colors.muted
            palette.dark: root.colors.panel
            palette.light: root.colors.panel
            background: Rectangle {
                radius: 8
                color: root.colors.bg
                border.width: 1
                border.color: root.colors.border
            }

            Flickable {
                anchors.fill: parent
                contentWidth: width
                contentHeight: menuColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Fluent.ScrollBar.vertical: Fluent.ScrollBar {}

                Column {
                    id: menuColumn
                    width: parent.width
                    spacing: 1

                    MenuRow {
                        title: qsTranslate("plugin.xuanhub_menu.main", "设置")
                        subtitle: qsTranslate("plugin.xuanhub_menu.main", "管理主题、布局与插件")
                        glyph: "\ue713"
                        onTriggered: {
                            menuWin.hide();
                            if (root.plugin)
                                root.plugin.invokeAction("settings");
                        }
                    }
                    MenuRow {
                        title: qsTranslate("plugin.xuanhub_menu.main", "开机自启")
                        glyph: "\ue7e8"
                        showSwitch: true
                        valueChecked: root.plugin ? root.plugin.autoStartEnabled : false
                        enabled: !!root.plugin
                        onTriggered: if (root.plugin)
                            root.plugin.invokeAction("autostart", !valueChecked)
                    }
                    Column {
                        width: parent.width
                        SectionLabel {
                            text: qsTranslate("plugin.xuanhub_menu.main", "显示模式")
                        }
                        SegmentedSelector {
                            firstObjectName: "menuTaskbarMode"
                            secondObjectName: "menuAppbarMode"
                            firstText: qsTranslate("plugin.xuanhub_menu.main", "任务栏模式")
                            secondText: qsTranslate("plugin.xuanhub_menu.main", "独立状态栏")
                            secondSelected: root.plugin ? !root.plugin.taskbarMode : true
                            onOptionChosen: function (second) {
                                root.selectMode(!second);
                            }
                        }
                        Item {
                            width: parent.width
                            height: 2
                        }
                    }
                    Column {
                        width: parent.width
                        SectionLabel {
                            text: qsTranslate("plugin.xuanhub_menu.main", "外观")
                        }
                        SegmentedSelector {
                            firstObjectName: "menuLightMode"
                            secondObjectName: "menuDarkMode"
                            firstText: qsTranslate("plugin.xuanhub_menu.main", "浅色")
                            secondText: qsTranslate("plugin.xuanhub_menu.main", "深色")
                            secondSelected: root.plugin ? root.plugin.themeMode !== "light" : true
                            onOptionChosen: function (second) {
                                root.selectTheme(!second);
                            }
                        }
                        Item {
                            width: parent.width
                            height: 4
                        }
                    }
                    MenuRow {
                        title: qsTranslate("plugin.xuanhub_menu.main", "重新加载配置")
                        glyph: "\ue777"
                        onTriggered: {
                            menuWin.hide();
                            if (root.plugin)
                                root.plugin.invokeAction("reload");
                        }
                    }

                    MenuSeparator {
                        visible: root.declaredActions.length > 0
                    }

                    Repeater {
                        model: root.declaredActions
                        delegate: MenuRow {
                            required property var modelData
                            title: modelData.label
                            glyph: modelData.icon === "refresh" ? "\ue72c" : "\ue8a5"
                            showSwitch: modelData.checkable
                            valueChecked: modelData.checked
                            enabled: modelData.enabled
                            onTriggered: {
                                menuWin.hide();
                                if (root.actionService)
                                    root.actionService.invoke(modelData.pluginId, modelData.actionId);
                            }
                        }
                    }

                    MenuSeparator {}

                    MenuRow {
                        title: qsTranslate("plugin.xuanhub_menu.main", "退出 XuanHub")
                        glyph: "\ue7e8"
                        dangerous: true
                        onTriggered: {
                            menuWin.hide();
                            if (root.plugin)
                                root.plugin.invokeAction("quit");
                        }
                    }
                }
            }
        }
    }
}
