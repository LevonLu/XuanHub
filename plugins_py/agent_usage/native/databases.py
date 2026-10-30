# databases.py — 只读投影各 Agent 的 SQLite 用量，兼容现存表结构
from __future__ import annotations

import math
from pathlib import Path
import re

from .common import (FIELDS, array, columns, database, event, first, integer,
                     json_value, obj, read_json, table_names, timestamp, totals)
from .formats import local_usage
from .transcripts import opencode_message


def rows(connection, table):
    if table not in table_names(connection):
        return []
    return (dict(row) for row in connection.execute(f'SELECT * FROM "{table}"'))


def opencode(path, source="opencode"):
    with database(path) as db:
        messages = {}
        # 迁移中的数据库可能同时保留旧 message 与新 session_message，不能二选一。
        for table in ("message", "session_message"):
            for row in rows(db, table):
                data = obj(json_value(row.get("data"), {}))
                if (row.get("type") if table == "session_message" else data.get("role")) != "assistant":
                    continue
                provider = str(data.get("providerID") or obj(data.get("model")).get("providerID") or "").lower()
                if source == "dumate" and provider != "qianfanpersonalquota":
                    continue
                if source == "mimo" and provider not in {"mimo", "xiaomi"}:
                    continue
                if not isinstance(data.get("tokens"), dict):
                    continue
                data.setdefault("role", "assistant")
                data.setdefault("id", row.get("id"))
                data.setdefault("sessionID", row.get("session_id"))
                key = (data.get("sessionID"), data.get("id"))
                if not key[1]:
                    continue
                updated = integer(row.get("time_updated"))
                previous = messages.get(key)
                if previous and previous[0] > updated:
                    continue
                if source == "dumate":
                    raw = data["tokens"]
                    data["tokens"] = {**raw, "output": max(0, integer(raw.get("output")) - integer(raw.get("reasoning")))}
                messages[key] = updated, data
        for key, (updated, data) in messages.items():
            result = opencode_message(data, source, (data.get("sessionID"), data.get("id")))
            if result:
                result["source_updated"] = updated
            yield result


def _utc_time(value):
    if isinstance(value, str):
        value = value.strip()
        if re.fullmatch(r"\d{4}-\d{2}-\d{2}(?:[T ]\d{2}:\d{2}:\d{2}(?:\.\d{1,3})?)?", value):
            value = value.replace(" ", "T") + ("T00:00:00" if len(value) == 10 else "") + "Z"
    return timestamp(value)


def hermes(path, source="hermes"):
    with database(path) as db:
        for row in rows(db, "sessions"):
            usage = totals(row.get("input_tokens"), row.get("output_tokens"), row.get("cache_read_tokens"),
                           row.get("cache_write_tokens"), row.get("reasoning_tokens"))
            yield event(source, row.get("model") or "hermes-agent", row.get("ended_at") or row.get("started_at"), usage, row.get("id"), cumulative=True)


def goose(path, source="goose"):
    with database(path) as db:
        for row in rows(db, "sessions"):
            model = obj(json_value(row.get("model_config_json"), {}))
            name = model.get("model_name")
            if not isinstance(name, str) or not name.strip() or not isinstance(row.get("id"), str):
                continue
            prompt = integer(first(row, "accumulated_input_tokens", "input_tokens"))
            output = integer(first(row, "accumulated_output_tokens", "output_tokens"))
            reported = integer(first(row, "accumulated_total_tokens", "total_tokens"))
            usage = totals(prompt, output, reasoning=max(0, reported - prompt - output))
            yield event(source, name.strip(), _utc_time(row.get("created_at")) or Path(path).stat().st_mtime * 1000,
                        usage, row["id"], cumulative=True, counter_reset=True)


def kiro(path, source="kiro"):
    from .extended_kiro import kiro_ide
    yield from kiro_ide(path, source)


def opensquilla(path, source="opensquilla"):
    with database(path) as db:
        if not {"usage_events", "usage_event_items"} <= table_names(db):
            return
        query = """SELECT e.event_id,e.completed_at_ms,i.ordinal,COALESCE(i.model,e.model) AS model,
            i.input_tokens,i.output_tokens,i.reasoning_tokens,i.cache_read_tokens,i.cache_write_tokens,i.total_tokens
            FROM usage_events e JOIN usage_event_items i ON i.event_id=e.event_id
            WHERE e.status='finalized' AND e.completed_at_ms IS NOT NULL"""
        for raw in db.execute(query):
            row = dict(raw)
            if not isinstance(row.get("event_id"), str) or not row["event_id"].strip() or row.get("ordinal") is None:
                continue
            usage = local_usage({**row, "cached_tokens": row.get("cache_read_tokens"), "cache_creation_input_tokens": row.get("cache_write_tokens")})
            yield event(source, row.get("model"), row.get("completed_at_ms"), usage, (row.get("event_id"), row.get("ordinal")))


def anythingllm(path, source="anythingllm"):
    with database(path) as db:
        for row in rows(db, "workspace_chats"):
            raw = obj(obj(json_value(row.get("response"), {})).get("metrics"))
            prompt, output = integer(raw.get("prompt_tokens")), integer(raw.get("completion_tokens"))
            if prompt == output == 0:
                continue
            usage = totals(prompt, output, reasoning=max(0, integer(raw.get("total_tokens")) - prompt - output))
            yield event(source, raw.get("model") or "anythingllm-unknown", _utc_time(row.get("createdAt")) or _utc_time(row.get("lastUpdatedAt")), usage, (str(path), row.get("id")))


def devin(path, source="devin"):
    with database(path) as db:
        sessions = {row.get("id"): row for row in rows(db, "sessions")}
        candidates = {}
        for row in rows(db, "message_nodes"):
            message = obj(json_value(row.get("chat_message"), {}))
            meta = obj(message.get("metadata"))
            raw = obj(meta.get("metrics"))
            request = meta.get("request_id")
            if message.get("role") != "assistant" or not isinstance(request, str) or not request.strip() or raw.get("input_tokens") is None or raw.get("output_tokens") is None:
                continue
            counts = []
            for key in ("input_tokens", "output_tokens", "cache_read_tokens", "cache_creation_tokens"):
                try:
                    value = float(raw[key]) if raw.get(key) is not None else 0
                except (ValueError, TypeError, OverflowError):
                    break
                if not math.isfinite(value) or not 0 <= value <= 2**53 - 1 or value % 1:
                    break
                counts.append(int(value))
            if len(counts) != 4:
                continue
            when = next((timestamp(meta[k]) for k in ("started_generation_at", "created_at") if isinstance(meta.get(k), str) and timestamp(meta[k]) > 0), 0)
            if not when:
                continue
            session_id = str(row.get("session_id") or "")
            created = timestamp(obj(sessions.get(session_id)).get("created_at")) or float("inf")
            order = when, created, session_id, -integer(row.get("row_id"))
            prior = candidates.get(request.strip())
            if prior is None or order < prior[0]:
                candidates[request.strip()] = order, event(source, meta.get("generation_model"), when, totals(*counts), request.strip())
        for _, result in candidates.values():
            yield result


def claude_science(path, source="claude-science"):
    with database(path) as db:
        for row in rows(db, "frames"):
            def value(field):
                return integer(row.get(field)) + integer(row.get("aux_" + field))
            read, write = value("cache_read_tokens"), value("cache_write_tokens")
            prompt = sum(max(0, integer(row.get(prefix + "input_tokens")) - integer(row.get(prefix + "cache_read_tokens")) - integer(row.get(prefix + "cache_write_tokens"))) for prefix in ("", "aux_"))
            when = next((timestamp(row.get(k)) for k in ("completed_at", "updated_at", "created_at") if timestamp(row.get(k)) > 0), 0)
            yield event(source, row.get("model"), when,
                        totals(prompt, value("output_tokens"), read, write), (str(path), row.get("id")))


def qoder(path, source="qoder"):
    with database(path) as db:
        extras = {row.get("request_id"): obj(json_value(row.get("extra"), {})) for row in rows(db, "chat_record")}
        preferred = {row.get("session_id"): obj(json_value(row.get("preferred_model_info"), {})) for row in rows(db, "chat_session")}
        if "chat_message" not in table_names(db):
            return
        for raw_row in db.execute("SELECT rowid AS row_id,* FROM chat_message"):
            row = dict(raw_row)
            raw = obj(json_value(row.get("token_info"), {}))
            if row.get("role") != "assistant" or not raw:
                continue
            try:
                numbers = [float(raw[k]) if raw.get(k) is not None else 0 if k == "cached_tokens" else float("nan") for k in ("prompt_tokens", "cached_tokens", "completion_tokens")]
            except (ValueError, TypeError, OverflowError):
                continue
            if any(not math.isfinite(n) or n < 0 for n in numbers):
                continue
            prompt, cached, output = [int(n) for n in numbers]
            model_info = obj(json_value(row.get("model_info"), {}))
            record_extra = extras.get(row.get("request_id"), {})
            session_model = preferred.get(row.get("session_id"), {})
            model = first(model_info, "model_key", "modelKey") or obj(first(record_extra, "modelConfig", "model_config")).get("key") or first(session_model, "model_key", "modelKey", "preferred_model", "preferredModel") or "qoder-agent"
            key = (row.get("session_id"), row.get("id")) if row.get("session_id") and row.get("id") else row.get("id") or ("row", row.get("row_id"))
            yield event(source, model, row.get("gmt_create"), totals(max(0, prompt - cached), output, min(prompt, cached)), key)


def unsloth(path, source="unsloth"):
    metered = {"anthropic", "deepseek", "gemini", "huggingface", "kimi", "mistral", "openai", "openrouter", "qwen"}
    with database(path) as db:
        thread_models = {row["id"]: row.get("model_id") for row in rows(db, "chat_threads")}
        for row in rows(db, "chat_messages"):
            if row.get("role") != "assistant":
                continue
            meta = obj(json_value(row.get("metadata_json"), {}))
            details, raw = obj(meta.get("responseDetails")), obj(meta.get("contextUsage"))
            model = details.get("responseModelId") or raw.get("modelId") or thread_models.get(row.get("thread_id")) or "unknown"
            provider = str(details.get("providerType") or "unknown").strip().lower()
            model = str(model).strip()
            model = ("local/" if provider == "local" else "" if provider in metered else "unpriced/") + ("" if provider == "local" or model.lower().startswith(provider + "/") else provider + "/") + model
            usage = local_usage({"prompt_tokens": raw.get("promptTokens"), "completion_tokens": raw.get("completionTokens"),
                                 "total_tokens": raw.get("totalTokens"), "cached_tokens": raw.get("cachedTokens"),
                                 "cache_creation_input_tokens": raw.get("cacheWriteTokens"), "reasoning_tokens": raw.get("reasoningTokens")})
            yield event(source, model, row.get("created_at"), usage, ("chat", row.get("id")))
        if {"id", "model", "prompt_tokens", "completion_tokens", "total_tokens", "created_at"} <= columns(db, "api_usage_events"):
            for row in rows(db, "api_usage_events"):
                usage = local_usage({k: row.get(k) for k in ("prompt_tokens", "completion_tokens", "total_tokens")})
                yield event(source, "local/" + str(row.get("model") or "unknown"), row.get("created_at"), usage, ("api", row.get("id")))


def zed(path, source="zed"):
    with database(path) as db:
        for row in rows(db, "threads"):
            data = row.get("data")
            kind = str(row.get("data_type") or "").strip().lower()
            if kind == "zstd":
                from .compression import decompress_zstd
                data = decompress_zstd(data, 32 * 1024 * 1024)
            elif kind != "json":
                continue
            if not isinstance(data, (str, bytes)) or len(data) > 32 * 1024 * 1024:
                continue
            record = obj(json_value(data, {}))
            if record.get("imported") is True:
                continue
            model = obj(record.get("model")).get("model")
            if not isinstance(model, str) or not model.strip():
                continue
            values = record.get("request_token_usage")
            values = list(values.values()) if isinstance(values, dict) else array(values)
            usage = dict.fromkeys(FIELDS, 0)
            for raw in values:
                raw = obj(raw)
                for key, value in totals(raw.get("input_tokens"), raw.get("output_tokens"), raw.get("cache_read_input_tokens"), raw.get("cache_creation_input_tokens")).items():
                    usage[key] += value
            if not usage["total_tokens"]:
                raw = obj(record.get("cumulative_token_usage"))
                usage = totals(raw.get("input_tokens"), raw.get("output_tokens"), raw.get("cache_read_input_tokens"), raw.get("cache_creation_input_tokens"))
            when = row.get("updated_at") or row.get("created_at") or record.get("updated_at") or Path(path).stat().st_mtime * 1000
            yield event(source, model.strip(), when, usage, row.get("id"), cumulative=True, counter_reset=True)


def workbuddy(path, source="workbuddy"):
    with database(path) as db:
        models = {row["id"]: row.get("model") for row in rows(db, "sessions")}
        settings_model = obj(read_json(Path(path).parent / "settings.json", {})).get("model") or "auto"
        for row in rows(db, "session_usage"):
            keys = {"input_tokens", "inputTokens", "prompt_tokens", "promptTokens", "output_tokens", "outputTokens", "completion_tokens", "completionTokens",
                    "cached_input_tokens", "cachedInputTokens", "cache_read_input_tokens", "cacheReadInputTokens", "cache_read_tokens", "cacheReadTokens",
                    "cache_creation_input_tokens", "cacheCreationInputTokens", "prompt_cache_write_tokens", "promptCacheWriteTokens",
                    "reasoning_output_tokens", "reasoningOutputTokens", "reasoning_tokens", "reasoningTokens"}
            # used/size 是上下文占用，不是累计消耗。
            if not keys.intersection(row) or integer(row.get("used")) <= 0 or integer(row.get("updated_at")) <= 0:
                continue
            session = row.get("session_id")
            if not isinstance(session, str) or not session.strip():
                continue
            usage = totals(first(row, "input_tokens", "inputTokens", "prompt_tokens", "promptTokens"),
                           first(row, "output_tokens", "outputTokens", "completion_tokens", "completionTokens"),
                           first(row, "cached_input_tokens", "cachedInputTokens", "cache_read_input_tokens", "cacheReadInputTokens", "cache_read_tokens", "cacheReadTokens"),
                           first(row, "cache_creation_input_tokens", "cacheCreationInputTokens", "prompt_cache_write_tokens", "promptCacheWriteTokens"),
                           first(row, "reasoning_output_tokens", "reasoningOutputTokens", "reasoning_tokens", "reasoningTokens"))
            yield event(source, models.get(session) or row.get("model") or settings_model, row.get("updated_at"), usage,
                        ("session", session), cumulative=True, counter_reset=True, counter_reset_any=True, session_id=session, fallback=True)
