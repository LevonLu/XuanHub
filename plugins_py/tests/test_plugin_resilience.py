# test_plugin_resilience.py — Python 子插件对异常输入/本地故障的回归测试
import importlib.util
from pathlib import Path
import unittest

from plugins_py.bridge_worker import PushRuntime


ROOT = Path(__file__).resolve().parents[1]


def load_plugin(directory: str, module_name: str):
    path = ROOT / directory / "main.py"
    spec = importlib.util.spec_from_file_location(module_name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PluginResilienceTests(unittest.TestCase):
    def test_heartrate_rejects_truncated_measurements(self):
        plugin = load_plugin("heartrate", "xuanhub_test_heartrate")
        self.assertIsNone(plugin._parse_hr(bytearray()))
        self.assertIsNone(plugin._parse_hr(bytearray([0x00])))
        self.assertIsNone(plugin._parse_hr(bytearray([0x01, 72])))
        self.assertEqual(plugin._parse_hr(bytearray([0x00, 72])), 72)
        self.assertEqual(plugin._parse_hr(bytearray([0x01, 0x34, 0x12])), 0x1234)

    def test_heartrate_pushes_measurements_without_polling(self):
        plugin = load_plugin("heartrate", "xuanhub_test_heartrate_push")
        pushes = PushRuntime()
        plugin._bridge_emit_values = pushes.emit_values
        plugin._cache["deviceId"] = "demo-device"

        for heart_rate in range(60, 160):
            plugin._hr_callback(None, bytearray([0x00, heart_rate]))
        messages, dropped = pushes.drain()

        self.assertTrue(plugin.PUSH_VALUES)
        self.assertEqual(dropped, 0)
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0]["type"], "values_patch")
        self.assertEqual(messages[0]["patch"]["hr"], 159)
        self.assertEqual(messages[0]["patch"]["state"], "connected")
        self.assertIsInstance(messages[0]["patch"]["lastSeen"], int)

    def test_todo_database_failure_keeps_plugin_renderable(self):
        plugin = load_plugin("todo", "xuanhub_test_todo")
        original_connect = plugin.sqlite3.connect

        def fail_connect(_path):
            raise plugin.sqlite3.OperationalError("simulated database failure")

        try:
            plugin.sqlite3.connect = fail_connect
            plugin.init({"prefix": "T", "barMode": "数量"})
            values = plugin.update()
        finally:
            plugin.sqlite3.connect = original_connect
        self.assertEqual(values["activeCount"], 0)
        self.assertEqual(values["prefix"], "T")
        self.assertEqual(values["lastErrorKey"], "databaseUnavailable")
        self.assertFalse(values["actionStates"]["clear_done"]["enabled"])
        self.assertIn("simulated database failure", values["lastErrorDetail"])
        plugin.invoke("add", ["不会写入的数据"])
        self.assertEqual(plugin.update()["lastErrorKey"], "databaseUnavailable")


if __name__ == "__main__":
    unittest.main()
