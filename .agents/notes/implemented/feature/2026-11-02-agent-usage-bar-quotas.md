# Agent Note: Agent 用量按工具选择 bar 额度窗口

Status: implemented

## Problem

bar 只显示 Token 总量，查看套餐限额必须打开弹窗。用户需要按 agent 或套餐分别选择是否显示 5h、7d 已用额度。

## Decision

为现有额度提供方和 Codex Spark 分别提供 `barQuota_<id>` 枚举：`none`、`5h`、`7d`、`both`，默认 `none`。bar 保留 Token 总量与自定义前缀，按稳定名称顺序显示所选窗口的已用百分比；额度选择与弹窗列表筛选独立，热更新不重启采集或中断详情查询。

只选一种时段时，总量在上、限额在下，共用一列宽度；同时选择 5h 与 7d 时，总量独立一列，额度列中 5h 在上、7d 在下。多来源混合选择仍按时段对齐，未选择的行留空；显示的是原有 Token 总用量，不增加逐 agent Token 汇总。

点击后的总览采用[紧凑弹窗](../simplification/2026-11-02-agent-usage-compact-popup.md)，设置页的工具范围与 bar 额度选择继续独立。

只匹配窗口 `seconds` 为 18000 或 604800 的记录，优先对应提供方的整体窗口，不混用 Codex Spark 或 Claude 模型子额度。Ark 明确的 5h、weekly 标签和 OpenCode Go 的 weekly 标签补充时长；不依据重置倒计时推测窗口长度。缺失、不支持、停用或未登录显示 `—`，有效零显示 `0%`，缓存值标记 `*`。新增额度区域按内容扩展宽度，原 Token 前缀继续限制长度。

## Historical audit

检索活跃笔记中的 agent、额度和限额，命中[移除预估金额](../../implemented/simplification/2026-11-02-agent-usage-remove-cost-estimates.md)。该决定保留 Token 与订阅额度，与本功能部分重叠且继续成立；两篇互链，不归档。其余命中无关。

## Alternatives considered

- 全局一个 5h、7d 开关配置简单，但无法逐工具控制，不满足用户要求。
- 按主、次窗口位置分别标为 5h、7d 可兼容缺少时长的数据，但 Cursor 等提供方的同名窗口可能代表月额度，会误报周期，故只显示有明确时长的记录。

## Testing

- `python -B -m unittest discover -s plugins_py/tests -p 'test_agent*.py'` 通过；覆盖四种选择、零值、缺失、缓存、额度停用、别名、整体与 Spark 窗口隔离，以及热更新不重采集。
- 插件 CLI 的 `validate`、`doctor`、`test --repeat 2` 和 `run` 均通过。中英文语言包上下文与键集合一致。
- Basic 样式、offscreen 平台的 Agent 额度与详情 QML 回归通过。运行整个 `plugins_py/tests/qml` 时，独立运行器缺少宿主 qrc；在构建目录复制 `app/qml`，复用生成的 XuanHub `qmldir` 并去掉资源优先路径后，全目录回归通过。该测试映射不修改产品源码。
- 最小真实 Qt 宿主截图核对多工具完整内容、有效零、缺失与缓存提示；bar 根宽度随内容扩展。本机 Release 插件文件已同步并重启宿主，用户配置保持不变，没有编译 C++。
- 两行布局回归覆盖单选 5h、单选 7d、双时段独立总量列、混合来源时段对齐、无额度和长前缀；分别渲染单、双时段截图检查行距与边界。

## Consequences

用户可按提供方选择需要常驻显示的已用百分比，默认继续只显示 Token 总量。代价是增加配置项，选择多个提供方会加宽 bar；固定宽度仍受宿主槽位裁剪。缺少明确时长的提供方显示未知，不能由本地 Token 日志推算真实套餐额度。真实账号、不同 DPI 与 Win32 弹窗仍需人工验收。
