// AccountPanel.qml — 独立于工具筛选的账户余额与套餐额度列表
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root
    property var plugin
    property var values: ({})
    property var colors
    contentWidth: width; contentHeight: cards.implicitHeight
    clip: true; boundsBehavior: Flickable.StopAtBounds
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function currency(b) { return (b.currency === "CNY" ? "¥" : b.currency === "USD" ? "$" : b.currency) }
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    ColumnLayout {
        id: cards; width: parent.width; spacing: 8
        Repeater {
            model: root.values.accounts || []
            delegate: Rectangle {
                id: card
                required property var modelData
                Layout.fillWidth: true; implicitHeight: details.implicitHeight + 24
                radius: 8; color: root.colors.panel
                ColumnLayout {
                    id: details; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12; spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: card.modelData.name; color: root.colors.fg; font.bold: true; Layout.fillWidth: true; textFormat: Text.PlainText }
                        Button { text: root.tr("刷新"); onClicked: if (root.plugin) root.plugin.call("refresh_account", [card.modelData.id]) }
                    }
                    Label { visible: !!card.modelData.plan; text: card.modelData.plan || ""; color: root.colors.muted; textFormat: Text.PlainText }
                    Label {
                        visible: card.modelData.subscribed !== undefined
                        text: card.modelData.subscribed ? root.tr("已订阅 OpenCode Go") : root.tr("未订阅")
                        color: root.colors.fg
                    }
                    Label {
                        visible: card.modelData.useBalance !== undefined
                        text: root.tr("Zen 余额兜底") + ": " + (card.modelData.useBalance ? root.tr("开启") : root.tr("关闭")); color: root.colors.muted
                    }
                    Repeater {
                        model: card.modelData.balances || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 4
                            Label { text: root.tr("余额") + " " + root.currency(modelData) + Number(modelData.balance).toFixed(2); color: root.colors.fg; font.pixelSize: 18 }
                            Label { text: root.tr("今日") + " ≈" + root.currency(modelData) + (modelData.spendToday === undefined ? "—" : Number(modelData.spendToday).toFixed(2)); color: root.colors.fg }
                            Label { text: root.tr("余额变化估算") + (modelData.since ? " · " + root.tr("自本次采样起") + " " + modelData.since.replace("T", " ") : ""); color: root.colors.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Label { visible: !!modelData.affected; text: root.tr("余额增加，统计受充值或退款等影响"); color: root.colors.warning; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                    Repeater {
                        model: card.modelData.windows || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 3
                            Label { text: (modelData.label ? root.tr(modelData.label) : modelData.seconds === 18000 ? "5h" : modelData.seconds === 604800 ? "7d" : root.tr("额度窗口")) + "  " + Number(modelData.used).toFixed(1) + "%"; color: root.colors.fg }
                            ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: modelData.used }
                            Label { visible: !!modelData.detail; text: modelData.detail || ""; color: root.colors.muted; textFormat: Text.PlainText }
                            Label { text: modelData.resetAt ? root.tr("重置") + " " + new Date(modelData.resetAt).toLocaleString(Qt.locale()) : root.tr("重置时间未知"); color: root.colors.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                    Label {
                        visible: card.modelData.status !== "ok"
                        text: root.tr(({not_configured: "未配置凭据", reauth: "凭据已过期，请重新登录", unavailable: "账户暂时无法查询", no_quota: "此账号未返回额度窗口", disabled: "套餐额度采集已关闭"})[card.modelData.status] || "账户暂时无法查询")
                        color: root.colors.warning; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Label {
                        text: (card.modelData.stale ? root.tr("上次缓存") + " · " : "") + (card.modelData.capturedAt ? new Date(card.modelData.capturedAt).toLocaleString(Qt.locale()) : "")
                        color: card.modelData.stale ? root.colors.warning : root.colors.muted
                    }
                }
            }
        }
        Label { visible: !(root.values.accounts || []).length; text: root.tr("尚无账户，请在设置中添加或登录对应工具。"); color: root.colors.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { visible: !!root.values.accountError; text: root.tr("账户暂时无法查询"); color: root.colors.warning }
    }
}
