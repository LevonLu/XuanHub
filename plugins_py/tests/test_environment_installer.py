# test_environment_installer.py — 私有环境离线 wheel 安装回归测试
from __future__ import annotations

import csv
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

from plugins_py import environment_installer


def _write_wheel(directory: Path) -> Path:
    wheel = directory / "xuanhub_demo-1.0.0-py3-none-any.whl"
    files = {
        "xuanhub_demo/__init__.py": "VALUE = 42\n",
        "xuanhub_demo-1.0.0.dist-info/METADATA": (
            "Metadata-Version: 2.1\nName: xuanhub-demo\nVersion: 1.0.0\n"),
        "xuanhub_demo-1.0.0.dist-info/WHEEL": (
            "Wheel-Version: 1.0\nGenerator: XuanHub test\n"
            "Root-Is-Purelib: true\nTag: py3-none-any\n"),
    }
    record = io.StringIO()
    writer = csv.writer(record, lineterminator="\n")
    for name in files:
        writer.writerow((name, "", ""))
    writer.writerow(("xuanhub_demo-1.0.0.dist-info/RECORD", "", ""))
    files["xuanhub_demo-1.0.0.dist-info/RECORD"] = record.getvalue()
    with zipfile.ZipFile(wheel, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    return wheel


class EnvironmentInstallerTests(unittest.TestCase):
    def test_rejects_empty_wheel_directory_without_creating_environment(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            environment = root_path / "environment"
            wheels = root_path / "wheels"
            wheels.mkdir()
            with self.assertRaisesRegex(ValueError, "missing or empty"):
                environment_installer.install({
                    "environment": str(environment),
                    "wheels": str(wheels),
                    "requirements": ["xuanhub-demo==1.0.0"],
                })
            self.assertFalse(environment.exists())

    def test_installs_pure_wheel_into_private_environment_offline(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            wheels = root_path / "wheels"
            wheels.mkdir()
            _write_wheel(wheels)
            environment = root_path / "environment"
            result = environment_installer.install({
                "environment": str(environment),
                "wheels": str(wheels),
                "requirements": ["xuanhub-demo==1.0.0"],
            })
            self.assertTrue(result["ok"])
            private_python = Path(result["python"])
            completed = subprocess.run(
                [str(private_python), "-c", "import xuanhub_demo; print(xuanhub_demo.VALUE)"],
                capture_output=True, text=True, timeout=30, check=True)
            self.assertEqual(completed.stdout.strip(), "42")


if __name__ == "__main__":
    unittest.main()
