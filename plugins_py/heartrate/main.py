# main.py — py_heartrate 子插件：bleak 心率显示
#
# 模块级四约定：update() -> dict 必需；init(config)、invoke(name,args)、stop() 可选。
# update/invoke 在隔离 worker 的主线程串行执行，必须轻量——所有 BLE 操作
# （扫描/连接/订阅通知/失联监视）都在 worker 内独立后台线程的 asyncio 事件循环里完成，
# 线程只碰纯 Python 对象、绝不触达 Qt/QML，结果写进模块级 _cache，update() 只瞬读。
# stop() 用于热重载/卸载前停掉线程并断开设备，防止旧模块线程泄漏。
#
# BLE 协议：标准心率服务 0x180D，心率测量特征 0x2A37（notify）。
# 数据格式：首字节 flags（bit0=0 心率 8 位，bit0=1 为 16 位小端），随后是心率值。

import asyncio
import copy
import json
import os
import sqlite3
import threading
import time
from collections import deque

try:
    from xuanhub_host import emit_values as _bridge_emit_values
except ImportError:
    # 允许纯 Python 单测直接导入插件；真实 Worker 总会在 import 前安装该模块。
    def _bridge_emit_values(_patch):
        return 0

PUSH_VALUES = True

try:
    from bleak import BleakScanner, BleakClient
    _BLEAK_OK = True
except Exception:  # bleak 未安装或平台后端缺失：降级，不崩宿主
    _BLEAK_OK = False

HR_SERVICE_UUID = "0000180d-0000-1000-8000-00805f9b34fb"
HR_MEASUREMENT_UUID = "00002a37-0000-1000-8000-00805f9b34fb"

_config = {}
_cache = {
    "state": "idle",        # idle | scanning | connecting | connected | error
    "hr": None,             # 最近一次心率（None=暂无数据）
    "deviceName": "",       # 目标/已连接设备名
    "deviceId": "",         # 目标/已连接设备地址（QML 高亮匹配用）
    "devices": [],          # 扫描结果 [{id, name, rssi}, ...]
    "errorCode": None,       # 稳定错误码；固定文案由 QML 语言包渲染
    "errorDetail": None,
    "lastSeen": None,       # 最近一次心率时间戳（供失联判定）
    "history": [],          # 曲线数据 [[ts, hr], ...]（仅弹窗打开时填充）
    "historyWindow": 5,     # 当前曲线时长（分钟），QML 打开时同步选中态
    "historyMax": None,     # 当前窗口最大值（bpm）
    "historyMin": None,     # 当前窗口最小值（bpm）
    "historyAvg": None,     # 当前窗口平均值（bpm，1 位小数）
    "knownDevices": [],     # 常用设备 [{id, name, lastSeen}, ...]（最新在前）
}
_lock = threading.Lock()
_stop_flag = threading.Event()
_loop_ready = threading.Event()
_loop = None
_thread = None
_client = None
_scan_task = None
_scanning_wanted = None
_connected_at = None
_reconnect_count = 0

# 历史记录：SQLite 持久化（供未来分析）+ 内存环形缓冲（曲线实时数据源）
_HISTORY_MAX = 3600                 # 缓冲上限：60min @ 1 样本/秒
_HISTORY_WINDOWS = (1, 3, 5, 10, 30)  # 可选曲线时长（分钟）
_db_conn = None                     # SQLite 连接，仅后台循环线程使用
_history = deque()                  # [(ts, hr), ...] 按时间升序
_chart_active = False               # 弹窗打开才向 values 输出曲线数据
_history_win = 5                    # 当前曲线窗口（分钟）

# 常用设备记忆（持久化在状态文件，最新在前，上限 _KNOWN_MAX）
_KNOWN_MAX = 8
_known = []                         # [{"address", "name", "lastSeen"}, ...]


# ================= 缓存读写（跨线程，统一持 _lock） =================

def _push_values(patch):
    """只写 bridge 内存队列；异常不能打断 BLE 回调或 asyncio 状态机。"""
    try:
        _bridge_emit_values(patch)
    except Exception:
        pass


def _snapshot_locked(keys):
    """调用方持 _lock；深拷贝避免解锁后容器继续变化。"""
    return {key: copy.deepcopy(_cache[key]) for key in keys}

def _set(**kw):
    with _lock:
        _cache.update(kw)
        patch = _snapshot_locked(kw.keys())
    _push_values(patch)


def _merge_device(entry):
    """扫描结果按 id 去重合并；同名设备保留更优 RSSI 与名字。"""
    with _lock:
        devs = _cache["devices"]
        for d in devs:
            if d["id"] == entry["id"]:
                if entry["name"]:
                    d["name"] = entry["name"]
                d["rssi"] = entry["rssi"]
                patch = _snapshot_locked(("devices",))
                break
        else:
            if len(devs) >= 200:  # 防环境里 BLE 杂音设备过多，列表失控
                return
            devs.append(entry)
            patch = _snapshot_locked(("devices",))
    _push_values(patch)


def _parse_hr(data):
    """解析心率测量值（0x2A37）。flags bit0：0=8 位，1=16 位小端。"""
    if not data:
        return None
    flags = data[0]
    if flags & 0x01:
        if len(data) < 3:
            return None
        return int.from_bytes(data[1:3], "little")
    if len(data) < 2:
        return None
    return data[1]


# ================= 状态持久化（记住上次设备，启动自动重连） =================

def _state_path():
    """状态文件放在 %APPDATA%/XuanHub/（与 xuanhub.log 同目录）。"""
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    folder = os.path.join(base, "XuanHub")
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError:
        pass
    return os.path.join(folder, "heartrate_state.json")


def _save_state(known_devices, last_device):
    """原子写状态文件：常用设备列表 + 最近连接设备。"""
    path = _state_path()
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump({"lastDevice": last_device, "knownDevices": known_devices}, f)
        os.replace(tmp, path)  # 原子替换，避免写一半崩溃留下坏文件
    except OSError:
        pass


def _load_state():
    """读取状态文件；兼容旧版单设备格式（{"address", "name"} → 迁移）。"""
    try:
        with open(_state_path(), "r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            return None
        if "lastDevice" not in data and data.get("address"):
            # 旧格式迁移：单设备 → lastDevice + 空常用列表
            data = {
                "lastDevice": {"address": data["address"], "name": data.get("name", "")},
                "knownDevices": [],
            }
        known = data.get("knownDevices")
        if not isinstance(known, list):
            known = []
        known = [d for d in known if isinstance(d, dict) and d.get("address")]
        return {
            "lastDevice": data.get("lastDevice") if isinstance(data.get("lastDevice"), dict) else None,
            "knownDevices": known,
        }
    except (OSError, ValueError):
        return None


def _remember_device(address, name):
    """连接成功后记入常用设备（去重更新、最新在前、上限 _KNOWN_MAX）并持久化。"""
    global _known
    now = int(time.time())
    _known = [d for d in _known if d["address"] != address]
    _known.insert(0, {"address": address, "name": name, "lastSeen": now})
    del _known[_KNOWN_MAX:]
    _save_state(_known, {"address": address, "name": name})
    with _lock:
        _cache["knownDevices"] = [
            {"id": d["address"], "name": d["name"], "lastSeen": d["lastSeen"]}
            for d in _known
        ]
        patch = _snapshot_locked(("knownDevices",))
    _push_values(patch)


# ================= 历史记录：SQLite 持久化 =================
# 表结构即分析契约：ts 秒级时间戳、hr bpm、device 设备地址（多设备历史可区分）。
# 连接只在后台循环线程创建/使用（sqlite3 默认 check_same_thread=True 顺带强制）。

def _db_path():
    """DB 放 %APPDATA%/XuanHub/（与状态文件同目录）。"""
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    return os.path.join(base, "XuanHub", "heartrate.db")


def _ensure_db():
    """惰性打开连接并建表。DB 不可用时返回 None，不影响插件其余功能。"""
    global _db_conn
    if _db_conn is not None:
        return _db_conn
    conn = None
    try:
        path = _db_path()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        conn = sqlite3.connect(path)
        conn.execute(
            "CREATE TABLE IF NOT EXISTS samples ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " ts INTEGER NOT NULL,"
            " hr INTEGER NOT NULL,"
            " device TEXT NOT NULL DEFAULT '')"
        )
        conn.execute("CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts)")
        conn.commit()
        _db_conn = conn
    except Exception:
        if conn is not None:
            try:
                conn.close()
            except Exception:
                pass
    return _db_conn


def _db_insert(ts, hr, device):
    """写入一条样本（后台循环线程调用）。失败静默：曲线仍走内存缓冲。"""
    try:
        conn = _ensure_db()
        if conn is None:
            return
        conn.execute(
            "INSERT INTO samples(ts, hr, device) VALUES (?,?,?)", (ts, hr, device)
        )
        conn.commit()
    except Exception:
        pass


def _db_query_since(since_ts):
    """查询 since_ts 之后的样本（重启后回灌内存缓冲用）。"""
    try:
        conn = _ensure_db()
        if conn is None:
            return []
        rows = conn.execute(
            "SELECT ts, hr FROM samples WHERE ts >= ? ORDER BY ts", (since_ts,)
        ).fetchall()
        return [(int(r[0]), int(r[1])) for r in rows]
    except Exception:
        return []


def _db_close():
    """关闭连接（后台循环线程内调用）。"""
    global _db_conn
    conn = _db_conn
    _db_conn = None
    if conn is not None:
        try:
            conn.close()
        except Exception:
            pass


# ================= 历史曲线数据（内存缓冲 → values） =================

def _build_series_locked():
    """按当前窗口从缓冲切出 [[ts, hr], ...] 并统计极值/均值（调用方持 _lock）。

    统计与序列同一次遍历完成，无额外开销；窗口内无数据时三项均为 None。
    """
    if not _chart_active or not _history:
        _cache["historyMax"] = _cache["historyMin"] = _cache["historyAvg"] = None
        return []
    cutoff = time.time() - _history_win * 60
    pts = []
    mn = None
    mx = None
    total = 0
    for t, h in _history:
        if t >= cutoff:
            pts.append((t, h))
            if mn is None or h < mn:
                mn = h
            if mx is None or h > mx:
                mx = h
            total += h
    if pts:
        _cache["historyMax"] = mx
        _cache["historyMin"] = mn
        _cache["historyAvg"] = round(total / len(pts), 1)
    else:
        _cache["historyMax"] = _cache["historyMin"] = _cache["historyAvg"] = None
    return [[t, h] for t, h in pts]


async def _hydrate_history():
    """弹窗打开时从 DB 回灌近 60min 样本：重启后曲线立即有历史数据。"""
    rows = _db_query_since(int(time.time()) - _HISTORY_MAX)
    if not rows:
        return
    with _lock:
        if _history:
            last_ts = _history[-1][0]
            # 只补缓冲里没有的更早样本（按时间戳去重，防与实时数据重复）
            rows = [r for r in rows if r[0] > last_ts]
        for r in rows:
            _history.append(r)
        while len(_history) > _HISTORY_MAX:
            _history.popleft()
        _cache["history"] = _build_series_locked()
        patch = _snapshot_locked(
            ("history", "historyMax", "historyMin", "historyAvg"))
    _push_values(patch)


async def _set_chart_active(active):
    """弹窗可见性开关：仅打开时向 values 输出曲线数据，避免每秒序列化大数组。"""
    global _chart_active
    with _lock:
        _chart_active = bool(active)
        _cache["history"] = _build_series_locked() if _chart_active else []
        patch = _snapshot_locked(
            ("history", "historyMax", "historyMin", "historyAvg"))
    _push_values(patch)
    if _chart_active:
        await _hydrate_history()


async def _set_window(minutes):
    """切换曲线时长窗口（1/3/5/10/30 分钟）。"""
    global _history_win
    try:
        m = int(minutes)
    except (TypeError, ValueError):
        return
    if m not in _HISTORY_WINDOWS:
        return
    with _lock:
        _history_win = m
        _cache["historyWindow"] = m
        _cache["history"] = _build_series_locked()
        patch = _snapshot_locked(
            ("historyWindow", "history", "historyMax", "historyMin", "historyAvg"))
    _push_values(patch)


# ================= 后台线程：BLE 事件循环 =================

def _worker():
    """后台线程主体：独立 asyncio 循环，全部 BLE 协程在此运行。"""
    global _loop
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    _loop = loop
    _loop_ready.set()
    try:
        loop.run_until_complete(_amain())
    finally:
        try:
            loop.run_until_complete(loop.shutdown_asyncgens())
        except Exception:
            pass
        loop.close()
        _loop = None


def _schedule(coro):
    """把协程投递到后台事件循环（从插件执行线程调用，线程安全）。"""
    loop = _loop
    if loop is None:
        return
    try:
        asyncio.run_coroutine_threadsafe(coro, loop)
    except RuntimeError:
        pass  # 循环已关闭（stop 竞态），丢弃即可


async def _amain():
    """事件循环主体：轮询停止标志 + 后台失联监视。"""
    monitor = asyncio.ensure_future(_monitor())
    while not _stop_flag.is_set():
        await asyncio.sleep(0.1)
    monitor.cancel()
    try:
        await monitor
    except asyncio.CancelledError:
        pass
    await _cleanup()


async def _cleanup():
    """退出前：停扫描、取消在途连接、断开客户端。

    在途连接任务必须显式 cancel：winrt 后端解析字符串地址时会内部启动
    BleakScanner 的 asyncgen，若不取消而直接 shutdown_asyncgens，
    会对运行中的生成器跨协程 aclose（"already running" 告警，实测踩过）。
    """
    await _do_stop_scan()
    me = asyncio.current_task()
    pending = [t for t in asyncio.all_tasks() if t is not me and not t.done()]
    for t in pending:
        t.cancel()
    if pending:
        await asyncio.gather(*pending, return_exceptions=True)
    await _disconnect_client()
    _db_close()


# ================= 扫描 =================

async def _do_scan(clear_error=True):
    """开始持续扫描（已在扫则跳过）。已连接/连接中不再搜索。"""
    global _scan_task, _scanning_wanted
    if not _BLEAK_OK:
        _set(state="error", errorCode="bleakMissing", errorDetail=None)
        return
    with _lock:
        if _cache["state"] in ("connected", "connecting"):
            return  # 连接成功后扫描保持停止，不再占用蓝牙无线电
    if _scan_task is not None and not _scan_task.done():
        return
    if _scanning_wanted is None:
        _scanning_wanted = asyncio.Event()
    _scanning_wanted.set()  # 新建的 Event 默认未置位，必须 set 扫描循环才会继续
    if clear_error:
        _set(state="scanning", errorCode=None, errorDetail=None, devices=[])
    else:
        # 连接失败后自动续扫：保留错误信息让用户看到失败原因
        _set(state="scanning", devices=[])
    _scan_task = asyncio.ensure_future(_scan_loop())


async def _scan_loop():
    scanner = None
    try:
        scanner = BleakScanner(detection_callback=_on_detect)
        await scanner.start()
        while _scanning_wanted.is_set() and not _stop_flag.is_set():
            await asyncio.sleep(0.5)
    except Exception as e:
        _set(state="error", errorCode="scanFailed", errorDetail=str(e))
    finally:
        if scanner is not None:
            try:
                await scanner.stop()
            except Exception:
                pass
        with _lock:
            # 只有「仍在扫描」才回落 idle：连接中途停止扫描不能覆盖 connecting
            if _cache["state"] == "scanning":
                _cache["state"] = "idle"
                patch = _snapshot_locked(("state",))
            else:
                patch = None
        if patch:
            _push_values(patch)


def _on_detect(device, adv):
    if _stop_flag.is_set():
        return
    rssi = None
    try:
        rssi = adv.rssi
    except Exception:
        pass
    _merge_device({
        "id": device.address,
        "name": (device.name or "").strip(),
        "rssi": rssi,
    })


async def _do_stop_scan():
    global _scan_task
    if _scanning_wanted is not None:
        _scanning_wanted.clear()
    if _scan_task is not None and not _scan_task.done():
        try:
            await _scan_task
        except Exception:
            pass
    _scan_task = None
    with _lock:
        if _cache["state"] == "scanning":
            _cache["state"] = "idle"
            patch = _snapshot_locked(("state",))
        else:
            patch = None
    if patch:
        _push_values(patch)


# ================= 连接 / 断开 =================

async def _do_connect(address, name):
    """连接设备并订阅心率通知。失败回落 error，由弹窗展示原因。"""
    global _client, _connected_at, _reconnect_count
    await _do_stop_scan()
    _set(state="connecting", deviceName=name or address, deviceId=address,
         errorCode=None, errorDetail=None)
    client = None
    try:
        client = BleakClient(address, timeout=10.0)
        await client.connect()
        if _stop_flag.is_set():
            await client.disconnect()
            return
        await client.start_notify(HR_MEASUREMENT_UUID, _hr_callback)
        _client = client
        _connected_at = time.time()
        _reconnect_count = 0
        dname = client.name or name or address
        _set(state="connected", deviceName=dname, hr=None, lastSeen=None)
        _remember_device(address, dname)
    except asyncio.CancelledError:
        # stop()/热重载取消：尽力断开半截连接，让 winrt 内部生成器随任务取消收尾
        if client is not None:
            try:
                if client.is_connected:
                    await client.disconnect()
            except Exception:
                pass
        raise
    except Exception as e:
        if client is not None:
            try:
                if client.is_connected:
                    await client.disconnect()
            except Exception:
                pass
        _set(state="error", errorCode="connectFailed", errorDetail=str(e))
        if _chart_active:
            await _do_scan(clear_error=False)  # 弹窗内失败后自动恢复扫描供重选


async def _disconnect_client():
    global _client
    client = _client
    _client = None
    if client is not None and client.is_connected:
        try:
            await client.disconnect()
        except Exception:
            pass


async def _do_disconnect():
    """手动断开：回 idle 并重置自动重连计数。弹窗内断开后自动恢复扫描。"""
    global _reconnect_count
    await _disconnect_client()
    _set(state="idle", hr=None, deviceName="", deviceId="",
         errorCode=None, errorDetail=None)
    _reconnect_count = 0
    if _chart_active:
        await _do_scan()


def _hr_callback(_sender, data):
    """心率通知回调（后台循环线程执行，只碰 Python 对象 + 锁内写缓存）。"""
    hr = _parse_hr(bytearray(data))
    if hr is None:
        return
    now = int(time.time())
    with _lock:
        device = _cache["deviceId"]
        _cache["hr"] = hr
        _cache["lastSeen"] = now
        _cache["state"] = "connected"
        _history.append((now, hr))
        if len(_history) > _HISTORY_MAX:
            _history.popleft()
        if _chart_active:
            _cache["history"] = _build_series_locked()
            keys = ("hr", "lastSeen", "state", "history",
                    "historyMax", "historyMin", "historyAvg")
        else:
            keys = ("hr", "lastSeen", "state")
        patch = _snapshot_locked(keys)
    _push_values(patch)
    _db_insert(now, hr, device)  # 持久化（DB 失败静默，实时显示不受影响）


async def _monitor():
    """失联监视：已连接但长时间收不到通知 → 断开回 idle，按配置自动重连（最多 3 次）。"""
    global _reconnect_count
    while not _stop_flag.is_set():
        await asyncio.sleep(3)
        with _lock:
            st = _cache["state"]
            ls = _cache["lastSeen"]
        if st != "connected":
            continue
        try:
            stale = int(_config.get("staleSeconds", 10))
        except (TypeError, ValueError):
            stale = 10
        stale = max(5, min(300, stale))
        ref = ls if ls is not None else _connected_at
        if ref is None or time.time() - ref <= stale:
            continue
        # 失联：断开并回到 idle
        await _disconnect_client()
        _set(state="idle", hr=None, deviceName="", deviceId="")
        if _config.get("autoReconnect", True) and _reconnect_count < 3:
            _reconnect_count += 1
            saved = _load_state()
            last = saved["lastDevice"] if saved else None
            if last:
                _set(state="connecting", deviceName=last.get("name", ""),
                     errorCode=None, errorDetail=None)
                try:
                    await asyncio.sleep(5)  # 退避，避免反复失败烧蓝牙栈
                except asyncio.CancelledError:
                    return
                await _do_connect(last["address"], last.get("name", ""))
                continue  # 重连结果下一轮评估（失败路径已自行恢复扫描）
        if _chart_active:
            await _do_scan()  # 自动重连不可用/耗尽：弹窗内恢复扫描


# ================= 模块级约定 =================

def init(config):
    """注入 config.json 中本子插件的 config 段，并启动后台 BLE 线程。"""
    global _config, _thread, _history_win, _known
    _config = dict(config or {})
    try:
        _history_win = int(_config.get("historyWindow", 5))
    except (TypeError, ValueError):
        _history_win = 5
    if _history_win not in _HISTORY_WINDOWS:
        _history_win = 5
    saved = _load_state()
    _known = saved["knownDevices"] if saved else []
    _set(historyWindow=_history_win,
         knownDevices=[{"id": d["address"], "name": d["name"], "lastSeen": d["lastSeen"]}
                       for d in _known])
    if not _BLEAK_OK:
        _set(state="error", errorCode="bleakMissing", errorDetail=None)
        return
    _stop_flag.clear()
    _thread = threading.Thread(target=_worker, daemon=True, name="heartrate-ble")
    _thread.start()
    _loop_ready.wait(timeout=5)  # 等循环就绪，随后投递自动重连
    last = saved["lastDevice"] if saved else None
    if _config.get("autoReconnect", True) and last:
        _set(state="connecting", deviceName=last.get("name", ""))
        _schedule(_do_connect(last["address"], last.get("name", "")))


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    with _lock:
        return dict(_cache)


def invoke(name, args):
    """QML 侧 plugin.call(name, args) 的入口。"""
    args = args or []
    if name == "startScan":
        _schedule(_do_scan())
    elif name == "stopScan":
        _schedule(_do_stop_scan())
    elif name == "connect":
        if args and isinstance(args[0], str):
            _schedule(_do_connect(args[0], args[1] if len(args) > 1 and isinstance(args[1], str) else ""))
    elif name == "disconnect":
        _schedule(_do_disconnect())
    elif name == "setChartActive":
        _schedule(_set_chart_active(bool(args[0]) if args else False))
    elif name == "setWindow":
        _schedule(_set_window(args[0] if args else 5))


def stop():
    """卸载/热重载前发出 BLE 停止信号；不等待设备断开。"""
    global _thread, _loop
    _stop_flag.set()
    # asyncio 主循环会在 100ms 内观察到标志并执行 _cleanup；旧模块继续持有
    # 自己的 globals，因此不与新实例共享状态。
    _thread = None
    _loop = None
