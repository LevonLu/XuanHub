# 软件一键更新

设置窗口“通用 → 软件更新”和托盘“检查软件更新”读取官方 GitHub 仓库的最新正式 Release。
自动检查默认开启，每天最多一次，可在更新卡片即时关闭；同一版本只提醒一次，可忽略。
手动检查仍可重新查看已忽略版本。网络失败、限流、无效发行数据会显示错误，不视为已是最新。

点击“立即更新”并确认后，宿主下载完整 Windows x64 便携 ZIP，检查大小、SHA-256、
包内文件清单和本地冲突。准备完成前仍运行当前版本，下载和准备阶段可取消。
随后宿主正常退出，由独立更新器备份并替换发行文件，自动启动新版。
主界面完成加载并持续运行通过启动确认后，更新器才报告成功；替换或启动失败时恢复旧文件并重启旧版。
更新不保存设置草稿，请先保存需要保留的更改。

## 保存范围与恢复

- 保留已有 `config.json`、`secrets.json`、`portable`、插件数据、受管插件、私有 Python 环境及不在官方清单内的文件。
- 只替换清单中的官方文件，并清理新版已删除的旧官方文件；本地改过、删除过的官方文件及新增路径冲突会阻止更新。
- 更新事务、结果和备份保存在 `%TEMP%/XuanHub-update-*/`；检查偏好与最近事务位置保存在配置目录的 `app-update-state.json`。
- 更新器中断时保留 `.xuanhub-update.json`。宿主下次能够正常装载时会先启动独立更新器恢复，再重启旧版。
  若中断导致 EXE 或 Qt 无法装载，可运行事务目录中的 `helper/XuanHubUpdater.exe --recover <事务目录> <正在退出的宿主PID>` 恢复；
  需要无宿主的离线恢复时使用 `helper/XuanHubUpdater.exe --recover-offline <事务目录>`。
- 启动确认覆盖宿主、主 QML 和短期进程存活，不证明每个第三方插件及外部服务正常。
  备份保留供诊断，系统清理临时目录后无法再使用该备份。

## 发行契约

只支持 GitHub 官方仓库 `LevonLu/XuanHub`，不提供 Gitee 或自定义更新源。
使用 GitHub 的[正式 Release](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)
和[附件摘要](https://docs.github.com/en/rest/releases/assets)，不需要在客户端配置 Token。
附件必须唯一命名为 `XuanHub-v<版本>-portable-x64.zip`，版本使用正式 SemVer，附件提供大小与 `sha256:` 摘要。
更新请求和重定向均限定 HTTPS 与官方 GitHub 下载主机。

`scripts/package_portable.ps1` 在干净 staging 上生成 `update-manifest.json`，再执行原有安全门禁。
清单 Schema 1 包含版本和 `files[]`（`path`、`size`、`sha256`）；路径为包内相对路径，哈希为 64 位小写十六进制。
清单覆盖官方 EXE、DLL、QML、Python 运行时与内置资源，排除默认配置和便携标记，清单自身不参与自哈希。
更新器拒绝额外 ZIP 文件、链接、路径穿越、Windows 路径别名、大小写碰撞及大小和哈希不符。

**v0.5.0 是首次支持一键更新的正式基线**；0.4.x 等历史包没有发行清单，需要退出旧版、备份配置与密钥后手动迁移一次。不要用新版默认配置覆盖自己的数据。
源码构建目录不自动生成清单，也不作为正式升级目标。独立构建验收可使用
`scripts/package_portable.ps1 -BuildDirectory build/app-update -PythonStrategy Bundle` 生成全新发行 staging；
该命令不补齐第三方包，完整正式包还需按根 [README](../README.md#从源码构建) 加入内置插件依赖。

## 验证范围

v0.5.0 发布已完成独立 Release 构建、更新器成功/失败回滚进程测试和全新解压验证，见
[发布说明](releases/v0.5.0.md)。这些证据不等于已完成从 v0.5.0 到后续正式 Release 的真实远端升级；
下一次跨版本发布需另行记录在线检查、下载、替换、重启和数据保留的完整验收。
