# XuanHub 插件框架升级路线图

> 基线版本：0.2.1
> 文档状态：草案，等待按里程碑逐项批准实施
> 最后更新：2026-08-22

本文档是插件框架后续升级的唯一总路线图，用于管理 C++ 插件、Python 子插件、QML
界面、设置系统、跨插件通信、分发安装和开发工具的演进。每个大型方向均拆成可独立评审、
实现、测试和回滚的完整计划；实施时不得把多个尚未稳定的基础设施方向合并到同一个大改中。

## 1. 使用方式

### 1.1 状态约定

| 状态 | 含义 |
|---|---|
| `待评审` | 方向已记录，但接口和范围尚未批准 |
| `已批准` | 设计已确认，可以拆任务实施 |
| `进行中` | 已有实现分支或提交 |
| `阻塞` | 存在明确外部依赖或未决设计 |
| `已完成` | 代码、测试、文档和迁移全部通过 |

### 1.2 更新规则

1. 开始一个方向前，先把该方向状态改为 `已批准`，确定目标版本和非目标。
2. 每个实施阶段单独提交；基础接口、宿主实现、插件迁移和文档不能混成不可回滚的大提交。
3. 修改公共接口时必须写兼容方案，不允许直接破坏 `IBarPlugin/1.0` 或现有 Python 契约。
4. 每完成一个阶段，更新本文件的检查项、已知风险和实际验证结果。
5. 只有“完成定义”全部满足，方向状态才能改为 `已完成`。
6. 新发现的问题优先归入现有方向；确实不属于任何方向时再新增 `PF-xx` 编号。

## 2. 当前基线

0.2.1 已具备以下插件框架能力：

- C++ DLL 插件统一通过 `IBarPlugin` 返回 ViewModel 与 QML 文件。
- `IBarPluginHost` 可以承载子插件；pybridge 已用它承载 Python 插件。
- 状态栏提供左、中、右三区域、顺序、自动宽度和固定宽度。
- C++ 插件可使用 EventBus；Python 插件按插件独占进程运行。
- Python 支持 `init/update/invoke/stop/set_locale` 生命周期。
- 设置窗口支持通用 schema、自绘 `settingsQml`、草稿保护和 DPAPI 密钥库。
- 宿主、插件和设置窗口支持中文、英文和运行时热切换。
- 插件可跟随主题，并能使用独立 QML `Window` 实现弹窗。
- 配置文件支持热重载；插件段未变化时不会销毁插件状态。
- Python Worker 具备 loopback IPC、随机令牌、超时、崩溃重启和进程回收。

当前基础能够支撑内置插件，但距离稳定的第三方插件平台仍有以下结构性缺口：

- 插件配置或布局变化会导致全部插件重载。
- Python `intervalMs` 文档契约尚未真正生效。
- C++ ABI、宿主版本和插件能力缺少加载前协商。
- EventBus 的卸载安全依赖插件自行销毁订阅对象，Python 也没有总线入口。
- 插件没有统一健康状态、熔断、标准通知、存储和弹窗服务。
- 嵌套密钥存在针对特定插件的宿主硬编码。
- 没有插件包格式、安装更新、签名验证和开发者校验工具。
- C++ 插件仍在宿主进程内运行，崩溃和卡死会直接影响 XuanHub。

## 3. 总体原则

### 3.1 兼容优先

- 保留 `IBarPlugin/1.0`，通过新增接口而不是原地修改虚函数表实现升级。
- Python V1 契约继续可用；新协议必须通过握手协商，旧插件无需修改即可运行。
- 配置升级必须保留未知字段，并提供向前迁移和失败回滚。
- 中文继续作为界面源码回退语言，现有语言包上下文不随意改名。

### 3.2 安全边界明确

- 进程隔离用于崩溃隔离，不得宣传为权限沙箱。
- C++ 进程内插件默认视为“受信任原生插件”。
- Python 或未来隔离后端按能力 API 与宿主交互，减少直接访问宿主内部对象。
- 所有插件路径、Manifest、密钥引用和包内容在执行前验证。

### 3.3 宿主提供机制，插件提供内容

- 弹窗定位、关闭、焦点和屏幕边界由宿主管理。
- 密钥、日志、通知、存储和设置跳转由统一服务管理。
- 插件只声明需要的动作、设置、权限和内容，不重复实现宿主级机制。

### 3.4 可观测、可停用、可恢复

- 每个插件都必须有明确运行状态和最近错误。
- 单插件故障不得迫使其他插件重启。
- 自动重启必须有熔断上限，并允许用户手动恢复。
- 加载、配置迁移、IPC 和卸载问题必须能从设置窗口和日志定位。

## 4. 路线图总览

| 编号 | 大型方向 | 优先级 | 目标版本 | 状态 | 主要依赖 |
|---|---|---:|---|---|---|
| PF-01 | 运行时健康、熔断与卸载安全 | P0 | 0.2.2 | 进行中 | 无 |
| PF-02 | 插件级增量生命周期与热重载 | P0 | 0.2.2 | 待评审 | PF-01 |
| PF-03 | SDK V2、Manifest 与兼容性协商 | P1 | 0.3.0 | 待评审 | PF-01 |
| PF-04 | HostServices、权限与安全边界 | P1 | 0.3.0 | 待评审 | PF-03 |
| PF-05 | EventBus 2.0 与跨运行时通信 | P1 | 0.3.0 | 待评审 | PF-03、PF-04 |
| PF-06 | 配置 Schema、迁移、校验与通用密钥 | P1 | 0.3.0 | 待评审 | PF-03、PF-04 |
| PF-07 | 标准弹窗、动作扩展与响应式布局 | P1 | 0.4.0 | 待评审 | PF-04 |
| PF-08 | Python Bridge Protocol V2 | P1 | 0.4.0 | 待评审 | PF-01、PF-05 |
| PF-09 | 插件包、安装、更新、签名与回滚 | P2 | 0.4.0 | 待评审 | PF-03、PF-06 |
| PF-10 | 开发者 CLI、测试工具与运行时观测 | P1 | 持续交付 | 待评审 | 各方向逐步接入 |
| PF-11 | 原生后端进程隔离 | P2 | 1.0 前评估 | 待评审 | PF-03、PF-04、PF-05 |

---

## PF-01：运行时健康、熔断与卸载安全

**优先级：** P0
**目标版本：** 0.2.2
**状态：** 进行中

### 目标

建立统一插件健康状态和故障边界，修复 Python 刷新周期，避免无限崩溃重启，并让宿主在
卸载插件时主动清理跨 DLL 订阅关系，不再把稳定性寄托于插件作者完全遵守生命周期规范。

### 当前问题

1. `PyWorkerViewModel::scheduleUpdate()` 未读取 `config.intervalMs`，一次 `update()` 完成后会
   立即安排下一次更新。
2. Python Worker 崩溃后按指数退避重启，但到 30 秒后会永久重复，没有熔断。
3. 插件模型只有“已加载”或“不存在”，没有启动中、异常、重启中、已熔断等状态。
4. EventBus 订阅对象若未随 DLL 一起销毁，后续发布可能调用已经卸载的机器码。
5. C++ 插件的同步 `init()` 和 `shutdown()` 没有耗时记录，卡顿只能从用户体感发现。

### 范围

- 修复 Python 刷新调度。
- 增加统一健康状态、错误详情、重启次数和最近成功时间。
- 增加 Python 崩溃熔断和手动恢复。
- 为 EventBus 增加按插件所有者清理的订阅作用域。
- 记录 C++ 插件初始化、关闭和 QML 加载耗时。

### 非目标

- 本阶段不把 C++ 插件移出宿主进程。
- 不重写 EventBus 的 Topic 和请求响应协议，完整升级放在 PF-05。
- 不实现完整性能分析器，只提供最小健康指标。

### 设计

统一状态枚举：

```text
Discovered -> Starting -> Running
                    |-> Degraded -> Restarting -> Running
                    |-> Failed
                    |-> CircuitOpen
Disabled
Stopping -> Stopped
```

每个插件记录：

```text
state
lastError
lastErrorAt
startedAt
lastHealthyAt
restartCount
consecutiveFailures
loadDurationMs
queueDepth（Python）
```

Python 刷新规则：

- `intervalMs` 缺省 1000ms，钳制到 100–60000ms。
- 从一次 `update()` 完成时开始计算下一周期，避免慢请求产生积压。
- `init/invoke/set_locale` 完成后可立即安排一次更新。
- 队列中最多存在一个待执行 `update`。
- 连续成功一次 `update` 后清零连续失败计数。

熔断规则初始建议：

- 10 分钟内连续失败 5 次则进入 `CircuitOpen`。
- 用户手动重启或配置发生变化后解除熔断。
- 熔断状态不再自动启动 Worker，但保留最后一次成功数据。

EventBus 作用域：

```cpp
class PluginEventScope {
public:
    Subscription subscribe(...);
    void clear();
};
```

宿主为每个插件创建作用域，`shutdown()` 前先禁止新增订阅，DLL unload 前强制 `clear()`。
V1 插件仍可通过原始 EventBus 工作，但宿主应逐步把内置插件迁移到作用域 API。

### 实施阶段

#### 阶段 A：刷新与熔断

- [x] 在 `PyWorkerViewModel` 解析并保存 `intervalMs`。
- [x] 修改更新定时策略并增加边界测试。
- [x] 增加故障时间窗口和熔断状态。
- [x] 增加 `restart()` 可调用入口。
- [x] 验证 UI 保留最后成功值。

#### 阶段 B：健康模型

- [x] 新增 `PluginHealth` 数据结构和模型角色。
- [x] `PluginManager`、pybridge 和 `PluginSlot` 上报状态。
- [x] 设置页显示状态、最近错误和重新启动按钮。
- [x] 红色占位符提供可读错误摘要，不只显示 ID。

#### 阶段 C：卸载安全

- [x] EventBus 订阅增加所有者 Token 或 Scope。
- [x] `PluginManager::shutdownAll()` 在 unload 前清除作用域。
- [x] 为泄漏 QObject 的测试插件增加回归测试。
- [x] 确认 locale/theme 事件在热重载后不会投递给旧对象。

### 主要修改点

- `plugins/pybridge/PyWorkerViewModel.{h,cpp}`
- `app/src/PluginManager.{h,cpp}`
- `app/src/PluginListModel.{h,cpp}`
- `app/qml/PluginSlot.qml`
- `lib/barplugin/include/barplugin/EventBus.h`
- `lib/barplugin/src/EventBus.cpp`
- `app/qml/SettingsWindow.qml`

### 测试与验收

- `intervalMs=100/1000/60000` 的调用间隔符合容差，慢更新不积压。
- Worker 连续崩溃达到阈值后停止重启，手动恢复成功。
- Worker 重启期间 QML 不访问悬空对象，保留旧值。
- 插件泄漏订阅对象后卸载，再发布事件不崩溃、不调用旧槽。
- 设置窗口能显示真实错误并只重启目标插件。
- Release 构建、CTest 和实际 appbar/taskbar 启动均通过。

### 风险与回滚

- 健康模型会触及 PluginManager、模型和 QML 三层，应先加入数据但不改变现有加载流程。
- 熔断阈值过低会误停偶发网络错误；只把 Worker/IPC 级失败计入熔断，普通业务错误不计入。
- EventBus V1 兼容层必须保留，出现回归可先关闭自动作用域、恢复现有订阅方式。

### 完成定义

- [ ] 所有测试与人工验收通过。
- [x] Python 刷新契约与文档一致。
- [x] 设置页能观察和恢复单插件故障。
- [x] EventBus 卸载回归测试稳定通过。
- [x] `plugins_py/README.md` 和 `AGENTS.md` 已更新。

---

## PF-02：插件级增量生命周期与热重载

**优先级：** P0
**目标版本：** 0.2.2
**状态：** 待评审

### 目标

把当前“插件段变化就全部卸载重载”改成按插件 ID 计算差异，只重建真正受影响的实例；
位置、顺序和宽度变化不得摧毁插件状态。

### 当前问题

插件指纹包含 `id/position/order/width/config`。任一字段变化都会清空整个模型、关闭所有
弹窗、终止全部 Python Worker、卸载所有 C++ DLL，再从头加载。待办、计时器、网络连接
和 BLE 会话都会丢失。

### 范围

- 区分布局变化、配置变化、启用/禁用和文件强制重载。
- PluginManager 支持按 ID 加载、停止、重启。
- PluginListModel 支持增删改，不再总是 reset 整个模型。
- V2 插件支持运行时 `applyConfig()`；V1 插件仅重启自己。
- pybridge 只重启目标 Python 子插件，不销毁全部 Worker。

### 非目标

- 不承诺在进程运行期间替换正在使用的 Qt/宿主 ABI DLL。
- QML 文件缓存清理与开发态版本化 URL可以后续单独处理。
- 不在本阶段做插件自动更新。

### 差异分类

| 变化 | 处理方式 |
|---|---|
| `position/order/width` | 只更新布局模型 |
| 插件 config | V2 调 `applyConfig`；V1 只重启该插件 |
| 新增并启用 | 加载目标插件 |
| 禁用或删除 | 关闭并卸载目标插件 |
| DLL/主 QML 更新 | 强制重启目标插件 |
| pybridge 总开关变化 | 只影响 Python 子插件集合 |
| 主题/语言 | 保持现有热切换，不重启插件 |

### 设计

引入运行实例表：

```cpp
QHash<QString, PluginRuntime> m_runtimes;

struct PluginRuntime {
    PluginDescriptor descriptor;
    LoadedPlugin loaded;
    PluginHealth health;
    QString configFingerprint;
    QString binaryFingerprint;
};
```

配置重载生成 `PluginChangeSet`：

```text
added
removed
layoutChanged
configChanged
binaryReloadRequested
unchanged
```

应用顺序固定为：

1. 从模型移除即将停止的实例。
2. 处理延迟删除。
3. 停止 removed/restart 集合。
4. 加载 added/restart 集合。
5. 对 layoutChanged 更新角色。
6. 一次性提交模型变化并重新计算 bar 尺寸。

### 实施阶段

#### 阶段 A：差异计算

- [ ] 抽出稳定的插件配置指纹工具。
- [ ] 新增 `PluginChangeSet` 单元测试。
- [ ] 区分布局字段和后端配置字段。

#### 阶段 B：PluginManager 单实例操作

- [ ] 增加 `loadOne/stopOne/restartOne`。
- [ ] 保留 `shutdownAll` 作为退出和灾难回退路径。
- [ ] host 子插件增加按 ID 创建和销毁能力。
- [ ] pybridge 不再只通过整体 `shutdown()` 管理全部 VM。

#### 阶段 C：模型增量更新

- [ ] PluginListModel 支持按 ID 插入、移除和角色更新。
- [ ] 位置变化正确地在三组 FilterModel 间移动。
- [ ] QML delegate 只销毁受影响插件。

#### 阶段 D：可选热配置接口

- [ ] SDK V2 定义 `validateConfig/applyConfig`。
- [ ] 内置 clock/sysinfo 迁移并验证无状态丢失。
- [ ] Python V2 增加 `set_config`，旧插件继续单实例重启。

### 主要修改点

- `app/src/ConfigHotReloader.{h,cpp}`
- `app/src/PluginManager.{h,cpp}`
- `app/src/PluginListModel.{h,cpp}`
- `app/src/PluginFilterModel.{h,cpp}`
- `plugins/pybridge/PyBridgePlugin.{h,cpp}`
- `plugins/pybridge/PyWorkerViewModel.{h,cpp}`

### 测试与验收

- 只改 clock 宽度时，clock 对象和其他插件对象地址均不变。
- 只改一个 Python 插件配置时，其他 Worker PID 不变。
- 禁用一个 C++ 插件不会关闭其他插件弹窗。
- 左/中/右移动后顺序正确，content width 记录不串插件。
- 目标插件加载失败时，其他插件继续运行，配置仍可再次修复。
- 强制重载仍可作为完整兜底流程使用。

### 风险与回滚

- 模型增量操作容易出现索引和 FilterModel 顺序问题；必须先完成差异和模型测试。
- DLL 单独卸载仍要求严格清空 QML 与 EventBus 引用，依赖 PF-01。
- 若增量应用中途失败，保留“目标插件失败、其他插件不回滚”的简单语义，避免复杂事务。

### 完成定义

- [ ] 布局变化不销毁运行对象。
- [ ] 配置变化最多重启目标插件。
- [ ] host 子插件可独立启停。
- [ ] 热重载回归覆盖 C++、Python、弹窗和三栏布局。
- [ ] 原有全量退出顺序仍通过测试。

---

## PF-03：SDK V2、Manifest 与兼容性协商

**优先级：** P1
**目标版本：** 0.3.0
**状态：** 待评审

### 目标

为第三方插件建立可演进、可预检、可兼容的 SDK 和 Manifest，避免只能通过加载 DLL 后的
崩溃或日志判断 ABI 是否匹配。

### 当前问题

- `PluginMetadata` 只有 ID、名称、版本和作者。
- IID 只能区分 `1.0`，不能表达最小宿主版本、能力或依赖。
- C++ 插件暴露 Qt C++ 类型，必须匹配 Qt、MSVC、架构和 CRT。
- Python `py_` 前缀只是文档约定，代码没有强制。
- sidecar 元数据与插件运行时返回的元数据没有一致性校验。

### 范围

- 定义统一 `PluginDescriptor` Manifest。
- 新增 `IBarPlugin2`，保持 V1 兼容。
- 加载代码前检查 ID、版本、宿主范围、架构和 SDK。
- 支持插件依赖和可选能力声明。
- 收紧 QML、设置页、i18n 和资源路径边界。

### 非目标

- 本阶段不解决 Qt C++ ABI 的根本稳定性问题。
- 不实现在线市场和自动下载依赖。
- 权限的运行时执行放在 PF-04。

### Manifest 草案

```json
{
  "id": "plugin.demo",
  "name": "示例插件",
  "version": "1.2.0",
  "apiVersion": 2,
  "minHostVersion": "0.3.0",
  "maxHostVersion": "0.x",
  "author": "Example",
  "entry": {
    "backend": "demo.dll",
    "qml": "Demo.qml",
    "settingsQml": "DemoSettings.qml"
  },
  "capabilities": ["barItem", "settings", "popup"],
  "permissions": ["notifications"],
  "dependencies": [
    {"id": "plugin.base", "version": ">=1.0.0", "optional": false}
  ]
}
```

ID 规则建议：

- 内置 C++：`clock` 等历史 ID 保持兼容。
- 新第三方：反向域名或 `vendor.plugin`。
- Python 历史插件继续接受 `py_*`；新插件推荐统一全局 ID 规则。
- 只允许 ASCII 字母、数字、点、下划线和短横线，长度不超过 128。

### SDK V2 草案

```cpp
class IBarPlugin2 {
public:
    virtual PluginDescriptor descriptor() const = 0;
    virtual InitResult initialize(const PluginContext2& context) = 0;
    virtual ConfigResult validateConfig(const QVariantMap& config) const = 0;
    virtual ApplyResult applyConfig(const QVariantMap& config) = 0;
    virtual QObject* viewModel() = 0;
    virtual QUrl qmlUrl() const = 0;
    virtual void shutdown() = 0;
};
```

宿主按 V2、V1 顺序探测；V1 包装成内部统一 Runtime，不要求旧插件重新编译。

### 实施阶段

#### 阶段 A：Manifest 解析与校验

- [ ] 新增 `PluginDescriptor` 和语义版本工具。
- [ ] 校验必填字段、ID、版本、宿主范围和路径。
- [ ] canonical path 必须处于插件目录内。
- [ ] sidecar 解析失败时不实例化 DLL、不执行 Python。

#### 阶段 B：V2 接口和适配器

- [ ] 新增独立 IID `xuanhub.BarPlugin/2.0`。
- [ ] 实现 V1 到内部 Runtime 的适配器。
- [ ] 迁移一个简单内置插件作为参考。
- [ ] 验证 V1/V2 混合加载和卸载。

#### 阶段 C：依赖与能力

- [ ] 构建插件依赖图和循环检测。
- [ ] 缺少必需依赖时显示明确状态。
- [ ] 可选依赖通过 Context 查询，不允许插件直接定位其他 DLL 对象。
- [ ] capability 用于 UI 和服务注入，不作为装饰字段。

#### 阶段 D：SDK 发布

- [ ] 提供 CMake 模板、固定编译器和 Qt 版本说明。
- [ ] 输出 `XuanHubPluginConfig.cmake` 或独立 SDK 包。
- [ ] 编写 V1 到 V2 迁移文档。

### 测试与验收

- 旧 V1 插件不修改即可加载。
- 宿主版本不满足时在加载 DLL 前拒绝并显示原因。
- `../outside.qml`、绝对越界路径和 junction 越界被拒绝。
- 重复 ID、循环依赖、缺少依赖和非法版本均有确定错误。
- V1/V2 插件可同时启用、热重载和退出。

### 风险与回滚

- 不得修改 V1 类的虚函数顺序或现有 IID。
- Manifest 双来源会产生漂移；V2 以 sidecar 为发现权威，运行时 descriptor 必须完全匹配。
- 语义版本解析先支持明确子集，避免一次引入过度复杂表达式。

### 完成定义

- [ ] Descriptor schema、解析器和校验器稳定。
- [ ] V1 兼容测试通过。
- [ ] 至少 clock 和一个第三方示例迁移到 V2。
- [ ] SDK 模板可在干净环境构建。
- [ ] ABI 和兼容错误能在设置页直接看到。

---

## PF-04：HostServices、权限与安全边界

**优先级：** P1
**目标版本：** 0.3.0
**状态：** 待评审

### 目标

把日志、通知、存储、密钥、设置跳转、弹窗和系统操作收敛为版本化宿主服务，减少插件直接
依赖全局 QML Context 或自行访问宿主内部对象，并为未来隔离插件建立权限边界。

### 当前问题

- `BarPluginContext` 只有 EventBus、configDir 和 pluginConfig。
- QML 插件隐式读取 `themeManager/barWindow/barScreenGeometry` 等全局对象。
- Python 无法使用托盘通知、宿主存储和设置跳转。
- 插件私有数据与共享配置没有正式目录契约。
- C++ 插件在宿主进程内，权限声明无法真正阻止直接 Win32/文件访问。

### 范围

- 定义版本化 `HostServices`。
- 提供每插件作用域的日志、通知、存储、密钥和导航服务。
- 为 QML 注入 `pluginContext`，逐步替代隐式全局。
- 定义权限声明、授权状态和拒绝语义。
- 明确“受信任原生插件”和“隔离插件”两种安全等级。

### 非目标

- 不承诺限制进程内 C++ 插件的操作系统权限。
- 不在本阶段实现 AppContainer。
- 不把业务 HTTP API 封装成宿主特定服务。

### 服务草案

```text
logger.debug/info/warning/error
notifications.show
storage.read/write/remove/list
secrets.read/write/remove
settings.open(pluginId)
shell.openUrl/openFile
locale.activeLocale
theme.currentPalette
popup.open/close
events.publish/subscribe/request
```

存储目录建议：

```text
<configDir>/plugin-data/<plugin-id>/
```

便携模式继续跟随 exe 同目录数据根。服务必须清理文件名和相对路径，拒绝离开插件私有目录。

权限草案：

```json
"permissions": [
  "notifications",
  "storage.private",
  "secrets.own",
  "shell.openUrl",
  "events.publish:plugin.demo.*"
]
```

### 实施阶段

#### 阶段 A：服务接口和作用域

- [ ] 定义 `IHostServices` 及各最小子接口。
- [ ] Context 只提供本插件绑定后的服务代理。
- [ ] 日志自动带插件 ID，不允许伪造其他插件来源。
- [ ] 存储和密钥服务自动限定插件命名空间。

#### 阶段 B：QML pluginContext

- [ ] `PluginSlot` 注入 `pluginContext`。
- [ ] 提供主题、语言、屏幕、弹窗和服务属性。
- [ ] 迁移内置插件，保留旧全局属性一整个兼容周期。
- [ ] 增加缺失权限时的确定错误。

#### 阶段 C：Python RPC 服务

- [ ] Worker 协议支持调用允许的 HostServices。
- [ ] 请求带插件身份，不接受 Worker 自报其他 ID。
- [ ] 限制消息大小、并发、超时和结果类型。
- [ ] 日志和通知先作为试点能力。

#### 阶段 D：权限展示

- [ ] 设置页显示插件声明和实际授予权限。
- [ ] 安装阶段预览权限。
- [ ] 对进程内插件明确显示“受信任、权限仅供说明”。

### 测试与验收

- 两个插件不能读取彼此存储或密钥。
- `../`、绝对路径、junction 和非法文件名无法越界。
- 未声明通知权限的隔离插件请求被拒绝并记录日志。
- 旧插件在兼容期继续通过全局 Context 正常工作。
- Python 可以通过服务发送通知但不能伪造其他插件 ID。

### 风险与回滚

- 服务接口过大将形成新的不稳定 ABI；优先小接口、能力查询和版本号。
- QML 全局移除不能一次完成，应先增加 `pluginContext`，最后再弃用旧属性。
- 原生插件的权限是信任提示而非强制隔离，UI 和文档必须明确说明。

### 完成定义

- [ ] HostServices 有稳定版本和能力查询。
- [ ] 存储、密钥、日志、通知完成跨插件隔离测试。
- [ ] 内置 QML 插件迁移到 pluginContext。
- [ ] Python 至少接入日志和通知服务。
- [ ] 权限说明与实际安全边界一致。

---

## PF-05：EventBus 2.0 与跨运行时通信

**优先级：** P1
**目标版本：** 0.3.0
**状态：** 待评审

### 目标

在保留松耦合 Topic 模型的前提下，增加作用域订阅、请求响应、状态保留、Payload 版本和
Python 接入，使 EventBus 成为 C++、Python、宿主服务之间统一且可观测的通信层。

### 当前问题

- V1 只有 `publish/subscribe/unsubscribe`。
- 同线程订阅者可能同步阻塞发布者。
- 没有请求响应、超时、取消和相关 ID。
- `locale.request/changed` 等状态通过自定义双 Topic 模拟。
- Python 子插件无法使用 EventBus。
- Topic 没有所有者、命名校验和 Payload 版本。

### 范围

- 保留 V1 广播接口。
- 新增订阅句柄、插件作用域和异步投递策略。
- 新增 retained state 和 request/response。
- 新增 Topic 命名和 Payload 版本约定。
- 通过 Python Bridge 暴露受限总线能力。
- 提供调试追踪与慢订阅者日志。

### 非目标

- 不把 EventBus 变成可调用任意宿主对象的通用 RPC。
- 不允许跨进程传递 QObject、指针或自定义 C++ 类型。
- 不在本阶段提供跨机器或局域网事件通信。

### Topic 规范

```text
app.*                 宿主事件
service.<name>.*      宿主服务
plugin.<id>.*         插件拥有的事件
```

Payload 保留字段：

```json
{
  "schemaVersion": 1,
  "source": "plugin.demo",
  "timestamp": "ISO-8601",
  "data": {}
}
```

插件只提交 `data`，source 和时间由宿主填充，防止身份伪造。

### API 草案

```text
Subscription subscribe(topic, handler, options)
void publish(topic, payload)
RequestId request(topic, payload, timeout)
void respond(requestId, payload, error)
State getState(topic)
void setState(topic, payload)
```

初期仍在 GUI 线程按事件循环排队投递；不得为了异步而直接从任意线程调用插件 QObject。

### 实施阶段

#### 阶段 A：V1 安全层

- [ ] 完成 PF-01 的作用域订阅。
- [ ] 记录发布来源、订阅数量和槽耗时。
- [ ] 慢槽只告警，不自动切线程。

#### 阶段 B：Retained State

- [ ] 用 state 替代 locale/theme 的 request/changed 双 Topic 模式。
- [ ] 新订阅者可选择立即收到当前状态。
- [ ] 状态有所有者，所有者卸载后按策略保留或清除。

#### 阶段 C：请求响应

- [ ] 加入相关 ID、超时、取消和错误码。
- [ ] 同一请求只接受一个响应或明确的聚合策略。
- [ ] 响应者卸载时自动结束未完成请求。

#### 阶段 D：Python 接入

- [ ] Worker 握手声明 EventBus V2 能力。
- [ ] Python API 支持 publish/subscribe/request/respond。
- [ ] IPC 对事件做队列上限和背压。
- [ ] 插件停止时自动清除所有订阅和未完成请求。

### 测试与验收

- V1 订阅行为完全兼容。
- 新订阅者可立即收到当前 locale/theme state。
- 请求成功、超时、响应者卸载和调用者取消均有确定结果。
- Python 与 C++ 插件可以通过命名空间受控通信。
- 事件风暴不会无限增长内存；超过队列限制时有日志和丢弃策略。
- 设置页或调试日志能查看最近 Topic 与耗时，不记录敏感 Payload 明文。

### 风险与回滚

- EventBus 不应演变成任意 RPC 容器；复杂宿主操作继续走 HostServices。
- Payload 必须保持 JSON/QVariant 安全集合，避免跨 DLL 自定义类型 ABI。
- 默认异步会改变时序；V1 保持现有语义，V2 新接口明确排队语义。

### 完成定义

- [ ] V1/V2 并存测试通过。
- [ ] C++/Python 跨运行时示例通过。
- [ ] state 和 request/response 有完整超时及卸载测试。
- [ ] Topic 命名和权限规则写入 SDK 文档。

---

## PF-06：配置 Schema、迁移、校验与通用密钥

**优先级：** P1
**目标版本：** 0.3.0
**状态：** 待评审

### 目标

让第三方插件能够自行声明配置版本、迁移规则、复杂字段和敏感数据，不再由宿主针对内置
插件编写迁移或密钥特殊代码。

### 当前问题

- 设置 schema 适合简单字段，但复杂数组通常必须自绘。
- 内置旧中文枚举迁移由宿主硬编码。
- `py_usage_hub` 嵌套厂商密钥由 SettingsPluginModel 和 ConfigService 特判。
- 自绘设置页可写任意 JSON，缺少统一插件侧验证。
- 插件没有配置版本和迁移失败恢复机制。

### 范围

- 在插件 config 内增加独立配置版本。
- 支持迁移、验证、默认值补全和弃用字段。
- 扩展通用 schema 表达能力。
- 密钥字段支持嵌套对象和可重复数组。
- 保存失败保持原配置和原密钥，不产生半写入状态。

### 非目标

- 不把设置 Schema 发展成完整通用 UI 描述语言。
- 不允许插件迁移代码直接写配置文件或密钥库。
- 不自动翻译用户输入、服务端内容和其他个人数据。

### 配置结构草案

```json
{
  "id": "plugin.demo",
  "configVersion": 3,
  "config": {}
}
```

Schema 扩展：

```json
{
  "key": "accounts",
  "type": "array",
  "itemKey": "id",
  "items": {
    "type": "object",
    "fields": [
      {"key": "name", "type": "string"},
      {"key": "token", "type": "string", "secret": true}
    ]
  }
}
```

密钥引用必须由宿主按插件 ID、字段 JSON Pointer 和稳定 itemKey 生成；数组重排不能交换密钥。

### 迁移接口

Python：

```python
CONFIG_VERSION = 3

def migrate_config(old_version, config):
    return config

def validate_config(config):
    return {"ok": True, "errors": []}
```

C++ V2 提供等价接口。迁移在临时副本执行，全部成功后再原子保存。

### 实施阶段

#### 阶段 A：版本与通用迁移器

- [ ] 配置条目增加 `configVersion`，缺失视为 1。
- [ ] 加载时只生成迁移建议，不直接覆盖文件。
- [ ] 设置保存或明确迁移操作时原子写入新版本。
- [ ] 迁移失败保留旧配置并停用目标插件，不影响其他插件。

#### 阶段 B：Schema 扩展

- [ ] 支持 object/array、条件显示、条件启用、regex 和 multiline。
- [ ] 枚举继续使用稳定 value 与可翻译 label。
- [ ] 自绘页与通用 schema 共用同一个 ConfigDraftModel。
- [ ] 加入插件侧最终验证钩子。

#### 阶段 C：通用密钥路径

- [ ] 用 schema 遍历替代 `py_usage_hub` 特判。
- [ ] 密钥 ID 使用插件 ID + JSON Pointer + 稳定 itemKey。
- [ ] DPAPI 写入与 config 原子提交形成可回滚事务。
- [ ] 删除数组项时清理对应孤儿密钥。

#### 阶段 D：内置迁移

- [ ] countdown/todo/intime 从宿主硬编码迁移到插件自有规则。
- [ ] usage_hub 迁移到通用数组密钥。
- [ ] 删除宿主中仅服务单插件的特殊逻辑。

### 测试与验收

- 从多个历史配置版本逐级迁移到最新版。
- 迁移抛异常、返回非法 JSON 或断电模拟时原配置不损坏。
- 数组重排、删除、新增不会把 A 账号凭据交给 B。
- 解密失败不会把引用对象或旧密文传给插件。
- 自绘页与通用表单执行同一套最终验证。
- 未知配置字段继续保留。

### 风险与回滚

- 自动迁移具有数据写入风险；初期只在用户点击保存时提交。
- 插件迁移代码本身不可信，Python 迁移应在 Worker 内运行并设置超时。
- 密钥事务需要先写新密钥、再保存配置、失败时恢复或删除新写入项。

### 完成定义

- [ ] 宿主不再包含具体插件 ID 的配置迁移和密钥特判。
- [ ] Schema object/array 与嵌套密钥通过测试。
- [ ] 配置迁移可观察、可失败、可回滚。
- [ ] 内置插件全部迁移并保留旧配置兼容。

---

## PF-07：标准弹窗、动作扩展与响应式布局

**优先级：** P1
**目标版本：** 0.4.0
**状态：** 待评审

### 目标

把插件反复实现的弹窗定位、焦点和关闭逻辑收归宿主，并允许插件声明快捷动作、托盘动作
和更完整的布局偏好，降低 QML 插件开发难度和界面类崩溃概率。

### 当前问题

- 多个插件复制 `mapToGlobal + barScreenGeometry + 稳定后显现` 逻辑。
- 点击 bar 关闭、toggle 防重开、transient parent 等细节容易遗漏。
- 插件无法标准化地向托盘或 XuanHub 快捷菜单贡献动作。
- 布局只有区域、顺序和固定宽度，空间不足时只能裁剪。
- QML 加载失败只有简单占位符。

### 范围

- 提供宿主管理的 PopupHost/PopupService。
- 定义插件 Action 描述和统一调用。
- 扩展 min/max/flex/priority/overflow 布局。
- 提供标准错误、加载中和空状态组件。
- 保持插件自建 Window 作为高级兼容路径。

### 非目标

- 不在本阶段重写整个状态栏 QML 布局系统。
- 不禁止高级插件继续创建自己的 Window。
- 不引入多行状态栏或任意网格布局，先完成单行空间管理。

### Popup API 草案

```qml
PluginItem {
    popup: PluginPopup {
        preferredWidth: 420
        preferredHeight: content.implicitHeight
        contentItem: MyPopupContent {}
    }
}
```

宿主负责：

- 当前屏幕与任务栏/AppBar 边界。
- 顶部/底部状态栏锚点。
- DPI 和多屏坐标。
- 弹窗互斥、焦点、Esc、点击外部关闭。
- bar 按下与 toggle 防重开。
- 内容高度稳定后再显示。
- 主题、语言和无障碍属性。

### Action Manifest 草案

```json
"actions": [
  {
    "id": "refresh",
    "label": "立即刷新",
    "placements": ["quickMenu", "tray", "pluginMenu"],
    "icon": "refresh",
    "defaultShortcut": ""
  }
]
```

动作调用统一进入插件 Runtime，复用健康状态、权限和异步结果。

### 布局扩展草案

```json
"layout": {
  "region": "right",
  "minWidth": 48,
  "preferredWidth": 160,
  "maxWidth": 320,
  "flex": 0,
  "priority": 50,
  "overflow": "menu"
}
```

### 实施阶段

#### 阶段 A：PopupHost

- [ ] 从 usage_hub/todo/heartrate 提取共同定位状态机。
- [ ] 实现单插件和跨插件弹窗互斥策略。
- [ ] 加入多屏、顶部/底部、100–200% DPI 测试。
- [ ] 迁移一个简单插件和一个异步高度复杂插件。

#### 阶段 B：Action 系统

- [ ] 解析动作元数据并本地化 label。
- [ ] 接入 XuanHub 快捷菜单和托盘菜单。
- [ ] 动作可根据插件状态动态 enabled/checked。
- [ ] Python/C++ 使用统一调用结果。

#### 阶段 C：响应式布局

- [ ] 计算三区域可用宽度和插件优先级。
- [ ] 支持收缩、隐藏和 overflow 菜单。
- [ ] 设置窗口提供可理解的布局设置，不暴露复杂数值为默认路径。
- [ ] 固定宽兼容逻辑保持不变。

### 测试与验收

- appbar/taskbar、顶部/底部和多 DPI 下弹窗不越界。
- 同时点击两个插件不会出现重叠失焦窗口。
- 语言切换和主题切换不销毁弹窗内部状态。
- Action 在托盘、快捷菜单和插件菜单行为一致。
- 状态栏空间不足时按 priority 收缩或进入 overflow，不覆盖相邻插件。
- 旧插件自建 Window 继续可用。

### 风险与回滚

- QML Window 平台行为复杂，先迁移内置插件验证后再标记为 SDK 稳定能力。
- overflow 会改变现有布局预期，默认只在明确启用时生效。
- Action 不得直接暴露任意 QML 函数名，应通过 Runtime 验证声明的 ID。

### 完成定义

- [ ] 至少三个复杂内置弹窗迁移到 PopupHost。
- [ ] 快捷菜单和托盘动作扩展可用。
- [ ] 响应式布局通过多分辨率验收。
- [ ] 插件开发示例不再需要复制弹窗定位代码。

---

## PF-08：Python Bridge Protocol V2

**优先级：** P1
**目标版本：** 0.4.0
**状态：** 待评审

### 目标

在保持 Python 插件易开发和独立进程隔离的前提下，增加能力握手、主动数据推送、异步调用
结果、取消、HostServices 与 EventBus，并建立清晰的背压和依赖管理策略。

### 当前问题

- 数据只能由宿主周期性 `update()` 拉取。
- `plugin.call()` 只返回是否入队，无法得到成功、错误和结果。
- 同一插件请求完全串行，慢请求会阻塞显示更新。
- Worker 只支持固定操作集合，无法协商新能力。
- 第三方依赖只能依赖用户 Python 或手工打入 Bundle。
- IPC Token 位于命令行，主要防误连接而非同用户恶意进程。

### 范围

- 协议握手与版本协商。
- Worker 主动推送值变化和事件。
- 调用 ID、结果、错误、取消和进度。
- HostServices/EventBus V2 RPC。
- 队列上限、优先级和背压。
- Python 版本与依赖声明、doctor 检查。

### 非目标

- 不在后台静默联网安装 pip 包。
- 不允许 Python 直接获得 Qt/QML 对象。
- 不把 IPC 当作通用远程网络协议。

### 握手草案

```json
{
  "type": "hello",
  "protocol": 2,
  "pluginId": "plugin.demo",
  "capabilities": ["pushValues", "callResults", "events", "services"]
}
```

V1 Worker 继续走当前 request/response；宿主按握手选择适配器。

### 主动推送草案

```json
{
  "type": "event",
  "event": "values.patch",
  "sequence": 42,
  "data": {"heartRate": 82}
}
```

- patch 只包含变化字段。
- sequence 防止重启或乱序后旧值覆盖新值。
- 高频数据由宿主按帧率合并，QML 不跟随每个原始采样刷新。

### 调用结果草案

```qml
const id = plugin.call("refresh", [])

Connections {
    target: plugin
    function onCallFinished(id, ok, result, error) {}
}
```

协议支持 `call.progress` 和 `call.cancel`，旧 `queued` 返回语义保留。

### 实施阶段

#### 阶段 A：协议层重构

- [ ] 提取 FrameCodec、消息校验和协议版本。
- [ ] 增加握手、能力列表和兼容测试。
- [ ] IPC 限制继续保持 4 MiB、深度限制和安全 JSON 集合。

#### 阶段 B：调用结果与取消

- [ ] 每次 QML 调用返回 request ID。
- [ ] Worker 返回结果、稳定错误码和动态详情。
- [ ] 支持取消尚未开始的请求。
- [ ] 运行中 Python 代码无法强制安全取消时，明确返回“不支持中断”。

#### 阶段 C：主动推送

- [ ] Worker 提供线程安全 `emit_values/emit_event` API。
- [ ] 协议发送集中在单线程，后台线程不得直接写 Socket。
- [ ] 宿主合并高频 patch，并限制事件队列。
- [ ] heartrate 作为高频推送验证插件。

#### 阶段 D：服务和事件

- [ ] 接入 PF-04 HostServices。
- [ ] 接入 PF-05 EventBus。
- [ ] 权限和插件身份由宿主绑定。

#### 阶段 E：依赖检查

- [ ] Manifest 支持 Python 版本和包声明。
- [ ] doctor 检测缺失包、版本、架构和原生扩展兼容。
- [ ] 支持离线 wheel 或插件私有环境，但安装必须用户确认。

### 测试与验收

- V1 插件无需修改继续运行。
- BLE 高频数据通过 push 更新，CPU/IPC 明显低于紧循环轮询。
- 调用成功、业务错误、异常、超时、取消和 Worker 重启均返回确定状态。
- 事件队列达到上限时内存不持续增长，日志说明丢弃策略。
- 后台线程只操作 Python 状态和桥接发送队列，不触碰 QML。
- 缺少依赖时插件显示明确 doctor 建议，宿主不崩溃。

### 风险与回滚

- Worker 主动发送会打破严格一问一答模型，必须先重构消息分发状态机。
- Python 线程写 Socket 容易交错损坏帧，必须由桥接主线程统一发送。
- 高频 patch 应合并，不能把每个心率采样直接变成 QML 信号。

### 完成定义

- [ ] V1/V2 协议兼容测试完整。
- [ ] 主动推送、调用结果、取消和服务调用通过测试。
- [ ] heartrate 与 usage_hub 迁移验证真实场景。
- [ ] SDK 文档包含线程和背压范式。

---

## PF-09：插件包、安装、更新、签名与回滚

**优先级：** P2
**目标版本：** 0.4.0
**状态：** 待评审

### 目标

定义可验证的 `.xuanplugin` 包和本地插件管理流程，让用户无需手工复制目录即可安装、升级、
停用、卸载和回滚插件，并为未来插件仓库保留接口。

### 当前问题

- 插件目录本身就是安装结果，没有包清单和文件哈希。
- 没有安装事务、版本回滚和来源记录。
- 不能在安装前查看权限、依赖和宿主兼容性。
- 第三方 QML/Python 文件没有签名或完整性信息。
- 宿主打包脚本只面向内置插件，不是第三方发布工具。

### 范围

- 定义离线 `.xuanplugin` 包、清单、哈希和可选签名。
- 支持本地安装、升级、停用、卸载和单版本回滚。
- 保存来源、权限、发布者和安装时间等记录。
- 在本地包流程稳定后支持用户显式配置的更新索引。

### 非目标

- 不在本阶段建设官方公开插件市场、评分或支付系统。
- 不默认自动下载安装更新。
- 不允许包安装脚本执行任意代码；安装过程只做声明式文件操作。

### 包格式草案

```text
plugin.json
manifest.sha256
signature.json          可选
backend/
qml/
i18n/
assets/
python/
LICENSE
README.md
```

包使用 ZIP 容器但扩展名为 `.xuanplugin`。所有文件必须在 manifest 中，禁止绝对路径、`..`、
设备名、符号链接和解压炸弹。

### 安装记录

```json
{
  "id": "plugin.demo",
  "version": "1.2.0",
  "source": "local-file",
  "installedAt": "ISO-8601",
  "packageHash": "sha256:...",
  "publisher": "...",
  "previousVersion": "1.1.0"
}
```

### 实施阶段

#### 阶段 A：本地包校验器

- [ ] 定义包 schema 和哈希清单。
- [ ] 校验路径、大小、压缩比、文件数量和 Manifest。
- [ ] 复用 PF-03 兼容性和 PF-04 权限检查。
- [ ] 只解压到临时目录，全部通过后再安装。

#### 阶段 B：事务安装与回滚

- [ ] 安装到版本化 staging。
- [ ] 停止目标插件后原子切换当前版本。
- [ ] 失败恢复旧版本和旧配置。
- [ ] 卸载默认保留用户数据，另提供明确“删除数据”选项。

#### 阶段 C：签名和发布者

- [ ] 定义签名覆盖范围和公钥标识。
- [ ] 未签名包允许本地安装但明确警告。
- [ ] 签名无效必须拒绝，不能降级为普通警告。
- [ ] UI 显示来源、哈希、发布者和权限。

#### 阶段 D：更新源

- [ ] 先支持用户显式添加的静态索引 URL。
- [ ] 检查更新与安装更新分离，默认不自动执行。
- [ ] 下载后仍走同一离线包校验器。
- [ ] 保留至少一个可回滚版本。

### 测试与验收

- 路径穿越、超大文件、重复路径、符号链接和哈希不匹配被拒绝。
- 安装中断不会留下半安装插件。
- 更新失败能恢复旧插件和旧配置。
- 不兼容宿主版本、缺少依赖和权限变化在安装前显示。
- 签名包验证通过；被修改一个字节后验证失败。
- 便携模式和普通模式安装根目录正确。

### 风险与回滚

- 包安装涉及文件替换和 DLL 占用，必须依赖 PF-02 单插件停止。
- Windows 杀毒软件可能短暂锁定新文件，应使用 staging、重试和明确错误。
- 在线索引属于供应链高风险能力，必须晚于本地包校验和签名。

### 完成定义

- [ ] `.xuanplugin` 规范和参考打包器完成。
- [ ] 本地安装、升级、卸载和回滚通过故障注入测试。
- [ ] 签名与权限信息在 UI 可见。
- [ ] 在线更新不绕过离线安全门禁。

---

## PF-10：开发者 CLI、测试工具与运行时观测

**优先级：** P1
**目标版本：** 持续交付
**状态：** 待评审

### 目标

把目前依赖文档和真实宿主人工验证的开发流程变成可重复工具，为 C++、Python、QML、设置
和语言包提供统一创建、校验、测试、打包和诊断入口。

### 当前问题

- 项目测试已覆盖部分配置、语言和 Python Worker，但没有插件 SDK 测试框架。
- QML 根节点、宽度、属性注入和弹窗契约主要靠人工遵守。
- Manifest、Schema、i18n、权限和包安全没有统一校验命令。
- 用户遇到插件问题时只能查看整份日志。
- 没有插件级 CPU、内存、队列和最近成功时间视图。

### 范围

- 提供插件创建、静态校验、Worker 测试、诊断和打包工具。
- 建立内置插件 CI 门禁和机器可读输出。
- 在设置窗口展示统一健康信息并导出脱敏诊断。
- 为 QML 提供最小 Smoke Test，不取代实际宿主视觉验收。

### 非目标

- 不把 CLI 变成另一套独立于宿主的 Manifest/Schema 实现。
- 不承诺自动判断所有视觉正确性。
- 不默认持续采集高频性能数据或上传遥测。

### CLI 规划

```text
xuanhub-plugin create
xuanhub-plugin validate
xuanhub-plugin test
xuanhub-plugin run
xuanhub-plugin doctor
xuanhub-plugin package
xuanhub-plugin install
xuanhub-plugin inspect
```

### 校验内容

- Manifest schema、ID、版本和宿主范围。
- 文件路径边界和未声明文件。
- C++ DLL 架构、Qt、MSVC、CRT 和 IID。
- Python 版本、依赖和原生扩展架构。
- QML 根节点 `property var plugin`、正宽度和 import。
- settings schema、默认值、枚举稳定值和 secret 标记。
- zh-CN/en-US 目录、上下文隔离和翻译完整度。
- 凭据形状、日志、缓存、VCS 元数据和用户绝对路径。
- 插件包哈希、签名和权限。

### 测试层级

1. **静态校验：** 不执行插件代码。
2. **Python Worker 测试：** 独立启动 Worker，验证生命周期、超时和异常。
3. **QML Smoke Test：** 在测试引擎中加载根 Item，检查契约和基本绑定。
4. **宿主集成测试：** 临时配置启用目标插件，验证布局和健康状态。
5. **人工视觉验收：** 多 DPI、多屏、主题和语言。

### 运行时观测

设置页每个插件显示：

```text
状态
版本/API 版本
启动耗时
最近成功更新时间
最近错误
重启次数
Python PID
CPU/内存（可选采样）
队列深度
打开日志
重新启动
导出诊断
```

诊断导出必须默认脱敏，不包含 API Key、Cookie、完整用户路径或配置明文。

### 实施阶段

#### 阶段 A：Validator

- [ ] 先实现只读 `validate`，复用宿主 Manifest/Schema/i18n 解析器。
- [ ] 输出机器可读 JSON 和用户可读文本。
- [ ] CI 对所有内置插件执行。

#### 阶段 B：Python Test Harness

- [ ] 模拟宿主 IPC，运行 init/update/invoke/set_locale/stop。
- [ ] 支持超时、崩溃、异常、消息大小和重启测试。
- [ ] 插件作者无需 Qt/MSVC 即可运行。

#### 阶段 C：QML Smoke Test

- [ ] 注入最小 theme/locale/pluginContext 假对象。
- [ ] 检查加载错误、根属性和尺寸。
- [ ] 生成可选截图供人工对比，不把截图测试作为唯一判据。

#### 阶段 D：诊断和指标

- [ ] PF-01 健康数据进入设置页。
- [ ] 导出插件级脱敏诊断包。
- [ ] 日志支持插件 ID 过滤和稳定错误码。

#### 阶段 E：创建和打包

- [ ] `create` 生成 C++/Python 模板。
- [ ] `package` 接入 PF-09 包格式和安全门禁。
- [ ] 文档示例全部由模板生成或验证，避免漂移。

### 测试与验收

- 在无 Qt 环境中可以校验和测试纯 Python 插件。
- 故意构造的 Manifest、QML、i18n、secret 和凭据问题均能被定位。
- CLI JSON 输出稳定，可用于 CI。
- 诊断包通过独立敏感信息扫描。
- 内置插件全部接入 CI Validator。

### 风险与回滚

- CLI 不应复制一套解析逻辑；尽量复用库或共享 schema 文件。
- QML 静态检查不能替代真实宿主测试，输出需区分“静态通过”和“运行验证”。
- 性能采样默认低频关闭，避免观测本身影响状态栏。

### 完成定义

- [ ] create/validate/test/doctor/package 最小闭环可用。
- [ ] CI 覆盖全部内置插件。
- [ ] 设置页健康和诊断导出可用。
- [ ] 插件 SDK 文档中的命令都由自动化测试验证。

---

## PF-11：原生后端进程隔离

**优先级：** P2
**目标版本：** 1.0 前完成技术评估，不承诺立即全面迁移
**状态：** 待评审

### 目标

评估并实现可选的原生后端隔离模式，使不完全受信任或稳定性未知的 C++/Rust 等原生插件
不能因崩溃、ABI 冲突或死锁直接终止 XuanHub。

### 当前问题

- C++ DLL 与宿主共享进程、Qt、CRT、事件循环和地址空间。
- `init/shutdown` 卡死无法通过宿主内超时安全终止。
- 原生扩展越界、ODR/ABI 冲突和未处理异常会直接崩溃宿主。
- 权限声明对进程内代码无法强制执行。

### 方案候选

#### 方案 A：隔离后端 + 宿主 QML

- 插件包仍提供 QML，由宿主加载。
- 原生后端运行在独立 `XuanHubPluginHost.exe`。
- 数据、调用、EventBus 和 HostServices 复用 Protocol V2。
- 与 Python 插件形成统一隔离模型。

优点：UI 性能与现状接近，复用最多。
缺点：宿主仍需信任插件 QML；复杂 Qt 类型必须转 JSON。

#### 方案 B：完整插件窗口进程

- 插件进程自行渲染窗口或纹理，宿主嵌入结果。

优点：隔离更完整。
缺点：DPI、输入、透明合成、无障碍和主题同步复杂，不适合近期实施。

#### 方案 C：继续仅信任进程内 DLL

- 原生 DLL 只允许内置或签名受信任来源。
- 第三方默认使用 Python/隔离协议。

优点：近期成本最低。
缺点：第三方高性能原生插件能力受限。

初步推荐先实施 A，同时保留 C 作为受信任高性能路径，不采用 B。

### 范围

- 独立原生 PluginHost 进程。
- JSON 安全类型的 values/calls/events/services。
- 启动超时、请求超时、崩溃重启和熔断。
- 架构与协议握手。
- 可选 Job Object 和用户级进程限制。

### 非目标

- 不支持把任意现有 `IBarPlugin/1.0` DLL 无修改搬入隔离进程。
- 不传递 QObject、QImage 指针或自定义 C++ 对象。
- 不承诺恶意插件级强安全沙箱，除非未来引入 AppContainer 并完成专门审计。

### 实施阶段

#### 阶段 A：技术原型

- [ ] 用独立进程实现一个计数器插件。
- [ ] 复用 Python Protocol V2 FrameCodec。
- [ ] 测量启动、更新、调用和内存开销。
- [ ] 验证进程崩溃不影响宿主。

#### 阶段 B：Native SDK

- [ ] 定义与语言无关的 C ABI 或 JSON-RPC 适配层。
- [ ] 提供 C++ 参考 SDK，后续可扩展 Rust。
- [ ] 明确线程、生命周期和类型限制。

#### 阶段 C：服务与安全

- [ ] 接入 HostServices、EventBus 和权限。
- [ ] 使用 Windows Job Object 确保宿主退出后子进程被回收。
- [ ] 评估命名管道 ACL、进程身份和 PID 验证。

#### 阶段 D：产品决策

- [ ] 与进程内 DLL 比较性能、开发体验和故障率。
- [ ] 决定第三方默认模式以及受信任原生插件政策。
- [ ] 若收益不足，保留实验能力而不强制迁移。

### 测试与验收

- 原生 Worker 崩溃、死锁和非法退出不终止宿主。
- 超时后可以终止并重启目标进程。
- 进程退出后无残留 Job、Socket 或插件状态。
- 性能满足状态栏常规刷新；高频场景有合并和背压。
- 不兼容架构或协议在执行插件代码前拒绝。

### 风险与回滚

- 这是成本最高的方向，必须先做数据验证，不以“更安全”作为无量化理由全面重写。
- QML 仍在宿主加载，因此必须继续做路径和包信任验证。
- 若原型显示启动和内存成本过高，可维持“内置 C++ + 第三方隔离协议”的分层策略。

### 完成定义

- [ ] 技术原型和性能报告完成。
- [ ] 明确采用、限制采用或放弃的架构决策记录。
- [ ] 若采用，至少一个真实插件完成迁移和故障注入测试。
- [ ] 安全边界在用户与开发者文档中准确说明。

---

## 5. 跨方向依赖与实施顺序

推荐依赖顺序：

```text
PF-01 运行时健康与卸载安全
  └─ PF-02 增量生命周期
       └─ PF-03 SDK V2 与 Manifest
            ├─ PF-04 HostServices 与权限
            │    ├─ PF-05 EventBus 2.0
            │    └─ PF-07 UI 扩展
            ├─ PF-06 配置与密钥
            └─ PF-09 插件包与更新

PF-05 + PF-01
  └─ PF-08 Python Protocol V2

PF-03 + PF-04 + PF-05 + PF-08
  └─ PF-11 原生后端隔离

PF-10 开发工具贯穿全部方向
```

## 6. 建议里程碑

### 0.2.2：稳定性版本

- PF-01：Python 刷新周期、熔断、健康模型、EventBus 卸载作用域。
- PF-02：布局不重启、单插件配置重启、Python Worker 独立启停。
- PF-10：为以上能力增加基础测试和诊断显示。

该版本不引入破坏性 SDK 变化，重点是让现有插件稳定运行。

### 0.3.0：插件 SDK V2

- PF-03：Descriptor、Manifest、V1/V2 兼容和依赖预检。
- PF-04：HostServices 与 pluginContext。
- PF-05：EventBus 2.0 基础和 Python 接入。
- PF-06：配置版本、迁移、通用嵌套密钥。
- PF-10：SDK Validator 和 Python Test Harness。

### 0.4.0：生态与体验

- PF-07：标准 PopupHost、Action 和响应式布局。
- PF-08：主动推送、调用结果、依赖 doctor。
- PF-09：本地插件包、安装、签名和回滚。
- PF-10：create/package/install 工具闭环。

### 1.0 前评估

- PF-11：完成原生后端隔离原型和产品决策。
- 确定长期支持的 V2 SDK、包格式和兼容周期。
- 发布第三方插件兼容政策和安全模型。

## 7. 全局验收门禁

每个里程碑发布前必须满足：

- [ ] Release 构建通过。
- [ ] CTest 全部通过。
- [ ] Python Worker 和内置 Python 插件测试通过。
- [ ] 中文、英文和跟随系统语言运行时切换通过。
- [ ] appbar/taskbar、顶部/底部、100%/150%/200% DPI 验收通过。
- [ ] 配置损坏、插件缺失、Worker 崩溃和 DLL 加载失败不会拖垮其他插件。
- [ ] 插件路径、包内容和凭据扫描通过。
- [ ] 便携包安全门禁与 ZIP 独立检查通过。
- [ ] V1 插件兼容测试通过，或发布说明明确列出经批准的兼容性变化。
- [ ] 文档、示例和实际接口保持一致。

## 8. 当前推荐的下一步

先批准并实施 PF-01，不同时启动 SDK V2 或插件包工作。PF-01 完成后，用健康状态和卸载安全
作为 PF-02 增量生命周期的基础。只有单插件启停、错误隔离和配置重载稳定后，再开始对外
承诺 SDK V2、权限或插件安装格式。

PF-01 的第一个提交应只修复 Python `intervalMs` 并增加回归测试；第二个提交再加入熔断；
第三个提交加入健康模型；EventBus 作用域作为独立提交完成。这样每一步都能单独验证和回滚。
