# package_plugin.py — 将单个插件目录确定性打包为可离线校验的 .xuanplugin
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile
import unicodedata
import zipfile


MAXIMUM_ENTRIES = 2048
MAXIMUM_ARCHIVE_BYTES = 256 * 1024 * 1024
MAXIMUM_FILE_BYTES = 64 * 1024 * 1024
MAXIMUM_EXPANDED_BYTES = 512 * 1024 * 1024
_SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?"
    r"(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$"
)
_PLUGIN_ID = re.compile(r"^[A-Za-z0-9._-]{1,128}$")
_DEVICE = re.compile(r"^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$", re.IGNORECASE)
_PRIVATE_KEY = re.compile(br"-----BEGIN (?:[A-Z0-9 ]+ )?PRIVATE KEY-----")
_CREDENTIAL_PATTERNS = (
    re.compile(br"AKIA[0-9A-Z]{16}"),
    re.compile(br"gh[pousr]_[A-Za-z0-9]{36,255}"),
)
_FORBIDDEN_PARTS = {".git", ".hg", ".svn", "__pycache__"}
_FORBIDDEN_NAMES = {"secrets.json", "credentials.json", "xuanhub.lock"}
_FORBIDDEN_SUFFIXES = {".pyc", ".pyo", ".log", ".key", ".p12", ".pfx"}
_ZIP_TIME = (1980, 1, 1, 0, 0, 0)


class PackageError(ValueError):
    pass


def _is_link(path: Path) -> bool:
    is_junction = getattr(os.path, "isjunction", lambda _path: False)
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        attributes = 0
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return (path.is_symlink() or bool(is_junction(path))
            or bool(attributes & reparse_flag))


def _validate_relative_path(relative: str) -> None:
    if not relative or "\\" in relative or relative.startswith("/") or ":" in relative:
        raise PackageError(f"unsafe path: {relative}")
    if unicodedata.normalize("NFC", relative) != relative:
        raise PackageError(f"path must use NFC Unicode normalization: {relative}")
    for part in relative.split("/"):
        if not part or part in {".", ".."} or part.endswith((".", " ")):
            raise PackageError(f"unsafe path segment: {relative}")
        if any(ord(character) < 0x20 for character in part):
            raise PackageError(f"path contains a control character: {relative}")
        if _DEVICE.fullmatch(part.split(".", 1)[0]):
            raise PackageError(f"Windows device name is not allowed: {relative}")


def _scan_source(source: Path, output: Path) -> dict[str, bytes]:
    if _is_link(source) or not source.is_dir():
        raise PackageError("source must be a real directory, not a link")
    try:
        output.relative_to(source)
    except ValueError:
        pass
    else:
        raise PackageError("output must be outside the plugin source directory")

    payload: dict[str, bytes] = {}
    folded_paths: set[str] = set()
    expanded = 0
    for root_text, directory_names, file_names in os.walk(source, followlinks=False):
        root = Path(root_text)
        directory_names.sort()
        file_names.sort()
        for name in tuple(directory_names):
            directory = root / name
            relative = directory.relative_to(source).as_posix()
            _validate_relative_path(relative)
            if _is_link(directory):
                raise PackageError(f"links and junctions are not allowed: {relative}")
            if name.casefold() in _FORBIDDEN_PARTS:
                raise PackageError(f"development metadata is not allowed: {relative}")
        for name in file_names:
            path = root / name
            relative = path.relative_to(source).as_posix()
            _validate_relative_path(relative)
            if _is_link(path) or not stat.S_ISREG(path.stat(follow_symlinks=False).st_mode):
                raise PackageError(f"only regular files are allowed: {relative}")
            folded = relative.casefold()
            if folded in folded_paths:
                raise PackageError(f"case-insensitive duplicate path: {relative}")
            folded_paths.add(folded)
            lower_name = name.casefold()
            if (lower_name.startswith(".env") or lower_name in _FORBIDDEN_NAMES
                    or path.suffix.casefold() in _FORBIDDEN_SUFFIXES):
                raise PackageError(f"runtime or credential file is not allowed: {relative}")
            size = path.stat().st_size
            if size > MAXIMUM_FILE_BYTES:
                raise PackageError(f"file exceeds {MAXIMUM_FILE_BYTES} bytes: {relative}")
            expanded += size
            if expanded > MAXIMUM_EXPANDED_BYTES:
                raise PackageError("expanded package size exceeds the limit")
            data = path.read_bytes()
            if _PRIVATE_KEY.search(data) or any(pattern.search(data) for pattern in _CREDENTIAL_PATTERNS):
                raise PackageError(f"credential-like content is not allowed: {relative}")
            payload[f"payload/{relative}"] = data
    if not payload:
        raise PackageError("plugin source directory is empty")
    if len(payload) + 2 > MAXIMUM_ENTRIES:
        raise PackageError(f"package has more than {MAXIMUM_ENTRIES} entries")
    return payload


def _load_runtime_manifest(source: Path, runtime: str) -> tuple[dict[str, object], str]:
    manifest_name = "metadata.json" if runtime == "native" else "plugin.json"
    manifest_path = source / manifest_name
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError(f"cannot read {manifest_name}: {error}") from error
    if not isinstance(manifest, dict):
        raise PackageError(f"{manifest_name} root must be an object")
    plugin_id = manifest.get("id")
    version = manifest.get("version")
    if not isinstance(plugin_id, str) or not _PLUGIN_ID.fullmatch(plugin_id):
        raise PackageError("runtime manifest id is invalid")
    if not isinstance(version, str) or not _SEMVER.fullmatch(version):
        raise PackageError("runtime manifest version must be SemVer")
    return manifest, manifest_name


def _zip_info(path: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path, _ZIP_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | 0o644) << 16
    info.flag_bits |= 0x800
    return info


def package_plugin(source: Path, output: Path, runtime: str,
                   publisher: str = "") -> dict[str, object]:
    source = source.expanduser().resolve(strict=True)
    output = output.expanduser().resolve(strict=False)
    if output.suffix.casefold() != ".xuanplugin":
        raise PackageError("output extension must be .xuanplugin")
    output.parent.mkdir(parents=True, exist_ok=True)
    runtime_manifest, manifest_name = _load_runtime_manifest(source, runtime)
    files = _scan_source(source, output)
    plugin_id = str(runtime_manifest["id"])
    version = str(runtime_manifest["version"])
    package_manifest = {
        "schemaVersion": 1,
        "id": plugin_id,
        "version": version,
        "runtime": runtime,
        "manifest": f"payload/{manifest_name}",
    }
    effective_publisher = publisher.strip() or str(runtime_manifest.get("author", "")).strip()
    if effective_publisher:
        package_manifest["publisher"] = effective_publisher
    files["plugin.json"] = (
        json.dumps(package_manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    ordered_files = dict(sorted(files.items()))
    manifest_sha256 = b"".join(
        hashlib.sha256(data).hexdigest().encode("ascii") + b"  "
        + path.encode("utf-8") + b"\n"
        for path, data in ordered_files.items()
    )

    temporary_path: Path | None = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{output.name}.", suffix=".tmp", dir=output.parent)
        os.close(descriptor)
        temporary_path = Path(temporary_name)
        with zipfile.ZipFile(temporary_path, "w", allowZip64=False) as archive:
            archive_files = dict(ordered_files)
            archive_files["manifest.sha256"] = manifest_sha256
            for path, data in sorted(archive_files.items()):
                archive.writestr(_zip_info(path), data)
        if temporary_path.stat().st_size > MAXIMUM_ARCHIVE_BYTES:
            raise PackageError("created package exceeds the archive size limit")
        os.replace(temporary_path, output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    return {
        "ok": True,
        "path": str(output),
        "id": plugin_id,
        "version": version,
        "runtime": runtime,
        "files": len(ordered_files),
        "sha256": f"sha256:{digest}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="创建 XuanHub .xuanplugin 离线插件包")
    parser.add_argument("source", type=Path, help="现有插件运行目录")
    parser.add_argument("--runtime", choices=("native", "python"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--publisher", default="")
    parser.add_argument("--json", action="store_true", help="输出机器可读结果")
    arguments = parser.parse_args()
    try:
        result = package_plugin(
            arguments.source, arguments.output, arguments.runtime, arguments.publisher)
    except (OSError, PackageError) as error:
        if arguments.json:
            print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        else:
            print(f"打包失败：{error}")
        return 1
    if arguments.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    else:
        print(f"已创建 {result['path']}")
        print(f"{result['sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
