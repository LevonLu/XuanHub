# XuanHub 主题包开发契约

主题包是**纯 JSON 声明式**的目录，放 `<exeDir>/themes/<id>/theme.json` 即被框架发现。无需 C++ 编译、无需重启（下次热重载/重启即生效）。本文件是开发契约全文。

## 1. 目录布局

```
themes/
├── xuanhub-default/theme.json   # 内置默认主题（兜底，勿删）
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

## 4. QML 如何取色

所有 QML（宿主 + 插件）通过 context property `themeManager.colors.<field>` 取色：

```qml
Rectangle { color: themeManager.colors.bg }
Text { color: themeManager.colors.fg }
```

`themeManager.colors` 在主题/模式切换时自动刷新所有绑定，无需手动监听。防御写法（防 themeManager 尚未注入）：

```qml
color: themeManager ? themeManager.colors.accent : "#89b4fa"
```

**换肤覆盖范围**：bar、全部插件部件与弹窗、设置窗口（含按钮/开关/输入框等自绘控件）均只吃这 11 个字段——设置窗口自绘控件经 `palette` 继承色板（`highlight=accent`、`button=panel` 等），hover/焦点等派生态用 `Qt.lighter/Qt.rgba` 从色板色推算，无任何硬编码色。也就是说一套 theme.json 即可给整个程序完整换肤；写新 QML 时请遵守同一规矩（派生态从色板推算，不要写死颜色）。

## 5. 模式切换 vs 主题包切换

- **模式（dark/light）**：托盘菜单 "切换深色/浅色" 快速切换；设置窗口"模式"下拉。每套主题包必须同时提供两套色板。
- **主题包**：设置窗口"主题包"下拉。切主题包保持当前模式。

## 6. 调试

- 日志：`%APPDATA%/XuanHub/xuanhub.log`，`[Theme]` 前缀记录加载/切换/fallback。
- 改 `theme.json` 保存后，触发任意一次配置热重载（如改 config.json 或托盘 "重新加载配置"）即重新扫描 `themes/`——新装的主题包立刻出现在设置窗口。
- 主题包加载失败不会影响其他主题包，也不会让程序崩溃（回落到内置默认）。
- 维护提醒：默认色板在代码内有多处硬编码兜底（`ThemeRegistry` 内置默认、`TrayIcon` 取色兜底、各 QML 防御默认值），调整 `xuanhub-default` 配色时需同步检查这些位置。
