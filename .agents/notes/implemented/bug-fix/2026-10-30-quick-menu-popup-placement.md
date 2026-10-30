# Agent Note: 玄枢菜单按最终内容尺寸定位弹窗

Status: implemented

## Problem

`plugins/xuanhub_menu/XuanHubMenu.qml` 原来在 `Window.show()` 前按当前高度计算一次位置。
隐藏窗口中的 Column 首次布局、插件动作列表更新都会改变菜单高度，但旧坐标不再更新。
taskbar 模式下锚点位于屏幕底部，菜单变高后底边可能越过可用工作区，首次打开只能显示一部分。

## Decision

玄枢菜单复用宿主 `PluginPopup`，传入根 Item 和作用域 `pluginContext`，保留原来的 7 像素间距。
宿主在显示及尺寸变化后重新定位，延迟显现，统一处理屏幕边界和 bar 点击关闭。
菜单继续在打开前请求宿主状态，菜单动作与高度上限保持原有语义。

## 历史决策检索

检索活跃笔记的 `PluginPopup`、弹窗、taskbar 和定位，未命中相关决定。
现有 `implemented/process/2026-10-30-project-agent-notes.md` 只涉及笔记流程，与此修复无关。
标准弹窗约定已存在于 `docs/plugin-sdk-v2.md`，本次沿用该约定。

## Alternatives considered

- **在原 Window 增加延迟定位及尺寸监听**：能保留独立实现，局部修改量小；但需要重复维护宿主已有的显现时序、屏幕钳制和点击守卫，因此采用标准组件。
- **固定菜单高度**：首次定位可以直接取得确定尺寸；但动作数量与本地化文字动态变化，固定高度会带来空白或滚动，不能表达原有内容自适应行为。

## Verification

- `tests/PluginSlotHealthTest.cpp` 加载真实菜单 QML，用 QTest 点击首次打开；旧实现的底部 taskbar 用例在屏幕包含性断言失败，替换为标准组件后通过。
- 回归覆盖顶部 appbar、底部 appbar，以及配置仍为 top 的底部 taskbar 锚点；首次打开、动作列表增加后的完整窗口均落在可用屏幕内。
- 同一 bar 再次点击关闭，后续点击重开，并通过另一弹窗激活验证互斥关闭。
- 通过 offscreen 与 Windows 原生窗口后端执行上述菜单回归，均通过。原生后端使用独立测试窗口构造锚点，不代表实际 Shell_TrayWnd 嵌入的视觉验收。
- `pixi run build` 通过；`ctest --test-dir build/default -C Release --output-on-failure` 已执行。补齐共享测试上下文的完整色板后，使用 `--rerun-failed` 重跑失败目标通过，其余目标在全量运行中通过。
- `python scripts/xuanhub_plugin.py validate build/default/dist/Release/plugins/xuanhub_menu` 通过且无告警；运行目录中的菜单 QML 与源码哈希一致。

## Consequences

- **收益**：菜单在首次布局和动态内容变化后按实际尺寸重定位，并复用统一的显示时序、边界钳制和点击守卫。
- **代价**：菜单依赖宿主 `XuanHub` QML 模块，并参与单活动弹窗策略，打开时会关闭其他标准弹窗。
- **边界**：未修改公共定位算法、菜单动作和高度上限。实际 Shell_TrayWnd 嵌入、真实 DPI 切换及视觉效果仍需宿主验收；已有进程须完全退出并重启，才能刷新 QML 组件缓存。
