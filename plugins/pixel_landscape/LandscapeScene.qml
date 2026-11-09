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
    property int staticPaintCount: 0
    property int dynamicPaintCount: 0
    readonly property bool ready: staticCanvas.available && dynamicCanvas.available && width > 0 && height > 0
    readonly property int gridHeight: expanded ? 180 : Math.max(1, Math.min(96, Math.round(height)))
    readonly property int gridWidth: expanded ? 320 : Math.max(1, Math.min(800, Math.round(width * gridHeight / Math.max(1, height))))
    readonly property bool animatedDetails: expanded || gridWidth >= 32
    readonly property var palette: Art.palette(phase, weather, previousPhase, blend)
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
