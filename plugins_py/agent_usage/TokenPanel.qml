// TokenPanel.qml — 工具用量与账户额度的统一弹窗
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "tokenUsagePanel"
    property var plugin
    property var pluginContext
    property var values: ({})
    property var colors
    property int page: 0
    implicitHeight: page === 0 ? tools.implicitHeight + 42 : Math.min(570, Math.max(140, accountsView.contentHeight + 64))
    color: colors.bg; radius: 12; border.color: colors.border
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    Row {
        id: tabs; anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; height: 40; spacing: 8
        Button { text: root.tr("工具用量"); highlighted: root.page === 0; onClicked: root.page = 0 }
        Button { text: root.tr("账户额度"); highlighted: root.page === 1; onClicked: root.page = 1 }
        Button { text: root.tr("刷新"); onClicked: if (root.plugin) root.plugin.call("refresh", []) }
    }
    UsagePanel {
        id: tools; visible: root.page === 0
        anchors.top: tabs.bottom; anchors.bottom: parent.bottom; width: parent.width
        plugin: root.plugin; pluginContext: root.pluginContext; values: root.values; colors: root.colors
        onRefreshRequested: if (root.plugin) root.plugin.call("refresh", [])
    }
    AccountPanel {
        id: accountsView
        visible: root.page === 1; anchors.top: tabs.bottom; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 12
        plugin: root.plugin; values: root.values; colors: root.colors
    }
}
