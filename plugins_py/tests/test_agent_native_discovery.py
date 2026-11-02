# test_agent_native_discovery.py — 工具目录、压缩归档与账号统计的隔离回归
from __future__ import annotations

import base64
import hashlib
import importlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "agent_usage"))
from native import account_usage as accounts
from native import compression, discovery
from native.network import RequestError


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.home = Path(self.temp.name)
        self.env = {"HOME": str(self.home), "USERPROFILE": str(self.home), "APPDATA": str(self.home / "AppData/Roaming"), "LOCALAPPDATA": str(self.home / "AppData/Local")}

    def touch(self, relative, content="{}"):
        path = self.home / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def discover(self, **kwargs):
        with patch.object(discovery.sys, "platform", "win32"):
            return list(discovery.discover(self.home, self.env, wsl_homes=kwargs.pop("wsl_homes", []), **kwargs))

    def test_every_local_tool_has_real_discovery_route_and_callable_parser(self):
        fixtures = {
            "acode": ".acode/sessions/rollout-a.jsonl",
            "codex": ".codex/archived_sessions/rollout-a.jsonl.zstd",
            "every-code": ".code/sessions/rollout-a.jsonl",
            "claude": ".claude/projects/p/session.jsonl",
            "gemini": ".gemini/tmp/p/chats/session-a.json",
            "antigravity": ".gemini/antigravity/brain/a/.system_generated/logs/transcript.jsonl",
            "opencode": ".local/share/opencode/opencode.db",
            "kilo-cli": "AppData/Roaming/kilo/kilo.db",
            "mimo": ".local/share/mimocode/mimocode.db",
            "zcode": ".zcode/cli/db/db.sqlite",
            "dumate": "AppData/Roaming/qianfan-desktop-app/qianfan_desk_xdg/i/data/opencode/opencode.db",
            "qoder": ".qoder/projects/p/a.jsonl",
            "qoder-cn": ".qoder-cn/projects/p/a.jsonl",
            "claude-science": ".claude-science/orgs/a/operon.db",
            "kiro": "AppData/Roaming/Kiro/User/globalStorage/kiro.kiroagent/dev_data/devdata.sqlite",
            "hermes": "AppData/Local/hermes/profiles/a/state.db",
            "kimi": ".kimi/sessions/p/s/wire.jsonl",
            "kimi-code": ".kimi-code/sessions/p/s/wire.jsonl",
            "openclaw": ".openclaw/agents/a/session-sqlite-import-archive/a.jsonl.reset.2026",
            "codebuddy": ".codebuddy/projects/p/a.jsonl",
            "workbuddy": ".workbuddy/projects/p/a.jsonl",
            "workbuddy-ai": ".workbuddy-ai/projects/p/a.jsonl",
            "omp": ".omp/agent/sessions/p/sub/a.jsonl",
            "omo": ".omo/agent/sessions/p/a.jsonl",
            "pi": ".pi/agent/sessions/p/a.jsonl",
            "prime-agent": ".prime/agent/sessions/a.jsonl",
            "minimax-code": ".minimax/v2/sessions/a/messages.jsonl",
            "craft": ".craft-agent/workspaces/p/sessions/a/session.jsonl",
            "reasonix": "AppData/Roaming/reasonix/projects/p/a.jsonl.telemetry.json",
            "muse": ".local/share/muse/sessions/p/session.jsonl",
            "copilot": ".copilot/otel/a.jsonl",
            "kilocode": "AppData/Roaming/Code/User/globalStorage/kilocode.kilo-code/tasks/a/ui_messages.json",
            "roocode": "AppData/Roaming/Cursor/User/globalStorage/rooveterinaryinc.roo-cline/tasks/a/ui_messages.json",
            "cline": ".cline/data/sessions/a/a.messages.json",
            "droid": ".factory/sessions/p/a.settings.json",
            "dsh": ".dsh/sessions/p/a/session.v3.jsonl.zstd",
            "command-code": ".commandcode/projects/p/a.jsonl",
            "lmstudio": ".lmstudio/server-logs/p/a.log",
            "grok": ".grok/sessions/p/a/updates.jsonl",
            "unsloth": ".unsloth/studio/studio.db",
            "anythingllm": "AppData/Roaming/anythingllm-desktop/storage/anythingllm.db",
            "devin": ".local/share/devin/cli/sessions.db",
            "goose": "AppData/Roaming/goose/sessions/sessions.db",
            "zed": "AppData/Local/Zed/threads/threads.db",
            "opensquilla": ".opensquilla/state/sessions.db",
            "trae": "AppData/Roaming/TRAE SOLO/ModularData/ai-agent/database.db",
        }
        for path in fixtures.values():
            self.touch(path)
        found = self.discover()
        self.assertEqual(set(fixtures), discovery.SOURCES - {"cursor", "trae-cn"})
        self.assertEqual(set(fixtures), {source for source, _, _ in found})
        for source, parser, path in found:
            module, function = parser.split(".")
            self.assertTrue(callable(getattr(importlib.import_module("native." + module), function)), parser)
            self.assertEqual(path, self.home / fixtures[source])

    def test_custom_roots_and_real_path_dedup(self):
        codex = self.touch("custom/sessions/rollout-a.jsonl")
        self.touch(".tokentracker/tracker/config.json", json.dumps({"scanRoots": {"codex": ["custom", "~/custom"]}}))
        self.env["CODEX_HOME"] = str(self.home / "custom")
        self.assertEqual([(s, p) for s, _, p in self.discover()], [("codex", codex)])

    def test_secondary_formats_and_authoritative_fallback(self):
        paths = [".copilot/session-store.db", ".copilot/data.db", ".copilot-otel/a.jsonl",
                 ".kiro/sessions/p/sess_opaque/messages.jsonl", ".kiro/sessions/cli/12345678-1234-1234-1234-123456789abc.json",
                 ".openclaw/agents/a/agent/openclaw-agent.sqlite", ".openclaw/agents/a/openclaw-agent.sqlite",
                 "AppData/Roaming/Code/logs/a/tencent-cloud.coding-copilot/a.log",
                 ".workbuddy/traces/1/trace_a.json", "AppData/Roaming/Qoder/SharedClientCache/cache/db/local.db"]
        for p in paths:
            self.touch(p)
        found = self.discover()
        parsers = {parser for _, parser, _ in found}
        self.assertTrue({"extended.copilot_store", "extended.copilot_app", "extended.copilot_otel", "extended.kiro_cli", "extended.codebuddy", "formats.workbuddy_trace", "databases.qoder"}.issubset(parsers))
        self.assertEqual(sum(parser == "extended.openclaw_db" for _, parser, _ in found), 1)

    def test_dsh_prefers_latest_artifact_and_skips_command_sidecars(self):
        old = self.touch(".dsh/sessions/p/s/session.jsonl")
        new = self.touch(".dsh/sessions/p/s/session.v3.jsonl.zstd")
        import os
        os.utime(old, (1, 1)); os.utime(new, (2, 2))
        self.touch(".commandcode/projects/p/a.prompts.1.jsonl")
        self.touch(".commandcode/projects/p/a.checkpoint.1.jsonl")
        self.assertEqual([(s, p) for s, _, p in self.discover()], [("dsh", new)])

    def test_pi_override_is_attributed_once(self):
        path = self.touch("custom/sessions/p/a.jsonl")
        self.env.update(TOKENTRACKER_OMP_AGENT_DIR=str(self.home / "custom"), TOKENTRACKER_PI_AGENT_DIR=str(self.home / "custom"))
        self.assertEqual([(s, p) for s, _, p in self.discover()], [("omp", path)])

    def test_wsl_only_reads_running_distribution_homes(self):
        self.touch(".codex/sessions/rollout-native.jsonl")
        linux_home = self.home / "linux-home"
        target = self.touch("linux-home/.codex/sessions/rollout-linux.jsonl")
        self.env["TOKENTRACKER_WSL_MODE"] = "wsl-only"
        self.assertEqual([(s, p) for s, _, p in self.discover(wsl_homes=[linux_home])], [("codex", target)])
        response = type("Result", (), {"returncode": 0, "stdout": b""})()
        with patch.object(discovery.sys, "platform", "win32"), patch.object(discovery.subprocess, "run", return_value=response) as run:
            self.assertEqual(discovery.running_wsl_homes(), [])
            self.assertEqual(run.call_args.args[0], ["wsl.exe", "--list", "--running", "--quiet"])

    def test_muse_override_outside_home_and_hidden_views(self):
        self.env["TOKENTRACKER_MUSE_HOME"] = str(self.home.parent / (self.home.name + "-muse"))
        with tempfile.TemporaryDirectory() as external:
            self.env["TOKENTRACKER_MUSE_HOME"] = external
            root = Path(external)
            for relative in ("sessions/a/session.jsonl", "sessions/.msp-view-v1/session.jsonl"):
                path = root / relative; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("{}")
            found = self.discover()
            self.assertEqual(len(found), 1)
            self.assertEqual(found[0][2], root / "sessions/a/session.jsonl")

    def test_permission_denied_in_one_wsl_home_does_not_end_scan(self):
        denied = self.home / "linux-denied"
        healthy = self.home / "linux-readable"
        wanted = self.touch("linux-readable/.codex/sessions/rollout-a.jsonl")
        self.env["TOKENTRACKER_WSL_MODE"] = "wsl-only"
        original = Path.is_file
        def check(path):
            if path.is_relative_to(denied):
                raise PermissionError("fixture_permission_denied")
            return original(path)
        with patch.object(Path, "is_file", check):
            found = self.discover(wsl_homes=[denied, healthy])
        self.assertEqual([(source, path) for source, _, path in found], [("codex", wanted)])

    def test_inaccessible_directory_operations_are_isolated(self):
        wanted = self.touch(".codex/sessions/rollout-a.jsonl")
        roots = {"glob": self.home / ".kiro/sessions/cli", "iterdir": self.home / ".openclaw/agents", "is_dir": self.home / ".pi"}
        for method, denied in roots.items():
            original = getattr(Path, method)
            def check(path, *args, original=original, **kwargs):
                if path == denied:
                    raise PermissionError("fixture_permission_denied")
                return original(path, *args, **kwargs)
            with self.subTest(method=method), patch.object(Path, method, check):
                self.assertEqual([(source, path) for source, _, path in self.discover()], [("codex", wanted)])

    def test_dsh_unreadable_candidate_does_not_block_readable_artifact(self):
        denied = self.touch(".dsh/sessions/p/s/session.v3.jsonl")
        wanted = self.touch(".dsh/sessions/p/s/session.jsonl")
        original = Path.stat
        def check(path, *args, **kwargs):
            if path == denied:
                raise PermissionError("fixture_permission_denied")
            return original(path, *args, **kwargs)
        with patch.object(Path, "stat", check):
            self.assertEqual([(source, path) for source, _, path in self.discover()], [("dsh", wanted)])


class AccountTests(unittest.TestCase):
    def test_cursor_redirect_preserves_cookie_only_once_on_official_hosts(self):
        import urllib.request
        handler = accounts._CursorRedirect()
        request = urllib.request.Request(accounts.CURSOR_CSV_URL, headers={"Cookie": "synthetic"})
        next_request = handler.redirect_request(request, None, 302, "", {}, "https://www.cursor.com/usage.csv")
        self.assertEqual(next_request.get_header("Cookie"), "synthetic")
        for target in ("https://example.test/usage.csv", "http://cursor.com/usage.csv", "https://cursor.com:9443/usage.csv"):
            with self.assertRaises(RequestError):
                handler.redirect_request(request, None, 302, "", {}, target)
        with self.assertRaises(RequestError):
            handler.redirect_request(next_request, None, 302, "", {}, accounts.CURSOR_CSV_URL)

    def test_cursor_csv_preserves_cache_split_and_identical_request_multiplicity(self):
        header = 'Date,Model,Kind,Max Mode,Input (w/ Cache Write),Input (w/o Cache Write),Cache Read,Output Tokens,Total Tokens,Cost\n'
        line = '2026-10-29T02:00:00Z,"model,quoted",Included,No,150,100,20,30,200,$0.1\n'
        rows = accounts.parse_cursor_csv(header + line + line)
        self.assertEqual(len(rows), 2)
        self.assertNotEqual(rows[0]["event_id"], rows[1]["event_id"])
        self.assertEqual(rows, accounts.parse_cursor_csv(header + line + line))
        self.assertEqual([rows[0][k] for k in ("input_tokens", "cache_creation_input_tokens", "cached_input_tokens", "output_tokens", "total_tokens")], [100, 50, 20, 30, 200])
        self.assertEqual(rows[0]["source_scope"], "account")
        without_cost = header.replace(",Cost", "") + line.replace(",$0.1", "") * 2
        self.assertEqual(accounts.parse_cursor_csv(without_cost), rows)
        with self.assertRaises(RequestError):
            accounts.parse_cursor_csv("unexpected,response\n1,2")

    def test_cursor_auth_uses_cli_subject_without_breaking_workos_prefix(self):
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            path = home / "AppData/Roaming/Cursor/User/globalStorage/state.vscdb"
            path.parent.mkdir(parents=True)
            with sqlite3.connect(path) as db:
                db.execute("CREATE TABLE ItemTable(key,value)")
                db.execute("INSERT INTO ItemTable VALUES(?,?)", ("cursorAuth/accessToken", "synthetic.access.token"))
            db.close()
            cli = home / ".cursor/cli-config.json"; cli.parent.mkdir()
            cli.write_text(json.dumps({"authInfo": {"authId": "google-oauth2|synthetic-user"}}))
            with patch.object(accounts.sys, "platform", "win32"):
                result = accounts.cursor_credentials(home, {})
            self.assertEqual(result["cookie"], "WorkosCursorSessionToken=google-oauth2|synthetic-user%3A%3Asynthetic.access.token")
            self.assertEqual(accounts.cursor_subject("auth0|user_example"), "user_example")
            self.assertIsNone(accounts.cursor_subject("other|nope"))

    def test_trae_cn_normalizes_session_snapshot_and_fails_closed(self):
        row = {"session_id": "synthetic", "usage_time": 1793239200, "model_name": "model", "extra_info": json.dumps({"input_token": 100, "output_token": 20, "cache_read_token": 30, "cache_write_token": 10})}
        first = accounts.normalize_trae_cn([row])[0]
        changed = {**row, "extra_info": {"input_token": 200, "output_token": 40}}
        rows = accounts.normalize_trae_cn([row, changed])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["event_id"], first["event_id"])
        self.assertEqual(first["input_tokens"], 60)
        self.assertEqual(rows[0]["total_tokens"], 240)
        zero = accounts.normalize_trae_cn([{**row, "extra_info": {"input_token": 0, "output_token": 0}}])[0]
        self.assertEqual(zero["event_id"], first["event_id"])
        self.assertEqual(zero["total_tokens"], 0)
        with self.assertRaises(RequestError):
            accounts.normalize_trae_cn([row, {**row, "extra_info": {"input_token": "100", "output_token": 1}}])

    def test_trae_cn_tc_v5_decryption_checks_digest(self):
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        from cryptography.hazmat.primitives.padding import PKCS7
        salt = bytes(range(32)); payload = b'{"token":"synthetic-test-only"}'
        key = hashlib.sha512(hashlib.sha512(salt).digest() + bytes(a ^ b for a, b in zip(accounts._JG, accounts._KG))).digest()
        def encrypt(raw):
            pad = PKCS7(128).padder(); padded = pad.update(raw) + pad.finalize()
            enc = Cipher(algorithms.AES(key[:16]), modes.CBC(key[16:32])).encryptor()
            return b"tc\x05\x10\x00\x00" + salt + enc.update(padded) + enc.finalize()
        self.assertEqual(accounts.decrypt_trae_cn(encrypt(hashlib.sha512(payload).digest() + payload)), payload)
        with self.assertRaises(RequestError):
            accounts.decrypt_trae_cn(encrypt(bytes(64) + payload))

    def test_trae_cn_capacity_split_does_not_overlap_windows(self):
        calls = []
        def http(url, **kwargs):
            body = kwargs["body"]; calls.append(body)
            if body["end_time"] - body["start_time"] > 1:
                return {"data": {"user_usage_group_by_sessions": [], "total": 21}}
            return {"data": {"user_usage_group_by_sessions": [{"window": (body["start_time"], body["end_time"])}], "total": 1}}
        rows = accounts.fetch_trae_cn_window("synthetic", 1, 4, http, delay=0, max_pages=1)
        self.assertEqual(rows, [{"window": (1, 2)}, {"window": (3, 4)}])
        self.assertEqual(len(calls), 3)
        self.assertTrue(all(c["usage_type"] == [7] for c in calls))

    def test_trae_cn_disabled_does_not_read_or_send_credentials(self):
        with patch.object(accounts, "trae_cn_token", side_effect=AssertionError("unexpected read")):
            self.assertEqual(accounts.trae_cn_usage(Path("unused"), {}), [])

    def test_trae_cn_auth_failure_rereads_once(self):
        tokens = iter(["synthetic-old", "synthetic-new"])
        calls = []
        def http(url, **kwargs):
            calls.append(kwargs["headers"]["Authorization"])
            if len(calls) == 1:
                raise RequestError("reauth", 401)
            return {"data": {"user_usage_group_by_sessions": [], "total": 0}}
        with patch.object(accounts, "trae_cn_token", side_effect=lambda *args: next(tokens)):
            self.assertEqual(accounts.trae_cn_usage(Path("unused"), {"TOKENTRACKER_TRAE_CN_USAGE": "1"}, http, now=1793239200, delay=0), [])
        self.assertEqual(calls, ["Cloud-IDE-JWT synthetic-old", "Cloud-IDE-JWT synthetic-new"])


class CompressionTests(unittest.TestCase):
    def test_zstd_limit_unknown_size_and_json_lines(self):
        import zstandard
        payload = b'{"type":"first"}\ninvalid\n{"type":"second"}\n'
        compressed = zstandard.ZstdCompressor(write_content_size=False).compress(payload)
        self.assertEqual(compression.decompress_zstd(compressed), payload)
        with self.assertRaisesRegex(RuntimeError, "decompressed_too_large"):
            compression.decompress_zstd(compressed, 10)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "rollout-test.jsonl.zstd"; path.write_bytes(compressed)
            self.assertEqual(list(compression.json_lines(path)), [(0, {"type": "first"}), (2, {"type": "second"})])


if __name__ == "__main__":
    unittest.main()
