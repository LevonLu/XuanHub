# common.py — 只读数据访问、时间和 Token 规范化
from __future__ import annotations

from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import sqlite3

FIELDS = ("total_tokens", "input_tokens", "output_tokens", "cached_input_tokens",
          "cache_creation_input_tokens", "reasoning_output_tokens")
COMPONENTS = FIELDS[1:]


def number(value):
    try:
        result = float(value)
        return max(0, result) if math.isfinite(result) else 0
    except (TypeError, ValueError, OverflowError):
        return 0


def integer(value):
    return int(number(value))


def obj(value):
    return value if isinstance(value, dict) else {}


def array(value):
    return value if isinstance(value, list) else []


def first(record, *keys, default=None):
    for key in keys:
        if record.get(key) is not None:
            return record[key]
    return default


def json_value(value, default=None):
    if isinstance(value, (str, bytes)):
        try:
            return json.loads(value)
        except (ValueError, UnicodeError):
            return default
    return value if value is not None else default


def read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError, UnicodeError):
        return default


def json_lines(path):
    if str(path).lower().endswith((".zst", ".zstd")):
        from .compression import json_lines as compressed_lines
        yield from compressed_lines(path)
        return
    # 不保存对话正文；未写完整的最后一行留给下次扫描。
    with Path(path).open("rb") as stream:
        for index, line in enumerate(stream):
            try:
                value = json.loads(line)
            except (ValueError, UnicodeError):
                continue
            if isinstance(value, dict):
                yield index, value


def timestamp(value):
    if isinstance(value, (int, float)):
        if not math.isfinite(value) or value <= 0 or value >= 253_402_300_800_000:
            return 0
        return int(value * 1000 if value < 100_000_000_000 else value)
    if isinstance(value, str):
        try:
            if value.replace(".", "", 1).isdigit():
                return timestamp(float(value))
            return int(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp() * 1000)
        except (ValueError, OverflowError, OSError):
            pass
    return 0


def iso(value):
    return datetime.fromtimestamp(value / 1000, timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def identity(*values):
    return hashlib.sha256(json.dumps(values, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def totals(input=0, output=0, cached=0, write=0, reasoning=0, *, reasoning_in_output=False):
    values = [integer(v) for v in (input, output, cached, write, reasoning)]
    return dict(zip(COMPONENTS, values), total_tokens=sum(values) - (values[-1] if reasoning_in_output else 0))


def anthropic(usage):
    output = integer(usage.get("output_tokens"))
    reasoning = min(output, integer(first(obj(usage.get("output_tokens_details")), "thinking_tokens", "reasoning_tokens")))
    return totals(usage.get("input_tokens"), output - reasoning,
                  usage.get("cache_read_input_tokens"), usage.get("cache_creation_input_tokens"), reasoning)


def openai(usage, *, reasoning_in_output=False):
    prompt = integer(first(usage, "prompt_tokens", "input_tokens"))
    output = integer(first(usage, "completion_tokens", "output_tokens"))
    details = obj(first(usage, "prompt_tokens_details", "input_tokens_details"))
    cached = max(integer(usage.get("cached_input_tokens")), integer(usage.get("cache_read_input_tokens")),
                 integer(details.get("cached_tokens")), integer(usage.get("prompt_cache_hit_tokens")))
    write = integer(first(usage, "cache_creation_input_tokens", "cache_write_input_tokens"))
    reasoning = min(output, integer(first(usage, "reasoning_output_tokens", "reasoning_tokens",
                     default=obj(first(usage, "completion_tokens_details", "output_tokens_details")).get("reasoning_tokens"))))
    return totals(max(0, prompt - cached - write), output if reasoning_in_output else max(0, output - reasoning),
                  cached, write, reasoning, reasoning_in_output=reasoning_in_output)


def event(source, model, when, usage, key, *, allow_zero=False, **extra):
    stamp = timestamp(when)
    if stamp <= 0 or not usage or (not allow_zero and not any(number(usage.get(k)) for k in FIELDS)):
        return None
    # 与原采集器保持半小时桶口径；事件身份独立于时间桶，避免重扫累加。
    return {"source": source, "model": str(model or "unknown")[:120],
            "hour_start": iso(stamp // 1_800_000 * 1_800_000),
            "event_id": identity(source, key), **usage, **extra}


@contextmanager
def database(path):
    # mode=ro 保留 WAL 可见性，不用 immutable；绝不对 Agent 的数据库写入 PRAGMA。
    connection = sqlite3.connect(Path(path).resolve().as_uri() + "?mode=ro", uri=True, timeout=2)
    connection.row_factory = sqlite3.Row
    try:
        yield connection
    finally:
        connection.close()


def table_names(connection):
    return {row[0] for row in connection.execute("SELECT name FROM sqlite_master WHERE type='table'")}


def columns(connection, table):
    if not table.replace("_", "").isalnum():
        raise ValueError("invalid_table")
    return {row[1] for row in connection.execute(f'PRAGMA table_info("{table}")')}


def home_path(home, env, key, fallback):
    return Path(os.path.expandvars(env.get(key, "") or str(Path(home) / fallback))).expanduser()


def files_under(roots, patterns):
    seen = set()
    for root in roots:
        root = Path(root)
        if not root.is_dir():
            continue
        for pattern in patterns:
            for path in root.glob(pattern):
                if path.is_file():
                    canonical = os.path.normcase(str(path.resolve()))
                    if canonical not in seen:
                        seen.add(canonical)
                        yield path
