# Python 子插件开发契约（M6）

> 由 `pybridge` 桥接插件承载：每个子插件运行在独立的 `python.exe` worker 进程中，
> 宿主只保留异步代理 ViewModel 注入 QML。Python 崩溃、GIL 卡死和扩展模块异常不会进入
> XuanHub 进程。写 Python 子插件**不需要**碰 C++。

## 目录结构

每个子目录是一个子插件，三个文件缺一不可（plugin.json / main.py / *.qml）：

```
plugins_py/
└── hello/
    ├── plugin.json   # 元数据（发现阶段的权威来源，读它不执行任何 Python 代码）
    ├── main.py       # 插件逻辑（模块级约定函数，见下）
    ├── Hello.qml     # 界面（契约同 C++ 插件）
    └── i18n/         # 可选；zh-CN.json / en-US.json，详见 docs/i18n.md
```

`plugin.json`：

```json
{
    "id": "py_hello",
    "name": "PyHello",
    "version": "0.1.0",
    "author": "you",
    "apiVersion": 2,
    "capabilities": ["barItem"],
    "permissions": [],
    "qml": "Hello.qml"
}
```

- `id` 全局唯一，约定 `py_` 前缀（与 DLL 插件的 id 命名空间区分，去重是全局的）。
- `qml`（可选）：显式声明 bar 视图 QML 文件名。目录里放了辅助组件（被视图
  import 的其它 .qml）时**必须**声明——未声明时框架按「排除 settingsQml 后
  字母序第一个 .qml」的启发式选择，组件名排在视图前就会被误选。
- 启用/布局在 `config.json` 里配置，与 C++ 插件同规则：

```json
{ "id": "py_hello", "position": "right", "order": 2, "config": { "intervalMs": 1000, "prefix": "Py" } }
```

- 另需保留 `{ "id": "pybridge", ... }` 条目作为总开关；删掉它则所有 Python 子插件都不加载。

### HostServices 与权限

`permissions` 使用稳定字符串声明需要的宿主能力；当前支持 `notifications`、
`storage.private`、`secrets.own`、`shell.openUrl` 和 `shell.openFile`。首批 Python RPC
开放日志和通知，其他服务会按路线图逐步接入。未声明通知权限时调用会返回
`{"success": false, "errorCode": "permission_denied", ...}`。

```python
from xuanhub_host import log, notify

def init(config):
    log("info", "plugin initialized", {"mode": config.get("mode", "default")})
    result = notify("Demo", "Ready")  # plugin.json 需声明 "notifications"
    if not result["success"]:
        log("warning", "notification rejected", {"code": result["errorCode"]})

# 也可选实现；对象与 xuanhub_host.services 是同一插件身份绑定的代理。
def set_host_services(services):
    pass
```

Worker 请求中没有可填写的插件 ID，来源由宿主按子插件实例绑定。单包上限 4 MiB，调用在
该插件现有 IPC 队列中串行并受请求超时约束。原生 DLL 是进程内受信任插件；Python 后端是
独立进程中的隔离插件，设置页会分别展示声明权限、实际授予权限与隔离级别。

### EventBus V2

从 `xuanhub_host` 导入 `events`。事件 handler 在 worker 主循环串行执行，不与
`update/invoke` 并发；单次只下发一个事件并等待 ack，避免与同步服务响应交错。

```python
from xuanhub_host import events

def on_locale(envelope):
    locale = envelope.get("data", {}).get("locale", "en-US")

def init(config):
    events.subscribe("app.locale", on_locale, replay_retained=True)
    events.publish("plugin.py_demo.ready", {"ready": True})

def invoke(name, args):
    if name == "query":
        result = events.request("plugin.py_demo.query", {"value": 1}, 5000)
```

API 为 `publish/subscribe/request/respond/state`。插件自己的 `plugin.<id>.*` 和 `app.*`
订阅不需要额外权限；跨插件订阅声明 `events.subscribe:plugin.other.*`，跨命名空间发布或
请求声明 `events.publish:<topic-pattern>`。身份、source 和 timestamp 均由宿主填写，Worker
不得自报插件 ID。每个 worker 最多缓存 128 个待下发事件，超限丢弃最新事件并记日志；停止
插件会清除作用域订阅与未完成请求。可选 `set_event_bus(events)` 在 `init()` 前收到同一代理。

### 设置项声明（settings / settingsQml，M10）

`plugin.json` 还可声明该插件在设置窗口里的设置区（均可选，C++ 插件的
`metadata.json` 契约相同）：

```json
{
    "id": "py_hello",
    "name": "PyHello",
    "settings": [
        { "group": "通用", "fields": [
            { "key": "intervalMs", "type": "int", "label": "刷新间隔 (ms)",
              "default": 1000, "min": 100, "max": 60000, "step": 100 },
            { "key": "prefix", "type": "string", "label": "前缀", "default": "Py" }
        ]}
    ]
}
```

- `settings`：分组 → 字段两级，设置窗口渲染为可折叠树。字段属性：`key`（config 段
  键名，必填）、`type`（必填，见下）、`label`（显示名，缺省=key）、`default`（config
  无此键时的初值）、`description`（控件旁小字提示）。
- 类型 → 控件：`bool`→CheckBox；`int`→SpinBox（`min`/`max`/`step`）；`double`→
  TextField+校验（`min`/`max`）；`string`→TextField（`placeholder`）；`color`→
  TextField+色块预览（**空串 = 插件内默认**，非空须 QColor 可解析）；`enum`→ComboBox
  （`options` 必填；推荐用 `{ "value": "daily", "label": "每日重复" }` 对象数组，
  `value` 是持久化稳定值、`label` 可翻译；旧字符串数组继续兼容）；`json`→多行编辑器
  （复杂结构兜底，保存前 JSON 校验）。不要把翻译后的标签写进 config。
- 编辑结果写入 config.json 的本插件 config 段（即 `init(config)` 收到的 dict），
  保存后热重载生效；schema 未声明的 config 键保存时原样保留。
- `settingsQml`：声明自绘设置页（如 `"settingsQml": "Settings.qml"`，相对本目录），
  适合列表/表格等复杂设置；与 `settings` 并存时**自绘优先**。页面契约：根 `Item`
  声明 `property var plugin`——但注入的是 **config 草稿桥**（不是运行中的代理 VM）：
  `plugin.value(key)` 读草稿、`plugin.setValue(key, v)` 写草稿（值须可 JSON 序列化）。
  `plugin.revision` 是草稿修订号，每次 `setValue` 后递增并触发 `valuesChanged`——
  `value()` 是函数调用、自身无通知，预览类绑定（如实时预览 Label）在求值表达式里
  引用一次 `plugin.revision` 建立依赖即可随编辑实时刷新（范式见
  `countdown/CountdownSettings.qml` 的 `v()`）。
  自绘页只碰草稿，应用/保存/取消由设置窗口统一，热重载后才真正生效。
  **时序契约（违反会把配置写进别的插件）**：`plugin` 在 `Loader.onLoaded` 才注入，
  晚于页面 `Component.onCompleted`——读草稿初始化必须放 `onPluginChanged`；
  注入时框架保证 `plugin.pluginId` 已指向本插件，异步写默认值（Timer 等）前
  仍须校验 `plugin.pluginId` 未变（范式见 `countdown/CountdownSettings.qml`）。

## main.py 模块级约定

```python
def init(config):    # 可选。注入 config.json 里本子插件的 config 段（dict）
def set_config(config): # 可选。运行中应用新 config；未实现时框架只重启本插件
def set_locale(locale): # 可选。init 后及运行中切换语言时收到 zh-CN / en-US
def update():        # 必需。每个 tick 调用，返回 dict → 原样填入 QML 的 plugin.values
def invoke(name, args):  # 可选。QML 里 plugin.call(name, args) 的入口
def stop():          # 可选。卸载/热重载前调用，用来停后台线程（见下节）
```

- 刷新节拍：`config.intervalMs`（毫秒，缺省 1000，合法范围 100–60000；越界值会被钳制，
  非数字回退缺省值）。下一次 `update()` 从上一次完成后开始计时，慢请求不会积压多个刷新任务。
- `invoke()` 会异步排队；worker 返回后框架立即再拉一次 `update()`，交互反馈不用等下个 tick。
  `plugin.call()` 的同步返回值仅表示是否成功入队，不能依赖 Python `invoke()` 的返回值。
- `update()` 返回的 dict 值支持：str / int / float / bool / list / dict（嵌套），其他类型转字符串。
- main.py 所在目录会追加到 `sys.path` 末尾，可导入同目录的辅助模块。
- 抛异常不会拖垮宿主：traceback 记入日志，UI 保留上一次的值。
- Worker/IPC 在 10 分钟内连续失败 5 次会进入熔断状态并停止自动重启；用户可在设置页查看
  最近错误、重启次数和队列深度，并只重启本插件。手动重启会清除熔断计数，其他插件不受影响。
- 实现 `set_config(config)` 后，设置保存会在同一 worker 内串行调用它并立即刷新数据，进程、
  模块级缓存和连接保持不变；未实现的旧插件继续采用“只重启本插件”的兼容路径。
- 固定界面文字应由 QML 通过 `qsTranslate("plugin.<id>.<page>", "中文源码")` 翻译；
  Python 后端返回状态码和动态明细。完整语言包、上下文隔离与回退规则见
  `docs/i18n.md`。

## 网络请求与后台线程

**`update()/invoke()` 内仍禁止阻塞**。它们现在在各自 worker 中执行，不会卡住宿主 GUI，
但同一插件的 IPC 请求串行：`requests.get()` 之类会让该插件的显示与点击反馈停滞，直到超时或
worker 被桥接层回收。允许自己起 Python 线程做后台 IO；线程只碰纯 Python 状态即可。
规则：线程只碰纯 Python 对象（**绝不触达任何 Qt/QML 对象**），结果写进模块级缓存，`update()` 只瞬读缓存；

```python
import threading, time, json, urllib.request

_cache = {"text": "加载中…"}
_stop = False

def _worker():
    while not _stop:
        try:
            with urllib.request.urlopen("https://api.example.com/data", timeout=5) as r:
                _cache["text"] = json.load(r)["value"]
        except Exception:
            _cache["text"] = "ERR"
        for _ in range(30):          # 分段 sleep，stop() 后 1s 内能退出
            if _stop: break
            time.sleep(1)

def init(config):
    threading.Thread(target=_worker, daemon=True).start()

def stop():
    global _stop
    _stop = True                     # 框架在卸载/热重载前调用；线程用 daemon，随时可被进程回收

def update():
    return dict(_cache)
```

**应实现 `stop()`**：热重载、宿主关闭或 IPC 断开时 worker 会尽力调用它；若插件代码卡死，
桥接层会在短暂宽限后终止整个 worker，避免旧线程或原生扩展残留在宿主进程中。
`urllib`（含 HTTPS）开箱即用；`requests` 等第三方库装进用户自装的 Python 即可直接 import
（解释器指向用户自装的 Python，其 `site-packages` 对插件可见）。
运行前提：机器装有 CPython ≥3.12（必须含 `python.exe`、标准库与 `python3XX.dll`）；未安装时
pybridge 整体停用并弹托盘提示，宿主与其他插件不受影响。打包脚本的 `-PythonStrategy Bundle`
会一并带上 `python.exe`、标准库与 DLL，做成免安装包。

## QML 契约

与 C++ 插件完全一致：根 `Item` 必须声明 `property var plugin` 和
`property var pluginContext`，框架加载后分别注入代理 ViewModel 与作用域宿主上下文：

```qml
Item {
    property var plugin
    property var pluginContext
    Text { text: (plugin && plugin.values.text) ? plugin.values.text : "..." }
    MouseArea { anchors.fill: parent; onClicked: if (plugin) plugin.call("bump", []) }
}
```

- 数据：`plugin.values.<key>`（dict 属性袋，整体替换触发刷新）。
- 行为：`plugin.call(name, args)` → 异步 IPC → main.py 的 `invoke(name, args)`；不要依赖其同步返回值。
- 宿主上下文：`pluginContext.colors/activeLocale/screenGeometry/barEdge/barPressed` 取代旧的
  全局 QML 属性；服务调用还提供稳定的 `success/errorCode/errorMessage` 结果。
- 健康：`plugin.health` 提供稳定的 `state`、`lastError`、`lastErrorAt`、`startedAt`、
  `lastHealthyAt`、`restartCount`、`consecutiveFailures`、`loadDurationMs` 和 `queueDepth` 字段。
  展示诊断信息时应容忍未来新增字段，不要根据错误文本驱动业务逻辑。

## 调试

- 日志：`%APPDATA%/XuanHub/xuanhub.log`（Python traceback 也在其中）。
- 改 main.py / *.qml 后**重新构建**（POST_BUILD 会同步到 dist）再重启宿主即生效，免重编 DLL。
  `bridge_worker.py` 是桥接协议实现，必须随 `plugins_py/` 一同部署，勿单独删除或修改其协议。
- 参考实现：`hello/`（时间刷新 + 点击计数）。

## 已知限制（后续里程碑）

- 依赖用户自装 CPython ≥3.12（未安装则整体停用 + 托盘提示）；
  `-PythonStrategy Bundle` 打包可内置标准库免除该依赖。
