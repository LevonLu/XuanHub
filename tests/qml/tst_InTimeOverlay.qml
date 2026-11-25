// tst_InTimeOverlay.qml — 双屏休息遮罩的全屏几何和统一退出
import QtQuick
import QtTest
import "../../plugins_py/intime" as InTime

TestCase {
    id: tests
    name: "InTimeOverlay"
    when: windowShown

    QtObject {
        id: worker
        property var values
        property int endCalls: 0
        function call(action, args) {
            compare(action, "end_rest")
            endCalls++
            setResting(false)
        }
    }
    QtObject {
        id: context
        property var colors: ({})
        // 模拟已扣除任务栏的工作区，刻意小于测试主屏。
        property var screenGeometry: ({x: 0, y: 0, width: 960, height: 600})
    }
    Component {
        id: barComponent
        InTime.InTime { plugin: worker; pluginContext: context }
    }

    function setResting(active) {
        worker.values = {status: active ? "resting" : "working", overlayActive: active,
                         remaining: 10, total: 20, timeStr: "00:10",
                         enterDurationMs: 60, exitDurationMs: 60}
    }

    function init() {
        worker.endCalls = 0
        setResting(false)
    }

    function test_rest_data() {
        return [{tag: "timeout", escapeScreen: -1},
                {tag: "escape-primary", escapeScreen: 0},
                {tag: "escape-secondary", escapeScreen: 1}]
    }

    function test_rest(data) {
        var bar = createTemporaryObject(barComponent, tests)
        verify(bar !== null)
        var windows = findChild(bar, "restOverlays")
        verify(windows !== null)
        compare(windows.count, 0)

        setResting(true)
        var screens = Qt.application.screens
        compare(screens.length, 2)
        tryCompare(windows, "count", screens.length)
        for (var i = 0; i < screens.length; ++i) {
            var win = windows.objectAt(i)
            var screen = screens[i]
            tryCompare(win, "visible", true)
            compare(win.visibility, Window.FullScreen)
            compare(win.screen.name, screen.name)
            compare(Qt.rect(win.x, win.y, win.width, win.height),
                    Qt.rect(screen.virtualX, screen.virtualY, screen.width, screen.height))
            verify(win.flags & Qt.WindowStaysOnTopHint)
        }
        tryCompare(bar, "reveal", 1)
        if (data.escapeScreen < 0) {
            setResting(false)
        } else {
            var active = windows.objectAt(data.escapeScreen)
            active.requestActivate()
            tryCompare(active, "active", true)
            keyClick(Qt.Key_Escape)
            tryCompare(worker, "endCalls", 1)
        }
        tryCompare(windows, "count", 0)
    }

    function test_unload_during_rest() {
        setResting(true)
        var bar = createTemporaryObject(barComponent, tests)
        var windows = findChild(bar, "restOverlays")
        tryCompare(windows, "count", 2)
        var primary = windows.objectAt(0)
        var secondary = windows.objectAt(1)
        verify(primary.visible && secondary.visible)
        bar.destroy()
        tryVerify(function() { return !Qt.isQtObject(primary) && !Qt.isQtObject(secondary) })
    }
}
