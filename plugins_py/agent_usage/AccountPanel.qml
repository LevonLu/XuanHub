// AccountPanel.qml — 独立于工具筛选的账户余额与套餐额度列表
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root
    objectName: "tokenAccountPanel"
    property var plugin
    property var values: ({})
    property var colors
    // 自动探测可能仅凭软件目录就标记 configured；额度页只列出已确认的账户，
    // 筛选留在视图内，避免改变状态栏列解析及后台采集、重试。
    readonly property var visibleAccounts: (values.accounts || []).filter(function(account) {
        if (account.configured === false || account.status === "not_configured") return false
        if (!account.automatic) return true
        return (account.windows || []).length > 0 || (account.balances || []).length > 0
            || !!account.plan || (account.configured && (account.status === "ok" || account.status === "no_quota"))
    })
    contentWidth: width; contentHeight: cards.implicitHeight
    clip: true; boundsBehavior: Flickable.StopAtBounds
    function tr(s) { return qsTranslate("plugin.py_agent_usage.main", s) }
    function currency(b) { return (b.currency === "CNY" ? "¥" : b.currency === "USD" ? "$" : b.currency) }
    ScrollBar.vertical: ScrollBar {
        width: 4; policy: ScrollBar.AsNeeded
        contentItem: Rectangle { implicitWidth: 4; radius: 2; color: root.colors.muted; opacity: 0.5 }
        background: Item {}
    }
    ColumnLayout {
        id: cards; width: parent.width - (root.contentHeight > root.height ? 8 : 0); spacing: 10
        Repeater {
            objectName: "accountCards"
            model: root.visibleAccounts
            delegate: Rectangle {
                id: card
                required property var modelData
                objectName: "accountCard_" + modelData.id
                Layout.fillWidth: true; implicitHeight: details.implicitHeight + 32
                radius: 12; color: Qt.alpha(root.colors.fg, 0.025)
                border.color: Qt.alpha(root.colors.border, 0.65)
                ColumnLayout {
                    id: details; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: card.modelData.name; color: root.colors.fg; font.weight: Font.DemiBold; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
                        PopupAction { text: root.tr("刷新"); iconText: "↻"; colors: root.colors; quiet: true; onClicked: if (root.plugin) root.plugin.call("refresh_account", [card.modelData.id]) }
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
                            Label { text: root.tr("余额") + " " + root.currency(modelData) + Number(modelData.balance).toFixed(2); color: root.colors.fg; font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { text: root.tr("今日") + " ≈" + root.currency(modelData) + (modelData.spendToday === undefined ? "—" : Number(modelData.spendToday).toFixed(2)); color: root.colors.fg }
                            Label { text: root.tr("余额变化估算") + (modelData.since ? " · " + root.tr("自本次采样起") + " " + modelData.since.replace("T", " ") : ""); color: root.colors.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Label { visible: !!modelData.affected; text: root.tr("余额增加，统计受充值或退款等影响"); color: root.colors.warning; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }
                    Repeater {
                        model: card.modelData.windows || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: modelData.label ? root.tr(modelData.label) : modelData.seconds === 18000 ? "5h" : modelData.seconds === 604800 ? "7d" : root.tr("额度窗口"); color: root.colors.muted; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: Number(modelData.used).toFixed(1) + "%"; color: modelData.used >= 90 ? root.colors.danger : root.colors.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                            }
                            Rectangle {
                                Layout.fillWidth: true; implicitHeight: 5; radius: 3; color: Qt.alpha(root.colors.accent, 0.12)
                                Rectangle {
                                    height: parent.height; radius: 3
                                    width: parent.width * Math.max(0, Math.min(100, modelData.used)) / 100
                                    color: modelData.used >= 90 ? root.colors.danger : root.colors.accent
                                }
                            }
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
                        Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10
                        visible: !!card.modelData.capturedAt || !!card.modelData.stale
                        text: (card.modelData.stale ? root.tr("上次缓存") + " · " : "") + (card.modelData.capturedAt ? new Date(card.modelData.capturedAt).toLocaleString(Qt.locale()) : "")
                        color: card.modelData.stale ? root.colors.warning : root.colors.muted
                    }
                }
            }
        }
        Label { objectName: "accountEmptyState"; visible: !root.visibleAccounts.length; text: root.tr("尚无账户，请在设置中添加或登录对应工具。"); color: root.colors.muted; font.pixelSize: 12; topPadding: 24; bottomPadding: 24; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { visible: !!root.values.accountError; text: root.tr("账户暂时无法查询"); color: root.colors.warning }
    }
}
