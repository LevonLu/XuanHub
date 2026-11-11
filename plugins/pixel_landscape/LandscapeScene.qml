// LandscapeScene.qml — 固定像素网格上的静态地形和有限动态覆盖层
import QtQuick
import "SceneArt.js" as Art
import "LighthouseArt.js" as Lighthouse
import "StreetArt.js" as Street
import "CampsiteArt.js" as Campsite
import "RailwayArt.js" as Railway
import "IslandArt.js" as Island

Item {
    id: scene
    property bool expanded: false
    property string sceneType: "cabin"
    property bool drawingVisible: true
    property string phase: "day"
    property string previousPhase: "day"
    property string weather: "clear"
    property double animationTime: 0
    property real blend: 1
    property real meteor: -1
    property var solarState: ({active: false})
    property var goldenHourState: ({strength: 0})
    readonly property real goldenStrength: weather === "clear" ? Number(goldenHourState.strength || 0) : 0
    readonly property bool solarActive: !!solarState && solarState.active === true
    // 太阳坐标变化不影响调色板键，因而不会使静态地形失效。
    readonly property string lightKey: solarActive ? [solarState.from, solarState.to, solarState.mix,
        solarState.night, solarState.warm, solarState.mist, solarState.twilightMix].join("|") : ""
    property int staticPaintCount: 0
    property int dynamicPaintCount: 0
    readonly property bool ready: staticCanvas.available && dynamicCanvas.available && width > 0 && height > 0
    readonly property int gridHeight: expanded ? 180 : Math.max(1, Math.min(96, Math.round(height)))
    readonly property int gridWidth: expanded ? 320 : Math.max(1, Math.min(800, Math.round(width * gridHeight / Math.max(1, height))))
    readonly property bool animatedDetails: expanded || gridWidth >= 32
    // 以最终颜色和光照强度判定失效，正午的相位变化不会重画颜色未变的地形。
    readonly property string paletteKey: JSON.stringify(Art.goldenPalette(
        lightKey ? Art.solarPalette(lightKey, weather) : Art.palette(phase, weather, previousPhase, blend),
        goldenStrength, Number(goldenHourState.evening || 0)))
    readonly property var palette: JSON.parse(paletteKey)
    readonly property var renderer: sceneType === "lighthouse" ? Lighthouse
        : sceneType === "street" ? Street : sceneType === "campsite" ? Campsite
        : sceneType === "railway" ? Railway : sceneType === "island" ? Island : Art
    readonly property var geometry: renderer.geometry(gridWidth, gridHeight, expanded)
    clip: true

    function repaintStatic() { if (drawingVisible && ready) staticCanvas.requestPaint() }
    function repaintDynamic() { if (drawingVisible && ready) dynamicCanvas.requestPaint() }
    function repaintAll() { repaintStatic(); repaintDynamic() }
    onPaletteChanged: repaintAll()
    onGeometryChanged: repaintAll()
    onAnimationTimeChanged: if (animatedDetails) repaintDynamic()
    onDrawingVisibleChanged: if (drawingVisible) repaintAll()
    onReadyChanged: if (ready) repaintAll()
    onMeteorChanged: repaintDynamic()

    Item {
        width: scene.gridWidth
        height: scene.gridHeight
        transform: Scale {
            xScale: scene.width / scene.gridWidth
            yScale: scene.height / scene.gridHeight
        }
        Rectangle {
            anchors.fill: parent
            visible: scene.solarActive
            color: scene.palette.horizon
            Repeater {
                model: 12
                Rectangle {
                    required property int index
                    readonly property real t: index / 11
                    x: 0; y: Math.floor(index * scene.gridHeight / 15)
                    width: scene.gridWidth
                    height: Math.ceil(scene.gridHeight / 15) + 1
                    color: t < 0.6 ? Art.mix(scene.palette.sky, scene.palette.skyLow, t / 0.6)
                                   : Art.mix(scene.palette.skyLow, scene.palette.horizon, (t - 0.6) / 0.4)
                }
            }
        }
        // 天空和太阳位于透明地形画布下方，山脊自然遮挡，不新增 Canvas 或遮罩纹理。
        Repeater {
            model: 3
            Rectangle {
                required property int index
                readonly property int spread: (index + 1) * Math.max(2, Math.round(scene.gridHeight * 0.025))
                x: solarDisc.x - spread; y: solarDisc.y - Math.floor(spread * 0.6)
                width: solarDisc.width + spread * 2; height: solarDisc.height + Math.round(spread * 1.2)
                color: scene.palette.horizon
                opacity: scene.goldenStrength * (0.13 - index * 0.03) * solarDisc.opacity
                visible: solarDisc.visible && scene.goldenStrength > 0
            }
        }
        Rectangle {
            id: solarDisc
            objectName: "solarDisc"
            visible: scene.solarActive && scene.weather === "clear"
            readonly property real horizon: scene.geometry.horizon || scene.gridHeight * 0.6
            readonly property real altitude: Number(scene.solarState.sunHeight || 0)
            width: Math.max(4, Math.round(scene.gridHeight * 0.09))
            height: width
            x: Math.round(scene.gridWidth * Number(scene.solarState.sunX || 0.5) - width / 2)
            y: Math.round(horizon - (horizon - scene.gridHeight * 0.10) * altitude - height / 2)
            opacity: Number(scene.solarState.sunOpacity || 0)
            color: scene.palette.sun
            Rectangle { x: -1; y: 1; width: parent.width + 2; height: Math.max(1,parent.height-2); color: parent.color }
        }
        Canvas {
            id: staticCanvas
            width: scene.gridWidth
            height: scene.gridHeight
            smooth: false
            renderTarget: Canvas.Image
            onPaint: {
                scene.renderer.drawStatic(getContext("2d"), scene.geometry, scene.palette, scene.weather)
                scene.staticPaintCount++
            }
        }
        Canvas {
            id: dynamicCanvas
            width: scene.gridWidth
            height: scene.gridHeight
            smooth: false
            renderTarget: Canvas.Image
            onPaint: {
                scene.renderer.drawDynamic(getContext("2d"), scene.geometry, scene.palette,
                                scene.weather, scene.animationTime, scene.meteor)
                scene.dynamicPaintCount++
            }
        }
    }
}
