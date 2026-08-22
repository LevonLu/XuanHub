# main.py — py_todo 子插件：待办事项（sqlite 持久化）
#
# 模块级四约定：update() -> dict 必需；init(config)、invoke(name,args)、stop() 可选。
# update/invoke 在隔离 worker 的主线程内串行执行——sqlite 读写是毫秒级本地操作，
# 直接在 invoke 里同步做，不起后台线程；update() 只瞬读 _cache（绝不访问 DB，
# 避免每个 tick 一个阻塞点）。DB 单连接单线程（全在 worker 主线程），
# WAL 模式抗崩溃；stop() 关连接释放句柄（热重载时 -wal/-shm 随连接关闭清理）。
import os
import sqlite3
import traceback

# ================= 模块级状态 =================
_config: dict = {}
_cache: dict = {
    "count": 0,
    "activeCount": 0,
    "doneCount": 0,
    "firstActive": None,
    "todos": [],
    "prefix": "📝",
    "barMode": "count",
    "lastErrorKey": None,
    "lastErrorDetail": None,
}
_db: sqlite3.Connection | None = None

# 文本长度上限（超长忽略）
_TEXT_MAX = 200


# ================= 数据库 =================


def _db_path():
    """数据库放在 %APPDATA%/XuanHub/（与 xuanhub.log 同目录），不放 dist 插件目录。"""
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    folder = os.path.join(base, "XuanHub")
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError:
        pass
    return os.path.join(folder, "todo.db")


def _set_database_error(error):
    """保留可渲染的空状态，避免数据库异常让整个插件初始化失败。"""
    global _cache
    _cache = {
        "count": 0,
        "activeCount": 0,
        "doneCount": 0,
        "firstActive": None,
        "todos": [],
        "prefix": str(_config.get("prefix", "📝")),
        "barMode": _normalized_bar_mode(_config.get("barMode", "count")),
        "lastErrorKey": "databaseUnavailable",
        "lastErrorDetail": str(error),
    }


def _open_db():
    """打开连接并建表；失败时降级为可见空状态而非让 worker 退出。"""
    global _db
    if _db is not None:
        return True
    conn = None
    try:
        conn = sqlite3.connect(_db_path())
        conn.execute("PRAGMA journal_mode=WAL")
        conn.execute(
            """
            CREATE TABLE IF NOT EXISTS todos (
                id           INTEGER PRIMARY KEY AUTOINCREMENT,
                text         TEXT    NOT NULL,
                done         INTEGER NOT NULL DEFAULT 0,
                sort_order   INTEGER NOT NULL DEFAULT 0,
                created_at   TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
                completed_at TEXT,
                updated_at   TEXT
            )
            """
        )
        conn.commit()
    except (sqlite3.Error, OSError) as exc:
        if conn is not None:
            try:
                conn.close()
            except sqlite3.Error:
                pass
        _set_database_error(str(exc))
        return False
    _db = conn
    return True


def _next_sort_order():
    """新条目的 sort_order：当前最大值 + 1（追加到列表末尾）。"""
    row = _db.execute("SELECT COALESCE(MAX(sort_order), 0) FROM todos").fetchone()
    return row[0] + 1


def _normalized_bar_mode(value):
    """新配置使用稳定 id；保留旧版中文值兼容。"""
    return "latest" if str(value) in ("latest", "最近一项") else "count"


# ================= 缓存构建 =================


def _rebuild_cache():
    """按 DB 全量重建 _cache（调用方保证已 commit；成功操作后调用会清掉 lastError）。"""
    global _cache
    rows = _db.execute(
        "SELECT id, text, done FROM todos ORDER BY done ASC, sort_order ASC, id ASC"
    ).fetchall()
    todos = [{"id": r[0], "text": r[1], "done": bool(r[2])} for r in rows]
    active = [t for t in todos if not t["done"]]
    _cache = {
        "count": len(todos),
        "activeCount": len(active),
        "doneCount": len(todos) - len(active),
        "firstActive": active[0]["text"] if active else None,
        "todos": todos,
        "prefix": str(_config.get("prefix", "📝")),
        "barMode": _normalized_bar_mode(_config.get("barMode", "count")),
        "lastErrorKey": None,
        "lastErrorDetail": None,
    }


# ================= CRUD =================


def _as_id(args):
    """把 args[0] 转 int；失败返回 -1（不匹配任何条目，命令静默忽略）。"""
    try:
        return int(args[0])
    except (TypeError, ValueError, IndexError):
        return -1


def _add(text: str):
    text = text.strip()
    if not text or len(text) > _TEXT_MAX:
        return
    _db.execute(
        "INSERT INTO todos (text, sort_order) VALUES (?, ?)",
        (text, _next_sort_order()),
    )
    _db.commit()
    _rebuild_cache()


def _toggle(tid: int):
    if tid < 0:
        return
    row = _db.execute("SELECT done FROM todos WHERE id = ?", (tid,)).fetchone()
    if row is None:
        return
    new_done = 0 if row[0] else 1
    _db.execute(
        "UPDATE todos SET done = ?, completed_at = CASE WHEN ? THEN datetime('now','localtime') "
        "ELSE NULL END, updated_at = datetime('now','localtime') WHERE id = ?",
        (new_done, new_done, tid),
    )
    _db.commit()
    _rebuild_cache()


def _edit(tid: int, text: str):
    if tid < 0:
        return
    text = text.strip()
    if not text or len(text) > _TEXT_MAX:
        return
    _db.execute(
        "UPDATE todos SET text = ?, updated_at = datetime('now','localtime') WHERE id = ?",
        (text, tid),
    )
    _db.commit()
    _rebuild_cache()


def _remove(tid: int):
    if tid < 0:
        return
    _db.execute("DELETE FROM todos WHERE id = ?", (tid,))
    _db.commit()
    _rebuild_cache()


def _clear_done():
    _db.execute("DELETE FROM todos WHERE done = 1")
    _db.commit()
    _rebuild_cache()


# ================= 模块级约定 =================


def init(config):
    """注入 config.json 中本子插件的 config 段。

    config 包含：
      - intervalMs: QTimer 刷新间隔（宿主侧使用，本模块不直接使用）
      - prefix / barMode: bar 显示相关，经 _rebuild_cache 汇入 values
    """
    global _config
    _config = dict(config or {})
    if not _open_db():
        return
    _rebuild_cache()


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    return dict(_cache)


def invoke(name, args):
    """QML 侧 plugin.call(name, args) 的入口。

    支持命令：
      - add:         args[0] = 新待办文本
      - toggle:      args[0] = 条目 id（取反完成状态）
      - edit:        args[0] = id, args[1] = 新文本
      - remove:      args[0] = 条目 id
      - clear_done:  删除所有已完成条目
    """
    if _db is None:
        _cache["lastErrorKey"] = _cache.get("lastErrorKey") or "databaseUnavailable"
        return

    try:
        if name == "add":
            _add(str(args[0]) if args else "")
        elif name == "toggle":
            _toggle(_as_id(args))
        elif name == "edit":
            _edit(_as_id(args), str(args[1]) if len(args) > 1 else "")
        elif name == "remove":
            _remove(_as_id(args))
        elif name == "clear_done":
            _clear_done()
    except Exception:
        # 异常穿透 invoke 会拖垮宿主：traceback 进日志、UI 显示错误、保留旧缓存
        traceback.print_exc()
        _cache["lastErrorKey"] = "operationFailed"
        _cache["lastErrorDetail"] = None


def stop():
    """卸载/热重载前由宿主调用，关闭 sqlite 连接（释放句柄与 WAL 文件）。"""
    global _db
    if _db is not None:
        try:
            _db.close()
        except sqlite3.Error:
            pass
        _db = None
