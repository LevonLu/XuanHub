# XuanHub 文档导航

本文档是仓库内开发资料的统一入口。根目录 `README.md` 面向用户和首次开发者；具体契约只在
下表指定的权威文档中维护，其他文件应链接到这里，不再复制整套协议。

## 按角色阅读

| 角色 | 从这里开始 | 继续阅读 |
|---|---|---|
| 框架维护者 | [`AGENTS.md`](../AGENTS.md) | [`plugin-framework-roadmap.md`](plugin-framework-roadmap.md)、[`native-worker-isolation-evaluation.md`](native-worker-isolation-evaluation.md) |
| 使用 Agent 创建插件 | [`.agents/skills/create-xuanhub-plugin/SKILL.md`](../.agents/skills/create-xuanhub-plugin/SKILL.md) | 根据运行时继续阅读 C++ 或 Python 权威契约 |
| C++ 插件开发者 | [`plugin-sdk-v2.md`](plugin-sdk-v2.md) | [`i18n.md`](i18n.md)、[`xuanplugin-format.md`](xuanplugin-format.md)、[`xuanplugin-update-index.md`](xuanplugin-update-index.md) |
| Python 插件开发者 | [`plugins_py/README.md`](../plugins_py/README.md) | [`i18n.md`](i18n.md)、[`xuanplugin-format.md`](xuanplugin-format.md)、[`xuanplugin-update-index.md`](xuanplugin-update-index.md) |
| 主题开发者 | [`themes/README.md`](../themes/README.md) | 根目录 [`README.md`](../README.md) 的运行和打包说明 |
| 插件发布者 | [`xuanplugin-format.md`](xuanplugin-format.md) | [`xuanplugin-update-index.md`](xuanplugin-update-index.md)、对应运行时的插件契约 |

## 权威文档

| 文档 | 负责的内容 |
|---|---|
| [`README.md`](../README.md) | 产品说明、安装使用、开发入口和源码构建 |
| [`AGENTS.md`](../AGENTS.md) | 框架架构、生命周期铁律、代码规范、测试与发布门禁 |
| [`.agents/skills/create-xuanhub-plugin/SKILL.md`](../.agents/skills/create-xuanhub-plugin/SKILL.md) | Agent 创建、实现、验证和打包 XuanHub 插件的标准工作流 |
| [`plugin-framework-roadmap.md`](plugin-framework-roadmap.md) | 大型升级方向、状态、实施记录和未完成项 |
| [`plugin-sdk-v2.md`](plugin-sdk-v2.md) | C++ SDK V2、Manifest、HostServices、EventBus、布局和 V1 迁移 |
| [`plugins_py/README.md`](../plugins_py/README.md) | Python Manifest、Protocol V2、worker、依赖和 QML 契约 |
| [`i18n.md`](i18n.md) | C++、QML、Python 共用的多语言契约 |
| [`xuanplugin-format.md`](xuanplugin-format.md) | 离线插件包、哈希、签名和安全边界 |
| [`xuanplugin-update-index.md`](xuanplugin-update-index.md) | 在线更新索引及发布者身份连续性 |
| [`native-worker-isolation-evaluation.md`](native-worker-isolation-evaluation.md) | 原生后端隔离原型结论和产品化门槛 |
| [`themes/README.md`](../themes/README.md) | Theme Schema v2、外观令牌和可信 Bar 外壳扩展契约 |

## 插件开发闭环

统一入口为 `scripts/xuanhub_plugin.py`：

```powershell
python scripts/xuanhub_plugin.py create plugins/my_plugin --runtime native --id com.example.my_plugin --name "My Plugin"
python scripts/xuanhub_plugin.py create plugins_py/my_plugin --runtime python --id py_my_plugin --name "My Plugin"
python scripts/xuanhub_plugin.py validate <插件目录>
python scripts/xuanhub_plugin.py test <Python 插件目录>
python scripts/xuanhub_plugin.py run <插件目录或运行时目录>
python scripts/xuanhub_plugin.py doctor <插件目录或运行时目录>
python scripts/xuanhub_plugin.py package <插件目录或运行时目录> --output <输出.xuanplugin>
```

使用 `python scripts/xuanhub_plugin.py <命令> --help` 查看该命令的准确参数。`validate` 是只读
预检；`test` 执行真实 Python Worker 生命周期；`run` 使用最小 Qt 宿主做 QML Smoke Test；
`doctor` 检查 Python 依赖或原生 ABI；`package` 复用安装器的安全门禁。

## 维护规则

1. 改动公开插件接口时，必须同时更新对应的 C++ 或 Python 权威契约和测试。
2. 改动跨运行时规则时，同时检查 `plugin-sdk-v2.md`、`../plugins_py/README.md` 和 `i18n.md`。
3. 改动插件包、签名或更新流程时，同时检查两个 `xuanplugin-*` 文档及 CLI 帮助。
4. 框架内部实现和生命周期铁律只写入 `AGENTS.md`，路线图只记录计划、状态和验收证据。
5. `README.md` 和工具专属的本地速查文件保持短小，只提供入口和高频命令，不复制完整协议。
6. 文档中的版本、测试数量和插件清单容易变化，优先写可执行命令，避免固定数量或重复清单。

## 发布记录

- [`v0.4.4`](releases/v0.4.4.md)：FluentWinUI3 设置界面、WinUI 配色与紧凑菜单、原生插件重载崩溃修复。
- [`v0.4.3`](releases/v0.4.3.md)：菜单分段切换、Token Usage 统一账户与弹窗优化、内存及多屏定位改进。
- [`v0.4.2`](releases/v0.4.2.md)：taskbar 几何与菜单定位修复、内置插件 SDK V2 升级、多工具用量监控。
- [`v0.4.1`](releases/v0.4.1.md)：主题 Schema v2、玄枢菜单动态操作与插件运行诊断修正。
- [`v0.4.0`](releases/v0.4.0.md)：插件框架 SDK V2、跨运行时服务、插件生态与开发工具链。
