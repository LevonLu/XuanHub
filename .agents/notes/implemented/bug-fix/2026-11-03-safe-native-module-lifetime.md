# Agent Note: 重载释放插件实例并保留原生模块映像

Status: implemented

## Problem

手动重新加载配置会销毁插件实例并立即物理卸载 DLL。Qt 的元类型句柄与 QML 缓存可以比插件实例活得更久，
仅确认 QObject、线程和 worker 已停止，不足以证明 DLL 已没有引用。
隔离程序重载后稳定出现 `Cannot queue arguments of type 'SysInfoSnapshot'`。
保留真实 `QMetaType` 句柄、销毁实例后再创建参数的回归，在旧实现中以 `0xC0000005` 退出，位置为 `QMetaType::create`。
用户在临时异常捕获下再次点击没有退出，因此没有取得原始偶发退出的调用栈。

## Decision

发现阶段与正式加载共用的 QPluginLoader 保留 Qt 默认 `PreventUnloadHint`，同时保留立即解析符号。
卸载仍先移除 QML 引用、关闭事件作用域、停止线程和 worker，再 shutdown/unload 销毁插件根实例；
DLL 代码、静态元对象与元类型实现保持映射直到进程结束。重载继续创建新的插件实例。
同一路径的原生 DLL 替换需要退出宿主；新版本目录发现机制保持原样。
该策略遵循 Qt 的 [QPluginLoader 默认加载提示](https://doc.qt.io/archives/qt-6.9/qpluginloader.html#loadHints-prop)
与 [PreventUnloadHint 契约](https://doc.qt.io/archives/qt-6.9/qlibrary.html#LoadHint-enum)。

## Historical audit

按卸载、重载、shutdown、DeferredDelete 与 QMetaObject 检索活跃笔记。
[常驻内存与窗口生命周期](../../implemented/architecture/2026-11-02-runtime-memory-lifetime.md)部分重叠，继续保留其视图释放与 worker 隔离，
本篇补充代码映像的存活边界。Fluent 设置与主题笔记只涉及界面和色板，与物理 DLL 卸载策略无关。
原 PluginCatalog 注释以同路径文件替换为由清除 PreventUnloadHint，本篇以确认的 Qt 元类型悬空风险取代该假设。

## Alternatives considered

在 DLL 卸载前强制 QML GC 能回收部分脚本对象，但不能证明所有 Qt 元类型句柄、连接参数和其他引擎缓存都已释放。
把 SysInfoSnapshot 改为基础容器能消除这一类私有元类型，但不能解决其他原生插件留下的元对象和类型引用。
让重新加载配置只走增量更新可避免本次卸载，但会失去手动重建插件与 worker 的恢复能力。

## Testing

`PluginManagerTest::nativeMetaTypesSurviveInstanceReload` 在旧实现中访问冲突；修复后连续三轮
验证旧 ViewModel 确实销毁、缓存元类型可创建与销毁参数、新系统指标信号持续到达。
受管原生版本测试在子进程内核对覆盖发现与实例重建，父进程核对退出后 DLL 文件可删除，
不再要求实例停止时物理卸载代码映像。

`pixi run ctest --test-dir build/default -C Release --output-on-failure` 的其余项目全部通过；
原生管理器测试中旧的文件即时解锁断言调整后，单独重跑该测试通过。
修复宿主通过 `pixi run configure -B build/reload-fix` 与
`pixi run cmake --build build/reload-fix --config Release --target XuanHub --parallel 6` 构建。
隔离便携运行目录使用原共享库、原插件及修复 EXE，经 QTest 注入真实菜单点击，
offscreen 与 Windows 原生任务栏各连续十次重载后正常退出，日志无 SysInfoSnapshot 入队失败；
原生轮次确认成功嵌入 Shell_TrayWnd。仅在现有 DPI 下验证，不覆盖多屏与 DPI 切换。
非活动桌面与屏外实例的 QML 定时器未完成重载，该轮因测试超时结束，
不计为通过；原生通过结果来自活动桌面上的可见隔离实例。

## Consequences

已加载 DLL 的同路径覆盖受 Windows 文件锁限制，需要退出宿主再替换。仅移除内存映像不再作为插件卸载成功的验收条件。
实例、线程和 worker 仍按原顺序销毁，新实例可以安全使用 Qt 已缓存的类型实现。
原生静态数据与代码页保留到进程退出；若未来必须支持同路径二进制热替换，应重新评估进程隔离，
不能仅恢复物理卸载开关。原始偶发退出未取得调用栈，本次修复覆盖已确认的类型生命周期崩溃路径。
