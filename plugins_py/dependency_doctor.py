# dependency_doctor.py — 在独立解释器中只读检查 Python 插件运行时依赖
from __future__ import annotations

import importlib.metadata
import importlib.util
import json
import platform
import re
import struct
import sys
import sysconfig
from typing import Any


_CONSTRAINT_ITEM = re.compile(r"^(==|!=|>=|<=|>|<)\s*(\d+(?:\.\d+){1,3})$")
_RELEASE = re.compile(r"^(\d+(?:\.\d+){1,3})")
_NATIVE_TAG = re.compile(r"\.cp(\d+)-([a-z0-9_]+)\.pyd$", re.IGNORECASE)


def _release_tuple(value: str) -> tuple[int, ...] | None:
    match = _RELEASE.match(value.strip())
    if not match:
        return None
    return tuple(int(part) for part in match.group(1).split("."))


def _compare(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    width = max(len(left), len(right))
    padded_left = left + (0,) * (width - len(left))
    padded_right = right + (0,) * (width - len(right))
    return (padded_left > padded_right) - (padded_left < padded_right)


def _matches(version: str, constraint: str) -> bool | None:
    if constraint.strip() == "*":
        return True
    actual = _release_tuple(version)
    if actual is None:
        return None
    for raw_item in constraint.split(","):
        match = _CONSTRAINT_ITEM.match(raw_item.strip())
        if not match:
            return None
        expected = _release_tuple(match.group(2))
        if expected is None:
            return None
        comparison = _compare(actual, expected)
        accepted = {
            "==": comparison == 0,
            "!=": comparison != 0,
            ">=": comparison >= 0,
            "<=": comparison <= 0,
            ">": comparison > 0,
            "<": comparison < 0,
        }[match.group(1)]
        if not accepted:
            return False
    return True


def _issue(code: str, message: str, suggestion: str,
           package: str = "") -> dict[str, Any]:
    result = {"code": code, "message": message, "suggestion": suggestion}
    if package:
        result["package"] = package
    return result


def _native_files(distribution: importlib.metadata.Distribution) -> list[str]:
    return [str(path).replace("\\", "/") for path in (distribution.files or [])
            if str(path).lower().endswith(".pyd")]


def _native_issues(name: str, distribution: importlib.metadata.Distribution) -> list[dict[str, Any]]:
    files = _native_files(distribution)
    if not files:
        return [_issue(
            "native_extension_missing",
            f"{name} 声明为原生扩展，但安装内容中没有 .pyd",
            "安装与当前 Python 版本及 Windows x64 匹配的 wheel",
            name,
        )]

    expected_cp = f"{sys.version_info.major}{sys.version_info.minor}"
    expected_platform = sysconfig.get_platform().lower().replace("-", "_").replace(".", "_")
    incompatible: list[str] = []
    for path in files:
        match = _NATIVE_TAG.search(path)
        if match and (match.group(1) != expected_cp
                      or match.group(2).lower() != expected_platform):
            incompatible.append(path)
    if incompatible:
        return [_issue(
            "native_extension_incompatible",
            f"{name} 包含与当前解释器不兼容的原生扩展",
            f"安装 cp{expected_cp}-{expected_platform} wheel",
            name,
        )]
    return []


def diagnose(payload: dict[str, Any]) -> dict[str, Any]:
    runtime_version = platform.python_version()
    pointer_bits = struct.calcsize("P") * 8
    machine = platform.machine() or "unknown"
    issues: list[dict[str, Any]] = []

    version_constraint = str(payload.get("version", ">=3.12"))
    runtime_match = _matches(runtime_version, version_constraint)
    if runtime_match is not True:
        issues.append(_issue(
            "python_version_mismatch",
            f"Python {runtime_version} 不满足 {version_constraint}",
            f"选择满足 {version_constraint} 的 Windows x64 Python",
        ))
    if pointer_bits != 64 or machine.lower() not in {"amd64", "x86_64"}:
        issues.append(_issue(
            "python_architecture_mismatch",
            f"当前解释器架构为 {machine}/{pointer_bits}-bit",
            "使用与 XuanHub 一致的 Windows x64 Python",
        ))

    for package in payload.get("packages", []):
        name = str(package.get("name", ""))
        import_name = str(package.get("import", ""))
        constraint = str(package.get("version", "*"))
        try:
            distribution = importlib.metadata.distribution(name)
            installed_version = distribution.version
        except importlib.metadata.PackageNotFoundError:
            issues.append(_issue(
                "package_missing",
                f"缺少 Python 包 {name}",
                f"安装满足 {constraint} 的 {name}，或提供离线 wheel",
                name,
            ))
            continue

        version_match = _matches(installed_version, constraint)
        if version_match is not True:
            code = "package_version_unknown" if version_match is None \
                else "package_version_mismatch"
            issues.append(_issue(
                code,
                f"{name} {installed_version} 不满足 {constraint}",
                f"安装满足 {constraint} 的 {name}",
                name,
            ))
            continue

        # 只探测顶层根，避免为了 doctor 执行第三方包的 __init__.py。
        import_root = import_name.split(".", 1)[0]
        if importlib.util.find_spec(import_root) is None:
            issues.append(_issue(
                "package_import_missing",
                f"{name} 已登记但无法找到导入根 {import_root}",
                f"重新安装 {name} 并检查解释器环境",
                name,
            ))
            continue
        if bool(package.get("native", False)):
            issues.extend(_native_issues(name, distribution))

    return {
        "ok": not issues,
        "runtime": {
            "version": runtime_version,
            "machine": machine,
            "pointerBits": pointer_bits,
            "platform": sysconfig.get_platform(),
        },
        "issues": issues,
    }


def main() -> int:
    try:
        payload = json.load(sys.stdin)
        if not isinstance(payload, dict):
            raise TypeError("doctor input must be an object")
        # Windows 控制台代码页不固定；ASCII JSON 避免 QProcess 侧把 GBK 当 UTF-8。
        json.dump(diagnose(payload), sys.stdout, ensure_ascii=True, separators=(",", ":"))
        return 0
    except Exception as error:  # doctor 自身错误也要返回稳定机器可读结果。
        json.dump({
            "ok": False,
            "runtime": {},
            "issues": [_issue("doctor_failed", str(error), "检查依赖声明和 Python 运行时")],
        }, sys.stdout, ensure_ascii=True, separators=(",", ":"))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
