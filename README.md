# 玄枢 / XuanHub

基于 **Qt6 / QML** 的 Windows 插件化状态栏框架。当前版本 0.4.4。

XuanHub 提供两种工作模式：`appbar` 以无边框、置顶窗口占据主屏顶部或底部边缘，并经 Win32 AppBar 向系统申请工作区；`taskbar` 则嵌入 Windows 原生任务栏左侧，背景透明且不额外占用桌面工作区。栏上的一切内容——时钟、网速、启动器……——都是可插拔的插件。插件有两种形态：

- **C++ 插件**：独立 DLL，性能最好，可直接调用 Win32 API；
- **Python 子插件**：由内置的 `pybridge` 桥接插件承载，每个插件运行在独立的 `python.exe` worker 中；**写插件不需要 Qt、MSVC 或 C++ 编译环境**。

## 特性

- 双工作模式：`appbar` 支持顶部/底部贴边并保留工作区，`taskbar` 嵌入原生任务栏并随内容收缩
- 插件化：DLL 热插拔，增删插件目录 + 重启即可装卸；三栏布局（左 / 中 / 右），顺序可配
- Python 桥接：每个 `plugins_py/` 子插件运行在独立 Python worker 进程，异常、超时或崩溃不会拖垮宿主；界面仍是 QML，逻辑用 Python 写
- 主题系统：Theme Schema v2 设计令牌 × dark/light 双模式；支持区域胶囊、单项卡片和可选可信 Bar 外壳扩展
- 配置热重载：保存 `config.json` 立即生效，无需重启
- 图形化设置窗口：插件启停 / 位置 / 排序、主题切换、插件级设置，全部可视化完成
- 托盘菜单：开机自启、明暗切换、重载配置、退出

## 运行环境

- Windows 10 / 11，x64
- **基础功能零依赖**：宿主与 C++ 插件开箱即用，分发包已附带 MSVC 运行库
- **官方 v0.4.0 便携包已内置 CPython 3.13.6 运行时**，Python 子插件开箱即用，无需另装 Python。正式发行仅提供这一种便携包，不区分 Bundle / 非 Bundle 版本

---

# 使用说明

## 安装与运行

分发包是绿色便携目录，解压到任意位置后运行其中的 `XuanHub.exe` 即可，无需安装、不写系统目录。程序为单实例：重复启动会直接退出。

带 `portable` 标记的便携包把 `config.json`、`secrets.json`、锁和 `xuanhub.log` 都放在 `XuanHub.exe` 同目录；普通安装模式的日志位于 `%APPDATA%/XuanHub/xuanhub.log`。日志每次启动清空重写，遇到插件不显示、界面异常时先看它。

## 托盘菜单

运行后系统托盘出现玄枢图标，右键菜单：

| 菜单项 | 作用 |
|---|---|
| 设置... | 打开设置窗口（见下） |
| 开机自启 | 开机自启开关（写注册表 Run 键） |
| 任务栏模式 | 切换 appbar / taskbar 工作模式（写回 `bar.mode`，热重载生效） |
| 切换深色/浅色 | 切换 dark / light 模式（即时生效，不写回配置文件） |
| 重新加载配置 | 手动重载配置（通常不需要——保存配置文件会自动热重载） |
| 退出 | 退出，自动归还占用的桌面工作区 |

## 配置文件 `config.json`

配置查找顺序：**exe 同目录的 `config.json` 优先**（便携模式）；不存在则使用 `%APPDATA%/XuanHub/config.json`（首次运行自动播种一份默认配置）。**保存文件即热重载**，插件、主题、栏位置全部即时刷新。

完整示例：

```json
{
  "bar": {
    "height": 40,
    "edge": "top",
    "monitor": "primary",
    "opacity": 1.0,
    "themeId": "xuanhub-default",
    "theme": "dark",
    "mode": "appbar",
    "taskbarOffsetX": 0
  },
  "plugins": [
    { "id": "sysinfo",  "position": "right", "order": 0, "config": { "intervalMs": 1000 } },
    { "id": "clock",    "position": "right", "order": 1, "config": { "format": "HH:mm:ss" } },
    { "id": "py_hello", "position": "right", "order": 2, "config": { "intervalMs": 1000, "prefix": "Py" } },
    { "id": "pybridge", "position": "right", "order": 9, "config": {} }
  ]
}
```

`bar` 段字段：

| 字段 | 取值 | 说明 |
|---|---|---|
| `height` | 整数（像素） | 栏高，缺省 40 |
| `edge` | `top` / `bottom` | 贴哪条边，缺省 `top` |
| `monitor` | `primary` | 预留字段（当前版本未解析，恒用主屏；写回配置时原样保留） |
| `opacity` | 0.0–1.0 | 栏体不透明度，缺省 1.0 |
| `themeId` | 主题包 id | 对应 `themes/<id>/`，缺省 `xuanhub-default` |
| `theme` | `dark` / `light` | 明暗模式，缺省 `dark` |
| `mode` | `appbar` / `taskbar` | 工作模式，缺省 `appbar`；`taskbar` 会嵌入 Windows 原生任务栏 |
| `taskbarOffsetX` | 整数（像素） | `taskbar` 模式下的水平位置微调，缺省 0 |

`plugins` 数组每项：

| 字段 | 说明 |
|---|---|
| `id` | 插件 id。**在数组中出现 = 启用**，删除该项即停用 |
| `position` | `left` / `center` / `right`，空默认 `right`，非法值回落 `right` |
| `order` | 同栏内的排序，小的在前 |
| `config` | 插件自己的配置段，原样透传给插件（键由插件各自定义） |

## 设置窗口

托盘菜单 → 设置... 打开。左侧为插件列表（启用在前，勾选即时归位），右侧为选中插件的设置：启停、位置、排序，以及插件自己声明的设置项（表单或插件自绘设置页）。点保存后写入 `config.json`，经热重载生效。Python 子插件的 `pybridge` 总开关由设置窗口自动托管，无需手工维护。

含 API Key 等敏感字段的设置会使用当前 Windows 用户的 **DPAPI** 加密：`config.json` 仅保存引用标记，运行时才解密注入插件。用量中心的厂商密钥也适用；已有明文配置在设置窗口中保存一次即可迁移。

## 主题

主题包位于 `themes/<id>/`。普通主题只需 `theme.json`，可声明色板、圆角、间距、区域与插件槽装饰；复杂主题还可提供经过路径校验的 `barChrome` QML 外壳扩展。放进 `themes/` 后即可被识别（设置窗口或热重载后可见）。完整契约与信任边界见 `themes/README.md`。

## 卸载

退出程序即归还工作区，无常驻服务。彻底清理时删除：便携目录本身、`%APPDATA%/XuanHub/`（配置与日志）；若开过开机自启，请先在同名菜单项关闭（或手动删除注册表 Run 键中的 XuanHub 项）。

---

# 插件开发总览

开发资料统一从 [`docs/README.md`](docs/README.md) 进入。公开契约按运行时分别维护：

| 开发目标 | 权威文档 |
|---|---|
| C++ 原生插件 | [`docs/plugin-sdk-v2.md`](docs/plugin-sdk-v2.md) |
| Python 隔离插件 | [`plugins_py/README.md`](plugins_py/README.md) |
| QML/C++/Python 多语言 | [`docs/i18n.md`](docs/i18n.md) |
| `.xuanplugin` 打包与签名 | [`docs/xuanplugin-format.md`](docs/xuanplugin-format.md) |
| 静态更新源 | [`docs/xuanplugin-update-index.md`](docs/xuanplugin-update-index.md) |

新插件先用统一 CLI 生成模板，再按对应权威文档实现：

```powershell
python scripts/xuanhub_plugin.py create plugins/my_plugin --runtime native --id com.example.my_plugin --name "My Plugin"
python scripts/xuanhub_plugin.py create plugins_py/my_plugin --runtime python --id py_my_plugin --name "My Plugin"
python scripts/xuanhub_plugin.py validate <插件目录>
```

CLI 还提供 `test/run/doctor/package`；准确参数使用
`python scripts/xuanhub_plugin.py <命令> --help` 查询。

当前原生 DLL 是同进程受信任代码，权限声明不能形成安全沙箱；第三方不受信任插件应优先使用
Python/Protocol V2 隔离路径。`experiments/native_worker/` 已验证原生后端进程隔离的崩溃、
超时和开销，但尚未接入发布版。边界、指标和产品化门槛见
`docs/native-worker-isolation-evaluation.md`。

| | C++ 插件 | Python 子插件 |
|---|---|---|
| 形态 | `plugins/<id>/<id>.dll + <Id>.qml + metadata.json` | `plugins_py/<name>/plugin.json + main.py + <X>.qml` |
| 界面 | QML（与宿主同引擎） | QML（与宿主同引擎，契约完全相同） |
| 逻辑 | C++ ViewModel（QObject + Q_PROPERTY） | 独立 worker 进程中的 `main.py` 模块级约定函数 |
| 构建 | 需与宿主同一构建树编译（ABI 锁定） | 无需编译；可直接使用便携包作为开发宿主 |
| 适用 | 性能敏感、要调 Win32 / 第三方 native 库 | 快速迭代、数据处理、网络 API 类插件 |

两种插件的**界面契约完全一致**：QML 根 `Item` 声明 `property var plugin`，框架加载后把 ViewModel（Python 插件是代理 ViewModel）注入该属性，QML 内一律用 `plugin ? plugin.someProperty : fallback` 防御空值。

---

# C++ 插件开发（SDK V2）

内置 `clock`、`sysinfo`、`xuanhub_menu` 和 `pybridge` 均实现 `IBarPlugin2`，使用 IID
`xuanhub.BarPlugin/2.0`。宿主只为外部旧插件保留 V1 适配；开发和迁移统一按
[`docs/plugin-sdk-v2.md`](docs/plugin-sdk-v2.md) 的公开契约进行。

使用开发者 CLI 创建 V2 骨架，再实现功能：

```powershell
python scripts/xuanhub_plugin.py create plugins/myplugin --runtime native --id myplugin --name "My Plugin"
python scripts/xuanhub_plugin.py validate plugins/myplugin
pixi run build
python scripts/xuanhub_plugin.py validate build/default/dist/Release/plugins/myplugin
python scripts/xuanhub_plugin.py run build/default/dist/Release/plugins/myplugin
python scripts/xuanhub_plugin.py doctor build/default/dist/Release/plugins/myplugin
```

仓库内插件需要将自己的目录接入顶层 CMake；独立 SDK 构建参考
[`examples/plugins/v2_demo/`](examples/plugins/v2_demo/)。插件只链接共享 `barplugin` 与必要的
Qt/系统库，不能依赖宿主 `app/` 源码；Qt、MSVC、x64 和 CRT 必须与宿主一致。

- `descriptor()` 与 `metadata.json` 的 V2 字段保持一致，供宿主在加载前预检。
- `initialize/validateConfig/applyConfig/shutdown` 提供结构化结果；配置变更优先原地应用。
- QML 声明 `plugin` 和 `pluginContext`，通过后者获取主题、语言、屏幕和弹窗/动作服务。
- 后台工作使用 `QObject + QThread + QTimer`；卸载前停止线程并销毁 DLL 创建的对象。
- 自绘设置页声明在 `entry.settingsQml`，事件通过 `PluginContext2::eventScope` 使用 V2 信封。

按功能参考 [`plugins/clock/`](plugins/clock/)（基本生命周期与配置迁移）、
[`plugins/sysinfo/`](plugins/sysinfo/)（后台采集、自绘设置与动作）、
[`plugins/xuanhub_menu/`](plugins/xuanhub_menu/)（作用域事件与宿主菜单）和
[`plugins/pybridge/`](plugins/pybridge/)（组合 host 扩展接口）。完整 Manifest、权限、
国际化、布局、设置及分发规则集中在 [SDK 文档](docs/plugin-sdk-v2.md) 中维护。

---

# Python 插件开发（pybridge）

`pybridge` 是一个特殊的桥接插件：它扫描 `<exeDir>/plugins_py/` 下的子插件，为每个插件启动独立的 `python.exe` worker，并创建通用代理 ViewModel 注入 QML。pybridge 不链接或内嵌 CPython，单个 Worker 的异常、阻塞或崩溃不会进入宿主进程。**写 Python 插件只需文本文件，零 C++、零编译。**

完整开发契约见 [`plugins_py/README.md`](plugins_py/README.md)，参考实现见
[`plugins_py/hello/`](plugins_py/hello/)。以下仅为快速上手要点，Protocol V2、依赖、
HostServices、EventBus 和配置迁移均以权威契约为准。

```powershell
python scripts/xuanhub_plugin.py create plugins_py/my_plugin --runtime python --id py_my_plugin --name "My Plugin"
python scripts/xuanhub_plugin.py validate plugins_py/my_plugin
python scripts/xuanhub_plugin.py test plugins_py/my_plugin
```

## 不安装 Qt/MSVC，直接开发 Python 插件

只开发 Python 插件时，不需要 clone 后构建整个工程，也不需要安装 Qt、Visual Studio 或 CMake。可直接把官方便携包当作开发宿主：

1. 从 GitHub Releases 下载 Windows x64 便携包，解压到一个可写目录；不要放进需要管理员权限才能修改的目录。
2. 官方便携包已经包含 Python 运行时，无需另行安装 Python。
3. 复制便携包里的 `plugins_py/hello/` 作为模板，例如改名为 `plugins_py/mytool/`，然后修改其中的 `plugin.json`、`main.py` 和 QML 文件。无需运行任何 CMake 命令。
4. 确认 `plugin.json.id` 全局唯一并使用 `py_` 前缀，例如 `py_mytool`；`qml` 字段应指向该目录中的主界面文件。
5. 启动或重启 `XuanHub.exe`，打开“托盘菜单 → 设置...”，在插件列表中启用新插件并选择位置。设置窗口会自动维护 `pybridge` 总开关。
6. 也可以直接编辑便携目录根部的 `config.json`，在 `plugins` 数组中同时保留 `pybridge` 并加入新插件：

```json
{
  "plugins": [
    { "id": "py_mytool", "position": "right", "order": 2, "config": { "intervalMs": 1000 } },
    { "id": "pybridge", "position": "right", "order": 9, "config": {} }
  ]
}
```

开发循环：

- 纯 Python 逻辑可以直接用本机 Python 运行单元测试，不依赖 Qt；让 `main.py` 保持只依赖普通 Python 模块，便于脱离宿主测试。
- 修改便携目录中的 `main.py`、`plugin.json` 或 QML 后，退出并重新启动 XuanHub 验证。配置值可以通过设置窗口或保存 `config.json` 触发热重载；QML 组件存在进程内缓存，改 QML 后必须重启。
- 便携模式的日志就在 `XuanHub.exe` 同目录的 `xuanhub.log`；Python traceback、worker 启动失败和 QML 加载错误都记录在这里。
- 标准库可直接使用。第三方纯 Python 包可以随插件目录一起分发；需要原生扩展的依赖必须与便携包内置的 CPython 版本和 x64 架构匹配。依赖应在 `plugin.json` 中声明，具体规则见 [`plugins_py/README.md`](plugins_py/README.md)。
- 不要把真实 API Key 写进 `plugin.json`、源码或示例配置。敏感设置应通过插件设置 schema 标记为 `secret`，由 XuanHub 使用 Windows DPAPI 保存。

准备分享插件时，只打包自己的 `plugins_py/<name>/` 目录，排除 `__pycache__`、`*.pyc`、日志、测试凭据和本机配置；使用者解压到其 XuanHub 的 `plugins_py/` 后重启即可发现。

## 目录契约

```
plugins_py/
└── mytool/
    ├── plugin.json   # 元数据（发现阶段的权威来源，读它不执行任何 Python 代码）
    ├── main.py       # 插件逻辑（模块级约定函数）
    └── MyTool.qml    # 界面（契约与 C++ 插件完全一致）
```

`plugin.json`：

```json
{
    "id": "py_mytool",
    "name": "PyMyTool",
    "version": "0.1.0",
    "author": "you"
}
```

- `id` 全局唯一，约定 `py_` 前缀（与 DLL 插件的 id 命名空间区分）；
- 启用/布局在 `config.json` 里配置，规则同 C++ 插件：`{ "id": "py_mytool", "position": "right", "order": 2, "config": { "intervalMs": 1000 } }`；
- `config.json` 中还必须保留 `pybridge` 条目作为 Python 插件总开关；通过设置窗口启用插件时框架会自动维护；
- 设置项声明（`settings` schema / `settingsQml` 自绘页）与 C++ 插件同一契约，见 `plugins_py/README.md`。

## `main.py` 模块级约定

```python
def init(config):           # 可选。注入 config.json 里本子插件的 config 段（dict）
def update():               # 必需。每个 tick 调用，返回 dict → 填入 QML 的 plugin.values
def invoke(name, args):     # 可选。QML 里 plugin.call(name, args) 的入口
def stop():                 # 可选。卸载/热重载前调用，用来停后台线程
```

- 刷新节拍：`config.intervalMs`（毫秒，缺省 1000）；
- `update()` 返回的 dict 值支持 str / int / float / bool / list / dict（嵌套），其他类型转字符串；
- `invoke()` 会异步排队；其同步返回值仅表示请求是否已进入队列，worker 完成后框架会立即补拉一次 `update()`；
- `main.py` 所在目录会追加到 `sys.path` 末尾，可导入同目录辅助模块；
- 每个子插件在独立 Python worker 进程中执行，异常、IPC 协议错误、超时或 worker 崩溃会被桥接层隔离并自动重试；健康完成一次 `update()` 后重试退避会重置；
- 抛异常不会拖垮宿主：traceback 记入日志，UI 保留上一次的值。

最小示例：

```python
from datetime import datetime

_prefix = "Py"

def init(config):
    global _prefix
    _prefix = config.get("prefix", "Py")

def update():
    return {"text": f"{_prefix} {datetime.now():%H:%M:%S}"}

def invoke(name, args):
    if name == "bump":
        return {"ok": True}
```

## QML 契约

与 C++ 插件完全一致，只是注入的是代理 ViewModel：

```qml
import QtQuick

Item {
    property var plugin
    width: 120
    height: 40

    Text {
        anchors.centerIn: parent
        color: themeManager ? themeManager.colors.fg : "#cdd6f4"
        text: (plugin && plugin.values.text) ? plugin.values.text : "..."
    }
    MouseArea {
        anchors.fill: parent
        onClicked: if (plugin) plugin.call("bump", [])
    }
}
```

- 数据：`plugin.values.<key>`（`update()` 返回的 dict 属性袋，整体替换触发刷新）；
- 行为：`plugin.call(name, args)` → `main.py` 的 `invoke(name, args)`；同步返回 queued/id，
  最终结果和进度由 `callFinished/callProgress` 信号交付，详见 `plugins_py/README.md`。

## 网络请求与后台线程（铁律）

- `init()` / `update()` / `invoke()` 在各自的 Python worker 进程中串行执行，不会卡住宿主 GUI；但同一插件的请求会相互等待，**仍禁止阻塞**（`requests.get()` 之类会让该插件的显示与点击反馈停滞，直至超时或 worker 被回收）；
- 允许自起 Python 线程做后台 IO（标准库阻塞 IO 和 `sleep` 会释放 GIL），但线程**只碰纯 Python 对象、绝不触达任何 Qt/QML 对象**，结果写模块级缓存，`update()` 只瞬读缓存；
- **必须实现 `stop()`**：热重载或隔离 worker 退出前会调用它；只发停止信号，不要等待网络请求或线程长时间退出。

范式（精简版，完整注释版见 `plugins_py/README.md`）：

```python
import threading, time

_cache = {"text": "加载中…"}
_stop = False

def _worker():
    while not _stop:
        _cache["text"] = do_slow_io()          # 后台线程算数/联网
        for _ in range(30):                    # 分段 sleep，stop() 后 1s 内能退出
            if _stop: break
            time.sleep(1)

def init(config):
    threading.Thread(target=_worker, daemon=True).start()

def stop():
    global _stop
    _stop = True

def update():
    return dict(_cache)
```

`urllib`（含 HTTPS）开箱即用。官方便携包优先使用 exe 同目录内置的 Python；第三方纯 Python 依赖可随插件目录分发，需要原生扩展的依赖必须匹配内置 Python 的版本与 x64 架构。

## 调试

- 日志：便携模式为 `<exeDir>/xuanhub.log`，普通模式为 `%APPDATA%/XuanHub/xuanhub.log`（Python traceback 也在其中）；
- 直接编辑便携包时无需构建，改 `main.py` / `plugin.json` / `*.qml` 后重启宿主；在源码构建树中开发时，重新构建会通过 POST_BUILD 把 `plugins_py/` 同步到运行目录；
- Python 子插件通过 pybridge 使用内置运行时；若运行时文件缺失或损坏，只会停用 Python 子插件并弹出托盘提示，不影响宿主与 C++ 插件。

---

# 从源码构建

前提：Qt 6.9.3（msvc2022_64）、MSVC 2022、CMake ≥ 3.21。Qt 安装路径不同就改 `CMakePresets.json` 或新建 `CMakeUserPresets.json` 覆盖 `CMAKE_PREFIX_PATH`。

```bash
cmake --preset default                 # 配置（生成到 build/default）
cmake --build --preset default         # Release
cmake --build --preset default-debug   # Debug
```

运行产物统一在 `build/default/dist/<CONFIG>/`（含 `XuanHub.exe`、插件目录、Qt 依赖，链接后自动 windeployqt）。仓库根的 `config/config.json` 是开发示例，需手动拷到该目录才会以便携模式生效。pybridge 始终参与构建，不链接 CPython；运行时找不到 Python ≥3.12 时只停用 Python 子插件。

正式发行提供单一的 `XuanHub-v<版本>-portable-x64.zip`，其中包含 Python 运行时，不再区分 Bundle / 非 Bundle 版本。仓库中的 `package-bundle` 等名称只是打包脚本的内部策略入口：它们会从构建输出创建全新的 staging，不复用运行目录里的配置、日志、锁或 DPAPI 密钥库；成品固定使用内置默认配置，并在压缩前检查敏感文件、密钥形状和本机用户路径。`pixi run package-nozip` 只保留通过同一安全检查的 staging 目录。

## 仓库目录速览

```
app/            # 宿主 XuanHub.exe（栏窗口、AppBar、配置、设置窗口、托盘、主题）
lib/barplugin/  # 插件接口共享库（IBarPlugin / BarPluginContext / EventBus）
plugins/        # C++ 插件：clock / sysinfo / pybridge
plugins_py/     # Python 子插件（README.md 即开发契约）
themes/         # 主题包（README.md 即开发契约）
config/         # 开发用示例配置
AGENTS.md       # 面向 AI 代理的项目说明（架构细节最齐全的一份文档）
```

## 许可证

XuanHub 使用 [BSD 3-Clause License](LICENSE)。允许个人和商业用途、修改及再分发；源码和二进制分发均须按许可证保留以下署名信息：

- 项目地址：<https://github.com/LevonLu/XuanHub>
- GitHub：[@LevonLu](https://github.com/LevonLu)
