# formats.py — 各工具专有 JSON、JSONL 和累计会话格式的解析
from __future__ import annotations

import json
from pathlib import Path
import re
import tomllib

from .common import (FIELDS, anthropic, array, event, first, identity, integer, json_lines,
                     json_value, number, obj, openai, read_json, timestamp, totals)
from .compression import json_lines, read_bytes


def local_usage(raw):
    prompt = integer(first(raw, "prompt_tokens", "promptTokens", "input_tokens", "inputTokens"))
    completion = integer(first(raw, "completion_tokens", "completionTokens", "output_tokens", "outputTokens"))
    total = max(integer(first(raw, "total_tokens", "totalTokens")), prompt + completion)
    pd = obj(first(raw, "prompt_tokens_details", "input_tokens_details", "inputTokensDetails"))
    od = obj(first(raw, "completion_tokens_details", "output_tokens_details", "completionTokensDetails", "outputTokensDetails"))
    cached = min(prompt, max(integer(first(pd, "cached_tokens", "cache_read_tokens")), integer(raw.get("cached_tokens"))))
    write = min(max(0, prompt - cached), max(integer(first(pd, "cache_creation_input_tokens", "cache_write_tokens")), integer(raw.get("cache_creation_input_tokens"))))
    reasoning = min(completion, max(integer(od.get("reasoning_tokens")), integer(raw.get("reasoning_tokens"))))
    return totals(max(0, total - completion - cached - write), completion - reasoning, cached, write, reasoning)


def kimi(path, source="kimi"):
    model = "kimi-for-coding"
    session_root = next((p for p in path.parents if p.name == "sessions"), None)
    if session_root:
        try:
            config = tomllib.loads((session_root.parent / "config.toml").read_text(encoding="utf-8"))
            default = config.get("default_model")
            if isinstance(default, str) and default:
                model = obj(obj(config.get("models")).get(default)).get("model") if source == "kimi" else None
                model = model or default.rsplit("/", 1)[-1]
        except (OSError, ValueError):
            pass
    seen = set()
    for index, entry in json_lines(path):
        if entry.get("type") in {"config.update", "profile.bind", "llm.request"}:
            model = str(entry.get("modelAlias") or entry.get("model") or model).rsplit("/", 1)[-1]
        message = obj(entry.get("message"))
        current = obj(entry.get("event")) if entry.get("type") == "context.append_loop_event" else entry
        legacy = message.get("type") == "StatusUpdate"
        raw = obj(message.get("payload")).get("token_usage") if legacy else current.get("usage") if current.get("type") == "step.end" else None
        if not isinstance(raw, dict):
            continue
        key = obj(message.get("payload")).get("message_id") if legacy else current.get("uuid")
        if not key or key in seen:
            continue
        seen.add(key)
        if legacy or raw.get("inputOther") is not None:
            usage = totals(raw.get("input_other" if legacy else "inputOther"), raw.get("output"),
                           raw.get("input_cache_read" if legacy else "inputCacheRead"),
                           raw.get("input_cache_creation" if legacy else "inputCacheCreation"))
        elif raw.get("cache_read_input_tokens") is not None:
            usage = totals(raw.get("input_tokens"), raw.get("output_tokens"), raw.get("cache_read_input_tokens"), raw.get("cache_creation_input_tokens"))
        else:
            cached = integer(obj(raw.get("input_tokens_details")).get("cached_tokens"))
            usage = totals(max(0, integer(raw.get("input_tokens")) - cached), raw.get("output_tokens"), cached, raw.get("cache_creation_input_tokens"))
        when = first(entry, "timestamp", default=obj(message.get("payload")).get("timestamp")) if legacy else first(entry, "time", default=current.get("time"))
        # 上游把新版 kimi-code 的 wire 协议归入 kimi，同一身份跨迁移副本去重。
        yield event("kimi", model, when, usage, key)


def craft(path, source="craft"):
    # 第一条非空行必须是有效 header，不能越过损坏头部把消息当成摘要。
    with path.open("rb") as stream:
        header = {}
        for line in stream:
            if line.strip():
                header = obj(json_value(line[:1024 * 1024], {}))
                break
    session = header.get("id") or header.get("sdkSessionId")
    if not session or not isinstance(header.get("tokenUsage"), dict):
        return
    raw = obj(header.get("tokenUsage"))
    usage = totals(raw.get("inputTokens"), raw.get("outputTokens"), raw.get("cacheReadTokens"), raw.get("cacheCreationTokens"))
    usage["total_tokens"] = integer(raw.get("totalTokens")) or usage["total_tokens"]
    yield event(source, header.get("model") or "craft-unknown", first(header, "lastMessageAt", "lastUsedAt", "createdAt") or path.stat().st_mtime * 1000,
                usage, session, cumulative=True, cumulative_mode="clamp")


def reasonix(path, source="reasonix"):
    raw = obj(obj(read_json(path, {})).get("usage"))
    meta = obj(read_json(str(path).removesuffix(".telemetry.json") + ".meta", {}))
    prompt, output = integer(raw.get("promptTokens")), integer(raw.get("completionTokens"))
    uncached = min(prompt, integer(raw["cacheMissTokens"])) if raw.get("cacheMissTokens") is not None else max(0, prompt - integer(raw.get("cacheHitTokens")))
    write = min(uncached, integer(raw.get("cacheWriteTokens")))
    reasoning = integer(raw.get("reasoningTokens"))
    model = str(meta.get("model") or "reasonix-unknown").strip().rstrip("/").rsplit("/", 1)[-1]
    when = next((meta[k] for k in ("updated_at", "created_at") if timestamp(meta.get(k)) > 0), path.stat().st_mtime * 1000)
    yield event(source, model, when,
                totals(uncached - write, max(0, output - reasoning), prompt - uncached, write, reasoning), str(path), cumulative=True, cumulative_mode="clamp")


def muse(path, source="muse"):
    model, models, seen = "muse", {}, set()
    for index, entry in json_lines(path):
        records = [obj(json_value(obj(child).get("record_json"), {})) for child in array(entry.get("children"))] if entry.get("retained_frame") else [entry]
        for record in records:
            payload = obj(record.get("payload"))
            if record.get("payload_type") == "run.model.configured":
                config = obj(payload.get("record"))
                model = config.get("model_id") or model
                models[obj(config.get("run_stream")).get("id")] = model
            current = obj(payload.get("event"))
            if record.get("payload_type") != "runtime.session" or current.get("kind") != "goal_usage_attribution":
                continue
            usage_record = obj(current.get("record"))
            raw = obj(usage_record.get("quantity"))
            usage_id = usage_record.get("usage_id")
            if usage_record.get("usage_family") != "provider" or raw.get("reported") is not True or not usage_id or usage_id in seen:
                continue
            seen.add(usage_id)
            prompt, output = integer(raw.get("input_tokens")), integer(raw.get("output_tokens"))
            cached, reasoning = min(prompt, integer(raw.get("cached_tokens"))), min(output, integer(raw.get("reasoning_tokens")))
            yield event(source, models.get(payload.get("run_id"), model), number(record.get("recorded_at")) / 1000,
                        totals(prompt - cached, output, cached, 0, reasoning, reasoning_in_output=True), usage_id)


def qoder(path, source="qoder"):
    for index, record in json_lines(path):
        message = obj(record.get("message"))
        if record.get("type") != "assistant" or message.get("role") != "assistant":
            continue
        raw = message.get("usage")
        if not isinstance(raw, dict):
            continue
        model = re.sub(r"^qoder-custom-[0-9a-f-]{36}/", "", str(message.get("model") or "qoder-agent"), flags=re.I)
        raw = {**raw, "cache_read_input_tokens": first(raw, "cache_read_input_tokens", "cached_tokens")}
        message_id = message.get("id") or record.get("uuid") or record.get("id")
        yield event(source, model, record.get("timestamp") or message.get("timestamp"),
                    totals(raw.get("input_tokens"), raw.get("output_tokens"), raw.get("cache_read_input_tokens"), raw.get("cache_creation_input_tokens")),
                    (record.get("sessionId") or record.get("session_id"), message_id) if message_id else (str(path), index))


def command_code(path, source="command-code"):
    for index, record in json_lines(path):
        raw = record.get("usage")
        if record.get("type") != "message" or not record.get("id") or not isinstance(raw, dict):
            continue
        cached, write = integer(raw.get("cacheReadTokens")), integer(raw.get("cacheWriteTokens"))
        yield event(source, str(record.get("model") or "unknown").rsplit("/", 1)[-1], record.get("timestamp"),
                    totals(max(0, integer(raw.get("inputTokens")) - cached - write), raw.get("outputTokens"), cached, write), record["id"])


def dsh(path, source="dsh"):
    session, model = str(path.parent), ""
    # DSH 的序号不能越过损坏的中间行，否则后续修复时会漏掉缺口。
    for index, line in enumerate(read_bytes(path).splitlines()):
        if not line.strip():
            continue
        record = json_value(line)
        if not isinstance(record, dict):
            break
        kind, data = record.get("type"), obj(record.get("data"))
        if kind in {"session", "session/start"}:
            session = record.get("id") or data.get("id") or data.get("sessionId") or session
        elif kind == "request/header":
            model = obj(obj(data.get("header")).get("config")).get("model") or model
        elif kind in {"assistant/message", "message/assistant"}:
            raw = obj(data.get("usage") or record.get("usage"))
            selected = obj(obj(data.get("message")).get("source")).get("model") or data.get("model") or model
            if not selected:
                continue
            selected = str(selected).rsplit("/", 1)[-1]
            yield event(source, selected, first(record, "time", "timestamp", default=first(data, "time", "timestamp")),
                        totals(raw.get("inputTokens"), raw.get("outputTokens"), raw.get("cacheReadTokens"), raw.get("cacheWriteTokens"), raw.get("reasoningTokens")),
                        (session, record.get("seq", index)))


def kiro_log(path, source="kiro"):
    from .extended_kiro import kiro_ide
    yield from kiro_ide(path, source)


def droid(path, source="droid"):
    record = obj(read_json(path, {}))
    raw = obj(record.get("tokenUsage"))
    def normalize(value):
        value = str(value or "").removeprefix("custom:")
        return re.sub(r"-+", "-", re.sub(r"[\s.]+", "-", re.sub(r"\[[^\]]*\]", "", value).lower())).strip("-")
    model = normalize(record.get("model"))
    if not model:
        try:
            with path.with_name(path.name.removesuffix(".settings.json") + ".jsonl").open(encoding="utf-8") as stream:
                for index, line in enumerate(stream):
                    if index >= 500:
                        break
                    if "Model:" in line:
                        model = normalize(re.split(r'["\\\[]', line.split("Model:", 1)[1], maxsplit=1)[0].strip())
                        if model:
                            break
        except OSError:
            pass
    if not model:
        provider = str(record.get("providerLock") or "").strip().lower().replace("-", "_")
        family = {"claude": "claude", "anthropic": "claude", "openai": "gpt", "google": "gemini", "google_ai": "gemini", "gemini": "gemini", "vertex": "gemini", "vertex_ai": "gemini", "xai": "grok", "x_ai": "grok", "grok": "grok"}.get(provider)
        model = family + "-unknown" if family else "unknown"
    usage = totals(raw.get("inputTokens"), raw.get("outputTokens"), raw.get("cacheReadTokens"), raw.get("cacheCreationTokens"), raw.get("thinkingTokens"))
    missing = max(0, integer(raw.get("totalTokens")) - usage["total_tokens"])
    usage["output_tokens" if not usage["output_tokens"] else "reasoning_output_tokens"] += missing
    usage["total_tokens"] += missing
    yield event(source, model, path.stat().st_mtime * 1000,
                usage, path.name.removesuffix(".settings.json"), cumulative=True, cumulative_mode="reset")


def cline(path, source="cline"):
    data = read_json(path, [])
    messages = data if isinstance(data, list) else array(obj(data).get("messages"))
    meta = obj(read_json(path.parent / (path.parent.name + ".json"), {}))
    imported = timestamp(obj(obj(meta.get("metadata")).get("importedFrom")).get("importedAt"))
    for index, message in enumerate(messages):
        message = obj(message)
        raw = message.get("metrics")
        if message.get("role") != "assistant" or not isinstance(raw, dict):
            continue
        if timestamp(message.get("ts")) <= imported:
            continue
        read, write = integer(raw.get("cacheReadTokens")), integer(raw.get("cacheWriteTokens"))
        usage = totals(max(0, integer(raw.get("inputTokens")) - read - write), raw.get("outputTokens"),
                       read, write, raw.get("reasoningTokenCount"), reasoning_in_output=True)
        provider = re.sub(r"[^a-z0-9._-]", "", str(obj(message.get("modelInfo")).get("provider") or "").strip().lower())
        model = obj(message.get("modelInfo")).get("id") or meta.get("model") or ("provider:" + provider if provider else "unknown")
        yield event(source, model, message.get("ts"), usage, (path.parent.name, path.name, message.get("id") or (message.get("ts"), index)))


def roo(path, source="roocode"):
    data = array(read_json(path, []))
    try:
        history = path.with_name("api_conversation_history.json").read_text(encoding="utf-8")[-1048576:]
    except OSError:
        history = ""
    found = re.findall(r"<model>\s*([^<\s][^<]*?)\s*</model>", history)
    model = found[-1].strip() if found else None
    seen = set()
    for record in data:
        record = obj(record)
        if record.get("say") not in {"api_req_started", "api_req_deleted"}:
            continue
        raw = obj(json_value(record.get("text"), {}))
        usage = totals(raw.get("tokensIn"), raw.get("tokensOut"), raw.get("cacheReads"), raw.get("cacheWrites"))
        key = (path.parent.name, record.get("ts"))
        if key in seen or not usage["total_tokens"]:
            continue
        seen.add(key)
        if source in {"kilocode", "kilo-code"}:
            slug = re.sub(r"[^a-z0-9._-]", "", re.sub(r"\s+", "-", str(raw.get("inferenceProvider") or "").strip().lower()))
            selected = "provider:" + (slug if re.search(r"[a-z0-9]", slug) else "unknown")
        else:
            protocol = re.sub(r"[^a-z0-9._-]", "", str(raw.get("apiProtocol") or "").strip().lower())
            selected = model or ("protocol:" + protocol if protocol else "unknown")
        yield event("kilo-code" if source == "kilocode" else source, selected, record.get("ts"), usage, (path.parent.name, record.get("ts")))


def grok(path, source="grok"):
    signals = obj(read_json(path.parent / "signals.json", {}))
    summary = obj(read_json(path.parent / "summary.json", {}))
    model = signals.get("primaryModelId") or next(iter(array(signals.get("modelsUsed"))), None) or signals.get("model") or "grok-build"
    fallback_time = first(signals, "lastActiveAt", "updatedAt", "lastActive", default=first(summary, "updated_at", "updatedAt")) or path.stat().st_mtime * 1000
    updates = path.parent / "updates.jsonl"
    contexts, turns, seen = [], [], set()
    for index, record in json_lines(updates) if updates.is_file() else []:
        params = obj(record.get("params"))
        update = obj(params.get("update"))
        meta = obj(params.get("_meta") or record.get("_meta"))
        when = first(meta, "agentTimestampMs", "timestampMs", default=first(record, "timestamp_ms", "timestamp", "time", default=fallback_time))
        key = first(meta, "eventId", default=record.get("eventId") or record.get("id") or update.get("prompt_id") or index)
        if update.get("sessionUpdate") != "turn_completed" or not isinstance(update.get("usage"), dict):
            if integer(meta.get("totalTokens")):
                contexts.append((key, when, integer(meta["totalTokens"])))
            continue
        raw = obj(update.get("usage"))
        model_usage = obj(raw.get("modelUsage")) or {model: raw}
        for selected, usage in model_usage.items():
            usage = obj(usage)
            event_key = (path.parent.name, key, selected)
            if event_key in seen:
                continue
            prompt = integer(first(usage, "inputTokens", "input_tokens"))
            read = integer(first(usage, "cachedReadTokens", "cacheReadInputTokens", "cache_read_input_tokens", "cached_input_tokens"))
            write = integer(first(usage, "cacheCreationTokens", "cachedWriteTokens", "cacheWriteInputTokens", "cache_creation_input_tokens"))
            reasoning = integer(first(usage, "reasoningTokens", "reasoning_output_tokens"))
            output = integer(first(usage, "outputTokens", "output_tokens"))
            normalized = totals(max(0, prompt - read - write) if "inputTokens" in usage else prompt, max(0, output - reasoning), read, write, reasoning)
            normalized["total_tokens"] = integer(first(usage, "totalTokens", "total_tokens")) or normalized["total_tokens"]
            lower = str(selected).lower()
            selected = "grok-build-free" if "build-free" in lower or lower.endswith("-free") or "free-tier" in lower else "grok-4.5-build" if lower in {"grok-4.5-build", "grok-4-5-build"} else selected
            item = event(source, selected, when, normalized, event_key, usage_precision="reported")
            if item:
                seen.add(event_key)
                turns.append(item)
    if turns:
        yield from turns
        return
    # 旧版 signals/context 只有上下文水位：保留上游 80/20 估算并显式标注，不能叠加到真实 turn 用量。
    high = 0
    signal_total = integer(signals.get("totalTokensBeforeCompaction")) + integer(signals.get("totalTokens"))
    if signals.get("contextTokensUsed") is not None:
        signal_total = max(integer(signals.get("totalTokens")), integer(signals.get("totalTokensBeforeCompaction")) + integer(signals.get("contextTokensUsed")))
    for key, when, value in contexts + [("signals", fallback_time, signal_total)]:
        if value <= high:
            continue
        delta = value - high
        high = value
        prompt = int(delta * .8 + .5)
        yield event(source, model, when, totals(prompt, delta - prompt), (path.parent.name, "context", key), usage_precision="estimated")


def workbuddy_trace(path, source="workbuddy"):
    record = obj(obj(read_json(path, {})).get("trace"))
    metadata = obj(record.get("metadata"))
    raw = obj(record.get("modelInfo")) or obj(metadata.get("modelInfo"))
    prompt, cached = integer(raw.get("totalInputTokens")), integer(raw.get("totalCachedTokens"))
    session = record.get("sessionId") or metadata.get("sessionId") or record.get("traceId") or path.stem
    root = next((p.parent for p in path.parents if p.name == "traces"), None)
    fallback_model = obj(read_json(root / "settings.json", {})).get("model") if root else None
    yield event(source, next(iter(array(raw.get("models"))), None) or raw.get("model") or fallback_model or "auto",
                record.get("startedAt") or metadata.get("startedAt"), totals(max(0, prompt - cached), raw.get("totalOutputTokens"), min(prompt, cached)),
                ("trace", session), session_id=session, fallback=True)


def lmstudio(path, source="lmstudio"):
    # 服务器日志的 usage 对象可能跨行；只解码匹配的对象，不把正文落入缓存。
    text = path.read_text(encoding="utf-8", errors="replace")
    decoder = json.JSONDecoder()
    for match in re.finditer(r'(?<!\\)"usage"\s*:\s*\{', text):
        start = text.index("{", match.start())
        try:
            raw, _ = decoder.raw_decode(text[start:])
        except ValueError:
            continue
        prefix = text[max(0, start - 8192):start]
        times = list(re.finditer(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\]\[", prefix, re.M))
        models = list(re.finditer(r'"model"\s*:\s*"([^"\r\n]+)"', prefix))
        ids = list(re.finditer(r'"id"\s*:\s*"((?:chatcmpl-|cmpl-|resp_)[^"\r\n]+)"', prefix))
        yield event(source, models[-1][1] if models else "unknown", times[-1][1] if times else path.stat().st_mtime * 1000,
                    local_usage(raw), ids[-1][1] if ids else (str(path), start))
