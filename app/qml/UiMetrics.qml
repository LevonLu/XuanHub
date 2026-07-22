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
    readonly property int space5: 20

    // 圆角阶梯
    readonly property int radiusS: 6    // 控件级（按钮/输入框/列表行/开关）
    readonly property int radiusM: 8    // 面板级（卡片/下拉浮层）
    readonly property int radiusL: 10   // 预留：更大容器

    // 控件高度
    readonly property int controlHeightS: 28
    readonly property int controlHeightM: 32

    // 字号 scale
    readonly property real fontTiny: 10.5   // 元信息小字
    readonly property real fontS: 12        // 表单 label / 描述
    readonly property real fontM: 13        // 正文 / 控件文字
    readonly property real fontL: 15        // 卡片标题
    readonly property real fontXL: 17       // 详情页头部

    // 微动效时长（hover/focus 过渡、页切换淡入、导航选中条滑动）
    readonly property int animFast: 120
}
