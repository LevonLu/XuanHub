# test_xuanhub_plugin_cli.py — 开发者 CLI 静态校验与 JSON 协议测试
from __future__ import annotations

import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]
CLI_PATH = REPOSITORY / "scripts" / "xuanhub_plugin.py"
SPEC = importlib.util.spec_from_file_location("xuanhub_plugin", CLI_PATH)
assert SPEC and SPEC.loader
CLI = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CLI
SPEC.loader.exec_module(CLI)


def write_plugin(root: Path, *, plugin_id: str = "py_demo") -> None:
    (root / "plugin.json").write_text(json.dumps({
        "id": plugin_id,
        "name": "Demo",
        "version": "1.2.3",
        "apiVersion": 2,
        "capabilities": ["barItem", "settings"],
        "permissions": [],
        "qml": "Demo.qml",
        "settings": [{"group": "通用", "fields": [{
            "key": "mode", "type": "enum", "default": "daily",
            "options": [{"value": "daily", "label": "每日"}],
        }]}],
    }, ensure_ascii=False), encoding="utf-8")
    (root / "main.py").write_text("def update():\n    return {}\n", encoding="utf-8")
    (root / "Demo.qml").write_text(
        "import QtQuick\nItem { property var plugin; width: 40; height: 40 }\n",
        encoding="utf-8",
    )
    i18n = root / "i18n"
    i18n.mkdir()
    contexts = {f"plugin.{plugin_id}.metadata": {"Demo": "Demo"}}
    for locale in ("zh-CN", "en-US"):
        (i18n / f"{locale}.json").write_text(json.dumps({
            "locale": locale, "contexts": contexts,
        }, ensure_ascii=False), encoding="utf-8")


class ValidatorTest(unittest.TestCase):
    def test_valid_plugin_and_json_protocol(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "validate", str(root), "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            result = json.loads(completed.stdout)
            self.assertEqual(result["schemaVersion"], 1)
            self.assertEqual(result["command"], "validate")
            self.assertTrue(result["ok"])
            self.assertEqual(result["plugin"]["id"], "py_demo")

    def test_reports_stable_codes_for_contract_errors(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root, plugin_id="py_bad")
            (root / "Demo.qml").write_text("import QtQuick\nItem { height: 40 }\n", encoding="utf-8")
            manifest = json.loads((root / "plugin.json").read_text(encoding="utf-8"))
            manifest["settings"][0]["fields"][0]["default"] = "missing"
            (root / "plugin.json").write_text(json.dumps(manifest), encoding="utf-8")
            result = CLI.Validator(root).validate()
            codes = {item["code"] for item in result["diagnostics"]}
            self.assertFalse(result["ok"])
            self.assertIn("qml.plugin_property_missing", codes)
            self.assertIn("settings.enum_default", codes)

    def test_rejects_duplicate_json_keys_and_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "plugin.json").write_text('{"id":"py_a","id":"py_b"}', encoding="utf-8")
            (root / "debug.log").write_text("token", encoding="utf-8")
            result = CLI.Validator(root).validate()
            codes = {item["code"] for item in result["diagnostics"]}
            self.assertIn("manifest.duplicate_key", codes)
            self.assertIn("tree.forbidden_artifact", codes)

    def test_rejects_context_escape_and_user_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            bad = {"locale": "en-US", "contexts": {"xuanhub.settings": {"A": "B"}}}
            (root / "i18n" / "en-US.json").write_text(json.dumps(bad), encoding="utf-8")
            (root / "README.md").write_text("C:\\Users\\developer\\secret", encoding="utf-8")
            result = CLI.Validator(root).validate()
            codes = {item["code"] for item in result["diagnostics"]}
            self.assertIn("i18n.context_escape", codes)
            self.assertIn("tree.user_absolute_path", codes)

    def test_uses_authoritative_host_probe_when_available(self) -> None:
        probe_text = os.environ.get("XUANHUB_PLUGIN_CONTRACT_PROBE", "")
        if not probe_text or not Path(probe_text).is_file():
            self.skipTest("host contract probe is not built")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            manifest_path = root / "plugin.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["minHostVersion"] = "99.0.0"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            result = CLI.Validator(root, Path(probe_text)).validate()
            self.assertFalse(result["ok"])
            self.assertIn("host.contract_rejected",
                          {item["code"] for item in result["diagnostics"]})

    def test_sdk_documented_commands_are_parser_backed(self) -> None:
        expected = {"create", "validate", "test", "run", "doctor", "package"}
        documented: set[str] = set()
        for path in (REPOSITORY / "docs" / "plugin-sdk-v2.md",
                     REPOSITORY / "plugins_py" / "README.md"):
            documented.update(re.findall(
                r"python scripts/xuanhub_plugin\.py ([a-z]+)",
                path.read_text(encoding="utf-8-sig")))
        self.assertEqual(documented, expected)

        examples = {
            "create": ["create", "target", "--runtime", "python",
                       "--id", "py_target", "--name", "Target"],
            "validate": ["validate", "target"],
            "test": ["test", "target"],
            "run": ["run", "target"],
            "doctor": ["doctor", "target"],
            "package": ["package", "target", "--output", "target.xuanplugin"],
        }
        parser = CLI.build_parser()
        for command, arguments in examples.items():
            self.assertEqual(parser.parse_args(arguments).command, command)


class CreateTemplateTest(unittest.TestCase):
    def test_creates_python_template_that_passes_validator(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "py_created"
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "create", str(destination),
                 "--runtime", "python", "--id", "py_created",
                 "--name", "Created", "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            result = json.loads(completed.stdout)
            self.assertEqual(result["command"], "create")
            self.assertTrue(CLI.Validator(destination).validate()["ok"])
            refused = subprocess.run(
                [sys.executable, str(CLI_PATH), "create", str(destination),
                 "--runtime", "python", "--id", "py_created", "--name", "Created"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(refused.returncode, 1)

    def test_creates_native_template_that_passes_source_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "native_created"
            created = CLI.create_plugin(
                destination, "native", "native_created", "Native Created", "Tests")
            self.assertTrue(created["ok"])
            validation = CLI.Validator(destination).validate()
            self.assertTrue(validation["ok"], validation["diagnostics"])
            self.assertEqual(validation["plugin"]["runtime"], "native")


class PackageCommandTest(unittest.TestCase):
    def test_packages_valid_plugin_through_pf09_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            write_plugin(source)
            output = root / "py_demo-1.2.3.xuanplugin"
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "package", str(source),
                 "--output", str(output), "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            result = json.loads(completed.stdout)
            self.assertTrue(result["ok"])
            self.assertEqual(result["command"], "package")
            self.assertEqual(result["package"]["signatureStatus"], "unsigned")
            self.assertTrue(output.is_file())

    def test_refuses_artifact_before_packaging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            write_plugin(source)
            (source / "runtime.log").write_text("secret", encoding="utf-8")
            output = root / "rejected.xuanplugin"
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "package", str(source),
                 "--output", str(output), "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 1)
            result = json.loads(completed.stdout)
            self.assertFalse(result["ok"])
            self.assertFalse(output.exists())


class DoctorCommandTest(unittest.TestCase):
    def test_checks_python_runtime_without_qt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "doctor", str(root), "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            result = json.loads(completed.stdout)
            self.assertTrue(result["ok"])
            self.assertEqual(result["command"], "doctor")
            self.assertEqual(result["doctor"]["runtime"]["pointerBits"], 64)

    def test_reports_missing_python_distribution_with_stable_code(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            manifest_path = root / "plugin.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["python"] = {
                "version": ">=3.12", "packages": [{
                    "name": "xuanhub-package-that-does-not-exist",
                    "import": "xuanhub_package_that_does_not_exist",
                    "version": "*",
                }],
            }
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "doctor", str(root), "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 1)
            result = json.loads(completed.stdout)
            self.assertIn("doctor.package_missing",
                          {item["code"] for item in result["diagnostics"]})


class WorkerHarnessTest(unittest.TestCase):
    def test_runs_protocol_v2_lifecycle_without_qt(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            (root / "main.py").write_text(
                "locale = 'unset'\n"
                "def init(config):\n    pass\n"
                "def update():\n    return {'locale': locale}\n"
                "def set_locale(value):\n    global locale\n    locale = value\n"
                "def invoke(name, args):\n    return sum(args)\n"
                "def stop():\n    pass\n",
                encoding="utf-8",
            )
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "test", str(root), "--json",
                 "--invoke", "sum", "--invoke-args", "[2,3]", "--repeat", "2"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            result = json.loads(completed.stdout)
            self.assertTrue(result["ok"])
            self.assertEqual(len(result["steps"]), 12)
            self.assertEqual({step["cycle"] for step in result["steps"]}, {1, 2})

    def test_reports_plugin_exception(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_plugin(root)
            (root / "main.py").write_text(
                "def update():\n    raise RuntimeError('boom')\n",
                encoding="utf-8",
            )
            completed = subprocess.run(
                [sys.executable, str(CLI_PATH), "test", str(root), "--json"],
                check=False, capture_output=True, text=True, encoding="utf-8",
            )
            self.assertEqual(completed.returncode, 1)
            result = json.loads(completed.stdout)
            self.assertFalse(result["ok"])
            self.assertEqual(result["diagnostics"][-1]["code"], "test.worker_failed")


if __name__ == "__main__":
    unittest.main()
