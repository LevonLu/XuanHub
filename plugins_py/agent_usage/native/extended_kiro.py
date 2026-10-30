# extended_kiro.py — Kiro CLI 各代会话格式和历史 SQLite 的 Token 投影
from __future__ import annotations

from collections import Counter
from pathlib import Path
import json
import re

from .common import array, database, event, first, integer, json_lines, json_value, obj, read_json, timestamp, totals
from .databases import rows


def _length(value):
    # JavaScript 的 length 按 UTF-16 代码单元计数，保持上游估算口径。
    return len(value.encode("utf-16-le")) // 2 if isinstance(value, str) else 0


def _chars(value):
    if isinstance(value, str):
        return _length(value)
    if isinstance(value, list):
        return sum(_chars(v) for v in value)
    if isinstance(value, dict):
        return _chars(first(value, "content", "text", "value", "parts", "entries"))
    return 0


def model_name(value):
    name = str(value or "").strip().lower()
    if not name or name == "auto":
        return "kiro-cli-agent"
    name = re.sub(r"^(?:qdev|kiro)::", "", name)
    name = re.sub(r"^(?:arn:aws:bedrock:[^:]*:[^:]*:(?:foundation-model/)?|anthropic\.|openai\.|aws\.)", "", name)
    name = re.sub(r":\d+$", "", name)
    name = re.sub(r"-\d{8}-v\d+$|-v\d+$|-\d{8}$|\.v\d+$", "", name)
    return name or "kiro-cli-agent"


def kiro_ide(path, source="kiro"):
    path = Path(path)
    base = path.parent.parent
    timeline = []
    for chat in base.glob("*/*.chat"):
        meta = obj(obj(read_json(chat, {})).get("metadata"))
        if meta.get("modelId") and meta.get("startTime"):
            timeline.append((timestamp(meta["startTime"]), timestamp(meta.get("endTime") or meta["startTime"]), str(meta["modelId"])))
    timeline.sort()

    def make(row, key, when):
        stamp = timestamp(when)
        match, distance = None, float("inf")
        for start, end, name in timeline:
            current = 0 if start <= stamp <= end else min(abs(stamp - start), abs(stamp - end))
            if current < distance:
                match, distance = name, current
            if current == 0:
                break
        if match and distance < 600_000:
            model = re.sub(r"_\d{8}_V\d+_\d+$|_V\d+$", "", match, flags=re.I).lower().replace("_", "-")
        else:
            model = "kiro-agent"
        return event(source, model, stamp, totals(row.get("tokens_prompt"), row.get("tokens_generated")), (str(base), key))

    if path.suffix == ".jsonl":
        # devdata.sqlite 与此日志记录相同事件，由目录发现器优先选择数据库。
        for index, row in json_lines(path):
            yield make({"tokens_prompt": row.get("promptTokens"), "tokens_generated": row.get("generatedTokens")}, index + 1, path.stat().st_mtime * 1000)
    else:
        with database(path) as db:
            for row in rows(db, "tokens_generated"):
                when = row.get("timestamp")
                if isinstance(when, str) and re.fullmatch(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", when):
                    when = when.replace(" ", "T") + "Z"
                yield make(row, row.get("id"), when)


def kiro_cli_db(path, source="kiro"):
    with database(path) as db:
        for row in rows(db, "conversations_v2"):
            record = obj(json_value(row.get("value"), {}))
            meta = obj(record.get("user_turn_metadata"))
            for request in array(meta.get("requests")):
                request = obj(request)
                if not request.get("request_id"):
                    continue
                yield event(source, model_name(request.get("model_id") or obj(record.get("model_info")).get("model_id")),
                            request.get("request_start_timestamp_ms"),
                            totals(integer(request.get("user_prompt_length")) // 4, integer(request.get("response_size")) // 4),
                            request["request_id"], session_id=row.get("conversation_id"),
                            kiro_lane="db", kiro_message_ids=[request["message_id"]] if request.get("message_id") else [],
                            usage_precision="estimated")


def kiro_cli(path, source="kiro"):
    path = Path(path)
    if path.name == "messages.jsonl":
        yield from _v2(path, source)
        return
    record = obj(read_json(path, {}))
    state = obj(record.get("session_state"))
    turns = array(obj(state.get("conversation_metadata")).get("user_turn_metadatas"))
    session = record.get("session_id") or path.stem
    model_info = obj(obj(state.get("rts_model_state")).get("model_info"))
    model = first(model_info, "model_id", "model_name")
    message_turn = {mid: i for i, turn in enumerate(turns) for mid in array(obj(turn).get("message_ids"))}
    message_chars, kinds, prompt_chars = Counter(), {}, Counter()
    attributed, pending = set(), 0
    sibling = path.with_suffix(".jsonl")
    if sibling.is_file():
        for _, record in json_lines(sibling):
            data = obj(record.get("data"))
            mid = data.get("message_id")
            if not mid:
                continue
            chars = 0
            for part in array(data.get("content")):
                part = obj(part)
                if part.get("kind") == "text":
                    chars += _length(part.get("data"))
                elif part.get("kind") == "toolUse":
                    chars += _length(json.dumps(obj(part.get("data")).get("input") or {}, ensure_ascii=False, separators=(",", ":")))
            message_chars[mid] += chars
            kinds.setdefault(mid, record.get("kind"))
            if record.get("kind") == "Prompt":
                pending += chars
            elif record.get("kind") == "AssistantMessage" and mid in message_turn:
                idx = message_turn[mid]
                if idx not in attributed:
                    attributed.add(idx)
                    prompt_chars[idx], pending = pending, 0
    for index, turn in enumerate(turns):
        turn = obj(turn)
        mids = array(turn.get("message_ids"))
        rand = first(obj(turn.get("loop_id")), "rand", "seed")
        key = str(session) + ":" + str(rand) if rand is not None else mids[0] if mids else None
        if not key:
            continue
        prompt, output = integer(turn.get("input_token_count")), integer(turn.get("output_token_count"))
        estimated = prompt == output == 0
        if estimated:
            prompt = prompt_chars[index] // 4
            output = sum(message_chars[mid] for mid in mids if kinds.get(mid) == "AssistantMessage") // 4
        yield event(source, model_name(turn.get("model_id") or model),
                    first(turn, "request_start_timestamp_ms", "start_timestamp", "end_timestamp"),
                    totals(prompt, output), key, session_id=session, kiro_lane="session", kiro_message_ids=mids,
                    usage_precision="estimated" if estimated else "reported")


def _v2(path, source):
    meta = obj(read_json(path.parent / "session.json", {}))
    session = meta.get("id") or path.parent.name
    pending_chars, pending_time, turn_index, turn = 0, 0, 0, None

    def finish(current):
        if not current or not (current["output"] or current["reasoning"]):
            return None
        when = current["time"] or timestamp(meta.get("createdAt") or meta.get("lastModifiedAt"))
        key = current["execution"] or f"{session}:v2:{turn_index}"
        return event(source, model_name(current["model"] or meta.get("modelId")), when,
                     totals(current["input"] // 4, current["output"] // 4, reasoning=current["reasoning"] // 4), key,
                     session_id=session, kiro_lane="session", kiro_message_ids=current["ids"], usage_precision="estimated")

    for _, record in json_lines(path):
        payload = obj(record.get("payload"))
        kind = str(payload.get("type") or "").lower()
        when = timestamp(record.get("timestamp"))
        if kind in {"user", "turn_start"}:
            if turn:
                yield finish(turn)
                turn_index += 1
                turn = None
            if kind == "user":
                pending_chars, pending_time = _chars(payload.get("content")), when
                continue
        if kind == "usage_summary":
            # credit 与 Token 不可换算；额度采集器单独读取 usage_summary。
            continue
        if kind == "turn_start" or kind in {"assistant", "tool_call", "tool_result"} and turn is None:
            turn = {"execution": payload.get("executionId"), "time": when or pending_time, "input": pending_chars,
                    "output": 0, "reasoning": 0, "model": None, "ids": []}
            pending_chars, pending_time = 0, 0
        if turn is None:
            continue
        turn["execution"] = turn["execution"] or payload.get("executionId")
        if record.get("id") and record["id"] not in turn["ids"]:
            turn["ids"].append(record["id"])
        if kind == "assistant":
            turn["reasoning" if str(payload.get("operationType") or "").lower() == "reasoning" else "output"] += _chars(payload.get("content"))
            turn["model"] = payload.get("reasoningModelId") or turn["model"]
        elif kind == "tool_result":
            turn["input"] += _chars(payload.get("content"))
        elif kind == "turn_end":
            yield finish(turn)
            turn_index += 1
            turn = None
    yield finish(turn)
