// PixelLandscape.qml — 宽幅风景条与按需创建的共享欣赏弹窗
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import XuanHub

Item {
    id: root
    property var plugin
    property var pluginContext
    property real hostContentWidth: -1
    property real hostContentHeight: -1
    implicitWidth: 320
    width: hostContentWidth >= 0 ? hostContentWidth : implicitWidth
    height: hostContentHeight >= 0 ? hostContentHeight : 40
    clip: true
    readonly property string frameStyle: plugin && plugin.frameStyle !== undefined ? plugin.frameStyle : "wood"
    readonly property int frameInset: barFrame.inset
    readonly property var colors: pluginContext ? pluginContext.colors : ({
        panel: "#20252e", fg: "#eef0f4", muted: "#acb6c4",
        border: "#495464", accent: "#b1c8e5"
    })
    readonly property bool barVisible: visible && width > 0 && height > 0
                                       && Window.window !== null && Window.window.visible
    readonly property string phase: plugin && plugin.phase !== undefined ? plugin.phase : "day"
    readonly property string weather: plugin && plugin.weather !== undefined ? plugin.weather : "clear"
    readonly property string scene: plugin && plugin.scene !== undefined ? plugin.scene : "cabin"
    readonly property string sceneLabel: scene === "lighthouse" ? qsTranslate("plugin.pixel_landscape.main", "海边灯塔")
        : scene === "street" ? qsTranslate("plugin.pixel_landscape.main", "夜雨街角")
        : scene === "campsite" ? qsTranslate("plugin.pixel_landscape.main", "湖畔营地")
        : scene === "railway" ? qsTranslate("plugin.pixel_landscape.main", "田野铁道")
        : scene === "island" ? qsTranslate("plugin.pixel_landscape.main", "空中浮岛")
        : qsTranslate("plugin.pixel_landscape.main", "山间小屋")
    readonly property bool animationsEnabled: plugin && plugin.animationsEnabled !== undefined
                                              ? plugin.animationsEnabled : false
    readonly property bool paused: plugin && plugin.paused !== undefined ? plugin.paused : false
    readonly property string phaseLabel: phase === "dawn" ? qsTranslate("plugin.pixel_landscape.main", "清晨")
        : phase === "dusk" ? qsTranslate("plugin.pixel_landscape.main", "黄昏")
        : phase === "night" ? qsTranslate("plugin.pixel_landscape.main", "夜晚")
        : qsTranslate("plugin.pixel_landscape.main", "白天")
    readonly property string weatherLabel: weather === "rain" ? qsTranslate("plugin.pixel_landscape.main", "小雨")
        : weather === "snow" ? qsTranslate("plugin.pixel_landscape.main", "飘雪")
        : qsTranslate("plugin.pixel_landscape.main", "晴天")
    readonly property string statusText: phaseLabel + " · " + weatherLabel
    readonly property var solar: plugin && plugin.solarState !== undefined ? plugin.solarState : ({active: false})
    readonly property var goldenHour: plugin && plugin.goldenHourState !== undefined ? plugin.goldenHourState : ({strength: 0})
    readonly property var location: plugin && plugin.locationState !== undefined ? plugin.locationState : ({})
    readonly property string locationLabel: location.source === "system" ? qsTranslate("plugin.pixel_landscape.main", "Windows 当前位置")
        : location.source === "cache" ? qsTranslate("plugin.pixel_landscape.main", "最近位置（24 小时内）")
        : location.source === "manual" ? qsTranslate("plugin.pixel_landscape.main", "手动位置")
        : qsTranslate("plugin.pixel_landscape.main", "未取得位置，使用固定时段")
    readonly property string locationHint: location.pending ? qsTranslate("plugin.pixel_landscape.main", "正在定位…")
        : location.error === "denied" ? qsTranslate("plugin.pixel_landscape.main", "定位被拒绝或关闭。可在 Windows 设置的隐私与安全中启用位置，或设置手动坐标。")
        : location.error === "timeout" ? qsTranslate("plugin.pixel_landscape.main", "定位超时，可重试或设置手动坐标。")
        : location.error === "foreground_required" ? qsTranslate("plugin.pixel_landscape.main", "请在此窗口中重新点击定位。")
        : location.error === "cache_write_failed" ? qsTranslate("plugin.pixel_landscape.main", "位置已取得，但缓存保存失败。")
        : location.error === "prompt" || (!solar.active && !location.error) ? qsTranslate("plugin.pixel_landscape.main", "点击获取当前位置，或在设置中启用手动坐标。")
        : location.error ? qsTranslate("plugin.pixel_landscape.main", "系统暂时无法提供位置，可重试或设置手动坐标。") : ""
    function solarTime(value) { return value ? Qt.formatTime(value, "hh:mm") : "—" }
    readonly property string solarTimes: !solar.active ? ""
        : (solar.dayKind === "polarDay" ? qsTranslate("plugin.pixel_landscape.main", "极昼")
           : solar.dayKind === "polarNight" ? qsTranslate("plugin.pixel_landscape.main", "极夜")
           : qsTranslate("plugin.pixel_landscape.main", "日出 %1 · 日落 %2").arg(solarTime(solar.sunrise)).arg(solarTime(solar.sunset)))
          + " · " + qsTranslate("plugin.pixel_landscape.main", "电脑时区 %1").arg(solar.timeZone || "")

    function openLandscape() {
        // 不提供服务时保持栏上可用；窗口定位只由宿主处理。
        if (pluginContext && pluginContext.popupService)
            details.toggleFromBar()
    }
    function openSettings() {
        if (pluginContext) pluginContext.openSettings()
    }
    function togglePause() {
        if (plugin && animationsEnabled) plugin.paused = !paused
    }

    LandscapeController {
        id: controller
        objectName: "landscapeController"
        backend: root.plugin
        shown: (root.barVisible && barScene.ready && barScene.animatedDetails) || (details.visible && popupLoader.item !== null)
        enabled: root.animationsEnabled && !root.paused
        phase: root.phase
        weather: root.weather
        rareEventsEnabled: root.plugin && root.plugin.rareEventsEnabled !== undefined
                           ? root.plugin.rareEventsEnabled : false
    }
    LandscapeFrame {
        id: barFrame
        objectName: "landscapeFrame"
        anchors.fill: parent
        frameStyle: root.frameStyle
        colors: root.colors
        hovered: barMouse.containsMouse
    }
    LandscapeScene {
        id: barScene
        objectName: "barScene"
        anchors.fill: parent
        anchors.margins: root.frameInset
        sceneType: root.scene
        phase: root.phase
        previousPhase: root.plugin && root.plugin.previousPhase !== undefined ? root.plugin.previousPhase : root.phase
        weather: root.weather
        animationTime: controller.animationTime
        blend: controller.blend
        meteor: controller.meteor
        solarState: root.solar
        goldenHourState: root.goldenHour
        drawingVisible: root.barVisible
    }
    MouseArea {
        id: barMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton) root.openLandscape()
            else root.openSettings()
        }
        ToolTip.visible: containsMouse && !details.visible
        ToolTip.delay: 700
        ToolTip.text: root.sceneLabel + " · " + root.statusText
    }
    PluginPopup {
        id: details
        objectName: "landscapePopup"
        // 画面和工具栏覆盖整个窗口，不需要透明合成表面。
        color: root.colors.panel
        anchorItem: root
        pluginContext: root.pluginContext
        readonly property var available: root.pluginContext ? root.pluginContext.screenGeometry : ({})
        readonly property real usableWidth: Math.max(1, Number(available.width || 800) - 24)
        readonly property real usableHeight: Math.max(1, Number(available.height || 600) - 24)
        // 先按窄版页脚预留高度，再决定换行；宽而矮的屏幕也不能挤掉定位提示。
        readonly property real initialSceneWidth: Math.max(1, Math.min(640, usableWidth,
                                                     (usableHeight - (root.location.automatic ? 268 : 88)) * 16 / 9))
        readonly property real actionHeight: initialSceneWidth < 460 ? 88 : 48
        readonly property real locationHeight: root.location.automatic ? (initialSceneWidth < 460 ? 180 : 140) : 0
        readonly property real toolbarHeight: actionHeight + locationHeight
        readonly property real sceneWidth: Math.max(1, Math.min(640, usableWidth,
                                                      (usableHeight - toolbarHeight) * 16 / 9))
        width: sceneWidth
        height: Math.min(usableHeight, sceneWidth * 9 / 16 + toolbarHeight)
        Loader {
            id: popupLoader
            objectName: "landscapePopupLoader"
            anchors.fill: parent
            active: details.visible
            sourceComponent: popupBody
        }
    }
    Component {
        id: popupBody
        Rectangle {
            color: root.colors.panel
            Item {
                width: parent.width
                height: Math.max(0, parent.height - details.toolbarHeight)
                LandscapeFrame {
                    id: popupFrame
                    objectName: "landscapePopupFrame"
                    anchors.fill: parent
                    frameStyle: root.frameStyle
                    colors: root.colors
                }
                LandscapeScene {
                    objectName: "expandedScene"
                    anchors.fill: parent
                    anchors.margins: popupFrame.inset
                    expanded: true
                    sceneType: root.scene
                    drawingVisible: details.visible
                    phase: root.phase
                    previousPhase: root.plugin && root.plugin.previousPhase !== undefined ? root.plugin.previousPhase : root.phase
                    weather: root.weather
                    animationTime: controller.animationTime
                    blend: controller.blend
                    meteor: controller.meteor
                    solarState: root.solar
                    goldenHourState: root.goldenHour
                }
            }
            Flow {
                x: 12
                y: parent.height - details.toolbarHeight + 6
                width: Math.max(0, parent.width - 24)
                spacing: 6
                Text {
                    width: details.width < 460 ? parent.width : Math.max(0, parent.width - actions.width - 6)
                    height: details.width < 460 ? 24 : 36
                    verticalAlignment: Text.AlignVCenter
                    color: root.colors.fg
                    elide: Text.ElideRight
                    text: root.statusText
                }
                Row {
                    id: actions
                    objectName: "landscapeActions"
                    spacing: 6
                    Repeater {
                        model: [
                            {label: root.paused ? qsTranslate("plugin.pixel_landscape.main", "继续")
                                               : qsTranslate("plugin.pixel_landscape.main", "暂停"), action: "pause"},
                            {label: qsTranslate("plugin.pixel_landscape.main", "设置"), action: "settings"},
                            {label: qsTranslate("plugin.pixel_landscape.main", "关闭"), action: "close"}
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: Math.max(52, buttonText.implicitWidth + 18)
                            height: 34
                            radius: 4
                            color: buttonMouse.containsMouse ? root.colors.border : "transparent"
                            opacity: modelData.action === "pause" && !root.animationsEnabled ? 0.45 : 1
                            Text {
                                id: buttonText
                                anchors.centerIn: parent
                                text: modelData.label
                                color: root.colors.fg
                                font.pixelSize: 12
                            }
                            MouseArea {
                                id: buttonMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (modelData.action === "pause") root.togglePause()
                                    else if (modelData.action === "settings") root.openSettings()
                                    else details.closePopup()
                                }
                                ToolTip.visible: containsMouse && modelData.action === "pause" && !root.animationsEnabled
                                ToolTip.text: qsTranslate("plugin.pixel_landscape.main", "动画已在设置中关闭")
                            }
                        }
                    }
                }
            }
            Column {
                objectName: "landscapeLocationInfo"
                visible: root.location.automatic === true
                x: 12; y: parent.height - details.locationHeight + 4
                width: Math.max(0,parent.width-24)
                spacing: 6
                Text { width: parent.width; color: root.colors.fg; font.pixelSize: 12; wrapMode: Text.Wrap; text: root.locationLabel }
                Text { width: parent.width; visible: text.length>0; color: root.colors.fg; font.pixelSize: 12; wrapMode: Text.Wrap; text: root.solarTimes }
                Rectangle {
                    objectName: "landscapeLocateButton"
                    width: Math.min(parent.width,Math.max(130,locateText.implicitWidth+20)); height: 30
                    radius: 4; color: locateMouse.containsMouse ? root.colors.border : root.colors.panel
                    visible: root.location.systemSelected === true
                    opacity: root.location.pending ? 0.5 : 1
                    Text {
                        id: locateText; anchors.centerIn: parent; color: root.colors.fg; font.pixelSize: 12
                        text: root.location.pending ? qsTranslate("plugin.pixel_landscape.main", "正在定位…")
                            : root.location.source === "system" || root.location.source === "cache"
                                ? qsTranslate("plugin.pixel_landscape.main", "重新定位") : qsTranslate("plugin.pixel_landscape.main", "获取当前位置")
                    }
                    MouseArea {
                        id: locateMouse; anchors.fill: parent; hoverEnabled: true; enabled: !root.location.pending
                        onClicked: if (root.plugin && root.plugin.requestLocation) root.plugin.requestLocation()
                    }
                }
                Text { width: parent.width; color: root.colors.fg; font.pixelSize: 11; wrapMode: Text.Wrap; text: root.locationHint }
            }
        }
    }
}
