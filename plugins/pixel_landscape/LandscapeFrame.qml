// LandscapeFrame.qml — 五种栏上画框，静态几何装饰不参与动画时钟
import QtQuick

Item {
    id: frame
    property string frameStyle: "wood"
    property bool hovered: false
    property var colors: ({ panel: "#20252e", border: "#495464", accent: "#b1c8e5" })
    readonly property int requestedInset: frameStyle === "none" ? 0 : frameStyle === "thin" ? 2 : frameStyle === "mat" ? 5 : 4
    // 为低矮和紧凑状态栏限制框厚；极窄槽位把全部空间留给剪影。
    readonly property int inset: width < 32 || height < 12 ? 0
        : Math.min(requestedInset, Math.floor(height / 6), Math.floor(width / 12))
    readonly property var tones: frameStyle === "wood"
        ? ({ edge: "#38271c", body: "#95663e", light: "#d7a86d", shade: "#63402b", inner: "#493020", detail: "#b7804a" })
        : frameStyle === "mat"
        ? ({ edge: "#827b70", body: "#eee7d6", light: "#fffaf0", shade: "#c9c0ac", inner: "#9d9484", detail: "#eee7d6" })
        : frameStyle === "metal"
        ? ({ edge: "#263641", body: "#738d9c", light: "#c0d5df", shade: "#455d6c", inner: "#30444e", detail: "#d9e7eb" })
        : ({ edge: colors.border, body: colors.panel, light: colors.panel,
             shade: colors.panel, inner: colors.panel, detail: colors.panel })
    visible: inset > 0
    clip: true

    Rectangle {
        anchors.fill: parent
        color: frame.hovered ? frame.colors.accent : frame.tones.edge
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        color: frame.tones.body
    }
    Rectangle {
        x: 1; y: 1
        width: Math.max(0, parent.width - 2); height: 1
        color: frame.tones.light
        visible: frame.inset >= 3
    }
    Rectangle {
        x: 1; y: 1
        width: 1; height: Math.max(0, parent.height - 2)
        color: frame.tones.light
        visible: frame.inset >= 3
    }
    Rectangle {
        x: 1; y: parent.height - 2
        width: Math.max(0, parent.width - 2); height: 1
        color: frame.tones.shade
        visible: frame.inset >= 3
    }
    Rectangle {
        x: parent.width - 2; y: 1
        width: 1; height: Math.max(0, parent.height - 2)
        color: frame.tones.shade
        visible: frame.inset >= 3
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: Math.max(0, frame.inset - 1)
        color: frame.tones.inner
        visible: frame.inset >= 3
    }
    // 少量固定木纹强调横向框条，不创建逐帧效果。
    Repeater {
        model: frame.frameStyle === "wood" && frame.inset >= 4 ? 8 : 0
        Rectangle {
            required property int index
            x: Math.round(10 + (index % 4) * (frame.width - 20) / 4)
            y: index < 4 ? 2 : frame.height - 3
            width: Math.max(1, Math.min(22, Math.floor(frame.width / 12)))
            height: 1
            color: frame.tones.detail
        }
    }
    Repeater {
        model: frame.frameStyle === "metal" && frame.inset >= 4 ? 4 : 0
        Rectangle {
            required property int index
            x: index % 2 ? frame.width - 4 : 2
            y: index < 2 ? 2 : frame.height - 4
            width: 2; height: 2
            color: frame.tones.edge
            Rectangle { width: 1; height: 1; color: frame.tones.detail }
        }
    }
}
