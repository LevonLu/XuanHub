# test_countdown.py — 倒计时悬浮提示、发薪日规则与原有显示回归
from datetime import date, datetime
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


PLUGIN_DIR = Path(__file__).resolve().parents[1] / "countdown"
spec = importlib.util.spec_from_file_location("countdown_under_test", PLUGIN_DIR / "main.py")
countdown = importlib.util.module_from_spec(spec)
spec.loader.exec_module(countdown)


class CountdownTest(unittest.TestCase):
    def setUp(self):
        countdown.init({})
        countdown.set_locale("zh-CN")

    def payday(self, today, day=10, holidays="", workdays=""):
        return countdown._next_payday(date.fromisoformat(today), day,
                                     countdown._date_list(holidays), countdown._date_list(workdays))

    def test_weekend_is_advanced(self):
        result = self.payday("2026-01-01")
        self.assertEqual(result["paydayDate"], "2026-01-09")
        self.assertEqual(result["paydayDays"], 8)

    def test_official_makeup_weekends_are_workdays(self):
        self.assertEqual(self.payday("2026-05-01")["paydayDate"], "2026-05-09")
        self.assertEqual(self.payday("2026-10-01")["paydayDate"], "2026-10-10")

    def test_holiday_is_advanced_across_the_full_break(self):
        self.assertEqual(self.payday("2026-02-01", 20)["paydayDate"], "2026-02-14")
        self.assertEqual(self.payday("2026-10-01", 5)["paydayDate"], "2026-11-05")

    def test_actual_payday_not_nominal_day_controls_rollover(self):
        self.assertEqual(self.payday("2026-05-09")["paydayDays"], 0)
        self.assertEqual(self.payday("2026-05-10")["paydayDate"], "2026-06-10")
        self.assertEqual(self.payday("2026-02-15", 20)["paydayDate"], "2026-03-20")

    def test_adjustment_can_cross_a_month(self):
        self.assertEqual(self.payday("2026-01-29", 1)["paydayDate"], "2026-01-30")
        self.assertEqual(self.payday("2026-01-31", 1)["paydayDate"], "2026-02-28")

    def test_short_month_uses_last_day(self):
        self.assertEqual(self.payday("2026-02-01", 31)["paydayDate"], "2026-02-28")

    def test_unknown_year_is_not_treated_as_weekdays_only(self):
        self.assertEqual(self.payday("2026-12-31", 1),
                         {"paydayStatus": "calendarUnavailable", "paydayCalendarYear": 2027})
        self.assertEqual(self.payday("2026-01-01", 1)["paydayDate"], "2026-01-30")

    def test_company_overrides_official_calendar(self):
        self.assertEqual(self.payday("2026-05-01", holidays="2026-05-09")["paydayDate"], "2026-05-08")
        self.assertEqual(self.payday("2026-05-01", workdays="2026-05-10")["paydayDate"], "2026-05-10")
        self.assertEqual(self.payday("2026-05-01", 1, workdays="2026-05-01")["paydayDate"], "2026-05-01")

    def test_invalid_config_rejects_without_changing_current_values(self):
        countdown.set_config({"tooltipText": "工资 {payday}", "paydayDay": 10})
        for invalid in ({"paydayDay": True}, {"paydayDay": 0}, {"paydayDay": 32},
                        {"paydayDay": 10.5}, {"tooltipText": 42}, {"tooltipText": "a" * 4001},
                        {"paydayHolidays": "2026-02-30"}, {"paydayWorkdays": ["2026-05-10"]},
                        {"paydayHolidays": "2026-05-09", "paydayWorkdays": "2026-05-09"}):
            with self.subTest(invalid=invalid):
                self.assertFalse(countdown.validate_config(invalid)["ok"])
                with self.assertRaises(ValueError):
                    countdown.set_config(invalid)
                self.assertEqual(countdown._config["tooltipText"], "工资 {payday}")

    def test_plain_text_does_not_need_calendar(self):
        countdown.set_config({"tooltipText": "<b>自定义</b>\n第二行 {unknown}"})
        values = countdown._tooltip_values(date(2030, 1, 1))
        self.assertEqual(values["tooltipTemplate"], "<b>自定义</b>\n第二行 {unknown}")
        self.assertEqual(values["paydayStatus"], "unused")

    def test_tooltip_survives_finished_and_invalid_countdown(self):
        for config in ({}, {"mode": "date", "targetDateTime": "2026-01-01 00:00"},
                       {"targetTime": "invalid"}, {"mode": "date", "targetDateTime": "invalid"}):
            countdown.set_config({**config, "tooltipText": "下次 {payday} / {paydayDays}"})
            with patch.object(countdown, "datetime") as clock:
                clock.now.return_value = datetime(2026, 11, 2, 12)
                clock.strptime.side_effect = datetime.strptime
                values = countdown.update()
            self.assertEqual(values["paydayDate"], "2026-11-10")
            self.assertEqual(values["paydayDays"], 8)

    def test_original_countdown_and_hot_config_remain_available(self):
        with patch.object(countdown, "datetime") as clock:
            clock.now.return_value = datetime(2026, 11, 2, 17)
            self.assertEqual(countdown.update()["remainingSeconds"], 3600)
            self.assertEqual(countdown.update()["tooltipTemplate"], "")
            countdown.set_config({"targetTime": "16:00", "reachedText": "完成", "tooltipText": "提示"})
            values = countdown.update()
            self.assertTrue(values["reached"])
            self.assertEqual(values["text"], "完成")
            self.assertEqual(values["tooltipTemplate"], "提示")


class CountdownQmlTest(unittest.TestCase):
    def test_qml_hover_interaction(self):
        runner = os.environ.get("XUANHUB_QMLTESTRUNNER") or shutil.which("qmltestrunner")
        root = PLUGIN_DIR.parents[1]
        cache = root / "build/default/CMakeCache.txt"
        if not runner and cache.is_file():
            match = re.search(r"^Qt6_DIR:PATH=(.+)$", cache.read_text(encoding="utf-8"), re.M)
            if match:
                candidate = Path(match[1].strip()).parents[2] / "bin/qmltestrunner.exe"
                if candidate.is_file():
                    runner = str(candidate)
        if not runner:
            self.skipTest("未找到 Qt qmltestrunner；Python 逻辑测试不依赖 Qt")
        with tempfile.TemporaryDirectory() as imports:
            module = Path(imports) / "XuanHub"
            module.mkdir()
            # 使用实际控件源文件，避免为 Python 插件的设置页验收编译宿主。
            controls = ("UiMetrics", "UiCard", "UiComboBox")
            for control in controls:
                shutil.copy2(root / "app/qml" / (control + ".qml"), module)
            (module / "qmldir").write_text("module XuanHub\n" + "\n".join(
                ("singleton " if name == "UiMetrics" else "") + name + " 1.0 " + name + ".qml"
                for name in controls), encoding="utf-8")
            for test in ("tst_CountdownTooltip.qml", "tst_CountdownSettings.qml"):
                result = subprocess.run(
                    [runner, "-import", imports, "-input", str(Path(__file__).with_name("qml") / test)],
                    env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software",
                         "QT_QUICK_CONTROLS_STYLE": "Basic"},
                    capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn("Binding loop", result.stdout + result.stderr)


class CountdownWorkerTest(unittest.TestCase):
    def test_real_worker_lifecycle_and_config_acknowledgement(self):
        from scripts.xuanhub_plugin import WorkerHarness

        # CLI test 当前未传递目标配置版本；直接复用同一 Harness 验证 V2 插件，
        # 不降级插件 Manifest，也不改动通用 CLI 来掩盖版本不匹配。
        for cycle in range(2):
            with self.subTest(cycle=cycle):
                worker = WorkerHarness(PLUGIN_DIR, PLUGIN_DIR.parent / "bridge_worker.py", "py_countdown", 10)
                try:
                    worker.start()
                    response = worker.request("init", {"configVersion": 2, "targetConfigVersion": 2,
                        "locale": "zh-CN", "config": {"tooltipText": "工资 {payday} / {paydayDays}"}})
                    self.assertTrue(response["ok"], response)
                    self.assertIn("set_config", response["result"]["capabilities"])
                    self.assertIn("tooltipTemplate", worker.request("update", {})["result"])
                    self.assertTrue(worker.request("set_config", {"config": {"tooltipText": "新提示"}})["ok"])
                    self.assertEqual(worker.request("update", {})["result"]["tooltipTemplate"], "新提示")
                    self.assertFalse(worker.request("set_config", {"config": {"paydayDay": 0}})["ok"])
                    self.assertEqual(worker.request("update", {})["result"]["tooltipTemplate"], "新提示")
                    self.assertTrue(worker.request("set_locale", {"locale": "en-US"})["ok"])
                    self.assertTrue(worker.request("stop", {})["ok"])
                    worker.wait_for_clean_exit()
                finally:
                    worker.close()


if __name__ == "__main__":
    unittest.main()
