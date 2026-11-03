// StandardBarChrome.qml — 内置通用 Bar 外壳，消费 Theme Schema v2 外观令牌。
pragma ComponentBehavior: Bound
import QtQuick
import XuanHub

Item {
    id: chrome

    required property var hostPluginModel
    required property var hostTheme
    required property var hostBarConfig

    readonly property bool compact: !!hostBarConfig && hostBarConfig.barMode === "taskbar"
    readonly property var appearance: hostTheme && hostTheme.appearance
                                      ? hostTheme.appearance : ({})
    readonly property var windowStyle: appearance.window || ({})
    readonly property var barStyle: appearance.bar || ({})
    readonly property var regionStyle: appearance.regions || ({})
    readonly property real outerPaddingX: Number(windowStyle.outerPaddingX || 0)
    readonly property real outerPaddingY: Number(windowStyle.outerPaddingY || 0)
    readonly property real regionPaddingX: Number(regionStyle.paddingX || 0)
    readonly property real regionPaddingY: Number(regionStyle.paddingY || 0)
    readonly property real regionSpacing: Number(regionStyle.spacing || 0)

    implicitWidth: contentRow.implicitWidth + outerPaddingX * 2

    function paletteColor(name, fallback) {
        if (name === "transparent") return "transparent"
        if (hostTheme && hostTheme.colors && hostTheme.colors[name] !== undefined)
            return hostTheme.colors[name]
        return fallback
    }

    function withOpacity(value, opacity) {
        const c = value
        return Qt.rgba(c.r, c.g, c.b,
                       c.a * Math.max(0, Math.min(1, Number(opacity))))
    }

    function regionOverhead(region) {
        return region.visible ? regionPaddingX * 2 : 0
    }

    function updateLayoutBudget() {
        if (!hostPluginModel) return
        const chromeOverhead = outerPaddingX * 2
                + regionOverhead(leftRegion)
                + regionOverhead(centerRegion)
                + regionOverhead(rightRegion)
                + (overflowButton.visible ? overflowButton.width : 0)
        hostPluginModel.setAvailableWidth(compact ? 0 : Math.max(0, width - chromeOverhead))
    }
    Component.onCompleted: updateLayoutBudget()
    onWidthChanged: updateLayoutBudget()
    onCompactChanged: updateLayoutBudget()
    onOuterPaddingXChanged: Qt.callLater(updateLayoutBudget)
    onRegionPaddingXChanged: Qt.callLater(updateLayoutBudget)

    Connections {
        target: chrome.hostPluginModel
        ignoreUnknownSignals: true
        function onResponsiveLayoutChanged() { Qt.callLater(chrome.updateLayoutBudget) }
    }

    Rectangle {
        id: barSurface
        x: chrome.outerPaddingX
        y: chrome.outerPaddingY
        width: Math.max(0, parent.width - chrome.outerPaddingX * 2)
        height: Math.max(0, parent.height - chrome.outerPaddingY * 2)
        radius: Number(chrome.barStyle.radius || 0)
        property color surfaceColor: chrome.paletteColor(
            chrome.compact ? "transparent" : (chrome.barStyle.background || "bg"),
            "#202020")
        color: chrome.withOpacity(surfaceColor,
            chrome.compact ? 0 : Number(chrome.barStyle.opacity !== undefined
                                        ? chrome.barStyle.opacity : 1)
                * Number(chrome.hostBarConfig && chrome.hostBarConfig.barOpacity !== undefined
                         ? chrome.hostBarConfig.barOpacity : 1))
        border.width: chrome.compact ? 0 : Number(chrome.barStyle.borderWidth || 0)
        border.color: chrome.paletteColor(chrome.barStyle.borderColor || "border", "#484848")
    }

    component RegionHost: Rectangle {
        id: region
        required property var regionModel
        readonly property bool decorated: chrome.regionStyle.decoration !== "none"
        // colors 是 QVariantMap 暴露给 QML 的字符串；先经 color 属性完成转换，
        // 再拆分 RGBA 通道。直接对字符串读 .r/.g/.b 会把胶囊背景算成透明。
        property color backgroundColor: chrome.paletteColor(
            chrome.regionStyle.background || "panel", "#2b2b2b")
        visible: list.count > 0
        width: visible ? list.contentWidth + chrome.regionPaddingX * 2 : 0
        height: contentRow.height
        color: decorated
               ? chrome.withOpacity(backgroundColor,
                   Number(chrome.regionStyle.opacity !== undefined
                          ? chrome.regionStyle.opacity : 1))
               : "transparent"
        radius: chrome.regionStyle.decoration === "pill"
                ? height / 2 : Number(chrome.regionStyle.radius || 0)
        border.width: decorated ? Number(chrome.regionStyle.borderWidth || 0) : 0
        border.color: chrome.paletteColor(
            chrome.regionStyle.borderColor || "border", "#484848")

        ListView {
            id: list
            x: chrome.regionPaddingX
            y: chrome.regionPaddingY
            width: contentWidth
            height: Math.max(0, parent.height - chrome.regionPaddingY * 2)
            model: region.regionModel
            delegate: PluginSlot {}
            orientation: ListView.Horizontal
            interactive: false
            spacing: Number((chrome.appearance.items || {}).spacing || 0)
        }
    }

    Row {
        id: contentRow
        x: chrome.outerPaddingX
        y: chrome.outerPaddingY
        width: Math.max(0, parent.width - chrome.outerPaddingX * 2)
        height: Math.max(0, parent.height - chrome.outerPaddingY * 2)
        spacing: chrome.compact ? chrome.regionSpacing : 0

        RegionHost {
            id: leftRegion
            regionModel: chrome.hostPluginModel ? chrome.hostPluginModel.leftPlugins : null
            onVisibleChanged: Qt.callLater(chrome.updateLayoutBudget)
        }

        Item {
            visible: !chrome.compact
            width: chrome.compact ? 0 : Math.max(0,
                (parent.width - leftRegion.width - centerRegion.width - rightRegion.width
                 - overflowButton.width) / 2)
            height: 1
        }

        RegionHost {
            id: centerRegion
            regionModel: chrome.hostPluginModel ? chrome.hostPluginModel.centerPlugins : null
            onVisibleChanged: Qt.callLater(chrome.updateLayoutBudget)
        }

        Item {
            visible: !chrome.compact
            width: chrome.compact ? 0 : Math.max(0,
                (parent.width - leftRegion.width - centerRegion.width - rightRegion.width
                 - overflowButton.width) / 2)
            height: 1
        }

        RegionHost {
            id: rightRegion
            regionModel: chrome.hostPluginModel ? chrome.hostPluginModel.rightPlugins : null
            onVisibleChanged: Qt.callLater(chrome.updateLayoutBudget)
        }

        Item {
            id: overflowButton
            visible: !chrome.compact && !!chrome.hostPluginModel
                     && chrome.hostPluginModel.overflowPlugins.length > 0
            width: visible ? 34 : 0
            height: parent.height
            onVisibleChanged: Qt.callLater(chrome.updateLayoutBudget)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: 6
                color: overflowArea.containsMouse
                       ? chrome.paletteColor("hover", "#383838") : "transparent"
            }
            Text {
                anchors.centerIn: parent
                text: "⋯"
                color: chrome.paletteColor("fg", "#ffffff")
                font.pixelSize: Number((chrome.appearance.typography || {}).fontSize || 12) + 8
                font.bold: true
            }
            MouseArea {
                id: overflowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: overflowPopup.toggleFromBar()
            }
        }
    }

    PluginPopup {
        id: overflowPopup
        anchorItem: overflowButton
        width: Math.min(520, Math.max(260, overflowColumn.implicitWidth + 24))
        height: Math.min(420, overflowColumn.implicitHeight + 24)

        Rectangle {
            anchors.fill: parent
            radius: 10
            color: chrome.paletteColor("bg", "#202020")
            border.width: 1
            border.color: chrome.paletteColor("border", "#484848")

            Flickable {
                anchors.fill: parent
                anchors.margins: 12
                contentWidth: overflowColumn.implicitWidth
                contentHeight: overflowColumn.implicitHeight
                clip: true

                Column {
                    id: overflowColumn
                    spacing: 6
                    Text {
                        text: qsTranslate("xuanhub.bar", "更多插件")
                        color: chrome.paletteColor("fg", "#ffffff")
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Repeater {
                        model: chrome.hostPluginModel ? chrome.hostPluginModel.overflowPluginModel : null
                        delegate: PluginSlot { overflowHost: true }
                    }
                }
            }
        }
    }
}
