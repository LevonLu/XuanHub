# 玄枢 / XuanHub

基于 **Qt6 / QML** 的 Windows 插件化状态栏框架（对标 polybar / waybar / Rainmeter）。当前版本 0.1.0。

状态栏以无边框、置顶窗口的形式占据主屏顶部或底部边缘（经 Win32 AppBar 向系统申请工作区，其他窗口最大化时不会遮挡它）；栏上的一切内容——时钟、网速、启动器……——都是可插拔的插件。插件有两种形态：

- **C++ 插件**：独立 DLL，性能最好，可直接调用 Win32 API；
- **Python 子插件**：由内置的 `pybridge` 桥接插件承载（内嵌 CPython，稳定 ABI 兼容 3.12+），**写插件不需要碰 C++**，改完即生效。

## 特性

- 贴边占位：顶部/底部可选，AppBar 工作区保留，退出自动归还
- 插件化：DLL 热插拔，增删插件目录 + 重启即可装卸；三栏布局（左 / 中 / 右），顺序可配
- Python 桥接：每个 `plugins_py/` 子插件运行在独立 Python worker 进程，异常、超时或崩溃不会拖垮宿主；界面仍是 QML，逻辑用 Python 写
- 主题系统：纯 JSON 主题包 × dark/light 双模式，可多主题包切换，全部插件自动跟随
- 配置热重载：保存 `config.json` 立即生效，无需重启
- 图形化设置窗口：插件启停 / 位置 / 排序、主题切换、插件级设置，全部可视化完成
- 托盘菜单：开机自启、明暗切换、重载配置、退出

## 运行环境

- Windows 10 / 11，x64
- **基础功能零依赖**：宿主与 C++ 插件开箱即用，分发包已附带 MSVC 运行库
- **Python 子插件需要自装 CPython 3.12+**（python.org 下载安装即可，程序经注册表/py launcher/PATH 自动定位，无需配置）。未安装时：宿主与 C++ 插件完全不受影响，Python 子插件自动停用，启动时托盘弹出提示；安装后重启自动启用

---

# 使用说明

## 安装与运行

分发包是绿色便携目录，解压到任意位置后运行其中的 `XuanHub.exe` 即可，无需安装、不写系统目录。程序为单实例：重复启动会直接退出。

日志写入 `%APPDATA%/XuanHub/xuanhub.log`（每次启动清空重写）。遇到插件不显示、界面异常时先看它。

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
    "theme": "dark"
  },
  "plugins": [
    { "id": "sysinfo",  "position": "right", "order": 0, "config": { "intervalMs": 1000 } },
    { "id": "clock",    "position": "right", "order": 1, "config": { "format": "HH:mm:ss" } },
    { "id": "py_hello", "position": "right", "order": 2, "config": { "intervalMs": 1000, "prefix": "Py" } }
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

主题包是 `themes/<id>/theme.json` 的纯 JSON 声明式目录，放进 `themes/` 即可被识别（设置窗口或热重载后可见）。制作规范见 `themes/README.md`。

## 卸载

退出程序即归还工作区，无常驻服务。彻底清理时删除：便携目录本身、`%APPDATA%/XuanHub/`（配置与日志）；若开过开机自启，请先在同名菜单项关闭（或手动删除注册表 Run 键中的 XuanHub 项）。

---

# 插件开发总览

| | C++ 插件 | Python 子插件 |
|---|---|---|
| 形态 | `plugins/<id>/<id>.dll + <Id>.qml + metadata.json` | `plugins_py/<name>/plugin.json + main.py + <X>.qml` |
| 界面 | QML（与宿主同引擎） | QML（与宿主同引擎，契约完全相同） |
| 逻辑 | C++ ViewModel（QObject + Q_PROPERTY） | 独立 worker 进程中的 `main.py` 模块级约定函数 |
| 构建 | 需与宿主同一构建树编译（ABI 锁定） | 无需编译，改文件重建同步即生效 |
| 适用 | 性能敏感、要调 Win32 / 第三方 native 库 | 快速迭代、数据处理、网络 API 类插件 |

两种插件的**界面契约完全一致**：QML 根 `Item` 声明 `property var plugin`，框架加载后把 ViewModel（Python 插件是代理 ViewModel）注入该属性，QML 内一律用 `plugin ? plugin.someProperty : fallback` 防御空值。

---

# C++ 插件开发

## 插件模型速览

- 插件是 Qt 插件机制的 **MODULE DLL**，经 `Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0")` 声明身份；
- 宿主启动（或配置热重载）时扫描 `<exeDir>/plugins/*/` 下的 `*.dll`，用 `QPluginLoader` 加载并校验 IID，再 `qobject_cast` 到 `IBarPlugin` 接口；
- 插件**只链接 `barplugin`** 接口库，对宿主核心零依赖；核心只通过接口拿到 `QObject* viewModel()` 与 `QUrl qmlUrl()`，QML 实例化由核心统一完成。

## 安装结构（= 源码目录结构）

```
plugins/
└── myplugin/
    ├── MyPlugin.h / MyPlugin.cpp      # 插件入口类（实现 IBarPlugin）
    ├── MyViewModel.h / .cpp           # 暴露给 QML 的数据/行为模型
    ├── MyPlugin.qml                   # 界面（裸文件，免重编 DLL 即可改样式）
    ├── metadata.json                  # 元数据（烧进 DLL，也是发现阶段的权威来源）
    └── CMakeLists.txt
```

构建产出的运行时布局：`<exeDir>/plugins/myplugin/myplugin.dll + MyPlugin.qml + metadata.json`。**三个文件缺一不可**。

## 接口契约

`lib/barplugin/include/barplugin/IBarPlugin.h`：

```cpp
namespace xuanhub {

struct PluginMetadata {                 // PluginMetadata.h
    QString id;          // 全局唯一，配置/EventBus 寻址用
    QString displayName; // 显示名
    QString version;
    QString author;
};

struct BarPluginContext {               // BarPluginContext.h，init 时注入
    EventBus*   eventBus = nullptr;     // 事件总线（进程内单例）
    QString     configDir;              // %APPDATA%/XuanHub
    QVariantMap pluginConfig;           // config.json 里本插件的 config 段
};

class IBarPlugin {
public:
    virtual ~IBarPlugin() = default;
    virtual PluginMetadata metadata() const = 0;        // 元数据
    virtual bool init(BarPluginContext* ctx) = 0;       // 初始化，返回 false 视为加载失败
    virtual QObject* viewModel() = 0;                   // 暴露给 QML 的模型，可为 nullptr
    virtual QUrl qmlUrl() const = 0;                    // 相对 DLL 目录的 QML 文件名
    virtual void shutdown() = 0;                        // 卸载前清理（先停线程，再删对象）
};

}
Q_DECLARE_INTERFACE(xuanhub::IBarPlugin, "xuanhub.BarPlugin/1.0")
```

铁律：

- 接口**绝不返回** `QWidget*` / `QQuickItem*` —— 界面一律走 QML；
- `viewModel()` 返回的对象生命周期归插件自己管，`shutdown()` 里必须删干净（宿主随后会 `unload()` DLL，泄漏的 QObject 会执行已释放的代码）。

## 最小完整示例

以精简版时钟为例（实际可照抄 `plugins/clock/`）。

`MyPlugin.h`：

```cpp
// MyPlugin.h — 最小示例插件入口
#pragma once
#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin.h"

class MyViewModel;

class MyPlugin : public QObject, public xuanhub::IBarPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/1.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin)

public:
    xuanhub::PluginMetadata metadata() const override;
    bool    init(xuanhub::BarPluginContext* ctx) override;
    QObject* viewModel() override;
    QUrl    qmlUrl() const override;
    void    shutdown() override;

private:
    MyViewModel* m_vm = nullptr;
};
```

`MyPlugin.cpp`：

```cpp
// MyPlugin.cpp
#include "MyPlugin.h"
#include "MyViewModel.h"

using namespace xuanhub;

static const PluginMetadata kMeta = {
    QStringLiteral("myplugin"),
    QStringLiteral("MyPlugin"),
    QStringLiteral("0.1.0"),
    QStringLiteral("you"),
};

PluginMetadata MyPlugin::metadata() const { return kMeta; }

bool MyPlugin::init(BarPluginContext* ctx) {
    const QVariantMap cfg = ctx ? ctx->pluginConfig : QVariantMap();
    m_vm = new MyViewModel();
    // 从 config.json 的本插件 config 段读配置
    const QString fmt = cfg.value(QStringLiteral("format")).toString();
    if (!fmt.isEmpty()) m_vm->setFormat(fmt);
    m_vm->start();
    return true;
}

QObject* MyPlugin::viewModel() { return m_vm; }

QUrl MyPlugin::qmlUrl() const { return QUrl(QStringLiteral("MyPlugin.qml")); }

void MyPlugin::shutdown() {
    if (m_vm) {
        m_vm->stop();      // 先停 QTimer / 线程
        delete m_vm;       // 再删对象
        m_vm = nullptr;
    }
}
```

`MyViewModel.h`（数据模型：`Q_PROPERTY` + `QTimer`）：

```cpp
// MyViewModel.h — 暴露给 QML 的数据模型
#pragma once
#include <QObject>
#include <QString>

class QTimer;

class MyViewModel : public QObject {
    Q_OBJECT
    // QML 里用 plugin.timeText 绑定；值变化必须 emit timeTextChanged()
    Q_PROPERTY(QString timeText READ timeText NOTIFY timeTextChanged)

public:
    explicit MyViewModel(QObject* parent = nullptr);
    ~MyViewModel() override;

    QString timeText() const { return m_timeText; }
    void setFormat(const QString& f);
    void start();
    void stop();

signals:
    void timeTextChanged();

private:
    QTimer* m_timer;
    QString m_timeText;
    QString m_format = QStringLiteral("HH:mm:ss");
};
```

`MyPlugin.qml`（界面契约）：

```qml
// MyPlugin.qml — 根 Item 必须声明 property var plugin，框架加载后注入 viewModel
import QtQuick

Item {
    id: root
    property var plugin

    width: 110          // 宽度自定；高度由宿主槽位接管（跟随栏高），这里写的仅作回退
    height: 40

    // 主题色统一从宿主 context property 取（见「QML 运行环境」）
    Text {
        anchors.centerIn: parent
        color: themeManager ? themeManager.colors.fg : "#cdd6f4"
        font.pixelSize: 16
        text: plugin ? plugin.timeText : "--:--:--"   // 必须防御 plugin 为空
    }

    MouseArea {
        anchors.fill: parent
        onClicked: console.log("[MyPlugin] clicked")
    }
}
```

`metadata.json`：

```json
{
    "id": "myplugin",
    "name": "MyPlugin",
    "version": "0.1.0",
    "author": "you"
}
```

`id` 必须与 `Q_PLUGIN_METADATA` 所在类返回的 `metadata().id` 一致、全局唯一；启用方式是在 `config.json` 的 `plugins` 数组加 `{ "id": "myplugin", "position": "right", "order": 0, "config": {} }`。

## CMake 模板

照抄 `plugins/clock/CMakeLists.txt`，关键点一个都不能少：

```cmake
add_library(myplugin MODULE
    MyPlugin.h MyPlugin.cpp
    MyViewModel.h MyViewModel.cpp
)
set_target_properties(myplugin PROPERTIES AUTOMOC ON)

target_link_libraries(myplugin PRIVATE barplugin Qt6::Core)
target_include_directories(myplugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 产出 myplugin.dll（无 lib 前缀），与 QML/元数据同目录
set_target_properties(myplugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME myplugin
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/myplugin"
)

# VS 生成器下 MODULE 的 .dll 不走 RUNTIME_OUTPUT_DIRECTORY，POST_BUILD 是兜底，必须保留
add_custom_command(TARGET myplugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:myplugin>" "${XUANHUB_BIN_DIR}/plugins/myplugin/myplugin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MyPlugin.qml" "${XUANHUB_BIN_DIR}/plugins/myplugin/MyPlugin.qml"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" "${XUANHUB_BIN_DIR}/plugins/myplugin/metadata.json"
)
```

然后在顶层 `CMakeLists.txt` 加 `add_subdirectory(plugins/myplugin)`。需要系统库就在 `target_link_libraries` 里加（如网络类插件链接 `iphlpapi ws2_32`，参考 `plugins/sysinfo/`）。

## QML 运行环境

插件 QML 与宿主**同一 QML 引擎**，可直接访问宿主根上下文的 context property：

- `themeManager`：`themeManager.colors` 是 11 字段色板 QVariantMap——`bg / panel / fg / muted / accent / danger / success / warning / hover / border / onAccent`，另有 `themeId`、`mode`。配色一律从这里取，不要在插件里硬编码色板；
- `barConfig`：栏配置（高度等）；
- `pluginModel`：插件列表模型（一般插件用不到）。

注意：

- **不用 `QML_ELEMENT` 静态注册**——外部 DLL 无法在宿主编译期注册类型，`Loader + source URL` 是既定方案；
- 插件 QML 以**裸文件**放 DLL 同目录（不进 qrc），改样式不用重编 DLL，重新构建（POST_BUILD 拷贝）后重启即生效；
- QML 加载失败时槽位会绘制红色占位符，真实错误在 `xuanhub.log` 的 QML 引擎 qWarning 里；
- 插件根 Item 的宽度自定（决定它在栏上占多宽），**高度被槽位强制为栏高**。

## 线程模型（铁律）

后台采集（网络、系统指标等耗时操作）必须用 Qt 惯用法，参照 `plugins/sysinfo/SysInfoWorker.{h,cpp}`：

- `QObject` 派生 worker + `moveToThread` + `QTimer` 驱动；**禁用** `std::thread` / `QThreadPool`；
- worker 只算数和 `emit` 信号，**绝不触碰**任何 ViewModel / QML 对象；跨线程靠 `Qt::AutoConnection` 自动排队到 GUI 线程，由 ViewModel 的 slot 更新 `Q_PROPERTY`；
- `shutdown()` 必须先 `workerThread->quit(); workerThread->wait();` 再销毁对象。

## 插件设置声明（settings / settingsQml）

`metadata.json` 里可声明该插件在设置窗口中的设置区（可选）：

- `settings`：分组 → 字段两级的 schema，框架自动生成表单。字段类型：`bool` / `int`（min/max/step）/ `double` / `string` / `color` / `enum`（options）/ `json`；完整契约见 `plugins_py/README.md` 的「设置项声明」一节（两种插件共用同一 schema）；
- `settingsQml`：声明插件自绘设置页（相对 DLL 目录的 QML 文件），适合列表/表格等复杂设置；与 `settings` 并存时**自绘优先**。示例见 `plugins/sysinfo/SysInfoSettings.qml`：根 Item 声明 `property var plugin`，注入的是 config 草稿桥，经 `plugin.value(key)` / `plugin.setValue(key, v)` 读写草稿，保存/取消由设置窗口统一。

编辑结果写入 `config.json` 的本插件 config 段，保存后热重载生效——即插件会在 `init()` 里拿到新值。

## EventBus（插件间通信）

插件间不直接互调，走 `ctx->eventBus`（`xuanhub::EventBus`，QObject）：

```cpp
// 订阅：slot 形如 onPayload(QVariantMap)，接收者以 QPointer 持有防悬空。
// 注意传裸签名串而非 SLOT() 宏（投递走 QMetaObject::invokeMethod，不接受 "1" 前缀）
ctx->eventBus->subscribe(QStringLiteral("theme.changed"), this, "onPayload(QVariantMap)");
// 退订（shutdown 时）
ctx->eventBus->unsubscribe(QStringLiteral("theme.changed"), this);
// 发布
ctx->eventBus->publish(QStringLiteral("my.topic"), QVariantMap{{"key", 1}});
```

现有 topic：`theme.changed`（主题切换时发布，payload 含 `themeId` 与 `mode`）。

## ABI 警告（重要）

插件 DLL 与宿主存在严格的 ABI 锁定：**同 Qt 版本（6.9.3）、同编译器（MSVC 2022 x64）、同 CRT（Release `/MD` / Debug `/MDd`）**，否则 `instance()` 返回 nullptr 或直接崩溃。当前没有独立的插件 SDK 分发，第三方 C++ 插件的实际开发路径是 **clone 本仓库、在 `plugins/` 下加子目录、用同一构建树整体编译**（Qt 路径见 `CMakePresets.json`，可用 `CMakeUserPresets.json` 覆盖）。

---

# Python 插件开发（pybridge）

`pybridge` 是一个特殊的桥接插件：内嵌 CPython 解释器（稳定 ABI，兼容 3.12+），扫描 `<exeDir>/plugins_py/` 下的子插件，为每个子插件创建通用代理 ViewModel 注入 QML。**写 Python 插件只需三个文本文件，零 C++、零编译。**

完整开发契约见 `plugins_py/README.md`，参考实现见 `plugins_py/hello/`。以下为要点。

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
- 行为：`plugin.call(name, args)` → `main.py` 的 `invoke(name, args)`。

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

`urllib`（含 HTTPS）开箱即用——运行时的解释器指向用户自装的 Python（≥3.12），其完整标准库与编译扩展模块（`_socket`、`_ssl` 等）均可用。第三方库（如 `requests`）装进该 Python 即可直接 import（其 `site-packages` 对插件可见）。

## 调试

- 日志：`%APPDATA%/XuanHub/xuanhub.log`（Python traceback 也在其中）；
- 改 `main.py` / `*.qml` 后重新构建（POST_BUILD 会把 `plugins_py/` 同步到运行目录）再重启宿主即生效，免重编任何 DLL；
- 已知限制：Python 子插件暂无 EventBus 接入；运行依赖用户自装的 CPython ≥3.12（未安装时自动停用 + 托盘提示）。

---

# 从源码构建

前提：Qt 6.9.3（msvc2022_64）、MSVC 2022、CMake ≥ 3.21。Qt 安装路径不同就改 `CMakePresets.json` 或新建 `CMakeUserPresets.json` 覆盖 `CMAKE_PREFIX_PATH`。克隆后先 `git submodule update --init --recursive`（拉取 nanobind 及嵌套的 ext/robin_map）。

```bash
cmake --preset default                 # 配置（生成到 build/default）
cmake --build --preset default         # Release
cmake --build --preset default-debug   # Debug
```

运行产物统一在 `build/default/dist/<CONFIG>/`（含 `XuanHub.exe`、插件目录、Qt 依赖，链接后自动 windeployqt）。仓库根的 `config/config.json` 是开发示例，需手动拷到该目录才会以便携模式生效。pybridge（Python 桥接）为可选组件：构建需本机装有 Python ≥3.12 开发包与已初始化的 nanobind 子模块，缺失时 configure 告警并跳过该插件，其余功能照常构建。

## 仓库目录速览

```
app/            # 宿主 XuanHub.exe（栏窗口、AppBar、配置、设置窗口、托盘、主题）
lib/barplugin/  # 插件接口共享库（IBarPlugin / BarPluginContext / EventBus）
plugins/        # C++ 插件：clock / sysinfo / pybridge
plugins_py/     # Python 子插件（README.md 即开发契约）
themes/         # 主题包（README.md 即开发契约）
config/         # 开发用示例配置
third_party/    # nanobind git submodule（含嵌套的 ext/robin_map）
AGENTS.md       # 面向 AI 代理的项目说明（架构细节最齐全的一份文档）
```
