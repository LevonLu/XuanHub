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
        // 先用单行工具栏估计可用宽度，再决定换行，避免 width 与 sceneWidth 相互依赖。
        readonly property real initialSceneWidth: Math.max(1, Math.min(640, usableWidth,
                                                     (usableHeight - 48) * 16 / 9))
        readonly property real toolbarHeight: initialSceneWidth < 460 ? 88 : 48
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
            LandscapeScene {
                objectName: "expandedScene"
                width: parent.width
                height: Math.max(0, parent.height - details.toolbarHeight)
                expanded: true
                sceneType: root.scene
                drawingVisible: details.visible
                phase: root.phase
                previousPhase: root.plugin && root.plugin.previousPhase !== undefined ? root.plugin.previousPhase : root.phase
                weather: root.weather
                animationTime: controller.animationTime
                blend: controller.blend
                meteor: controller.meteor
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
        }
    }
}
