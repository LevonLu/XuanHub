# Token Usage

统一查看 AI 工具的 Token 消耗、订阅额度和按量付费账户余额。内部插件 ID 仍为 `py_agent_usage`，现有 Token 历史库不变；旧 Usage Hub 已移除，其账户由宿主兼容导入。

## 安装和运行

本插件使用独立 Python 采集器，不安装或执行 Node.js，不调用 TokenTracker 的 JavaScript 模块。实现参考 TokenTracker 1.5.1 的固定源码，许可证见 `THIRD_PARTY_NOTICES.md`。

1. 将本目录放入宿主的 `plugins_py/agent_usage/`。
2. pybridge 解释器需要 Python 3.12+，以及匹配该解释器和 x64 架构的 `cryptography >= 41.0`、`zstandard >= 0.22`。依赖由 `plugin.json` 声明，插件不会自动联网安装。
3. 重启 XuanHub，在设置中启用 **Token Usage**。pybridge 总开关由宿主维护。

不再需要配置 Node 路径或 TokenTracker 目录。原配置中的旧字段会被忽略，宿主保存时仍保留未知字段。Ark 两类额度使用已安装的原生 `arkcli` 可执行文件；插件直接运行原生程序，不经过 npm、`.cmd` 或 JavaScript 启动器。没有该客户端时额度显示不可用。

## 显示与设置

设置页包含“状态栏布局”“账户管理”“统计与网络”。每列可选单块独占或上下双块，可增删、左右移动列，交换上下块；单双块切换保留各自选择。示例预览不访问真实账户，保存后由配置监听器应用。

显示块支持 Token 总量或指定工具、5h/7d 已用限额、余额变化估算的今日金额、账户余额、其他额度窗口、重置倒计时与留空。每块可选择来源、币种及标签。默认只显示 Token 总量；原 `barQuota_*` 与 `barPrefix` 在配置版本 2 中转换为列设置。选择只影响展示，不改变采集、Token 合计和弹窗筛选；缺失为 `—`，有效零正常显示，缓存或受影响估算标 `*`。

点击状态栏打开“工具用量 / 账户额度”。工具页保留紧凑卡片和日期、按日、按模型明细；账户页独立于工具筛选，支持全部刷新和单账户刷新，展示套餐窗口、余额与 Go 订阅/Zen 余额兜底状态。Go 滚动窗口未知时长时不标为 5h，不按固定美元限额推算费用。

账户自动复用已有工具登录，也可手填 DeepSeek、GLM、OpenCode Go 的 API Key 或 Cookie。支持多个独立账户、查询间隔及自定义地址；凭据通过宿主 schema 和 DPAPI 密钥事务保护。完整 Cookie 头或单独 auth 值均可用于 Go，未能发现 Workspace 时会提示补充。

DeepSeek 今日金额使用十进制运算，累加相邻成功采样的余额减少量，始终显示 `≈` 和“余额变化估算”。余额增加不计为消费，且提示受充值、退款等影响；同一采样间的消费与充值仍可能抵消。按账户、凭据指纹、币种与本地业务日隔离状态，不换汇。新账户每日边界默认为零点，旧账户保留原起始时间；首次或跨日缺少边界采样时显示“自本次采样起”。旧状态无凭据身份时不会冒用为可信基线，原文件保留。

宿主启动只在内存合并旧 Hub 的配置与合法密钥引用；在本页“采用合并配置”或编辑设置后点全局应用，才将账户安全转存到当前插件并移除旧配置条目。仅保存主题、语言或其他插件不顺带提交该迁移。无法解密的账户保留并提示补充凭据；反复启动不会重复导入。

工具显示范围仍可设为全部、本周期有 Token 消耗或自定义选择。周期支持今天、近 7 天、近 30 天、全部历史；详情日期包含首尾全天，按本地日期划分。详情只读本地数据库，需要最新数据时先刷新总览。

Token 分别显示非缓存输入、输出、缓存读取、缓存写入和推理。是否将推理计入输出取决于工具自己的记录契约，不能把所有列直接相加。K = 千、M = 百万、B = 十亿、T = 万亿，最多显示两位小数；缓存和查询保持原始计数精度。

独立适配器覆盖参考版本的本地来源，包括 Codex/AStudio/Every Code、Claude Code、Gemini、Antigravity、Kiro、OpenCode/Kilo CLI/MiMo/DuMate/ZCode、OpenClaw、CodeBuddy、WorkBuddy/WorkBuddy AI、OMP/OmO/Pi/Prime Agent、MiniMax、Craft、Reasonix、Muse、Kimi/Kimi Code、Droid、Cline、Roo/Kilo Code、Grok、Qoder/Qoder CN、Zed、Goose、Hermes、Devin、Copilot、Claude Science、AnythingLLM、OpenSquilla、LM Studio、Unsloth 和 Command Code/DSH。Pi 的后端来源动态列出。Cursor 和 TRAE Work CN 另通过账号接口采集，TRAE 本地加密数据库单独解析。

额度包含 Claude、Codex、Cursor、Kimi、Gemini、Kiro、Antigravity、Copilot、Grok、ZCode、OpenCode Go、Command Code、Devin、Qoder、Qoder CN、Ark Coding Plan、Ark Agent Plan、WorkBuddy 共 18 类提供方。Devin 额度默认关闭。工具覆盖不代表每种工具都有额度接口，也不代表所有真实账号均已验收；接口随服务更新可能需要调整。

- 本地发现覆盖 Windows 和**已经运行的** WSL，不启动已停止的发行版。保留上游相关目录环境变量；无权限目录跳过，单源解析失败保留历史并显示提示。
- 不安装日志钩子，不修改遥测开关，只读已有记录。默认每 300 秒刷新；首次扫描完整历史，后续只重读变化文件及其伴随元数据。
- Cursor、TRAE Work CN 可能包含其他设备，默认在详情中显示，但不计入状态栏合计。TRAE CN 保留 `TOKENTRACKER_TRAE_CN_USAGE=1` 显式开关，接口最近窗口会累积保存在插件数据库中。
- 每个事件使用稳定身份；归档复制、fork、多存储镜像按来源规则去重。追加日志轮转保留旧历史，结构化记录修正按最新身份更新。累计快照记录增量，不把历史全部移到最新日期。
- 只有累计快照的工具，首次采集无法恢复过去每次请求的时间；沿用工具提供的会话时间。切换到独立采集器时会从现存记录重建，不自动导入旧 TokenTracker 汇总队列，旧缓存保留。已被工具删除且只存在旧缓存中的历史无法由原始文件重新生成。
- Kiro CLI、Antigravity 或 Grok 缺少精确计数时保留上游估算方式，并在 UI 标明。TRAE 加密数据库只在内存解密，数据库或 WAL 超过 256 MiB 时明确报告不可读，不显示虚假零用量。
- 不计算模型计价预估，不加载价格表；账户真实余额与余额差估算单独展示。

## 数据、网络和生命周期

缓存位置为 `%LOCALAPPDATA%/XuanHub/agent-usage/native-v2/`：`usage.sqlite3`、仅含白名单统计及凭据指纹的额度缓存、独立代理配置；`accounts.sqlite3` 保存账户余额与采样状态。原始对话正文、访问令牌、账号资料及 HTTP 错误正文不写入统计库或送入 QML。

首次“自动迁移已有代理”会读取旧 TokenTracker 代理设置并存为插件自己的配置，之后无需旧目录存在。也可选择系统代理、关闭代理、手动 HTTP/HTTPS/SOCKS5 代理。手动地址为敏感设置，由宿主保护；无效手动代理不会悄悄直连。系统模式兼容 HTTP(S)/ALL_PROXY、NO_PROXY 和 Windows 代理配置。

访问各工具的官方额度接口使用其已有登录状态，必要时刷新并原子保存过期凭据；刷新期间若登录文件已改变则拒绝覆盖。可以关闭套餐额度采集。工具未登录、服务不可用、没有额度窗口和不支持会分别显示；有效缓存额度标明时效，换号后失效。

Token、额度和详情分别在独立 Python 子进程运行，有超时及 Windows Job 回收。热更新、停用和退出终止对应进程树。SQLite 事务在整轮完成后发布，详情可同时读取上一轮完整快照；单源失败不会清空其他来源。

## 验证

```powershell
python -B -m unittest discover -s plugins_py/tests -p 'test_agent*.py' -v
python scripts/xuanhub_plugin.py validate plugins_py/agent_usage --json
python scripts/xuanhub_plugin.py test plugins_py/agent_usage --repeat 2 --json
python scripts/xuanhub_plugin.py doctor plugins_py/agent_usage --json
python scripts/xuanhub_plugin.py run plugins_py/agent_usage --json
```

单测使用隔离 fixture，不访问真实账号，不依赖 Node。覆盖解析口径、加密、WAL、发现、额度、代理、累计增量、日期、轮转、事务、取消和子进程树回收。开发迁移过程中另用固定上游做差分验证；参考仓库和临时 oracle 脚本不随插件分发。

QML 交互回归见 `plugins_py/tests/qml/tst_AgentUsageDetails.qml`，使用 Basic 样式。改 Python、Manifest 或 QML 后须重启真实宿主；自动测试不替代不同 DPI、弹窗位置和真实账号的人工验收。

参考源码：[TokenTracker](https://github.com/xiufengsun/TokenTracker/tree/f80ed15a0976f076a3f41ccdd9a202ac7d09d409)，主要为 `src/lib/rollout.js`、`src/lib/usage-limits.js`。
