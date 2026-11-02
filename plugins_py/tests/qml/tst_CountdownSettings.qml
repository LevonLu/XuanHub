// tst_CountdownSettings.qml — 草稿注入、提示编辑和模板按钮回归
import QtQuick
import QtTest
import "../../countdown" as Countdown

TestCase {
    id: tests
    name: "CountdownSettings"
    when: windowShown
    visible: true
    width: 760; height: 1600
    property var page
    QtObject {
        id: draft
        property string pluginId: "py_countdown"
        property int revision: 0
        property int writes: 0
        property var values: ({})
        function value(key) { return values[key] }
        function setValue(key, value) {
            verify(pluginId === "py_countdown", "不得写入其他插件草稿")
            if (values[key] === value) return
            var next = Object.assign({}, values)
            next[key] = value
            values = next
            writes++
            revision++
        }
    }
    Component {
        id: settingsComponent
        Countdown.CountdownSettings { width: tests.width }
    }
    function init() {
        draft.pluginId = "py_countdown"
        draft.values = {mode: "daily", targetTime: "18:00:33", prefix: "原前缀", suffix: "原后缀",
                        tooltipText: "已保存的提示", paydayDay: 15}
        draft.writes = 0
        draft.revision++
        page = createTemporaryObject(settingsComponent, tests)
        verify(page !== null)
        page.plugin = draft
        verify(waitForRendering(page))
    }
    function test_injection_preserves_existing_config() {
        compare(draft.value("mode"), "daily")
        compare(draft.value("targetTime"), "18:00:33")
        compare(draft.value("prefix"), "原前缀")
        compare(findChild(page, "countdownTooltipEditor").text, "已保存的提示")
        var writes = draft.writes
        wait(100)
        compare(draft.writes, writes)
    }
    function test_template_button_and_multiline_edit() {
        var button = findChild(page, "countdownPaydayExample")
        verify(button !== null)
        mouseClick(button, 20, 15)
        verify(draft.value("tooltipText").indexOf("{payday}") >= 0)
        verify(draft.value("tooltipText").indexOf("{paydayDays}") >= 0)
        var editor = findChild(page, "countdownTooltipEditor")
        compare(editor.text, draft.value("tooltipText"))
        editor.text = "自定义\n第二行 {payday}"
        compare(draft.value("tooltipText"), "自定义\n第二行 {payday}")
        compare(draft.value("prefix"), "原前缀")
        compare(draft.value("paydayDay"), 15)
    }
    function test_proxy_switch_cannot_write_other_plugin() {
        draft.pluginId = "py_other"
        var writes = draft.writes
        findChild(page, "countdownTooltipEditor").text = "过期页面写入"
        compare(draft.writes, writes)
        compare(draft.value("tooltipText"), "已保存的提示")
    }
}
