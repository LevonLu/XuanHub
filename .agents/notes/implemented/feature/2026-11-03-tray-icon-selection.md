# Agent Note: 托盘同步主框架图标选择

Status: implemented

## Problem

主框架图标选择只影响状态栏入口，系统托盘仍显示固定图片。同一应用的两个入口外形不一致，
用户要求设置图标时同步更新托盘。

## Decision

托盘直接监听 `ConfigService::menuIconChanged`，复用 `bar.menuIcon`，不增加第二个设置。
首次启动读取已生效的选择，保存后与菜单一起更新，Explorer 重建时沿用当前图标。
原版保留原图片；A、C 由 QPainter 按菜单相同的 24×24 几何规则绘制，使用主题前景色，
生成 16、20、24、32、40、48、64 像素图像交给 QIcon 选择，避免只缩放一张小图。

## Historical audit

检索托盘图标、QIcon、BrandIcon 和 menuIcon，命中[菜单图标选择](../../implemented/feature/2026-11-03-menu-icon-selection.md)。
本篇部分扩展其适用入口，替代其中“托盘不变”的范围限制；原有配置、草稿和菜单通信决定继续有效。
其余设置视觉笔记不持有托盘绘制决定。

## Alternatives considered

让托盘离屏渲染 BrandIcon.qml 可以只维护一份绘制代码，但需要为一个静态托盘图标引入 Qt Quick
渲染场景、异步抓取与生命周期管理。使用原生 QPainter 更直接；几何规则在图标文档中统一约定。

预制单张 PNG 最容易加载，但会增加资产并在非原始尺寸缩放；按目标尺寸绘制更符合原始可复现要求。

## Verification

`pixi run build` 完成 Release 构建，完整 CTest 45/45 通过；`PluginSlotHealthTest` 的新增回归使用真实的原生绘制函数，
检查 100%、125%、150%、200% DPI 对应的图像尺寸、透明背景、非空内容、A/C 中心区别与换色结果。
真实宿主已恢复运行，菜单正常加载并嵌入任务栏，运行配置 SHA-256 未变。
配置持久化与非法值回退继续由已有 ConfigService 回归覆盖。

## Consequences

同一个设置控制菜单和托盘，保存后同步生效，重启时恢复选择，不增加另一份用户配置。
QML 与 QPainter 分别实现同一几何规则，未来修改比例时须同步维护；无需离屏 Qt Quick 场景。
自动检查覆盖原生图像输出；真实 Shell 中的图标切换、Explorer 重建与多 DPI 视觉仍需独立验收，
本次未更改用户实际配置来执行这些操作。
