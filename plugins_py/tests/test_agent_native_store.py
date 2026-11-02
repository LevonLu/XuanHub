# test_agent_native_store.py — 用量事务发布、累计历史、详情查询与引擎接缝回归
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch

PLUGIN = Path(__file__).resolve().parents[1] / "agent_usage"
sys.path.insert(0, str(PLUGIN))
from native import engine, network
from native.common import iso, timestamp, totals
from native.store import Store
sys.path.pop(0)

D1 = "2026-10-28T08:30:00Z"
D2 = "2026-10-29T08:30:00Z"


class NativeStoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / "native-v2/usage.sqlite3"
        self.store = Store(self.path)
        self.addCleanup(self.store.close)

    def row(self, key="e", when=D1, source="test", input=100, output=0, **extra):
        return {"event_id": source + ":" + key, "source": source, "model": "model", "hour_start": when,
                **totals(input, output), **extra}

    def query(self, source="test", store=None, **extra):
        request = {"lane": "detail", "sourceId": source, "fromMs": 0, "toMs": timestamp("2100-01-01T00:00:00Z"), **extra}
        return (store or self.store).query(request)

    def publish(self, entries, file="file", signature="signature", **kwargs):
        self.store.begin()
        self.store.replace(file, signature, entries, **kwargs)
        self.store.commit()

    def test_legacy_cost_fields_do_not_enter_usage_or_detail(self):
        self.publish([self.row(total_cost_usd=12, cost=12, unpriced=True, usage_precision="estimated")])
        usage = self.query(lane="usage")
        detail = self.query()
        aggregates = usage["sources"] + [detail["totals"]] + detail["byDay"] + detail["byModel"]
        for row in aggregates:
            self.assertEqual(row["total_tokens"], 100)
            self.assertTrue(row["estimated"])
            for field in ("cost", "total_cost_usd", "unpriced"):
                self.assertNotIn(field, row)

    def test_legacy_cost_only_counter_change_does_not_create_event(self):
        self.publish([self.row(cumulative=True, source_updated=100, total_cost_usd=1)])
        self.publish([self.row(cumulative=True, source_updated=200, total_cost_usd=2)], signature="cost-only")
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM events").fetchone()[0], 1)
        self.assertEqual(self.query()["totals"]["total_tokens"], 100)

    def test_transaction_visibility_and_interrupted_writer_rollback(self):
        self.publish([self.row(input=100)])
        writer = Store(self.path)
        reader = Store(self.path, readonly=True)
        try:
            writer.begin()
            writer.replace("file", "new", [self.row(input=200)])
            self.assertEqual(self.query(store=reader)["totals"]["total_tokens"], 100)
            writer.close()
            writer = None
            self.assertEqual(self.query(store=reader)["totals"]["total_tokens"], 100)
            self.assertEqual(self.store.signature("file"), "signature")
        finally:
            if writer:
                writer.close()
            reader.close()

    def test_savepoint_rolls_back_partly_written_rows_and_counter_baseline(self):
        self.publish([self.row(input=100)])
        self.store.begin()
        with self.assertRaises(KeyError):
            self.store.replace("file", "broken", [self.row(key="counter", input=20, cumulative=True), {"source": "test"}])
        self.store.commit()
        self.assertEqual(self.query()["totals"]["total_tokens"], 100)
        self.assertEqual(self.store.signature("file"), "signature")
        self.assertEqual(self.store.db.execute("SELECT COUNT(*) FROM counters").fetchone()[0], 0)

    def test_generator_failure_preserves_previous_complete_file(self):
        self.publish([self.row(input=100)])
        def broken():
            yield self.row(input=200)
            raise ValueError("incomplete")
        self.store.begin()
        with self.assertRaises(ValueError):
            self.store.replace("file", "new", broken())
        self.store.commit()
        self.assertEqual(self.query()["totals"]["total_tokens"], 100)
        self.assertEqual(self.store.signature("file"), "signature")

    def test_cumulative_cross_day_history_unchanged_scan_and_old_copy(self):
        self.publish([self.row(input=100, cumulative=True, source_updated=100)])
        self.publish([self.row(input=150, when=D2, cumulative=True, source_updated=200)], signature="next")
        self.publish([self.row(input=150, when=D2, cumulative=True, source_updated=200)], signature="same")
        self.publish([self.row(input=50, cumulative=True, source_updated=50)], file="old-copy")
        result = self.query()
        self.assertEqual(result["totals"]["total_tokens"], 150)
        self.assertEqual([row["total_tokens"] for row in result["byDay"]], [50, 100])

    def test_counter_total_reset_preserves_old_history(self):
        self.publish([self.row(input=100, cumulative=True, counter_reset=True, source_updated=100)])
        self.publish([self.row(input=40, when=D2, cumulative=True, counter_reset=True, source_updated=200)], signature="reset")
        self.assertEqual(self.query()["totals"]["total_tokens"], 140)

    def test_equal_timestamp_counter_copies_cannot_roll_back_baseline(self):
        for reset in (False, True):
            with self.subTest(reset=reset):
                key = "counter-reset" if reset else "counter-monotonic"
                for index, amount in enumerate((100, 50, 100)):
                    self.publish([self.row(key=key, input=amount, cumulative=True, counter_reset=reset,
                                           source_updated=100)], file=key + str(index))
                baseline = json.loads(self.store.db.execute("SELECT data FROM counters WHERE event_id=?", ("test:" + key,)).fetchone()[0])
                self.assertEqual(baseline["total_tokens"], 100)
        self.assertEqual(self.query()["totals"]["total_tokens"], 200)

    def test_counter_any_component_reset_even_when_total_unchanged(self):
        first = {**self.row(cumulative=True, counter_reset=True, counter_reset_any=True, source_updated=100), **totals(100, cached=20)}
        second = {**self.row(when=D2, cumulative=True, counter_reset=True, counter_reset_any=True, source_updated=200), **totals(110, cached=10)}
        self.publish([first])
        self.publish([second], signature="reset")
        self.assertEqual(self.query()["totals"]["total_tokens"], 240)

    def test_cumulative_total_matches_sum_of_clamped_component_deltas(self):
        first = self.row(cumulative=True, source_updated=100)
        second = {**self.row(when=D2, cumulative=True, source_updated=200), **totals(50, cached=60)}
        self.publish([first])
        self.publish([second], signature="corrected-split")
        result = self.query()
        self.assertEqual(result["totals"]["total_tokens"], 160)
        self.assertEqual(sum(result["totals"][k] for k in ("input_tokens", "output_tokens", "cached_input_tokens", "cache_creation_input_tokens", "reasoning_output_tokens")), 160)

    def test_latest_event_copy_and_zero_tombstone_precede_date_filter(self):
        self.publish([self.row(input=100, source_updated=100)], file="old")
        self.publish([self.row(input=200, source_updated=200)], file="new")
        self.assertEqual(self.query()["totals"]["total_tokens"], 200)
        self.publish([self.row(input=0, when=D2, source_updated=300)], file="new", signature="zero")
        result = self.query(toMs=timestamp(D2))
        self.assertFalse(result["hasRecords"])
        self.assertEqual(result["totals"]["total_tokens"], 0)

    def test_detailed_session_suppresses_fallback_before_date_filter(self):
        self.publish([self.row(key="fallback", input=100, cumulative=True, fallback=True, session_id="s")], file="database")
        self.publish([self.row(key="detail", input=20, when=D2, session_id="s")], file="transcript")
        self.assertEqual(self.query()["totals"]["total_tokens"], 20)
        self.assertFalse(self.query(toMs=timestamp(D2))["hasRecords"])

    def test_local_calendar_day_and_exclusive_end(self):
        start = datetime(2026, 10, 29)
        end = start + timedelta(days=1)
        moments = (start - timedelta(minutes=30), start, end - timedelta(minutes=30), end)
        self.publish([self.row(key=str(i), when=iso(int(moment.timestamp() * 1000)), input=(i + 1) * 10) for i, moment in enumerate(moments)])
        result = self.query(fromMs=int(start.timestamp() * 1000), toMs=int(end.timestamp() * 1000))
        self.assertEqual(result["totals"]["total_tokens"], 50)
        self.assertEqual([row["day"] for row in result["byDay"]], ["2026-10-29"])

    def test_account_append_retains_old_windows_and_marks_scope(self):
        self.publish([self.row(key="old", source="cursor", input=100)], file="account:cursor", append=True)
        self.publish([self.row(key="new", source="cursor", input=50, when=D2)], file="account:cursor", signature="new-window", append=True)
        self.assertEqual(self.query(source="cursor")["totals"]["total_tokens"], 150)
        self.assertEqual(self.query(source="cursor")["scope"], "account")

    def test_readonly_detail_rejects_writes_and_does_not_collect(self):
        self.publish([self.row(input=100)])
        readonly = Store(self.path, readonly=True)
        try:
            with self.assertRaises(sqlite3.OperationalError):
                readonly.set_metadata("forbidden", True)
        finally:
            readonly.close()
        before = hashlib.sha256(self.path.read_bytes()).hexdigest()
        request = {"cacheDir": str(self.root), "lane": "detail", "sourceId": "test", "fromMs": 0, "toMs": timestamp(D2)}
        with patch.object(engine, "discover", side_effect=AssertionError("detail scanned sources")), patch.object(engine.account_usage, "collect", side_effect=AssertionError("detail fetched account")), patch.object(network.Http, "__call__", side_effect=AssertionError("detail fetched network")):
            result = engine.collect(request, home=self.root, env={})
        self.assertEqual(result["totals"]["total_tokens"], 100)
        self.assertEqual(hashlib.sha256(self.path.read_bytes()).hexdigest(), before)

    def test_engine_file_failure_preserves_prior_and_publishes_other_source(self):
        one, two = self.root / "one", self.root / "two"
        one.write_text("old", encoding="utf-8")
        two.write_text("old", encoding="utf-8")
        request = {"cacheDir": str(self.root), "lane": "usage", "fromMs": 0, "toMs": timestamp(D2)}
        discovered = [("first", "formats.test", one), ("second", "formats.test", two)]
        state = {"fail": False}
        def parser(path, source):
            if state["fail"] and source == "first":
                yield self.row(source=source, input=999)
                raise ValueError("private detail must not leak")
            yield self.row(source=source, input=200 if state["fail"] else 100)
        with patch.object(engine, "discover", return_value=discovered), patch.object(engine, "parser_for", return_value=parser):
            engine.collect(request, home=self.root, env={}, accounts=False)
            state["fail"] = True
            one.write_text("changed one", encoding="utf-8")
            two.write_text("changed two", encoding="utf-8")
            result = engine.collect(request, home=self.root, env={}, accounts=False)
        self.assertEqual({s["id"]: s["total_tokens"] for s in result["sources"]}, {"first": 100, "second": 200})
        self.assertEqual(result["sourceErrors"], {"first": "parse_failed"})

    def test_local_usage_collection_does_not_fetch_or_write_prices(self):
        request = {"cacheDir": str(self.root), "lane": "usage", "fromMs": 0, "toMs": timestamp(D2)}
        with patch.object(engine, "discover", return_value=[]), patch.object(network.Http, "__call__", side_effect=AssertionError("usage fetched network")):
            result = engine.collect(request, home=self.root, env={}, accounts=False)
        self.assertEqual(result["sources"], [])
        self.assertFalse((self.path.parent / "prices.json").exists())

    def test_engine_account_append_keeps_history_across_shorter_response(self):
        request = {"cacheDir": str(self.root), "lane": "usage", "fromMs": 0, "toMs": timestamp("2026-10-30T00:00:00Z")}
        responses = [({"cursor": [self.row(key="old", source="cursor", input=100)]}, {}),
                     ({"cursor": [self.row(key="new", source="cursor", input=50, when=D2)]}, {})]
        with patch.object(engine, "discover", return_value=[]), patch.object(engine.account_usage, "collect", side_effect=responses):
            engine.collect(request, home=self.root, env={})
            result = engine.collect(request, home=self.root, env={})
        self.assertEqual(result["sources"][0]["total_tokens"], 150)

    def test_engine_source_alias_failure_is_visible_to_detail(self):
        path = self.root / "source.jsonl"
        path.write_text("{}", encoding="utf-8")
        request = {"cacheDir": str(self.root), "lane": "usage", "fromMs": 0, "toMs": timestamp(D2)}
        for raw_source, source in (("kilocode", "kilo-code"), ("kimi-code", "kimi")):
            with self.subTest(source=source), patch.object(engine, "discover", return_value=[(raw_source, "formats.test", path)]), patch.object(engine, "parser_for", side_effect=ValueError("broken source")):
                result = engine.collect(request, home=self.root, env={}, accounts=False)
                self.assertEqual(result["sourceErrors"], {source: "parse_failed"})
                detail = engine.collect({**request, "lane": "detail", "sourceId": source}, home=self.root, env={}, accounts=False)
                self.assertEqual(detail["sourceError"], "parse_failed")

    def test_engine_kimi_rotated_log_keeps_previous_day(self):
        path = self.root / "kimi/messages.jsonl"
        path.parent.mkdir()
        request = {"cacheDir": str(self.root), "lane": "usage", "fromMs": 0, "toMs": timestamp("2026-10-30T00:00:00Z")}
        def write_message(key, when, count):
            record = {"timestamp": timestamp(when) // 1000, "message": {"type": "StatusUpdate", "payload": {
                "message_id": key, "token_usage": {"input_other": count}}}}
            path.write_text(json.dumps(record) + "\n", encoding="utf-8")
        with patch.object(engine, "discover", return_value=[("kimi", "formats.kimi", path)]):
            write_message("old", D1, 100)
            engine.collect(request, home=self.root, env={}, accounts=False)
            write_message("new", D2, 50)
            result = engine.collect(request, home=self.root, env={}, accounts=False)
            detail = engine.collect({**request, "lane": "detail", "sourceId": "kimi"}, home=self.root, env={}, accounts=False)
        self.assertEqual(result["sources"][0]["total_tokens"], 150)
        self.assertEqual([day["total_tokens"] for day in detail["byDay"]], [50, 100])

    def test_signature_watches_kiro_cli_message_and_metadata_companions(self):
        legacy = self.root / "s.json"
        legacy.write_text("{}", encoding="utf-8")
        sibling = self.root / "s.jsonl"
        sibling.write_text("{}", encoding="utf-8")
        before = engine.signature(legacy, "extended.kiro_cli")
        sibling.write_text('{"content":"new"}', encoding="utf-8")
        self.assertNotEqual(engine.signature(legacy, "extended.kiro_cli"), before)
        messages = self.root / "sess_s/messages.jsonl"
        messages.parent.mkdir()
        messages.write_text("{}", encoding="utf-8")
        meta = messages.parent / "session.json"
        meta.write_text("{}", encoding="utf-8")
        before = engine.signature(messages, "extended.kiro_cli")
        meta.write_text('{"modelId":"new-model"}', encoding="utf-8")
        self.assertNotEqual(engine.signature(messages, "extended.kiro_cli"), before)

    def test_signature_watches_kiro_ide_model_timeline(self):
        db = self.root / "kiro/dev_data/devdata.sqlite"
        db.parent.mkdir(parents=True)
        db.write_bytes(b"fixture")
        chat = self.root / "kiro/session/session.chat"
        chat.parent.mkdir()
        chat.write_text("{}", encoding="utf-8")
        before = engine.signature(db, "extended.kiro_ide")
        chat.write_text('{"metadata":{"modelId":"new-model"}}', encoding="utf-8")
        self.assertNotEqual(engine.signature(db, "extended.kiro_ide"), before)

    def test_known_trae_failure_codes_are_actionable_without_raw_error_text(self):
        self.assertEqual(engine.error_code(ValueError("trae_database_too_large")), "trae_database_too_large")
        self.assertEqual(engine.error_code(ValueError("trae_sqlcipher_authentication")), "trae_decryption_failed")
        self.assertEqual(engine.error_code(ValueError("arbitrary private detail")), "parse_failed")


if __name__ == "__main__":
    unittest.main()
