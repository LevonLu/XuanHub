# test_dependency_doctor.py — Python 依赖 doctor 的离线判定回归测试
from __future__ import annotations

import importlib.metadata
import platform
import struct
import unittest
from unittest import mock

from plugins_py import dependency_doctor


class _Distribution:
    def __init__(self, version: str, files: list[str]) -> None:
        self.version = version
        self.files = [importlib.metadata.PackagePath(path) for path in files]


class DependencyDoctorTests(unittest.TestCase):
    def test_reports_runtime_and_missing_package(self) -> None:
        result = dependency_doctor.diagnose({
            "version": ">=3.12,<4.0",
            "packages": [{
                "name": "xuanhub-package-that-does-not-exist",
                "import": "xuanhub_missing",
                "version": ">=1.0",
                "native": False,
            }],
        })
        self.assertFalse(result["ok"])
        self.assertEqual(result["runtime"]["version"], platform.python_version())
        self.assertEqual(result["runtime"]["pointerBits"], struct.calcsize("P") * 8)
        self.assertEqual(result["issues"][0]["code"], "package_missing")

    def test_detects_python_version_mismatch(self) -> None:
        result = dependency_doctor.diagnose({"version": "<2.0", "packages": []})
        self.assertFalse(result["ok"])
        self.assertIn("python_version_mismatch",
                      [issue["code"] for issue in result["issues"]])

    def test_detects_package_version_mismatch_without_importing_it(self) -> None:
        distribution = _Distribution("1.5.0", [])
        with mock.patch.object(importlib.metadata, "distribution", return_value=distribution), \
                mock.patch.object(importlib.util, "find_spec") as find_spec:
            result = dependency_doctor.diagnose({
                "version": ">=3.12",
                "packages": [{
                    "name": "demo",
                    "import": "demo",
                    "version": ">=2.0",
                    "native": False,
                }],
            })
        self.assertFalse(result["ok"])
        self.assertEqual(result["issues"][0]["code"], "package_version_mismatch")
        find_spec.assert_not_called()

    def test_detects_incompatible_native_abi_tag(self) -> None:
        current_cp = f"{platform.python_version_tuple()[0]}{platform.python_version_tuple()[1]}"
        wrong_cp = "312" if current_cp != "312" else "313"
        distribution = _Distribution("1.0.0", [f"demo/native.cp{wrong_cp}-win_amd64.pyd"])
        with mock.patch.object(importlib.metadata, "distribution", return_value=distribution), \
                mock.patch.object(importlib.util, "find_spec", return_value=object()):
            result = dependency_doctor.diagnose({
                "version": ">=3.12",
                "packages": [{
                    "name": "demo",
                    "import": "demo",
                    "version": "==1.0.0",
                    "native": True,
                }],
            })
        self.assertFalse(result["ok"])
        self.assertEqual(result["issues"][0]["code"],
                         "native_extension_incompatible")


if __name__ == "__main__":
    unittest.main()
