# XuanHub 原生插件 SDK V2

SDK V2 用可预检的 `metadata.json` 和独立 IID 替代“先加载 DLL 再判断是否兼容”。
宿主仍支持 V1，旧插件不需要重新编译；新插件应使用 V2。

## 构建约束

- Windows x64、MSVC 2022、C++17。
- Qt 必须与宿主完全一致，当前为 6.9.3 `msvc2022_64`。
- CRT 使用动态运行时：Release `/MD`，Debug `/MDd`。
- 插件只链接 `XuanHub::barplugin` 和所需 Qt/系统库，不依赖 `app/` 源码。

先安装本仓库生成的 SDK，再独立构建示例：

```powershell
cmake --install build/default --config Release --prefix build/sdk-install
cmake -S examples/plugins/v2_demo -B build/sdk-v2-demo `
  -G "Visual Studio 17 2022" -A x64 `
  -DXuanHubPlugin_DIR="$PWD/build/sdk-install/lib/cmake/XuanHubPlugin" `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.9.3/msvc2022_64"
cmake --build build/sdk-v2-demo --config Release
```

产物在 `build/sdk-v2-demo/dist/v2_demo/`，整个目录可复制到
`<XuanHub>/plugins/v2_demo/`。启用时配置 ID 使用 `example.v2_demo`。

## Manifest 契约

V2 必填字段为 `id/name/version/apiVersion/entry/compatibility`。`apiVersion` 必须为 2；
`entry.backend`、`entry.qml`、`entry.settingsQml` 都只能是插件目录内的相对路径。
宿主会解析 Windows junction 的最终路径，绝对路径、`..` 和联接越界均在加载 DLL 前拒绝。

`compatibility` 必须声明 `architecture/qtVersion/compiler/crt`。原生 ABI 任一项不匹配都会
在设置页显示失败原因。`minHostVersion` 是最低宿主 SemVer；`maxHostVersion` 接受完整版本或
主版本通配（如 `0.x`）。

能力具有运行时语义：

- `barItem`：允许创建 bar QML；声明后必须提供 `entry.qml`。
- `settings`：允许提供 `entry.settingsQml` 或 schema 表单。
- `popup`：声明插件需要弹窗 UI；标准 PopupHost 在 PF-07 提供。

`dependencies` 的必需项会参与拓扑排序，缺失、版本不匹配或循环都会阻止插件执行。可选依赖
不会阻止加载；插件通过 `PluginContext2::hasDependency(id)` 查询，不能获取其他 DLL 对象。

`configVersion` 是可选正整数，缺失视为 1。需要升级旧配置时，插件对象另外实现
`IPluginConfigMigration`；既有 `IBarPlugin2` 虚函数表不变：

```cpp
#include "barplugin/IPluginConfigMigration.h"

PluginConfigMigrationResult DemoPlugin::migrateConfig(
    int fromVersion, const QVariantMap& config) const {
    QVariantMap migrated = config;
    if (fromVersion == 1) migrated.insert(QStringLiteral("newKey"), true);
    else return PluginConfigMigrationResult::failed(QStringLiteral("unsupported version"));
    return PluginConfigMigrationResult::migrated(migrated);
}
```

宿主只把临时副本交给迁移器，并用 `validateConfig()` 再校验结果。启动时可使用迁移后的
内存配置，但不会自动覆盖 `config.json`；设置页显示建议，用户采用并保存后才以
`QSaveFile` 原子写入新 `configVersion`。未来版本、缺失迁移器、迁移或校验失败会阻止目标
插件初始化，原配置和其他插件不受影响。插件不得在迁移函数中自行写配置文件或密钥库。

## 设置 Schema

`settings` 使用“分组 → fields”结构；通用类型为 `bool/int/double/string/color/enum/json`，
另支持 `object/array`。`object/array/json` 都由 JSON 编辑器输入，前两者还会检查顶层类型。
`string` 可用 `regex` 校验（整串匹配需自行添加 `^`、`$`），并以 `multiline: true` 选择
多行编辑器。枚举推荐写成稳定值与显示标签分离的对象：

```json
{
  "key": "endpoint",
  "type": "string",
  "regex": "^https://",
  "multiline": false,
  "visibleWhen": {"key": "mode", "equals": "custom"},
  "enabledWhen": {"key": "editable", "truthy": true}
}
```

条件只读取本插件 config 顶层键，运算符为 `equals/notEquals/in/truthy`；若同时声明多个，
按此顺序使用第一个。条件只影响呈现，隐藏或禁用不会删除既有值，也不会跳过保存时的最终
Schema 校验。`settingsQml` 自绘页和通用表单均写入同一份设置草稿，最终都经过宿主的
Schema 校验，未知键继续保留。已加载的 V2 插件还会在写文件前执行 `validateConfig()`；
验证不调用 `applyConfig()`，拒绝时 config.json 与运行态均保持不变。未实现验证接口的 V1
插件保持兼容，新启用但尚未加载的插件仍会在首次初始化时执行 V2 验证。

## HostServices 与权限

`PluginContext2::hostServices` 是按插件 ID 绑定的版本化服务入口。插件不得缓存到
`shutdown()` 之后，也不得把指针交给其他插件。使用前查询版本、能力与权限：

```cpp
#include "barplugin/HostServices.h"

PluginInitResult DemoPlugin::initialize(const PluginContext2& context) {
    if (context.hostServices) {
        context.hostServices->logger()->log(
            PluginLogLevel::Info, QStringLiteral("initialized"));
        if (context.hostServices->hasPermission(QStringLiteral("notifications")))
            context.hostServices->notifications()->show(
                QStringLiteral("Demo"), QStringLiteral("Ready"));
    }
    return PluginInitResult::succeeded();
}
```

当前权限为 `notifications`、`storage.private`、`secrets.own`、`shell.openUrl` 和
`shell.openFile`。日志、语言、主题和打开自身设置页不要求权限；其他服务未声明或未授予时
返回稳定错误码 `permission_denied`。私有存储位于
`<configDir>/plugin-data/<plugin-id>/`，密钥自动进入本插件命名空间。

原生 DLL 与宿主同进程，设置页显示的权限是信任说明，不能阻止插件直接调用 Win32 或文件
API；隔离 Python 插件的 HostServices 请求会强制检查实际授予权限。

插件 bar QML 根对象除 `property var plugin` 外应声明 `property var pluginContext`。
它提供 `pluginId/apiVersion/trustLevel/declaredPermissions/grantedPermissions`、主题色、语言、
屏幕与 bar 边缘，以及 `log/notify/readText/writeText/openSettings/openUrl` 等受控调用。
旧的 `themeManager/barWindow/barScreenGeometry/i18n` 全局仍保留一个兼容周期，新插件不要依赖。

## EventBus V2

V1 `EventBus::publish/subscribe` 保持同步语义以兼容旧插件。V2 必须通过
`PluginContext2::eventScope` 使用，投递按宿主事件循环排队，作用域销毁会取消已排队回调、
订阅和未完成请求：

```cpp
#include "barplugin/PluginEventScope.h"

EventSubscriptionOptions options;
options.replayRetained = true;
context.eventScope->subscribeV2(
    QStringLiteral("app.locale"), receiver,
    "onLocaleEnvelope(QVariantMap)", options);
context.eventScope->publish(
    QStringLiteral("plugin.example.v2_demo.changed"),
    {{QStringLiteral("value"), 1}});
```

Topic 只能使用 `app.*`、`service.<name>.*` 或 `plugin.<id>.*`。插件可发布和订阅自己的
命名空间，也可订阅 `app.*`；跨插件订阅需声明
`events.subscribe:plugin.<id>.*`，跨命名空间发布/请求需声明相应的
`events.publish:<topic-pattern>`。`*` 只允许作为末尾整段通配符。

V2 handler 收到的 envelope 为：

```json
{
  "schemaVersion": 1,
  "topic": "plugin.example.v2_demo.changed",
  "source": "plugin.example.v2_demo",
  "timestamp": "2026-08-22T12:00:00.000Z",
  "kind": "event",
  "data": {"value": 1}
}
```

`setState()` 保存最后状态，订阅时可选择立即回放；插件状态默认随所有者卸载清除，宿主
`app.locale/app.theme` 状态保留。`request/respond/cancelRequest` 使用相关 ID，首个响应生效，
超时、取消和响应者卸载分别返回稳定错误码 `timeout/canceled/responder_unloaded`。
Payload 只允许 JSON/QVariant 安全集合。每订阅者最多排队 256 条，超限丢弃最新事件并写入
不含 Payload 的诊断日志；慢同步 V1 槽也只告警，不自动切线程。

## V1 迁移步骤

1. 将基类从 `IBarPlugin` 改为 `IBarPlugin2`，IID 改为 `xuanhub.BarPlugin/2.0`。
2. 把 `metadata()` 改成返回完整 `PluginDescriptor` 的 `descriptor()`。
3. 把 `init(BarPluginContext*)` 改为 `initialize(const PluginContext2&)`，返回结构化结果。
4. 把可选 `IBarPluginConfigurable` 合并为 V2 的 `validateConfig/applyConfig`。
5. 在 sidecar 中增加 `apiVersion/entry/capabilities/permissions/dependencies/compatibility`。
6. 保证运行时 Descriptor 与 sidecar 完全一致；宿主发现以 sidecar 为权威，漂移时拒绝加载。
7. 将 QML 的宿主依赖迁到 `pluginContext`，将系统访问迁到 `hostServices` 并声明最小权限。

完整参考见 `plugins/clock/`；不依赖仓库内部目标的第三方参考见
`examples/plugins/v2_demo/`。
