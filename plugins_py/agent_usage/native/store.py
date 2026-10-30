# store.py — 原子发布采集快照、记录累计差值并按本地日期查询
from __future__ import annotations

from datetime import datetime
import json
from pathlib import Path
import sqlite3

from .common import FIELDS, identity, number, timestamp
from .extended_reconcile import reconcile

SCHEMA_VERSION = 2


def packed(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


class Store:
    def __init__(self, path, *, readonly=False):
        self.path = Path(path)
        if readonly:
            if not self.path.is_file():
                raise RuntimeError("data_not_ready")
            self.db = sqlite3.connect(self.path.resolve().as_uri() + "?mode=ro", uri=True, timeout=5)
            return
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(self.path, timeout=20)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS files (path TEXT PRIMARY KEY, signature TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS events (
                file TEXT NOT NULL, event_id TEXT NOT NULL, source TEXT NOT NULL,
                stamp INTEGER NOT NULL, data TEXT NOT NULL,
                PRIMARY KEY (file,event_id));
            CREATE INDEX IF NOT EXISTS event_query ON events (source,stamp);
            CREATE INDEX IF NOT EXISTS event_identity ON events (event_id);
            CREATE TABLE IF NOT EXISTS counters (event_id TEXT PRIMARY KEY, data TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, data TEXT NOT NULL);
        ''')

    def close(self):
        self.db.close()

    def begin(self):
        # 一轮采集一次提交；后台被终止时自动回滚，详情仍读上轮完整数据。
        self.db.execute("BEGIN IMMEDIATE")

    def commit(self):
        self.db.commit()

    def signature(self, path):
        row = self.db.execute("SELECT signature FROM files WHERE path=?", (str(path),)).fetchone()
        return row[0] if row else None

    def metadata(self, key, default=None):
        row = self.db.execute("SELECT data FROM metadata WHERE key=?", (key,)).fetchone()
        return json.loads(row[0]) if row else default

    def set_metadata(self, key, value):
        self.db.execute("INSERT OR REPLACE INTO metadata VALUES (?,?)", (key, packed(value)))

    def put(self, file, row):
        self.db.execute("INSERT OR REPLACE INTO events VALUES (?,?,?,?,?)", (
            str(file), row["event_id"], row["source"], timestamp(row["hour_start"]), packed(row)))

    def cumulative(self, row):
        base = row["event_id"]
        previous = self.db.execute("SELECT data FROM counters WHERE event_id=?", (base,)).fetchone()
        old = json.loads(previous[0]) if previous else {}
        if number(row.get("source_updated")) < number(old.get("source_updated")):
            return
        if previous and "source_updated" in row and row.get("source_updated") == old.get("source_updated"):
            # 复制文件可能保留 mtime；同一版本只选确定的较大快照，不能来回降低基线。
            if tuple(number(row.get(k)) for k in FIELDS) <= tuple(number(old.get(k)) for k in FIELDS):
                return
        numeric = {k for k, v in row.items() if (k.endswith("_tokens") or k == "total_cost_usd") and isinstance(v, (int, float))}
        reset = (row.get("counter_reset") or row.get("cumulative_mode") == "reset") and number(row.get("total_tokens")) < number(old.get("total_tokens"))
        reset |= bool(row.get("counter_reset_any") and any(number(row.get(k)) < number(old.get(k)) for k in FIELDS))
        delta = {k: max(0, number(row.get(k)) - (0 if reset else number(old.get(k)))) for k in numeric}
        delta["total_tokens"] = sum(delta.get(k, 0) for k in FIELDS[1:])
        # 原工具的累计计数器下降表示重置或被修正，下轮从新基线继续；历史不搬移。
        if any(delta.values()):
            entry = {**row, **delta, "event_id": identity(base, old.get("_revision", 0) + 1)}
            entry.pop("cumulative", None)
            self.put("counter:" + base, entry)
        self.db.execute("INSERT OR REPLACE INTO counters VALUES (?,?)", (base, packed({**row, "_revision": old.get("_revision", 0) + 1})))

    def replace(self, path, signature, events, *, append=False):
        # 先完整解析再写入，解析失败保留该文件上次成功快照。
        entries = [row for row in events if row]
        self.db.execute("SAVEPOINT file_update")
        try:
            if not append:
                self.db.execute("DELETE FROM events WHERE file=?", (str(path),))
            for row in entries:
                if row.get("cumulative"):
                    self.cumulative(row)
                else:
                    self.put(path, row)
            self.db.execute("INSERT OR REPLACE INTO files VALUES (?,?)", (str(path), signature))
        except BaseException:
            self.db.execute("ROLLBACK TO file_update")
            raise
        finally:
            self.db.execute("RELEASE file_update")

    def query(self, request, pricing):
        # 先选择全库最新身份，再跨格式去重，最后过滤时间，防止边界镜像重复。
        sql = '''SELECT data FROM (
            SELECT data,ROW_NUMBER() OVER (PARTITION BY event_id ORDER BY
                COALESCE(json_extract(data,'$.source_updated'),0) DESC,stamp DESC,file ASC) AS n
            FROM events) WHERE n=1'''
        records = [json.loads(raw) for (raw,) in self.db.execute(sql)]
        sessions = {}
        for row in records:
            if row.get("session_id") and number(row.get("total_tokens")) > 0:
                key = (row["source"], row["session_id"])
                sessions[key] = max(sessions.get(key, 0), row.get("fallback_rank", 1 if row.get("fallback") else 3))
        records = [r for r in records if r.get("fallback_rank", 1 if r.get("fallback") else 3) >= sessions.get((r["source"], r.get("session_id")), 0)]
        records = reconcile(records)
        # OpenCode fork 复制保留同会话的不同消息，只去掉别的会话里相同指纹的镜像。
        counts, unique = {}, []
        for row in records:
            fp, session = row.get("fork_fingerprint"), row.get("fork_session")
            if fp and session:
                sessions_for_fp = counts.setdefault(fp, {})
                sessions_for_fp[session] = sessions_for_fp.get(session, 0) + 1
        owner = {fp: max(sessions, key=lambda session: (sessions[session], session)) for fp, sessions in counts.items()}
        for row in records:
            fp, session = row.get("fork_fingerprint"), row.get("fork_session")
            if fp and session and owner[fp] != session:
                continue
            unique.append(row)
        known = sorted({r["source"] for r in unique} | set(self.metadata("knownSources", [])))
        sources, days, models = {}, {}, {}
        has_records = False
        for row in unique:
            stamp = timestamp(row["hour_start"])
            source, model = row["source"], row["model"]
            if not request["fromMs"] <= stamp < request["toMs"] or request.get("sourceId") not in (None, "", source):
                continue
            if not any(number(row.get(k)) for k in FIELDS):
                continue
            cost, unpriced = pricing.cost(row)
            target = sources.setdefault(source, {"id": source, "scope": scope(source), "models": set(), **empty()})
            target["models"].add(model)
            targets = [target]
            if request.get("lane") == "detail":
                day = datetime.fromtimestamp(stamp / 1000).date().isoformat()
                targets += [days.setdefault(day, {"day": day, **empty()}), models.setdefault(model, {"model": model, **empty()})]
            for target in targets:
                for field in FIELDS:
                    target[field] += number(row.get(field))
                target["cost"] += cost
                target["unpriced"] |= unpriced
                target["estimated"] |= row.get("usage_precision") == "estimated"
            has_records = True
        for source in sources.values():
            source["models"] = sorted(source["models"])
        diagnostics = self.metadata("sourceErrors", {})
        if request.get("lane") == "detail":
            source_id = request["sourceId"]
            return {"sourceId": source_id, "totals": sources.get(source_id, empty()), "scope": scope(source_id),
                    "hasRecords": has_records, "byDay": sorted(days.values(), key=lambda x: x["day"]),
                    "byModel": sorted(models.values(), key=lambda x: -x["total_tokens"]), "malformed": 0,
                    "sourceError": diagnostics.get(source_id, "")}
        return {"sources": list(sources.values()), "knownSources": known, "malformed": 0, "sourceErrors": diagnostics}


def empty():
    return {**dict.fromkeys(FIELDS, 0), "cost": 0, "unpriced": False, "estimated": False}


def scope(source):
    return "account" if source in {"cursor", "trae-cn"} else "local"
