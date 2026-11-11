// tst_AppUpdateCard.qml — 检查更新、确认、取消与错误状态的真实 QML 交互
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtTest
import XuanHub

TestCase {
    id: tests
    name: "AppUpdateCard"
    when: windowShown
    width: 560
    height: 800
    visible: true
    QtObject {
        id: backend
        property string state: "idle"
        property string error: ""
        property string latestVersion: "0.4.5"
        property string releaseNotes: "修复问题\n新增功能 <b>按普通文本展示</b>"
        property string publishedAt: "2026-11-10T01:00:00Z"
        property real progress: 0.42
        property bool automaticChecks: true
        property bool busy: state === "downloading" || state === "preparing" || state === "restarting"
        property int checkCalls: 0
        property int installCalls: 0
        property int cancelCalls: 0
        function check() { ++checkCalls; state = "available" }
        function install() { ++installCalls; state = "downloading" }
        function cancel() { ++cancelCalls; state = "idle" }
        function ignoreVersion() { state = "ignored" }
    }
    Component {
        id: cardComponent
        AppUpdateCard { width: 500; updater: backend }
    }
    function init() {
        backend.state = "idle"
        backend.error = ""
        backend.checkCalls = 0
        backend.installCalls = 0
        backend.cancelCalls = 0
    }
    function test_confirm_required_before_install_and_cancel_is_available() {
        const card = createTemporaryObject(cardComponent, tests)
        verify(waitForPolish(card.Window.window))
        mouseClick(findChild(card, "appUpdateCheck"))
        compare(backend.checkCalls, 1)
        tryCompare(findChild(card, "appUpdateInstall"), "visible", true)
        verify(waitForPolish(card.Window.window))
        mouseClick(findChild(card, "appUpdateInstall"))
        const dialog = findChild(card, "appUpdateConfirm")
        tryCompare(dialog, "visible", true)
        compare(backend.installCalls, 0)
        mouseClick(findChild(card, "appUpdateConfirmInstall"))
        compare(backend.installCalls, 1)
        compare(findChild(card, "appUpdateProgress").visible, true)
        tryCompare(findChild(card, "appUpdateConfirm"), "visible", false)
        verify(waitForPolish(card.Window.window))
        mouseClick(findChild(card, "appUpdateCancel"))
        compare(backend.cancelCalls, 1)
        backend.state = "restarting"
        compare(findChild(card, "appUpdateCancel").visible, false)
    }
    function test_errors_and_long_notes_do_not_disable_retry() {
        backend.state = "error"
        backend.error = "网络连接失败，请稍后重试"
        const card = createTemporaryObject(cardComponent, tests, {width: 340})
        verify(waitForPolish(card.Window.window))
        compare(findChild(card, "appUpdateCheck").enabled, true)
        compare(findChild(card, "appUpdateNotes").textFormat, TextEdit.PlainText)
        mouseClick(findChild(card, "appUpdateCheck"))
        compare(backend.checkCalls, 1)
        tryCompare(findChild(card, "appUpdateIgnore"), "visible", true)
        verify(waitForPolish(card.Window.window))
        mouseClick(findChild(card, "appUpdateIgnore"))
        compare(backend.state, "ignored")
    }
}
