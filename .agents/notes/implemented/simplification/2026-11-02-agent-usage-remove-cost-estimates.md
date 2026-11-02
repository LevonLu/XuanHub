# Agent Note: Agent 用量移除预估金额

Status: implemented

## Problem

用户只需要 Token 消耗与订阅额度，要求 Agent 用量插件不计算也不显示预估使用金额。现有采集器加载模型价格、更新价格缓存，并在总览、工具卡片、日期与模型详情中提供费用估算。

## Decision

删除模型计价实现和内置价格数据，采集与查询只汇总 Token；总览、卡片、详情及中英文语言包移除金额展示与说明。日志中的金额不再提取或换算，Cursor 导出不再要求金额列。订阅额度、Token 估算提示、来源口径与统计周期保留。

现有 SQLite 历史仍可读取，不迁移或清空用户数据；旧事件中的金额字段不参与增量或查询。旧价格缓存保留在用户目录中，但不再读取或更新。

bar 的订阅额度显示由[按工具选择额度窗口](../feature/2026-11-02-agent-usage-bar-quotas.md)扩展，金额移除决定继续成立。

## Historical audit

按 `agent_usage`、`agent-usage`、预估、金额、定价和 `pricing` 检索活跃笔记，无相关历史笔记，无需互链或归档。

## Alternatives considered

仅隐藏 QML 中的金额可以缩小改动，但后台仍会加载价格、联网更新并计算费用，不满足用户同时停止计算的要求，因此移除整个计价链路。

## Testing

- `python -B -m unittest discover -s plugins_py/tests -p 'test_agent*.py'` 通过，包含旧金额字段、累计快照金额单独变化、无价格网络请求和 Cursor 无金额列回归。
- `python -B scripts/xuanhub_plugin.py validate plugins_py/agent_usage --json`、`doctor`、`test --repeat 2` 和 `run` 均通过；Worker 输出已无费用汇总与缺价状态。
- `qmltestrunner` 使用 Basic 样式与 offscreen 平台执行 `plugins_py/tests/qml/tst_AgentUsageDetails.qml` 通过，覆盖携带旧金额数据时总览及日期、模型详情均不显示金额。Qt 的 offscreen 字体目录提示不影响断言。
- 修改同步到本机 Release 运行目录并重启宿主；没有编译 C++。运行目录校验无错误，现有 Python 字节码缓存产生分发警告。

## Consequences

价格表、价格下载和金额汇总不再参与运行，Token、订阅额度及历史数据库继续使用。代价是取消金额及缺价提示；后续若重新引入账单功能，须单独确认真实账单或模型估价的需求。旧用户价格缓存不主动删除，避免无关数据清理。真实账号、不同 DPI 与 Win32 弹窗行为仍需要人工验收。

后续的[Token Usage 账户与列配置](../../implemented/feature/2026-11-02-token-usage-unified-accounts.md)接管统一入口和布局；本篇仍保留各自的历史理由与未被替代的口径约束。
