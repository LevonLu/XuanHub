# AGENTS.md — 玄枢 / XuanHub

> 面向 AI 编码代理的项目说明。阅读本文件即可了解项目全貌（含设计决策与验收清单）。

## 1. 项目概览

**玄枢（XuanHub）** 是一个基于 **Qt6 / QML 的 Windows 插件化状态栏框架**（对标 polybar / waybar / Rainmeter），当前版本 0.1.0。

- `XuanHub.exe`：无边框、置顶、`Qt::Tool` 的 `QQuickView`，两种形态（`bar.mode`）：`appbar`（默认，Win32 AppBar 占住主屏顶部/底部工作区）；`taskbar`（`SetParent` 嵌入原生任务栏左侧，透明、不占工作区）。
- 插件是独立 DLL（`QPluginLoader` 加载），自带 QML 界面，框架只负责发现、加载、布局。内置示例插件：`clock`、`sysinfo`。
- 仅支持 Windows，直接用 Win32 API，不做跨平台抽象。
- `pybridge` 桥接插件为 `plugins_py/` 下每个 Python 子插件启动独立的 `python.exe` worker，并以受随机令牌认证的 loopback IPC 连接 QML 代理；Python 崩溃、GIL 卡死或扩展模块异常不会进入宿主。分发默认依赖用户自装 CPython ≥3.12，打包脚本可选 Bundle 策略带上解释器与标准库（见 §5、§8）。

## 2. 技术栈

| 维度 | 选型 |
|---|---|
| 语言 / 标准 | C++17（无编译器扩展） |
| UI | Qt 6.9.3 — Qt Quick / QML（另有 Widgets、Network） |
| 编译器 | **MSVC 2022（VS 17，x64）**；本机虽有 MinGW 但不采用 |
| 构建 | CMake ≥ 3.21 + `CMakePresets.json`，VS 17 2022 生成器 |
| 插件加载 | `QPluginLoader` + `Q_PLUGIN_METADATA`（IID `xuanhub.BarPlugin/1.0`；host 插件另有 `xuanhub.BarPluginHost/1.0`） |
| Python 桥接 | Qt `QTcpServer/QTcpSocket` + 每子插件独立 `python.exe` worker；4 字节长度前缀 JSON、一次性随机令牌认证，仅绑定 `127.0.0.1` |
| 系统 API | Win32：`SHAppBarMessage`、`GetIfTable2`、注册表 Run 键、`RegisterWindowMessage("TaskbarCreated")` |
| 配置 | JSON（`QJsonDocument`），热重载（`QFileSystemWatcher`） |

## 3. 构建与运行

前提：Qt 6.9.3 msvc2022_64 已安装；克隆后先 `git submodule update --init --recursive`。预设默认 `CMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64`，路径不同就在 `CMakeUserPresets.json`（已 gitignore）覆盖。

pybridge 不要求构建机安装 Python 开发包或 nanobind；它始终参与构建。运行时若找不到 Python ≥3.12 的 `python.exe`、标准库和 `python3XX.dll`，pybridge 自动停用子插件并托盘提示，其余功能照常运行。

```bash
cmake --preset default                 # 配置（生成到 build/default）
cmake --build --preset default         # Release
cmake --build --preset default-debug   # Debug
```

常用命令封装为 pixi 任务（见 `pixi.toml`）：`pixi run build`、`build-debug`、`run`、`run-settings`、`package` / `package-bundle` / `package-nozip`、`setup-config`、`deploy`、`log`、`clean`。

- 运行时输出统一在 **`build/default/dist/<CONFIG>/`**：`XuanHub.exe`、`barplugin.dll`、`plugins/<id>/<id>.dll + <Id>.qml + metadata.json`（根目录由顶层 `XUANHUB_BIN_DIR` 定义）。pybridge 额外同步 `plugins_py/` 整树（含 `bridge_worker.py`），不再产出或加载 `pybackend.dll`。
- **注意**：根 `config/config.json` 是开发示例，CMake 不自动拷贝，需手动复制到 dist（或 `pixi run setup-config`）；没有时退用 `%APPDATA%/XuanHub/config.json`（首运行从 qrc 播种）。
- 运行：直接执行 `build/default/dist/Release/XuanHub.exe`。日志写 `%APPDATA%/XuanHub/xuanhub.log`（每次启动 Truncate）+ `OutputDebugString`。单实例经 `%TEMP%/XuanHub.lock`（`QLockFile`）。

## 4. 目录结构

```
XuanHub/
├── CMakeLists.txt / CMakePresets.json   # 顶层工程与预设
├── config/config.json                   # 开发用示例配置（手动拷贝到 dist）
├── lib/barplugin/                       # 插件接口共享库 barplugin.dll（SHARED）
│   └── include/barplugin/               # IBarPlugin / IBarPluginHost / BarPluginContext
│                                        # / PluginMetadata / EventBus
├── app/                                 # XuanHub.exe 宿主
│   ├── src/                             # main.cpp、BarWindow、AppBarWin32、WorkAreaGuard、TaskbarEmbed、
│   │                                    # ConfigService、ConfigHotReloader、Settings*（设置窗口）、
│   │                                    # PluginManager、PluginListModel/FilterModel、ThemeManager、
│   │                                    # ThemeRegistry、TrayIcon、AutoStart.h
│   ├── qml/                             # main.qml / Bar.qml（三栏）/ PluginSlot.qml（Loader 槽）
│   │                                    # / SettingsWindow.qml / Ui*.qml（设计令牌 + 自绘控件库）
│   └── resources/default_config.json    # qrc 内置默认配置（首运行播种用）
├── plugins/
│   ├── clock/  sysinfo/                 # 两个示例插件（各含 Plugin + ViewModel + QML）
│   └── pybridge/                        # PyBridgePlugin 外壳 + PyWorkerViewModel（无 CPython 链接）
├── plugins_py/                          # Python 子插件 + bridge_worker.py（README 即开发契约）
├── themes/                              # 主题包（ThemeRegistry 扫描；README 即开发契约；xuanhub-default 兜底勿删）
└── third_party/nanobind/                # 历史子模块；当前 pybridge 不再依赖
```

依赖方向：**插件只链接 `barplugin`**，宿主 `app` 也链接 `barplugin`。`barplugin` 必须是 **SHARED**（保证宿主与插件 DLL 共享同一份 `EventBus` 类型/单例语义）。插件对宿主核心零依赖，核心仅通过接口拿到 `QObject* viewModel()` 与 `QUrl qmlUrl()`。

## 5. 运行时架构要点

```
QML 渲染层 (app/qml)     main.qml → Bar.qml（三栏）→ PluginSlot.qml（Loader）
插件适配层 (app/src)     PluginManager · PluginListModel · ConfigService · ThemeManager · TrayIcon
横切服务 (lib/barplugin) IBarPlugin · BarPluginContext · EventBus · PluginMetadata
Win32 平台层 (app/src)   BarWindow（QQuickView）· AppBarWin32 · WorkAreaGuard · TaskbarEmbed
                         ↑ 加载 ↑   plugins/<id>/<id>.dll + <Id>.qml（+ plugins_py/ 经 pybridge）
```

### 插件加载流程（`PluginManager::loadFrom`）

扫描 `<exeDir>/plugins/*/` 的 `*.dll` → 校验 IID → `instance()` + `qobject_cast<IBarPlugin*>` → 按 id 去重 → 与 config 比对（未启用则跳过并 unload）→ `init(ctx)` 注入 `BarPluginContext`（EventBus、configDir、本插件配置段）→ position 校验（非法回落 `right`）→ 收入 `PluginListModel`。

**host 插件分支**：`init` 后若命中 `IBarPluginHost`（如 pybridge），自身不进可见列表，走两阶段契约：`subPluginIds()` 枚举 → 核心按 config 过滤 → `createSubPlugin(id, cfg)` 实例化。子插件以 `iface=nullptr` 收入模型（生命周期归 host）；`shutdownAll()` 中 host **最后** shutdown（子插件 VM 依赖 host 持有的运行时），随后统一 `unload()`。

### QML 契约（写插件时必须遵守）

- `IBarPlugin` 只返回 `QObject* viewModel()` 与 `QUrl qmlUrl()`，**绝不返回 `QWidget*`/`QQuickItem*`**；QML 实例化由核心统一做。
- 每个插件 QML 的**根 `Item` 必须声明 `property var plugin`**；`PluginSlot.qml` 会把 viewModel 注入该属性。插件内用 `plugin ? plugin.x : fallback` 防御空值。
- 槽位（`PluginSlot`）`clip: true`：插件根 Item 声明的 width 即其占位宽度，**任何超出槽位边界的绘制都会被裁掉**（ListView 只裁整列不裁单个 delegate，槽位不裁剪时溢出内容会直接画到相邻插件上）。插件必须保证根 Item 宽度 ≥ 实际内容宽度；固定宽模式下被截断属预期行为，由插件自行处理对齐。
- 不用 `QML_ELEMENT` 静态注册——外部 DLL 无法在宿主编译期注册类型；`Loader + source URL` 是既定方案。
- 宿主 QML 根上下文的 context property（插件 QML 同引擎可访问）：`pluginModel`（含 left/center/right 过滤子模型）、`themeManager`（`themeId`/`mode`/`colors` 11 字段色板，用 `themeManager.colors.bg` 取色）、`barConfig`、`barScreenGeometry`（当前屏 `availableGeometry` 逻辑像素，弹窗越界修正的唯一可靠来源——taskbar 模式下 `Item.Screen` 失效）、`barWindow`（插件用 `Connections` 收听其 `barPressed()` 实现"点 bar 关闭弹窗"；信号先于 item 的 `onClicked` 到达，toggle 部件需记录按下瞬间的弹窗状态防重开，范式见 `plugins_py/usage_hub/UsageHub.qml`）。
- 插件弹窗（独立 `Window`）定位：锚点一律 `root.mapToGlobal`，边界用 `barScreenGeometry`（rect 只有 x/y/width/height，right/bottom 自己算）；高度异步收敛的弹窗用「隐身创建 + show 后 clamp + 底边锚定 + 稳定后显现」，完整实现见 UsageHub.qml 的 `popupWin`。**改插件 QML 后必须重启进程验证**：引擎对 file:// 组件有内存缓存，热重载不加载磁盘新文件。
- QML 加载失败默认静默：`PluginSlot.qml` 监听 `Loader.Error` 画红色占位符，真实错误在 `xuanhub.log` 的 qWarning 里。

### 线程模型（铁律）

- 后台采集用 `QObject` worker + `moveToThread` + `QTimer`（参照 `SysInfoWorker`）；**不用** `std::thread`/`QThreadPool`。
- Worker 只算数和 `emit` 信号，**不碰** ViewModel/QML；跨线程靠 `Qt::AutoConnection` 排队到 GUI 线程。
- `shutdown()` 必须先 `workerThread->quit(); wait();` 再销毁对象；`shutdownAll` 顺序：先各插件 `shutdown()`（删光 DLL 出身的 QObject），再 `unload()`。

### Python 子插件（pybridge）

- 目录契约：`<exeDir>/plugins_py/<name>/plugin.json + main.py + X.qml`；开发契约全文见 `plugins_py/README.md`。
- `main.py` 约定不变：`update() -> dict`（必需）、`init(config)`、`invoke(name, args)`、`stop()`（可选）。每个子插件拥有独立解释器进程；`update/invoke` 都经异步 IPC 串行执行，QML 的 `plugin.call()` 只能表示请求是否入队，不能依赖 Python 返回值。
- worker 只连随机端口的 `127.0.0.1`，先以一次性 256-bit token 认证，再收取 `init` 配置；API Key 等敏感 config 不放进 worker 命令行。协议用 4 字节长度前缀 JSON，单包上限 4 MiB、请求超时 15 秒。崩溃/超时仅重启本插件 worker，UI 保留旧值。
- 卸载时代理先断开 IPC，worker 在 EOF 的 `finally` 中尽力调用 `stop()`；宽限后由常驻 `barplugin` 的 `ProcessReaper` 终止并兜底 kill。**不可**在 pybridge DLL 留下异步 lambda 回调给已卸载对象，必须交给 `ProcessReaper`。
- 标准库定位顺序：exe 旁 Bundle（`python.exe` + `Lib/`）→ 用户 `PYTHONHOME` → cache 变量 `XUANHUB_PYTHON_HOME_FALLBACK` → 注册表 `PythonCore` → `py -3` → PATH。全部未命中时 `subPluginIds()` 返回空并经 EventBus `app.notify` 托盘提示。调试钩子 `XUANHUB_DISABLE_PYTHON=1` 强制停用；检测结果仍存 qApp 动态属性以跨 pybridge 热重载保持。

### EventBus（插件间通信）

`xuanhub::EventBus`（宿主 main 栈上单例，经 `BarPluginContext` 下发）：`subscribe(topic, receiver, slot)` / `publish(topic, QVariantMap)`，slot 形如 `onPayload(QVariantMap)`；订阅者以 `QPointer` 持有防悬空；同 (receiver, slot) 重复订阅会被去重忽略（publish 不重复投递）。插件间不直接互调。**插件 `shutdown()` 必须删光自己订阅过 EventBus 的对象**——泄漏的订阅对象在 DLL unload 后 QPointer 仍非空，下次 publish 会对已卸载代码 invoke → 宿主崩溃。现有 topic：`theme.changed`；`app.notify`（插件 → 托盘气泡，为此 `TrayIcon` 在 `loadFrom` **之前**创建——订阅必须先于发布）。`EventBus.h` 用 `Q_SIGNALS` 而非 `signals`（`QT_NO_KEYWORDS` 插件也要包含它）。

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

**热重载**：保存 config.json 自动生效（`QFileSystemWatcher` + 300ms 防抖，重命名式保存后自动重挂；文件缺失期退而监视父目录，永不失挂）。`reload()` 顺序：`load()`（解析失败保留旧配置；**运行中文件丢失同样保留当前设置**——qrc 播种仅限首运行从未成功加载过时）→ theme 按文件覆盖 → `setupBar()` 重协商 → plugins 段指纹比对：**未变则跳过插件重载**（改主题/高度不再摧毁插件状态），变了才 `shutdownAll() → loadFrom() → setPlugins()` 全量重载（同一槽函数内完成，QML 不会碰到悬空 viewModel）。托盘 "重新加载配置" 走 `forceReload()` 无条件全量（插件 DLL 更新后用它）。

**写回与设置窗口**：`ConfigService::save()` 用 QSaveFile 原子写，只改已知键、未知键不丢。设置窗口（托盘 "设置..." → `SettingsController` 懒建 + `SettingsWindow.qml`）只做一件事：校验后写 config.json，**生效全靠 watcher，不直接调 reload**。插件列表来自 config ∪ sidecar 元数据并集；`pybridge` 总开关保存时自动托管。**草稿过期保护**：打开窗口时快照配置指纹（bar 已知键 + plugins 数组原文，紧凑 JSON），apply 前重算比对，不一致（窗口打开期间被热重载/手动编辑改动）拒绝保存并提示重开窗口，防旧草稿静默覆盖外部改动；保存成功后更新指纹。插件条目草稿行内存原始 QJsonObject，导出以其为底只覆盖已知键（条目级手写未知键不丢）。开机自启逻辑在 `AutoStart.h`。设置窗口把 `themeManager.colors` 灌进根 `palette.*` 驱动全部自绘控件（`Ui*.qml` 只引用 palette）；**Popup/ComboBox 下拉是独立顶层窗口，palette 继承在窗口边界断裂**——弹出层内必须用主窗口控件的 `control.palette`（用 delegate 自身 `item.palette` 会回落系统 palette，深色模式下深文字配深背景，实测踩过）。

**插件设置 schema 化**：设置窗口插件页为 master-detail。插件 config 编辑方式两种并存，**自绘优先（settingsQml > settings > 无）**：`settings` 由 sidecar 声明分组→字段（bool/int/double/string/color/enum/json），框架生成表单（契约见 `plugins_py/README.md`）；`settingsQml` 是插件自带 QML（示例 `plugins/sysinfo/SysInfoSettings.qml`），根 Item 声明 `property var plugin`，注入 `SettingsPluginProxy` 草稿桥，经 `value(key)/setValue(key,v)` 读写草稿；`revision` 属性随 `setValue` 递增并触发 `valuesChanged`——`value()` 是函数调用无通知，预览类绑定在表达式里引用一次 `plugin.revision` 建立依赖即可实时刷新。草稿存于 `SettingsPluginModel` 行内（未知键保留），导出按 schema 校验，保存仍走 `apply() → save → watcher`。**时序契约**：切换选中时框架保证 `pluginProxy.pluginId` 先于右侧面板刷新指向新插件（`pluginList.onCurrentIdChanged` 显式定序，不用 Binding——求值顺序不保证，曾致 countdown 默认键写进 sysinfo 条目）；自绘页读草稿初始化放 `onPluginChanged`（注入晚于 `Component.onCompleted`），异步写默认值前校验 `plugin.pluginId`。

## 6. 代码风格与约定

- **注释与文档一律使用简体中文**；源文件首行是 `// 文件名 — 一句话职责` 头注释；关键设计取舍在代码附近注释"为什么"（含被拒绝的备选）。
- 头文件 `#pragma once`；字符串字面量 `QStringLiteral`；接口/POD 放 `namespace xuanhub`（`lib/barplugin`），宿主应用层类在全局命名空间。
- Qt 惯用法优先：信号槽、`Q_PROPERTY`、`QPointer`、`QVariantMap`；日志用 `qDebug()/qWarning()`（全局 message handler 带 `[时间戳] [级别]` 前缀）。
- 新增插件的 CMake 模板（照抄 `plugins/clock/CMakeLists.txt`）：`add_library(<id> MODULE ...)` + `AUTOMOC ON`；`PREFIX "" OUTPUT_NAME <id>`；链接 `barplugin`（+ 需要的 Qt/Win32 库）；`RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/<id>"` + `POST_BUILD copy_if_different` 拷 DLL/QML/metadata.json（VS 生成器下 MODULE 的 .dll 不走 RUNTIME_OUTPUT_DIRECTORY，POST_BUILD 兜底必须保留）；顶层 `add_subdirectory(plugins/<id>)`。
- 插件 QML 以**裸文件**放 DLL 同目录（不进 qrc），改样式免重编 DLL；DLL 内只烧 `metadata.json`。
- MSVC 统一 `/utf-8 /Zc:__cplusplus /permissive- /W3`；CRT 固定 Release `/MD`、Debug `/MDd`。宿主编译定义：`WIN32_LEAN_AND_MEAN`、`NOMINMAX`、`UNICODE/_UNICODE`、`QT_DISABLE_DEPRECATED_BEFORE=0x060000`。
- Git 提交信息使用中文 + conventional 前缀（如 `refactor(plugins/netstat): ...`）。

## 7. 测试与验证

**项目没有自动化测试框架。** 验证方式为手动验收：

- 窗口与工作区：贴顶置顶；最大化其他窗口不遮挡；正常退出立即归还工作区（强杀依赖系统延迟清理，已知行为）。
- 插件加载：增删 `plugins/<id>/` 后重启验证出现/消失与日志 `[Plugin] loaded: <id>`；IID 不符的 DLL 应 qWarning 跳过不崩溃。
- 布局/热重载：改 config.json 的 position/order/height/theme、增删插件条目，保存后 ~300ms 内生效；写坏 JSON 应告警且保留旧配置。
- 采集与交互：sysinfo 大流量时刷新且 UI 不卡。
- 设置窗口：改主题/高度保存即时生效；勾选插件即时上下线；非法输入保存被拒并红字提示；自启勾选与注册表 Run 键、托盘菜单同步。
- 托盘：菜单各项逐一点验；重启 Explorer 后托盘自动重建。
- taskbar 模式：勾选后嵌入任务栏左侧、背景透出、宽度随内容收缩；切回 appbar 工作区 reserve 无残留；重启 explorer 自动重新嵌入；100%/150% DPI 各验一次。
- DPI：100%/150%/200% 切换不糊、appbar 矩形物理像素换算正确。
- Python 子插件：改 `main.py` 重启即生效；`update()` 抛异常时 UI 保留旧值、日志有 traceback、宿主不崩；无 Python 环境的机器上 pybridge 自动停用 + 托盘提示。

调试入口：`%APPDATA%/XuanHub/xuanhub.log` 与 `OutputDebugString`（DebugView）。`--settings` 启动即开设置窗口；`--uitest` 读 `<exeDir>/uitest.txt` 回放 UI 验收脚本（`click:x,y` / `kp:文本` / `fc:可见文本`；事件必须经 QTest 注入——直接 postEvent 不走 Qt Quick 输入分发，实测无效）。

## 8. 部署

- **部署已自动化**：`XuanHub` 目标 POST_BUILD 在每次链接后执行 `windeployqt --release/--debug --qmldir app/qml <exe>`。`--qmldir app/qml` 不可少：不带它不扫描 QML import，会漏 QML 插件 DLL（设置窗口曾因缺 `qtquickcontrols2plugin.dll` 打开空白）。
- 已知无害告警：`VCINSTALLDIR is not set`——只是不复制 MSVC 运行时 DLL，绿色分发包需自行带上 `vcruntime140.dll` 等。
- 手动重新部署：`pixi run deploy`（`scripts/deploy_qt.ps1`，windeployqt 自动定位，不写死 Qt 路径）。
- 插件目录结构即安装结构：`plugins/<id>/<id>.dll + <Id>.qml + metadata.json`。
- **便携分发打包**：`scripts/package_portable.ps1` 把 dist 加工为绿色分发目录并打 zip：收集 MSVC 运行时 DLL；`-PythonStrategy` 两策略——`None`（默认，不内置 Python）/ `Bundle`（拷 `python.exe`、`Lib/`（排除 site-packages）、`DLLs/`（`_socket`/`_ssl` 等编译扩展）和 `python3*.dll`，使隔离 worker 免安装运行）；创建 `portable` 标记文件（配置与日志改放 exe 旁）；裁剪 translations/、qmltooling/、非 Basic 样式（**`qml/QtQuick/Controls/impl/` 必须保留**——Basic 样式自身依赖它）；`-NoZip` 只准备目录。注意：该脚本含中文注释，**必须保存为 UTF-8 with BOM**（PowerShell 5.1 对无 BOM 脚本按 GBK 误读）。

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

- 插件接口：`lib/barplugin/include/barplugin/IBarPlugin.h`（含 IID）；host 契约 `IBarPluginHost.h`
- 插件扫描/生命周期：`app/src/PluginManager.cpp`；QML 注入契约：`app/qml/PluginSlot.qml`
- 三栏布局：`app/qml/Bar.qml`；模型：`app/src/PluginListModel.h` + `PluginFilterModel`
- 配置解析：`app/src/ConfigService.cpp`；热重载：`app/src/ConfigHotReloader.{h,cpp}`
- 设置窗口：`app/src/SettingsController/SettingsViewModel/SettingsPluginModel/SettingsPluginSortModel/SettingsPluginProxy.{h,cpp}` + `app/qml/SettingsWindow.qml`；自绘设置页示例 `plugins/sysinfo/SysInfoSettings.qml`
- 工作区/AppBar：`app/src/AppBarWin32.cpp`、`WorkAreaGuard.{h,cpp}`；任务栏嵌入：`TaskbarEmbed.{h,cpp}`
- 线程范式样例：`plugins/sysinfo/SysInfoWorker.{h,cpp}`；新插件 CMake 模板：`plugins/clock/CMakeLists.txt`
- Python 桥接：`plugins/pybridge/PyBridgePlugin.{h,cpp}`（发现/运行时）、`PyWorkerViewModel.{h,cpp}`（异步 IPC 代理）、`plugins_py/bridge_worker.py`（隔离执行器）、`lib/barplugin/ProcessReaper.{h,cpp}`（跨 DLL 进程回收）
- Python 子插件契约：`plugins_py/README.md`；示例 `hello/`（参考样例）、`intime/`（定时提醒）、`usage_hub/`（用量看板）
