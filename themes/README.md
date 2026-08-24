# XuanHub 主题包开发契约

> 统一文档导航见 [`docs/README.md`](../docs/README.md)。本文是主题包格式的权威来源。

主题包放在 `<exeDir>/themes/<id>/`，由框架启动或热重载时发现。普通主题只含安全的声明式 JSON；复杂主题可选带一个可信 `barChrome` QML 外壳入口。两种形式都无需 C++ 编译。本文件是开发契约全文。

## 1. 目录布局

```
themes/
├── xuanhub-default/theme.json   # 内置默认主题（兜底，勿删）
├── floating-capsules/theme.json # 纯声明式区域胶囊示例
├── glass-panel/                 # 带可信 Bar 外壳扩展的示例
│   ├── theme.json
│   └── GlassPanelChrome.qml
├── catppuccin/theme.json        # Catppuccin：Latte / Mocha
├── github/theme.json             # GitHub Primer：Light / Dark
├── one/theme.json                # Atom One：Light / Dark
├── solarized/theme.json          # Solarized：Light / Dark
├── tokyo-night/theme.json        # Tokyo Night：Day / Night
└── <your-theme>/theme.json      # 你的主题：一个目录 + 一个 theme.json
```

- 安装 = 把目录拷进 `themes/`；卸载 = 删目录。
- 目录名建议等于 `theme.json` 里的 `id`（不强制，但便于辨认）。

## 2. theme.json 结构

```json
{
  "id": "my-theme",
  "name": "My Theme",
  "version": "0.1.0",
  "author": "your-name",
  "dark":  { "bg": "#1e1e2e", "fg": "#cdd6f4", "accent": "#89b4fa", ... },
  "light": { "bg": "#ececec", "fg": "#313244", "accent": "#89b4fa", ... }
}
```

**必填字段**：`id`、`dark.bg`、`dark.fg`、`dark.accent`、`light.bg`、`light.fg`、`light.accent`。
缺任何一个必填字段，整个主题包会被拒绝加载（日志记 `[Theme] ... missing required fields`）。

**其余字段缺失时**：从内置默认主题（`xuanhub-default`）同模式同名字段 fallback，日志记 `[Theme] field <f> missing ... -> fallback to default`。所以你可以只覆盖关心的几个字段。

## 3. 色板字段语义（11 个）

| 字段 | 语义 | 典型用途 |
|---|---|---|
| `bg` | 根背景 | 状态栏底色、弹窗/设置窗口底色 |
| `panel` | 面板/卡片背景 | 设置窗口分组卡片、日历底栏、控件底色 |
| `fg` | 主文字 | 标题、正文 |
| `muted` | 次要文字 | 副标题、描述小字、占位文本、非当月日 |
| `accent` | 强调色 | 主按钮底色、导航选中条、开关开态、焦点描边、今日高亮、链接 |
| `danger` | 危险/错误 | 错误提示、非法输入红框、关闭按钮 hover、加载失败 badge |
| `success` | 成功/正常 | 下行网速、用量正常状态 |
| `warning` | 警告 | 用量进度中高水位 |
| `hover` | 悬停反馈 | bar 部件 hover 底色、按钮/列表行 hover |
| `border` | 边框 | 弹窗描边、输入框描边、分隔线 |
| `onAccent` | accent 背景上的文字色 | 主按钮文字、今日高亮上的字（按 accent 明度选黑/白） |

字段值必须是 QColor 可解析的颜色字符串（`#RGB` / `#RRGGBB` / `#RRGGBBAA` 等 hex 形态、SVG 命名色），不支持 `rgb()` 函数式。非法色值视为该字段**缺失**（日志记 `[Theme] ... invalid color value ... -> treated as missing`）：必填字段非法 → 整包拒载；其余字段非法 → 走内置默认主题 fallback。

## 4. Theme Schema v2 外观令牌

`appearance` 是 dark/light 共用值，`darkAppearance`、`lightAppearance` 按当前模式覆盖。宿主对字段使用 allowlist，并对数值钳制；未知字段忽略，缺失字段从内置默认外观补齐。

```json
{
  "appearance": {
    "window": { "outerPaddingX": 12, "outerPaddingY": 5 },
    "bar": {
      "background": "bg", "opacity": 0.9, "radius": 12,
      "borderWidth": 1, "borderColor": "border"
    },
    "regions": {
      "decoration": "pill", "background": "panel",
      "paddingX": 5, "paddingY": 3, "spacing": 8
    },
    "items": {
      "decoration": "panel", "background": "panel",
      "hoverBackground": "hover", "paddingX": 7, "paddingY": 3,
      "spacing": 5, "radius": 6
    },
    "typography": { "fontFamily": "", "fontSize": 12, "fontWeight": 500 },
    "motion": { "duration": 140 }
  }
}
```

- `background/borderColor/hoverBackground` 引用 11 个语义色字段；`background` 另接受 `transparent`。
- `decoration` 为 `none/panel/pill`。
- 尺寸均为逻辑像素。外边距、内边距、间距、圆角、边框、字号、字重、透明度和动效时长均有宿主限制，非法值不会直接进入布局。
- `bar.opacity` 只作用于 Bar 表面，并与 `config.json` 的 `bar.opacity` 相乘；taskbar 模式仍强制整条 Bar 表面透明，但区域和单项装饰可以保留。
- 插件配置 `width > 0` 继续表示完整槽位宽度，主题内边距不会暗中扩大手动宽度。

## 5. 可信 Bar 外壳扩展

声明式令牌无法表达的外壳，可以在同一主题包中声明：

```json
{
  "extension": {
    "type": "barChrome",
    "apiVersion": 1,
    "qml": "MyChrome.qml",
    "supports": ["appbar", "taskbar"]
  }
}
```

`qml` 必须是主题目录内真实存在的相对 `.qml` 文件；绝对路径、`..`、不存在文件以及 junction/symlink 最终越界都会被拒绝。`supports` 可省略，省略表示两种工作模式都支持；不支持当前模式时自动使用内置通用外壳。

扩展根对象必须声明三个属性：

```qml
import QtQuick
import XuanHub

Item {
    property var hostPluginModel: null
    property var hostTheme: null
    property var hostBarConfig: null

    StandardBarChrome {
        anchors.fill: parent
        hostPluginModel: parent.hostPluginModel
        hostTheme: parent.hostTheme
        hostBarConfig: parent.hostBarConfig
    }
}
```

推荐组合 `StandardBarChrome` 添加装饰层，而不是复制插件模型和响应式布局。根契约缺失或 QML 加载失败时，宿主记录日志并立即回退内置外壳。

**信任边界**：`barChrome` QML 是可执行代码，不等同于安全 JSON 色板。仅安装和选择可信来源的扩展主题；普通配色、间距和胶囊效果优先使用声明式令牌完成。

## 6. QML 如何消费主题

所有 QML（宿主 + 插件）通过 context property `themeManager.colors.<field>` 取色：

```qml
Rectangle { color: themeManager.colors.bg }
Text { color: themeManager.colors.fg }
Item { property real gap: themeManager.appearance.items.spacing }
```

`themeManager.colors` 在主题/模式切换时自动刷新所有绑定，无需手动监听。防御写法（防 themeManager 尚未注入）：

```qml
color: themeManager ? themeManager.colors.accent : "#89b4fa"
```

**换肤覆盖范围**：bar、全部插件部件与弹窗、设置窗口（含按钮/开关/输入框等自绘控件）均只吃这 11 个字段——设置窗口自绘控件经 `palette` 继承色板（`highlight=accent`、`button=panel` 等），hover/焦点等派生态用 `Qt.lighter/Qt.rgba` 从色板色推算，无任何硬编码色。也就是说一套 theme.json 即可给整个程序完整换肤；写新 QML 时请遵守同一规矩（派生态从色板推算，不要写死颜色）。

## 7. 模式切换 vs 主题包切换

- **模式（dark/light）**：托盘菜单 "切换深色/浅色" 快速切换；设置窗口"模式"下拉。每套主题包必须同时提供两套色板。
- **主题包**：设置窗口"主题包"下拉。切主题包保持当前模式。

## 8. 调试

- 日志：`%APPDATA%/XuanHub/xuanhub.log`，`[Theme]` 前缀记录加载/切换/fallback。
- 改 `theme.json` 保存后，触发任意一次配置热重载（如改 config.json 或托盘 "重新加载配置"）即重新扫描 `themes/`——新装的主题包立刻出现在设置窗口。
- 修改 `barChrome` QML 后必须重启 XuanHub；QML 引擎会缓存已经加载过的 file 组件，配置热重载不保证重新读取同 URL 的文件。
- 主题包加载失败不会影响其他主题包，也不会让程序崩溃；外壳扩展失败会回退到 `StandardBarChrome`。
- 维护提醒：默认色板在代码内有多处硬编码兜底（`ThemeRegistry` 内置默认、`TrayIcon` 取色兜底、各 QML 防御默认值），调整 `xuanhub-default` 配色时需同步检查这些位置。
