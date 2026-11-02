# Agent 用量

查看各类 AI 工具的 Token 消耗和订阅额度。状态栏默认只显示所选周期总量，使用 K、M、B、T 自适应单位，不带 AI 和 tokens 文案；点击打开工具列表，再点击工具查看指定日期范围、按日及按模型明细。

## 安装和运行

本插件使用独立 Python 采集器，不安装或执行 Node.js，不调用 TokenTracker 的 JavaScript 模块。实现参考 TokenTracker 1.5.1 的固定源码，许可证见 `THIRD_PARTY_NOTICES.md`。

1. 将本目录放入宿主的 `plugins_py/agent_usage/`。
2. pybridge 解释器需要 Python 3.12+，以及匹配该解释器和 x64 架构的 `cryptography >= 41.0`、`zstandard >= 0.22`。依赖由 `plugin.json` 声明，插件不会自动联网安装。
3. 重启 XuanHub，在设置中启用 **Agent 用量**。pybridge 总开关由宿主维护。

不再需要配置 Node 路径或 TokenTracker 目录。原配置中的旧字段会被忽略，宿主保存时仍保留未知字段。Ark 两类额度使用已安装的原生 `arkcli` 可执行文件；插件直接运行原生程序，不经过 npm、`.cmd` 或 JavaScript 启动器。没有该客户端时额度显示不可用。

## 查询和统计口径

点击 bar 打开紧凑总览：宽度为 420 逻辑像素，少量工具时高度随内容收缩，工具较多时列表滚动。弹窗不提供即时搜索或筛选提示；工具显示范围仍在插件设置中选择。卡片集中显示用量与套餐额度，点击后查看 Token 分项、日期及模型明细。

“设置 → 插件 → Agent 用量 → 状态栏 → 用量前缀”支持自定义文字或 emoji，留空即只显示数字，例如 `3.67M`；填写 `🤖` 后显示 `🤖 3.67M`。保存后即时更新，状态栏宽度随内容调整，较长前缀会省略显示。

在“状态栏套餐额度”中，可为各 agent 或套餐分别选择“不显示、仅 5h、仅 7d、5h 和 7d”，默认均不显示。数值为套餐限额的**已用百分比**，有效零显示 `0%`。只显示一种时段时，第一行显示 Token 总用量，第二行显示所选限额（如 `Codex 5h 42%`）；同时显示 5h 和 7d 时，Token 总用量保持独立一列，限额区域第一行显示 5h、第二行显示 7d。多个工具按列排列，同一时段的限额保持同行。Codex Spark 独立设置，Ark Coding Plan 与 Ark Agent Plan 分别设置。选择即时生效，与弹窗筛选、Token 统计周期及账号来源是否计入总量无关。

只显示接口明确标识为五小时或七天的整体窗口，不将模型子额度或月额度冒充整体周期。未登录、未返回对应窗口、时长未知或关闭“获取套餐额度”时显示 `—`；旧缓存标记 `*`。额度区域随所选工具数量加宽；手动固定插件宽度过小时仍可能被宿主裁剪。

在“设置 → 插件 → Agent 用量 → 点击后的工具列表”中，将“显示范围”改为“自定义选择”，再勾选希望查看的工具并应用。默认“自动显示全部”保持原有行为；切换模式会保留之前的勾选。Pi 和 Prime Agent 的选项包含各自动态发现的后端，“其他工具”控制尚未列入选项的来源。

也可选择“仅本周期有 Token 消耗”：只展示当前统计周期内 Token 总数大于 0 的工具，随统计周期和采集结果更新。只有历史记录、登录状态或套餐额度的工具不会列出；该模式忽略自定义勾选，并优先于“列出无记录的工具”。

选择只影响点击状态栏后的工具卡片和详情入口，不停止采集、不删除历史，也不改变状态栏及弹窗顶部的总量口径。全部显示和自定义模式中，没有记录或登录状态的工具仍受“列出无记录的工具”控制；自定义模式没有勾选时列表为空。应用设置后立即更新列表，不中断采集或正在进行的日期查询；当前详情工具被筛除时会返回列表。

周期可选今天、近 7 天、近 30 天、全部历史。详情开始和结束日期均包含全天，以系统本地日期划分；结束日期的次日零点为右开边界。详情只查询本地数据库，不重新扫描 Agent 或联网。需要最新数据时先刷新总览，完成后重新查询。

Token 分别显示非缓存输入、输出、缓存读取、缓存写入和推理。是否将推理计入输出取决于工具自己的记录契约，不能把所有列直接相加。K = 千、M = 百万、B = 十亿、T = 万亿，最多显示两位小数；缓存和查询保持原始计数精度。

独立适配器覆盖参考版本的本地来源，包括 Codex/AStudio/Every Code、Claude Code、Gemini、Antigravity、Kiro、OpenCode/Kilo CLI/MiMo/DuMate/ZCode、OpenClaw、CodeBuddy、WorkBuddy/WorkBuddy AI、OMP/OmO/Pi/Prime Agent、MiniMax、Craft、Reasonix、Muse、Kimi/Kimi Code、Droid、Cline、Roo/Kilo Code、Grok、Qoder/Qoder CN、Zed、Goose、Hermes、Devin、Copilot、Claude Science、AnythingLLM、OpenSquilla、LM Studio、Unsloth 和 Command Code/DSH。Pi 的后端来源动态列出。Cursor 和 TRAE Work CN 另通过账号接口采集，TRAE 本地加密数据库单独解析。

额度包含 Claude、Codex、Cursor、Kimi、Gemini、Kiro、Antigravity、Copilot、Grok、ZCode、OpenCode Go、Command Code、Devin、Qoder、Qoder CN、Ark Coding Plan、Ark Agent Plan、WorkBuddy 共 18 类提供方。Devin 额度默认关闭。工具覆盖不代表每种工具都有额度接口，也不代表所有真实账号均已验收；接口随服务更新可能需要调整。

- 本地发现覆盖 Windows 和**已经运行的** WSL，不启动已停止的发行版。保留上游相关目录环境变量；无权限目录跳过，单源解析失败保留历史并显示提示。
- 不安装日志钩子，不修改遥测开关，只读已有记录。默认每 300 秒刷新；首次扫描完整历史，后续只重读变化文件及其伴随元数据。
- Cursor、TRAE Work CN 可能包含其他设备，默认在详情中显示，但不计入状态栏合计。TRAE CN 保留 `TOKENTRACKER_TRAE_CN_USAGE=1` 显式开关，接口最近窗口会累积保存在插件数据库中。
- 每个事件使用稳定身份；归档复制、fork、多存储镜像按来源规则去重。追加日志轮转保留旧历史，结构化记录修正按最新身份更新。累计快照记录增量，不把历史全部移到最新日期。
- 只有累计快照的工具，首次采集无法恢复过去每次请求的时间；沿用工具提供的会话时间。切换到独立采集器时会从现存记录重建，不自动导入旧 TokenTracker 汇总队列，旧缓存保留。已被工具删除且只存在旧缓存中的历史无法由原始文件重新生成。
- Kiro CLI、Antigravity 或 Grok 缺少精确计数时保留上游估算方式，并在 UI 标明。TRAE 加密数据库只在内存解密，数据库或 WAL 超过 256 MiB 时明确报告不可读，不显示虚假零用量。
- 不计算或显示预估使用金额，不加载模型价格，也不联网更新价格缓存。

## 数据、网络和生命周期

缓存位置为 `%LOCALAPPDATA%/XuanHub/agent-usage/native-v2/`：`usage.sqlite3`、仅含白名单统计及凭据指纹的额度缓存、独立代理配置。原始对话正文、访问令牌、账号资料及 HTTP 错误正文不写入统计库或送入 QML。

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
