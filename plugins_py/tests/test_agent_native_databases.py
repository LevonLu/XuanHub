# test_agent_native_databases.py — SQLite 用量解析、迁移历史和模型计价资格回归
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest

PLUGIN = Path(__file__).resolve().parents[1] / "agent_usage"
sys.path.insert(0, str(PLUGIN))
from native import databases
sys.path.pop(0)

STAMP = "2026-10-29T08:30:00Z"
MILLIS = 1793262600000


class DatabaseUsageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def fixture(self, tables):
        path = self.root / (str(len(list(self.root.iterdir()))) + ".db")
        db = sqlite3.connect(path)
        try:
            for table, values in tables.items():
                keys = list(dict.fromkeys(k for row in values for k in row))
                db.execute(f'CREATE TABLE "{table}" ({",".join(chr(34) + k + chr(34) for k in keys)})')
                for row in values:
                    db.execute(f'INSERT INTO "{table}" VALUES ({",".join("?" for _ in keys)})', [row.get(k) for k in keys])
            db.commit()
        finally:
            db.close()
        return path

    def events(self, parser, path, source=None):
        return list(filter(None, parser(path, source) if source else parser(path)))

    def message(self, key, provider="openai", input=100, output=20, reasoning=5):
        return {"id": key, "sessionID": "s", "role": "assistant", "providerID": provider, "modelID": "gpt-5",
                "time": {"created": MILLIS - 500, "completed": MILLIS},
                "tokens": {"input": input, "output": output, "reasoning": reasoning, "cache": {"read": 60, "write": 10}}}

    def test_opencode_reads_both_generations_and_latest_duplicate(self):
        old, duplicate, latest = self.message("old"), self.message("copy", input=1), self.message("copy", input=200)
        path = self.fixture({"message": [
            {"id": "old", "session_id": "s", "data": json.dumps(old), "time_updated": 1},
            {"id": "copy", "session_id": "s", "data": json.dumps(duplicate), "time_updated": 2}],
            "session_message": [{"id": "copy", "session_id": "s", "type": "assistant", "data": json.dumps(latest), "time_updated": 3},
                                {"id": "user", "session_id": "s", "type": "user", "data": json.dumps(latest), "time_updated": 4}]})
        result = self.events(databases.opencode, path)
        self.assertEqual(len(result), 2)
        self.assertEqual(sorted(r["input_tokens"] for r in result), [100, 200])
        self.assertEqual(sorted(r["source_updated"] for r in result), [1, 3])

    def test_mimo_native_providers_and_dumate_reasoning_subset(self):
        path = self.fixture({"message": [{"id": provider, "data": json.dumps(self.message(provider, provider=provider)), "time_updated": MILLIS}
                                         for provider in ("mimo", "xiaomi", "mimocode", "openai", "QianfanPersonalQuota")]})
        result = self.events(databases.opencode, path, "mimo")
        self.assertEqual(len(result), 2)
        result = self.events(databases.opencode, path, "dumate")
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["output_tokens"], 15)
        self.assertEqual(result[0]["reasoning_output_tokens"], 5)
        self.assertEqual(result[0]["total_tokens"], 190)

    def test_hermes_disjoint_counters_and_finished_time(self):
        path = self.fixture({"sessions": [{"id": "s", "model": None, "started_at": MILLIS / 1000 - 3600,
                                            "ended_at": MILLIS / 1000, "input_tokens": 100, "output_tokens": 20,
                                            "cache_read_tokens": 60, "cache_write_tokens": 10, "reasoning_tokens": 5}]})
        row = self.events(databases.hermes, path)[0]
        self.assertEqual(row["total_tokens"], 195)
        self.assertEqual(row["hour_start"], "2026-10-29T08:30:00.000Z")
        self.assertEqual(row["model"], "hermes-agent")
        self.assertTrue(row["cumulative"])

    def test_goose_accumulated_counters_reasoning_and_utc(self):
        path = self.fixture({"sessions": [
            {"id": "s", "model_config_json": json.dumps({"model_name": "gpt-5"}), "created_at": "2026-10-29 08:30:00",
             "input_tokens": 1, "output_tokens": 1, "total_tokens": 2,
             "accumulated_input_tokens": 100, "accumulated_output_tokens": 20, "accumulated_total_tokens": 150},
            {"id": "malformed", "model_config_json": json.dumps({"name": "gpt-5"}), "created_at": STAMP, "input_tokens": 100}]})
        result = self.events(databases.goose, path)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["reasoning_output_tokens"], 30)
        self.assertEqual(result[0]["hour_start"], "2026-10-29T08:30:00.000Z")
        self.assertEqual(result[0]["total_tokens"], 150)

    def test_anythingllm_pending_records_and_residual_reasoning(self):
        path = self.fixture({"workspace_chats": [
            {"id": 1, "createdAt": "2026-10-29 08:30:00", "response": json.dumps({"metrics": {"prompt_tokens": 100, "completion_tokens": 20, "total_tokens": 150}})},
            {"id": 2, "createdAt": STAMP, "response": json.dumps({"metrics": {"total_tokens": 1000}})},
            {"id": 3, "createdAt": "invalid", "lastUpdatedAt": STAMP, "response": json.dumps({"metrics": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 1}})}]})
        result = self.events(databases.anythingllm, path)
        self.assertEqual(len(result), 2)
        self.assertEqual([r["total_tokens"] for r in result], [150, 15])
        self.assertEqual(result[0]["reasoning_output_tokens"], 30)
        self.assertEqual(result[0]["hour_start"], "2026-10-29T08:30:00.000Z")

    def test_qoder_model_associations_and_message_not_request_identity(self):
        def message(key, session, request, model=None, usage=None):
            return {"id": key, "session_id": session, "request_id": request, "role": "assistant", "gmt_create": STAMP,
                    "model_info": json.dumps(model), "token_info": json.dumps(usage or {"prompt_tokens": 100, "cached_tokens": 60, "completion_tokens": 20})}
        path = self.fixture({
            "chat_message": [message("m1", "s1", "r", {"model_key": "direct"}), message("m2", "s1", "r"),
                             message("m3", "s2", "r2"), message("m4", "s3", "r3", usage={"prompt_tokens": -1, "completion_tokens": 5})],
            "chat_record": [{"request_id": "r", "extra": json.dumps({"modelConfig": {"key": "record"}})}],
            "chat_session": [{"session_id": "s2", "preferred_model_info": json.dumps({"preferredModel": "preferred"})}]})
        result = self.events(databases.qoder, path)
        self.assertEqual([r["model"] for r in result], ["direct", "record", "preferred"])
        self.assertEqual(len({r["event_id"] for r in result}), 3)
        self.assertEqual(result[0]["input_tokens"], 40)
        self.assertEqual(result[0]["total_tokens"], 120)

    def test_claude_science_primary_and_aux_cache_exclusions(self):
        path = self.fixture({"frames": [{"id": "f", "model": "claude-sonnet-4", "completed_at": "invalid", "updated_at": STAMP,
                                          "input_tokens": 100, "cache_read_tokens": 60, "cache_write_tokens": 10, "output_tokens": 20,
                                          "aux_input_tokens": 10, "aux_cache_read_tokens": 20, "aux_cache_write_tokens": 0, "aux_output_tokens": 5}]})
        row = self.events(databases.claude_science, path)[0]
        self.assertEqual(row["input_tokens"], 30)
        self.assertEqual(row["cached_input_tokens"], 80)
        self.assertEqual(row["total_tokens"], 145)

    def test_devin_canonical_oldest_session_newest_correction_and_bad_counts(self):
        def node(rowid, session, prompt, request="r"):
            return {"row_id": rowid, "session_id": session, "chat_message": json.dumps({"role": "assistant", "metadata": {
                "request_id": request, "generation_model": "gpt-5", "started_generation_at": STAMP,
                "metrics": {"input_tokens": prompt, "output_tokens": 20, "cache_read_tokens": 60, "cache_creation_tokens": 10}}})}
        path = self.fixture({"sessions": [{"id": "old", "created_at": MILLIS / 1000 - 1000}, {"id": "new", "created_at": MILLIS / 1000}],
                             "message_nodes": [node(1, "old", 100), node(2, "old", 120), node(3, "new", 900), node(4, "old", -1, "bad"), node(5, "old", 1.5, "fraction")]})
        result = self.events(databases.devin, path)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["input_tokens"], 120)
        self.assertEqual(result[0]["total_tokens"], 210)

    def test_unsloth_metered_qualification_and_local_api(self):
        metadata = lambda provider: json.dumps({"responseDetails": {"providerType": provider, "responseModelId": "model"},
                                              "contextUsage": {"promptTokens": 100, "completionTokens": 20, "totalTokens": 130,
                                                               "cachedTokens": 60, "cacheWriteTokens": 10, "reasoningTokens": 5}})
        path = self.fixture({"chat_messages": [{"id": provider, "role": "assistant", "created_at": STAMP, "metadata_json": metadata(provider)} for provider in ("qwen", "huggingface", "kimi", "openai", "google", "local")],
                             "api_usage_events": [{"id": "api", "model": "model", "prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15, "created_at": STAMP,
                                                   "cached_tokens": 8, "reasoning_tokens": 2}]})
        result = self.events(databases.unsloth, path)
        self.assertEqual([r["model"] for r in result], ["qwen/model", "huggingface/model", "kimi/model", "openai/model", "unpriced/google/model", "local/model", "local/model"])
        self.assertEqual([result[0][k] for k in ("input_tokens", "output_tokens", "cached_input_tokens", "cache_creation_input_tokens", "reasoning_output_tokens", "total_tokens")], [40, 15, 60, 10, 5, 130])
        self.assertEqual(result[-1]["cached_input_tokens"], 0)
        self.assertEqual(result[-1]["reasoning_output_tokens"], 0)

    def test_zed_zstd_requests_win_over_cumulative_imports_ignored(self):
        import zstandard
        record = {"model": {"provider": "anthropic", "model": "claude-sonnet-4"}, "request_token_usage": {"request": {"input_tokens": 100, "output_tokens": 20, "cache_read_input_tokens": 60, "cache_creation_input_tokens": 10, "output_tokens_details": {"thinking_tokens": 5}}},
                  "cumulative_token_usage": {"input_tokens": 9999}}
        path = self.fixture({"threads": [
            {"id": "zstd", "updated_at": STAMP, "data_type": "zstd", "data": zstandard.ZstdCompressor().compress(json.dumps(record).encode())},
            {"id": "imported", "updated_at": STAMP, "data_type": "json", "data": json.dumps({**record, "imported": True})},
            {"id": "fallback", "updated_at": STAMP, "data_type": "json", "data": json.dumps({"model": record["model"], "cumulative_token_usage": {"input_tokens": 10, "output_tokens": 5}})}]})
        result = self.events(databases.zed, path)
        self.assertEqual([r["total_tokens"] for r in result], [190, 15])
        self.assertEqual(result[0]["reasoning_output_tokens"], 0)
        self.assertTrue(result[0]["cumulative"])

    def test_workbuddy_does_not_treat_context_size_as_usage(self):
        context = self.fixture({"session_usage": [{"session_id": "s", "used": 5000, "size": 10000, "updated_at": MILLIS}]})
        self.assertEqual(self.events(databases.workbuddy, context), [])
        counters = self.fixture({"session_usage": [{"session_id": "s", "used": 5000, "size": 10000, "updated_at": MILLIS,
                                                     "cache_read_tokens": 60, "reasoning_tokens": 5}], "sessions": [{"id": "s", "model": "configured"}]})
        row = self.events(databases.workbuddy, counters)[0]
        self.assertEqual(row["total_tokens"], 65)
        self.assertEqual(row["model"], "configured")
        self.assertTrue(row["fallback"])

    def test_opensquilla_only_finalized_and_reconciles_six_columns(self):
        path = self.fixture({"usage_events": [{"event_id": "a", "status": "finalized", "completed_at_ms": MILLIS, "model": "gpt-5"},
                                              {"event_id": "b", "status": "pending", "completed_at_ms": MILLIS, "model": "gpt-5"}],
                             "usage_event_items": [{"event_id": key, "ordinal": 0, "model": None, "input_tokens": 100, "output_tokens": 30,
                                                    "reasoning_tokens": 5, "cache_read_tokens": 60, "cache_write_tokens": 10, "total_tokens": 140} for key in ("a", "b")]})
        result = self.events(databases.opensquilla, path)
        self.assertEqual(len(result), 1)
        self.assertEqual([result[0][k] for k in ("input_tokens", "output_tokens", "cached_input_tokens", "cache_creation_input_tokens", "reasoning_output_tokens", "total_tokens")], [40, 25, 60, 10, 5, 140])


if __name__ == "__main__":
    unittest.main()
