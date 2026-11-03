// UiMetrics.qml — 设计令牌单例：间距/圆角/控件高/字号/动画时长（QML 模块级 singleton）
// 只放无量纲数值，不放颜色——颜色一律走 palette（themeManager.colors 灌入），保证主题数据驱动。
pragma Singleton
import QtQuick

QtObject {
    // 间距阶梯（4px 基准网格）
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 24
    readonly property int space6: 32

    // 圆角阶梯
    readonly property int radiusS: 4    // 控件级，遵循 WinUI 的小圆角
    readonly property int radiusM: 8    // 卡片与浮层
    readonly property int radiusL: 8    // 窗口与页面容器

    // 控件高度
    readonly property int controlHeightS: 32
    readonly property int controlHeightM: 32

    // 字号 scale
    readonly property real fontTiny: 11     // 元信息小字
    readonly property real fontS: 12        // 表单 label / 描述
    readonly property real fontM: 14        // 正文 / 控件文字
    readonly property real fontL: 16        // 卡片标题
    readonly property real fontXL: 28       // 页面/详情页头部

    // 微动效时长（hover/focus 过渡、页切换淡入、导航选中条滑动）
    readonly property int animFast: 120
}
