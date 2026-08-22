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

## V1 迁移步骤

1. 将基类从 `IBarPlugin` 改为 `IBarPlugin2`，IID 改为 `xuanhub.BarPlugin/2.0`。
2. 把 `metadata()` 改成返回完整 `PluginDescriptor` 的 `descriptor()`。
3. 把 `init(BarPluginContext*)` 改为 `initialize(const PluginContext2&)`，返回结构化结果。
4. 把可选 `IBarPluginConfigurable` 合并为 V2 的 `validateConfig/applyConfig`。
5. 在 sidecar 中增加 `apiVersion/entry/capabilities/permissions/dependencies/compatibility`。
6. 保证运行时 Descriptor 与 sidecar 完全一致；宿主发现以 sidecar 为权威，漂移时拒绝加载。

完整参考见 `plugins/clock/`；不依赖仓库内部目标的第三方参考见
`examples/plugins/v2_demo/`。
