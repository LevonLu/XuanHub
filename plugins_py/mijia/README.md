# 米家 / Mi Home

`py_mijia` 使用 Python 标准库与 QML 连接米家云，不依赖 Home Assistant。
面板沿用玄枢的 FluentWinUI3 控件、8px 卡片圆角和主题色板，支持深浅主题及双列／单列布局。
使用 XuanHub v0.5.0 或后续兼容正式包，包内已含所需 Python 与密钥 RPC。源码开发基线为提交
`e0de08c` 或后续版本，解释器要求 CPython 3.12+ x64。旧宿主需升级；本插件不需要第三方 Python 包。

## 使用

1. 在宿主设置中启用“米家”并保存，设置窗口自动维护 pybridge 总开关。
2. 点击状态栏入口，选择与米家 App 一致的区域，获取二维码并用 App 扫描确认。
3. 按家庭、房间或设备名称／型号筛选，自有与共享家庭均显示。星标用于收藏。
4. 卡片显示设备状态；点击单路电源图标可切换，空调温度使用加减按钮，其余控制进入详情。
   网格图标切换双列与单列布局。
5. 退出登录删除此插件保存的本机会话。删除失败会保留提示，可再次点击退出重试。

## 修改任务栏显示

点击米家入口 → 面板底部“任务栏显示”，将显示内容改为“设备读数”，选择一台设备及其可读属性。
可以自定义名称、隐藏名称或单位，并设置 0–3 位最大小数位；窗口提供当前值预览，保存后生效。
也可在设备详情点击属性旁的“显示到任务栏”快速固定该项。首版一次只显示一个设备的一项属性。

改回“在线设备数”或使用右上角菜单清除读数可恢复默认展示。取消不修改已保存选择，
选择及显示格式按米家账号和区域隔离，重启后恢复；关闭面板仍按设置的周期刷新所选读数。
没有规格或没有可读属性的设备会说明原因，不能保存为任务栏读数。

开发时将该目录复制到已有便携宿主的 `plugins_py/mijia/`，退出并重启宿主。
手写配置示例（还需保留 pybridge 条目）：

```json
{"id":"py_mijia","configVersion":1,"position":"right","config":{"intervalMs":1000,"refreshSeconds":10}}
```

`intervalMs` 固定 1000，用于后台结果与主线程密钥交接；`refreshSeconds` 为关闭面板后的
状态栏刷新周期，范围 5–300 秒。打开面板时每 5 秒读取当前页设备；目录每 5 分钟刷新。
手动刷新会重新获取目录并重试缺失规格。读取最多 100 项一批，控制请求优先。

## 兼容范围

灯、插座／开关面板、空调、风扇、窗帘、净化／加湿设备和传感器使用类型卡片的标准控件。
真实规格决定开关、枚举、数值范围、步长与只读读数，多键面板绑定实际父设备与服务。
其他品类使用通用详情，写入前确认设备、属性及目标值。原始字符串、内部配置及通用动作
不执行，门锁只读。仅适配窗帘无参开／停／关动作；缺少规格或属性的设备保留并说明原因。

写入后回读，状态相符才显示已确认；超时不自动重发。无法确认时显示“指令已发送，状态未确认”。
读取失败保留旧值并标记失效，状态栏图标变灰表示旧读数；没有指定读数时显示在线数量。
指定设备或属性消失后提示重新选择。云端能发现设备不代表其所有功能均可远程控制。

首版没有场景、批量控制、自动化、多账号、摄像头视频、局域网直连或历史记录。
离线规格测试不代表所有品类都已通过真机验证。

## 数据与实现

- `cloud.py`：扫码、Cookie、区域路由、签名、设备分页与共享补查、属性协议。
- `specs.py`：规格索引、内存缓存、类型绑定、通用控件与写入校验。
- `runtime.py`：一个后台网络线程，主线程每秒交接；取消及退出使旧代次请求失效。
- `Mijia.qml`：状态栏、交互回执和共享 PluginPopup；偏好仍按账号隔离。
- `MijiaPanel.qml` / `DeviceCard.qml` / `PropertyControl.qml`：家庭摘要、设备卡片及通用详情。
- `BarDisplaySettings.qml`：设备和可读属性选择、名称／单位／精度预览，保存到账号私有偏好。
- `DeviceGlyph.qml`：随 DPI 重绘的矢量图标，界面跟随宿主主题并提供中英翻译。

会话整体保存到宿主 DPAPI 密钥库的插件私有键 `login_session`，成功后才宣布已登录。
二维码只在内存流转，令牌、Cookie 和签名材料不进入 QML、日志或 config.json。
收藏、家庭、状态栏选项及显示格式位于配置目录的
`plugin-data/py_mijia/preferences-<账号与区域摘要>.json`。便携模式使用便携目录，普通模式
使用 `%APPDATA%/XuanHub`；运行过的配置、密钥库和缓存不得进入插件分发目录。

协议来源和许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 验证

```powershell
python -B -m unittest plugins_py.tests.test_mijia -v
python scripts/xuanhub_plugin.py validate plugins_py/mijia
python scripts/xuanhub_plugin.py test plugins_py/mijia --repeat 2
python scripts/xuanhub_plugin.py doctor plugins_py/mijia
python scripts/xuanhub_plugin.py run plugins_py/mijia --json
```

测试使用公开脱敏合同夹具和合成会话。QML 交互测试需要 Qt qmltestrunner，无 Qt 环境会
明确跳过；截图位于构建目录。真实验收另行覆盖扫码、读写、重启恢复、收藏和固定读数，
以及多屏边界、DPI 与 appbar/taskbar。
