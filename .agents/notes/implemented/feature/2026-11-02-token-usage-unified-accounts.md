# Agent Note: Token Usage 统一账户与可配置列

Status: implemented

## Problem

Agent 用量与 Usage Hub 的额度查询重叠，但余额、手填账户和历史统计分散。固定的 bar 布局无法让用户按列组合 Token、限额和余额。

## Decision

保留 py_agent_usage 身份与历史库，将展示名称改为 Token Usage。列明确选择单块独占或上下双块，工具统计与账户额度各自展示；账户凭据继续使用宿主 schema 密钥事务。启动只在内存合并旧 Hub，在用户采用 Token Usage 设置后保存迁移，移除旧插件可执行入口并保留用户数据。

DeepSeek 金额按相邻余额减少量累计，使用 Decimal，始终标为估算。余额增加标记受影响，换号、换币种及日期边界不复用不可信基线。

## Historical audit

[bar 额度](../../implemented/feature/2026-11-02-agent-usage-bar-quotas.md)的固定布局由列编辑接管，精确窗口匹配继续成立；[移除预估金额](../../implemented/simplification/2026-11-02-agent-usage-remove-cost-estimates.md)的模型计价禁用继续成立，本篇只加入真实余额与余额差；[紧凑弹窗](../../implemented/simplification/2026-11-02-agent-usage-compact-popup.md)的内容分层继续成立。三篇均部分重叠，保留并互链，不归档。其余检索结果无关。

## Alternatives considered

列表自动两两成列能减少编辑控件，但无法明确保留独占列，用户选择逐列指定单双块。保留两个插件能缩小迁移范围，但会重复查询与配置，不能满足统一入口和移除旧 Hub 的要求。余额取绝对差实现简单，却会将充值当作消费，因此拒绝。

## Testing

- `pixi run build` 完成 Release 构建。首次链接因运行实例占用失败，关闭对应实例后重新构建通过。
- 完整 CTest 43/43 通过；部署退役 fixture 验证旧代码移除、用户配置和未知状态文件保留。本机 Release 重启日志确认加载，运行配置 SHA256 保持不变。
- 原 Agent 的 155 项 Python 回归、新账户与列解析的 10 项隔离回归通过；全目录 QML 回归 31 项通过。插件 CLI 的 validate、doctor、两轮 Worker 生命周期及 run 通过。
- 宿主迁移回归覆盖仅旧 Hub、两者共存、重复读取、DPAPI 转存、原配置不写回及跨插件凭据拒绝。既有密钥事务回归继续覆盖失败回滚。
- 原生 Qt 截图核对单列 Token 与双行额度、余额的混排。真实账号、真实不同 DPI 和所有 Win32 交互仍需人工验收，不以离屏截图代替。

## Consequences

统一入口减少重复账户配置，可按列独立组合指标；代价是自绘设置和跨插件兼容导入需要额外维护。账户状态使用独立 SQLite，避免与 Token 写入事务争用。

余额接口没有账单，采样间充值和消费可能相互抵消，估算不能称为完整消费。旧状态缺少币种和凭据身份时保留原文件而重新采样；未来取得正式消费接口时再替换这一估算来源。

部署定向移除旧 Hub 已知代码，保留旧配置及第三方插件，避免整树清理损失用户数据。开发者 CLI 同步传递 Manifest 配置版本，避免新版插件被误当 V1 拒绝；为没有 V1 迁移器的新 V2 插件补充生命周期回归。
