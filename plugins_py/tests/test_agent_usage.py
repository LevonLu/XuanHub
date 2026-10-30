# test_agent_usage.py — Agent 用量的周期、口径、后台生命周期与采集协议回归
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from datetime import datetime
from unittest.mock import patch

PLUGIN = Path(__file__).resolve().parents[1] / "agent_usage"
spec = importlib.util.spec_from_file_location("agent_usage_main", PLUGIN / "main.py")
monitor = importlib.util.module_from_spec(spec)
sys.path.insert(0, str(PLUGIN))
try:
    spec.loader.exec_module(monitor)
finally:
    sys.path.pop(0)


class AgentUsageTests(unittest.TestCase):
    def test_local_calendar_period_and_exclusive_end(self):
        now = datetime(2026, 10, 29, 15, 0)
        bounds = monitor.period_bounds("7d", now)
        self.assertEqual(bounds["fromMs"], int(datetime(2026, 10, 23).timestamp() * 1000))
        self.assertEqual(bounds["toMs"], int(datetime(2026, 10, 30).timestamp() * 1000))
        self.assertEqual(monitor.period_bounds("all", now)["fromMs"], 0)

    def test_account_scope_excluded_from_total_but_visible(self):
        usage = {"sources": [{"id": "codex", "scope": "local", "total_tokens": 10, "cost": 1},
                             {"id": "cursor", "scope": "account", "total_tokens": 90, "cost": 9}]}
        result = monitor.compose(usage, {}, {})
        self.assertEqual(result["totalTokens"], 10)
        self.assertEqual(len(result["agents"]), 2)
        self.assertEqual(monitor.compose(usage, {}, {"includeAccount": True})["totalTokens"], 100)

    def test_unknown_source_and_provider_alias_retained(self):
        result = monitor.compose({"sources": [{"id": "new-agent", "total_tokens": 4}]},
            {"providers": {"commandCode": {"configured": True, "status": "ok", "windows": []}}}, {})
        self.assertEqual({a["id"] for a in result["agents"]}, {"new-agent", "command-code"})
        self.assertEqual(result["agents"][0]["quota"]["status"], "unsupported")

    def test_popup_selection_preserves_totals_and_historical_data(self):
        usage = {"sources": [{"id": "codex", "total_tokens": 10, "cost": 1},
                             {"id": "claude", "total_tokens": 30, "cost": 3},
                             {"id": "cursor", "scope": "account", "total_tokens": 60, "cost": 6}],
                 "knownSources": ["gemini"]}
        quota = {"providers": {"commandCode": {"configured": True, "status": "ok", "windows": []}}}
        config = {"popupAgentMode": "custom", "popupAgent_codex": True, "popupAgent_gemini": True}
        result = monitor.compose(usage, quota, config)
        self.assertEqual({a["id"] for a in result["agents"]}, {"codex", "gemini"})
        self.assertEqual(result["hiddenAgentIds"], ["claude", "command-code", "cursor"])
        self.assertTrue(result["popupFiltered"])
        self.assertEqual((result["totalTokens"], result["totalCost"], result["agentCount"]), (40, 4, 3))
        self.assertEqual(monitor.compose(usage, quota, {**config, "includeAccount": True})["totalTokens"], 100)
        # 旧配置和自动模式保持原有显示，已取消勾选的项不会影响自动模式。
        for settings in ({}, {**config, "popupAgentMode": "all"}):
            self.assertEqual(len(monitor.compose(usage, quota, settings)["agents"]), 5)

    def test_popup_selection_handles_aliases_dynamic_backends_and_new_sources(self):
        ids = ["pi-openai", "pi-anthropic", "prime-agent-openai", "codex", "new-agent", "kilocode", "kimi-code"]
        usage = {"knownSources": ids}
        config = {"popupAgentMode": "custom", "popupAgent_pi": True,
                  "popupAgent_kilo-code": True, "popupAgent_kimi": True}
        self.assertEqual({a["id"] for a in monitor.compose(usage, {}, config)["agents"]},
                         {"pi-openai", "pi-anthropic", "kilocode", "kimi-code"})
        config.update(popupAgent_other=True, **{"popupAgent_prime-agent": True})
        self.assertEqual({a["id"] for a in monitor.compose(usage, {}, config)["agents"]}, set(ids) - {"codex"})

    def test_popup_empty_selection_and_show_all(self):
        usage = {"sources": [{"id": "codex", "total_tokens": 25}]}
        config = {"popupAgentMode": "custom", "showAll": True}
        result = monitor.compose(usage, {}, config)
        self.assertEqual(result["agents"], [])
        self.assertEqual(result["totalTokens"], 25)
        self.assertTrue(result["popupFiltered"])
        result = monitor.compose(usage, {}, {**config, "popupAgent_claude": True})
        self.assertEqual([a["id"] for a in result["agents"]], ["claude"])
        self.assertIsNone(result["agents"][0]["stats"])

    def test_popup_hot_update_preserves_collection_and_detail(self):
        service = monitor.Monitor({})
        service.payload["usage"] = {"sources": [{"id": "codex", "total_tokens": 50},
                                              {"id": "claude", "total_tokens": 10}]}
        service.captured["usage"] = "collected"
        service.request_detail("codex", "2026-10-01", "2026-10-29", "query")
        before = service.snapshot()["detail"]
        config = {"popupAgentMode": "custom", "popupAgent_codex": True}
        with patch.object(service, "cancel_processes") as cancel, patch.object(service, "refresh") as refresh:
            service.configure(config)
            self.assertEqual([a["id"] for a in service.snapshot()["agents"]], ["codex"])
            service.configure({**config, "popupAgentMode": "all"})
            self.assertEqual(len(service.snapshot()["agents"]), 2)
            service.configure({**config, "popupAgentMode": "active"})
            self.assertEqual(len(service.snapshot()["agents"]), 2)
            # 模拟设置保存后的 JSON 再读入，模式切换保留选择。
            service.configure(json.loads(json.dumps(config)))
            cancel.assert_not_called()
            refresh.assert_not_called()
        self.assertEqual(service.generation, 0)
        self.assertEqual(service.snapshot()["detail"], before)
        self.assertEqual(service.snapshot()["usageUpdated"], "collected")
        self.assertEqual(service.snapshot()["totalTokens"], 60)

    def test_popup_active_requires_period_tokens_not_quota_or_history(self):
        usage = {"sources": [{"id": "codex", "total_tokens": 10, "cost": 0},
                             {"id": "claude", "total_tokens": 0, "cost": 5},
                             {"id": "cursor", "total_tokens": 20, "scope": "account"},
                             {"id": "pi-openai", "total_tokens": 1},
                             {"id": "unknown-agent", "total_tokens": 2},
                             {"id": "kimi", "cost": 1}],
                 "knownSources": ["gemini"]}
        quota = {"providers": {"commandCode": {"configured": True, "status": "ok", "windows": [{"used": 80}]}}}
        for show_all in (False, True):
            with self.subTest(showAll=show_all):
                config = {"popupAgentMode": "active", "showAll": show_all, "popupAgent_claude": True}
                result = monitor.compose(usage, quota, config)
                self.assertEqual({a["id"] for a in result["agents"]}, {"codex", "cursor", "pi-openai", "unknown-agent"})
                self.assertEqual(result["popupAgentMode"], "active")
                self.assertTrue(result["popupFiltered"])
                self.assertEqual(result["totalTokens"], 13)
                self.assertEqual(result["totalCost"], 6)

    def test_popup_active_follows_period_and_new_collection(self):
        service = monitor.Monitor({"popupAgentMode": "active"})
        service.payload["usage"] = {"sources": [{"id": "codex", "total_tokens": 5}]}
        self.assertEqual([a["id"] for a in service.snapshot()["agents"]], ["codex"])
        service.configure({"popupAgentMode": "active", "period": "7d"})
        self.assertEqual(service.snapshot()["agents"], [])
        service.payload["usage"] = {"sources": [{"id": "claude", "total_tokens": 9}], "knownSources": ["codex"]}
        self.assertEqual([a["id"] for a in service.snapshot()["agents"]], ["claude"])
        service.payload["usage"] = {"sources": [{"id": "claude", "total_tokens": 0}], "knownSources": ["codex"]}
        self.assertEqual(service.snapshot()["agents"], [])

    def test_popup_schema_defaults_and_invalid_configuration(self):
        manifest = json.loads((PLUGIN / "plugin.json").read_text(encoding="utf-8"))
        fields = {f["key"]: f for group in manifest["settings"] for f in group["fields"]}
        config = {key: field["default"] for key, field in fields.items() if "default" in field}
        self.assertEqual(monitor.normalize_config(config), monitor.normalize_config({}))
        self.assertEqual({key.removeprefix("popupAgent_") for key in fields if key.startswith("popupAgent_")},
                         set(monitor.POPUP_AGENT_KEYS))
        service = monitor.Monitor({})
        for config in ({"popupAgentMode": "invalid"}, {"popupAgent_codex": "false"}, {"popupAgent_pi": 1}):
            with self.subTest(config=config), self.assertRaises(ValueError):
                service.configure(config)
            self.assertEqual(service.config["popupAgentMode"], "all")
            self.assertEqual(service.generation, 0)

    def test_configuration_validation_and_old_period_clear(self):
        service = monitor.Monitor({})
        service.payload["usage"] = {"sources": [{"id": "codex", "total_tokens": 50}]}
        service.captured["usage"] = "old"
        with self.assertRaises(ValueError):
            service.configure({"period": "invalid"})
        self.assertEqual(service.snapshot()["totalTokens"], 50)
        service.configure({"period": "30d"})
        self.assertEqual(service.snapshot()["totalTokens"], 0)
        self.assertEqual(service.snapshot()["usageUpdated"], "")
        self.assertEqual(monitor.normalize_config({"refreshSeconds": 1})["refreshSeconds"], 60)

    def test_snapshot_is_detached(self):
        service = monitor.Monitor({"showAll": True})
        result = service.snapshot()
        result["agents"][0]["name"] = "changed"
        self.assertNotEqual(service.snapshot()["agents"][0]["name"], "changed")

    def test_slow_old_generation_cannot_replace_new_configuration(self):
        entered, release = threading.Event(), threading.Event()
        service = monitor.Monitor({"quotaEnabled": False})

        def collect(lane, config, generation):
            if generation == 0:
                entered.set()
                release.wait(2)
            return {"sources": [{"id": "codex", "total_tokens": 100 if generation == 0 else 20}], "version": "test"}

        service.run = collect
        service.start()
        try:
            self.assertTrue(entered.wait(2))
            service.configure({"period": "7d", "quotaEnabled": False})
            release.set()
            deadline = time.monotonic() + 2
            while service.snapshot()["totalTokens"] != 20 and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(service.snapshot()["totalTokens"], 20)
        finally:
            release.set()
            service.stop()
        self.assertTrue(all(not t.is_alive() for t in service.threads))

    def test_failure_preserves_last_good_result(self):
        service = monitor.Monitor({"quotaEnabled": False})
        service.payload["usage"] = {"sources": [{"id": "codex", "total_tokens": 20}]}
        with patch.object(service, "run", side_effect=RuntimeError("timeout")):
            service.start()
            try:
                deadline = time.monotonic() + 2
                while service.status["usage"] != "timeout" and time.monotonic() < deadline:
                    time.sleep(0.01)
                self.assertEqual(service.snapshot()["totalTokens"], 20)
                self.assertEqual(service.status["usage"], "timeout")
            finally:
                service.stop()

    def test_stop_terminates_owned_process(self):
        service = monitor.Monitor({})
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        script_path = Path(temporary.name) / "wait.py"
        script_path.write_text("import time\ntime.sleep(120)\n", encoding="utf-8")
        process = subprocess.Popen([sys.executable, "-I", str(script_path)],
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        service.processes["usage"] = process
        try:
            service.stop()
            process.wait(timeout=2)
            self.assertIsNotNone(process.returncode)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    def test_runtime_missing_reports_stable_error(self):
        with patch.object(monitor.sys, "executable", ""):
            with self.assertRaisesRegex(RuntimeError, "python_missing"):
                monitor.resolve_runtime(monitor.normalize_config({}))

    @unittest.skipUnless(os.name == "nt", "需要 Windows")
    def test_job_closes_descendant_pipes_on_shutdown(self):
        job = monitor.Job()
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        script_path = Path(temporary.name) / "tree.py"
        ready_path = script_path.with_suffix(".ready")
        script_path.write_text("import sys, subprocess, time\nfrom pathlib import Path\nif len(sys.argv) == 1:\n sys.stdin.readline()\n subprocess.Popen([sys.executable, '-I', '-B', __file__, 'child'], stdout=sys.stdout, stderr=sys.stderr)\nelse:\n print('ready', flush=True)\n Path(__file__).with_suffix('.ready').touch()\ntime.sleep(120)\n", encoding="utf-8")
        process = subprocess.Popen([sys.executable, "-I", "-B", str(script_path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            job.attach(process)
            # 先加入 Job 再创建后代；等子进程确实就绪后才验证关闭，避免依赖解释器启动速度。
            process.stdin.write(b"start\n")
            process.stdin.flush()
            deadline = time.monotonic() + 30
            while not ready_path.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(.02)
            started = ready_path.exists()
            if started:
                with self.assertRaises(subprocess.TimeoutExpired):
                    process.communicate(timeout=.2)
            job.close()
            output, error = process.communicate(timeout=2)
            self.assertTrue(started, f"子进程未在期限内就绪；退出码 {process.returncode}；{error.decode(errors='replace')}")
            self.assertIn(b"ready", output)
        finally:
            job.close()
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=2)

    def test_python_collector_contracts_without_node(self):
        with tempfile.TemporaryDirectory() as tmp:
            request = {"lane": "detail", "cacheDir": tmp, "fromMs": 0, "toMs": 1793404800000, "sourceId": "codex"}
            result = subprocess.run([sys.executable, "-I", "-B", str(PLUGIN / "collector.py")],
                                    input=json.dumps(request), capture_output=True, text=True, encoding="utf-8", timeout=30,
                                    env={**os.environ, "PATH": ""},
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), {"ok": False, "error": "data_not_ready"})

    def test_detail_dates_include_the_entire_last_day(self):
        query = monitor.detail_query("codex", "2026-10-01", "2026-10-29", "request-1")
        self.assertEqual(query["fromMs"], int(datetime(2026, 10, 1).timestamp() * 1000))
        self.assertEqual(query["toMs"], int(datetime(2026, 10, 30).timestamp() * 1000))
        for start, end in [("2026-02-30", "2026-03-01"), ("2026-10-30", "2026-10-29"), ("2026-1-1", "2026-10-29")]:
            with self.subTest(start=start), self.assertRaises(ValueError):
                monitor.detail_query("codex", start, end, "request")
        self.assertEqual(monitor.default_detail_dates("7d", datetime(2026, 10, 29)), {"from": "2026-10-23", "to": "2026-10-29"})

    def test_historical_tools_remain_available_for_detail(self):
        result = monitor.compose({"sources": [], "knownSources": ["claude", "cursor"]}, {}, {})
        self.assertEqual(result["totalTokens"], 0)
        self.assertEqual({a["id"] for a in result["agents"]}, {"claude", "cursor"})
        self.assertTrue(all(a["stats"] is None for a in result["agents"]))
        self.assertEqual(next(a for a in result["agents"] if a["id"] == "cursor")["scope"], "account")

    def test_detail_is_async_and_latest_request_wins(self):
        entered, release = threading.Event(), threading.Event()
        service = monitor.Monitor({})

        def collect(lane, config, generation, query):
            self.assertEqual(lane, "detail")
            if query["requestId"] == "first":
                entered.set()
                release.wait(2)
            return {"sourceId": query["sourceId"], "totals": {"total_tokens": 7}}

        service.run = collect
        service.threads[-1].start()
        try:
            started = time.monotonic()
            ticket = service.request_detail("codex", "2026-10-01", "2026-10-29", "first")
            self.assertTrue(ticket["queued"])
            self.assertLess(time.monotonic() - started, 0.2)
            self.assertTrue(entered.wait(2))
            service.request_detail("claude", "2026-10-23", "2026-10-29", "second")
            self.assertIsNone(service.snapshot()["detail"]["data"])
            release.set()
            deadline = time.monotonic() + 2
            while service.snapshot()["detail"]["status"] != "ok" and time.monotonic() < deadline:
                time.sleep(0.01)
            detail = service.snapshot()["detail"]
            self.assertEqual(detail["requestId"], "second")
            self.assertEqual(detail["data"]["sourceId"], "claude")
            service.configure({"period": "7d"})
            self.assertEqual(service.snapshot()["detail"]["status"], "cancelled")
            self.assertIsNone(service.snapshot()["detail"]["data"])
        finally:
            release.set()
            service.stop()
        self.assertFalse(service.threads[-1].is_alive())

    def test_invalid_detail_does_not_replace_valid_request(self):
        service = monitor.Monitor({})
        service.request_detail("codex", "2026-10-01", "2026-10-29", "valid")
        with self.assertRaises(ValueError):
            service.request_detail("codex", "2026-11-01", "2026-10-29", "invalid")
        self.assertEqual(service.snapshot()["detail"]["requestId"], "valid")


if __name__ == "__main__":
    unittest.main()
