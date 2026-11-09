// LandscapeController.qml — 两处画面共用的低频时钟与有限流星调度
import QtQuick

QtObject {
    id: controller
    property var backend
    property bool shown: false
    property bool enabled: false
    property string phase: "day"
    property string weather: "clear"
    property bool rareEventsEnabled: true
    readonly property bool running: shown && enabled
    property double animationTime: 0
    property real blend: 1
    property double lastTick: 0
    property double nextMeteor: 600
    property double meteorStart: -100
    property int randomState: 713
    readonly property real meteor: phase === "night" && weather === "clear" && rareEventsEnabled
                                  && animationTime - meteorStart < 1.6
                                  ? (animationTime - meteorStart) / 1.6 : -1
    function scheduleMeteor() {
        randomState = (randomState * 16807) % 2147483647
        nextMeteor = animationTime + 480 + (randomState % 240)
        meteorStart = -100
    }
    function resetTransition() {
        blend = backend && backend.transitionAllowed && running ? 0 : 1
    }
    function advance(now) {
        const elapsed = now - lastTick
        lastTick = now
        if (elapsed < 0 || elapsed > 2000) {
            blend = 1
            scheduleMeteor()
            if (backend && backend.resync) backend.resync()
            return
        }
        animationTime += Math.max(0, elapsed) / 1000
        if (blend < 1) blend = Math.min(1, blend + elapsed / 20000)
        if (animationTime >= nextMeteor) {
            scheduleMeteor()
            if (phase === "night" && weather === "clear" && rareEventsEnabled)
                meteorStart = animationTime
        }
    }
    onRunningChanged: {
        lastTick = Date.now()
        if (!running) blend = 1
    }
    onShownChanged: {
        if (shown && backend && backend.resync) backend.resync()
        // 隐藏期间取消未完流星；恢复后重新等待，避免出现历史事件。
        scheduleMeteor()
    }
    onPhaseChanged: scheduleMeteor()
    onWeatherChanged: scheduleMeteor()
    onRareEventsEnabledChanged: scheduleMeteor()
    property Connections backendConnections: Connections {
        target: controller.backend || null
        ignoreUnknownSignals: true
        function onPhaseChanged() { controller.resetTransition() }
    }
    property Timer clock: Timer {
        interval: 100
        repeat: true
        running: controller.running
        onTriggered: controller.advance(Date.now())
    }
}
