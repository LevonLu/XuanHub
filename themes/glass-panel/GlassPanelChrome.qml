// GlassPanelChrome.qml — Glass Panel 主题扩展：在通用外壳上增加边缘高光。
import QtQuick
import XuanHub

Item {
    id: extension
    property var hostPluginModel: null
    property var hostTheme: null
    property var hostBarConfig: null
    readonly property var windowStyle: hostTheme && hostTheme.appearance
                                       ? (hostTheme.appearance.window || ({})) : ({})
    implicitWidth: standard.implicitWidth

    StandardBarChrome {
        id: standard
        anchors.fill: parent
        hostPluginModel: extension.hostPluginModel
        hostTheme: extension.hostTheme
        hostBarConfig: extension.hostBarConfig
    }

    Rectangle {
        x: Number(extension.windowStyle.outerPaddingX || 0) + 16
        width: Math.max(0, parent.width
                        - Number(extension.windowStyle.outerPaddingX || 0) * 2 - 32)
        height: 1
        y: extension.hostBarConfig && extension.hostBarConfig.barEdge === "bottom"
           ? parent.height - Number(extension.windowStyle.outerPaddingY || 0) - 1
           : Number(extension.windowStyle.outerPaddingY || 0)
        color: extension.hostTheme && extension.hostTheme.colors
               ? extension.hostTheme.colors.accent : "#c59cff"
        opacity: 0.45
    }
}
