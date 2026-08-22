# xuanhub_plugin.py — 无 Qt 依赖的 XuanHub 插件开发者命令行入口
"""XuanHub 插件开发者 CLI。

当前先提供只读 ``validate``。输出协议刻意保持简单稳定，后续 create/test/doctor/package
都复用相同的诊断结构；宿主权威校验入口接入后会作为额外检查层，而不是改变 JSON 形状。
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


RESULT_SCHEMA_VERSION = 1
ID_PATTERN = re.compile(r"^[A-Za-z0-9._-]{1,128}$")
SEMVER_PATTERN = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
QML_PLUGIN_PROPERTY = re.compile(r"\bproperty\s+var\s+plugin\b")
QML_ROOT_ITEM = re.compile(r"\b(Item|Rectangle|Pane|Control)\s*\{")
WINDOWS_ABSOLUTE_PATH = re.compile(r"(?i)(?:^|[^A-Za-z0-9_])[A-Z]:[\\/]")
POSIX_USER_PATH = re.compile(r"(?:^|[^A-Za-z0-9_])/(?:home|Users)/[^/\s]+/")
SUSPICIOUS_NAMES = {
    ".git", ".hg", ".svn", "__pycache__", "secrets.json",
}
SUSPICIOUS_SUFFIXES = {".pyc", ".pyo", ".log", ".lock"}
FIELD_TYPES = {"bool", "int", "double", "string", "color", "enum", "json", "object", "array"}


class DuplicateKeyError(ValueError):
    pass


@dataclass(frozen=True)
class Diagnostic:
    severity: str
    code: str
    message: str
    path: str = ""


class Validator:
    def __init__(self, plugin_dir: Path) -> None:
        self.plugin_dir = plugin_dir.resolve()
        self.diagnostics: list[Diagnostic] = []
        self.manifest: dict[str, Any] = {}
        self.manifest_path: Path | None = None
        self.runtime = "unknown"
        self.plugin_id = ""

    def add(self, severity: str, code: str, message: str, path: Path | str = "") -> None:
        display_path = ""
        if path:
            candidate = Path(path)
            try:
                display_path = candidate.resolve().relative_to(self.plugin_dir).as_posix()
            except (OSError, ValueError):
                display_path = str(path)
        self.diagnostics.append(Diagnostic(severity, code, message, display_path))

    def validate(self) -> dict[str, Any]:
        if not self.plugin_dir.is_dir():
            self.add("error", "path.not_directory", "插件路径不是可读取目录", self.plugin_dir)
            return self.result()

        python_manifest = self.plugin_dir / "plugin.json"
        native_manifest = self.plugin_dir / "metadata.json"
        if python_manifest.is_file() and native_manifest.is_file():
            self.add("error", "manifest.ambiguous", "目录不能同时包含 plugin.json 和 metadata.json")
            return self.result()
        if python_manifest.is_file():
            self.runtime = "python"
            self.manifest_path = python_manifest
        elif native_manifest.is_file():
            self.runtime = "native"
            self.manifest_path = native_manifest
        else:
            self.add("error", "manifest.missing", "缺少 plugin.json 或 metadata.json")
            return self.result()

        self.manifest = self._read_json_object(self.manifest_path, "manifest") or {}
        if self.manifest:
            self._validate_manifest()
            self._validate_settings()
            self._validate_qml()
            self._validate_i18n()
        self._scan_tree()
        return self.result()

    def result(self) -> dict[str, Any]:
        errors = sum(item.severity == "error" for item in self.diagnostics)
        warnings = sum(item.severity == "warning" for item in self.diagnostics)
        return {
            "schemaVersion": RESULT_SCHEMA_VERSION,
            "command": "validate",
            "ok": errors == 0,
            "plugin": {
                "id": self.plugin_id,
                "runtime": self.runtime,
                "path": str(self.plugin_dir),
            },
            "summary": {"errors": errors, "warnings": warnings},
            "diagnostics": [asdict(item) for item in self.diagnostics],
        }

    def _read_json_object(self, path: Path, kind: str) -> dict[str, Any] | None:
        def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
            result: dict[str, Any] = {}
            for key, value in pairs:
                if key in result:
                    raise DuplicateKeyError(key)
                result[key] = value
            return result

        try:
            if path.stat().st_size > 4 * 1024 * 1024:
                self.add("error", f"{kind}.too_large", "JSON 文件超过 4 MiB", path)
                return None
            with path.open("r", encoding="utf-8-sig") as stream:
                value = json.load(stream, object_pairs_hook=reject_duplicates)
        except DuplicateKeyError as error:
            self.add("error", f"{kind}.duplicate_key", f"JSON 包含重复键：{error}", path)
            return None
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            self.add("error", f"{kind}.invalid_json", f"无法解析 JSON：{error}", path)
            return None
        if not isinstance(value, dict):
            self.add("error", f"{kind}.not_object", "JSON 根节点必须是对象", path)
            return None
        return value

    def _required_string(self, key: str) -> str:
        value = self.manifest.get(key)
        if not isinstance(value, str) or not value.strip():
            self.add("error", "manifest.required_string", f"字段 {key} 必须是非空字符串", self.manifest_path or "")
            return ""
        return value.strip()

    def _validate_manifest(self) -> None:
        self.plugin_id = self._required_string("id")
        self._required_string("name")
        version = self._required_string("version")
        if self.plugin_id and not ID_PATTERN.fullmatch(self.plugin_id):
            self.add("error", "manifest.invalid_id", "插件 ID 只能包含字母、数字、点、下划线和连字符，长度 1-128", self.manifest_path or "")
        if version and not SEMVER_PATTERN.fullmatch(version):
            self.add("error", "manifest.invalid_version", "version 必须是完整 SemVer（如 1.2.0）", self.manifest_path or "")

        api_version = self.manifest.get("apiVersion", 1)
        if isinstance(api_version, bool) or not isinstance(api_version, int) or api_version not in (1, 2):
            self.add("error", "manifest.invalid_api_version", "apiVersion 只能为 1 或 2", self.manifest_path or "")

        for key in ("capabilities", "permissions"):
            value = self.manifest.get(key, [])
            if not isinstance(value, list) or any(not isinstance(item, str) or not item.strip() for item in value):
                self.add("error", f"manifest.invalid_{key}", f"{key} 必须是非空字符串数组", self.manifest_path or "")
            elif len(value) != len(set(value)):
                self.add("error", f"manifest.duplicate_{key}", f"{key} 不能包含重复项", self.manifest_path or "")

        if self.runtime == "python":
            if not self.plugin_id.startswith("py_"):
                self.add("warning", "manifest.python_id_prefix", "Python 插件 ID 建议使用 py_ 前缀", self.manifest_path or "")
            qml = self.manifest.get("qml")
            if "barItem" in self.manifest.get("capabilities", []) and not isinstance(qml, str):
                self.add("error", "manifest.qml_missing", "barItem Python 插件必须声明 qml", self.manifest_path or "")
            self._validate_declared_path("qml", qml)
            self._validate_declared_path("settingsQml", self.manifest.get("settingsQml"))
            if not (self.plugin_dir / "main.py").is_file():
                self.add("error", "python.main_missing", "Python 插件缺少 main.py", self.plugin_dir / "main.py")
        else:
            entry = self.manifest.get("entry")
            if api_version == 2 and not isinstance(entry, dict):
                self.add("error", "manifest.entry_missing", "V2 原生插件必须声明 entry 对象", self.manifest_path or "")
            if isinstance(entry, dict):
                self._validate_declared_path(
                    "entry.backend", entry.get("backend"), allow_unbuilt_native=True)
                self._validate_declared_path("entry.qml", entry.get("qml"))
                self._validate_declared_path("entry.settingsQml", entry.get("settingsQml"))

    def _validate_declared_path(
        self, key: str, value: Any, *, allow_unbuilt_native: bool = False
    ) -> None:
        if value is None or value == "":
            return
        if not isinstance(value, str):
            self.add("error", "manifest.invalid_path", f"{key} 必须是相对路径字符串", self.manifest_path or "")
            return
        normalized = value.replace("\\", "/")
        pure = PurePosixPath(normalized)
        if pure.is_absolute() or ".." in pure.parts or ":" in normalized:
            self.add("error", "manifest.path_escape", f"{key} 必须留在插件目录内", self.manifest_path or "")
            return
        candidate = self.plugin_dir.joinpath(*pure.parts)
        try:
            candidate.resolve().relative_to(self.plugin_dir)
        except (OSError, ValueError):
            self.add("error", "manifest.path_escape", f"{key} 解析后越出插件目录", self.manifest_path or "")
            return
        if not candidate.is_file():
            if allow_unbuilt_native:
                self.add(
                    "warning", "native.backend_unbuilt",
                    "源码目录中尚无原生后端；DLL ABI/IID 需在构建产物上继续校验", candidate,
                )
            else:
                self.add("error", "manifest.path_missing", f"{key} 指向的文件不存在", candidate)

    def _validate_settings(self) -> None:
        schema = self.manifest.get("settings")
        if schema is None:
            return
        if not isinstance(schema, list):
            self.add("error", "settings.not_array", "settings 必须是分组数组", self.manifest_path or "")
            return
        seen_keys: set[str] = set()
        for group_index, group in enumerate(schema):
            if not isinstance(group, dict) or not isinstance(group.get("fields"), list):
                self.add("error", "settings.invalid_group", f"settings[{group_index}] 必须包含 fields 数组", self.manifest_path or "")
                continue
            for field_index, field in enumerate(group["fields"]):
                location = f"settings[{group_index}].fields[{field_index}]"
                if not isinstance(field, dict):
                    self.add("error", "settings.invalid_field", f"{location} 必须是对象", self.manifest_path or "")
                    continue
                key = field.get("key")
                field_type = field.get("type")
                if not isinstance(key, str) or not key:
                    self.add("error", "settings.invalid_key", f"{location}.key 必须是非空字符串", self.manifest_path or "")
                elif key in seen_keys:
                    self.add("error", "settings.duplicate_key", f"设置字段重复：{key}", self.manifest_path or "")
                else:
                    seen_keys.add(key)
                if field_type not in FIELD_TYPES:
                    self.add("error", "settings.invalid_type", f"{location}.type 不受支持：{field_type}", self.manifest_path or "")
                if "secret" in field and not isinstance(field["secret"], bool):
                    self.add("error", "settings.invalid_secret", f"{location}.secret 必须是布尔值", self.manifest_path or "")
                if field_type == "enum":
                    self._validate_enum(field, location)

    def _validate_enum(self, field: dict[str, Any], location: str) -> None:
        options = field.get("options")
        if not isinstance(options, list) or not options:
            self.add("error", "settings.enum_options", f"{location}.options 必须是非空数组", self.manifest_path or "")
            return
        values: list[str] = []
        for option in options:
            value = option.get("value") if isinstance(option, dict) else option
            if not isinstance(value, str) or not value:
                self.add("error", "settings.enum_value", f"{location} 的枚举值必须是非空字符串", self.manifest_path or "")
                continue
            values.append(value)
        if len(values) != len(set(values)):
            self.add("error", "settings.enum_duplicate", f"{location} 包含重复枚举值", self.manifest_path or "")
        if "default" in field and field["default"] not in values:
            self.add("error", "settings.enum_default", f"{location}.default 不在枚举稳定值中", self.manifest_path or "")

    def _qml_paths(self) -> Iterable[Path]:
        declared: list[str] = []
        if self.runtime == "python":
            declared.extend(value for value in (self.manifest.get("qml"), self.manifest.get("settingsQml")) if isinstance(value, str))
        else:
            entry = self.manifest.get("entry", {})
            if isinstance(entry, dict):
                declared.extend(value for value in (entry.get("qml"), entry.get("settingsQml")) if isinstance(value, str))
        if declared:
            return [self.plugin_dir / value for value in dict.fromkeys(declared)]
        candidates = sorted(self.plugin_dir.glob("*.qml"))
        # V1 原生清单没有 QML 路径。优先按插件 id/目录名匹配主文件，避免把纯辅助组件
        # （如 XuanHubGlyph.qml）误判成必须接受 plugin 注入的插件根。
        normalized_names = {
            re.sub(r"[^a-z0-9]", "", self.plugin_id.lower()),
            re.sub(r"[^a-z0-9]", "", self.plugin_dir.name.lower()),
        }
        matched = [path for path in candidates
                   if re.sub(r"[^a-z0-9]", "", path.stem.lower()) in normalized_names]
        return matched or candidates

    def _validate_qml(self) -> None:
        for path in self._qml_paths():
            if not path.is_file():
                continue
            try:
                text = path.read_text(encoding="utf-8-sig")
            except (OSError, UnicodeError) as error:
                self.add("error", "qml.unreadable", f"无法读取 QML：{error}", path)
                continue
            if not QML_ROOT_ITEM.search(text):
                self.add("warning", "qml.root_unknown", "未识别到受支持的 QML 根可视项，仍需 Smoke Test", path)
            if not QML_PLUGIN_PROPERTY.search(text):
                self.add("error", "qml.plugin_property_missing", "QML 根契约缺少 property var plugin", path)
            if not re.search(r"\b(?:width|implicitWidth)\s*:", text):
                self.add("warning", "qml.width_unverified", "未找到宽度声明，运行时可能得到零宽度", path)

    def _validate_i18n(self) -> None:
        directory = self.plugin_dir / "i18n"
        if not directory.exists():
            self.add("warning", "i18n.missing", "缺少 i18n/zh-CN.json 和 i18n/en-US.json")
            return
        locale_contexts: dict[str, set[str]] = {}
        for locale in ("zh-CN", "en-US"):
            path = directory / f"{locale}.json"
            if not path.is_file():
                self.add("error", "i18n.locale_missing", f"缺少 {locale}.json", path)
                continue
            data = self._read_json_object(path, "i18n")
            if data is None:
                continue
            if data.get("locale") != locale or not isinstance(data.get("contexts"), dict):
                self.add("error", "i18n.invalid_root", f"{locale}.json 的 locale/contexts 无效", path)
                continue
            contexts = data["contexts"]
            locale_contexts[locale] = set(contexts)
            prefix = f"plugin.{self.plugin_id}."
            for context, translations in contexts.items():
                if not context.startswith(prefix):
                    self.add("error", "i18n.context_escape", f"上下文必须以 {prefix} 开头：{context}", path)
                if not isinstance(translations, dict) or any(not isinstance(k, str) or not isinstance(v, str) for k, v in translations.items()):
                    self.add("error", "i18n.invalid_context", f"上下文 {context} 必须是字符串映射", path)
                elif locale == "en-US" and any(not value for value in translations.values()):
                    self.add("error", "i18n.english_missing", f"上下文 {context} 包含空英文翻译", path)
        if set(locale_contexts) == {"zh-CN", "en-US"} and locale_contexts["zh-CN"] != locale_contexts["en-US"]:
            self.add("error", "i18n.context_mismatch", "中英文语言包的上下文集合不一致", directory)

    def _scan_tree(self) -> None:
        for path in self.plugin_dir.rglob("*"):
            relative = path.relative_to(self.plugin_dir)
            if any(part in SUSPICIOUS_NAMES for part in relative.parts) or path.suffix.lower() in SUSPICIOUS_SUFFIXES:
                severity = "error" if "secrets.json" in relative.parts else "warning"
                self.add(severity, "tree.forbidden_artifact", "插件目录包含缓存、日志、密钥或 VCS 文件", path)
                continue
            if path.is_symlink():
                self.add("error", "tree.symlink", "插件目录不允许符号链接", path)
                continue
            if not path.is_file() or path.stat().st_size > 2 * 1024 * 1024:
                continue
            if path.suffix.lower() not in {".json", ".qml", ".py", ".md", ".txt"}:
                continue
            try:
                text = path.read_text(encoding="utf-8-sig")
            except (OSError, UnicodeError):
                continue
            if WINDOWS_ABSOLUTE_PATH.search(text) or POSIX_USER_PATH.search(text):
                self.add("error", "tree.user_absolute_path", "文件包含本机用户绝对路径", path)


def format_text(result: dict[str, Any]) -> str:
    plugin = result["plugin"]
    summary = result["summary"]
    mark = "PASS" if result["ok"] else "FAIL"
    lines = [
        f"[{mark}] {plugin['id'] or Path(plugin['path']).name} ({plugin['runtime']})",
        f"errors={summary['errors']} warnings={summary['warnings']}",
    ]
    for item in result["diagnostics"]:
        location = f" [{item['path']}]" if item["path"] else ""
        lines.append(f"{item['severity'].upper()} {item['code']}{location}: {item['message']}")
    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="xuanhub-plugin", description="XuanHub 插件开发者工具")
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate = subparsers.add_parser("validate", help="只读校验插件目录")
    validate.add_argument("plugin_dir", type=Path)
    validate.add_argument("--json", action="store_true", dest="json_output", help="输出稳定 JSON")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "validate":
        result = Validator(args.plugin_dir).validate()
        if args.json_output:
            print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
        else:
            print(format_text(result))
        return 0 if result["ok"] else 1
    return 2


if __name__ == "__main__":
    sys.exit(main())
