# Agent Note: 设置界面采用 FluentWinUI3

Status: implemented

## Problem

设置界面强制 Basic，并重绘所有表单控件；外观与 Windows 11 的控件、焦点和导航体验不一致。
便携裁剪流程删除 FluentWinUI3 与 Fusion，单改运行样式会导致分发后缺依赖。

## Decision

使用 Qt 6.9.3 自带 FluentWinUI3，并让现有 Ui 控件封装保留业务接口、采用 Qt 自带视觉与交互。
按 WinUI 的左导航、分组设置卡片、适量强调色组织外观和通用页，插件保留列表与详情。
应用明暗模式同步到 Qt colorScheme，完整传递 palette（含 accent）到弹层。
保留草稿、应用、保存、懒加载与关闭释放机制；便携包保留 FluentWinUI3、Fusion、Basic 和共享实现。

内置配色集合与兼容边界见[三套 WinUI 配色](../../implemented/simplification/2026-11-03-winui-theme-selection.md)，本篇的控件与窗口契约继续有效。

插件详情只在顶部保留名称、版本和启用状态，位置、排序、宽度合并为紧凑表单；移除重复的插件设置标题和实现类型提示。
正常运行状态、包管理与插件信息默认折叠，运行异常、依赖安装和迁移继续直接提示。
权限、服务和响应式布局说明归入插件信息，长标识换行，详情仅沿纵向滚动。
玄枢菜单把总开关与逐动作开关合为一张卡片，技术动作标识放入悬停提示。
折叠仅隐藏内容，不销毁编辑器；schema 分组也保留模型，避免非法输入消失后保存按钮被错误放开。
窗口首次创建按鼠标所在屏幕的工作区限制尺寸并居中，防止无边框窗口默认落点越界。
SysInfo 显示项只使用上下按钮排序，移除未实现拖拽的手柄和说明；字段伸缩并为右侧操作预留宽度，
窄行将操作区换行，确保删除按钮完整处于卡片内。

## Historical audit

检索设置、Fluent、Basic、palette、导航及 Settings。
[窗口生命周期](../../implemented/architecture/2026-11-02-runtime-memory-lifetime.md)部分重叠，其草稿与视图释放约束继续有效。
Token Usage、倒计时等命中仅涉及插件专有内容，与本次宿主视觉改造无关。

## Alternatives considered

只改全局样式开关改动最少，但原自绘背景、指示器和文字仍覆盖 Qt 样式，因此同步简化控件封装。
引入第三方 WinUI QML 库可获得更多专用控件，但增加依赖和维护成本，现有 Qt 控件已覆盖本次需求。
即时保存符合 Windows 设置的惯例，但改变现有草稿、验证与热重载事务，超出本次视觉改造范围。
完全删除诊断与权限信息可以减少页面长度，但会失去故障排查入口，因此改为按需展开。
折叠时卸载内容可减少驻留，但会丢失本次编辑的非法输入，与既有窗口生命周期约束冲突，因此只改变可见性。

## References

[Qt 6.9 FluentWinUI3](https://doc.qt.io/archives/qt-6.9/qtquickcontrols-fluentwinui3.html)
说明样式从 Qt 6.8 提供，使用图形资源与 palette，未覆盖控件回退 Fusion。
[Windows 设置设计](https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings)
建议相关项分组、单列卡片与可滚动内容；本项目保留现有显式提交事务。
[Windows 导航原则](https://learn.microsoft.com/en-us/windows/apps/design/basics/navigation-basics)
用于平级导航与插件列表/详情的信息层级。

## Testing

`pixi run build` 完成 Release 构建；CTest 45/45 通过。
新增 QML 回归直接使用实际控件，覆盖折叠/键盘展开后非法输入和草稿保留，以及快捷动作总开关
不清除逐项选择、动作开关按稳定标识写回。
`package_portable.ps1 -NoZip` 完成裁剪与安全门禁，核对 FluentWinUI3、Fusion、Basic、impl 及对应运行库存在。
隔离运行目录清空 Qt/QML 导入环境变量，并从 PATH 排除开发机 Qt 后成功启动真实设置窗口。
实际检查深色 Fluent 控件与 Token Usage 自绘页；紧凑详情以浅色、仅启用菜单/系统信息/时钟核对，
常用设置、快捷动作与折叠入口完整显示，详情无横向滚动，日志没有 QML 加载错误。

字体提供 Segoe UI 与 Microsoft YaHei UI 字体族；插件总数使用 ListView.count，避免显示方法文本。
用户提供的完整设置窗口截图确认标题栏、内容区与底部操作按钮均可见。
不同 DPI、全部插件的专有编辑页、最小化恢复及完整应用/保存流程仍需独立验收。
已有配置、热重载及插件模型自动回归继续通过。

SysInfo 后续修正通过离屏 QML 回归：360、440、520、620 像素页面宽度及反复缩放时，
所有字段和操作按钮都位于行内；上下移动、删除保持指标与前缀/颜色的对应关系。
插件源目录和同步后的运行目录通过 CLI validate。仅修改裸 QML 与翻译，无需重编译宿主；
遵循用户的桌面使用约束，不操作窗口或重启进程；用户自行重启后提供截图，确认删除按钮和行内控件显示正常。

## Consequences

使用 Qt 自带的控件状态、焦点与交互，减少重复样式代码；设置卡片保留主题色板，插件自绘内容继续兼容。
代价是分发保留更多样式依赖，并由 main 维护已应用主题与 colorScheme 的一致性。
Qt 6.9 的 FluentWinUI3 仍有未实现控件，使用 Qt 的 Fusion 回退；图形资源并非所有部位都支持色板重着色。
保留显式保存事务，不切换即时写回；不增加第三方 UI 库，也不声称实现系统 Mica。
真实多屏 DPI、全部第三方插件及未完成的视觉交互仍需独立验收。
