# test_package_plugin.py — 参考插件打包器的确定性与安全门禁测试
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

from scripts import package_plugin


def _write_python_plugin(directory: Path) -> None:
    (directory / "plugin.json").write_text(json.dumps({
        "id": "py_package_test",
        "name": "Package Test",
        "version": "1.2.3",
        "author": "XuanHub Tests",
        "apiVersion": 2,
        "qml": "Test.qml",
    }), encoding="utf-8")
    (directory / "main.py").write_text("def update(): return {}\n", encoding="utf-8")
    (directory / "Test.qml").write_text("import QtQuick\nItem {}\n", encoding="utf-8")


class PackagePluginTests(unittest.TestCase):
    def test_builds_deterministic_package_with_complete_hash_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            source = root_path / "source"
            source.mkdir()
            _write_python_plugin(source)
            first = root_path / "first.xuanplugin"
            second = root_path / "second.xuanplugin"
            first_result = package_plugin.package_plugin(source, first, "python")
            second_result = package_plugin.package_plugin(source, second, "python")
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first_result["sha256"], second_result["sha256"])

            with zipfile.ZipFile(first) as archive:
                self.assertEqual(archive.namelist(), sorted(archive.namelist()))
                package_manifest = json.loads(archive.read("plugin.json"))
                self.assertEqual(package_manifest["schemaVersion"], 1)
                self.assertEqual(package_manifest["manifest"], "payload/plugin.json")
                declared = {}
                for line in archive.read("manifest.sha256").decode("utf-8").splitlines():
                    digest, path = line.split("  ", 1)
                    declared[path] = digest
                expected = set(archive.namelist()) - {"manifest.sha256"}
                self.assertEqual(set(declared), expected)
                for path, digest in declared.items():
                    self.assertEqual(hashlib.sha256(archive.read(path)).hexdigest(), digest)

    def test_rejects_output_inside_source_and_sensitive_files(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            source = Path(root) / "source"
            source.mkdir()
            _write_python_plugin(source)
            with self.assertRaisesRegex(package_plugin.PackageError, "outside"):
                package_plugin.package_plugin(
                    source, source / "bad.xuanplugin", "python")

            (source / ".env.local").write_text("TOKEN=test", encoding="utf-8")
            with self.assertRaisesRegex(package_plugin.PackageError, "credential"):
                package_plugin.package_plugin(
                    source, Path(root) / "bad.xuanplugin", "python")

    def test_rejects_private_keys_and_links(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            source = root_path / "source"
            source.mkdir()
            _write_python_plugin(source)
            (source / "private.pem").write_text(
                "-----BEGIN PRIVATE KEY-----\nsecret\n", encoding="utf-8")
            with self.assertRaisesRegex(package_plugin.PackageError, "credential-like"):
                package_plugin.package_plugin(
                    source, root_path / "bad.xuanplugin", "python")
            (source / "private.pem").unlink()

            link = source / "linked.py"
            try:
                os.symlink(source / "main.py", link)
            except OSError:
                self.skipTest("当前环境不允许创建符号链接")
            with self.assertRaisesRegex(package_plugin.PackageError, "links|regular files"):
                package_plugin.package_plugin(
                    source, root_path / "link.xuanplugin", "python")

    def test_rejects_invalid_runtime_identity(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            source = root_path / "source"
            source.mkdir()
            _write_python_plugin(source)
            manifest = json.loads((source / "plugin.json").read_text(encoding="utf-8"))
            manifest["version"] = "1.2"
            (source / "plugin.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(package_plugin.PackageError, "SemVer"):
                package_plugin.package_plugin(
                    source, root_path / "bad.xuanplugin", "python")


if __name__ == "__main__":
    unittest.main()
