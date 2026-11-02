# Agent Note: 在项目内启用决策笔记与统一校验

Status: implemented

## Problem

项目原有 `AGENTS.md` 记录工程约定，缺少独立的决策笔记目录与校验入口。
用户要求安装 `write-notes-like-deepseek` 并在当前项目启用，使后续非平凡改动的理由、备选与验证依据能够随仓库保存。

## Decision

- 上游技能完整保存在 `.agents/skills/write-notes-like-deepseek/`，供当前项目及其子目录发现。
- 安装来源为 `czm15053/write-notes-like-deepseek` 的 `skills/write-notes-like-deepseek/`，固定提交 `72573a5b179de7798bbb7c4aeacdc0df6bf62439`，保留上游文件内容。
- `AGENTS.md` 明确触发范围、历史检索、生命周期和提交前校验要求。
- 采用 `.agents/notes/{lifecycle}/{class}/yyyy-mm-dd-topic.md`，仅创建实际使用的目录。
- `pixi run verify-notes` 沿用现有任务入口，按目录、格式、归档封印顺序运行上游三个校验器；通过 `npx --yes tsx@4.23.15` 执行。

技能副本的版本控制边界由[决策笔记与外部技能的版本管理](2026-11-02-agent-notes-version-control.md)补充；项目内安装与统一校验入口继续沿用本篇决定。

## 历史决策检索

初始化前 `.agents/notes/`、`docs/adr/` 与 `docs/decisions/` 均不存在，没有同主题笔记需要更新、互链或归档。现有工程约定继续由 `AGENTS.md` 承载。

## Alternatives considered

- **安装到用户全局技能目录**：可供多个仓库复用，也符合安装器的默认位置；但当前需求明确限定当前项目，项目内安装让维护者按同一路径使用技能与校验脚本，因此选择项目目录。
- **新增 `package.json` 并通过 npm scripts 编排**：与上游示例一致，也便于 npm 管理运行器；但本项目已经使用 Pixi 统一管理任务且没有 `package.json`，因此采用 Pixi 调用固定版本的 `tsx`。

## Consequences

- **收益**：使用规则与校验命令随项目保存，后续代理有明确的历史检索与留痕入口，机械改动免写笔记。
- **代价**：校验依赖本地安装的技能、Node.js 与 npm/npx；首次运行或本地无缓存时需要访问 npm 仓库。维护者从上游获取技能副本，更新时检查规则与脚本差异。
- **边界**：校验覆盖目录、格式、相对链接与归档封印；笔记语义质量仍需人工或代理自检。当前入口为本地 Pixi 任务；需要合并前强制执行时再接入 CI。

## Verification

- 安装完整性检查确认项目技能目录包含 21 个文件，技能入口、引用文档、模板、校验脚本与看板资源均存在。
- 在项目根目录运行 `pixi run verify-notes`，目录与相对链接、格式、归档封印三项校验通过。
- `git diff --check` 通过。本次接入仅涉及开发流程与笔记，没有执行 Qt/MSVC 构建或宿主运行验收。
