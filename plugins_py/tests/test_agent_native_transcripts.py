# test_agent_native_transcripts.py — 固定上游会话语义的合成回归与差分样本
from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "agent_usage"))
from native import transcripts
from native.common import FIELDS, timestamp

T = "2026-09-20T04:00:00.000Z"
HOUR = T
MS = timestamp(T)
SESSION = "019f0000-0000-7000-8000-000000000652"


def write(root, relative, data, lines=True):
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(json.dumps(row, separators=(",", ":")) for row in data) + "\n" if lines else json.dumps(data)
    path.write_text(text, encoding="utf-8")
    return path


def usage(input=0, cached=0, output=0, reasoning=0, write=0):
    return dict(input_tokens=input, cached_input_tokens=cached, cache_write_input_tokens=write,
                output_tokens=output, reasoning_output_tokens=reasoning, total_tokens=input + output)


def add_usage(*values):
    return {key: sum(value.get(key, 0) for value in values) for key in values[0]}


def meta(fork=False):
    return [{"type": "session_meta", "timestamp": T, "payload": {"id": SESSION, **({"forked_from_id": "parent"} if fork else {})}},
            {"type": "turn_context", "timestamp": T, "payload": {"model": "gpt-fixture", "current_date": "2026-09-20"}}]


def count(second, last, total, nested=False):
    stamp = f"2026-09-20T04:00:{second}Z"
    payload = {"type": "token_count", "info": {"last_token_usage": last, "total_token_usage": total}}
    return {"type": "event_msg", "timestamp": stamp, "payload": {"msg": payload} if nested else payload}


def normalized(*values):
    total = add_usage(*values)
    return [total["total_tokens"], total["input_tokens"] - total["cached_input_tokens"], total["output_tokens"],
            total["cached_input_tokens"], total["cache_write_input_tokens"], total["reasoning_output_tokens"]]


def aggregate(rows):
    # 与事件存储一致：复制/归档文件具有相同事件身份时只计一次。
    seen, buckets = set(), {}
    for row in rows:
        if not row or not row.get("total_tokens"):
            continue
        if row["event_id"] in seen:
            continue
        seen.add(row["event_id"])
        key = (row["source"], row["model"], row["hour_start"])
        totals = buckets.setdefault(key, [0] * len(FIELDS))
        for index, field in enumerate(FIELDS):
            totals[index] += row.get(field, 0)
    return buckets


def native_rows(case):
    return [row for path in case["paths"] for row in getattr(transcripts, case["parser"])(path, case["source"]) if row]


def build_cases(root):
    cases = []

    def append(name, parser, source, data, expected, model="gpt-fixture", *, lines=True, relative=None):
        path = write(root, relative or f"{name}/rollout-2026-09-20T04-00-00-{SESSION}.jsonl", data, lines)
        case = dict(name=name, parser=parser, source=source, paths=[path],
                    expected={(source, model, HOUR): expected} if expected else {})
        cases.append(case)
        return case

    a, b, c, d = usage(100), usage(200), usage(50), usage(30)
    append("codex_multistream", "codex", "codex", meta() + [count("01", a, a), count("02", b, b), count("03", a, a), count("04", c, add_usage(b, c)), count("05", d, add_usage(a, d))], normalized(a, b, c, d))
    append("codex_inconsistent_last", "codex", "codex", meta() + [count("01", usage(15), usage(15)), count("02", usage(150), usage(22))], normalized(usage(22)))
    append("codex_total_only_sentinel", "codex", "codex", meta() + [count("01", {"total_tokens": 16000}, {"total_tokens": 0})], [])
    rerouted = append("codex_model_reroute", "codex", "codex", meta() + [count("01", a, a),
                      {"method": "model/rerouted", "params": {"fromModel": "gpt-fixture", "toModel": "gpt-routed"}},
                      count("02", c, add_usage(a, c)), meta()[1], count("03", d, add_usage(a, c, d))], normalized(a, d))
    rerouted["expected"][("codex", "gpt-routed", HOUR)] = normalized(c)
    append("everycode_nested", "codex", "every-code", meta() + [count("01", usage(10, 3, 5, 2), usage(10, 3, 5, 2), True)], normalized(usage(10, 3, 5, 2)))
    replay = [usage(100, 0, 10), usage(200, 0, 20), usage(300, 0, 30)]
    live = usage(7, 0, 3)
    append("codex_fork_burst", "codex", "codex", meta(True) + [count("01.100", replay[0], replay[0]), count("01.101", replay[1], add_usage(*replay[:2])), count("01.102", replay[2], add_usage(*replay)), count("31.102", live, add_usage(*replay, live))], normalized(replay[0], live))
    append("codex_fork_backward", "codex", "codex", meta(True) + [count("01.100", a, a), count("00.900", c, add_usage(a, c)), count("01.000", d, add_usage(a, c, d))], normalized(a, c, d))
    crossday = meta(True)
    crossday[1]["payload"]["current_date"] = "2026-09-19"
    append("codex_fork_previous_day", "codex", "codex", crossday + [count("01", a, a)], [])
    first, compact, final = usage(1000, 400, 50, 10), usage(240000, 200000, 3000), usage(2000, 1500, 70, 5, 30)
    for advances in (False, True):
        def record(second, value):
            return {"type": "token_usage_record", "timestamp": f"2026-09-20T04:00:{second}Z", "payload": {"response_id": "response-" + second, "usage": value}}
        after = add_usage(first, compact) if advances else first
        rows = meta() + [record("10", first), count("11", first, first), record("40", compact),
                        {"type": "compacted", "timestamp": "2026-09-20T04:00:40.050Z", "payload": {}},
                        {"type": "world_state", "timestamp": "2026-09-20T04:00:40.060Z", "payload": {}},
                        meta()[1], count("40.080", compact if advances else usage(24000, 0, 453), after),
                        record("50", final), count("51", final, add_usage(after, final))]
        append("codex_compaction_" + str(advances).lower(), "codex", "codex", rows, normalized(first, compact, final))
    copied = append("codex_copied_archive", "codex", "codex", meta() + [count("01", a, a)], normalized(a))
    copied["paths"].append(write(root, f"codex_copied_archive/archive/rollout-2026-09-20T04-00-00-{SESSION}.jsonl", meta() + [count("01", a, a)]))
    # 压缩快照通过原生标准库或已声明的 zstandard 依赖产生，Node 仅用于开发差分。
    try:
        from compression import zstd
        compress = zstd.compress
    except ImportError:
        try:
            import zstandard
            compress = zstandard.ZstdCompressor().compress
        except ImportError:
            compress = None
    if compress:
        for suffix in (".zst", ".zstd"):
            compressed = append("codex_archive" + suffix, "codex", "codex", meta() + [count("01", a, a)], normalized(a))
            archive = compressed["paths"][0].with_suffix(".jsonl" + suffix)
            archive.write_bytes(compress(compressed["paths"][0].read_bytes()))
            compressed["paths"] = [archive]

    raw = dict(input_tokens=420, cache_read_input_tokens=100, cache_creation_input_tokens=20, output_tokens=518,
               output_tokens_details={"thinking_tokens": 163})
    claude = {"type": "assistant", "timestamp": T, "requestId": "request-1", "message": {"id": "message-1", "model": "claude-fixture", "usage": raw}}
    append("claude_reasoning_and_dedup", "claude", "claude", [claude, deepcopy(claude)], [1058, 420, 355, 100, 20, 163], "claude-fixture")
    zero = deepcopy(claude)
    zero["message"]["usage"]["output_tokens_details"] = {"thinking_tokens": 0, "reasoning_tokens": 163}
    append("claude_thinking_zero_precedence", "claude", "claude", [zero], [1058, 420, 518, 100, 20, 0], "claude-fixture")
    mirror = deepcopy(claude)
    mirror.update(entrypoint="sdk-ts", userType="external")
    append("claude_omo_mirror", "claude", "claude", [mirror], [], "claude-fixture")
    upper = deepcopy(claude)
    upper["message"]["usage"]["output_tokens_details"] = {"reasoning_tokens": 9999}
    append("claude_reasoning_clamped", "claude", "claude", [upper], [1058, 420, 0, 100, 20, 518], "claude-fixture")
    copies = append("claude_copied_session", "claude", "claude", [claude], [1058, 420, 355, 100, 20, 163], "claude-fixture")
    copies["paths"].append(write(root, "claude_copied_session/archive/copy.jsonl", [claude]))

    for source in ("omp", "omo", "pi", "prime-agent", "minimax-code"):
        def pi_message(key, usage, **extra):
            message = {"role": "assistant", "model": "pi-fixture", "timestamp": MS, "usage": usage, **extra}
            return {"message_id": key, "message": message} if source == "minimax-code" else {"type": "message", "id": key, "timestamp": T, "message": message}
        raw = dict(input=10, output=4, cacheRead=3, cacheWrite=2, reasoningTokens=1, reasoning=999)
        total = 19 if source == "omo" else 20
        append(source + "_usage_and_dedup", "pi_style", source, [pi_message("p1", raw), pi_message("p1", raw)], [total, 10, 4, 3, 2, 1], "pi-fixture")
        append(source + "_reasoning_spelling", "pi_style", source, [pi_message("p2", dict(input=10, output=4, reasoning=3))], [14, 10, 4, 0, 0, 3 if source == "omo" else 0], "pi-fixture")
        append(source + "_missing_model", "pi_style", source, [pi_message("p3", raw, model=None)], [total, 10, 4, 3, 2, 1], source + "-unknown")
        if source in {"omp", "omo"}:
            old = pi_message("p4", raw, timestamp=1000)
            old["timestamp"] = "1970-01-01T00:00:01Z"
            append(source + "_invalid_timestamp", "pi_style", source, [old], [])
            fallback = pi_message("p6", raw, timestamp=1000)
            append(source + "_timestamp_fallback", "pi_style", source, [fallback], [total, 10, 4, 3, 2, 1], "pi-fixture")
            seconds = pi_message("p7", raw, timestamp=MS // 1000)
            seconds["timestamp"] = None
            append(source + "_seconds_are_not_milliseconds", "pi_style", source, [seconds], [])
            numeric_string = pi_message("p8", raw, timestamp=str(MS))
            numeric_string["timestamp"] = None
            append(source + "_numeric_string_timestamp", "pi_style", source, [numeric_string], [total, 10, 4, 3, 2, 1], "pi-fixture")
        if source in {"pi", "prime-agent"}:
            provider = append(source + "_provider", "pi_style", source, [pi_message("p5", raw, provider=" GitHub Copilot ")], [total, 10, 4, 3, 2, 1], "pi-fixture")
            provider["expected"] = {(source + "-github-copilot", "pi-fixture", HOUR): [total, 10, 4, 3, 2, 1]}

    append("gemini_cumulative_and_cache", "gemini", "gemini", {"sessionId": "gemini-session", "messages": [
        {"id": "g1", "timestamp": T, "model": "gemini-fixture", "tokens": {"input": 10, "output": 5, "cached": 20, "thoughts": 3, "tool": 2, "total": 17}},
        {"id": "g2", "timestamp": T, "tokens": {"input": 15, "output": 7, "cached": 22, "thoughts": 4, "tool": 2, "total": 24}}]}, [50, 15, 9, 22, 0, 4], "gemini-fixture", lines=False)
    workbuddy = {"id": "wb1", "timestamp": MS, "sessionId": "wb-session", "providerData": {"messageId": "response-1", "model": "wb-fixture", "rawUsage": {"prompt_tokens": 100, "completion_tokens": 40, "cache_read_input_tokens": 20, "prompt_tokens_details": {"cached_tokens": 30}, "cache_creation_input_tokens": 10, "completion_tokens_details": {"reasoning_tokens": 5}}}}
    duplicate = deepcopy(workbuddy)
    duplicate["id"] = "wb2"
    append("workbuddy_cache_and_response_dedup", "workbuddy", "workbuddy", [workbuddy, duplicate], [140, 60, 35, 30, 10, 5], "wb-fixture")
    unknown = deepcopy(workbuddy)
    unknown["providerData"].pop("model")
    append("workbuddy_default_model", "workbuddy", "workbuddy", [unknown], [140, 60, 35, 30, 10, 5], "auto")
    for version in (1, 2):
        message = {"id": "open-message", "sessionID": "open-session", "role": "assistant", "time": {"created": MS - 3600000, "completed": MS}, "tokens": {"input": 10, "output": 4, "reasoning": 1, "cache": {"read": 3, "write": 2}}}
        message.update({"modelID": "opencode-fixture", "providerID": "provider"} if version == 1 else {"model": {"id": "opencode-fixture", "providerID": "provider"}})
        append("opencode_model_v" + str(version), "opencode", "opencode", message, [20, 10, 4, 3, 2, 1], "opencode-fixture", lines=False)
    return cases


class TranscriptTests(unittest.TestCase):
    def test_fixed_upstream_shapes(self):
        with tempfile.TemporaryDirectory() as directory:
            for case in build_cases(Path(directory)):
                with self.subTest(case=case["name"]):
                    self.assertEqual(aggregate(native_rows(case)), case["expected"])

    def test_codex_delta_interleaved_baselines(self):
        state = transcripts.CodexDelta()
        values = [state.consume(usage(last), usage(total)) for last, total in ((100, 100), (200, 200), (100, 100), (50, 250), (30, 130))]
        self.assertEqual([None if value is None else value["total_tokens"] for value in values], [100, 200, None, 50, 30])

    def test_codex_delta_limits_retained_baselines(self):
        state = transcripts.CodexDelta()
        for index in range(100):
            state.consume(usage(index + 1), usage(index + 1))
        self.assertLessEqual(len(state.heads), 32)

    def test_codex_record_only_warns_after_completed_call(self):
        record = {"type": "token_usage_record", "timestamp": T, "payload": {"response_id": "record-only", "usage": usage(10)}}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            pending = write(root, "pending.jsonl", meta() + [record])
            self.assertEqual(list(transcripts.codex(pending)), [])
            complete = write(root, "complete.jsonl", meta() + [record, {"type": "event_msg", "timestamp": T, "payload": {"type": "task_complete"}}])
            with self.assertRaisesRegex(RuntimeError, "unsupported_usage_record"):
                list(transcripts.codex(complete))


if __name__ == "__main__":
    unittest.main()
