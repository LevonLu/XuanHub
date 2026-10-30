# extended.py — SQLite、遥测和 IDE 专有用量格式的只读适配
from __future__ import annotations

from collections import Counter
from pathlib import Path
import os
import re

from .common import (FIELDS, array, database, event, first, identity, integer, json_lines,
                     json_value, obj, read_json, table_names, timestamp, totals)
from .databases import rows
from .extended_kiro import kiro_cli, kiro_cli_db, kiro_ide
from .extended_antigravity import antigravity
from .extended_trae import trae, normalize_trae_usage


def zcode(path, source="zcode"):
    native_start = float("inf")
    with database(path) as db:
        if "model_usage" in table_names(db):
            for row in rows(db, "model_usage"):
                provider = str(row.get("provider_id") or "").lower()
                if row.get("status") != "completed" or not provider or any(x in provider for x in ("anthropic", "openai", "google")):
                    continue
                if not row.get("model_id") or not row.get("session_id"):
                    continue
                key = row.get("id") or (str(row.get("logical_request_id")) + "#" + str(integer(row.get("attempt_index"))))
                read, write, reasoning = [integer(row.get(k)) for k in ("cache_read_input_tokens", "cache_creation_input_tokens", "reasoning_tokens")]
                result = event(source, row.get("model_id"), row.get("started_at"),
                               totals(max(0, integer(row.get("input_tokens")) - read - write),
                                      max(0, integer(row.get("output_tokens")) - reasoning), read, write, reasoning), key)
                if result:
                    native_start = min(native_start, timestamp(row.get("started_at")))
                    yield result
        # 新表接管后的重复 message 不重复计数，迁移前历史仍保留。
        for table in ("message", "session_message"):
            for row in rows(db, table):
                data = obj(json_value(row.get("data"), {}))
                provider = str(data.get("providerID") or obj(data.get("model")).get("providerID") or "").lower()
                role = row.get("type") if table == "session_message" else data.get("role")
                if role != "assistant" or not provider or any(x in provider for x in ("anthropic", "openai", "google")):
                    continue
                updated = timestamp(row.get("time_updated"))
                if native_start < float("inf") and (updated <= 0 or updated >= native_start):
                    continue
                raw, cache = obj(data.get("tokens")), obj(obj(data.get("tokens")).get("cache"))
                read, write, reasoning = integer(cache.get("read")), integer(cache.get("write")), integer(raw.get("reasoning"))
                yield event(source, data.get("modelID") or obj(data.get("model")).get("modelID"), obj(data.get("time")).get("created") or row.get("time_created"),
                            totals(max(0, integer(raw.get("input")) - read - write), max(0, integer(raw.get("output")) - reasoning), read, write, reasoning),
                            data.get("id") or row.get("id"))


def _copilot_model(value):
    return re.sub(r"^(claude-(?:sonnet|opus|haiku)-\d+)\.(\d+)", r"\1-\2", str(value or "github-copilot").strip().lower())


def _otel_time(value):
    try:
        return round(float(value[0]) * 1000 + float(value[1] or 0) / 1_000_000) if isinstance(value, list) and len(value) >= 2 else 0
    except (ValueError, TypeError, OverflowError):
        return 0


def _copilot_match(session, model, when, usage):
    # 精确时间保留供汇总器在 ±2 秒内逐项消耗匹配，不把两次相同请求合并。
    return {"session": str(session or ""), "model": model, "time": timestamp(when),
            "tokens": [usage[k] for k in FIELDS]}


def copilot_otel(path, source="copilot"):
    for index, record in json_lines(path):
        attrs = obj(record.get("attributes"))
        if record.get("scopeMetrics") or not (attrs.get("gen_ai.operation.name") == "chat" or record.get("type") == "span" and str(record.get("name", "")).startswith("chat ")):
            continue
        cli = record.get("type") == "span"
        prompt, output = integer(attrs.get("gen_ai.usage.input_tokens")), integer(attrs.get("gen_ai.usage.output_tokens"))
        read = min(prompt, integer(first(attrs, "gen_ai.usage.cache_read.input_tokens", "gen_ai.usage.cache_read_input_tokens", "gen_ai.usage.cached_input_tokens")))
        write = integer(first(attrs, "gen_ai.usage.cache_write.input_tokens", "gen_ai.usage.cache_creation.input_tokens", "gen_ai.usage.cache_write_input_tokens", "gen_ai.usage.cache_creation_input_tokens"))
        if cli:
            write = min(write, max(0, prompt - read))
        reasoning = min(output, integer(first(attrs, "gen_ai.usage.reasoning.output_tokens", "gen_ai.usage.reasoning_tokens", "gen_ai.usage.reasoning_output_tokens")))
        usage = totals(max(0, prompt - read - (write if cli else 0)), output - reasoning, read, write, reasoning)
        when = next((_otel_time(record.get(k)) for k in ("endTime", "startTime", "hrTime", "hrTimeObserved") if _otel_time(record.get(k))), 0)
        model = _copilot_model(first(attrs, "gen_ai.response.model", "gen_ai.request.model"))
        response = attrs.get("gen_ai.response.id")
        key = (record.get("traceId"), record.get("spanId")) if cli and record.get("traceId") and record.get("spanId") else ("response", response) if response else (str(path), index)
        yield event(source, model, when, usage, key, copilot_lane="otel-cli" if cli else "otel-chat",
                    copilot_match=_copilot_match(attrs.get("gen_ai.conversation.id"), model, when, usage))


def copilot_store(path, source="copilot"):
    with database(path) as db:
        for row in rows(db, "assistant_usage_events"):
            prompt, output = integer(row.get("input_tokens")), integer(row.get("output_tokens"))
            details = Counter()
            for item in array(json_value(row.get("token_details_json"), [])):
                if isinstance(item, dict) and item.get("tokenType") in {"input", "cache_read", "cache_write", "output"}:
                    details[item["tokenType"]] += integer(item.get("tokenCount"))
            if details and sum(details[k] for k in ("input", "cache_read", "cache_write")) == prompt and details["output"] == output:
                read, write = details["cache_read"], details["cache_write"]
            else:
                read = min(prompt, integer(row.get("cache_read_tokens")))
                write = min(max(0, prompt - read), integer(row.get("cache_write_tokens")))
            reasoning = min(output, integer(row.get("reasoning_tokens")))
            usage = totals(prompt - read - write, output - reasoning, read, write, reasoning)
            model = _copilot_model(row.get("model"))
            yield event(source, model, row.get("created_at"), usage, ("store", row.get("session_id"), row.get("id")),
                        copilot_lane="store", copilot_match=_copilot_match(row.get("session_id"), model, row.get("created_at"), usage))


def copilot_app(path, source="copilot"):
    with database(path) as db:
        for row in rows(db, "sessions"):
            markers = [str(row.get(k) or "").lower().strip() for k in ("session_type", "provider_id")]
            if any(v in {"cli", "copilot-cli"} or any(x in v for x in ("vscode", "extension", "otel")) for v in markers):
                continue
            prompt, output = integer(row.get("total_input_tokens")), integer(row.get("total_output_tokens"))
            read, reasoning = min(prompt, integer(row.get("total_cached_tokens"))), min(output, integer(row.get("total_reasoning_tokens")))
            yield event(source, _copilot_model(row.get("model")), row.get("updated_at") or row.get("created_at"),
                        totals(prompt - read, output - reasoning, read, 0, reasoning), ("app", row.get("id")), cumulative=True)


def openclaw_record(record, source="openclaw", counts=None):
    msg = obj(record.get("message"))
    raw = obj(msg.get("usage"))
    if record.get("type") != "message" or msg.get("role") != "assistant" or not raw:
        return None
    if any(not isinstance(v, (int, float)) or isinstance(v, bool) or not 0 <= v <= 2**53 - 1 or v % 1 != 0 for k, v in raw.items() if k in {"input", "cacheRead", "cacheWrite", "output", "totalTokens"}):
        return None
    usage = totals(max(0, integer(raw.get("input")) - integer(raw.get("cacheRead"))), raw.get("output"), raw.get("cacheRead"), raw.get("cacheWrite"))
    key = record.get("id")
    if not isinstance(key, str) or not key.strip():
        key = identity(record.get("timestamp"), msg.get("timestamp"), msg.get("responseId"), msg.get("model"), msg.get("provider"), msg.get("api"),
                       [integer(raw.get(k)) for k in ("input", "cacheRead", "cacheWrite", "output", "totalTokens")])
        if counts is not None:
            counts[key] += 1
            key = (key, counts[key])
    return event(source, msg.get("model"), record.get("timestamp"), usage, key)


def openclaw_db(path, source="openclaw"):
    with database(path) as db:
        if not {"transcript_events", "session_windows"} <= table_names(db):
            return
        query = "SELECT e.session_id,e.event_json FROM transcript_events e JOIN session_windows w ON w.session_id=e.session_id WHERE COALESCE(w.reason,'')!='compaction' ORDER BY e.session_id,e.seq"
        counts, session = Counter(), None
        for row in db.execute(query):
            if row[0] != session:
                counts, session = Counter(), row[0]
            yield openclaw_record(obj(json_value(row[1], {})), source, counts)


def codebuddy(path, source="codebuddy"):
    path = Path(path)
    candidates = [parent / "settings.json" for parent in path.parents if parent.name.lower() in {".codebuddy", "codebuddy"}]
    candidates.append(Path(os.environ.get("CODEBUDDY_HOME") or Path.home() / ".codebuddy") / "settings.json")
    fallback = next((str(record["model"]).strip().lower() for candidate in candidates
                     if isinstance((record := obj(read_json(candidate, {}))).get("model"), str) and record["model"].strip()), "codebuddy-unknown")
    if path.suffix != ".log":
        for index, record in json_lines(path):
            provider = obj(record.get("providerData"))
            raw = obj(provider.get("rawUsage"))
            if not raw:
                continue
            prompt, output = integer(raw.get("prompt_tokens")), integer(raw.get("completion_tokens"))
            read = max(integer(obj(raw.get("prompt_tokens_details")).get("cached_tokens")), integer(raw.get("prompt_cache_hit_tokens")), integer(raw.get("cache_read_input_tokens")))
            write = max(integer(raw.get("cache_creation_input_tokens")), integer(raw.get("prompt_cache_write_tokens")))
            reasoning = min(output, integer(obj(raw.get("completion_tokens_details")).get("reasoning_tokens")))
            usage = totals(max(0, prompt - read - write), output - reasoning, read, write, reasoning)
            model = str(provider.get("model") or record.get("model") or fallback).lower()
            when = timestamp(record.get("timestamp"))
            key = provider.get("messageId") or record.get("uuid") or record.get("id") or (record.get("sessionId") or path.stem, when)
            yield event(source, model, when, usage, key, codebuddy_lane="jsonl",
                        codebuddy_match=identity(when // 1000, model, usage))
        return
    models = {}
    with path.open(encoding="utf-8-sig", errors="replace") as stream:
        for index, line in enumerate(stream):
            match = re.search(r"\[(CraftInvokableAgent|AgentReporter)\]\s*\[([^]]+)\]", line)
            if not match:
                continue
            agent = match[2]
            if "Model prepared:" in line:
                model = line.split("Model prepared:", 1)[1].strip()
                tail = re.search(r"\(([^()]*)\)[^()]*$", model)
                models[agent] = (tail[1] if tail else model).lower()
                continue
            marker = "Agent execution successful with usage:"
            if marker not in line:
                continue
            raw = obj(json_value(line.split(marker, 1)[1].rsplit("}", 1)[0] + "}", {}))
            prompt = integer(first(raw, "cachedMissTokens", "cacheMissTokens", "input_tokens", "inputTokens", "prompt_tokens"))
            read = max(integer(raw.get(k)) for k in ("cache_read_input_tokens", "cacheReadInputTokens", "cacheTokens", "prompt_cache_hit_tokens", "cached_tokens"))
            write = max(integer(raw.get(k)) for k in ("cache_creation_input_tokens", "cacheCreationInputTokens", "cachedWriteTokens", "prompt_cache_write_tokens"))
            if not any(k in raw for k in ("cachedMissTokens", "cacheMissTokens", "prompt_cache_miss_tokens")):
                prompt = max(0, prompt - read)
            usage = totals(prompt, first(raw, "output_tokens", "outputTokens", "completion_tokens"), read, write,
                           first(raw, "completion_thinking_tokens", "completionThinkingTokens", "reasoningTokens"))
            raw_time = line[1:line.find("]")] if line.startswith("[") else line.split(" [", 1)[0]
            when = timestamp(raw_time.replace("/", "-")) or int(path.stat().st_mtime * 1000)
            model = models.get(agent, fallback)
            fingerprint = identity(when // 1000, model, usage)
            yield event(source, model, when, usage, (agent, fingerprint), codebuddy_lane="log", codebuddy_match=fingerprint)
