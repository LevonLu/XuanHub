# test_agent_native_extended.py — 用上游已确认格式构造复杂解析和加密快照回归
import hashlib
import hmac
import json
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

PLUGIN = Path(__file__).resolve().parents[1] / "agent_usage"
sys.path.insert(0, str(PLUGIN))
from native import extended as ext
from native.extended_antigravity import gen_info, proto_fields, estimate
from native.extended_reconcile import reconcile
from native.extended_trae import TRAE_APPLICATION_KEY, _checksum, encrypted_snapshot, normalize_trae_usage
sys.path.pop(0)

STAMP = "2026-10-29T08:30:00Z"
MILLIS = 1793262600000


def varint(value):
    output = bytearray()
    while value > 127:
        output.append((value & 127) | 128)
        value >>= 7
    return bytes(output + bytes([value]))


def field(number, value):
    if isinstance(value, int):
        return varint(number << 3) + varint(value)
    return varint(number << 3 | 2) + varint(len(value)) + value


class ExtendedUsageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def lines(self, name, values):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("".join(json.dumps(v, ensure_ascii=False) + "\n" for v in values), encoding="utf-8")
        return path

    def db(self, name, schema, table, values):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        db = sqlite3.connect(path)
        try:
            db.executescript(schema)
            for row in values:
                db.execute(f'INSERT INTO "{table}" VALUES ({",".join("?" for _ in row)})', row)
            db.commit()
        finally:
            db.close()
        return path

    def test_zcode_completed_only_inclusive_cache_and_reasoning(self):
        path = self.db("zcode.db", "CREATE TABLE model_usage (id, logical_request_id, attempt_index, session_id, provider_id, model_id, started_at, input_tokens, output_tokens, reasoning_tokens, cache_creation_input_tokens, cache_read_input_tokens, status)", "model_usage", [
            ("a", "r", 0, "s", "zai", "glm-5", MILLIS, 100, 30, 5, 10, 60, "completed"),
            ("b", "r", 1, "s", "zai", "glm-5", MILLIS, 100, 30, 5, 10, 60, "failed"),
            ("c", "r", 0, "s", "openai", "gpt-5", MILLIS, 100, 30, 5, 10, 60, "completed")])
        rows = list(filter(None, ext.zcode(path)))
        self.assertEqual(len(rows), 1)
        self.assertEqual([rows[0][k] for k in ("input_tokens", "output_tokens", "cached_input_tokens", "cache_creation_input_tokens", "reasoning_output_tokens", "total_tokens")], [30, 25, 60, 10, 5, 130])
        db = sqlite3.connect(path)
        try:
            db.executescript("CREATE TABLE message (id,session_id,time_created,time_updated,data); CREATE TABLE session_message (id,session_id,time_created,time_updated,type,data)")
            raw = {"role": "assistant", "model": {"providerID": "zai", "modelID": "glm-5"}, "tokens": {"input": 100, "output": 30, "reasoning": 5, "cache": {"read": 60, "write": 10}}}
            db.execute("INSERT INTO message VALUES (?,?,?,?,?)", ("old-v1", "s", MILLIS - 1000, MILLIS - 1000, json.dumps(raw)))
            db.execute("INSERT INTO session_message VALUES (?,?,?,?,?,?)", ("old-v2", "s", MILLIS - 500, MILLIS - 500, "assistant", json.dumps(raw)))
            db.execute("INSERT INTO message VALUES (?,?,?,?,?)", ("mirrored", "s", MILLIS, MILLIS, json.dumps(raw)))
            db.commit()
        finally:
            db.close()
        self.assertEqual(len(list(filter(None, ext.zcode(path)))), 3)

    def test_copilot_otel_cli_and_vscode_have_distinct_cache_write_semantics(self):
        attrs = {"gen_ai.operation.name": "chat", "gen_ai.usage.input_tokens": 100,
                 "gen_ai.usage.output_tokens": 30, "gen_ai.usage.cache_read.input_tokens": 60,
                 "gen_ai.usage.cache_write.input_tokens": 10, "gen_ai.usage.reasoning.output_tokens": 5,
                 "gen_ai.response.model": "claude-sonnet-4.5", "gen_ai.response.id": "r",
                 "gen_ai.conversation.id": "s"}
        path = self.lines("otel.jsonl", [{"type": "span", "attributes": attrs, "endTime": [1793262600, 0], "traceId": "trace", "spanId": "span"},
                                       {"type": "log", "attributes": attrs, "hrTime": [1793262600, 0]},
                                       {"scopeMetrics": [1], "attributes": attrs}])
        rows = list(filter(None, ext.copilot_otel(path)))
        self.assertEqual([r["input_tokens"] for r in rows], [30, 40])
        self.assertEqual([r["total_tokens"] for r in rows], [130, 140])
        self.assertEqual(rows[0]["model"], "claude-sonnet-4-5")
        self.assertEqual(rows[0]["copilot_match"]["session"], "s")

    def test_copilot_store_token_details_and_app_cli_exclusion(self):
        details = json.dumps([{"tokenType": key, "tokenCount": val} for key, val in (("input", 30), ("cache_read", 60), ("cache_write", 10), ("output", 30))])
        path = self.db("store.db", "CREATE TABLE assistant_usage_events (id,session_id,model,input_tokens,output_tokens,cache_read_tokens,cache_write_tokens,reasoning_tokens,token_details_json,created_at)", "assistant_usage_events", [(1, "s", "gpt-5", 100, 30, 0, 0, 5, details, STAMP)])
        row = next(ext.copilot_store(path))
        self.assertEqual(row["input_tokens"], 30)
        self.assertEqual(row["cached_input_tokens"], 60)
        path = self.db("app.db", "CREATE TABLE sessions (id, session_type,provider_id,model,updated_at,total_input_tokens,total_output_tokens,total_cached_tokens,total_reasoning_tokens)", "sessions", [
            ("app", "project", None, "gpt-5", STAMP, 100, 30, 60, 5),
            ("cli", "copilot-cli", None, "gpt-5", STAMP, 100, 30, 60, 5)])
        result = list(ext.copilot_app(path))
        self.assertEqual(len(result), 1)
        self.assertTrue(result[0]["cumulative"])
        self.assertEqual(result[0]["total_tokens"], 130)

    def test_openclaw_sqlite_skips_compaction_and_matches_transcript_identity(self):
        record = {"type": "message", "id": "id1", "timestamp": STAMP,
                  "message": {"role": "assistant", "model": "gpt-5", "usage": {"input": 100, "cacheRead": 80, "cacheWrite": 10, "output": 20, "totalTokens": 120}}}
        path = self.db("openclaw.sqlite", "CREATE TABLE transcript_events (session_id,seq,event_json); CREATE TABLE session_windows (session_id,reason); INSERT INTO session_windows VALUES ('s','normal'),('c','compaction');", "transcript_events", [("s", 1, json.dumps(record)), ("c", 1, json.dumps(record))])
        result = list(ext.openclaw_db(path))
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0], ext.openclaw_record(record))
        self.assertEqual(result[0]["input_tokens"], 20)
        self.assertEqual(result[0]["total_tokens"], 130)

    def test_kiro_v1_explicit_tokens_and_v2_estimates_and_migration_tags(self):
        path = self.root / "s.json"
        path.write_text(json.dumps({"session_id": "s", "session_state": {"rts_model_state": {"model_info": {"model_id": "kiro::anthropic.claude-sonnet-4-20250514-v1:0"}}, "conversation_metadata": {"user_turn_metadatas": [
            {"loop_id": {"rand": 10}, "message_ids": ["m1"], "input_token_count": 100, "output_token_count": 20, "start_timestamp": STAMP}]}}}), encoding="utf-8")
        result = list(filter(None, ext.kiro_cli(path)))
        self.assertEqual(result[0]["total_tokens"], 120)
        self.assertEqual(result[0]["model"], "claude-sonnet-4")
        self.assertEqual(result[0]["kiro_message_ids"], ["m1"])
        v2 = self.lines("sess_s/messages.jsonl", [
            {"timestamp": STAMP, "payload": {"type": "user", "content": "abcdefgh"}},
            {"timestamp": STAMP, "payload": {"type": "turn_start", "executionId": "e"}},
            {"id": "m2", "timestamp": STAMP, "payload": {"type": "assistant", "content": "abcd", "reasoningModelId": "gpt-5"}},
            {"id": "m3", "timestamp": STAMP, "payload": {"type": "assistant", "content": "abcdefgh", "operationType": "reasoning"}},
            {"timestamp": STAMP, "payload": {"type": "tool_result", "content": "abcd"}},
            {"timestamp": STAMP, "payload": {"type": "turn_end"}}])
        result = list(filter(None, ext.kiro_cli(v2)))
        self.assertEqual([result[0][k] for k in ("input_tokens", "output_tokens", "reasoning_output_tokens")], [3, 1, 2])
        self.assertEqual(result[0]["usage_precision"], "estimated")

    def test_kiro_db_estimates_request_characters(self):
        path = self.db("kiro.db", "CREATE TABLE conversations_v2 (conversation_id,value)", "conversations_v2", [("s", json.dumps({"model_info": {"model_id": "gpt-5"}, "user_turn_metadata": {"requests": [{"request_id": "r", "message_id": "m", "request_start_timestamp_ms": MILLIS, "user_prompt_length": 400, "response_size": 80}]}}))])
        row = next(ext.kiro_cli_db(path))
        self.assertEqual(row["total_tokens"], 120)
        self.assertEqual(row["kiro_lane"], "db")
        self.assertEqual(row["kiro_message_ids"], ["m"])

    def test_kiro_ide_utc_and_chat_model_timeline(self):
        path = self.db("kiro/dev_data/devdata.sqlite", "CREATE TABLE tokens_generated (id,model,tokens_prompt,tokens_generated,timestamp)", "tokens_generated", [(1, "agent", 100, 20, "2026-10-29 08:30:00")])
        chat = self.root / "kiro/session/session.chat"
        chat.parent.mkdir()
        chat.write_text(json.dumps({"metadata": {"modelId": "CLAUDE_SONNET_4_20250514_V1_0", "startTime": MILLIS, "endTime": MILLIS + 1000}}), encoding="utf-8")
        row = next(ext.kiro_ide(path))
        self.assertEqual(row["model"], "claude-sonnet-4")
        self.assertEqual(row["hour_start"], "2026-10-29T08:30:00.000Z")

    def test_codebuddy_jsonl_and_extension_log_overlap_fingerprints(self):
        raw = {"prompt_tokens": 100, "completion_tokens": 20, "prompt_tokens_details": {"cached_tokens": 60}}
        path = self.lines("buddy.jsonl", [{"timestamp": MILLIS, "providerData": {"rawUsage": raw, "model": "gpt-5", "messageId": "m"}}])
        row = next(ext.codebuddy(path))
        log = self.root / "buddy.log"
        log.write_text(f"[{STAMP}] [CraftInvokableAgent] [agent] Model prepared: GPT5 (gpt-5)\n[{STAMP}] [AgentReporter] [agent] Agent execution successful with usage: " + json.dumps({"input_tokens": 100, "output_tokens": 20, "cacheTokens": 60}) + "\n", encoding="utf-8")
        log_row = next(ext.codebuddy(log))
        self.assertEqual(row["total_tokens"], 120)
        self.assertEqual(row["codebuddy_match"], log_row["codebuddy_match"])

    def test_antigravity_proto_usage_beats_text_estimates(self):
        usage = field(1, 5) + field(2, 15) + field(3, 30) + field(5, 80) + field(10, 10)
        blob = field(1, field(4, usage) + field(19, b"gemini-3-pro") + field(20, field(1, b"last_step_index") + field(2, b"0")))
        self.assertEqual(gen_info(blob)["usage"]["total_tokens"], 130)
        path = self.lines("antigravity/brain/s/.system_generated/logs/transcript.jsonl", [
            {"type": "USER_INPUT", "created_at": STAMP, "content": "abcd"},
            {"type": "PLANNER_RESPONSE", "step_index": 1, "created_at": STAMP, "content": "xyz", "thinking": "think"}])
        dbpath = self.db("antigravity/conversations/s.db", "CREATE TABLE gen_metadata (idx,data)", "gen_metadata", [(1, blob)])
        row = next(ext.antigravity(path))
        self.assertEqual(row["total_tokens"], 130)
        self.assertEqual(row["usage_precision"], "reported")
        dbpath.unlink()
        estimate_row = next(ext.antigravity(path))
        self.assertEqual(estimate_row["total_tokens"], 4)
        self.assertEqual(estimate_row["usage_precision"], "estimated")
        self.assertEqual(estimate("中文abcd"), 3)
        self.assertIsNone(gen_info(b"\x0a\xff"))
        with self.assertRaises(ValueError):
            proto_fields(b"\x00")

    def test_trae_scope_reconciliation_and_unpriced_remainder(self):
        usage = normalize_trae_usage({"prompt_tokens": 100, "prompt_tokens_total": 1000, "completion_tokens": 30,
                                     "completion_tokens_total": 300, "cache_read_input_tokens": 60,
                                     "cache_creation_input_tokens": 10, "reasoning_tokens": 20, "total_tokens": 130})
        self.assertEqual(usage["total_tokens"], 1300)
        self.assertEqual(usage["input_tokens"], 30)
        self.assertEqual(usage["output_tokens"], 280)
        self.assertEqual(usage["unpriced_input_tokens"], 900)
        self.assertIsNone(normalize_trae_usage({"prompt_tokens": 100, "completion_tokens": 30, "cache_read_input_tokens": 50}))
        self.assertIsNone(normalize_trae_usage({"prompt_tokens": "100", "completion_tokens": 30}))
        self.assertIsNone(normalize_trae_usage({"prompt_tokens": 100, "input_tokens": 99, "completion_tokens": 30}))
        usage = normalize_trae_usage({"prompt_tokens": 100, "completion_tokens": 30, "total_tokens": 140}, "gemini-3-pro")
        self.assertEqual(usage["reasoning_output_tokens"], 10)
        self.assertEqual(usage["usage_precision"], "estimated")

    def test_reconcile_consumes_mirror_once_and_keeps_unmigrated_turn(self):
        match = {"session": "s", "model": "gpt-5", "time": MILLIS, "tokens": [120, 100, 20, 0, 0, 0]}
        records = [
            {"event_id": "db", "copilot_lane": "store", "copilot_match": match},
            {"event_id": "otel1", "copilot_lane": "otel-cli", "copilot_match": {**match, "time": MILLIS + 2000}},
            {"event_id": "otel2", "copilot_lane": "otel-cli", "copilot_match": match},
            {"event_id": "buddy-json", "codebuddy_lane": "jsonl", "codebuddy_match": "fingerprint"},
            {"event_id": "buddy-log1", "codebuddy_lane": "log", "codebuddy_match": "fingerprint"},
            {"event_id": "buddy-log2", "codebuddy_lane": "log", "codebuddy_match": "fingerprint"},
            {"event_id": "kiro-db", "kiro_lane": "db", "kiro_message_ids": ["m1"]},
            {"event_id": "kiro-old", "kiro_lane": "session", "kiro_message_ids": ["m1", "m2"]},
            {"event_id": "kiro-live", "kiro_lane": "session", "kiro_message_ids": ["m3"]}]
        self.assertEqual({x["event_id"] for x in reconcile(records)}, {"db", "otel2", "buddy-json", "buddy-log2", "kiro-db", "kiro-live"})
        # 2000 ms 是包含边界，2001 ms 不足以证明两个来源属于同一请求。
        records[1]["copilot_match"]["time"] += 1
        self.assertIn("otel1", {x["event_id"] for x in reconcile(records)})

    def _trae_clear(self, prompt=100):
        db = sqlite3.connect(":memory:")
        db.execute("PRAGMA page_size=4096")
        db.execute("CREATE TABLE chat_turn (id INTEGER PRIMARY KEY, session_id TEXT,turn_id TEXT,created_at INTEGER,context TEXT)")
        db.execute("INSERT INTO chat_turn VALUES (1,'s','t',?,?)", (MILLIS, json.dumps({"model_name": "gpt-5", "token_usage": {"prompt_tokens": prompt, "completion_tokens": 20, "cache_read_input_tokens": 80, "total_tokens": prompt + 20}})))
        data = bytearray(db.serialize())
        db.close()
        # 挪动小型叶子页，为测试 SQLCipher 页保留 80 字节尾部。
        for offset in range(0, len(data), 4096):
            header = offset + (100 if offset == 0 else 0)
            self.assertEqual(data[header], 13)
            count, content = struct.unpack_from(">HH", data, header + 3)
            data[offset + content - 80:offset + 4016] = data[offset + content:offset + 4096]
            data[offset + 4016:offset + 4096] = b"\x00" * 80
            struct.pack_into(">H", data, header + 5, content - 80)
            for index in range(count):
                pointer = header + 8 + 2 * index
                struct.pack_into(">H", data, pointer, struct.unpack_from(">H", data, pointer)[0] - 80)
        data[20] = 80
        return data

    def _encrypt(self, plaintext):
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        key, salt = bytes.fromhex(TRAE_APPLICATION_KEY), b"5" * 16
        mac_key = hashlib.pbkdf2_hmac("sha512", key, bytes(v ^ 0x3A for v in salt), 2, 32)
        encrypted = bytearray(len(plaintext))
        for offset in range(0, len(plaintext), 4096):
            number = offset // 4096 + 1
            start, iv = (16 if number == 1 else 0), bytes([number]) * 16
            cipher = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
            if number == 1:
                encrypted[:16] = salt
            encrypted[offset + start:offset + 4016] = cipher.update(plaintext[offset + start:offset + 4016]) + cipher.finalize()
            encrypted[offset + 4016:offset + 4032] = iv
            encrypted[offset + 4032:offset + 4096] = hmac.new(mac_key, encrypted[offset + start:offset + 4032] + struct.pack("<I", number), "sha512").digest()
        return encrypted

    def _wal(self, frames, endian):
        data = bytearray(32)
        struct.pack_into(">IIIIII", data, 0, 0x377F0683 if endian == ">" else 0x377F0682, 3007000, 4096, 0, 33, 71)
        state = _checksum(data[:24], endian)
        struct.pack_into(">II", data, 24, *state)
        for number, commit, page in frames:
            frame = bytearray(24) + page
            struct.pack_into(">II", frame, 0, number, commit)
            frame[8:16] = data[16:24]
            state = _checksum(page, endian, _checksum(frame[:8], endian, state))
            struct.pack_into(">II", frame, 16, *state)
            data.extend(frame)
        return data

    def test_trae_encrypted_sqlite_does_not_write_cleartext_and_checks_hmac(self):
        path = self.root / "database.db"
        encrypted = self._encrypt(self._trae_clear())
        path.write_bytes(encrypted)
        before = set(self.root.iterdir())
        row = next(ext.trae(path))
        self.assertEqual(row["total_tokens"], 120)
        self.assertEqual(set(self.root.iterdir()), before)
        self.assertEqual(path.read_bytes(), encrypted)
        encrypted[-1] ^= 1
        path.write_bytes(encrypted)
        with self.assertRaisesRegex(ValueError, "authentication"):
            list(ext.trae(path))

    def test_trae_encrypted_wal_only_replays_committed_frames_both_endians(self):
        base, committed, later = [self._encrypt(self._trae_clear(v)) for v in (100, 200, 300)]
        path = self.root / "database.db"
        path.write_bytes(base)
        for endian in ("<", ">"):
            frames = [(number, len(committed) // 4096 if number == len(committed) // 4096 else 0, committed[(number - 1) * 4096:number * 4096]) for number in range(1, len(committed) // 4096 + 1)]
            frames.append((2, 0, later[4096:8192]))
            Path(str(path) + "-wal").write_bytes(self._wal(frames, endian))
            row = next(ext.trae(path))
            self.assertEqual(row["total_tokens"], 220)


if __name__ == "__main__":
    unittest.main()
