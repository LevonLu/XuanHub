# Python 子插件开发契约（M6）

> 由 `pybridge` 桥接插件承载：它内嵌 CPython 解释器，扫描本目录下的子插件，
> 为每个子插件创建一个通用代理 ViewModel 注入 QML。写 Python 子插件**不需要**碰 C++。

## 目录结构

每个子目录是一个子插件，三个文件缺一不可（plugin.json / main.py / *.qml）：

```
plugins_py/
└── hello/
    ├── plugin.json   # 元数据（发现阶段的权威来源，读它不执行任何 Python 代码）
    ├── main.py       # 插件逻辑（模块级约定函数，见下）
    └── Hello.qml     # 界面（契约同 C++ 插件）
```

`plugin.json`：

```json
{
    "id": "py_hello",
    "name": "PyHello",
    "version": "0.1.0",
    "author": "you"
}
```

- `id` 全局唯一，约定 `py_` 前缀（与 DLL 插件的 id 命名空间区分，去重是全局的）。
- 启用/布局在 `config.json` 里配置，与 C++ 插件同规则：

```json
{ "id": "py_hello", "position": "right", "order": 2, "config": { "intervalMs": 1000, "prefix": "Py" } }
```

- 另需保留 `{ "id": "pybridge", ... }` 条目作为总开关；删掉它则所有 Python 子插件都不加载。

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
  （`options` 字符串数组，必填）；`json`→多行编辑器（复杂结构兜底，保存前 JSON 校验）。
- 编辑结果写入 config.json 的本插件 config 段（即 `init(config)` 收到的 dict），
  保存后热重载生效；schema 未声明的 config 键保存时原样保留。
- `settingsQml`：声明自绘设置页（如 `"settingsQml": "Settings.qml"`，相对本目录），
  适合列表/表格等复杂设置；与 `settings` 并存时**自绘优先**。页面契约：根 `Item`
  声明 `property var plugin`——但注入的是 **config 草稿桥**（不是运行中的代理 VM）：
  `plugin.value(key)` 读草稿、`plugin.setValue(key, v)` 写草稿（值须可 JSON 序列化）。
  自绘页只碰草稿，应用/保存/取消由设置窗口统一，热重载后才真正生效。

## main.py 模块级约定

```python
def init(config):    # 可选。注入 config.json 里本子插件的 config 段（dict）
def update():        # 必需。每个 tick 调用，返回 dict → 原样填入 QML 的 plugin.values
def invoke(name, args):  # 可选。QML 里 plugin.call(name, args) 的入口
def stop():          # 可选。卸载/热重载前调用，用来停后台线程（见下节）
```

- 刷新节拍：`config.intervalMs`（毫秒，缺省 1000）。
- `invoke()` 返回后框架会立即再拉一次 `update()`，交互反馈不用等下个 tick。
- `update()` 返回的 dict 值支持：str / int / float / bool / list / dict（嵌套），其他类型转字符串。
- main.py 所在目录会追加到 `sys.path` 末尾，可导入同目录的辅助模块。
- 抛异常不会拖垮宿主：traceback 记入日志，UI 保留上一次的值。

## 网络请求与后台线程

**`update()/invoke()` 内禁止阻塞**（它们在宿主 GUI 线程执行且持 GIL，`requests.get()` 之类会卡死整个 bar）。
但**允许**自己起 Python 线程做后台 IO——CPython 在标准库阻塞 IO 和 `sleep` 时会释放 GIL，不影响界面。
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

**必须实现 `stop()`**：热重载（如设置窗口保存配置）会销毁旧模块并重新 import，旧线程若不停下会泄漏运行。
`urllib`（含 HTTPS）开箱即用；`requests` 等第三方库装进用户自装的 Python 即可直接 import
（解释器指向用户自装的 Python，其 `site-packages` 对插件可见）。
运行前提：机器装有 CPython ≥3.12（python.org 或任意渠道）；未安装时 pybridge 整体停用并弹托盘提示，
宿主与其他插件不受影响（打包脚本可选 `-PythonStrategy Bundle` 内置标准库做成免安装包）。

## QML 契约

与 C++ 插件完全一致：根 `Item` 必须声明 `property var plugin`，框架加载后注入代理 ViewModel：

```qml
Item {
    property var plugin
    Text { text: (plugin && plugin.values.text) ? plugin.values.text : "..." }
    MouseArea { anchors.fill: parent; onClicked: if (plugin) plugin.call("bump", []) }
}
```

- 数据：`plugin.values.<key>`（dict 属性袋，整体替换触发刷新）。
- 行为：`plugin.call(name, args)` → main.py 的 `invoke(name, args)`。

## 调试

- 日志：`%APPDATA%/XuanHub/xuanhub.log`（Python traceback 也在其中）。
- 改 main.py / *.qml 后**重新构建**（POST_BUILD 会同步到 dist）再重启宿主即生效，免重编 DLL。
- 参考实现：`hello/`（时间刷新 + 点击计数）。

## 已知限制（后续里程碑）

- 无 EventBus 接入（Python 子插件暂不能收发总线事件）。
- 依赖用户自装 CPython ≥3.12（未安装则整体停用 + 托盘提示）；
  `-PythonStrategy Bundle` 打包可内置标准库免除该依赖（Embedded zip 策略仍实验性，未验证）。
