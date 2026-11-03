// AgentUsage.qml — 紧凑用量状态栏与宿主统一弹窗
import QtQuick
import QtQuick.Controls
import XuanHub
import "TokenFormat.js" as TokenFormat

Item {
    id: root
    property var plugin
    property var pluginContext
    width: Math.max(40, summary.implicitWidth + 28)
    height: 40

    readonly property var values: plugin && plugin.values ? plugin.values : ({})
    readonly property var colors: pluginContext && pluginContext.colors ? pluginContext.colors : ({
        "fg": "#ffffff", "accent": "#60cdff", "bg": "#202020", "panel": "#2b2b2b",
        "muted": "#cecece", "border": "#484848", "hover": "#383838",
        "warning": "#fce100", "danger": "#ff99a4", "success": "#6ccb5f"
    })
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function compact(n) { return TokenFormat.compact(n) }

    Rectangle { anchors.fill: parent; radius: 6; color: root.colors.hover; opacity: mouse.containsMouse ? 0.6 : 0 }
    Rectangle {
        x: 8; anchors.verticalCenter: parent.verticalCenter; width: 5; height: 5; radius: 3
        color: root.values.usageStatus === "ok" ? root.colors.success : root.colors.warning
    }

    BarColumns {
        id: summary
        anchors.left: parent.left; anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        columns: root.values.barColumns || []
        colors: root.colors
    }
    MouseArea {
        id: mouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        // 状态栏太矮，Controls ToolTip 会被约束到栏内并盖住鼠标区域；
        // 用无障碍名称提供操作说明，不在点击入口上叠加提示弹层。
        objectName: "agentUsageButton"
        Accessible.role: Accessible.Button
        Accessible.name: root.tr("查看各 Agent 的 Token 消耗和套餐额度")
        Accessible.onPressAction: popup.toggleFromBar()
        onClicked: popup.toggleFromBar()
    }
    PluginPopup {
        id: popup; anchorItem: root; pluginContext: root.pluginContext
        width: Math.min(420, root.pluginContext && root.pluginContext.screenGeometry ? root.pluginContext.screenGeometry.width - 20 : 420)
        height: Math.min(popupContent.item ? popupContent.item.implicitHeight : 140, 620,
            root.pluginContext && root.pluginContext.screenGeometry ? root.pluginContext.screenGeometry.height - 40 : 620)
        // 只保留轻量窗口和宿主状态机；隐藏时卸载详情，重开后由宿主按最终尺寸重新定位。
        Loader {
            id: popupContent
            anchors.fill: parent
            active: popup.visible
            sourceComponent: TokenPanel {
                plugin: root.plugin; pluginContext: root.pluginContext
                values: root.values; colors: root.colors
            }
        }
    }
}
