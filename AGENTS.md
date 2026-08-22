# AGENTS.md — 玄枢 / XuanHub

> 面向 AI 编码代理的项目说明。阅读本文件即可了解项目全貌（含设计决策与验收清单）。

## 1. 项目概览

**玄枢（XuanHub）** 是一个基于 **Qt6 / QML 的 Windows 插件化状态栏框架**（对标 polybar / waybar / Rainmeter），当前版本 0.4.0。

- `XuanHub.exe`：无边框、置顶、`Qt::Tool` 的 `QQuickView`，两种形态（`bar.mode`）：`appbar`（默认，Win32 AppBar 占住主屏顶部/底部工作区）；`taskbar`（`SetParent` 嵌入原生任务栏左侧，透明、不占工作区）。
- 原生插件是独立 DLL（`QPluginLoader` 加载），Python 插件由 pybridge 托管；两者都使用 QML 界面。内置原生插件包括 `clock`、`sysinfo`、`xuanhub_menu` 和 host 插件 `pybridge`。
- 仅支持 Windows，直接用 Win32 API，不做跨平台抽象。
- `pybridge` 桥接插件为 `plugins_py/` 下每个 Python 子插件启动独立的 `python.exe` worker，并以受随机令牌认证的 loopback IPC 连接 QML 代理；Python 崩溃、GIL 卡死或扩展模块异常不会进入宿主。分发默认依赖用户自装 CPython ≥3.12，打包脚本可选 Bundle 策略带上解释器与标准库（见 §5、§8）。

## 2. 技术栈

| 维度 | 选型 |
|---|---|
| 语言 / 标准 | C++17（无编译器扩展） |
| UI | Qt 6.9.3 — Qt Quick / QML（另有 Widgets、Network） |
| 编译器 | **MSVC 2022（VS 17，x64）**；不支持 MinGW 工具链 |
| 构建 | CMake ≥ 3.21 + `CMakePresets.json`，VS 17 2022 生成器 |
| 插件加载 | `QPluginLoader` + `Q_PLUGIN_METADATA`；新插件使用 `xuanhub.BarPlugin/2.0`，V1 与 host IID 保持兼容 |
| Python 桥接 | Qt `QTcpServer/QTcpSocket` + 每子插件独立 `python.exe` worker；4 字节长度前缀 JSON、一次性随机令牌认证，仅绑定 `127.0.0.1` |
| 系统 API | Win32：`SHAppBarMessage`、`GetIfTable2`、注册表 Run 键、`RegisterWindowMessage("TaskbarCreated")` |
| 配置 | JSON（`QJsonDocument`），热重载（`QFileSystemWatcher`） |

## 3. 构建与运行

宿主和 C++ 插件的构建前提是 Qt 6.9.3 msvc2022_64。若仓库默认预设无法定位 Qt，请在已被 gitignore 的 `CMakeUserPresets.json` 中按本机安装位置覆盖 `CMAKE_PREFIX_PATH`。只开发 Python 子插件时不需要 Qt/MSVC，使用 §5 的便携宿主流程。

pybridge 不要求构建机安装 Python 开发包；它始终参与构建。运行时若找不到 Python ≥3.12 的 `python.exe`、标准库和 `python3XX.dll`，pybridge 自动停用子插件并托盘提示，其余功能照常运行。

```bash
cmake --preset default                 # 配置（生成到 build/default）
cmake --build --preset default         # Release
cmake --build --preset default-debug   # Debug
```

常用命令封装为 pixi 任务（见 `pixi.toml`）：`pixi run build`、`build-debug`、`run`、`run-settings`、`package` / `package-bundle` / `package-nozip`、`setup-config`、`deploy`、`log`、`clean`。

- 运行时输出统一在 **`build/default/dist/<CONFIG>/`**：`XuanHub.exe`、`barplugin.dll`、`plugins/<id>/<id>.dll + <Id>.qml + metadata.json`（根目录由顶层 `XUANHUB_BIN_DIR` 定义）。pybridge 额外同步 `plugins_py/` 整树（含 `bridge_worker.py`），不再产出或加载 `pybackend.dll`。
- **注意**：根 `config/config.json` 是开发示例，CMake 不自动拷贝，需手动复制到 dist（或 `pixi run setup-config`）；没有时退用 `%APPDATA%/XuanHub/config.json`（首运行从 qrc 播种）。
- 运行：直接执行 `build/default/dist/Release/XuanHub.exe`。普通模式的日志写 `%APPDATA%/XuanHub/xuanhub.log`、单实例锁写 `%TEMP%/XuanHub.lock`；exe 同目录存在 `portable` 标记时，配置、密钥库、日志和锁均改放 exe 同目录。日志每次启动 Truncate，同时输出到 `OutputDebugString`。

## 4. 目录结构

```
XuanHub/
├── CMakeLists.txt / CMakePresets.json   # 顶层工程与预设
├── config/config.json                   # 开发用示例配置（手动拷贝到 dist）
├── lib/barplugin/                       # 插件接口共享库 barplugin.dll（SHARED）
│   └── include/barplugin/               # V1/V2 插件接口、HostServices、EventBus、运行时模型
├── app/                                 # XuanHub.exe 宿主
│   ├── src/                             # main.cpp、BarWindow、AppBarWin32、WorkAreaGuard、TaskbarEmbed、
│   │                                    # ConfigService、ConfigHotReloader、Settings*（设置窗口）、
│   │                                    # PluginManager、PluginListModel/FilterModel、ThemeManager、
│   │                                    # ThemeRegistry、TrayIcon、AutoStart.h
│   ├── qml/                             # main.qml / Bar.qml（三栏）/ PluginSlot.qml（Loader 槽）
│   │                                    # / SettingsWindow.qml / Ui*.qml（设计令牌 + 自绘控件库）
│   └── resources/default_config.json    # qrc 内置默认配置（首运行播种用）
├── plugins/
│   ├── clock/  sysinfo/  xuanhub_menu/  # 内置可见原生插件
│   └── pybridge/                        # Python host + PyWorkerViewModel（无 CPython 链接）
├── plugins_py/                          # Python 子插件 + bridge_worker.py（README 即开发契约）
├── themes/                              # 主题包（ThemeRegistry 扫描；README 即开发契约；xuanhub-default 兜底勿删）
├── docs/                                # 统一导航、SDK、路线图、i18n、插件包与更新契约
├── scripts/xuanhub_plugin.py            # 插件 create/validate/test/run/doctor/package 统一 CLI
├── examples/plugins/v2_demo/            # 不依赖宿主内部目标的 C++ SDK V2 示例
├── tests/                               # CTest、Python Worker 与 CLI 回归测试
└── LICENSE                              # BSD-3-Clause；源码与二进制分发均须保留
```

依赖方向：**插件只链接 `barplugin`**，宿主 `app` 也链接 `barplugin`。`barplugin` 必须是 **SHARED**（保证宿主与插件 DLL 共享同一份 `EventBus` 类型/单例语义）。插件对宿主核心零依赖，核心仅通过接口拿到 `QObject* viewModel()` 与 `QUrl qmlUrl()`。

## 5. 运行时架构要点

```
QML 渲染层 (app/qml)     main.qml → Bar.qml（三栏）→ PluginSlot.qml（Loader）
插件适配层 (app/src)     PluginManager · PluginListModel · ConfigService · ThemeManager · TrayIcon
横切服务 (lib/barplugin) IBarPlugin2/V1 · PluginContext2 · HostServices · EventBus V2/V1
Win32 平台层 (app/src)   BarWindow（QQuickView）· AppBarWin32 · WorkAreaGuard · TaskbarEmbed
                         ↑ 加载 ↑   plugins/<id>/<id>.dll + <Id>.qml（+ plugins_py/ 经 pybridge）
```

### 插件加载流程（`PluginManager::loadFrom`）

扫描 `<exeDir>/plugins/*/metadata.json` 与 DLL → 在执行代码前校验 Manifest、路径、宿主范围、
SDK、架构、依赖和权限 → 加载 DLL 并按 IID 选择 `IBarPlugin2` 或 V1 适配器 → 按依赖拓扑与配置
初始化 → 注入作用域化事件、HostServices、语言、主题和插件配置 → 收入 `PluginListModel`。V2 的
sidecar 与运行时 Descriptor 必须一致；V1 继续兼容，但新插件契约以 `docs/plugin-sdk-v2.md` 为准。

**host 插件分支**：`init` 后若命中 `IBarPluginHost`（如 pybridge），自身不进可见列表，走两阶段契约：`subPluginIds()` 枚举 → 核心按 config 过滤 → `createSubPlugin(id, cfg)` 实例化。子插件以 `iface=nullptr` 收入模型（生命周期归 host）；`shutdownAll()` 中 host **最后** shutdown（子插件 VM 依赖 host 持有的运行时），随后统一 `unload()`。

### QML 契约（写插件时必须遵守）

- V1/V2 插件只向宿主提供 ViewModel 和 QML URL，**绝不返回 `QWidget*`/`QQuickItem*`**；QML 实例化由核心统一做。
- 每个插件 QML 的**根 `Item` 必须声明 `property var plugin`**；`PluginSlot.qml` 会把 viewModel 注入该属性。插件内用 `plugin ? plugin.x : fallback` 防御空值。
- 槽位（`PluginSlot`）`clip: true`：插件根 Item 声明的 width 即其占位宽度，**任何超出槽位边界的绘制都会被裁掉**（ListView 只裁整列不裁单个 delegate，槽位不裁剪时溢出内容会直接画到相邻插件上）。插件必须保证根 Item 宽度 ≥ 实际内容宽度；固定宽模式下被截断属预期行为，由插件自行处理对齐。
- Manifest 可声明 `layout.region/minWidth/preferredWidth/maxWidth/flex/priority/overflow`。appbar 按全栏预算先收缩低 priority，再执行 `hide/menu`；`menu` 进入末尾 overflow 弹窗。未声明者、taskbar 紧凑模式和 config `width > 0` 保持旧语义，完整契约见 `docs/plugin-sdk-v2.md`。
- 不用 `QML_ELEMENT` 静态注册——外部 DLL 无法在宿主编译期注册类型；`Loader + source URL` 是既定方案。
- 宿主 QML 根上下文的 context property（插件 QML 同引擎可访问）：`pluginModel`（含 left/center/right 过滤子模型）、`themeManager`（`themeId`/`mode`/`colors` 11 字段色板，用 `themeManager.colors.bg` 取色）、`barConfig`、`barScreenGeometry`（当前屏 `availableGeometry` 逻辑像素，弹窗越界修正的唯一可靠来源——taskbar 模式下 `Item.Screen` 失效）、`barWindow`（插件用 `Connections` 收听其 `barPressed()` 实现"点 bar 关闭弹窗"；信号先于 item 的 `onClicked` 到达，toggle 部件需记录按下瞬间的弹窗状态防重开，范式见 `plugins_py/usage_hub/UsageHub.qml`）。
- 插件弹窗优先使用 `import XuanHub` 提供的 `PluginPopup`：设置 `anchorItem`、`pluginContext`，bar 点击调用 `toggleFromBar()`；宿主统一处理当前屏边界、顶部/底部方向、动态高度稳定显现、跨插件互斥、Esc/失焦/bar 点击关闭。只有特殊 flags 或非互斥场景才自建 `Window` 并自行处理定位。**改插件 QML 后必须重启进程验证**：引擎对 file:// 组件有内存缓存，热重载不加载磁盘新文件。
- QML 加载失败默认静默：`PluginSlot.qml` 监听 `Loader.Error` 画红色占位符，真实错误在 `xuanhub.log` 的 qWarning 里。

### 线程模型（铁律）

- 后台采集用 `QObject` worker + `moveToThread` + `QTimer`（参照 `SysInfoWorker`）；**不用** `std::thread`/`QThreadPool`。
- Worker 只算数和 `emit` 信号，**不碰** ViewModel/QML；跨线程靠 `Qt::AutoConnection` 排队到 GUI 线程。
- `shutdown()` 必须先 `workerThread->quit(); wait();` 再销毁对象；`shutdownAll` 顺序：先各插件 `shutdown()`（删光 DLL 出身的 QObject），再 `unload()`。

### Python 子插件（pybridge）

- 目录契约：`<exeDir>/plugins_py/<name>/plugin.json + main.py + X.qml`；开发契约全文见 `plugins_py/README.md`。
- 第三方 Python 依赖声明放在 `plugin.json.python`：`version` 使用受限比较约束，`packages` 分离 distribution 名、import 根、版本和 native 标记。启用前由实际 Worker 解释器运行只读 `dependency_doctor.py`；缺包、版本、x64 或 `.pyd` ABI 不匹配时不执行 `main.py`，错误通过 host diagnostics 显示在设置页。`environment.mode=private` 可声明插件根内的离线 wheel 目录；只有用户在设置页确认后才异步创建配置目录 `python-envs/<id>`，安装强制 `--no-index --only-binary=:all:`，成功后目标插件才改用私有解释器。
- `main.py` 约定：`update() -> dict`（必需）、`init(config)`、`set_config(config)`、`invoke(name, args)`、`stop()`（后四项可选）。每个子插件拥有独立解释器进程；`update/invoke/set_config` 都经异步 IPC 串行执行。QML 的 `plugin.call()` 同步返回 queued/id，最终结果经 `callFinished`，进度经 `callProgress`；`cancelCall` 只取消排队项，运行中返回 `not_interruptible`。Python 可用 `CallError/report_progress` 返回稳定错误与进度。worker 在 `init` 响应中声明 `set_config` 能力；实现者原地热更新并保留模块状态，旧插件只重启自身。
- 高频插件可声明 `PUSH_VALUES = True`，用 `xuanhub_host.emit_values/emit_event` 从后台线程写入 bridge 队列；后台线程绝不直接写 socket。Worker 和 Qt 代理双层合并 values patch，以 sequence 防乱序；事件队列两端各限 128，满时丢弃最新。push-only 插件初始 `update()` 后停用周期轮询，交互和配置变更后仍兼容刷新一次；范式见 `plugins_py/heartrate/`。
- worker 只连随机端口的 `127.0.0.1`，先以一次性 256-bit token 认证，再收取 `init` 配置；API Key 等敏感 config 不放进 worker 命令行。`PyBridgeProtocol` 统一处理 4 字节长度前缀 JSON、4 MiB 单包和 32 层深度限制；V2 hello 绑定 protocol/pluginId/capabilities，缺少这些字段的旧 hello 按 V1 兼容。请求超时 15 秒。`config.intervalMs` 缺省 1000ms 并钳制到 100–60000ms，下一次刷新从上次完成后计时，不积压 `update`。崩溃/超时仅重启本插件 worker，UI 保留旧值；10 分钟内连续 5 次 Worker/IPC 故障后熔断，设置页可观察健康指标并手动清除熔断、只重启目标插件。
- 卸载时代理先断开 IPC，worker 在 EOF 的 `finally` 中尽力调用 `stop()`；宽限后由常驻 `barplugin` 的 `ProcessReaper` 终止并兜底 kill。**不可**在 pybridge DLL 留下异步 lambda 回调给已卸载对象，必须交给 `ProcessReaper`。
- 标准库定位顺序：exe 旁 Bundle（`python.exe` + `Lib/`）→ 用户 `PYTHONHOME` → cache 变量 `XUANHUB_PYTHON_HOME_FALLBACK` → 注册表 `PythonCore` → `py -3` → PATH。全部未命中时 `subPluginIds()` 返回空并经 EventBus `app.notify` 托盘提示。调试钩子 `XUANHUB_DISABLE_PYTHON=1` 强制停用；检测结果仍存 qApp 动态属性以跨 pybridge 热重载保持。

#### 无 Qt/MSVC 的 Python 插件开发路径（Agent 决策规则）

- 当任务范围仅为 `plugins_py/<name>/`，或开发者明确没有 Qt/C++ 环境时，**不得把配置 CMake、安装 Qt 或编译宿主当作前置条件**。优先使用 GitHub Release 的 Windows x64 Bundle 便携包作为已编译宿主。
- 把便携包解压到可写目录，复制其中的 `plugins_py/hello/` 为新插件模板；只修改 `plugin.json`、`main.py`、主 QML 及同目录辅助文件。`plugin.json.id` 使用全局唯一的 `py_` 前缀，`qml` 显式指向主界面文件。
- 启用插件优先走“托盘 → 设置...”，框架会自动维护 `pybridge` 总开关。手改便携目录根部 `config.json` 时，`plugins` 数组必须同时包含新插件条目和 `{ "id": "pybridge", ... }`。
- 开发验证顺序：解析 `plugin.json` → 对纯 Python 逻辑运行单测 → 扫描凭据/缓存残留 → 在便携宿主中重启验证。纯 Python 插件不以 Qt/C++ 全量构建作为验收门槛；但 QML 只能由真实宿主完成最终视觉和交互验收。
- 便携模式日志是 `<exeDir>/xuanhub.log`。改 `config.json` 可热重载；改 `main.py`、`plugin.json` 或 QML 后统一退出并重启，尤其 QML 存在进程内组件缓存。
- Bundle 模式优先使用 exe 同目录的 Python。纯 Python 依赖可随插件目录分发；原生扩展必须匹配 Bundle 的 Python 版本与 x64 架构并放入 `Lib/site-packages/`。若要直接复用本机 Python 的 `site-packages`，使用非 Bundle 宿主。
- 真实 API Key 不得进入 `plugin.json`、源码或示例配置。设置 schema 中的敏感字符串字段标记 `"secret": true`，由设置窗口写入 Windows DPAPI 密钥库。
- 对外分发只打包 `plugins_py/<name>/`，排除 `__pycache__`、`*.pyc`、日志、测试凭据、本机配置和 VCS 元数据；不要把整个便携宿主作为插件包发布。

### EventBus（插件间通信）

`xuanhub::EventBus`（宿主 main 栈上单例，经 `BarPluginContext` 下发）：`subscribe(topic, receiver, slot)` / `publish(topic, QVariantMap)`，slot 形如 `onPayload(QVariantMap)`；订阅者以 `QPointer` 持有防悬空；同 (receiver, slot) 重复订阅会被去重忽略（publish 不重复投递）。插件间不直接互调。新插件订阅应优先使用 `BarPluginContext::eventScope->subscribe(...)`；宿主在 `shutdown()` 前关闭作用域并在 DLL unload 前强制清除其全部订阅，即使插件遗漏销毁接收者也不会再投递到已卸载代码。`eventScope` 为空时才回退 V1 `EventBus::subscribe()`，且 V1 插件仍须在 `shutdown()` 中销毁所有订阅对象。现有 topic：`theme.changed`；`app.notify`（插件 → 托盘气泡，为此 `TrayIcon` 在 `loadFrom` **之前**创建——订阅必须先于发布）。`EventBus.h` 用 `Q_SIGNALS` 而非 `signals`（`QT_NO_KEYWORDS` 插件也要包含它）。

V2 的命名空间、异步投递、保留状态、请求响应、权限和背压规则以
`docs/plugin-sdk-v2.md` 的“EventBus V2”为准；不要从上述 V1 topic 示例推导新协议。

### 主题系统（多主题包 × 双模式）

- 主题包是纯 JSON 目录 `<exeDir>/themes/<id>/theme.json`，`ThemeRegistry` 启动扫描 + 校验（`bg/fg/accent` 必填，其余 fallback）。契约见 `themes/README.md`。
- QML 统一 `themeManager.colors.<field>` 取色，不本地维护色板。配置双字段：`bar.themeId`（缺省 `xuanhub-default`）+ `bar.theme`（dark/light，缺省 dark）。
- 托盘 "切换深色/浅色" 仅切模式（不写回文件，热重载时回滚）；主题包切换走设置窗口。热重载时重扫 `themes/` 再 `applyTheme`。`themes/` 全丢时有硬编码默认主题兜底。

### 配置（`config.json`）

解析顺序：**exeDir（便携/开发优先）→ `%APPDATA%/XuanHub`**（缺失时从 qrc 播种）。结构：

```json
{
  "bar": { "height": 40, "edge": "top", "monitor": "primary", "themeId": "xuanhub-default", "theme": "dark", "mode": "appbar", "taskbarOffsetX": 0 },
  "plugins": [
    { "id": "clock", "position": "right", "order": 1, "config": { "format": "HH:mm:ss" } }
  ]
}
```

`position` 取值 `left/center/right`（空默认 `right`）；`edge` 仅 `top/bottom`；`config` 段原样经 `ctx->pluginConfig` 传给插件。插件条目另有 `width`（像素，0=跟随内容自适应，>0 固定宽，上限 800）；设置窗口的宽度「手动」模式初始值取 `PluginSlot` 实时上报的自然内容宽度（`PluginListModel::reportContentWidth` → `SettingsViewModel::pluginContentWidth`）。

**工作模式**：`bar.mode` = `appbar`（默认）/ `taskbar`。taskbar 选型：`SetParent` 嵌入 `Shell_TrayWnd` 是业界验证方案（TrafficMonitor 等；Deskband 已被 Win11 移除、顶层透明叠加有 z-order 缺陷，均未采用）。`TaskbarEmbed` 把 BarWindow `SetParent` 到 `Shell_TrayWnd`（强制 `WS_CHILD`、保留 `WS_EX_NOACTIVATE`），贴合任务栏左侧（`bar.taskbarOffsetX` 微调），高度跟随任务栏、背景全透明、宽度收缩为内容宽；500ms 轮询矫正位置与 z-order。explorer 重启时经 `TaskbarCreated` 与 `taskbarVanished` 双路触发 `recreateForTaskbar()` 重新嵌入（`TaskbarCreated` 是广播、进程内每个顶层窗口都收到，`TaskbarMonitor` 去重后顺延到事件循环只重建一次，不在消息分发栈内同步 destroy/create）。切换方式：改 config 热重载 / 托盘勾选项 / 设置窗口。

**热重载**：保存 config.json 自动生效（`QFileSystemWatcher` + 300ms 防抖，重命名式保存后自动重挂；文件缺失期退而监视父目录，永不失挂）。`reload()` 顺序：`load()`（解析失败保留旧配置；**运行中文件丢失同样保留当前设置**——qrc 播种仅限首运行从未成功加载过时）→ theme 按文件覆盖 → 按插件 ID 计算差异。`position/order/width` 只增量更新模型，C++ `IBarPluginConfigurable` 与 Python `set_config` 支持原地应用配置，V1 插件只停止并重建目标实例；新增/删除也只处理目标 ID，失败不回滚或重启其他插件。旧第三方 host 无法独立销毁子插件时回退全量重载。操作 QML/DLL 的顺序始终是先从模型解除目标引用并处理 DeferredDelete，再 shutdown/unload。托盘 "重新加载配置" 走 `forceReload()` 无条件全量，作为 DLL 更新和灾难恢复入口。

**写回与设置窗口**：`ConfigService::save()` 用 QSaveFile 原子写，只改已知键、未知键不丢。设置窗口（托盘 "设置..." → `SettingsController` 懒建 + `SettingsWindow.qml`）只做一件事：校验后写 config.json，**生效全靠 watcher，不直接调 reload**。插件列表来自 config ∪ sidecar 元数据并集；`pybridge` 总开关保存时自动托管。**草稿过期保护**：打开窗口时快照配置指纹（bar 已知键 + plugins 数组原文，紧凑 JSON），apply 前重算比对，不一致（窗口打开期间被热重载/手动编辑改动）拒绝保存并提示重开窗口，防旧草稿静默覆盖外部改动；保存成功后更新指纹。插件条目草稿行内存原始 QJsonObject，导出以其为底只覆盖已知键（条目级手写未知键不丢）。开机自启逻辑在 `AutoStart.h`。设置窗口把 `themeManager.colors` 灌进根 `palette.*` 驱动全部自绘控件（`Ui*.qml` 只引用 palette）；**Popup/ComboBox 下拉是独立顶层窗口，palette 继承在窗口边界断裂**——弹出层内必须用主窗口控件的 `control.palette`（用 delegate 自身 `item.palette` 会回落系统 palette，深色模式下深文字配深背景，实测踩过）。

**插件设置 schema 化**：设置窗口插件页为 master-detail。插件 config 编辑方式两种并存，**自绘优先（settingsQml > settings > 无）**：`settings` 由 sidecar 声明分组→字段（bool/int/double/string/color/enum/json），框架生成表单（契约见 `plugins_py/README.md`）；`settingsQml` 是插件自带 QML（示例 `plugins/sysinfo/SysInfoSettings.qml`），根 Item 声明 `property var plugin`，注入 `SettingsPluginProxy` 草稿桥，经 `value(key)/setValue(key,v)` 读写草稿；`revision` 属性随 `setValue` 递增并触发 `valuesChanged`——`value()` 是函数调用无通知，预览类绑定在表达式里引用一次 `plugin.revision` 建立依赖即可实时刷新。草稿存于 `SettingsPluginModel` 行内（未知键保留），导出按 schema 校验，保存仍走 `apply() → save → watcher`。**时序契约**：切换选中时框架保证 `pluginProxy.pluginId` 先于右侧面板刷新指向新插件（`pluginList.onCurrentIdChanged` 显式定序，不用 Binding——求值顺序不保证，曾致 countdown 默认键写进 sysinfo 条目）；自绘页读草稿初始化放 `onPluginChanged`（注入晚于 `Component.onCompleted`），异步写默认值前校验 `plugin.pluginId`。

## 6. 代码风格与约定

- **注释与文档一律使用简体中文**；源文件首行是 `// 文件名 — 一句话职责` 头注释；关键设计取舍在代码附近注释"为什么"（含被拒绝的备选）。
- 文档引用项目文件时使用仓库相对路径，引用用户数据目录时使用 `%APPDATA%`、`%TEMP%` 等环境变量；不得写入开发者用户名、盘符或个人工作区绝对路径。
- 头文件 `#pragma once`；字符串字面量 `QStringLiteral`；接口/POD 放 `namespace xuanhub`（`lib/barplugin`），宿主应用层类在全局命名空间。
- Qt 惯用法优先：信号槽、`Q_PROPERTY`、`QPointer`、`QVariantMap`；日志用 `qDebug()/qWarning()`（全局 message handler 带 `[时间戳] [级别]` 前缀）。
- 新增插件的 CMake 模板（照抄 `plugins/clock/CMakeLists.txt`）：`add_library(<id> MODULE ...)` + `AUTOMOC ON`；`PREFIX "" OUTPUT_NAME <id>`；链接 `barplugin`（+ 需要的 Qt/Win32 库）；`RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/<id>"` + `POST_BUILD copy_if_different` 拷 DLL/QML/metadata.json（VS 生成器下 MODULE 的 .dll 不走 RUNTIME_OUTPUT_DIRECTORY，POST_BUILD 兜底必须保留）；顶层 `add_subdirectory(plugins/<id>)`。
- 插件 QML 以**裸文件**放 DLL 同目录（不进 qrc），改样式免重编 DLL；DLL 内只烧 `metadata.json`。
- MSVC 统一 `/utf-8 /Zc:__cplusplus /permissive- /W3`；CRT 固定 Release `/MD`、Debug `/MDd`。宿主编译定义：`WIN32_LEAN_AND_MEAN`、`NOMINMAX`、`UNICODE/_UNICODE`、`QT_DISABLE_DEPRECATED_BEFORE=0x060000`。
- Git 提交信息使用中文 + conventional 前缀（如 `refactor(plugins/netstat): ...`）。

## 7. 测试与验证

项目使用 CTest 统一编排 C++、QML Smoke、Python Worker、CLI、Manifest、插件包和内置插件
Validator 回归。Release 构建后先运行：

```bash
ctest --test-dir build/default -C Release --output-on-failure
```

查看当前测试清单使用 `ctest --test-dir build/default -C Release -N`，不要在文档中固定测试数量。
只改插件时还应使用统一开发者 CLI；准确参数以 `--help` 为准：

```bash
python scripts/xuanhub_plugin.py validate <插件目录>
python scripts/xuanhub_plugin.py test <Python 插件目录>
python scripts/xuanhub_plugin.py run <插件目录>
python scripts/xuanhub_plugin.py doctor <插件目录或运行时目录>
```

自动测试不能覆盖 Win32 shell、真实 DPI、窗口层级和视觉效果，以下项目仍需手动验收：

- 窗口与工作区：贴顶置顶；最大化其他窗口不遮挡；正常退出立即归还工作区（强杀依赖系统延迟清理，已知行为）。
- 插件加载：增删 `plugins/<id>/` 后重启验证出现/消失与日志 `[Plugin] loaded: <id>`；IID 不符的 DLL 应 qWarning 跳过不崩溃。
- 布局/热重载：改 config.json 的 position/order/height/theme、增删插件条目，保存后 ~300ms 内生效；写坏 JSON 应告警且保留旧配置。
- 采集与交互：sysinfo 大流量时刷新且 UI 不卡。
- 设置窗口：改主题/高度保存即时生效；勾选插件即时上下线；非法输入保存被拒并红字提示；自启勾选与注册表 Run 键、托盘菜单同步。
- 托盘：菜单各项逐一点验；重启 Explorer 后托盘自动重建。
- taskbar 模式：勾选后嵌入任务栏左侧、背景透出、宽度随内容收缩；切回 appbar 工作区 reserve 无残留；重启 explorer 自动重新嵌入；100%/150% DPI 各验一次。
- DPI：100%/150%/200% 切换不糊、appbar 矩形物理像素换算正确。
- Python 子插件：无 Qt/MSVC 路径按 §5 决策规则验证；改 `main.py` / `plugin.json` / QML 后重启即生效；`update()` 抛异常时 UI 保留旧值、日志有 traceback、宿主不崩；无 Python 环境的机器上 pybridge 自动停用 + 托盘提示。

调试入口：普通模式为 `%APPDATA%/XuanHub/xuanhub.log`，便携模式为 `<exeDir>/xuanhub.log`，另有 `OutputDebugString`（DebugView）。`--settings` 启动即开设置窗口；`--uitest` 读 `<exeDir>/uitest.txt` 回放 UI 验收脚本（`click:x,y` / `kp:文本` / `fc:可见文本`；事件必须经 QTest 注入——直接 postEvent 不走 Qt Quick 输入分发，实测无效）。

## 8. 部署

- **部署已自动化**：`XuanHub` 目标 POST_BUILD 在每次链接后执行 `windeployqt --release/--debug --qmldir app/qml <exe>`。`--qmldir app/qml` 不可少：不带它不扫描 QML import，会漏 QML 插件 DLL（设置窗口曾因缺 `qtquickcontrols2plugin.dll` 打开空白）。
- 已知无害告警：`VCINSTALLDIR is not set`——只是不复制 MSVC 运行时 DLL，绿色分发包需自行带上 `vcruntime140.dll` 等。
- 手动重新部署：`pixi run deploy`（`scripts/deploy_qt.ps1`，windeployqt 自动定位，不写死 Qt 路径）。
- 插件目录结构即安装结构：`plugins/<id>/<id>.dll + <Id>.qml + metadata.json`。
- **插件分发**：`python scripts/xuanhub_plugin.py package <插件目录> --output <文件.xuanplugin>`
  复用安装器的 Manifest、路径、凭据、哈希和签名安全门禁。格式见 `docs/xuanplugin-format.md`，
  更新源见 `docs/xuanplugin-update-index.md`；不得手工改扩展名或直接压缩插件目录冒充安装包。
- **便携分发打包**：`scripts/package_portable.ps1` 把只读构建输出复制到全新的 `dist/XuanHub-v<版本>-portable-x64/` staging，再加工并打 zip；绝不直接压缩运行过的 build dist。脚本收集 MSVC 运行时 DLL；`-PythonStrategy` 两策略——`None`（默认，不内置 Python）/ `Bundle`（拷 `python.exe`、`Lib/`（排除 site-packages）、`DLLs/`（`_socket`/`_ssl` 等编译扩展）和 `python3*.dll`，使隔离 worker 免安装运行）；统一排除 VCS 元数据、删除 `__pycache__`/`*.pyc`，只复制内置默认配置，并在压缩前 fail-closed 检查 `.git/.hg/.svn`、`secrets.json`、锁/日志/数据库/密钥文件、高置信凭据形状及本机用户路径；创建 `portable` 标记文件；裁剪 translations/、qmltooling/、非 Basic 样式（**`qml/QtQuick/Controls/impl/` 必须保留**——Basic 样式自身依赖它）；`-NoZip` 只准备通过同一安全检查的 staging。注意：该脚本含中文注释，**必须保存为 UTF-8 with BOM**（PowerShell 5.1 对无 BOM 脚本按 GBK 误读）。

## 9. 安全与注意事项（改代码前必读）

- **ABI 锁定**：插件 DLL 必须与宿主同 Qt 版本、同编译器、同 CRT，否则 `instance()` 返回 nullptr 或直接崩溃。所有目标必须经同一 toolchain 构建。
- `barplugin` 保持 SHARED；改回 STATIC 会引入跨 DLL 对象语义/ABI 问题。
- 卸载顺序铁律：先 `shutdown()` 删光插件对象，再 `unload()`（已清除 `PreventUnloadHint`）；host 插件（pybridge）在所有普通插件之后 shutdown。
- Python worker 与宿主必须同为 x64；用户安装的 Python 必须 ≥3.12，且含 `python.exe`、`Lib/os.py` 与 `python3XX.dll`。worker 的异常、崩溃、超时或强制终止不得影响宿主进程。
- 崩溃残留 AppBar：`WorkAreaGuard` 负责启动清旧注册/退出 `ABM_REMOVE`。工作区只用 `SHAppBarMessage(ABM_NEW)`（系统托管生命周期），**不用** `SPI_SETWORKAREA`（易被覆盖、残留难清）。
- DPI：`main.cpp` 首行必须保持 `PassThrough` 舍入策略；AppBar 矩形用物理像素、QML 内用逻辑像素，换算见 `BarWindow::setupBar`。
- 注册表自启项（Run 键）与 `%APPDATA%/XuanHub` 配置文件——改动注意权限与残留清理。
- 日志文件每次启动 Truncate；单实例锁必须先于日志初始化（顺序见 main.cpp 注释，勿颠倒）。

## 10. 关键文件索引

- 文档总入口：`docs/README.md`；框架路线图：`docs/plugin-framework-roadmap.md`
- 插件接口：`lib/barplugin/include/barplugin/IBarPlugin2.h`（V2）与 `IBarPlugin.h`（V1 兼容）；host 契约 `IBarPluginHost.h`
- V2 上下文与服务：`IBarPlugin2.h` 中的 `PluginContext2`、`HostServices.h`、`PluginEventScope.h`；公开契约 `docs/plugin-sdk-v2.md`
- 插件扫描/生命周期：`app/src/PluginManager.cpp`；QML 注入契约：`app/qml/PluginSlot.qml`
- 三栏布局：`app/qml/Bar.qml`；模型：`app/src/PluginListModel.h` + `PluginFilterModel`
- 配置解析：`app/src/ConfigService.cpp`；热重载：`app/src/ConfigHotReloader.{h,cpp}`
- 设置窗口：`app/src/SettingsController/SettingsViewModel/SettingsPluginModel/SettingsPluginSortModel/SettingsPluginProxy.{h,cpp}` + `app/qml/SettingsWindow.qml`；自绘设置页示例 `plugins/sysinfo/SysInfoSettings.qml`
- 工作区/AppBar：`app/src/AppBarWin32.cpp`、`WorkAreaGuard.{h,cpp}`；任务栏嵌入：`TaskbarEmbed.{h,cpp}`
- 线程范式样例：`plugins/sysinfo/SysInfoWorker.{h,cpp}`；新插件 CMake 模板：`plugins/clock/CMakeLists.txt`
- Python 桥接：`plugins/pybridge/PyBridgePlugin.{h,cpp}`（发现/运行时）、`PyWorkerViewModel.{h,cpp}`（异步 IPC 代理）、`plugins_py/bridge_worker.py`（隔离执行器）、`lib/barplugin/ProcessReaper.{h,cpp}`（跨 DLL 进程回收）
- Python 子插件契约：`plugins_py/README.md`；示例 `hello/`（参考样例）、`intime/`（定时提醒）、`usage_hub/`（用量看板）
- 开发者 CLI：`scripts/xuanhub_plugin.py`；测试入口：`tests/` 与顶层 `CMakeLists.txt`
