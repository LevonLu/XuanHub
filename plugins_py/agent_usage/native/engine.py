# engine.py — 发现本地数据源、隔离单源故障并发布完整用量快照
from __future__ import annotations

import importlib
import os
from pathlib import Path
import sqlite3
import time

from . import account_usage
from .common import identity
from .discovery import discover
from .network import Http
from .pricing import Pricing
from .store import Store

VERSION = "native-0.2.0"
PARSER_VERSION = 2


def signature(path, parser):
    paths = [path, Path(str(path) + "-wal")]
    # 这些格式的模型、精确计数或授权元数据存于伴随文件，变化也须触发重算。
    if parser == "extended.antigravity":
        root = path.parents[4]
        db = root / "conversations" / (path.parents[2].name + ".db")
        paths += [db, Path(str(db) + "-wal"), root / "settings.json"]
    if parser.startswith("formats.") or parser == "extended.kiro_ide":
        paths.extend(p for p in path.parent.iterdir() if p.suffix.lower() in {".json", ".meta", ".chat", ".jsonl"})
    if parser == "extended.kiro_cli":
        paths += [path.with_suffix(".jsonl"), path.parent / "session.json"]
    if parser == "extended.kiro_ide":
        paths.extend(path.parent.parent.glob("*/*.chat"))
    if parser == "databases.workbuddy":
        paths.append(path.parent / "settings.json")
    if parser in {"extended.codebuddy", "transcripts.workbuddy"}:
        root = next((p.parent for p in path.parents if p.name == "projects"), None)
        if root:
            paths.append(root / "settings.json")
    if parser in {"formats.kimi", "formats.workbuddy_trace"}:
        dirname = "sessions" if parser == "formats.kimi" else "traces"
        root = next((p.parent for p in path.parents if p.name == dirname), None)
        if root:
            paths.append(root / ("config.toml" if dirname == "sessions" else "settings.json"))
    values = []
    for item in sorted(set(paths)):
        try:
            stat = item.stat()
            values.append((str(item), stat.st_size, stat.st_mtime_ns, stat.st_ino))
        except FileNotFoundError:
            pass
    return identity(PARSER_VERSION, values)


def parser_for(name):
    module, function = name.split(".", 1)
    if module not in {"transcripts", "formats", "databases", "extended"}:
        raise RuntimeError("unknown_parser")
    return getattr(importlib.import_module("." + module, __package__), function)


def error_code(error):
    if isinstance(error, sqlite3.Error):
        return "database_unavailable"
    if isinstance(error, OSError):
        return "file_unavailable"
    known = {"compression_unavailable", "decompressed_too_large", "trae_database_too_large",
             "trae_decryption_failed", "crypto_unavailable", "invalid_response", "reauth", "capacity_exceeded", "unsupported_usage_record", "trae_database_changed"}
    if str(error) == "trae_sqlcipher_authentication":
        return "trae_decryption_failed"
    return str(error) if isinstance(error, (RuntimeError, ValueError)) and str(error) in known else "parse_failed"


def collect(request, *, home=None, env=None, accounts=True, fetch_prices=True):
    cache = Path(request["cacheDir"]) / "native-v2"
    home = Path.home() if home is None else Path(home)
    env = dict(os.environ if env is None else env)
    prices = Pricing(cache / "prices.json", Http(8) if fetch_prices and request["lane"] == "usage" else None)
    store = Store(cache / "usage.sqlite3", readonly=request["lane"] == "detail")
    try:
        if request["lane"] == "usage":
            errors, known, droid = {}, set(), {}
            store.begin()
            for source, parser_name, path in discover(home, env):
                known.add({"kilocode": "kilo-code", "kimi-code": "kimi"}.get(source, source))
                try:
                    current = signature(path, parser_name)
                    key = os.path.normcase(str(path.resolve()))
                    if source != "droid" and store.signature(key) == current:
                        continue
                    entries = list(parser_for(parser_name)(path, source))
                    for row in entries:
                        if row and row.get("cumulative"):
                            row.setdefault("source_updated", path.stat().st_mtime_ns // 1_000_000)
                        if row and parser_name == "formats.workbuddy_trace":
                            row["fallback_rank"] = 2
                    if source == "droid":
                        for row in filter(None, entries):
                            previous = droid.get(row["event_id"])
                            rank = (row["total_tokens"], row["source_updated"])
                            if previous is None or rank > previous[0] or (rank == previous[0] and key < previous[1]):
                                droid[row["event_id"]] = (rank, key, row)
                        continue
                    # 有效空响应不抹掉先前记录，文件旋转和暂时未写完整时尤其重要。
                    if any(entries) or store.signature(key) is None:
                        append = path.suffix.lower() in {".jsonl", ".log", ".zst", ".zstd"} and parser_name != "formats.grok"
                        store.replace(key, current, entries, append=append)
                except Exception as error:
                    errors[{"kilocode": "kilo-code", "kimi-code": "kimi"}.get(source, source)] = error_code(error)
            for event_id, (_, _, row) in droid.items():
                store.replace("droid:" + event_id, str(row["source_updated"]), [row])
            if accounts:
                values, failures = account_usage.collect(home, env)
                errors.update(failures)
                for source, entries in values.items():
                    if entries:
                        known.add(source)
                        # 账号接口可能仅返回最近窗口。只更新返回的事件，保留更早历史。
                        store.replace("account:" + source, str(time.time_ns()), entries, append=True)
            store.set_metadata("knownSources", sorted(known))
            store.set_metadata("sourceErrors", errors)
            store.set_metadata("capturedAt", time.time())
            store.commit()
        return {**store.query(request, prices), "version": VERSION}
    finally:
        store.close()
