# test_settings_presentation.py — 离屏验证设置页面布局与草稿交互
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SettingsPresentationTest(unittest.TestCase):
    def test_real_qml_editing_contracts(self):
        runner = os.environ.get("XUANHUB_QMLTESTRUNNER") or shutil.which("qmltestrunner")
        cache = ROOT / "build/default/CMakeCache.txt"
        if not runner and cache.is_file():
            match = re.search(r"^Qt6_DIR:PATH=(.+)$", cache.read_text(encoding="utf-8"), re.M)
            if match:
                candidate = Path(match[1].strip()).parents[2] / "bin/qmltestrunner.exe"
                if candidate.is_file():
                    runner = str(candidate)
        if not runner:
            self.skipTest("未找到 Qt qmltestrunner")
        with tempfile.TemporaryDirectory() as imports:
            module = Path(imports) / "XuanHub"
            module.mkdir()
            controls = sorted((ROOT / "app/qml").glob("Ui*.qml"))
            for control in controls:
                shutil.copy2(control, module)
            (module / "qmldir").write_text("module XuanHub\n" + "\n".join(
                ("singleton " if path.stem == "UiMetrics" else "")
                + path.stem + " 1.0 " + path.name for path in controls), encoding="utf-8")
            log = Path(imports) / "settings-presentation.txt"
            result = subprocess.run(
                [runner, "-import", imports, "-input", str(ROOT / "tests/qml/tst_SettingsPresentation.qml"),
                 "-o", str(log) + ",txt"],
                env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software",
                     "QT_QPA_FONTDIR": str(Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"),
                     "QT_QUICK_CONTROLS_STYLE": "FluentWinUI3"},
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
            output = result.stdout + result.stderr
            if log.is_file():
                output += log.read_text(encoding="utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, output)
            self.assertNotIn("Binding loop", output)


if __name__ == "__main__":
    unittest.main()
