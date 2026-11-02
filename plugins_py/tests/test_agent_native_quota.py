# test_agent_native_quota.py — 十八类原生额度适配器的隔离凭据、协议和失败回归
import base64
import ctypes
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "agent_usage"))
from native.quota import Quotas, fraction, result, window
from native.network import RequestError
from native import quota_local as local, quota_products as products


class FakeHttp:
    def __init__(self, responses):
        self.responses, self.calls = responses, []

    def __call__(self, url, **kwargs):
        self.calls.append((url, kwargs))
        value = self.responses.get(url)
        if isinstance(value, Exception):
            raise value
        if value is None:
            raise RequestError("fixture_unexpected_endpoint")
        return value


class QuotaTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def write(self, path, value):
        path = self.home / path
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value) if not isinstance(value, str) else value, encoding="utf-8")
        return path

    def q(self, responses=None, env=None, **kwargs):
        value = Quotas(self.home, env or {}, FakeHttp(responses or {}), **kwargs)
        value._workbuddy_wsl_homes = []
        return value

    def test_window_unknown_is_not_zero(self):
        for value in (None, "", "invalid", float("nan"), float("inf"), True, {}):
            self.assertIsNone(window("primary_window", value), repr(value))
        self.assertEqual(window("primary_window", 0)["used"], 0)
        self.assertIsNone(fraction("primary_window", {"limit": 100}))
        self.assertIsNone(fraction("primary_window", {"limit": 100, "used": "changed format"}))
        self.assertIsNone(fraction("primary_window", {"limit": 100, "remaining": "changed format"}))
        with self.assertRaises(RequestError):
            products.qoder_rpc_body({"userQuota": {"total": 100}})

    def test_claude_scoped_and_extra_windows(self):
        self.write(".claude/.credentials.json", {"claudeAiOauth": {"accessToken": "fixture", "subscriptionType": "max"}})
        q = self.q({"https://api.anthropic.com/api/oauth/usage": {"five_hour": {"utilization": 22, "resets_at": "2030-01-01T00:00:00Z"}, "seven_day": {"utilization": 33}, "extra_usage": {"is_enabled": True, "monthly_limit": 100, "used_credits": 4}}})
        value = q.claude()
        self.assertEqual([w["used"] for w in value["windows"]], [22, 33, 4])
        self.assertEqual(q.http.calls[0][1]["headers"]["anthropic-beta"], "oauth-2025-04-20")

    def test_codex_swapped_windows_spark_and_credit(self):
        self.write(".codex/auth.json", {"tokens": {"access_token": "fixture", "account_id": "account"}, "last_refresh": "2030-01-01T00:00:00Z"})
        q = self.q({"https://chatgpt.com/backend-api/wham/usage": {"plan_type": "pro", "rate_limit": {
            "primary_window": {"used_percent": 60, "limit_window_seconds": 604800}, "secondary_window": {"used_percent": 25, "limit_window_seconds": 18000}},
            "additional_rate_limits": [{"limit_name": "gpt-spark", "rate_limit": {"primary_window": {"used_percent": 5}}}],
            "spend_control": {"individual_limit": {"limit": 10, "used": 3}}}})
        value = q.codex()
        windows = {w["id"]: w["used"] for w in value["windows"]}
        self.assertEqual(windows, {"primary_window": 25, "secondary_window": 60, "spark_primary_window": 5, "credit_window": 30})
        self.assertEqual(q.http.calls[0][1]["headers"]["ChatGPT-Account-Id"], "account")

    def test_codex_refresh_preserves_unrelated_fields(self):
        path = self.write(".codex/auth.json", {"other": True, "tokens": {"access_token": "old", "refresh_token": "refresh", "account_id": "a"}, "last_refresh": "2000-01-01"})
        q = self.q({"https://auth.openai.com/oauth/token": {"access_token": "new", "refresh_token": "next"}, "https://chatgpt.com/backend-api/wham/usage": {"rate_limit": {"primary_window": {"used_percent": 0}}}})
        q.codex()
        self.assertTrue(json.loads(path.read_text())["other"])
        self.assertEqual(q.http.calls[1][1]["headers"]["Authorization"], "Bearer new")

    def test_cursor_summary_and_grokbot_protobuf(self):
        token = "fixture-token"
        self.write(".cursor/cli-config.json", {"authInfo": {"authId": "auth0|fixture"}})
        db_path = self.home / "AppData/Roaming/Cursor/User/globalStorage/state.vscdb"
        db_path.parent.mkdir(parents=True)
        db = sqlite3.connect(db_path)
        try:
            db.execute("CREATE TABLE ItemTable(key, value)")
            db.execute("INSERT INTO ItemTable VALUES ('cursorAuth/accessToken', ?)", (token,))
            db.commit()
        finally:
            db.close()
        future = int(time.time()) + 3600
        def varint(n):
            output = bytearray()
            while n > 127:
                output.append((n & 127) | 128)
                n >>= 7
            output.append(n)
            return bytes(output)
        stamp = b"\x08" + varint(future)
        payload = b"\x12" + varint(len(stamp)) + stamp + b"\x19" + struct.pack("<d", 27.5)
        base = "https://api2.cursor.sh/aiserver.v1.DashboardService/"
        q = self.q({"https://cursor.com/api/usage-summary": {"individualUsage": {"plan": {"autoPercentUsed": 20, "apiPercentUsed": 40}}, "membershipType": "pro"}, base + "GetSandAccessStatus": b"\x08\x01", base + "GetSandUsageStatus": payload})
        value = q.cursor()
        self.assertEqual([w["used"] for w in value["windows"]], [30, 20, 40, 27.5])
        self.assertEqual(q.http.calls[1][1]["raw_body"], b"")
        self.assertNotIn("Cookie", q.http.calls[1][1]["headers"])

    def test_kimi_default_login_without_config(self):
        self.write(".kimi-code/credentials/kimi-code.json", {"access_token": "fixture", "expires_at": time.time() + 3600})
        q = self.q({"https://api.kimi.com/coding/v1/usages": {"usage": {"limit": 100, "remaining": 75}, "limits": [{"detail": {"limit": 50, "used": 10}}]}})
        self.assertEqual([w["used"] for w in q.kimi()["windows"]], [25, 20])

    def test_kimi_global_profile_does_not_mix_slots(self):
        self.write(".kimi-code/config.toml", '[providers."managed:kimi-code"]\nbase_url="https://api.kimi.ai/coding/v1"\n[providers."managed:kimi-code".oauth]\noauth_host="https://auth.kimi.ai"\n')
        self.write(".kimi-code/credentials/kimi-code.json", {"access_token": "mainland"})
        self.assertFalse(self.q().kimi()["configured"])
        self.write(".kimi-code/credentials/kimi-code-env-0e4f99c69cc27850.json", {"access_token": "global"})
        q = self.q({"https://api.kimi.ai/coding/v1/usages": {"usage": {"limit": 10, "used": 4}}})
        self.assertEqual(q.kimi()["windows"][0]["used"], 40)

    def test_kimi_rejected_token_refreshes_once(self):
        path = self.write(".kimi-code/credentials/kimi-code.json", {"access_token": "old", "refresh_token": "refresh", "expires_at": time.time() + 3600})
        q = self.q()
        q.http = unittest.mock.Mock(side_effect=[RequestError("reauth", 401), {"access_token": "new", "expires_in": 900}, {"usage": {"limit": 100, "used": 40}}])
        self.assertEqual(q.kimi()["windows"][0]["used"], 40)
        self.assertEqual(q.http.call_count, 3)
        self.assertEqual(json.loads(path.read_text())["access_token"], "new")

    def test_gemini_refresh_and_worst_model_bucket(self):
        self.write(".gemini/oauth_creds.json", {"access_token": "old", "refresh_token": "fixture", "expiry_date": 1})
        base = "https://cloudcode-pa.googleapis.com/v1internal:"
        q = self.q({"https://oauth2.googleapis.com/token": {"access_token": "new", "expires_in": 3600}, base + "loadCodeAssist": {"cloudaicompanionProject": {"id": "p"}, "currentTier": {"id": "standard-tier"}}, base + "retrieveUserQuota": {"buckets": [{"modelId": "gemini-pro", "remainingFraction": .8}, {"modelId": "gemini-pro-preview", "remainingFraction": .6}, {"modelId": "gemini-flash", "remainingFraction": .7}, {"modelId": "gemini-flash-lite", "remainingFraction": .9}]}})
        value = q.gemini()
        self.assertEqual([round(w["used"]) for w in value["windows"]], [40, 30, 10])
        self.assertEqual(q.http.calls[-1][1]["body"], {"project": "p"})

    def test_command_code_bearer_and_org(self):
        base = "https://api.commandcode.ai/alpha/"
        q = self.q({base + "whoami?limits=1": {"data": {"org": {"id": "o"}}}, base + "billing/credits?orgId=o": {"windowLimits": {"fiveHour": {"cap": 100, "used": 10}, "weekly": {"cap": 200, "used": 100}}}, base + "billing/subscriptions?orgId=o": {"data": {"planId": "pro"}}}, {"COMMAND_CODE_API_KEY": "fixture"})
        self.assertEqual([w["used"] for w in q.command_code()["windows"]], [10, 50])
        self.assertEqual(q.http.calls[0][1]["headers"], {"Authorization": "Bearer fixture"})

    def test_devin_proto_default_exhausted_and_hidden(self):
        self.write(".local/share/devin/credentials.toml", 'windsurf_api_key="fixture"\n')
        q = self.q({"https://server.codeium.com/exa.seat_management_pb.SeatManagementService/GetPlanStatus": {"planStatus": {"planInfo": {"planName": "Pro", "hideWeeklyQuota": True}, "dailyQuotaResetAtUnix": int(time.time()) + 3600, "weeklyQuotaRemainingPercent": 20}}})
        self.assertEqual([w["used"] for w in q.devin()["windows"]], [100])

    def test_kiro_native_cli_parsing(self):
        output = "Plan: KIRO PRO\n(25 of 100 covered)\nresets on 2030-01-01\nBonus credits: 3/10\nexpires in 4 days"
        with patch.object(local.shutil, "which", return_value="kiro-cli.exe"), patch.object(local, "command", return_value=(output, 0)) as run:
            value = local.kiro(self.q())
        self.assertEqual([w["used"] for w in value["windows"]], [25, 30])
        self.assertEqual(run.call_args[0][0][1:], ["chat", "--no-interactive", "/usage"])

    def test_copilot_plaintext_and_sqlite_v0(self):
        path = self.home / ".config/github-copilot/auth.db"
        path.parent.mkdir(parents=True)
        db = sqlite3.connect(path)
        try:
            db.execute("CREATE TABLE oauth_tokens(token_schema_version, token_ciphertext, auth_authority, last_used_at, updated_at)")
            db.execute("INSERT INTO oauth_tokens VALUES (0, ?, 'github.com',1,1)", (b"gho_fixture",))
            db.commit()
        finally:
            db.close()
        q = self.q({"https://api.github.com/copilot_internal/user": {"copilot_plan": "pro", "quota_reset_date": "2030-01-01", "quota_snapshots": {"premium_interactions": {"entitlement": 300, "remaining": 150}, "chat": {"entitlement": 0, "remaining": 0, "percent_remaining": 0}}}})
        value = local.copilot(q)
        self.assertEqual([w["used"] for w in value["windows"]], [50])
        self.assertEqual(q.http.calls[0][1]["headers"]["Authorization"], "token gho_fixture")

    def test_antigravity_official_remote_groups(self):
        self.write(".gemini/antigravity/antigravity-oauth-token", {"access_token": "fixture", "expiry": "2030-01-01"})
        q = self.q({"https://daily-cloudcode-pa.googleapis.com/v1internal:retrieveUserQuotaSummary": {"groups": [{"buckets": [{"bucketId": "3p-weekly", "remainingFraction": .2}, {"bucketId": "gemini-5h", "remainingFraction": 1}]}]}, "https://daily-cloudcode-pa.googleapis.com/v1internal:loadCodeAssist": {"paidTier": {"name": "Google AI Pro"}}})
        self.assertEqual([w["used"] for w in local.antigravity(q)["windows"]], [80, 0])

    def test_antigravity_model_fallback(self):
        data = {"userStatus": {"cascadeModelConfigData": {"clientModelConfigs": [{"label": "Claude Opus", "quotaInfo": {"remainingFraction": .2}}, {"label": "Claude Sonnet", "quotaInfo": {"remainingFraction": .8}}, {"label": "Gemini Pro", "quotaInfo": {"remainingFraction": .6}}]}}}
        self.assertEqual([round(w["used"]) for w in local.antigravity_models(data)["windows"]], [80, 20, 40, 40])

    def test_antigravity_rejected_token_refreshes_once(self):
        path = self.write(".gemini/antigravity/antigravity-oauth-token", {"access_token": "old", "refresh_token": "refresh", "expiry": "2030-01-01"})
        q = self.q()
        q.http = unittest.mock.Mock(side_effect=[RequestError("reauth", 401), {"access_token": "new", "expires_in": 3600}, {"groups": [{"buckets": [{"bucketId": "3p-weekly", "remainingFraction": .6}]}]}, {}])
        self.assertEqual(local.antigravity(q)["windows"][0]["used"], 40)
        self.assertEqual(q.http.call_count, 4)
        self.assertEqual(json.loads(path.read_text())["access_token"], "new")

    @unittest.skipUnless(os.name == "nt", "仅 Windows 进程发现路径")
    def test_antigravity_extension_port_fallback(self):
        process = json.dumps({"ProcessId": 123, "CommandLine": "antigravity --csrf_token fixture --extension_server_port 9002"})
        models = {"clientModelConfigs": [{"label": "Claude", "quotaInfo": {"remainingFraction": .5}}]}
        with patch.object(local, "command", side_effect=[(process, 0), ("TCP 127.0.0.1:9001 0.0.0.0:0 LISTENING 123", 0)]), patch.object(local, "local_json", side_effect=[RequestError("unavailable"), RequestError("unavailable"), models]) as request:
            self.assertEqual(local.antigravity(self.q())["windows"][0]["used"], 50)
        self.assertEqual(request.call_args[0][0], 9002)
        self.assertEqual(request.call_args[0][-1], "http")

    def test_grok_refresh_and_product_pool(self):
        path = self.write(".grok/auth.json", {"login::client": {"key": "old", "refresh_token": "refresh", "expires_at": "2000-01-01"}})
        q = self.q({"https://auth.x.ai/oauth2/token": {"access_token": "new", "expires_in": 3600}, "https://cli-chat-proxy.grok.com/v1/billing?format=credits": {"subscriptionTier": "SUPERGROK", "config": {"productUsage": [{"usagePercent": 15}, {"usagePercent": 20}], "onDemandCap": 100, "onDemandUsed": 10}}})
        self.assertEqual([w["used"] for w in products.grok(q)["windows"]], [35, 10])
        self.assertEqual(json.loads(path.read_text())["login::client"]["key"], "new")

    def test_zcode_encrypted_credentials_and_coding_plan(self):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        secret, iv = "fixture-secret", bytes(range(12))
        encrypted = AESGCM(hashlib.sha256(secret.encode()).digest()).encrypt(iv, b"fixture-key", None)
        enc = lambda b: base64.urlsafe_b64encode(b).decode().rstrip("=")
        self.write(".zcode/v2/credentials.json", {"account-provider:coding-plan:account:zai-individual-coding-plan:account:fixture:api-key": "enc:v1:" + ".".join(map(enc, (iv, encrypted[-16:], encrypted[:-16])) )})
        q = self.q({"https://api.z.ai/api/monitor/usage/quota/limit": {"code": 0, "data": {"level": "max", "limits": [{"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 42}, {"type": "TOKENS_LIMIT", "unit": 6, "number": 1, "percentage": 20}]}}}, {"ZCODE_CREDENTIAL_SECRET": secret})
        self.assertEqual([w["used"] for w in products.zcode(q)["windows"]], [42, 20])
        self.assertEqual(q.http.calls[0][1]["headers"]["authorization"], "fixture-key")

    def test_zcode_start_plan_priority(self):
        body = {"data": {"plans": [{"entitlements": [{"entitlement_id": "gift", "period": "one_time"}]}], "balances": [{"entitlement_id": "gift", "total_units": 1000, "used_units": 500, "priority": 99}, {"entitlement_id": "plan", "total_units": 100, "used_units": 10, "priority": 1}]}}
        self.assertEqual([w["used"] for w in products.zcode_balance(body)["windows"]], [10, 50])

    def test_zcode_active_account_wins_without_config(self):
        self.write(".zcode/v2/credentials.json", {"account-provider:coding-plan:account:zai-individual-coding-plan:account:old:api-key": "old-key", "account-provider:coding-plan:account:zai-individual-coding-plan:account:new:api-key": "new-key", "oauth:zai:user_info": json.dumps({"id": "new"})})
        q = self.q({"https://api.z.ai/api/monitor/usage/quota/limit": {"data": {"limits": [{"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 5}]}}})
        self.assertEqual(products.zcode(q)["windows"][0]["used"], 5)
        self.assertEqual(q.http.calls[0][1]["headers"]["authorization"], "new-key")

    def test_opencode_go_api_modern_and_legacy(self):
        q = self.q({"https://opencode.ai/zen/go/v1/usage": {"usage": {"rolling": {"percent": 20, "resetInSec": 3600}}, "weeklyUsage": {"usagePercent": 40, "resetInSec": 6000}, "monthly": {"percent": 10}}}, {"OPENCODE_GO_API_KEY": "fixture"})
        windows = products.opencode_go(q)["windows"]
        self.assertEqual([w["used"] for w in windows], [20, 40, 10])
        self.assertEqual([w["seconds"] for w in windows], [0, 604800, 0])

    def test_opencode_go_dashboard_slots(self):
        html = '<div data-slot="usage-item"><span data-slot="usage-label">Rolling</span><span data-slot="usage-value">35%</span><span data-slot="reset-time">Resets in 2h 30m</span></div>'
        q = self.q({"https://opencode.ai/workspace/wrk_fixture/go": html.encode()}, {"OPENCODE_GO_AUTH_COOKIE": "fixture", "OPENCODE_GO_WORKSPACE_ID": "wrk_fixture"})
        self.assertEqual(products.opencode_go(q)["windows"][0]["used"], 35)

    def test_qoder_rpc_and_addon(self):
        q = self.q()
        with patch.object(products, "qoder_rpc", side_effect=[{"userType": "pro", "isQuotaExceeded": True, "userQuota": {"used": 20, "total": 100, "remaining": 80}, "addOnQuota": {"used": 1, "total": 10}}, {}]):
            value = products.qoder(q)
        self.assertEqual([w["used"] for w in value["windows"]], [20, 10])

    def test_qoder_cn_pat_exchange(self):
        base = "https://openapi.qoder.com.cn"
        q = self.q({base + "/api/v1/jobToken/exchange": {"token": "session"}, base + "/api/v2/quota/usage": {"displayMode": "qoder", "qoderUsage": {"user_quota": {"total": 100, "used": 40}, "user_type": "pro"}}}, {"QODERCN_PERSONAL_ACCESS_TOKEN": "fixture"})
        with patch.object(products, "qoder_rpc", side_effect=RequestError("unavailable")):
            value = products.qoder(q, china=True)
        self.assertEqual(value["windows"][0]["used"], 40)
        self.assertEqual(q.http.calls[1][1]["headers"]["Authorization"], "Bearer session")

    def test_qoder_cn_aes_cbc_credentials(self):
        from cryptography.hazmat.primitives import padding
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        key = b"1234567890abcdef"
        raw = json.dumps({"access_token": "fixture", "organization_id": "org"}).encode()
        pad = padding.PKCS7(128).padder()
        raw = pad.update(raw) + pad.finalize()
        encrypt = Cipher(algorithms.AES(key), modes.CBC(key)).encryptor()
        self.write(".qoder-cn/.auth/machine_id", key.decode())
        self.write(".qoder-cn/.auth/user", base64.b64encode(encrypt.update(raw) + encrypt.finalize()).decode())
        q = self.q({"https://openapi.qoder.com.cn/sash/api/v2/me/usage": {"userQuota": {"used": 25, "total": 50}}})
        self.assertEqual(products.qoder_cn_cli(q)["windows"][0]["used"], 50)

    def test_qoder_cn_reauth_is_not_hidden_by_absent_desktop(self):
        q = self.q({"https://openapi.qoder.com.cn/api/v1/jobToken/exchange": RequestError("reauth", 401)}, {"QODERCN_PERSONAL_ACCESS_TOKEN": "fixture"})
        with patch.object(products, "qoder_rpc", side_effect=RequestError("unavailable")):
            with self.assertRaisesRegex(RequestError, "reauth"):
                products.qoder(q, china=True)

    def test_ark_both_plans_skip_daily_and_node_shim(self):
        binary = self.home / "AppData/Roaming/npm/node_modules/@volcengine/ark-cli/bin/arkcli-windows-amd64.exe"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"MZfixture")
        q = self.q()
        items = [{"product": p, "tier": "pro", "periods": [{"label": "session", "percent": 25}, {"label": "weekly", "used": 1, "total": 2}, {"label": "daily", "percent": 99}]} for p in ("coding-plan", "agent-plan")]
        with patch.object(products, "command", return_value=(json.dumps({"items": items}), 0)) as run:
            for product in ("coding-plan", "agent-plan"):
                windows = products.ark(q, product)["windows"]
                self.assertEqual([w["used"] for w in windows], [25, 50])
                self.assertEqual([w["seconds"] for w in windows], [0, 604800])
            self.assertEqual(Path(run.call_args[0][0][0]), binary)
            items[0]["periods"][0]["label"] = "5h"
            run.return_value = (json.dumps({"items": items}), 0)
            self.assertEqual(products.ark(q, "coding-plan")["windows"][0]["seconds"], 18000)

    def test_ark_node_shim_without_binary_is_cli_missing(self):
        self.write("npm/arkcli.cmd", "node fixture.js")
        q = self.q(env={"PATH": str(self.home / "npm")})
        with patch.object(products, "command") as run:
            value = products.ark(q, "coding-plan")
        self.assertTrue(value["configured"])
        self.assertEqual(value["status"], "cli_missing")
        run.assert_not_called()

    def test_workbuddy_real_packages_exclude_rewards(self):
        base = "https://www.workbuddy.cn/billing/meter/get-user-resource-"
        rows = [{"PackageCode": "TCACA_code_007_nzdH5h4Nl0", "CycleTotalCapacity": 100000, "CycleUsedCapacity": 1}, {"PackageCode": "TCACA_code_002_AkiJS3ZHF5", "CycleTotalCapacity": 100, "CycleRemainCapacity": 70}]
        q = self.q({base + "summary": {"code": 0, "data": {"SubscriptionPackageCode": "TCACA_code_002_AkiJS3ZHF5"}}, base + "paid-packages": {"data": {"Accounts": rows}}, base + "free-packages": {"data": {"Accounts": []}}}, {"WORKBUDDY_ACCESS_TOKEN": "fixture"})
        value = local.workbuddy(q)
        self.assertEqual(value["windows"][0]["used"], 30)
        self.assertEqual(value["plan"], "STANDARD")

    def test_workbuddy_ai_aggregate(self):
        base = "https://www.workbuddy.ai/billing/meter/get-user-resource-"
        q = self.q({base + "summary": {"data": {"Packages": [{"PackageCode": "TCACA_code_002_AkiJS3ZHF5", "total": 100, "used": 20}]}}, base + "paid-packages": {"data": {"Accounts": []}}, base + "free-packages": {"data": {"Accounts": []}}}, {"WORKBUDDY_AI_ACCESS_TOKEN": "fixture"})
        value = local.workbuddy(q)
        self.assertEqual(value["windows"][0]["used"], 20)
        self.assertEqual(value["plan"], "Pro")

    @unittest.skipUnless(os.name == "nt", "Windows 已运行 WSL 数据目录")
    def test_workbuddy_running_wsl_file_fallback(self):
        wsl = self.home / "running-wsl/home/fixture"
        self.write("running-wsl/home/fixture/.workbuddy/auth/workbuddy-desktop.info", {"auth": {"domain": "https://www.workbuddy.cn", "accessToken": "wsl-fixture"}})
        base = "https://www.workbuddy.cn/billing/meter/get-user-resource-"
        q = self.q({base + "summary": {"data": {"Packages": [{"PackageCode": "TCACA_code_002_AkiJS3ZHF5", "total": 100, "used": 20}]}}, base + "paid-packages": {"data": {"Accounts": []}}, base + "free-packages": {"data": {"Accounts": []}}})
        q._workbuddy_wsl_homes = [wsl]
        self.assertEqual(local.workbuddy_variant(q)["windows"][0]["used"], 20)
        self.assertEqual(q.http.calls[0][1]["headers"]["Authorization"], "Bearer wsl-fixture")

    def test_qoder_recent_account_and_legacy_logs(self):
        root = self.home / "qoder"
        payload = {"qoderUsage": {"userType": "pro", "userQuota": {"used": 15, "total": 100, "remaining": 85}}}
        path = self.write("qoder/logs/current/main.log", "[" + datetime.now().isoformat() + "] account: " + json.dumps(payload))
        q = self.q(env={"QODER_HOME": str(root)})
        with patch.object(products, "qoder_rpc", side_effect=RequestError("unavailable")):
            value = products.qoder(q)
            self.assertEqual(value["windows"][0]["used"], 15)
            self.assertTrue(value["stale"])
            path.write_text("userType=pro isQuotaExceeded=false userQuota used=35,total=100,remaining=65,percentage=35,unit=credits")
            value = products.qoder(q)
            self.assertEqual(value["windows"][0]["used"], 35)

    def test_zcode_recent_nested_balance_log_fallback(self):
        payload = {"success": True, "code": 0, "payload": {"code": 0, "data": {"balances": [{"total_units": 100, "used_units": 20}]}}}
        self.write(".zcode/v2/logs/fixture.log", "[" + datetime.now().isoformat(sep=" ", timespec="milliseconds") + "] [usage-stats] billing/balance 请求完成 " + json.dumps(payload))
        value = products.zcode(self.q())
        self.assertTrue(value["stale"])
        self.assertEqual(value["windows"][0]["used"], 20)

    def test_workbuddy_proof_is_length_framed_and_role_separated(self):
        a = local.wb_proof("ticket", "client", "pipe", "nonce-c", "nonce-s")
        b = local.wb_proof("ticket", "server", "pipe", "nonce-c", "nonce-s")
        self.assertEqual(len(a), 43)
        self.assertNotEqual(a, b)
        self.assertNotEqual(a, local.wb_proof("ticket", "client", "pip", "enonce-c", "nonce-s"))

    def test_workbuddy_authenticated_ipc_handshake_and_billing(self):
        endpoint, ticket = r"\\.\pipe\wbipc-fixture", "a" * 43
        self.write(".workbuddy/wbipc/endpoint.json", {"endpoint": endpoint, "ticket": ticket})
        class Stream:
            def __init__(self, *_):
                self.packets, self.phase, self.closed = [], 0, False
            def send_json(self, packet):
                self.packets.append(packet)
            def line_json(self):
                self.phase += 1
                if self.phase == 1:
                    nonce = self.packets[0]["client_nonce"]
                    return {"type": "session_challenge", "protocol": 1, "server_nonce": "s" * 22,
                            "server_proof": local.wb_proof(ticket, "server", endpoint, nonce, "s" * 22)}
                if self.phase == 2:
                    expected = local.wb_proof(ticket, "client", endpoint, self.packets[0]["client_nonce"], "s" * 22)
                    assert self.packets[-1]["client_proof"] == expected
                    return {"type": "session_hello_ack", "protocol": 1, "pipes": ["wb.request"]}
                if self.phase == 3:
                    return {"jsonrpc": "2.0", "id": 1, "result": {"channel": "c:wb.request", "methods": ["http.fetch"]}}
                return {"jsonrpc": "2.0", "id": 2, "result": {"status": 200, "body_b64": base64.b64encode(b'{"data":{"Accounts":[]}}').decode()}}
            def close(self):
                self.closed = True
        with patch.object(local, "LocalStream", Stream):
            client = local.WorkbuddyIpc(self.home / ".workbuddy")
            self.assertEqual(client.post("/billing/meter/get-user-resource-summary", {}), {"data": {"Accounts": []}})
            with self.assertRaises(RequestError):
                client.post("https://attacker.invalid/", {})
            self.assertNotIn(ticket, json.dumps(client.stream.packets))
            client.close()
            self.assertTrue(client.stream.closed)

    def test_workbuddy_rejects_forged_server_proof(self):
        self.write(".workbuddy/wbipc/endpoint.json", {"endpoint": r"\\.\pipe\wbipc-fixture", "ticket": "a" * 43})
        class Stream:
            def __init__(self, *_): pass
            def send_json(self, packet): pass
            def line_json(self):
                return {"type": "session_challenge", "protocol": 1, "server_nonce": "s" * 22, "server_proof": "forged"}
            def close(self): pass
        with patch.object(local, "LocalStream", Stream), self.assertRaisesRegex(RequestError, "reauth"):
            local.WorkbuddyIpc(self.home / ".workbuddy")

    @unittest.skipUnless(os.name == "nt", "Windows 命名管道")
    def test_windows_pipe_overlapped_io_roundtrip(self):
        from ctypes import wintypes as w
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.CreateNamedPipeW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, w.DWORD, ctypes.c_void_p]
        kernel.CreateNamedPipeW.restype = w.HANDLE
        kernel.ConnectNamedPipe.argtypes = [w.HANDLE, ctypes.c_void_p]
        kernel.ReadFile.argtypes = kernel.WriteFile.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.c_void_p]
        kernel.CloseHandle.argtypes = [w.HANDLE]
        endpoint = r"\\.\pipe\wbipc-xuanhub-test-" + str(os.getpid()) + "-" + str(time.time_ns())
        handle = kernel.CreateNamedPipeW(endpoint, 3, 0, 1, 65536, 65536, 2000, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        messages, failures = [], []
        def serve():
            try:
                if not kernel.ConnectNamedPipe(handle, None) and ctypes.get_last_error() != 535:
                    raise AssertionError("connect")
                buffer, count = ctypes.create_string_buffer(65536), w.DWORD()
                if not kernel.ReadFile(handle, buffer, 65536, ctypes.byref(count), None):
                    raise AssertionError("read")
                messages.append(json.loads(buffer.raw[:count.value]))
                response = b'{"ok":true}\n'
                if not kernel.WriteFile(handle, response, len(response), ctypes.byref(count), None):
                    raise AssertionError("write")
            except Exception as error:
                failures.append(type(error).__name__)
            finally:
                kernel.CloseHandle(handle)
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        with local.LocalStream(endpoint, 3) as stream:
            stream.send_json({"method": "fixture"})
            self.assertEqual(stream.line_json(), {"ok": True})
        thread.join(3)
        self.assertFalse(thread.is_alive())
        self.assertFalse(failures)
        self.assertEqual(messages, [{"method": "fixture"}])

    def test_cached_429_cooldown_and_401_invalidation(self):
        self.write(".claude/.credentials.json", {"claudeAiOauth": {"accessToken": "fixture"}})
        q = self.q(cache_dir=self.home / "cache", timeout=1)
        safe = result([window("primary_window", 25, time.time() + 3600)])
        with patch.object(local, "antigravity", return_value=result(configured=False)), patch.object(local, "kiro", return_value=result(configured=False)):
            with patch.object(q, "claude", return_value=safe):
                self.assertEqual(q.collect()["claude"]["windows"][0]["used"], 25)
            with patch.object(q, "claude", side_effect=RequestError("unavailable", 429, 60)) as request:
                self.assertTrue(q.collect()["claude"]["stale"])
                self.assertTrue(q.collect()["claude"]["stale"])
                self.assertEqual(request.call_count, 1)
            self.write(".claude/.credentials.json", {"claudeAiOauth": {"accessToken": "another-account"}})
            with patch.object(q, "claude", side_effect=RequestError("reauth", 401)):
                value = q.collect()["claude"]
                self.assertEqual(value["status"], "reauth")
                self.assertFalse(value["windows"])
                self.assertNotIn("claude", json.loads((self.home / "cache/quota.json").read_text())["providers"])

    def test_provider_deadline_does_not_wait_for_slow_request(self):
        q = self.q(timeout=.05)
        entered, release, finished = threading.Event(), threading.Event(), threading.Event()
        def slow():
            entered.set()
            release.wait(5)
            finished.set()
            return result([window("primary_window", 50)])
        with patch.object(local, "antigravity", return_value=result(configured=False)), patch.object(local, "kiro", return_value=result(configured=False)), patch.object(q, "claude", side_effect=slow):
            try:
                value = q.collect()["claude"]
                self.assertTrue(entered.wait(2))
                # 验证采集在请求仍阻塞时返回，避免把凭据磁盘读取和机器负载计入毫秒阈值。
                self.assertFalse(finished.is_set())
                self.assertEqual(value["status"], "unavailable")
            finally:
                release.set()
                self.assertTrue(finished.wait(2))

    def test_registry_all_eighteen_and_error_isolation(self):
        q = self.q(timeout=.1)
        with patch.object(local, "antigravity", return_value=result(configured=False)), patch.object(local, "kiro", return_value=result(configured=False)), patch.object(q, "claude", side_effect=RequestError("reauth")), patch.object(q, "codex", side_effect=RequestError("unavailable", 429, 120)):
            values = q.collect()
        self.assertEqual(len(values), 18)
        self.assertEqual(values["claude"]["status"], "reauth")
        self.assertEqual(values["codex"]["status"], "unavailable")
        self.assertEqual(values["devin"]["configured"], False)

    def test_cache_reset_and_age_not_reused(self):
        q = self.q()
        value = result([window("primary_window", 10, time.time() - 60), window("secondary_window", 20, time.time() + 60)])
        cached = q.cached(value)
        self.assertTrue(cached["stale"])
        self.assertEqual([w["id"] for w in cached["windows"]], ["secondary_window"])
        value["capturedAt"] = "2000-01-01"
        self.assertIsNone(q.cached(value))

    def test_quota_output_has_no_account_or_credentials(self):
        self.write(".claude/.credentials.json", {"claudeAiOauth": {"accessToken": "TOP_SECRET", "subscriptionType": "pro"}})
        q = self.q({"https://api.anthropic.com/api/oauth/usage": {"email": "private@example.com", "five_hour": {"utilization": 10}}})
        raw = json.dumps(q.claude())
        self.assertNotIn("TOP_SECRET", raw)
        self.assertNotIn("private@example.com", raw)
        self.assertNotIn(str(self.home), raw)


if __name__ == "__main__":
    unittest.main()
