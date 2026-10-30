# test_agent_native_formats.py — 按固定上游格式构造专有采集器回归数据
from __future__ import annotations

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "agent_usage"))
from native import formats
from native.common import FIELDS, timestamp

T = "2026-10-29T02:00:00Z"
MS = timestamp(T)


def write(root, relative, data, lines=False):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(json.dumps(row) for row in data) + "\n" if lines else data if isinstance(data, str) else json.dumps(data)
    path.write_text(text, encoding="utf-8")
    os.utime(path, (MS / 1000, MS / 1000))
    return path


def build_cases(root):
    cases = []
    def add(name, relative, data, expected, *, lines=False, source=None):
        cases.append((name, write(root, relative, data, lines), source or name, expected))
    add("kimi", ".kimi/sessions/p/s/wire.jsonl", [{"timestamp": MS / 1000, "message": {"type": "StatusUpdate", "payload": {"message_id": "k1", "token_usage": {"input_other": 10, "output": 4, "input_cache_read": 3, "input_cache_creation": 2}}}}], ("kimi-for-coding", [19, 10, 4, 3, 2, 0]), lines=True)
    add("kimi", ".kimi-code/sessions/p/s/agents/a/wire.jsonl", [{"type": "config.update", "modelAlias": "openai/gpt-6"}, {"type": "context.append_loop_event", "time": MS, "event": {"type": "step.end", "uuid": "k2", "usage": {"input_tokens": 20, "output_tokens": 5, "input_tokens_details": {"cached_tokens": 7}}}}], ("gpt-6", [25, 13, 5, 7, 0, 0]), lines=True, source="kimi-code")
    add("craft", ".craft-agent/workspaces/p/sessions/c/session.jsonl", [{"id": "craft-session", "model": "craft-model", "lastMessageAt": MS, "tokenUsage": {"inputTokens": 10, "outputTokens": 4, "cacheReadTokens": 3, "cacheCreationTokens": 2}}], ("craft-model", [19, 10, 4, 3, 2, 0]), lines=True)
    add("reasonix", ".reasonix/projects/p/a.jsonl.telemetry.json", {"usage": {"promptTokens": 100, "completionTokens": 40, "cacheMissTokens": 60, "cacheHitTokens": 90, "cacheWriteTokens": 10, "reasoningTokens": 5}}, ("deepseek-v4", [140, 50, 35, 40, 10, 5]))
    write(root, ".reasonix/projects/p/a.jsonl.meta", {"model": "deepseek/deepseek-v4", "updated_at": T})
    model = {"payload_type": "run.model.configured", "payload": {"record": {"run_stream": {"id": "run-1"}, "model_id": "muse-model"}}}
    usage = {"payload_type": "runtime.session", "recorded_at": MS * 1000, "payload": {"run_id": "run-1", "event": {"kind": "goal_usage_attribution", "record": {"usage_id": "muse-use", "usage_family": "provider", "quantity": {"reported": True, "input_tokens": 100, "output_tokens": 40, "cached_tokens": 30, "reasoning_tokens": 5}}}}}
    add("muse", ".local/share/muse/sessions/s/session.jsonl", [{"retained_frame": True, "children": [{"record_json": json.dumps(model)}, {"record_json": json.dumps(usage)}]}], ("muse-model", [140, 70, 40, 30, 0, 5]), lines=True)
    add("qoder", ".qoder/projects/p/a.jsonl", [{"type": "assistant", "sessionId": "qoder-session", "timestamp": T, "message": {"id": "q1", "role": "assistant", "model": "qoder-custom-12345678-1234-1234-1234-123456789abc/deepseek-v4", "usage": {"input_tokens": 10, "output_tokens": 4, "cached_tokens": 3, "cache_creation_input_tokens": 2}}}], ("deepseek-v4", [19, 10, 4, 3, 2, 0]), lines=True)
    add("command_code", ".commandcode/projects/p/c.jsonl", [{"type": "session", "id": "command-session"}, {"type": "message", "id": "c1", "timestamp": T, "model": "deepseek/deepseek-v4", "usage": {"inputTokens": 100, "outputTokens": 20, "cacheReadTokens": 30, "cacheWriteTokens": 10}}], ("deepseek-v4", [120, 60, 20, 30, 10, 0]), lines=True, source="command-code")
    add("dsh", ".dsh/sessions/p/s/session.v3.jsonl", [{"type": "session/start", "data": {"sessionId": "dsh-session"}}, {"type": "request/header", "seq": 1, "data": {"header": {"config": {"model": "deepseek/deepseek-v4"}}}}, {"type": "assistant/message", "seq": 2, "time": MS, "data": {"usage": {"inputTokens": 10, "outputTokens": 4, "cacheReadTokens": 3, "cacheWriteTokens": 2, "reasoningTokens": 1}}}], ("deepseek-v4", [20, 10, 4, 3, 2, 1]), lines=True)
    add("droid", ".factory/sessions/p/d.settings.json", {"providerLock": "anthropic", "updatedAt": "2020-01-01T00:00:00Z", "tokenUsage": {"inputTokens": 10, "outputTokens": 4, "cacheReadTokens": 3, "cacheCreationTokens": 2, "thinkingTokens": 1, "totalTokens": 30}}, ("claude-opus-4-6", [30, 10, 4, 3, 2, 11]))
    write(root, ".factory/sessions/p/d.jsonl", '{"content":"Model: custom:Claude Opus 4.6 [max]"}\n')
    add("cline", ".cline/data/sessions/s/teammate.messages.json", {"messages": [{"id": "old", "role": "assistant", "ts": MS - 2000, "metrics": {"inputTokens": 999, "outputTokens": 999}}, {"id": "cl1", "role": "assistant", "ts": MS, "metrics": {"inputTokens": 100, "outputTokens": 40, "cacheReadTokens": 30, "cacheWriteTokens": 10, "reasoningTokenCount": 5, "cost": 0.123}}]}, ("cline-model", [140, 60, 40, 30, 10, 5]))
    write(root, ".cline/data/sessions/s/s.json", {"model": "cline-model", "metadata": {"importedFrom": {"importedAt": "2026-10-29T01:59:59Z"}}})
    payload = {"tokensIn": 10, "tokensOut": 4, "cacheReads": 3, "cacheWrites": 2, "inferenceProvider": "Moonshot AI", "apiProtocol": "Anthropic", "model": "must-not-guess"}
    add("roo", "roo/task/ui_messages.json", [{"say": "api_req_deleted", "ts": MS, "text": json.dumps(payload)}], ("roo-model", [19, 10, 4, 3, 2, 0]), source="roocode")
    write(root, "roo/task/api_conversation_history.json", [{"content": "<environment_details><model>roo-model</model></environment_details>"}])
    add("roo", "kilo/task/ui_messages.json", [{"say": "api_req_started", "ts": MS, "text": json.dumps(payload)}], ("provider:moonshot-ai", [19, 10, 4, 3, 2, 0]), source="kilocode")
    add("grok", ".grok/sessions/p/reported/updates.jsonl", [{"params": {"_meta": {"agentTimestampMs": MS, "eventId": "g1"}, "update": {"sessionUpdate": "turn_completed", "usage": {"inputTokens": 100, "outputTokens": 40, "cachedReadTokens": 30, "cacheCreationTokens": 10, "reasoningTokens": 5, "costUsdTicks": 1000000000}}}}], ("grok-4.5-build", [140, 60, 35, 30, 10, 5]), lines=True)
    write(root, ".grok/sessions/p/reported/signals.json", {"primaryModelId": "grok-4-5-build", "lastActiveAt": T, "totalTokens": 99999})
    add("grok", ".grok/sessions/p/fallback/signals.json", {"primaryModelId": "grok-build", "lastActiveAt": T, "totalTokens": 80, "contextTokensUsed": 60, "totalTokensBeforeCompaction": 50}, ("grok-build", [110, 88, 22, 0, 0, 0]))
    add("workbuddy_trace", ".workbuddy/traces/1/trace_s.json", {"trace": {"traceId": "tr1", "metadata": {"sessionId": "wb-session", "startedAt": T, "modelInfo": {"models": ["wb-model"], "totalInputTokens": 100, "totalOutputTokens": 20, "totalCachedTokens": 30}}}}, ("wb-model", [120, 70, 20, 30, 0, 0]), source="workbuddy")
    add("lmstudio", ".lmstudio/server-logs/a.log", '[2026-10-29 10:00:00][INFO]\nFinal response: {\n"id":"chatcmpl-fixture","model":"local-model","usage":{"prompt_tokens":100,"completion_tokens":40,"prompt_tokens_details":{"cached_tokens":30,"cache_write_tokens":10},"completion_tokens_details":{"reasoning_tokens":5}}}\n', ("local-model", [140, 60, 35, 30, 10, 5]))
    return cases


class FormatTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_fixed_upstream_shapes_have_expected_totals(self):
        for name, path, source, (model, values) in build_cases(self.root):
            with self.subTest(parser=name, source=source):
                rows = [r for r in getattr(formats, name)(path, source) if r]
                self.assertEqual(len(rows), 1)
                self.assertEqual(rows[0]["model"], model)
                self.assertEqual([rows[0][key] for key in FIELDS], values)
                self.assertEqual(rows[0]["source"], "kilo-code" if source == "kilocode" else "kimi" if source == "kimi-code" else source)

    def test_droid_model_fallback_and_total_gap(self):
        path = write(self.root, "d.settings.json", {"providerLock": "vertex-ai", "tokenUsage": {"inputTokens": 10, "totalTokens": 20}})
        row = next(formats.droid(path))
        self.assertEqual(row["model"], "gemini-unknown")
        self.assertEqual(row["output_tokens"], 10)
        self.assertEqual(row["cumulative_mode"], "reset")
        self.assertEqual(row["hour_start"], "2026-10-29T02:00:00.000Z")

    def test_dsh_stops_at_broken_middle_and_handles_zstd(self):
        path = write(self.root, "s/session.jsonl", [{"type": "request/header", "data": {"header": {"config": {"model": "p/model"}}}}, {"type": "assistant/message", "seq": 1, "time": MS, "data": {"usage": {"inputTokens": 10}}}], True)
        import zstandard
        compressed = path.with_suffix(".jsonl.zstd")
        compressed.write_bytes(zstandard.ZstdCompressor().compress(path.read_bytes()))
        self.assertEqual(list(formats.dsh(path)), list(formats.dsh(compressed)))
        with path.open("a", encoding="utf-8") as stream:
            stream.write('{broken\n' + json.dumps({"type": "assistant/message", "seq": 2, "time": MS, "data": {"usage": {"inputTokens": 50}}}) + "\n")
        self.assertEqual(sum(r["total_tokens"] for r in formats.dsh(path) if r), 10)

    def test_grok_context_highwater_then_signal_only_counts_growth(self):
        path = write(self.root, "g/updates.jsonl", [{"params": {"_meta": {"totalTokens": value, "eventId": str(i), "agentTimestampMs": MS}, "update": {"sessionUpdate": "thought"}}} for i, value in enumerate((100, 50, 120))], True)
        write(self.root, "g/signals.json", {"totalTokens": 150, "lastActiveAt": T})
        rows = [r for r in formats.grok(path) if r]
        self.assertEqual([r["total_tokens"] for r in rows], [100, 20, 30])
        self.assertTrue(all(r["usage_precision"] == "estimated" for r in rows))

    def test_roo_zero_placeholder_does_not_hide_completion(self):
        path = write(self.root, "task/ui_messages.json", [{"say": "api_req_started", "ts": MS, "text": "{}"}, {"say": "api_req_deleted", "ts": MS, "text": json.dumps({"tokensIn": 10, "apiProtocol": "Anthropic"})}])
        rows = [r for r in formats.roo(path) if r]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["model"], "protocol:anthropic")

    def test_qoder_excludes_nonassistant_and_no_guessed_credit_tokens(self):
        path = write(self.root, "q.jsonl", [{"type": "assistant", "timestamp": T, "message": {"role": "assistant", "usage": {"credits": 5}}}, {"type": "user", "timestamp": T, "message": {"role": "user", "usage": {"input_tokens": 100}}}], True)
        self.assertEqual([r for r in formats.qoder(path) if r], [])


if __name__ == "__main__":
    unittest.main()
