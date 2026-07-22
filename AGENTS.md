# AGENTS.md — 玄枢 / XuanHub

> 面向 AI 编码代理的项目说明。阅读本文件即可了解项目全貌（含设计决策与验收清单）。

## 1. 项目概览

**玄枢（XuanHub）** 是一个基于 **Qt6 / QML 的 Windows 插件化状态栏框架**（对标 polybar / waybar / Rainmeter），当前版本 0.1.0。

- 可执行文件 `XuanHub.exe`：无边框、置顶、`Qt::Tool` 的 `QQuickView`，两种工作形态（`bar.mode`，M11）——`appbar`（默认）：通过 Win32 AppBar（`SHAppBarMessage(ABM_NEW)`）在主屏顶部/底部占住桌面工作区；`taskbar`：`SetParent` 嵌入原生任务栏左侧，背景透明、不占工作区，与任务栏融为一体。
- 插件系统：插件是独立 DLL（`QPluginLoader` 加载），自带 QML 界面文件，框架只负责发现、加载、布局。
- 内置三个示例插件：`clock`（时钟，验证刷新链路）、`netstat`（网络速率，验证后台线程采集）、`launcher`（快捷启动器，验证交互与列表模型）。
- 仅支持 Windows，直接使用 Win32 API，不做跨平台抽象。
- Python Bridge（M6/M12）：`pybridge` 桥接插件内嵌 CPython（nanobind v2.14.0，**稳定 ABI** `Py_LIMITED_API=0x030C0000`，一份 DLL 通吃 **Python 3.12+**），承载 `plugins_py/` 下的 Python 子插件；分发默认**不**内置标准库——Python 子插件依赖用户自装 CPython ≥3.12（注册表全版本枚举 / py launcher / PATH 定位），未安装时 pybridge 自动停用并经托盘气泡提示（见 §5）；打包脚本可选 Bundle 策略内置标准库做成免安装包（见 §8）。

## 2. 技术栈

| 维度 | 选型 |
|---|---|
| 语言 / 标准 | C++17（`CMAKE_CXX_STANDARD_REQUIRED ON`，无编译器扩展） |
| UI | Qt 6.9.3 — Qt Quick / QML（另有 Widgets、Network 组件） |
| 编译器 | **MSVC 2022（VS 17，x64）**；本机虽有 MinGW 但不采用 |
| 构建 | CMake ≥ 3.21 + `CMakePresets.json`，VS 17 2022 生成器 |
| 插件加载 | `QPluginLoader` + `Q_PLUGIN_METADATA`（IID `xuanhub.BarPlugin/1.0`）；host 插件另有 `xuanhub.BarPluginHost/1.0` |
| Python 桥接 | nanobind v2.14.0（`third_party/nanobind` git submodule）+ 内嵌 CPython（稳定 ABI `Py_LIMITED_API=0x030C0000`，链接 `python3.lib`，运行时兼容 ≥3.12；解释器初始化走裸 `Py_Initialize()`——nanobind 官方不支持嵌入，只用作对象包装/GIL/转换层） |
| 系统 API | Win32：`SHAppBarMessage`（工作区）、`GetIfTable2`（网络）、注册表 Run 键（自启）、`RegisterWindowMessage("TaskbarCreated")`（托盘重建） |
| 配置 | JSON（`QJsonDocument`）；M7 起热重载（`QFileSystemWatcher` + 托盘 "重新加载配置" 手动触发） |

## 3. 构建与运行命令

前提：Qt 6.9.3 msvc2022_64 已安装；克隆后先 `git submodule update --init --recursive`（拉取 nanobind 及其嵌套的 ext/robin_map）。预设默认 `CMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64`；安装路径不同就改 `CMakePresets.json` 或新建 `CMakeUserPresets.json`（已被 .gitignore 忽略）覆盖。

pybridge 是**可选组件**：nanobind 子模块未初始化、或本机无 Python ≥3.12 开发包（稳定 ABI 头文件 + `python3.lib`）时，configure 仅 `WARNING` 并跳过该插件，宿主与其余插件照常构建（Python 子插件不可用）。

```bash
cmake --preset default                 # 配置（生成到 build/default）
cmake --build --preset default         # Release
cmake --build --preset default-debug   # Debug
```

常用命令已封装为 pixi 任务（见根目录 `pixi.toml`，`pixi task list` 查看全部）：`pixi run build`（Release）、`build-debug`、`run`、`run-settings`（启动即开设置窗口）、`package` / `package-bundle`（内置 Python 标准库的免安装包）/ `package-nozip`、`setup-config`（拷贝开发配置到 dist）、`deploy`（手动 windeployqt）、`log`（看日志尾部）、`clean`。pixi 仅作任务入口，工具链仍依赖系统安装的 MSVC/Qt/CMake。

- 运行时输出统一在 **`build/default/dist/<CONFIG>/`**：`XuanHub.exe`、`barplugin.dll`、`plugins/<id>/<id>.dll + <Id>.qml + metadata.json` 同根产出（顶层 CMakeLists 的 `XUANHUB_BIN_DIR` 定义该根目录）。
- pybridge 额外产物：`plugins_py/` 整树（POST_BUILD 从仓库根同步）。**M12 起不再拷贝 `python3XX.dll` 到 exe 旁**：稳定 ABI 下解释器 DLL 由 init 按发现到的 Python 目录全路径预装。**双 DLL 结构**（nanobind 以数据符号导入 `python3.dll`，`/DELAYLOAD` 只支持函数导入，LNK1194 规避）：触碰 CPython/nanobind 的代码整体收进 exe 旁的 `pybackend.dll`（静态导入 `python3.dll`），`pybridge.dll` 外壳只经 `PyBackendApi.h` 函数接口调用、`/DELAYLOAD:pybackend.dll` 延迟到预装之后。nanobind 用 `third_party/nanobind` git submodule（2.14.0，即 v2.13.0 之后的 master 快照 6fa7215，含嵌套子模块 `ext/robin_map`；克隆后 `git submodule update --init --recursive`）。
- **注意**：仓库根 `config/config.json` 是开发用示例配置，CMake **不会**自动拷贝它；需手动复制到 `build/default/dist/<CONFIG>/config.json`（当前 dist 里的副本就是手动放入的）。没有该文件时程序会退而用 `%APPDATA%/XuanHub/config.json`（首次运行自动从 qrc 里的 `default_config.json` 播种）。
- 运行：直接执行 `build/default/dist/Release/XuanHub.exe`。日志写入 `%APPDATA%/XuanHub/xuanhub.log`（每次启动 Truncate），同时输出到 `OutputDebugString`。
- 单实例：经 `%TEMP%/XuanHub.lock`（`QLockFile`）保证，第二个实例直接退出。

## 4. 目录结构与模块划分

```
XuanHub/
├── CMakeLists.txt / CMakePresets.json   # 顶层工程与预设
├── config/config.json                   # 开发用示例配置（手动拷贝到 dist）
├── lib/barplugin/                       # 插件接口共享库 barplugin.dll（SHARED）
│   ├── include/barplugin/               # IBarPlugin.h / IBarPluginHost.h / BarPluginContext.h
│   │                                    # PluginMetadata.h / EventBus.h / barplugin_export.h
│   └── src/EventBus.cpp
├── app/                                 # XuanHub.exe 宿主
│   ├── src/                             # main.cpp（入口/日志/单实例）、BarWindow（QQuickView）、
│   │                                    # AppBarWin32、WorkAreaGuard（工作区 reserve/归还）、
│   │                                    # ConfigService、ConfigHotReloader（M7 热重载）、
│   │                                    # SettingsController/ViewModel/PluginModel/PluginSortModel/
│   │                                    # PluginProxy（M8/M10 设置）、
│   │                                    # AutoStart.h、PluginManager、PluginListModel、
│   │                                    # PluginFilterModel、ThemeManager（M9 多主题）、
│   │                                    # ThemeRegistry（M9 主题包扫描/校验）、TrayIcon
│   ├── qml/                             # main.qml / Bar.qml（三栏 ListView）/ PluginSlot.qml（Loader 槽）
│   │                                    # / SettingsWindow.qml（M8 设置窗口，Qt Quick Controls）
│   └── resources/default_config.json    # qrc 内置默认配置（首运行播种用）
├── plugins/
│   ├── clock/                           # ClockPlugin + ClockViewModel（QTimer 1s）+ Clock.qml
│   ├── netstat/                         # NetStatPlugin + NetStatWorker（QThread）+ ViewModel + NetStat.qml
│   ├── launcher/                        # LauncherPlugin + LauncherViewModel(QAbstractListModel) + Launcher.qml
│   └── pybridge/                        # PyBridgePlugin（外壳：环境探测/DLL 预装）+ pybackend.dll（PyProxyViewModel 内嵌 CPython，PyBackendApi.h 窄接口）
├── plugins_py/                          # Python 子插件（pybridge 扫描这里；README 即开发契约）
│   ├── hello/                           # 参考样例：plugin.json + main.py（update/init/invoke 三约定）+ Hello.qml
│   ├── intime/                          # 定时休息提醒：plugin.json + main.py + InTime.qml + InTimeSettings.qml
│   └── usage_hub/                       # 用量中心（多厂商 API 用量看板）：plugin.json + main.py + UsageHub.qml + UsageSettings.qml + vendors/
├── themes/                              # M9 主题包（ThemeRegistry 扫描这里；README 即开发契约）
│   ├── xuanhub-default/theme.json       # 内置默认主题（提炼自 Catppuccin Mocha，兜底勿删）
│   ├── tokyo-night/theme.json           # 示例第二主题
│   ├── gruvbox/theme.json               # Gruvbox
│   ├── nord/theme.json                  # Nord
│   └── rose-pine/theme.json             # Rosé Pine（示例第三主题）
└── third_party/nanobind/                # git submodule（2.14.0，含嵌套子模块 ext/robin_map）
```

依赖方向：**插件只链接 `barplugin`**；宿主 `app` 也链接 `barplugin`。`barplugin` 必须是 **SHARED** 库，保证宿主与所有插件 DLL 共享同一份 `EventBus` 类型/单例语义（STATIC 链接会有跨 DLL 的 ABI 隐患）。插件对宿主核心零依赖，核心仅通过接口拿到 `QObject* viewModel()` 与 `QUrl qmlUrl()`。

## 5. 运行时架构要点

分层视图（依赖自上而下，插件只依赖 `lib/barplugin`，对宿主核心零依赖）：

```
QML 渲染层 (app/qml)     main.qml → Bar.qml（三栏）→ PluginSlot.qml（Loader）
插件适配层 (app/src)     PluginManager · PluginListModel · ConfigService · ThemeManager · TrayIcon
横切服务 (lib/barplugin) IBarPlugin · BarPluginContext · EventBus · PluginMetadata
Win32 平台层 (app/src)   BarWindow（QQuickView）· AppBarWin32 · WorkAreaGuard · TaskbarEmbed
                         ↑ 加载 ↑   plugins/<id>/<id>.dll + <Id>.qml（+ plugins_py/ 经 pybridge）
```

### 插件加载流程（`PluginManager::loadFrom`）

扫描 `<exeDir>/plugins/*/` 下的 `*.dll` → `QPluginLoader::metaData()` 校验 IID 是否为 `xuanhub.BarPlugin/1.0` → `instance()` + `qobject_cast<IBarPlugin*>` → 按 `metadata().id` 去重 → 与 config.json 的 `plugins[*].id` 比对（未启用则跳过并 unload）→ `init(ctx)` 注入 `BarPluginContext`（EventBus 指针、configDir、本插件配置段）→ position 校验（非法值回落 `right`）→ 收入 `PluginListModel`。

**host 插件分支（M6）**：`init(ctx)` 成功后若 `qobject_cast<IBarPluginHost*>` 命中（如 pybridge），该插件自身**不进可见列表**，改走两阶段契约：`subPluginIds()` 枚举子插件 id → 核心按 config 过滤/去重 → `createSubPlugin(id, cfg)` 实例化（此刻才传入该子插件 config 段）。子插件以 `iface=nullptr` 收入模型（生命周期归 host）；`shutdownAll()` 中 host **最后** shutdown（其子插件 VM 依赖 host 持有的运行时），随后才统一 `unload()`。

### QML 契约（写插件时必须遵守）

- 插件接口 `IBarPlugin` 只返回 `QObject* viewModel()` 与相对 DLL 目录的 `QUrl qmlUrl()`，**绝不返回 `QWidget*`/`QQuickItem*`**；QML 实例化由核心统一做。
- 每个插件 QML 的**根 `Item` 必须声明 `property var plugin`**；`PluginSlot.qml` 的 `Loader.onLoaded` 会把 viewModel 注入该属性（`slot.item.plugin = model.viewModel`）。插件 QML 内用 `plugin ? plugin.someProperty : fallback` 防御空值。
- 不用 `QML_ELEMENT` 静态注册 —— 外部 DLL 无法在宿主编译期注册类型；`Loader + source URL` 是既定方案。
- 宿主 QML 根上下文提供的 context property（插件 QML 与宿主同引擎，均可访问）：`pluginModel`（含 `leftPlugins/centerPlugins/rightPlugins` 三个过滤子模型）、`themeManager`（M9 起暴露 `themeId`/`mode`/`colors`，**`colors` 是 11 字段色板 QVariantMap**，QML 用 `themeManager.colors.bg` 取色）、`barConfig`、`barScreenGeometry`（当前屏 `availableGeometry` 逻辑像素，`BarWindow::setupBar` 每次刷新；弹窗越界修正的唯一可靠屏幕几何来源——taskbar 模式下 bar 是任务栏子窗口，`Item.Screen` attached 失效为 undefined，弹窗开在屏幕外时其内部 Item 的 Screen 同样无效）、`barWindow`（BarWindow 本体，插件用 `Connections` 收听其 `barPressed()` 信号实现"点 bar 任意处关闭弹窗"——bar 是 `WindowDoesNotAcceptFocus`，点 bar 不会让弹窗失活，点桌面/其他窗口仍靠弹窗自身 `onActiveChanged` 关闭；信号先于 item 的 `onClicked` 到达，toggle 部件需记录按下瞬间的弹窗状态防重开，范式见 `plugins_py/usage_hub/UsageHub.qml`）。
- 插件弹窗（独立 `Window`）定位范式：锚点一律 `root.mapToGlobal`（真实屏幕坐标，taskbar 嵌入下也可靠），屏幕边界用 `barScreenGeometry`（QML rect 只有 x/y/width/height，right/bottom 要自己算）；宽高固定的弹窗（如 clock 日历）show 前一次算定，高度异步收敛的弹窗（如 usage_hub）用「隐身创建 + show 后 clamp + 底边锚定跟随高度 + 稳定后显现」，完整实现见 UsageHub.qml 的 `popupWin`。**改插件 QML 后必须重启进程验证**：QML 引擎对 file:// 组件有内存缓存，M7 热重载不会加载磁盘新文件。
- QML 加载失败默认静默：`PluginSlot.qml` 已监听 `Loader.Error` 并绘制红色占位符，真实错误在 `xuanhub.log` 的 QML 引擎 qWarning 里。

### 线程模型（铁律）

- 后台采集用 `QObject` 派生 worker + `moveToThread` + `QTimer`（参照 `NetStatWorker`）；**不用** `std::thread`/`QThreadPool`。
- Worker 只算数和 `emit` 信号，**不碰**任何 ViewModel/QML 对象；跨线程靠 `Qt::AutoConnection` 自动排队到 GUI 线程，由 ViewModel 的 slot 更新 `Q_PROPERTY`。
- 插件 `shutdown()` 必须先 `workerThread->quit(); wait();` 再销毁对象；`PluginManager::shutdownAll` 的顺序是：先各插件 `shutdown()`（删光 DLL 出身的 QObject），再 `QPluginLoader::unload()`。

### Python 子插件（pybridge，M6）

- 目录契约：`<exeDir>/plugins_py/<name>/plugin.json + main.py + X.qml`；开发契约全文见 `plugins_py/README.md`。
- `main.py` 模块级约定：`update() -> dict`（必需；`PyProxyViewModel` 的 QTimer 持 GIL 定时「拉」取，dict 填入 `QVariantMap values` 属性袋）、`init(config)`（可选，config 段透传）、`invoke(name, args)`（可选，`plugin.call()` 入口，成功后立即补拉一次 update）、`stop()`（可选，卸载/热重载前调用以停后台线程）。
- GIL 铁律：进出 Python 一律 `py::gil_scoped_acquire`；**初始化后主线程永久 `py::gil_scoped_release`（有意泄漏）**——否则主线程持有 GIL 却从不进入解释器求值循环，后台 python 线程会饿死（实测踩过）。update/invoke 在 GUI 线程执行，禁止阻塞；python 可起线程做后台 IO（标准库 IO/sleep 释放 GIL），但线程只碰纯 Python 对象、绝不触达 Qt/QML，且必须实现 `stop()` 防热重载泄漏。范式见 `plugins_py/README.md`。
- 解释器是进程级单例，**任何时刻不主动 finalize**（热重载靠静态守卫复用 + `sys.modules` pop 后重新 import；进程退出由 OS 回收）；Python 异常经 `py::error_already_set` 捕获后只记日志、UI 保留旧值。
- pybridge 目标定义了 `QT_NO_KEYWORDS`（Python 头 `PyType_Spec.slots` 字段与 Qt `slots` 宏冲突），其源码一律用 `Q_SIGNALS`/`Q_EMIT` 显式写法。
- 标准库定位（init 时检测一次，以 `Lib/os.py` 地标 + home 下 `python3<minor>.dll` 版本号 ≥12 双重校验）：exe 旁内置 `Lib/`（Bundle 打包，**压掉**用户 `PYTHONHOME`）→ 用户显式 `PYTHONHOME` → 可选烧入路径兜底（cache 变量 `XUANHUB_PYTHON_HOME_FALLBACK`，**默认空**——分发产物不内嵌构建机路径，本机开发在 CMakeUserPresets.json 里设置）→ 注册表 `PythonCore` 全版本枚举（HKCU→HKLM，≥3.12 取最高）→ `py -3` launcher → PATH。全部未命中则**不初始化解释器**（缺标准库时初始化会 fatal error 拖垮宿主）：pybridge 照常 init 成功但 `subPluginIds()` 返回空（Python 子插件整体停用），并经 EventBus `app.notify` 让托盘弹一次提示。调试钩子 `XUANHUB_DISABLE_PYTHON=1` 强制走停用路径。命中后 init 先全路径预装 `python3XX.dll` 与 `python3.dll`，再显式 `LoadLibrary` exe 旁的 `pybackend.dll` 并调 `xb_py_initialize()`（内部裸 `Py_Initialize()`——nanobind 无 `initialize_interpreter`，嵌入非其官方支持场景；预装/加载失败均按无环境降级）。

### EventBus（插件间通信）

`xuanhub::EventBus`（QObject，宿主 main 栈上单例，经 `BarPluginContext` 下发）：`subscribe(topic, receiver, slot)` / `unsubscribe` / `publish(topic, QVariantMap)`，slot 形如 `onPayload(QVariantMap)`，经 `QMetaObject::invokeMethod` 投递；订阅者以 `QPointer` 持有防悬空。插件间不直接互调。现有 topic：`theme.changed`（`ThemeManager::applyTheme` 发布，payload 含 `themeId` 与 `mode`）；`app.notify`（插件 → 宿主的气泡提示请求，payload 含 `title`/`message`，托盘订阅后 `showMessage`；为此 `TrayIcon` 在 `PluginManager::loadFrom` **之前**创建——订阅必须先于发布，pybridge 的「未检测到 Python」提示走的就是这条链路）。注意 `EventBus.h` 用 `Q_SIGNALS` 而非 `signals`：pybridge 等 `QT_NO_KEYWORDS` 插件也要包含它。

### 主题系统（M9：多主题包 × 双模式）

- 主题包是**纯 JSON 声明式**目录：`<exeDir>/themes/<id>/theme.json`，由 `ThemeRegistry` 启动时扫描 + 校验（`bg/fg/accent` 必填，其余 fallback 到内置默认）。开发契约见 `themes/README.md`。
- `ThemeManager` 暴露给 QML：`themeId`（当前主题包 id）、`mode`（dark/light）、`colors`（11 字段色板 QVariantMap，已 fallback 填满）。QML 统一 `themeManager.colors.bg` 取色，不再本地维护色板。
- 配置双字段：`bar.themeId`（主题包 id，缺省 `xuanhub-default`）+ `bar.theme`（模式 dark/light，缺省 dark）。
- 托盘 "切换深色/浅色" 仅切模式（`switchMode`，不写回文件，热重载时回滚）；主题包切换走设置窗口（写 config.json → 热重载生效）。
- 热重载时 `ConfigHotReloader` 会**重扫 `themes/`**（新装/卸载主题包立刻可见），再按文件值 `applyTheme(themeId, mode)`。
- 内置兜底：`ThemeRegistry` 持有一份硬编码默认主题，`themes/` 全丢或 themeId 找不到时仍可用。

### 配置（`config.json`）

解析顺序：**exeDir（便携/开发优先）→ `%APPDATA%/XuanHub`**（缺失时从 qrc 播种默认配置）。结构：

```json
{
  "bar": { "height": 40, "edge": "top", "monitor": "primary", "themeId": "xuanhub-default", "theme": "dark", "mode": "appbar", "taskbarOffsetX": 0 },
  "plugins": [
    { "id": "clock", "position": "right", "order": 1, "config": { "format": "HH:mm:ss" } }
  ]
}
```

`position` 取值 `left/center/right`（空默认 `right`，非法值告警并回落）；`edge` 仅 `top/bottom`；`config` 段原样经 `ctx->pluginConfig` 传给插件。

**工作模式（M11）**：`bar.mode` = `appbar`（默认，占工作区）/ `taskbar`（嵌入任务栏）。taskbar 选型：`SetParent` 嵌入 `Shell_TrayWnd` 是业界验证方案（TrafficMonitor 等）；Deskband 已被 Win11 移除、顶层透明叠加有 z-order 争夺缺陷，均未采用。taskbar 模式下 `TaskbarEmbed` 把 BarWindow `SetParent` 到 `Shell_TrayWnd`（`QWindow::fromWinId` 包裹 + 强制 `WS_CHILD`，保留 `WS_EX_NOACTIVATE`），贴合任务栏左侧（`bar.taskbarOffsetX` 微调，Win11 居中图标/小组件遮挡时用），高度跟随任务栏，背景强制全透明，宽度收缩为 QML 内容宽；500ms 轮询矫正位置与 z-order（任务栏重绘会压盖子窗口）。explorer 重启时子窗口随任务栏销毁：`TaskbarCreated`（main.cpp `TaskbarMonitor`）与 `TaskbarEmbed::taskbarVanished` 双路触发 `BarWindow::recreateForTaskbar()` 重建窗口重新嵌入。切换方式：改 config.json 热重载、托盘 "任务栏模式" 勾选项（只写回 `bar.mode` 一键，生效全靠 watcher），或设置窗口「外观 → 状态栏 → 工作模式」（随整页草稿经 apply 写回）。

**热重载（M7）**：保存 config.json 自动生效（`ConfigHotReloader` 的 `QFileSystemWatcher` 监听 `activePath()`，300ms 防抖，编辑器重命名式保存后自动重挂监视），或托盘 "重新加载配置" 手动触发。`reload()` 顺序：`load()`（解析失败保留旧配置）→ theme 按文件覆盖 → `setupBar()` 重协商 → `shutdownAll() → loadFrom() → setPlugins()` 插件全量重载（同一槽函数内完成，QML 不会碰到悬空 viewModel）。注意：托盘「切换深色/浅色」不写回文件，热重载时文件值会覆盖它。

**写回与设置窗口（M8）**：`ConfigService` 的 `m_raw` 留存原始 JSON，`save(QJsonObject)` 用 QSaveFile 原子写 `activePath()`——调用方只改已知键，未知键（如 `bar.monitor`）不丢。设置窗口（托盘 "设置..." → `SettingsController` 懒建的独立 QQuickView + `SettingsWindow.qml`，720×640）只做一件事：校验后写 config.json，**生效全靠 M7 watcher，不直接调 reload**。插件列表来自 config ∪ sidecar 元数据（`plugins/*/metadata.json`、`plugins_py/*/plugin.json`）的并集；`pybridge` 总开关在保存时自动托管（有 py_* 启用则自动补条目，全禁则移除）。开机自启逻辑在 `AutoStart.h`（托盘菜单与设置窗口共用）。

**插件设置 schema 化（M10）**：设置窗口插件页为 master-detail——左侧插件列表（`SettingsPluginSortModel`：启用在前 → order → id，勾选即时归位），右侧选中插件的设置（未启用只显示启用框；启用后才显示位置/排序与插件级设置）。插件 config 的编辑方式两种并存，sidecar 声明、**自绘优先（settingsQml > settings > 无）**：

- `settings`（schema 生成表单）：sidecar 声明分组→字段（类型 bool/int/double/string/color/enum/json），框架生成可折叠树状表单；字段与类型契约全文见 `plugins_py/README.md`「设置项声明」。
- `settingsQml`（自绘设置页）：插件自带 QML（首个落地 `plugins/launcher/LauncherSettings.qml`），根 Item 声明 `property var plugin`，注入 `SettingsPluginProxy` 草稿桥，经 `value(key)/setValue(key,v)` 读写 config 草稿（JS 数组/对象会在 C++ 侧深转后存储）；只碰草稿，保存/取消由设置窗口统一。

草稿存于 `SettingsPluginModel` 行内 QVariantMap（schema 未声明的未知键随之保留），导出按 schema 逐字段校验，保存仍走 `apply() → save → M7 watcher`。

## 6. 代码风格与约定

- **注释与文档一律使用简体中文**；每个源文件首行是 `// 文件名 — 一句话职责` 格式的头注释；关键设计取舍在代码附近写注释说明"为什么"（含被拒绝的备选）。
- C++ 头文件用 `#pragma once`；Qt 字符串字面量用 `QStringLiteral`；接口/POD 放在 `namespace xuanhub`（`lib/barplugin`），宿主应用层类（`BarWindow` 等）在全局命名空间。
- Qt 惯用法优先：信号槽、`Q_PROPERTY`、`QPointer` 防御、`QVariantMap` 传参；日志用 `qDebug()/qWarning()` 等（main.cpp 装了全局 message handler，带 `[时间戳] [级别]` 前缀）。
- 新增插件的 CMake 模板（照抄 `plugins/clock/CMakeLists.txt`）：
  - `add_library(<id> MODULE ...)` + `AUTOMOC ON`；
  - `set_target_properties(... PREFIX "" OUTPUT_NAME <id>)`（产出 `<id>.dll` 而非 `lib<id>.dll`）；
  - 链接 `barplugin`（+ 需要的 Qt 组件 / Win32 库，如 netstat 链接 `iphlpapi ws2_32`）；
  - `RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/<id>"`，并用 `POST_BUILD copy_if_different` 把 DLL、`<Id>.qml`、`metadata.json` 拷到该目录（VS 生成器下 MODULE 的 .dll 不走 RUNTIME_OUTPUT_DIRECTORY，POST_BUILD 是兜底，必须保留）；
  - 在顶层 `CMakeLists.txt` 加 `add_subdirectory(plugins/<id>)`。
- 插件 QML 以**裸文件**放 DLL 同目录（不进 qrc），改样式免重编 DLL；DLL 内只烧 `metadata.json`（`Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")`）。
- MSVC 下统一 `/utf-8 /Zc:__cplusplus /permissive- /W3`（顶层设置）；CRT 固定 `MultiThreaded DLL`（Release `/MD`、Debug `/MDd`），与 Qt 官方预编译库一致。
- 宿主编译定义：`WIN32_LEAN_AND_MEAN`、`NOMINMAX`、`UNICODE/_UNICODE`、`QT_DISABLE_DEPRECATED_BEFORE=0x060000`。
- Git 提交信息使用中文 + conventional 前缀（如 `refactor: ...`、`refactor(plugins/netstat): ...`）。

## 7. 测试与验证

**项目没有自动化测试框架，也没有任何测试目录。** 验证方式为手动验收：

- 窗口与工作区：运行 `XuanHub.exe` 看贴顶置顶；最大化其他窗口验证不遮挡；正常退出立即归还工作区（强杀依赖系统延迟清理，属已知行为）。
- 插件加载：增删 `plugins/<id>/` 目录后重启，验证插件出现/消失与日志 `[Plugin] loaded: <id> pos=...`；放入 IID 不符的 DLL 验证 qWarning 跳过不崩溃。
- 布局：改 `config.json` 的 position/order（保存即热重载）验证三栏布局。
- 采集与交互：netstat 在大流量下载时观察速率刷新且 UI 不卡；launcher 点击能拉起进程（`QProcess::startDetached`）。
- 热重载：运行中保存 `config.json`（position/order/height/theme、增删插件条目）观察 ~300ms 内生效；写坏 JSON 验证告警且保留旧配置；托盘 "重新加载配置" 手动触发。
- 设置窗口：托盘 "设置..." 打开；改主题/高度保存后即时生效；勾选/取消插件即时上下线；非法输入保存被拒并红字提示；通用页勾选自启后查注册表 Run 键、与托盘菜单勾选同步。
- 托盘：菜单各项（设置.../开机自启/任务栏模式/切换深色浅色/重新加载配置/退出）逐一点验；重启 Explorer 后托盘自动重建。
- taskbar 模式：托盘勾 "任务栏模式"（或设置窗口/手改 `bar.mode`）观察条栏嵌入任务栏左侧、背景透出任务栏、宽度随内容收缩；切回 appbar 验证工作区重新 reserve 无残留；重启 explorer 验证自动重新嵌入；100%/150% DPI 各验一次位置与高度。
- DPI：100%/150%/200% 切换验证不糊、appbar 矩形物理像素换算正确。
- Python 子插件：改 `plugins_py/<name>/main.py` 重启即生效（免重编 DLL）；`update()` 抛异常时 UI 保留旧值、日志有 traceback、宿主不崩；无 Python 环境的机器上 pybridge 自动停用 + 托盘提示，其余功能正常。

调试入口：`%APPDATA%/XuanHub/xuanhub.log` 与 `OutputDebugString`（可用 DebugView 查看）。`--settings` 启动即打开设置窗口（免点托盘）；`--uitest` 读 `<exeDir>/uitest.txt` 回放 UI 验收脚本（`click:x,y` / `kp:文本` / `fc:可见文本`（定位后点击），供无头自动化验收；事件必须经 QTest 注入——直接 postEvent 不走 Qt Quick 输入分发，实测无效）。

## 8. 部署

- `build/default/dist/Release/` 是一份已用 **windeployqt** 部署过的完整运行目录（含 Qt6*.dll、qml/、platforms/ 等）。**部署已自动化**：`XuanHub` 目标的 POST_BUILD 会在每次链接后执行
  `windeployqt --release/--debug（按 CONFIG）--qmldir app/qml <exe>`，
  新增 Qt 模块依赖或 QML import 后无需手动干预。`--qmldir app/qml` 不可少：不带它 windeployqt 不扫描 QML import，会漏 QML 插件 DLL（M8 设置窗口曾因缺 `qtquickcontrols2plugin.dll` 打开空白）。
- 已知无害告警：构建日志里 windeployqt 报 `VCINSTALLDIR is not set` —— 它只是不复制 MSVC 运行时 DLL，本机有 VS 运行时可解析；做绿色分发包时需自行带上 `vcruntime140.dll` 等。
- 手动重新部署（如在未重新链接的情况下 Qt 升级了）：`pixi run deploy`，即 `scripts/deploy_qt.ps1`（windeployqt 自动定位：PATH → CMakeCache 的 `Qt6_DIR` 反推，不写死 Qt 路径；等价命令为 `windeployqt --release --qmldir app/qml build/default/dist/Release/XuanHub.exe`）。
- 插件目录结构即安装结构：`plugins/<id>/<id>.dll + <Id>.qml + metadata.json`。
- **便携分发打包**：`scripts/package_portable.ps1`（PowerShell，在构建之后执行）把 dist 加工为绿色分发目录并打 zip：
  - 收集 MSVC 运行时 DLL（vswhere → 已知 VS 路径 → System32 兜底）；
  - Python 标准库两策略（`-PythonStrategy`）：`None`（**默认**，不内置——Python 子插件依赖用户自装 CPython 3.12+，未装则 pybridge 自动停用 + 托盘提示）；`Bundle`（注册表全版本枚举/py launcher/PATH 定位本机 Python ≥3.12，拷 `Lib/`（**排除 site-packages**——打包机第三方包动辄 GB 级）+ `DLLs/`（`_socket`/`_ssl` 等编译扩展，缺了 urllib/HTTPS 在无 Python 机器必挂）+ `python3.dll`/`python3XX.dll` 解释器 DLL，做成免安装包）；
  - 创建 `portable` 标记文件（便携模式：配置与日志改放 exe 旁而非 %APPDATA%）；
  - 裁剪（translations/、qmltooling/、非 Basic 的 Controls 样式；**`qml/QtQuick/Controls/impl/` 必须保留**——它是 Basic 样式自身依赖的共享实现模块，误删会导致 ScrollView/ToolTip 全部加载失败）；
  - `-NoZip` 只准备目录不打 zip。
  - 注意：该脚本含中文注释，**必须保存为 UTF-8 with BOM**——Windows PowerShell 5.1 对无 BOM 脚本按 ANSI(GBK) 误读，会直接解析失败。

## 9. 安全与注意事项（改代码前必读）

- **ABI 锁定**：插件 DLL 必须与宿主同 Qt 版本、同编译器、同 CRT（/MD 或 /MDd 对应），否则 `instance()` 返回 nullptr 或直接崩溃。所有目标必须经同一 toolchain 构建。
- `barplugin` 保持 SHARED；任何将其改回 STATIC 的"简化"都会引入跨 DLL 对象语义/ABI 问题。
- 卸载顺序铁律：先 `shutdown()` 删光插件对象，再 `unload()`；`PluginManager` 已清除 `PreventUnloadHint` 使 `unload()` 真正生效，破坏顺序会导致卸载后执行已释放代码。host 插件（pybridge）额外要求：host 在所有普通插件之后 shutdown（其子插件 VM 依赖 host 持有的解释器）。
- CPython 解释器任何时刻**不主动 finalize**（CPython 重复 init/finalize 不稳，进程退出由 OS 回收）；pybridge 走稳定 ABI（`Py_LIMITED_API=0x030C0000`），不再与某个 `python3XX.dll` 次版本绑定，但 nanobind/Qt 侧仍同受 ABI 锁定；用户安装的 Python 必须 ≥3.12 且与系统架构一致（x64）。
- 崩溃残留 AppBar：`WorkAreaGuard` 负责启动清旧注册/退出 `ABM_REMOVE` 归还工作区；强杀进程依赖系统延迟清理，属已知行为。工作区占用只用 `SHAppBarMessage(ABM_NEW)`（系统托管生命周期），**不用** `SPI_SETWORKAREA`（易被其他程序覆盖、残留难清）。
- DPI：`main.cpp` 首行必须保持 `PassThrough` 舍入策略；AppBar 矩形用物理像素、QML 内用逻辑像素，换算见 `BarWindow::setupBar`。
- 注册表自启项（Run 键）由托盘菜单切换；配置文件会写入 `%APPDATA%/XuanHub` —— 涉及这两处的改动注意权限与残留清理。
- 日志文件每次启动以 Truncate 打开；单实例锁必须先于日志初始化（顺序已在 main.cpp 注释中说明，勿颠倒）。

## 10. 关键文件索引

- 插件接口：`lib/barplugin/include/barplugin/IBarPlugin.h`（含 IID 声明）
- 插件扫描/生命周期：`app/src/PluginManager.cpp`
- QML 注入契约：`app/qml/PluginSlot.qml`
- 三栏布局：`app/qml/Bar.qml`；模型：`app/src/PluginListModel.h` + `PluginFilterModel`
- 配置解析：`app/src/ConfigService.cpp`；热重载编排：`app/src/ConfigHotReloader.{h,cpp}`
- 设置窗口（M8/M10）：`app/src/SettingsController.{h,cpp}`（懒建窗口、`--settings`/`--uitest` 调试钩子）、`SettingsViewModel.{h,cpp}`（草稿/校验/保存）、`SettingsPluginModel.{h,cpp}`（插件全集模型 + schema/config 草稿）、`SettingsPluginSortModel.{h,cpp}`（启用在前排序代理）、`SettingsPluginProxy.{h,cpp}`（自绘设置页草稿桥）、`app/qml/SettingsWindow.qml`；自绘设置页示例 `plugins/launcher/LauncherSettings.qml`
- 工作区/AppBar：`app/src/AppBarWin32.cpp`、`app/src/WorkAreaGuard.{h,cpp}`
- 任务栏嵌入（M11）：`app/src/TaskbarEmbed.{h,cpp}`（SetParent 到 Shell_TrayWnd + 轮询矫正）
- 线程范式样例：`plugins/netstat/NetStatWorker.{h,cpp}`
- 新插件 CMake 模板：`plugins/clock/CMakeLists.txt`
- Python 桥接：`lib/barplugin/include/barplugin/IBarPluginHost.h`（两阶段契约）、`plugins/pybridge/PyBridgePlugin.{h,cpp}`（host 外壳 + 环境探测/预装，不触碰 Python 符号）、`plugins/pybridge/PyBackendApi.h` + `PyBackend.cpp`（pybackend.dll：解释器初始化 + VM 工厂，LNK1194 拆分的唯一 Python 模块）、`plugins/pybridge/PyProxyViewModel.{h,cpp}`（代理 VM + QVariant↔py 转换，编入 pybackend.dll）
- Python 子插件开发契约：`plugins_py/README.md`；示例：`plugins_py/hello/`（参考样例）、`plugins_py/intime/`（定时休息提醒 + 全屏遮罩 + 自绘设置页）、`plugins_py/usage_hub/`（多厂商 API 用量看板 + 自绘设置页）
