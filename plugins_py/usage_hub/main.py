# main.py — py_usage_hub 子插件：多厂商用量中心
#
# 支持两种查询模式：
#   - balance: 余额查询（如 DeepSeek），显示今日用量 + 当前余额
#   - plan:    限额查询（如 GLM Coding Plan），显示 5h/每周窗口进度条
#
# 模块级四约定：update() -> dict 必需；init(config)、invoke(name,args)、stop() 可选。
# update/invoke 在宿主 GUI 线程内执行（持 GIL），必须轻量——网络 IO 全在后台 daemon
# 线程里完成，线程只碰纯 Python 对象、绝不触达 Qt/QML，结果写进模块级 _cache，
# update() 只瞬读缓存。stop() 用来在热重载/卸载前停掉旧线程防泄漏。
import json
import os
import threading
import time

from vendors import VendorBase
from vendors.deepseek import DeepseekVendor
from vendors.glm import GLMVendor

# ================= 模块级状态 =================
_vendors: list[VendorBase] = []
_cache: dict = {"summary": "初始化中..."}
_lock = threading.Lock()
_stop_flag = False
_refresh_event = threading.Event()

# 厂商类注册表：user_config.json 中的 mode 字段 → Vendor 子类
_VENDOR_REGISTRY = {
    "balance": DeepseekVendor,
    "plan": GLMVendor,
}

# ================= 配置加载 =================


def _plugin_dir():
    return os.path.dirname(os.path.abspath(__file__))


def _user_config_path():
    return os.path.join(_plugin_dir(), "user_config.json")


def _state_path():
    """状态文件放在 %APPDATA%/XuanHub/（与 xuanhub.log 同目录）。"""
    base = os.environ.get("APPDATA") or os.path.expanduser("~")
    folder = os.path.join(base, "XuanHub")
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError:
        pass
    return os.path.join(folder, "usage_hub_state.json")


def _load_user_config():
    """读取 user_config.json；不存在则返回空。"""
    path = _user_config_path()
    if not os.path.exists(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return {}
    return data if isinstance(data, dict) else {}


def _load_state():
    """从持久化文件恢复各厂商状态。"""
    try:
        with open(_state_path(), "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError):
        return
    if not isinstance(data, dict):
        return
    for v in _vendors:
        if v.id in data:
            v.load_state(data[v.id])


def _save_state():
    """将各厂商状态落盘。"""
    snap = {}
    for v in _vendors:
        state = v.save_state()
        if state:
            snap[v.id] = state
    if not snap:
        return
    try:
        with open(_state_path(), "w", encoding="utf-8") as f:
            json.dump(snap, f, ensure_ascii=False)
    except OSError:
        pass


# ================= 缓存构建 =================


def _rebuild_cache():
    """根据各厂商当前状态重建 _cache 扁平字典（调用方持 _lock）。"""
    cache: dict = {}
    parts = []
    vendor_ids = []

    for v in _vendors:
        if not v.enabled:
            continue

        vendor_ids.append(v.id)
        prefix = v.id + "_"
        cache[prefix + "status"] = v.status
        cache[prefix + "error"] = v.error_message
        cache[prefix + "last_update"] = v.last_update_str
        cache[prefix + "mode"] = v.mode
        cache[prefix + "name"] = v.name

        if v.status == "ok":
            # 合并厂商 query() 返回的展示数据
            for k, val in v.data.items():
                cache[prefix + k] = val

            # 生成摘要片段（含字符进度条）
            if v.mode == "balance":
                today = v.data.get("today", "...")
                balance = v.data.get("balance", "...")
                parts.append(f"[{v.name}] {today}/{balance}")
            elif v.mode == "plan":
                pct_5h = v.data.get("plan_5h_pct", 0)
                pct_w = v.data.get("plan_weekly_pct", 0)
                bar_5h = _make_mini_bar(pct_5h)
                bar_w = _make_mini_bar(pct_w)
                parts.append(f"[{v.name}] 5h:{bar_5h}{pct_5h}% W:{bar_w}{pct_w}%")
        elif v.status == "error":
            parts.append(f"[{v.name}] ERR")
        else:
            parts.append(f"[{v.name}] ...")

    if parts:
        cache["summary"] = " | ".join(parts)
    else:
        # 区分两种空态：完全没配厂商 → 引导用户去配置；配了但全部禁用 → 如实说明
        cache["summary"] = "无已启用厂商" if _vendors else "未配置密钥"
    cache["vendor_ids"] = vendor_ids
    return cache


def _make_mini_bar(pct: int, width: int = 5) -> str:
    """生成迷你字符进度条，如 '███░░' 表示 60%。"""
    pct = max(0, min(100, pct))
    filled = round(pct / 100 * width)
    return "█" * filled + "░" * (width - filled)


# ================= 查询循环 =================


def _worker():
    """后台轮询：遍历厂商，独立判断 should_query() 后执行查询。"""
    global _cache
    while not _stop_flag:
        for v in _vendors:
            if _stop_flag:
                break
            if not v.enabled:
                continue
            if not v.should_query():
                continue

            try:
                data = v.query()
            except Exception as exc:
                v.status = "error"
                v.error_message = str(exc)
                v.data = {}
            else:
                v.data = data

            with _lock:
                _cache = _rebuild_cache()
            _save_state()

        if _stop_flag:
            break

        # 计算下一个最近的查询时间
        now = time.time()
        delays = []
        for v in _vendors:
            if v.enabled:
                elapsed = now - v._last_query_ts
                remaining = max(0, v.interval_sec - elapsed)
                delays.append(remaining)
        wait = min(delays) if delays else 60
        _refresh_event.wait(timeout=max(1, wait))
        _refresh_event.clear()


# ================= 模块级约定 =================


def init(config):
    """注入 config.json 中本子插件的 config 段。

    config 包含：
      - intervalMs: QTimer 刷新间隔（宿主侧使用，本模块不直接使用）
      - vendors: 厂商配置数组
    """
    global _vendors, _cache

    vendors_cfg = config.get("vendors", []) if isinstance(config, dict) else []

    # 如果 config.json 中未配置厂商，尝试从 user_config.json 读取
    if not vendors_cfg:
        uc = _load_user_config()
        vendors_cfg = uc.get("vendors", [])

    _vendors = []
    for vc in vendors_cfg:
        if not isinstance(vc, dict):
            continue
        mode = vc.get("mode", "balance")
        cls = _VENDOR_REGISTRY.get(mode)
        if cls is None:
            continue
        vendor = cls(vc)
        _vendors.append(vendor)

    # 恢复持久化状态
    _load_state()

    # 恢复可用的旧数据：如果上次查询距今不足 intervalSec，直接复用持久化数据
    for v in _vendors:
        if not v.enabled:
            continue
        if v.should_query():
            continue  # 需要重新查询

        # 尝试从持久化状态恢复展示数据
        if isinstance(v, DeepseekVendor) and v.current_balance is not None:
            v.data = {
                "today": "%.2f" % abs(v.today_usage),
                "balance": "%.2f" % v.current_balance,
            }
            v.status = "ok"
        elif isinstance(v, GLMVendor) and v.data:
            v.status = "ok"

    # 构建初始缓存
    with _lock:
        _cache = _rebuild_cache()

    # 启动后台线程
    global _stop_flag
    _stop_flag = False
    threading.Thread(target=_worker, daemon=True).start()


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    with _lock:
        return dict(_cache)


def invoke(name, args):
    """QML 侧 plugin.call(name, args) 的入口。

    支持命令：
      - "refresh": 强制刷新所有厂商
      - "refresh_vendor": 刷新指定厂商（args[0] = vendor_id）
    """
    if name == "refresh":
        for v in _vendors:
            v.force_refresh()
        _refresh_event.set()
    elif name == "refresh_vendor" and args:
        vid = str(args[0])
        for v in _vendors:
            if v.id == vid:
                v.force_refresh()
                _refresh_event.set()
                break


def stop():
    """卸载/热重载前由宿主调用，停掉后台线程。"""
    global _stop_flag
    _stop_flag = True
    _refresh_event.set()
