# Agent Note: Agent 用量弹窗收窄与内容分层

Status: implemented

## Problem

Agent 用量弹窗固定为宽 620、高 660，单个工具也占用大量空白。弹窗的搜索框、筛选说明、分项 Token 和来源文案同时出现，主要用量与套餐额度不够集中。用户要求移除弹窗的 Agent 筛选并重新设计更窄的界面。

## Decision

移除弹窗的即时搜索框及筛选提示，设置页已有的工具显示范围继续生效。总览宽度改为 420 逻辑像素，高度由内容决定，并受当前屏幕可用高度约束；工具多时仅列表滚动，详情采用固定的舒适高度。复用 `PluginPopup` 的动态定位与互斥规则。

总览集中显示 Token 总量、统计周期和紧凑工具卡片。卡片显示工具名、套餐、总用量及额度进度；分项 Token 留在点击后的详情页，异常与缓存状态继续可见。更新时间缩短为时分。

## Historical audit

检索 agent、弹窗和筛选，命中[bar 额度](../../implemented/feature/2026-11-02-agent-usage-bar-quotas.md)、[移除金额](../../implemented/simplification/2026-11-02-agent-usage-remove-cost-estimates.md)和[共享弹窗定位](../../implemented/bug-fix/2026-10-30-quick-menu-popup-placement.md)。它们的 bar 布局、金额边界与宿主定位约定继续成立，本次只收窄点击后的数据展示，无须归档；其他命中无关。

## Alternatives considered

只缩窄原布局可以减少代码改动，但仍保留搜索框和单工具的大块空白，且分项文案会频繁换行，因此同时调整信息层级和高度。删除设置页的工具范围可以彻底取消所有筛选，但用户针对的是弹窗界面，该设置仍承担隐藏无关工具的职责，因此保留。

## Testing

- Basic 样式、offscreen 平台运行 `plugins_py/tests/qml` 通过，使用构建目录中的宿主 QML 测试映射。覆盖少量工具缩高、多工具限高滚动、刷新、日期查询、返回和设置隐藏当前工具。
- 插件 CLI 的 `validate` 与 `run` 通过，中英文语言包键一致。最小 Qt 宿主渲染浅色、深色、多额度、英文及详情截图并检查；单工具示例的自然高度为 279 逻辑像素。
- 使用 `pixi run cmake --build --preset default --target xuanhub_plugin_slot_health_tests -- /p:BuildProjectReferences=false` 定向构建回归目标，执行 `agentUsageClickOpensPopupThroughSlot` 的立即点击与悬停后点击场景均通过，验证动态高度、宽度、边界、关闭与重开。未声称完成全量宿主构建。
- 本机运行目录同步界面及语言文件后重启，日志确认插件正常加载，用户配置保持不变。

## Consequences

总览集中显示主要用量和限额，少量工具不占据固定大块空白。代价是查看完整 Token 分项需要点击进入详情。动态高度以内容自然尺寸计算，避免父子布局循环，并由宿主重新定位防止越界；真实账号和不同 DPI 的人工验收仍未覆盖。

后续的[Token Usage 账户与列配置](../../implemented/feature/2026-11-02-token-usage-unified-accounts.md)接管统一入口和布局；本篇仍保留各自的历史理由与未被替代的口径约束。
