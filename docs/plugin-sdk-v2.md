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
