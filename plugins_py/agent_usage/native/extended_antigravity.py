# extended_antigravity.py — Antigravity protobuf 精确计量与 transcript 估算回退
from __future__ import annotations

import json
import math
from pathlib import Path
import re

from .common import database, event, integer, json_lines, obj, read_json, table_names, totals


def _varint(data, offset):
    value = 0
    for count in range(10):
        if offset >= len(data):
            raise ValueError("truncated_protobuf")
        byte = data[offset]
        offset += 1
        if count == 9 and byte > 1:
            raise ValueError("overflow_protobuf")
        value |= (byte & 127) << (count * 7)
        if not byte & 128:
            return value, offset
    raise ValueError("overflow_protobuf")


def proto_fields(data):
    if not isinstance(data, bytes):
        raise ValueError("invalid_protobuf")
    offset, fields = 0, []
    while offset < len(data):
        tag, offset = _varint(data, offset)
        number, wire = tag >> 3, tag & 7
        if not number:
            raise ValueError("invalid_protobuf_tag")
        if wire == 0:
            value, offset = _varint(data, offset)
            fields.append((number, value))
        elif wire in {1, 2, 5}:
            if wire == 2:
                length, offset = _varint(data, offset)
            else:
                length = 8 if wire == 1 else 4
            if length > len(data) - offset:
                raise ValueError("truncated_protobuf")
            if wire == 2:
                fields.append((number, data[offset:offset + length]))
            offset += length
        else:
            raise ValueError("unsupported_protobuf_wire")
    return fields


def gen_info(data):
    try:
        fields = proto_fields(dict(proto_fields(data)).get(1))
        values = dict(fields)
        model = values.get(19, b"").decode("utf-8").strip()
        context = dict(proto_fields(dict(proto_fields(values.get(9, b""))).get(10, b""))).get(1, 0)
        step = None
        for number, value in fields:
            if number == 20:
                pair = dict(proto_fields(value))
                if pair.get(1) == b"last_step_index" and re.fullmatch(rb"\d+", pair.get(2, b"")):
                    step = int(pair[2])
        usage = dict(proto_fields(values.get(4, b"")))
        # 上游仅接受安全整数；未知字段不能伪装成有效计量。
        has_usage = any(isinstance(usage.get(k), int) and usage[k] <= 2**53 - 1 for k in (1, 2, 3, 5, 9, 10))
        safe = lambda key: usage.get(key, 0) if isinstance(usage.get(key, 0), int) and usage.get(key, 0) <= 2**53 - 1 else 0
        return {"model": model, "context": context if isinstance(context, int) else 0, "step": step,
                "usage": totals(safe(1) + safe(2), safe(9) or max(0, safe(3) - safe(10)), safe(5), 0, safe(10)) if has_usage else None}
    except (ValueError, TypeError, UnicodeError):
        return None


def _model(value):
    value = re.sub(r"\([^)]*\)", " ", str(value or ""))
    value = re.sub(r"\b(thinking|xhigh|high|medium|low|fast)\b", " ", value, flags=re.I)
    slug = re.sub(r"[^a-z0-9.]+", "-", value.lower()).strip("-")
    for marker in ("gemini", "claude", "gpt"):
        if marker in slug:
            slug = slug[slug.index(marker):]
            break
    if not slug:
        return "antigravity-unknown"
    return slug if re.match(r"^(gemini|claude|gpt)-", slug) else "antigravity-" + slug


def estimate(value):
    if value is None:
        return 0
    text = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    # 与上游保持 CJK 每字一 token、其余字符四字一 token 的估算。
    cjk = sum(1 for char in text if (0x3400 <= ord(char) <= 0x4DBF or 0x4E00 <= ord(char) <= 0x9FFF or
              0x3040 <= ord(char) <= 0x30FF))
    return cjk + math.ceil((len(text) - cjk) / 4)


def antigravity(path, source="antigravity"):
    path = Path(path)
    match = re.match(r"^(.*)[/\\]brain[/\\]([^/\\]+)[/\\]\.system_generated[/\\]logs[/\\]transcript.*\.jsonl$", str(path))
    step_map, default_model, session = {}, "antigravity-unknown", str(path)
    if match:
        root, session = Path(match[1]), match[2]
        default_model = _model(obj(read_json(root / "settings.json", {})).get("model"))
        db_path = root / "conversations" / (session + ".db")
        if db_path.is_file():
            # 已存在的数据库读取失败必须传播，避免暂时锁定把精确数据替换成估算。
            with database(db_path) as db:
                if "gen_metadata" in table_names(db):
                    for row in db.execute("SELECT idx,data FROM gen_metadata ORDER BY idx"):
                        info = gen_info(row[1])
                        if info and info["step"] is not None and (info["usage"] or info["context"]):
                            step_map[info["step"] + 1] = info
    context, previous, last_model, model = 0, 0, None, default_model
    for index, record in json_lines(path):
        kind = record.get("type")
        if kind in {"USER_INPUT", "USER_SETTINGS_CHANGE"}:
            selected = re.search(r"changed setting `Model Selection` from .*? to ([^`\n]+?)(?:\s*\([^)]*\))?\.(?:\s+|$)", str(record.get("content", "")), re.I)
            if selected:
                model = _model(selected[1])
        count = estimate(record.get("content")) + (estimate(record.get("tool_calls")) if kind == "PLANNER_RESPONSE" else 0)
        if kind != "PLANNER_RESPONSE" or not record.get("created_at"):
            context += count
            continue
        info = step_map.get(record.get("step_index"), {})
        if info.get("model"):
            model = _model(info["model"])
        if info.get("context"):
            context = info["context"]
        if info.get("usage"):
            usage = info["usage"]
            context = info["context"] or usage["input_tokens"] + usage["cached_input_tokens"]
        else:
            if last_model and last_model != model:
                previous = 0
            usage = totals(max(0, context - previous), estimate(record.get("content")) + estimate(record.get("tool_calls")),
                           reasoning=estimate(record.get("thinking")))
        yield event(source, model, record.get("created_at"), usage,
                    (session, record.get("step_index", index)), usage_precision="reported" if info.get("usage") else "estimated")
        previous, last_model = context, model
        context += count
