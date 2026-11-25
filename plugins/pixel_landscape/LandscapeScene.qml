// LandscapeScene.qml — 固定像素网格上的静态地形和有限动态覆盖层
import QtQuick
import "SceneArt.js" as Art
import "LighthouseArt.js" as Lighthouse
import "StreetArt.js" as Street
import "CampsiteArt.js" as Campsite
import "RailwayArt.js" as Railway
import "IslandArt.js" as Island
import "WindowCityArt.js" as WindowCity
import "BelowSurfaceArt.js" as BelowSurface
import "WestwardArt.js" as Westward

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
    readonly property bool cityView: sceneType === "window_city"
    // 取经路的地形随时段变化；真实光照下，相邻时段可以具有相同的调色板。
    readonly property string journeyPhase: sceneType === "westward_journey" ? phase : ""
    readonly property int crossingSteps: 80
    readonly property int crossingStep: journeyPhase && previousPhase !== phase && animatedDetails
        ? Math.floor(blend * crossingSteps) : crossingSteps
    readonly property var renderer: sceneType === "lighthouse" ? Lighthouse
        : sceneType === "street" ? Street : sceneType === "campsite" ? Campsite
        : sceneType === "railway" ? Railway : sceneType === "island" ? Island
        : cityView ? WindowCity : sceneType === "below_surface" ? BelowSurface
        : journeyPhase ? Westward : Art
    readonly property var geometry: renderer.geometry(gridWidth, gridHeight, expanded)
    // 水下剖面只露出一小段天空，真实太阳与色带沿用该段天空的像素比例。
    readonly property int skyHeight: geometry.sky ? geometry.sky.h : gridHeight
    clip: true

    function repaintStatic() { if (drawingVisible && ready) staticCanvas.requestPaint() }
    function repaintDynamic() { if (drawingVisible && ready) dynamicCanvas.requestPaint() }
    function repaintAll() { repaintStatic(); repaintDynamic() }
    function paintLayer(ctx, dynamic) {
        function draw(phaseToDraw) {
            if (dynamic)
                renderer.drawDynamic(ctx, geometry, palette, weather, animationTime, meteor, phaseToDraw)
            else
                renderer.drawStatic(ctx, geometry, palette, weather, phaseToDraw)
        }
        if (crossingStep === crossingSteps) {
            draw(phase)
            return
        }
        // 新路从西侧进入。地面、人物和水中倒影在同一位置交接。
        draw(previousPhase)
        if (crossingStep === 0) return
        ctx.save()
        ctx.beginPath()
        ctx.rect(0, 0, Math.round(gridWidth * crossingStep / crossingSteps), gridHeight)
        ctx.clip()
        draw(phase)
        ctx.restore()
    }
    onPaletteChanged: repaintAll()
    onJourneyPhaseChanged: repaintAll()
    onCrossingStepChanged: repaintAll()
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
            visible: scene.solarActive || scene.cityView
            color: scene.palette.horizon
            // 窗景的余光集中在屋顶上方，真实太阳仍在这层天空与地形之间移动。
            gradient: scene.cityView ? windowSky : null
            Gradient {
                id: windowSky
                readonly property real twilight: Math.max(0, scene.palette.warm - scene.palette.night)
                GradientStop { position: 0; color: Art.mix(scene.palette.sky, "7691ae", windowSky.twilight * 0.91) }
                GradientStop { position: 0.37; color: Art.mix(scene.palette.skyLow, "bdcad6", windowSky.twilight * 0.90) }
                GradientStop { position: 0.56; color: Art.mix(scene.palette.skyLow, scene.palette.snow, 0.06 + 0.30 * windowSky.twilight) }
                GradientStop { position: 0.66; color: Art.mix(scene.palette.horizon, scene.palette.cloud, 0.28) }
                GradientStop { position: 0.72; color: scene.palette.horizon }
                GradientStop { position: 1; color: scene.palette.far }
            }
            Repeater {
                model: scene.cityView ? 0 : 12
                Rectangle {
                    required property int index
                    readonly property real t: index / 11
                    x: 0; y: Math.floor(index * scene.skyHeight / 15)
                    width: scene.gridWidth
                    height: Math.ceil(scene.skyHeight / 15) + 1
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
                readonly property int spread: (index + 1) * Math.max(2, Math.round(scene.skyHeight * 0.025))
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
            readonly property real sunX: Number(scene.solarState.sunX ?? 0.5)
            width: Math.max(4, Math.round(scene.skyHeight * 0.09))
            height: width
            x: Math.round(scene.gridWidth * (scene.journeyPhase ? 1 - sunX : sunX) - width / 2)
            y: Math.round(horizon - (horizon - scene.skyHeight * 0.10) * altitude - height / 2)
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
                scene.paintLayer(getContext("2d"), false)
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
                scene.paintLayer(getContext("2d"), true)
                scene.dynamicPaintCount++
            }
        }
    }
}
