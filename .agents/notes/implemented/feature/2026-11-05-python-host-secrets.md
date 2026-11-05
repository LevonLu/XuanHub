# Agent Note: Python 插件复用宿主密钥服务

Status: implemented

## Problem

扫码登录产生的会话需要在运行期保存和删除。宿主已有按插件身份隔离的 DPAPI 密钥库，
但 Python Worker 的 HostServices RPC 只开放日志与通知，插件无法直接复用它。

## Decision

`xuanhub_host` 及其 `services` 对象提供 `read_secret/write_secret/remove_secret`。
沿用 `host_service` 请求和结果结构，以 `secrets.read/write/remove` 转发给绑定的宿主服务；
不修改 IPC 版本、公开 C++ ABI、设置 schema 或用户配置。

宿主校验 `secrets.own` 权限与参数类型，密钥名称继续使用现有单路径段规则。
单个值限制为 UTF-8 64 KiB，读写均检查，避免已有大值造成响应超出 IPC 上限。
空字符串写入沿用宿主的删除语义；删除不存在的键成功，读取不存在的键返回 `not_found`。
错误信息不包含密钥值；请求不得指定目标插件身份。

同步 HostServices 只允许在创建代理的 Worker 主线程调用。后台网络线程通过私有队列
把凭据交给 `update/invoke` 等回调保存，不直接读写 socket，不把凭据放入 QML values。

## Historical audit

- [统一账户](../../implemented/feature/2026-11-02-token-usage-unified-accounts.md)：部分重叠。
  其 schema 密钥事务继续成立，本篇增加独立的运行期服务，不改配置迁移。
- [常驻内存与窗口内容生命周期](../../implemented/architecture/2026-11-02-runtime-memory-lifetime.md)：
  检索命中但决定无关；进程隔离、stdout 与窗口生命周期保持原约定。
- `portable-public-key-marker` 处理分发扫描，与运行期密钥 RPC 无关。

## Alternatives considered

- 插件自行调用凭据管理器或 DPAPI：不必改宿主，但重复维护存储、身份和错误处理，
  其他扫码或 OAuth 插件仍会重复实现，因此复用宿主现有服务。
- 凭据经 QML 或配置草稿中转：可复用界面已有入口，但会使后台会话保存依赖界面生命周期，
  并增加明文传播范围，因此提供 Worker 直接调用的受控服务。

## Verification

- `python -m unittest plugins_py.tests.test_bridge_worker` 通过：非字符串参数和后台线程
  调用在 socket 发送前失败，新 Worker 保留旧宿主的 `unsupported_service` 返回。
- `pixi run build` 与 `pixi run ctest --test-dir build/default -C Release --output-on-failure`
  通过。新增真实 Worker 集成回归覆盖写入、读取、覆盖、删除、Worker 重启和重建服务后的恢复，
  以及两个插件同名键隔离、磁盘不含测试密钥明文。
- 同一集成回归覆盖未声明和未授予权限、伪造身份、无服务绑定、非法键/类型/额外参数、
  UTF-8 64 KiB 边界，以及超限读写失败不误删旧值、不触发 Worker 重启。
- 构建目录中的 pybridge 通过开发者 CLI `validate` 和 `doctor`，部署脚本与源码 SHA-256 一致。

## Consequences

Python 登录插件可复用宿主密钥存储和命名空间，代价是需要部署配套的 pybridge DLL 与 Worker 脚本。
同步服务共享已有请求超时，不支持后台线程直接调用；原先从后台线程调用日志或通知的插件
需要改为主线程交接。批量密钥不提供事务，会话应编码为一个字符串值。
独立进程和服务权限不等同于操作系统沙箱；不能阻止恶意插件绕过服务直接访问同用户资源。
本次未实现米家登录、设备控制或 QML 界面，自动回归不构成真实账号接入验收。
