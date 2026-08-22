---
name: create-xuanhub-plugin
description: Create, extend, validate, test, and optionally package XuanHub plugins with an AI coding agent. Use when a user asks to create a XuanHub C++ SDK V2 plugin or Python pybridge plugin, turn a feature idea into a status-bar plugin, add settings, QML UI, popups, i18n, actions, EventBus or HostServices integration to a plugin, or prepare a plugin for local testing and .xuanplugin distribution.
---

# 创建 XuanHub 插件

把用户的功能描述落实为符合当前仓库契约的 C++ 或 Python 插件。复用仓库的脚手架、示例和验证器；不要凭记忆重建 Manifest、生命周期或打包格式。

## 1. 定位仓库并读取契约

1. 使用 `git rev-parse --show-toplevel` 定位仓库根目录。
2. 确认根目录存在 `scripts/xuanhub_plugin.py` 和 `docs/README.md`。若不在 XuanHub 仓库中，先找到用户指定的 XuanHub checkout；找不到时再请求路径。
3. 完整读取根目录 `AGENTS.md`、`docs/README.md` 和 `docs/i18n.md`。
4. 按运行时完整读取权威契约：
   - C++：`docs/plugin-sdk-v2.md`。
   - Python：`plugins_py/README.md`。
5. 仅在需要发布时读取 `docs/xuanplugin-format.md`；需要在线更新源时再读取 `docs/xuanplugin-update-index.md`。
6. 检查 `git status --short`，保留用户已有改动。检查相关内置插件或示例，优先复用已验证的模式。

## 2. 收敛需求并选择运行时

从用户描述中提取：插件名称、全局唯一 ID、状态栏展示、交互或弹窗、设置项、刷新方式、权限、数据来源、中英文文案和交付目标。只询问会实质改变实现的缺失信息；其余采用小而安全的默认值，并在开始实现时说明。

按以下规则选择运行时：

- 优先 Python：业务逻辑以数据获取、网络 API、文本处理或定时任务为主，且不要求宿主进程内原生 API 或极低延迟。Python 插件 ID 使用 `py_` 前缀。
- 选择 C++ SDK V2：需要 Win32/Qt 原生能力、严格低延迟、宿主服务的原生接口或已有 C++ 实现。不要为新插件创建 V1 接口。
- 用户已指定运行时时遵从用户选择，并在发现明显不适配时说明具体约束。
- 用户仅开发 Python 插件或没有 Qt/MSVC 时，不要求其配置或编译整个宿主；完成无 Qt 验证后，把真实宿主视觉验收列为待办。

为非平凡插件建立短计划，至少覆盖脚手架、实现、自动验证和真实宿主验收。

## 3. 用官方 CLI 创建骨架

先运行 `python scripts/xuanhub_plugin.py create --help` 获取当前参数，不把本 Skill 中的命令当作版本锁定接口。

在仓库内开发时，默认目录为：

- C++：`plugins/<目录名>`。
- Python：`plugins_py/<目录名>`。

使用 CLI 原子创建新目录：

```powershell
python scripts/xuanhub_plugin.py create plugins/my_plugin --runtime native --id com.example.my_plugin --name "My Plugin"
python scripts/xuanhub_plugin.py create plugins_py/my_plugin --runtime python --id py_my_plugin --name "My Plugin"
```

不要手工复制旧插件作为新骨架，不要覆盖已存在目录。若目标插件已经存在，跳过 `create`，先审阅现有 Manifest、实现和 Git 差异，再做增量修改。

对于仓库内 C++ 插件，将其 `add_subdirectory(...)` 接入顶层 `CMakeLists.txt`。第三方独立插件则遵循 SDK 安装包的独立构建方式，不引入宿主 `app/` 源码依赖。

## 4. 实现插件

### 通用要求

- 让 Manifest、运行时 Descriptor、QML 文件名、插件 ID、能力、权限和配置版本保持一致。
- 只声明实际使用的权限。使用 HostServices、EventBus、动作或弹窗前，按权威契约检查能力和失败路径。
- QML 根可视项声明 `property var plugin`；需要主题、弹窗或宿主上下文时声明并防御性使用 `property var pluginContext`。
- 用内容驱动 `implicitWidth` 或 `width`，处理长英文、固定宽度、不同 DPI 和 `PluginSlot` 裁剪。
- 标准弹窗使用 `PluginPopup`，遵循宿主的互斥、定位和关闭行为，不自行复制窗口管理逻辑。
- 界面文字使用 `qsTranslate("plugin.<id>.<page>", "源码文字")`。同时维护 `i18n/zh-CN.json` 与 `i18n/en-US.json`，上下文仅使用本插件前缀；英文不得缺项。
- 设置枚举使用稳定值与可翻译标签分离。用户内容、令牌和个人数据不翻译；敏感字段使用 `secret: true`，绝不写入源码、Manifest、示例配置或测试产物。
- 保留用户未要求变更的文件和未知配置键。不要通过放宽 Validator 或安全门禁来让错误通过。

### C++ 插件

- 实现 `IBarPlugin2`，只链接 `XuanHub::barplugin` 或仓库内 `barplugin` 及必要 Qt/系统库。
- 保持 `initialize/applyConfig/shutdown` 生命周期对称；先销毁 DLL 创建的对象，再允许卸载。
- 后台工作使用 `QObject + moveToThread + QTimer`，不从工作线程访问 ViewModel 或 QML。
- 配置输入先在 `validateConfig()` 中校验，热更新在 `applyConfig()` 中完成；需要迁移时使用公开迁移接口。
- 参考 `examples/plugins/v2_demo/` 获取独立 SDK 用法；按功能参考 `plugins/clock/`、`plugins/sysinfo/` 或 `plugins/xuanhub_menu/`，不要依赖它们的私有实现。

### Python 插件

- `main.py` 至少实现 `update() -> dict`；按需实现 `init(config)`、`set_config(config)`、`invoke(name, args)`、`set_locale(locale)` 和 `stop()`。
- 不在 `update()` 或 `invoke()` 中无限阻塞。需要主动推送时采用 `PUSH_VALUES` 与 `xuanhub_host` 契约，后台线程不直接操作 IPC socket。
- 第三方依赖只在 `plugin.json.python` 中声明；不自动联网安装。原生扩展明确标记并接受 Python/x64 ABI 诊断。
- 固定提示优先返回稳定状态码或值，在 QML 层翻译。参考 `plugins_py/hello/`；需要异步动作、弹窗或推送时再分别参考最接近的内置示例。

## 5. 分层验证

每次修复验证错误后，重新运行受影响的最小验证层。所有命令以当前 `--help` 为准。

### 所有插件

```powershell
python scripts/xuanhub_plugin.py validate <插件源码目录>
```

检查新增文件中是否有凭据、本机绝对路径、日志、缓存或 VCS 元数据。修改 QML 后优先运行 Smoke Test；可用时输出截图并人工检查，不把截图生成成功当作视觉通过。

### Python

```powershell
python scripts/xuanhub_plugin.py test <插件目录> --repeat 2
python scripts/xuanhub_plugin.py doctor <插件目录>
python scripts/xuanhub_plugin.py run <插件目录>
```

若本机没有 QML Smoke Runner，可以明确跳过 `run`，但不能把它报告为通过。纯 Python 单元测试应覆盖数据解析、错误路径、配置热更新和稳定状态值。

### C++

先完成 Release 构建，再对构建输出中的插件目录执行：

```powershell
python scripts/xuanhub_plugin.py validate <Release插件目录>
python scripts/xuanhub_plugin.py run <Release插件目录>
python scripts/xuanhub_plugin.py doctor <Release插件目录>
```

若修改了共享框架、CLI 或模板，再运行相关目标和 `ctest --test-dir build/default -C Release --output-on-failure`。不要因为宿主可执行文件被占用而声称完成了新构建；先关闭运行中的 XuanHub 再重试。

### 真实宿主验收

重启 XuanHub 后验证插件加载、三栏位置、主题、中英文、长英文、100%/150% DPI、弹窗边界、设置保存与热更新、禁用/启用及退出清理。修改 `main.py`、Manifest、DLL 或 QML 后必须重启进程，不能依赖配置热重载替代代码重载。

## 6. 可选打包与交付

仅在用户要求分发包时执行：

```powershell
python scripts/xuanhub_plugin.py package <插件目录> --output <名称>-<版本>.xuanplugin
```

C++ 插件对 Release 运行目录打包，Python 插件对源插件目录打包。签名私钥必须位于插件目录和版本库之外。打包后报告输出路径、SHA-256、签名状态和仍需人工验证的项目。

交付时简要列出：选择的运行时与原因、实现的能力、关键文件、实际执行并通过的验证、未执行的真实环境验证，以及 Git 提交（仅当用户要求或仓库协作约定要求提交）。
