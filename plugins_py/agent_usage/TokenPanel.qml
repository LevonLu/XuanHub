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
    implicitHeight: page === 0
        ? (tools.item ? tools.item.implicitHeight : 140) + 108
        : Math.min(620, Math.max(220, (accountsView.item ? accountsView.item.contentHeight : 0) + 140))
    color: colors.bg; radius: 16; border.color: Qt.alpha(colors.border, 0.8)
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    RowLayout {
        id: header
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.topMargin: 18; anchors.leftMargin: 20; anchors.rightMargin: 16
        height: 32; spacing: 12
        Text {
            text: root.tr("Token Usage"); color: root.colors.fg
            font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true
        }
        PopupAction {
            objectName: "tokenRefreshButton"
            text: root.tr("刷新"); iconText: "↻"; colors: root.colors; quiet: true
            onClicked: if (root.plugin) root.plugin.call("refresh", [])
        }
    }
    Rectangle {
        id: tabs
        anchors.top: header.bottom; anchors.topMargin: 14
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 20; anchors.rightMargin: 20
        height: 38; radius: 10; color: Qt.alpha(root.colors.fg, 0.045)
        RowLayout {
            anchors.fill: parent; anchors.margins: 3; spacing: 4
            PopupAction {
                Layout.fillWidth: true; Layout.fillHeight: true
                text: root.tr("工具用量"); colors: root.colors; quiet: true
                selected: root.page === 0; onClicked: root.page = 0
            }
            PopupAction {
                Layout.fillWidth: true; Layout.fillHeight: true
                text: root.tr("账户额度"); colors: root.colors; quiet: true
                selected: root.page === 1; onClicked: root.page = 1
            }
        }
    }
    Loader {
        id: tools; active: root.page === 0
        anchors.top: tabs.bottom; anchors.bottom: parent.bottom; width: parent.width
        sourceComponent: UsagePanel {
            plugin: root.plugin; pluginContext: root.pluginContext; values: root.values; colors: root.colors
            showHeader: false; framed: false
            onRefreshRequested: if (root.plugin) root.plugin.call("refresh", [])
        }
    }
    Loader {
        id: accountsView
        active: root.page === 1; anchors.top: tabs.bottom; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        anchors.topMargin: 16; anchors.bottomMargin: 20; anchors.leftMargin: 20; anchors.rightMargin: 20
        sourceComponent: AccountPanel {
            plugin: root.plugin; values: root.values; colors: root.colors
        }
    }
}
