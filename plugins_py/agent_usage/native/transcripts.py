# transcripts.py — Codex、Claude、Gemini、OpenCode 与 Pi 家族会话解析
from __future__ import annotations

import math
import re

from .common import (COMPONENTS, FIELDS, anthropic, array, event, first, identity, integer,
                     json_lines, obj, openai, read_json, timestamp, totals)


def canonical(value):
    if not isinstance(value, dict):
        return None
    valid = False
    for key in (*FIELDS, "cache_write_input_tokens"):
        if key not in value:
            continue
        try:
            n = float(value[key]) if value[key] is not None else 0
            valid |= math.isfinite(n) and n >= 0
        except (ValueError, TypeError, OverflowError):
            pass
    return {key: integer(value.get(key, value.get("cache_write_input_tokens") if key == "cache_creation_input_tokens" else 0)) for key in FIELDS} if valid else None


class CodexDelta:
    def __init__(self):
        self.heads = []
        self.current = None

    def touch(self, value, previous=None):
        if previous in self.heads:
            self.heads.remove(previous)
        if value in self.heads:
            self.heads.remove(value)
        self.heads.append(value)
        self.heads = self.heads[-32:]
        self.current = value

    def consume(self, last, total):
        last, total = canonical(last), canonical(total)
        if total is None:
            return last
        if total in self.heads:
            self.touch(total)
            return None
        if last and all(total[k] >= last[k] for k in FIELDS):
            expected = {k: total[k] - last[k] for k in FIELDS}
            self.touch(total, expected)
            return last
        if self.current and total["total_tokens"] >= self.current["total_tokens"]:
            delta = {k: max(0, total[k] - self.current[k]) for k in FIELDS}
            if last is None or delta["total_tokens"] <= last["total_tokens"]:
                self.touch(total, self.current)
                return delta
        self.touch(total)
        return last or total


def codex_usage(raw, tier):
    usage = canonical(raw)
    if not usage:
        return None
    usage["input_tokens"] = max(0, usage["input_tokens"] - usage["cached_input_tokens"])
    prefix = "priority_" if tier in {"priority", "fast"} else "ultrafast_" if tier == "ultrafast" else ""
    if prefix:
        usage.update({prefix + key: value for key, value in list(usage.items()) if key != "total_tokens"})
    return usage


def codex(path, source="codex"):
    state = CodexDelta()
    model = selected_model = ""
    match = re.search(r"([0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})", path.name, re.I)
    session, tier, current_date = match[1] if match else path.stem, "", ""
    session_seen, unmatched, record_only = False, False, False
    file_date = re.search(r"rollout-(\d{4}-\d{2}-\d{2})", path.name)
    fork, prefix, previous_time = False, True, None
    saw_count, compaction = False, None
    seen = set()
    for index, record in json_lines(path):
        kind, payload = record.get("type"), obj(record.get("payload"))
        if compaction and not compaction["compacted"]:
            if kind == "compacted":
                compaction["compacted"] = True
                continue
            compaction = None
        if kind in {"turn_context", "session_meta"}:
            if kind == "session_meta":
                if not session_seen:
                    session = (match[1] if match else payload.get("id")) or session
                    session_seen = True
                fork |= bool(payload.get("forked_from_id"))
                if not selected_model:
                    selected_model = payload.get("model") or ""
                    model = selected_model
            else:
                selected_model = payload.get("model") or selected_model
                model = selected_model or model
                current_date = payload.get("current_date") or current_date
            continue
        for envelope in (record, payload, obj(payload.get("msg")), obj(record.get("msg"))):
            name = str(envelope.get("method") or envelope.get("type") or "").replace("_", "/")
            if name == "model/rerouted":
                data = obj(envelope.get("params") or envelope.get("payload") or envelope)
                model = first(data, "toModel", "to_model") or model
        if kind == "event_msg" and payload.get("type") == "thread_settings_applied":
            tier = str(obj(payload.get("thread_settings")).get("service_tier") or tier).strip().lower()
        if kind == "token_usage_record" and source == "codex":
            if not saw_count:
                record_only |= unmatched
                unmatched = True
            if saw_count and payload.get("response_id") and isinstance(payload.get("usage"), dict):
                compaction = {"compacted": False, "id": payload["response_id"], "usage": payload["usage"],
                              "time": record.get("timestamp"), "model": model, "tier": tier}
            continue
        if kind == "event_msg" and payload.get("type") in {"task_complete", "turn_aborted"} and not saw_count:
            record_only |= unmatched
            unmatched = False
        token = payload if payload.get("type") == "token_count" else obj(payload.get("msg"))
        if token.get("type") != "token_count" or not isinstance(token.get("info"), dict):
            continue
        info = token["info"]
        total, last = info.get("total_token_usage"), info.get("last_token_usage")
        if compaction and compaction["compacted"] and state.current and canonical(total) == state.current:
            yield event(source, compaction["model"], compaction["time"], codex_usage(compaction["usage"], compaction["tier"]),
                        ("compaction", compaction["id"]))
        compaction, saw_count = None, True
        delta = state.consume(last, total)
        # 压缩后的纯 total 占位事件不是实际模型调用。
        if source == "codex" and integer(obj(last).get("total_tokens")) > 0 and integer(obj(total).get("total_tokens")) == 0:
            if not any(integer(obj(u).get(k)) for u in (last, total) for k in COMPONENTS):
                continue
        if not delta:
            continue
        stamp = timestamp(record.get("timestamp"))
        skip = False
        if fork and source in {"codex", "acode"} and prefix:
            if not stamp or (previous_time is not None and (stamp < previous_time or stamp - previous_time >= 500)):
                prefix = False
            skip = prefix and previous_time is not None and stamp - previous_time < 500
            previous_time = stamp
        if skip or (fork and file_date and current_date and current_date < file_date[1]):
            continue
        key = (session, record.get("timestamp"))
        if key in seen:
            continue
        seen.add(key)
        yield event(source, model, stamp, codex_usage(delta, tier), key)
    if source == "codex" and record_only and not saw_count:
        raise RuntimeError("unsupported_usage_record")


def claude(path, source="claude"):
    seen = set()
    for index, record in json_lines(path):
        if source == "claude" and record.get("entrypoint") == "sdk-ts" and record.get("userType") == "external":
            continue
        message = obj(record.get("message"))
        usage = message.get("usage") or record.get("usage")
        if not isinstance(usage, dict):
            continue
        key = (message["id"], record.get("requestId")) if message.get("id") else record.get("uuid") or identity(record.get("sessionId", path.stem), index, record.get("timestamp"), usage)
        if key in seen:
            continue
        result = event(source, message.get("model") or record.get("model"), record.get("timestamp"), anthropic(usage), key)
        if result:
            seen.add(key)
            yield result


def gemini(path, source="gemini"):
    session = obj(read_json(path, {}))
    previous, model = None, ""
    for index, message in enumerate(array(session.get("messages"))):
        model = message.get("model") or model
        raw = message.get("tokens")
        if not isinstance(raw, dict):
            continue
        usage = totals(raw.get("input"), integer(raw.get("output")) + integer(raw.get("tool")), raw.get("cached"), 0, raw.get("thoughts"))
        usage["total_tokens"] = max(usage["total_tokens"], integer(raw.get("total")))
        delta = {k: max(0, usage[k] - previous[k]) for k in FIELDS} if previous and usage["total_tokens"] >= previous["total_tokens"] else usage
        previous = usage
        yield event(source, model, message.get("timestamp"), delta, (session.get("sessionId", path.stem), message.get("id", index)))


def opencode_message(message, source="opencode", fallback=None):
    raw = message.get("tokens")
    if not isinstance(raw, dict) or message.get("role") == "user":
        return None
    cache = obj(raw.get("cache"))
    usage = totals(raw.get("input"), raw.get("output"), cache.get("read"), cache.get("write"), raw.get("reasoning"))
    nested = obj(message.get("model"))
    model = message.get("modelID") or (message.get("model") if isinstance(message.get("model"), str) else None) or message.get("modelId") or nested.get("id")
    provider = first(message, "providerID", "provider", "providerId") or nested.get("providerID")
    when = first(obj(message.get("time")), "completed", "created") or message.get("created_at")
    session = message.get("sessionID") or message.get("sessionId")
    key = (session, message["id"]) if message.get("id") else fallback
    fingerprint = identity(source, obj(message.get("time")), model, provider, usage) if when else None
    return event(source, model, when, usage, key, allow_zero=True, fork_fingerprint=fingerprint, fork_session=session)


def opencode(path, source="opencode"):
    yield opencode_message(obj(read_json(path, {})), source, str(path))


def workbuddy(path, source="workbuddy"):
    seen = set()
    root = next((p.parent for p in path.parents if p.name == "projects"), None)
    fallback_model = obj(read_json(root / "settings.json", {})).get("model") if root else None
    for index, record in json_lines(path):
        provider = obj(record.get("providerData"))
        raw = provider.get("rawUsage")
        if not isinstance(raw, dict):
            continue
        session = record.get("sessionId") or path.stem
        key = provider.get("messageId") or record.get("uuid") or record.get("id") or (session, record.get("timestamp"))
        key_hash = identity(key)
        if key_hash in seen:
            continue
        result = event(source, provider.get("model") or provider.get("requestModelId") or record.get("model") or fallback_model or "auto",
                       record.get("timestamp"), openai(raw), key, session_id=session)
        if result:
            seen.add(key_hash)
            yield result


def pi_style(path, source="pi"):
    seen = set()
    for index, entry in json_lines(path):
        if source == "minimax-code":
            record = obj(entry.get("message") or entry)
        else:
            if entry.get("type") != "message":
                continue
            record = obj(entry.get("message"))
        if record.get("role") != "assistant" or not isinstance(record.get("usage"), dict):
            continue
        raw = record["usage"]
        key = entry.get("message_id") if source == "minimax-code" else entry.get("id")
        if not key or key in seen:
            continue
        seen.add(key)
        usage = totals(raw.get("input"), raw.get("output"), raw.get("cacheRead"), raw.get("cacheWrite"),
                       first(raw, "reasoningTokens", "reasoning") if source == "omo" else raw.get("reasoningTokens"),
                       reasoning_in_output=source == "omo")
        if integer(raw.get("totalTokens")):
            usage["total_tokens"] = integer(raw["totalTokens"])
        provider = re.sub(r"[^a-z0-9]+", "-", str(record.get("provider") or "").strip().lower()).strip("-")[:64]
        attributed = source + "-" + provider if source in {"pi", "prime-agent"} and provider else source
        when = record.get("timestamp") or entry.get("timestamp")
        if source in {"omp", "omo"}:
            raw_time = record.get("timestamp")
            try:
                raw_time = float(raw_time)
            except (ValueError, TypeError, OverflowError):
                raw_time = 0
            when = raw_time if 1_577_836_800_000 <= raw_time < 253_402_300_800_000 else entry.get("timestamp")
            if timestamp(when) < 1_577_836_800_000:
                continue
        yield event(attributed, record.get("model") or source + "-unknown", when, usage, key)


def openclaw(path, source="openclaw"):
    from collections import Counter
    from .extended import openclaw_record
    occurrences = Counter()
    for index, entry in json_lines(path):
        yield openclaw_record(entry, source, occurrences)
