// tst_CountdownTooltip.qml — 提示模板、悬停延迟和非交互窗口回归
import QtQuick
import QtQuick.Window
import QtTest
import "../../countdown" as Countdown

TestCase {
    id: tests
    name: "CountdownTooltip"
    when: windowShown
    visible: true
    width: 500; height: 160
    property var bar
    property var tip

    QtObject {
        id: fake
        property var values: ({})
    }
    QtObject {
        id: placement
        property int calls: 0
        function calculate(anchor, screen, width, height, gap, edge) {
            calls++
            return {x: screen.x + screen.width - width,
                    y: edge === "bottom" ? screen.y + screen.height - height - gap : screen.y + gap}
        }
        function activate() { fail("悬停提示不得抢占交互弹窗") }
    }
    QtObject {
        id: context
        property var popupService: placement
        property var colors: ({fg: "#cdd6f4", panel: "#313244", border: "#45475a"})
        property var screenGeometry: ({x: 0, y: 0, width: 800, height: 600})
        property string barEdge: "top"
        signal barPressed()
    }
    Component {
        id: barComponent
        Countdown.Countdown { plugin: fake; pluginContext: context }
    }
    function init() {
        mouseMove(tests, 490, 150)
        context.barEdge = "top"
        fake.values = {tooltipTemplate: "下次 {payday}，还有 {paydayDays} 天", paydayStatus: "ready",
                       paydayDate: "2026-11-10", paydayDays: 8, remainingSeconds: 3600}
        placement.calls = 0
        bar = createTemporaryObject(barComponent, tests)
        verify(bar !== null)
        verify(waitForRendering(bar))
        tip = findChild(bar, "countdownTooltip")
        verify(tip !== null)
    }
    function showTip() {
        mouseMove(bar, 20, 20)
        tryCompare(tip, "visible", true, 1500)
        tryCompare(tip, "opacity", 1)
    }
    function cleanup() {
        mouseMove(tests, 490, 150)
        tip.hide()
    }
    function test_template_plain_text_and_unknown_tokens() {
        compare(bar.tooltipText, "下次 2026-11-10，还有 8 天")
        fake.values = {tooltipTemplate: "<b>原样</b>\n{unknown} / {payday} / {payday}",
                       paydayStatus: "ready", paydayDate: "2026-11-10", paydayDays: 8}
        compare(bar.tooltipText, "<b>原样</b>\n{unknown} / 2026-11-10 / 2026-11-10")
        compare(findChild(bar, "countdownTooltipLabel").textFormat, Text.PlainText)
    }
    function test_hover_delay_leave_and_flags() {
        mouseMove(bar, 20, 20)
        wait(180)
        compare(tip.visible, false)
        tryCompare(tip, "visible", true, 1500)
        tryCompare(tip, "opacity", 1)
        verify((tip.flags & Qt.WindowDoesNotAcceptFocus) !== 0)
        verify((tip.flags & Qt.WindowTransparentForInput) !== 0)
        verify(placement.calls > 0)
        mouseMove(tests, 490, 150)
        tryCompare(tip, "visible", false)
    }
    function test_empty_and_hot_update() {
        showTip()
        fake.values = {tooltipTemplate: "新提示"}
        compare(bar.tooltipText, "新提示")
        compare(tip.visible, true)
        fake.values = {tooltipTemplate: "  "}
        tryCompare(tip, "visible", false)
        mouseMove(tests, 490, 150)
        mouseMove(bar, 20, 20)
        wait(500)
        compare(tip.visible, false)
    }
    function test_unknown_calendar_is_visible_to_user() {
        fake.values = {tooltipTemplate: "工资 {payday} / {paydayDays}",
                       paydayStatus: "calendarUnavailable", paydayCalendarYear: 2027}
        verify(bar.tooltipText.indexOf("待确认") >= 0)
        verify(bar.tooltipText.indexOf("2027") >= 0)
        verify(bar.tooltipText.indexOf("{payday}") < 0)
    }
    function test_bottom_bar_and_long_text_reposition() {
        context.barEdge = "bottom"
        showTip()
        var callsBefore = placement.calls
        fake.values = {tooltipTemplate: Array(80).join("很长的多行提示\n")}
        tryVerify(function() { return placement.calls > callsBefore })
        verify(tip.width <= 440)
        verify(tip.height <= context.screenGeometry.height - 16)
        verify(tip.y + tip.height <= context.screenGeometry.height)
        context.barPressed()
        tryCompare(tip, "visible", false)
    }
}
