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
            # 运行实际 schema 枚举编辑器，而非复制实现：Loader 后注入 field 时，
            # options 模型重置不能把已保存的 dusk/rain 改成列表第一个选项。
            settings_source = (ROOT / "app/qml/SettingsWindow.qml").read_text(encoding="utf-8")
            enum_component = re.search(
                r'Component\s*\{\s*id: enumFieldComp\b[\s\S]*?(?=\n\s*Component\s*\{)', settings_source)
            self.assertIsNotNone(enum_component)
            enum_test = Path(imports) / "tst_SavedEnum.qml"
            enum_test.write_text('''import QtQuick
import QtTest
import XuanHub
TestCase {
 id: root; name: "SavedSchemaEnum"; when: windowShown; width: 400; height: 200
 function draftValue(key, fallback) { return key === "timeMode" ? "dusk" : "rain" }
 QtObject { id: settingsVM; property QtObject pluginProxy: QtObject { function setValue(key, value) {} } }
 ''' + enum_component.group(0) + '''
 function test_late_field_injection_keeps_saved_selection() {
  const editor = createTemporaryObject(enumFieldComp, root)
  verify(editor !== null)
  editor.field = {key: "timeMode", default: "auto", options: [
   {value: "auto", label: "Auto"}, {value: "day", label: "Day"}, {value: "dusk", label: "Dusk"}]}
  tryCompare(editor, "currentValue", "dusk")
  editor.field = {key: "weather", default: "clear", options: [
   {value: "clear", label: "Clear"}, {value: "rain", label: "Rain"}]}
  tryCompare(editor, "currentValue", "rain")
 }
}''', encoding="utf-8")
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
            enum_result = subprocess.run(
                [runner, "-import", imports, "-input", str(enum_test), "-o", str(log) + ",txt"],
                env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software",
                     "QT_QUICK_CONTROLS_STYLE": "FluentWinUI3"},
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15)
            self.assertEqual(enum_result.returncode, 0, enum_result.stderr + log.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
