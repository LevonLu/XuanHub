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

## main.py 模块级约定

```python
def init(config):    # 可选。注入 config.json 里本子插件的 config 段（dict）
def update():        # 必需。每个 tick 调用，返回 dict → 原样填入 QML 的 plugin.values
def invoke(name, args):  # 可选。QML 里 plugin.call(name, args) 的入口
```

- 刷新节拍：`config.intervalMs`（毫秒，缺省 1000）。
- `invoke()` 返回后框架会立即再拉一次 `update()`，交互反馈不用等下个 tick。
- `update()` 返回的 dict 值支持：str / int / float / bool / list / dict（嵌套），其他类型转字符串。
- main.py 所在目录会追加到 `sys.path` 末尾，可导入同目录的辅助模块。
- **阻塞警告**：`update/invoke` 在宿主 GUI 线程内执行（持 GIL）。必须轻量、瞬时返回；
  禁止 `time.sleep`、网络请求等阻塞调用，禁止自己起线程触达 UI —— 整个 bar 会卡死。
  重采集需求是后续里程碑的事（C++ worker 线程 + GIL release）。
- 抛异常不会拖垮宿主：traceback 记入日志，UI 保留上一次的值。

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
- 无 embeddable Python 分发（当前依赖系统 Python 3.13，经构建期烧入的 PYTHONHOME 兜底定位）。
- 无重负载采集通道（见上方阻塞警告）。
