# plugin_templates.py — XuanHub C++/Python 插件模板的唯一生成源
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import tempfile
from typing import Any


PLUGIN_ID = re.compile(r"^[A-Za-z0-9._-]{1,128}$")


class TemplateError(ValueError):
    pass


def _class_name(plugin_id: str) -> str:
    parts = [part for part in re.split(r"[^A-Za-z0-9]+", plugin_id) if part]
    value = "".join(part[:1].upper() + part[1:] for part in parts) or "Plugin"
    return f"X{value}" if value[0].isdigit() else value


def _render(text: str, tokens: dict[str, str]) -> str:
    for key, value in tokens.items():
        text = text.replace(f"@@{key}@@", value)
    return text


def _json_file(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2) + "\n"


def _shared_files(plugin_id: str, qml_name: str) -> dict[str, str]:
    contexts = {f"plugin.{plugin_id}.main": {"Ready": "就绪"}}
    return {
        "i18n/zh-CN.json": _json_file({"locale": "zh-CN", "contexts": contexts}),
        "i18n/en-US.json": _json_file({
            "locale": "en-US",
            "contexts": {f"plugin.{plugin_id}.main": {"Ready": "Ready"}},
        }),
        qml_name: _render("""// @@QML_NAME@@ — 插件状态栏界面
import QtQuick

Item {
    id: root
    property var plugin
    property var pluginContext
    width: label.implicitWidth + 24
    height: 40

    readonly property var colors: pluginContext ? pluginContext.colors : ({
        "fg": "#cdd6f4", "accent": "#89b4fa"
    })

    Text {
        id: label
        anchors.centerIn: parent
        color: root.colors.fg
        text: plugin && plugin.text ? plugin.text
              : qsTranslate("plugin.@@PLUGIN_ID@@.main", "Ready")
    }
}
""", {"QML_NAME": qml_name, "PLUGIN_ID": plugin_id}),
    }


def _python_files(plugin_id: str, name: str, author: str) -> dict[str, str]:
    qml_name = f"{_class_name(plugin_id.removeprefix('py_'))}.qml"
    files = _shared_files(plugin_id, qml_name)
    files.update({
        "plugin.json": _json_file({
            "id": plugin_id,
            "name": name,
            "version": "0.1.0",
            "author": author,
            "apiVersion": 2,
            "configVersion": 1,
            "capabilities": ["barItem", "settings"],
            "permissions": [],
            "qml": qml_name,
            "settings": [{
                "group": "显示",
                "fields": [{
                    "key": "text", "type": "string", "label": "显示文本",
                    "default": "Ready",
                }],
            }],
        }),
        "main.py": """# main.py — 隔离 Worker 中运行的插件逻辑
_config = {}


def init(config):
    set_config(config)


def set_config(config):
    global _config
    _config = dict(config or {})


def update():
    return {"text": str(_config.get("text", "Ready"))}


def set_locale(locale):
    # 固定界面文字应在 QML 语言包翻译；这里只保留处理日期/数字格式的扩展点。
    pass


def stop():
    pass
""",
        "README.md": f"""# {name}

由 `xuanhub-plugin create --runtime python` 生成。

```powershell
python scripts/xuanhub_plugin.py validate {plugin_id}
python scripts/xuanhub_plugin.py test {plugin_id}
python scripts/xuanhub_plugin.py package {plugin_id} --output {plugin_id}-0.1.0.xuanplugin
```
""",
    })
    return files


def _native_files(plugin_id: str, name: str, author: str) -> dict[str, str]:
    stem = _class_name(plugin_id)
    qml_name = f"{stem}.qml"
    tokens = {
        "PLUGIN_ID": plugin_id, "NAME": name, "AUTHOR": author,
        "CLASS": stem, "QML_NAME": qml_name,
    }
    files = _shared_files(plugin_id, qml_name)
    files.update({
        "metadata.json": _json_file({
            "id": plugin_id,
            "name": name,
            "version": "0.1.0",
            "author": author,
            "apiVersion": 2,
            "configVersion": 1,
            "minHostVersion": "0.2.1",
            "maxHostVersion": "0.x",
            "entry": {"backend": f"{plugin_id}.dll", "qml": qml_name},
            "capabilities": ["barItem"],
            "permissions": [],
            "compatibility": {
                "architecture": "x64", "qtVersion": "6.9.3",
                "compiler": "msvc2022", "crt": "dynamic",
            },
        }),
        f"{stem}Plugin.h": _render("""// @@CLASS@@Plugin.h — @@NAME@@ 插件入口
#pragma once

#include <QObject>
#include <QUrl>
#include "barplugin/IBarPlugin2.h"

class @@CLASS@@ViewModel;

class @@CLASS@@Plugin final : public QObject, public xuanhub::IBarPlugin2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "xuanhub.BarPlugin/2.0" FILE "metadata.json")
    Q_INTERFACES(xuanhub::IBarPlugin2)

public:
    xuanhub::PluginDescriptor descriptor() const override;
    xuanhub::PluginInitResult initialize(const xuanhub::PluginContext2& context) override;
    xuanhub::PluginConfigValidationResult validateConfig(const QVariantMap& config) const override;
    xuanhub::PluginConfigApplyResult applyConfig(const QVariantMap& config) override;
    QObject* viewModel() override;
    QUrl qmlUrl() const override;
    void shutdown() override;

private:
    @@CLASS@@ViewModel* m_viewModel = nullptr;
};
""", tokens),
        f"{stem}Plugin.cpp": _render("""// @@CLASS@@Plugin.cpp — @@NAME@@ 生命周期实现
#include "@@CLASS@@Plugin.h"
#include "@@CLASS@@ViewModel.h"

xuanhub::PluginDescriptor @@CLASS@@Plugin::descriptor() const {
    xuanhub::PluginDescriptor value;
    value.id = QStringLiteral("@@PLUGIN_ID@@");
    value.displayName = QStringLiteral("@@NAME@@");
    value.version = QStringLiteral("0.1.0");
    value.apiVersion = 2;
    value.configVersion = 1;
    value.minHostVersion = QStringLiteral("0.2.1");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("@@AUTHOR@@");
    value.entry.backend = QStringLiteral("@@PLUGIN_ID@@.dll");
    value.entry.qml = QStringLiteral("@@QML_NAME@@");
    value.capabilities = {QStringLiteral("barItem")};
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

xuanhub::PluginInitResult @@CLASS@@Plugin::initialize(const xuanhub::PluginContext2& context) {
    m_viewModel = new @@CLASS@@ViewModel;
    applyConfig(context.pluginConfig);
    return xuanhub::PluginInitResult::succeeded();
}

xuanhub::PluginConfigValidationResult @@CLASS@@Plugin::validateConfig(const QVariantMap&) const {
    return xuanhub::PluginConfigValidationResult::accepted();
}

xuanhub::PluginConfigApplyResult @@CLASS@@Plugin::applyConfig(const QVariantMap&) {
    return xuanhub::PluginConfigApplyResult::applied();
}

QObject* @@CLASS@@Plugin::viewModel() { return m_viewModel; }
QUrl @@CLASS@@Plugin::qmlUrl() const { return QUrl(QStringLiteral("@@QML_NAME@@")); }

void @@CLASS@@Plugin::shutdown() {
    delete m_viewModel;
    m_viewModel = nullptr;
}
""", tokens),
        f"{stem}ViewModel.h": _render("""// @@CLASS@@ViewModel.h — @@NAME@@ 的 QML 数据模型
#pragma once

#include <QObject>
#include <QString>

class @@CLASS@@ViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text CONSTANT)

public:
    using QObject::QObject;
    QString text() const { return QStringLiteral("Ready"); }
};
""", tokens),
        "CMakeLists.txt": _render("""# @@NAME@@ 插件
add_library(@@PLUGIN_ID@@ MODULE
    @@CLASS@@Plugin.h
    @@CLASS@@Plugin.cpp
    @@CLASS@@ViewModel.h
)
set_target_properties(@@PLUGIN_ID@@ PROPERTIES AUTOMOC ON PREFIX "" OUTPUT_NAME @@PLUGIN_ID@@)
target_link_libraries(@@PLUGIN_ID@@ PRIVATE barplugin Qt6::Core)
target_include_directories(@@PLUGIN_ID@@ PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(@@PLUGIN_ID@@ PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${XUANHUB_BIN_DIR}/plugins/@@PLUGIN_ID@@")
add_custom_command(TARGET @@PLUGIN_ID@@ POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:@@PLUGIN_ID@@>" "${XUANHUB_BIN_DIR}/plugins/@@PLUGIN_ID@@/@@PLUGIN_ID@@.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/@@QML_NAME@@" "${XUANHUB_BIN_DIR}/plugins/@@PLUGIN_ID@@/@@QML_NAME@@"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" "${XUANHUB_BIN_DIR}/plugins/@@PLUGIN_ID@@/metadata.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/i18n" "${XUANHUB_BIN_DIR}/plugins/@@PLUGIN_ID@@/i18n")
""", tokens),
        "README.md": f"""# {name}

由 `xuanhub-plugin create --runtime native` 生成。把本目录加入顶层 `CMakeLists.txt`：

```cmake
add_subdirectory(plugins/{plugin_id})
```

构建后再对运行目录执行 `validate` 和 `run`；源码目录缺少 DLL 的警告属于预期。
""",
    })
    return files


def create_plugin(destination: Path, runtime: str, plugin_id: str,
                  name: str, author: str) -> dict[str, Any]:
    destination = destination.resolve()
    if destination.exists():
        raise TemplateError("destination already exists")
    if runtime not in {"python", "native"}:
        raise TemplateError("runtime must be python or native")
    if not PLUGIN_ID.fullmatch(plugin_id):
        raise TemplateError("plugin id contains unsupported characters")
    if runtime == "python" and not plugin_id.startswith("py_"):
        raise TemplateError("python plugin id must start with py_")
    if not name.strip() or not author.strip():
        raise TemplateError("name and author must not be empty")

    files = (_python_files(plugin_id, name.strip(), author.strip())
             if runtime == "python"
             else _native_files(plugin_id, name.strip(), author.strip()))
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".xuanhub-plugin-", dir=destination.parent))
    try:
        for relative, content in files.items():
            output = temporary / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(content, encoding="utf-8", newline="\n")
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return {
        "ok": True,
        "path": str(destination),
        "id": plugin_id,
        "runtime": runtime,
        "files": sorted(files),
    }
