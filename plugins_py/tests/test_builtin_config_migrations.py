# test_builtin_config_migrations.py — 内置 Python 插件自有配置迁移回归
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_plugin(plugin_id: str):
    path = ROOT / plugin_id / "main.py"
    spec = importlib.util.spec_from_file_location(f"xuanhub_test_{plugin_id}", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuiltInConfigMigrationTests(unittest.TestCase):
    def test_countdown_migrates_chinese_and_once_modes(self) -> None:
        plugin = load_plugin("countdown")
        self.assertEqual(plugin.CONFIG_VERSION, 2)
        self.assertEqual(
            plugin.migrate_config(1, {"mode": "每日重复", "future": 7}),
            {"mode": "daily", "future": 7},
        )
        self.assertEqual(plugin.migrate_config(1, {"mode": "once"})["mode"], "date")
        self.assertFalse(plugin.validate_config({"mode": "指定日期"})["ok"])

    def test_todo_migrates_display_label_without_touching_user_data(self) -> None:
        plugin = load_plugin("todo")
        self.assertEqual(plugin.CONFIG_VERSION, 2)
        migrated = plugin.migrate_config(
            1, {"barMode": "最近一项", "prefix": "我的待办", "future": {"x": 1}}
        )
        self.assertEqual(migrated["barMode"], "latest")
        self.assertEqual(migrated["prefix"], "我的待办")
        self.assertEqual(migrated["future"], {"x": 1})
        self.assertFalse(plugin.validate_config({"barMode": "数量"})["ok"])

    def test_intime_migrates_minutes_and_visual_labels(self) -> None:
        plugin = load_plugin("intime")
        self.assertEqual(plugin.CONFIG_VERSION, 2)
        migrated = plugin.migrate_config(1, {
            "workMinutes": 30,
            "restMinutes": 6,
            "maskType": "纯色",
            "progressStyle": "环形进度条",
            "future": True,
        })
        self.assertEqual(migrated["workSeconds"], 1800)
        self.assertEqual(migrated["restSeconds"], 360)
        self.assertEqual(migrated["maskType"], "color")
        self.assertEqual(migrated["progressStyle"], "ring")
        self.assertTrue(migrated["future"])
        self.assertTrue(plugin.validate_config(migrated)["ok"])

    def test_unknown_source_version_is_rejected(self) -> None:
        for plugin_id in ("countdown", "todo", "intime"):
            with self.subTest(plugin_id=plugin_id):
                with self.assertRaises(ValueError):
                    load_plugin(plugin_id).migrate_config(0, {})


if __name__ == "__main__":
    unittest.main()
