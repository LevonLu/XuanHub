# 像素风景初版与六场景验证记录

> 本文保存 2026-11-08～11-09 的历史样本，不是当前插件全部能力的重新验收。
> 当前 0.3.0 功能见[插件说明](../plugins/pixel_landscape/README.md)，太阳与黄金时刻证据见[后续验证](pixel-landscape-solar-validation.md)。

验证日期：2026-11-08。范围为原生 `pixel_landscape` 插件、五种画框、六种风景、可选 `hostContentWidth` 槽位契约、首次启用时采用清单默认区域，以及实际验证中发现的 schema 枚举初始化时序。

## 产物与范围

- 源码：`plugins/pixel_landscape/`；Release 运行目录：`build/default/dist/Release/plugins/pixel_landscape/`。
- 检查图和完整原始日志：本机忽略目录 `build/pixel-landscape-evidence/`，没有打包、提交或远端发布。
- 图像均来自实际 QML 渲染，不是概念图。首版 `landscape-gallery.png` 展示小屋四时段 × 三天气，`landscape-widths.png` 展示 160/320/480/64/24/8 内容宽度和 40/28 高度；扩展后的分场景检查图见下文“六场景扩展”。
- `host-settings-zh.png`、`host-settings-en.png` 来自独立便携宿主的实际设置窗口。
- 原有工作区改动保留；插件不自行写入启用配置。本轮扩展保持日常宿主配置原值，测试只使用证据目录中的独立配置。
- 测试便携副本保存设置时，触发了宿主既有的 Windows 自启路径写回；现已纠正为测试前运行的日常宿主 `build/default/dist/Release/XuanHub.exe`。首次保存前没有单独快照自启项原文，因此只确认当前目标已纠正，不声称原注册表值逐字恢复。

## 构建与自动回归

`pixi run build` 通过；完整 CTest 通过。准确测试清单应以当前 `ctest -N` 为准，不把测试数量当作固定产品契约。

专用 `xuanhub_pixel_landscape_tests` 覆盖：

| 内容 | 证据 |
|---|---|
| 时段边界、跨午夜、固定时段、前跳/回拨 | `phasesAndClockJumps` |
| 缺省配置、非法类型/枚举拒绝、旧状态保留 | `invalidConfigPreservesState` |
| 原生插件重复初始化/关闭 | `nativeLifecycle` |
| 暂停冻结绘制、隐藏停止时间、点击切换、释放弹窗、右键设置 | `popupAnimationAndRelease` |
| 内边距扣除、零宽、自然宽度稳定、旧插件宽度不变 | `slotWidthOptInAndLegacy` |
| 有界流星间隔、关闭偶发效果、跳时不补播 | `sharedControllerAndRareEvents` |
| 双语键一致、窄屏英文工具栏、关闭后重开仍暂停、配置禁用动画 | `englishPopupAndPausedReopen` |
| 六场景七十二种组合与各六档宽度实际渲染 | `renderAllCombinations`、`renderAdaptiveWidths` |
| 场景 schema、热更新与双画面同步、暂停和弹窗对象保持 | `scenesAndHotUpdate` |
| 六场景栏上与弹窗概览 | `renderSceneOverview` |

`tests/test_settings_presentation.py` 另外运行从实际 SettingsWindow 提取的枚举编辑器，验证 Loader 后注入 field 时，已保存的 dusk/rain 不被 options 模型更新重置。

`ConfigServiceTest::newlyDiscoveredPluginUsesDeclaredRegion` 覆盖新发现插件的合法区域、非法/缺省回退、默认不启用、不标记草稿已修改，以及已有用户位置不被清单覆盖。

插件 CLI `validate/run/doctor` 对构建后的插件目录均通过，零校验错误、零插件诊断告警。源码目录缺少已编译 DLL 的提示不当作运行目录结果。

离屏平台会提示不支持窗口 raise/opacity 和默认字体目录，这些是测试平台限制；没有 QML Binding loop、TypeError 或 ReferenceError。检查图测试显式载入系统字体，不随插件分发字体文件。

## 实际宿主和缩放

使用 Windows 11、Qt 6.9.3 / MSVC x64，AMD Ryzen 7 8845H（16 逻辑处理器）、Radeon 780M，当前系统显示缩放为 200%。

独立便携宿主已验证：插件发现与初始化、中文和英文 schema、保存天气后 watcher 应用、禁用时独立 shutdown、重新启用、顶部/底部 appbar 切换与 taskbar 挂接。日志包含 AppBar 注册位置、解除注册和 `TaskbarEmbed attached to Shell_TrayWnd`。窗口截图验证了设置值回显和英文布局。

Qt 原生 Windows 后端另以 `QT_SCREEN_SCALE_FACTORS=1/1.25/1.5/2` 分别执行全部插件回归，均通过；这验证四档 Qt 渲染比例，不等同于实际系统 DPI 热切换和跨屏验收。

以下仍需人工设备验收：跨物理屏幕切换、实际系统 100%/125%/150%/200% DPI 热切换、真实休眠恢复，以及 taskbar 内风景与弹窗的最终层级/视觉检查。共享 PopupService 的自动回归与独立 QQuickWindow 交互不能代替这些检查。

## CPU 与内存口径

CPU 使用进程 user + kernel 秒差除以墙钟秒数，100% 表示占满一个逻辑核，未再除以 16。内存同时记录私有提交量（PrivateUsage）和私有工作集（USS），以 MiB 表示，不使用强制清空工作集或 GC。

每个短样本运行 65 秒，排除前 10 秒预热，CPU 取剩余时间总差，内存取剩余样本中位数。场景固定夜晚、小雨；独立探针栏上 320 × 40，完整宿主栏高 40、槽宽采用清单默认值，实际内容扣除主题内边距。弹窗场景为 640 × 360 加 48 高度工具栏。

独立 Qt 探针的原始数据为 `performance.json`、`perf-*/metrics.json`：

| 样本 | 单核 CPU % | 私有提交 MiB | 私有工作集 MiB |
|---|---:|---:|---:|
| 空栏基线 | 0.17 | 47.60 | 20.91 |
| 同尺寸空弹窗 | 0.45 | 77.28 | 42.04 |
| 风景条 | 2.28 | 58.15 | 28.81 |
| 风景条 + 欣赏弹窗 | 5.04 | 107.00 | 67.32 |

此探针中风景条相对空栏增加约 10.55 MiB 私有提交，弹窗组合增加约 59.40 MiB。空弹窗自身增加约 29.68 MiB；风景组合还包含 Canvas、QML 和文字资源，不能把全部差值归因于粒子。独立探针的弹窗结果超过 35 MiB 目标，不能报为达标。最终完整宿主的启用/禁用对照结果在下节记录。

三十次开关使用独立原生 Qt 探针的 `cycles` 样本，并非完整宿主内的人工回放：预热后开关阶段 10–30 秒私有提交中位数处于 117.22–117.40 MiB；关闭后 30–65 秒稳定在 116.125 MiB。内容释放后 Qt/图形驱动仍留有缓存，此短样本没有随开关次数持续增长的趋势，不能单独作为长期无泄漏证明。

## 完整宿主对照与长运行

完整宿主对照脚本 `build/pixel-landscape-evidence/measure_host.py` 对同一便携运行目录、同一设置窗口、同一时钟配置，分别启动未启用风景、启用风景、启用并打开弹窗三个独立进程。仅弹窗样本在测试副本 QML 中增加一次性自动打开定时器，禁止失焦关闭以保持整个采样周期可比；正式插件文件不含该钩子。

完整宿主样本为 `host-performance.json`，结果如下：

| 样本 | 单核 CPU % | 私有提交 MiB | 私有工作集 MiB |
|---|---:|---:|---:|
| 未启用风景 | 0.26 | 228.94 | 175.00 |
| 启用风景条 | 2.81 | 240.21 | 189.91 |
| 风景条 + 欣赏弹窗 | 1.83 | 293.03 | 237.35 |

完整宿主中，风景条的单核 CPU 增量约 2.55%，私有提交增量约 11.27 MiB，满足 5%/20 MiB 目标。弹窗组合的 CPU 增量约 1.57%，满足 10% 目标；私有提交增加约 64.09 MiB，**未达到 35 MiB 目标**。CPU 差异受窗口呈现状态影响，不由本次单轮数据推导弹窗比栏上更省 CPU。

当前实现已限制虚拟画布、刷新率、缓存和粒子容量，并采用不透明弹窗背景；这台机器 200% 缩放下仍有上述内存缺口。不能用强制 GC、工作集修剪或扣除窗口开销宣称整体达标。进一步降低这项指标需要针对 Qt/图形后端窗口资源分析；首版功能可用，资源验收不宣称全部通过。

最终离屏场景连续运行 1800 秒后正常退出。静态层绘制 1 次、动态层绘制 18000 次，符合固定时段和天气下缓存地形、10 FPS 更新动态层的设计。私有提交中位数 24.18 MiB，首 1–5 分钟中位数 24.79 MiB，末 25–30 分钟中位数 24.18 MiB；未见随时间持续增长。完整数据为 `soak-final-30min/metrics.json`、`probe-bar.json`，汇总为 `soak-summary.json`。这是离屏平台结果，不用于替代原生窗口资源测量。

最终完整宿主采用正式插件资源，在原生 Windows 后端、底部 appbar、固定夜晚小雨、时钟同时启用的条件下连续运行 1800.32 秒，期间没有异常退出或 QML 错误。该样本没有打开设置窗口，因此不与上表带设置窗口的绝对内存值混比。

排除前 60 秒后，完整宿主单核 CPU 平均 1.62%，私有提交中位数 120.79 MiB、范围 120.63–121.13 MiB，私有工作集中位数 74.66 MiB。首 1–5 分钟私有提交中位数 120.89 MiB，末 25–30 分钟为 120.75 MiB，变化 −0.14 MiB；本次样本未见持续增长。这是整个测试宿主的绝对占用，不是插件增量，也不等同于长期无泄漏证明。

完整数据为 `host-soak-30min.json`，对应宿主日志为 `host-soak-30min-host.log`。达到时长后采样脚本主动终止自己启动的测试进程，记录中的 `exitCode=1` 来自此操作，不是运行途中崩溃；正常卸载另由生命周期回归和设置页禁用验证。临时测试进程与探针副本已清理，日常宿主仍运行，日常配置 SHA-256 与测试前一致。

## 画框外观细化

最初的细框实际渲染前后图保留在 `build/pixel-landscape-evidence/frame-before/` 与 `frame-after/`。当前版本通过 `frameStyle` 提供无框、细框、木框、卡纸框、金属框五种选择，默认木框。框厚按实际宽高限制，内容宽度小于 32 或高度小于 12 时取消画框；自然宽度和交互范围保持原值。

本轮 Release 构建、插件回归 13 项、CLI `validate/run/doctor` 均通过。新增回归覆盖清单与后端缺省一致、五种合法值与非法类型拒绝、热更新保留暂停、不改变自然宽度，以及极窄/极矮时保留非负画面尺寸。深浅背景、320 × 40 和 160 × 28 的实际 QML 检查图为 `build/pixel-landscape-evidence/frame-options/landscape-frames.png`。

真实便携宿主设置页已核对五种中文选项与下拉选择回显；在独立配置中依次切换五种值，watcher 均重新读取，没有插件重新初始化或 QML 错误，日志为 `frame-options/host-hot-update.log`。设置页测试草稿取消，未保存到日常配置。相关 CTest 三项（插件回归、设置呈现、清单校验）及决策笔记校验通过。日常宿主为更换 DLL 重启，配置文件保持原值。上文长运行和资源数据对应加框前版本，本轮未重复资源采样。

## 六场景扩展

`scene` 可选山间小屋、海边灯塔、夜雨街角、湖畔营地、田野铁道和空中浮岛，缺省仍为小屋；不改变已有配置与配置版本。五个新增绘图模块共用天空、天气、调色与 10 FPS 时钟，没有新增定时器或隐藏场景画布。场景切换更新栏上与已打开的弹窗，保持暂停和动画时间。

本轮证据集中于 `build/pixel-landscape-evidence/scenes/`：

- Release 构建通过；插件回归 15 项通过；完整 CTest 48/48 通过，耗时 91.69 秒。`validate/run/doctor` 均为零错误、零告警；决策笔记校验通过。
- `scenesAndHotUpdate` 从真实 schema 枚举六种场景，验证缺省、双画面同步、打开的弹窗内容不重建、暂停绘制计数不增长，以及关闭再打开保留场景与暂停。非法场景枚举、数字、布尔和空值均拒绝，原有效配置保持。
- `landscape-scenes.png` 是六种实际 QML 场景与栏上构图概览；`landscape-gallery-<scene>.png` 共覆盖 72 种场景、时段、天气组合；`landscape-widths-<scene>.png` 覆盖各场景 160/320/480/64/24/8 宽度及 40/28 高度。逐图检查地标、雨雪和昼夜配色，修正灯塔、街角、铁道与风车在低矮栏中的顶部空间约束。
- 原生 Windows 设置窗口已核对六个中文下拉选项，选择空中浮岛后正确回显；测试草稿取消，未保存日常设置。独立便携配置依次写入六场景，`host-hot-update.log` 有六次 watcher 读取，仅一次插件初始化，没有 QML 错误。中英文键一致及六种英文名称由实际 QML 回归检查。

短期资源对照由 `scenes/host_scenes.py` 驱动完整便携宿主，原始数据为 `scenes/performance.json`。底部 appbar、40 逻辑像素栏高（日志为 80 物理像素，即 200% 缩放）、固定夜晚小雨、木框、同一时钟插件；不打开设置窗口。每个样本独立启动 25 秒，排除前 5 秒，CPU 按一个逻辑核 100% 计算，内存取稳态样本中位数。未启用风景基线 CPU 0.49%，私有提交 110.45 MiB，私有工作集 79.31 MiB。

| 场景 | 栏上 CPU 增量 % | 栏上私有提交增量 MiB | 弹窗组合 CPU 增量 % | 弹窗组合私有提交增量 MiB |
|---|---:|---:|---:|---:|
| 山间小屋 | 0.33 | 12.18 | 5.64 | 70.46 |
| 海边灯塔 | 1.88 | 12.29 | 4.65 | 70.84 |
| 夜雨街角 | 0.74 | 11.76 | 5.80 | 69.54 |
| 湖畔营地 | 0.08 | 12.35 | 1.14 | 69.66 |
| 田野铁道 | 1.72 | 12.25 | 6.12 | 69.79 |
| 空中浮岛 | -0.25 | 12.36 | 2.37 | 69.93 |

六场景短样本的栏上 CPU 和私有提交增量均低于原定 5% / 20 MiB，弹窗组合 CPU 低于 10%；**弹窗组合私有提交增量仍超过 35 MiB 目标**。不能因功能回归通过而将资源验收记为全部达标。短样本 CPU 受调度和窗口呈现状态影响，负差值不表示启用插件能降低 CPU，也不从单轮样本推导弹窗比栏上更省 CPU。数据保留每秒 CPU、私有提交与私有工作集；不使用强制 GC 或工作集修剪。所有样本运行期间无提前退出或 QML 错误，达到采样时长后脚本终止自己的测试进程；退出码不用于声称自然退出成功。弹窗样本仅在测试副本注入一次性打开钩子，采样完成后恢复正式 QML。

本轮没有重复原版半小时长运行、真实系统 DPI 热切换、物理多屏与休眠验收。此前资源结果对应扩展前版本，不直接代表新增场景；本轮短期对照另行记录，不能据此宣称长期无泄漏。

## 提交前独立验证（2026-11-09）

将本轮功能对应的暂存内容导出到 `build/pixel-landscape-evidence/commit-check/`，在不包含菜单图标、米家和拖拽排序等未提交修改的源码副本中，通过 `pixi run build` 完成全新 Release 构建。

首次运行风景测试发现该全新运行目录没有离屏平台插件；开发运行目录此前留有 qoffscreen.dll，掩盖了测试部署依赖。测试目标现通过 `Qt6::QOffscreenIntegrationPlugin` 导入目标自动复制当前配置的插件，不写死 Qt 安装路径。重建后确认文件自动生成，风景回归与插件 Validator 两项通过；同一独立副本中的配置和设置呈现测试也通过，CLI `run/doctor` 通过。日志分别为 `commit-build.log`、`commit-build-final.log`、`commit-tests.log`、`commit-tests-final.log`、`commit-run.json` 与 `commit-doctor.json`，均保留在证据目录。笔记校验通过。

本次只复测拆分提交所影响的范围，未重复上文的全工作区完整 CTest、视觉检查和资源采样；既有弹窗内存预算缺口保持原结论。

## 复现入口

```powershell
pixi run build
ctest --test-dir build/default -C Release --output-on-failure
python scripts/xuanhub_plugin.py validate build/default/dist/Release/plugins/pixel_landscape
python scripts/xuanhub_plugin.py run build/default/dist/Release/plugins/pixel_landscape
python scripts/xuanhub_plugin.py doctor build/default/dist/Release/plugins/pixel_landscape
pixi run verify-notes
```

检查图：设置 `XUANHUB_PIXEL_EVIDENCE` 指向输出目录后运行 `build/default/dist/Release/xuanhub_pixel_landscape_tests.exe`。短期与长运行入口为 `--probe baseline|shell|bar|popup|cycles <秒数>`，不纳入常规 CTest 的等待时间。
