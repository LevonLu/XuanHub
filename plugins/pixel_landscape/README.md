# 像素小风景 / Pixel Landscape

六种离线像素风景，原生 SDK V2 插件，ID `pixel_landscape`，版本 `0.1.0`。原创绘图数据随源码分发，不需要图片下载、Python、网络权限或私有数据文件。

## 使用

构建后在玄枢的「设置 → 插件」启用。默认不写入现有启用配置。左键打开或关闭欣赏弹窗，右键进入本插件设置；弹窗可暂停、继续和关闭，Esc、失焦及其他插件弹窗沿用宿主规则。

默认自然内容宽度 320，位置为中间；位置、顺序、手动宽度使用宿主通用设置。槽位内边距会占用一部分实际画面宽度。自动布局最低槽位宽度 160，继续不足时隐藏；手动设置不足 32 的内容宽度时保留当前场景的简化地标，停止栏上动态细节。弹窗场景最大 640 × 360，按屏幕可用范围缩小，工具栏单独换行。

在「设置 → 插件 → 像素小风景 → 场景」选择风景，保存后两处画面同步更新，暂停状态和动画时间保持。旧配置默认使用山间小屋；选择街角不会自动改为雨天或夜晚，所有场景仍由独立的时段与天气设置控制。

| 场景 | 配置值 | 画面和动态细节 |
|---|---|---|
| 山间小屋 | `cabin` | 远山、树林、溪流、炊烟 |
| 海边灯塔 | `lighthouse` | 礁岸、帆船、海浪、海鸥、黄昏和夜间扫光 |
| 夜雨街角 | `street` | 小店、雨棚、暖橱窗、路灯、雨天倒影与涟漪 |
| 湖畔营地 | `campsite` | 帐篷、木栈桥、湖光、篝火、晴朗黄昏和夜间萤火虫 |
| 田野铁道 | `railway` | 麦田、小站、电线杆、信号灯；列车每 90 秒经过一次，持续约 12 秒 |
| 空中浮岛 | `island` | 云海、风车、花园、小瀑布和飞鸟 |

在「设置 → 插件 → 像素小风景 → 画框样式」选择栏上画框：无画框、极简细框、暖木框、白色卡纸框、像素金属框。默认暖木框；细框跟随宿主主题，其他样式使用各自的材质颜色，悬停时外沿提亮。框厚随槽位宽高缩减，极窄或极矮时自动取消画框，保留剪影空间。保存设置即可切换，暂停状态保持。

| 配置键 | 可选值 | 缺省值 |
|---|---|---|
| `scene` | `cabin / lighthouse / street / campsite / railway / island` | `cabin` |
| `timeMode` | `auto / dawn / day / dusk / night` | `auto` |
| `weather` | `clear / rain / snow` | `clear` |
| `frameStyle` | `none / thin / wood / mat / metal` | `wood` |
| `animationsEnabled` | 布尔值 | `true` |
| `rareEventsEnabled` | 布尔值 | `true` |

自动时段按电脑本地时间计算：清晨 05:00–08:00、白天 08:00–17:00、黄昏 17:00–20:00，其余为夜晚。这些时间表达氛围，不计算真实日出日落。天气手动选择。六场景、四时段与三天气共 72 种组合；画框独立选择。

配置通过宿主草稿、保存与文件监视流程生效；非法枚举和类型会被拒绝，保留上一份有效状态。暂停是当前插件实例的状态，两处画面共用，关闭弹窗不重置；重启插件恢复正常运行。关闭 `animationsEnabled` 后仍会校时和应用天气，暂停按钮不可用。

## 实现边界

- `PixelLandscapeViewModel` 在 GUI 线程中校时，每 30 秒一次。正常跨时段渐变约 20 秒，时钟跳变、唤醒和重新显示直接同步当前时段。
- `LandscapeController.qml` 以一个 100 毫秒时钟驱动两处画面。暂停或两处均不可见时停止；重新显示不补播隐藏期间的动画或流星。
- `LandscapeScene.qml` 使用两个低分辨率 Canvas：地形和建筑缓存，云、烟、水光及雨雪更新动态层。展开图使用 320 × 180 虚拟像素，栏上上限 800 × 96，不使用平滑纹理。
- `SceneArt.js` 提供固定种子、有限调色板和整数像素轮廓。雨雪数量最多为栏上 48、弹窗 96；不积累粒子或运行历史。晴朗夜间流星间隔 8–12 分钟，可关闭。
- 五个新增场景各有独立的 `*Art.js` 绘制模块，共享天空、天气和像素绘图函数；只有当前场景的几何与两层画布参与渲染。所有动态细节从同一动画时间计算，没有新定时器、后台任务或场景对象缓存。
- 弹窗内容由 Loader 首次打开时创建，关闭后释放。窗口外壳复用宿主 `PluginPopup`，没有替代窗口和屏幕定位逻辑。
- UI 外壳使用作用域 `pluginContext` 的主题色，风景调色独立于宿主深浅主题。中英文语言包位于 `i18n/`。

## 构建与验证

```powershell
pixi run build
ctest --test-dir build/default -C Release --output-on-failure
python scripts/xuanhub_plugin.py validate build/default/dist/Release/plugins/pixel_landscape
python scripts/xuanhub_plugin.py run build/default/dist/Release/plugins/pixel_landscape
python scripts/xuanhub_plugin.py doctor build/default/dist/Release/plugins/pixel_landscape
pixi run verify-notes
```

构建产物位于 `build/default/dist/Release/plugins/pixel_landscape/`。资产同步目标在无需重新链接 DLL 时也会更新 QML、绘图脚本和语言包。QML 修改后仍需重启宿主。

专用回归为 `xuanhub_pixel_landscape_tests`。设置环境变量 `XUANHUB_PIXEL_EVIDENCE` 为输出目录后运行测试，可生成 `landscape-scenes.png`（六场景概览）、`landscape-gallery-<scene>.png`（各场景 12 种组合）与 `landscape-widths-<scene>.png`（160/320/480/64/24/8 宽度）。测试程序另有独立长运行入口：`--probe baseline|shell|bar|popup|cycles <秒数>`；用环境变量 `XUANHUB_PIXEL_SCENE` 选择探针场景，缺省 `cabin`。`cycles` 在前 30 秒完成 30 次开关，其余时间观察释放后的稳定状态。常规 CTest 不执行半小时等待。

实测、检查图位置和尚未完成的真实设备验收见[验证记录](../../docs/pixel-landscape-validation.md)。设计依据见[决策笔记](../../.agents/notes/implemented/feature/2026-11-08-pixel-landscape.md)。
