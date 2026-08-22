# PF-11 原生 Worker 隔离评估

## 结论

本轮完成方案 A“隔离后端 + 宿主 QML”的可运行技术原型，但暂不把它接入 0.x 正式插件
加载链路，也不把现有 `IBarPlugin/1.0` DLL 自动迁入子进程。产品决策是：

- 内置、随宿主同工具链构建的 C++ 插件继续使用受信任进程内 DLL 路径。
- 第三方默认使用已经产品化的 Python/Protocol V2 隔离路径。
- 保留原生 Worker 原型作为 1.0 前的实验能力；只有 HostServices/EventBus 权限代理、背压、
  安装签名策略和真实插件迁移都完成后，才考虑向第三方开放。

这个决策不是把原型描述成权限沙箱。独立进程能隔离崩溃、ABI 冲突和可终止的死锁，但当前
用户身份下的 Worker 仍可直接访问该用户有权访问的文件、网络和 Win32 API。若未来需要抵御
恶意插件，必须另行评估 AppContainer、低完整性令牌或专用 Broker，并接受相应兼容成本。

## 原型范围

原型代码位于 `experiments/native_worker/`：

- `XuanHubPluginHost.exe` 承载一个原生计数器后端。
- `NativeWorkerIsolationTest.cpp` 作为最小宿主，负责启动、握手、请求、超时、终止和指标采样。
- 帧编解码直接编译现有 `plugins/pybridge/PyBridgeProtocol.cpp`，继续执行 4 字节大端长度、
  4 MiB 单帧上限、JSON 对象顶层和 32 层嵌套限制。
- IPC 使用父子进程匿名 stdin/stdout 管道。它不对其他本机进程开放监听端口，因此当前原型
  不需要命名管道名称、ACL 或对端 PID 猜测；QProcess 返回的子进程 PID 用于 Job Object
  归属和工作集采样。随机令牌通过子进程环境传递，作为握手的纵深校验。
- 每个 Worker 被放入 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` Job Object；宿主退出或会话销毁
  时关闭 Job 即回收子进程。超时 Worker 可单独 `kill()`，不等待其事件循环配合。

原型不加载任意旧 DLL，也不把 QObject、QImage 指针或自定义 C++ 类型跨进程传递。QML 仍由
宿主加载，因此包签名、路径边界、QML 信任和静态校验仍然必要。

## JSON 请求契约

Worker 启动后必须先发送带 token、协议版本、插件 ID 和能力列表的 Protocol V2 `hello`。
宿主在身份、协议和架构校验完成前不发送 `init`，所以不兼容后端不会进入插件初始化代码。

请求与响应使用 JSON 安全类型：null、bool、有限数字、字符串、数组和字符串键对象。

```json
{"type":"request","id":1,"method":"init","args":{"initial":4}}
{"type":"response","id":1,"ok":true,"result":{"initialized":true}}
```

原型方法为 `init/update/invoke/stop`，错误返回稳定 `error.code`。生产化时可沿用同一 envelope
增加 `event.publish/subscribe` 与 `service.request/response`，但必须由宿主根据安装时实际授权
进行代理，不能信任 Worker 自报权限。每个会话串行处理请求；耗时实现必须自建受控 worker
线程，不能阻塞协议读循环。宿主侧必须为请求设置截止时间，并为高频 values/event 增加有界
队列、合并和背压，不能把原型的同步逐帧循环直接用于高频采集。

## 实测结果

测试环境：Windows 11 x64、MSVC 2022、Qt 6.9.3、Release，2026-08-22 本机样本：

| 指标 | 结果 | 原型预算 |
|---|---:|---:|
| 启动并完成身份握手 | 32 ms | < 2000 ms |
| 500 次串行 `update` 平均往返 | 0.043 ms | < 20 ms |
| Worker 工作集 | 9,699,328 bytes（约 9.25 MiB） | < 100 MiB |

这些数字只证明普通状态栏刷新有足够余量，不是跨机器性能承诺。CI 只使用宽松预算防明显退化；
发布决策还需要低端设备、多 Worker、冷启动、杀毒软件干预和高频背压测试。

## 故障注入结果

- `abort()` 使 Worker 以 `CrashExit` 退出，宿主测试继续执行后续断言。
- `Sleep(INFINITE)` 模拟死锁；200 ms 无响应后宿主可在 2 秒预算内终止目标进程。
- 错误插件 ID 在 `hello` 阶段被拒绝，未发送 `init`。
- 会话使用匿名管道，没有残留监听 Socket；测试析构确保无残留子进程。

运行方式：

```powershell
cmake --build --preset default --config Release --target xuanhub_native_worker_isolation_tests
ctest --test-dir build/default -C Release -R xuanhub_native_worker_isolation_tests -V
```

## 产品化前置条件

1. 把会话管理器接入宿主的 PF-01 健康状态、PF-02 单插件启停和熔断。
2. 完成 HostServices、EventBus 和权限代理的跨进程实现及跨插件隔离测试。
3. 增加有界发送队列、values 合并、事件丢弃计数和请求取消。
4. 定义可独立安装的 C++ Worker SDK；Rust 只需实现同一 JSON/帧契约。
5. 迁移至少一个真实内置插件，比较进程内与隔离模式的启动、内存、交互和维护成本。
6. 让 `.xuanplugin` 显式声明 `runtime: native-worker`，并在执行前完成签名、架构和协议校验。

在这些条件完成前，`XuanHubPluginHost.exe` 只由 `BUILD_TESTING` 构建，不进入便携发布包，也不
出现在设置界面的可选运行模式中。
