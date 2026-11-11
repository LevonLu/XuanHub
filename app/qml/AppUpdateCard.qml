// AppUpdateCard.qml — 软件更新卡片，后台状态独立于设置窗口生命周期
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import XuanHub

UiCard {
    id: root
    objectName: "appUpdateCard"
    property var updater: null
    readonly property string updateState: updater ? updater.state : "idle"
    readonly property bool updating: updater ? updater.busy : false
    function trText(source) { return qsTranslate("xuanhub.update", source) }
    function stateText() {
        switch (updateState) {
        case "checking": return trText("正在检查 GitHub 更新…")
        case "available": return trText("发现新版本 %1").arg(updater.latestVersion)
        case "up-to-date": return trText("当前已是最新正式版本")
        case "downloading": return trText("正在下载更新… %1%").arg(Math.floor(updater.progress * 100))
        case "preparing": return trText("正在校验并准备更新…")
        case "cancelling": return trText("正在取消更新…")
        case "restarting": return trText("即将退出并更新，请稍候…")
        case "ignored": return trText("已忽略此版本，手动检查可再次查看")
        case "error": return trText("更新未完成")
        default: return trText("从 GitHub 获取正式版本")
        }
    }
    title: trText("软件更新")
    description: stateText()

    Label {
        Layout.fillWidth: true
        text: root.trText("当前版本：%1").arg(Qt.application.version)
        wrapMode: Text.Wrap
    }
    Label {
        Layout.fillWidth: true
        visible: root.updater && root.updater.error.length > 0
        text: root.updater ? root.updater.error : ""
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: typeof themeManager !== "undefined" ? themeManager.colors.danger : root.palette.text
    }
    ProgressBar {
        objectName: "appUpdateProgress"
        Layout.fillWidth: true
        visible: root.updating
        indeterminate: root.updateState !== "downloading"
        value: root.updater ? root.updater.progress : 0
    }
    UiSwitch {
        objectName: "appUpdateAutomatic"
        text: root.trText("自动检查更新（每天最多一次，立即生效）")
        enabled: !!root.updater && !root.updating
        checked: root.updater ? root.updater.automaticChecks : false
        onToggled: if (root.updater) root.updater.automaticChecks = checked
    }
    Flow {
        Layout.fillWidth: true
        spacing: UiMetrics.space2
        UiButton {
            objectName: "appUpdateCheck"
            text: root.trText("检查软件更新")
            enabled: !!root.updater && !root.updating
            onClicked: root.updater.check()
        }
        UiButton {
            objectName: "appUpdateInstall"
            text: root.trText("立即更新")
            variant: "primary"
            visible: root.updateState === "available"
            enabled: !root.updating
            onClicked: confirm.open()
        }
        UiButton {
            objectName: "appUpdateIgnore"
            text: root.trText("忽略此版本")
            visible: root.updateState === "available"
            onClicked: root.updater.ignoreVersion()
        }
        UiButton {
            objectName: "appUpdateCancel"
            text: root.trText("取消")
            visible: root.updating && root.updateState !== "restarting"
            enabled: root.updateState !== "cancelling"
            onClicked: root.updater.cancel()
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.updater && root.updater.publishedAt.length > 0
        text: root.updater ? root.trText("发布日期：%1").arg(
            Qt.formatDateTime(new Date(root.updater.publishedAt), "yyyy-MM-dd HH:mm")) : ""
        wrapMode: Text.Wrap
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(220, notes.implicitHeight + 12)
        visible: root.updater && root.updater.releaseNotes.length > 0
        clip: true
        TextArea {
            id: notes
            objectName: "appUpdateNotes"
            readOnly: true
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            text: root.updater ? root.updater.releaseNotes : ""
        }
    }
    Dialog {
        id: confirm
        objectName: "appUpdateConfirm"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, parent ? parent.width - 32 : 480)
        modal: true
        title: root.trText("更新并重新启动玄枢？")
        // Popup 是独立窗口，显式继承卡片 palette，避免深色系统色板断裂。
        palette: root.palette
        contentItem: Label {
            text: root.trText("下载和校验完成后，玄枢将退出并重新启动。未保存的设置草稿不会保留，请先保存需要保留的更改。更新失败时将恢复旧版。")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            palette: root.palette
        }
        footer: DialogButtonBox {
            palette: root.palette
            UiButton {
                text: root.trText("取消")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            UiButton {
                objectName: "appUpdateConfirmInstall"
                text: root.trText("立即更新")
                variant: "primary"
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            onAccepted: { confirm.close(); if (root.updater) root.updater.install() }
            onRejected: confirm.close()
        }
    }
}
