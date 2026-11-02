# test_token_usage.py — 列配置、账户隔离、余额估算与合并迁移回归
from datetime import datetime, timedelta
from decimal import Decimal
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "agent_usage"))
from configuration import migrate, validate_accounts, validate_layout, block, column
from blocks import resolve_columns
from native.accounts import Accounts, observe, business_day, account_key, fetch
from native.network import RequestError


class TokenUsageTests(unittest.TestCase):
    def test_http_coalesces_same_credential_only_and_copies_results(self):
        from native import network
        calls = []
        def perform(http, url, **kwargs):
            calls.append((url, kwargs))
            return {"value": [1]}
        with patch.object(network, "_coalesce", True), patch.object(network, "_requests", {}), patch.object(network.Http, "_perform", perform):
            first, second = network.Http(), network.Http()
            value = first("https://fixture.example/usage", headers={"Authorization": "fixture"})
            value["value"].append(2)
            self.assertEqual(second("https://fixture.example/usage", headers={"authorization": "fixture"}), {"value": [1]})
            self.assertEqual(len(calls), 1)
            second("https://fixture.example/usage", headers={"authorization": "another"})
            self.assertEqual(len(calls), 2)
            first("https://fixture.example/usage", body={"refresh": True})
            first("https://fixture.example/usage", body={"refresh": True})
            self.assertEqual(len(calls), 4)

    def test_targeted_quota_does_not_query_or_drop_other_providers(self):
        from native.quota import Quotas, result, window
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            path.joinpath("quota.json").write_text(json.dumps({"providers": {"other": {"identity": "kept", "value": {}}}}))
            quota = Quotas(path, {}, cache_dir=path)
            calls = []
            with patch.object(quota, "codex", side_effect=lambda: calls.append("codex") or result([window("primary_window", 1)])), patch.object(quota, "claude", side_effect=AssertionError("not selected")):
                value = quota.collect(only={"codex"})
            self.assertEqual(calls, ["codex"])
            self.assertEqual(set(value), {"codex"})
            self.assertIn("other", json.loads(path.joinpath("quota.json").read_text())["providers"])

    def test_legacy_columns_and_idempotence(self):
        config = {"barQuota_codex": "both", "legacyVendors": [{"id": "deepseek", "apiKey": "fixture", "enabled": True}], "unknown": 5}
        value = migrate(config)
        self.assertEqual(value["unknown"], 5)
        self.assertEqual(value, migrate(value))
        self.assertEqual(value["barColumns"][0]["layout"], "single")
        self.assertEqual(value["barColumns"][1]["layout"], "split")
        self.assertEqual(value["accounts"][0]["credential"], "fixture")
        self.assertNotIn("apiKey", value["accounts"][0])
        self.assertEqual(value["barColumns"][-1]["top"]["metric"], "spendToday")

    def test_explicit_columns_not_overwritten(self):
        existing = [column("x", block("tokens", "codex"))]
        value = migrate({"barColumns": existing, "legacyVendors": [{"id": "a", "apiKey": "fixture"}]})
        self.assertEqual(value["barColumns"], existing)

    def test_invalid_layout_and_accounts(self):
        for value in ({}, [column("a"), column("a")], [{"id": "a", "layout": "automatic"}]):
            with self.assertRaises(ValueError):
                validate_layout({"barColumns": value})
        for extra in ({"id": "auto:codex"}, {"intervalSec": 0}, {"apiUrl": "http://example.com"}, {"credential": {"$xuanhubSecret": "wrong"}}):
            with self.assertRaises(ValueError):
                validate_accounts({"accounts": [{"id": "a", "providerId": "deepseek", **extra}]})

    def test_blocks_independent_of_popup_and_missing_is_not_zero(self):
        columns = [column("a", block("tokens", "codex")), column("b", top=block("quota5h", "account"), bottom=block("balance", "missing"))]
        values = resolve_columns(columns, {"codex": {"total_tokens": 42}}, [{"id": "account", "windows": [{"id": "primary_window", "seconds": 18000, "used": 0}]}], 100, True)
        self.assertEqual(values[0]["single"]["value"], 42)
        self.assertEqual(values[1]["top"]["value"], 0)
        self.assertIsNone(values[1]["bottom"]["value"])
        self.assertEqual(columns[0]["single"], block("tokens", "codex"))
        monetary = [column("money", top=block("spendToday", "a"), bottom=block("balance", "a"))]
        value = resolve_columns(monetary, {}, [{"id": "a", "balances": [{"currency": "CNY", "balance": "10.00", "spendToday": "1.00", "affected": True}]}], 0, True)
        self.assertTrue(value[0]["top"]["affected"])
        self.assertFalse(value[0]["bottom"]["affected"])

    def test_daily_decimal_recharge_and_boundary(self):
        now = datetime(2026, 11, 2, 9, 0)
        state = observe(None, Decimal("10.10"), now, {})
        state = observe(state, Decimal("10.00"), now + timedelta(minutes=1), {})
        self.assertEqual(state["spent"], "0.10")
        state = observe(state, Decimal("30.00"), now + timedelta(minutes=2), {})
        self.assertEqual(state["spent"], "0.10")
        self.assertTrue(state["affected"])
        state = observe(state, Decimal("29.80"), now + timedelta(minutes=3), {})
        self.assertEqual(state["spent"], "0.30")
        state = observe(state, Decimal("29.70"), now + timedelta(days=1), {})
        self.assertEqual(state["spent"], "0")
        self.assertFalse(state["affected"])
        self.assertEqual(business_day(datetime(2026, 11, 2, 8), {"startHour": 8, "startMin": 30}), "2026-11-01")

    def test_unverified_legacy_baseline_not_imported(self):
        state = observe(None, Decimal(90), datetime(2026, 11, 2, 9), {}, {"start_date": "2026-11-02", "start_time": "08:30", "start_balance": 100, "current_balance": 95})
        self.assertEqual(state["spent"], "0")

    def test_account_cache_single_refresh_dedup_and_currency(self):
        calls = []
        balances = {"first": "10.00", "second": "20.00"}
        def factory(timeout):
            def http(url, **kwargs):
                key = kwargs["headers"]["Authorization"].split()[-1]
                calls.append(key)
                return {"balance_infos": [{"currency": "CNY", "total_balance": balances[key]}, {"currency": "USD", "total_balance": "1.00"}]}
            return http
        a = {"id": "a", "providerId": "deepseek", "credential": "first"}
        b = {**a, "id": "alias"}
        c = {**a, "id": "c", "credential": "second"}
        with tempfile.TemporaryDirectory() as folder:
            collector = Accounts(folder, factory)
            first = collector.collect([a, b, c])
            self.assertCountEqual(calls, ["first", "second"])
            self.assertEqual(len(first[0]["balances"]), 2)
            collector.collect([a, b, c])
            self.assertEqual(len(calls), 2)
            balances["first"] = "9.90"
            values = collector.collect([a, b, c], ["a"])
            self.assertEqual(len(calls), 3)
            self.assertEqual(values[0]["balances"][0]["spendToday"], "0.10")
            self.assertEqual(values[1]["balances"][0]["spendToday"], "0.10")
            self.assertNotEqual(account_key(a), account_key(c))
            a["credential"] = "second"
            values = collector.collect([a], ["*"])
            self.assertEqual(values[0]["balances"][0]["spendToday"], "0")
            self.assertNotIn("credential", json.dumps(values))

    def test_glm_exact_windows_and_go_full_cookie(self):
        calls = []
        def http(url, **kwargs):
            calls.append((url, kwargs))
            if "quota/limit" in url:
                return {"data": {"limits": [{"type": "TOKENS_LIMIT", "unit": 3, "number": 5, "percentage": 20}, {"type": "TOKENS_LIMIT", "unit": 6, "number": 1, "percentage": 10}]}}
            return b'rollingUsage:$R[0]={status:"ok",resetInSec:3600,usagePercent:20} useBalance:!0 You\'re subscribed to OpenCode Go'
        value = fetch({"providerId": "glm", "credential": "fixture"}, http)
        self.assertEqual([w["seconds"] for w in value["windows"]], [18000, 604800])
        value = fetch({"providerId": "opencode-go", "credential": "auth=fixture; other=1", "authMode": "cookie", "workspace": "wrk_fixture"}, http)
        self.assertEqual(calls[-1][1]["headers"]["Cookie"], "auth=fixture; other=1")
        self.assertTrue(value["subscribed"])
        self.assertTrue(value["useBalance"])
        self.assertEqual(value["windows"][0]["seconds"], 0)


if __name__ == "__main__":
    unittest.main()
