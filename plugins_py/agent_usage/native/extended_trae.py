# extended_trae.py — TRAE 用量校验与 SQLCipher/WAL 只读内存快照
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import hmac
import os
from pathlib import Path
import re
import sqlite3
import struct

from .common import columns, database, event, first, integer, json_value, obj, table_names, totals

SQLITE_HEADER = b"SQLite format 3\x00"
# TRAE 应用公开共享格式密钥，来自 MIT 许可的 TokenTracker；不是用户凭据。
TRAE_APPLICATION_KEY = "3605f6691095a993f03d5009c918352ef5be31ae31e8f000212b81ff058da773"
MAX_DATABASE = 256 * 1024 * 1024


def normalize_trae_model(value):
    model = re.sub(r"\s+", "-", str(value or "").split("__")[0].strip().lower())
    if model == "claude-4-sonnet":
        return "claude-sonnet-4-20250514"
    return model if model and model != "-" and "|" not in model else "trae-unknown"


def normalize_trae_usage(raw, model=None):
    raw = json_value(raw)
    if not isinstance(raw, dict):
        return None
    keys = ("prompt_tokens", "prompt_tokens_total", "input_tokens", "completion_tokens", "completion_tokens_total", "output_tokens",
            "cache_read_input_tokens", "cache_creation_input_tokens", "reasoning_tokens", "reasoning_output_tokens", "total_tokens")
    if any(raw.get(k) is not None and (not isinstance(raw[k], (int, float)) or isinstance(raw[k], bool) or not 0 <= raw[k] <= 2**53 - 1 or raw[k] % 1 != 0) for k in keys):
        return None
    raw = {k: int(v) if k in keys and v is not None else v for k, v in raw.items()}
    if any(raw.get(a) is not None and raw.get(b) is not None and raw[a] != raw[b] for a, b in
           (("prompt_tokens", "input_tokens"), ("completion_tokens", "output_tokens"), ("reasoning_tokens", "reasoning_output_tokens"))):
        return None
    if not any(raw.get(k) is not None for k in keys[:6]):
        return None
    prompt = first(raw, "prompt_tokens", "input_tokens", "prompt_tokens_total", default=0)
    output = first(raw, "completion_tokens", "output_tokens", "completion_tokens_total", default=0)
    aggregate_input, aggregate_output = raw.get("prompt_tokens_total") or prompt, raw.get("completion_tokens_total") or output
    if aggregate_input < prompt or aggregate_output < output:
        return None
    cached, write = raw.get("cache_read_input_tokens") or 0, raw.get("cache_creation_input_tokens") or 0
    reasoning = first(raw, "reasoning_tokens", "reasoning_output_tokens", default=0)
    estimated = False
    gemini = bool(re.match(r"^gemini(?:-|$)", normalize_trae_model(model or raw.get("name"))))
    if gemini and cached > 0 and write == cached and cached + write > prompt:
        write, estimated = 0, True
    if gemini and raw.get("reasoning_tokens") is None and raw.get("reasoning_output_tokens") is None and (raw.get("total_tokens") or 0) > prompt + output and write == 0 and cached <= prompt:
        reasoning, estimated = raw["total_tokens"] - prompt - output, True
    if raw.get("total_tokens") is None and (cached or write or reasoning):
        return None
    total = first(raw, "total_tokens", default=prompt + output)
    candidates = {}
    for included_cache in {cached + write, cached, 0}:
        for included_reason in {reasoning, 0}:
            uncached, normal_output = prompt - included_cache, output - included_reason
            if min(uncached, normal_output) < 0:
                continue
            values = totals(uncached, normal_output, cached, write, reasoning)
            if values["total_tokens"] == total:
                candidates[tuple(values.items())] = values
    if len(candidates) != 1:
        return None
    usage = next(iter(candidates.values()))
    unpriced = aggregate_input - prompt
    usage["output_tokens"] += aggregate_output - output
    usage["total_tokens"] += unpriced + aggregate_output - output
    if unpriced:
        usage["unpriced_input_tokens"] = unpriced
    if estimated:
        usage["usage_precision"] = "estimated"
    return usage if all(v <= 2**53 - 1 for v in usage.values() if isinstance(v, int)) else None


def _stat(path):
    try:
        stat = path.stat()
        return stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_ino
    except FileNotFoundError:
        return None


def _checksum(data, endian, state=(0, 0)):
    a, b = state
    for offset in range(0, len(data), 8):
        x, y = struct.unpack_from(endian + "II", data, offset)
        a = (a + x + b) & 0xFFFFFFFF
        b = (b + y + a) & 0xFFFFFFFF
    return a, b


def _wal_pages(wal, page_size):
    pages, pending, page_count = {}, {}, None
    if not wal:
        return pages, page_count
    if len(wal) < 32:
        raise ValueError("trae_incomplete_wal")
    magic, version, size = struct.unpack_from(">III", wal)
    if magic not in {0x377F0682, 0x377F0683} or version != 3007000 or size != page_size:
        raise ValueError("trae_unsupported_wal")
    endian = ">" if magic == 0x377F0683 else "<"
    checksum = _checksum(wal[:24], endian)
    if checksum != struct.unpack_from(">II", wal, 24):
        raise ValueError("trae_wal_checksum")
    for offset in range(32, len(wal) - page_size - 23, page_size + 24):
        frame = wal[offset:offset + page_size + 24]
        if frame[8:16] != wal[16:24]:
            break
        checksum = _checksum(frame[24:], endian, _checksum(frame[:8], endian, checksum))
        if checksum != struct.unpack_from(">II", frame, 16):
            break
        number, commit = struct.unpack_from(">II", frame)
        if number in {0, 0xFFFFFFFF} or commit == 0xFFFFFFFF:
            raise ValueError("trae_wal_page")
        pending[number] = frame[24:]
        if commit:
            pages.update(pending)
            pending = {}
            page_count = commit
            pages = {key: value for key, value in pages.items() if key <= commit}
    return pages, page_count


def _decrypt(page, number, key, hmac_key):
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    start, iv_offset = (16 if number == 1 else 0), 4096 - 80
    expected = hmac.new(hmac_key, page[start:iv_offset + 16] + struct.pack("<I", number), "sha512").digest()
    if not hmac.compare_digest(expected, page[iv_offset + 16:]):
        raise ValueError("trae_sqlcipher_authentication")
    decryptor = Cipher(algorithms.AES(key), modes.CBC(page[iv_offset:iv_offset + 16])).decryptor()
    clear = decryptor.update(page[start:iv_offset]) + decryptor.finalize()
    result = (SQLITE_HEADER if number == 1 else b"") + clear + b"\x00" * 80
    if number == 1 and (struct.unpack_from(">H", result, 16)[0] != 4096 or result[20] != 80):
        raise ValueError("trae_sqlcipher_format")
    return result


def encrypted_snapshot(path, env=None):
    env = os.environ if env is None else env
    path = Path(path)
    paths = [path, Path(str(path) + "-wal"), Path(str(path) + "-journal")]
    for _ in range(3):
        before = [_stat(p) for p in paths]
        if not before[0]:
            raise FileNotFoundError(path)
        if any(stat and stat[0] > MAX_DATABASE for stat in before):
            raise ValueError("trae_database_too_large")
        raw = path.read_bytes()
        wal = paths[1].read_bytes() if before[1] else b""
        if before[2]:
            with paths[2].open("rb") as stream:
                if any(stream.read(8)):
                    raise ValueError("trae_active_rollback")
        if before != [_stat(p) for p in paths]:
            continue
        if len(raw) < 4096 or len(raw) % 4096:
            raise ValueError("trae_truncated_database")
        key_text = str(env.get("TOKENTRACKER_TRAE_SQLCIPHER_KEY") or TRAE_APPLICATION_KEY).strip()
        if not re.fullmatch(r"[0-9a-fA-F]{64}", key_text):
            raise ValueError("trae_invalid_key")
        key = bytes.fromhex(key_text)
        hmac_key = hashlib.pbkdf2_hmac("sha512", key, bytes(v ^ 0x3A for v in raw[:16]), 2, 32)
        pages, count = _wal_pages(wal, 4096)
        count = len(raw) // 4096 if count is None else count
        if not 0 < count * 4096 <= MAX_DATABASE:
            raise ValueError("trae_database_too_large")
        decoded = bytearray(count * 4096)
        accepted = False
        try:
            for number in range(1, count + 1):
                data = pages.get(number, raw[(number - 1) * 4096:number * 4096])
                if len(data) != 4096:
                    raise ValueError("trae_truncated_database")
                decoded[(number - 1) * 4096:number * 4096] = _decrypt(data, number, key, hmac_key)
            decoded[18:20] = b"\x01\x01"
            struct.pack_into(">I", decoded, 28, count)
            decoded[92:96] = decoded[24:28]
            if before != [_stat(p) for p in paths]:
                continue
            accepted = True
            return decoded
        finally:
            if not accepted:
                decoded[:] = b"\x00" * len(decoded)
    raise ValueError("trae_database_changed")


@contextmanager
def _trae_database(path):
    with Path(path).open("rb") as stream:
        encrypted = stream.read(16) != SQLITE_HEADER
    if not encrypted:
        with database(path) as db:
            yield db
        return
    clear = encrypted_snapshot(path)
    db = sqlite3.connect(":memory:")
    db.row_factory = sqlite3.Row
    try:
        # Python 3.12 的 deserialize 不写入磁盘；设置只读查询以防将来调用误改。
        db.deserialize(clear)
        db.execute("PRAGMA query_only=ON")
        yield db
    finally:
        db.close()
        clear[:] = b"\x00" * len(clear)


def trae(path, source="trae"):
    with _trae_database(path) as db:
        if "chat_turn" not in table_names(db):
            raise ValueError("trae_unsupported_schema")
        available = columns(db, "chat_turn")
        if "context" not in available or not {"created_at", "updated_at"}.intersection(available):
            raise ValueError("trae_unsupported_schema")
        for raw in db.execute("SELECT * FROM chat_turn" if "id" in available else "SELECT rowid AS _rowid,* FROM chat_turn"):
            row = dict(raw)
            context = obj(json_value(row.get("context"), {}))
            model_info = obj(obj(context.get("persist_user_message_context")).get("model_info"))
            candidates = [model_info.get(k) for k in ("display_model_name", "model_name", "config_name")]
            candidates.extend(context.get(k) for k in ("model_name", "model", "agent_model"))
            candidates.extend(obj(context.get(k)).get("name") for k in ("selected_model", "selectedModel"))
            model = normalize_trae_model(next((v for v in candidates if isinstance(v, str) and v.strip() not in {"", "-"}), None))
            usage = normalize_trae_usage(context.get("token_usage"), model)
            if usage is None:
                continue
            key = (str(path), row.get("session_id"), row.get("turn_id") or row.get("id") or row.get("_rowid"))
            yield event(source, model, row.get("created_at") or row.get("updated_at"), usage, key)
