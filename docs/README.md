# XuanHub 文档导航

本文档是仓库内开发资料的统一入口。根目录 `README.md` 面向用户和首次开发者；具体契约只在
下表指定的权威文档中维护，其他文件应链接到这里，不再复制整套协议。

## 按角色阅读

| 角色 | 从这里开始 | 继续阅读 |
|---|---|---|
| 框架维护者 | [`AGENTS.md`](../AGENTS.md) | [`plugin-framework-roadmap.md`](plugin-framework-roadmap.md)、[`native-worker-isolation-evaluation.md`](native-worker-isolation-evaluation.md) |
| C++ 插件开发者 | [`plugin-sdk-v2.md`](plugin-sdk-v2.md) | [`i18n.md`](i18n.md)、[`xuanplugin-format.md`](xuanplugin-format.md)、[`xuanplugin-update-index.md`](xuanplugin-update-index.md) |
| Python 插件开发者 | [`plugins_py/README.md`](../plugins_py/README.md) | [`i18n.md`](i18n.md)、[`xuanplugin-format.md`](xuanplugin-format.md)、[`xuanplugin-update-index.md`](xuanplugin-update-index.md) |
| 主题开发者 | [`themes/README.md`](../themes/README.md) | 根目录 [`README.md`](../README.md) 的运行和打包说明 |
| 插件发布者 | [`xuanplugin-format.md`](xuanplugin-format.md) | [`xuanplugin-update-index.md`](xuanplugin-update-index.md)、对应运行时的插件契约 |

## 权威文档

| 文档 | 负责的内容 |
|---|---|
| [`README.md`](../README.md) | 产品说明、安装使用、开发入口和源码构建 |
| [`AGENTS.md`](../AGENTS.md) | 框架架构、生命周期铁律、代码规范、测试与发布门禁 |
| [`plugin-framework-roadmap.md`](plugin-framework-roadmap.md) | 大型升级方向、状态、实施记录和未完成项 |
| [`plugin-sdk-v2.md`](plugin-sdk-v2.md) | C++ SDK V2、Manifest、HostServices、EventBus、布局和 V1 迁移 |
| [`plugins_py/README.md`](../plugins_py/README.md) | Python Manifest、Protocol V2、worker、依赖和 QML 契约 |
| [`i18n.md`](i18n.md) | C++、QML、Python 共用的多语言契约 |
| [`xuanplugin-format.md`](xuanplugin-format.md) | 离线插件包、哈希、签名和安全边界 |
| [`xuanplugin-update-index.md`](xuanplugin-update-index.md) | 在线更新索引及发布者身份连续性 |
| [`native-worker-isolation-evaluation.md`](native-worker-isolation-evaluation.md) | 原生后端隔离原型结论和产品化门槛 |
| [`themes/README.md`](../themes/README.md) | 主题包格式和色板规则 |

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
