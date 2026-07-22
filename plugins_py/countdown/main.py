# main.py — py_countdown 子插件：下班 / 事件倒计时
#
# 两种目标模式（settings 表单里选 mode）：
#   - 每日重复：每天到达 targetTime（如 18:00）的时刻归零，显示「到点文字」；
#     次日 0 点后 target 自然重新落在本日未来，自动恢复倒计时。
#   - 指定日期：倒数到 targetDateTime（一次性），到点后永久显示「到点文字」。
#
# 纯 datetime 差值计算，update() 极轻量，无后台线程、无需 stop()。
# 时间格式解析失败不抛异常——返回提示文字，让用户在栏上直接看到问题。
from datetime import datetime

_config = {}

# 「指定日期」模式接受的输入格式（带秒优先，缺秒容错）
_DATE_FORMATS = ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M")


def _try_parse_time(text):
    """解析 'HH:MM' 或 'HH:MM:SS' → (h, m, s)；非法返回 None。"""
    s = str(text or "").strip()
    if not s:
        return None
    parts = s.split(":")
    if len(parts) < 2 or len(parts) > 3:
        return None
    try:
        nums = [int(p) for p in parts]
    except ValueError:
        return None
    h, m = nums[0], nums[1]
    sec = nums[2] if len(nums) > 2 else 0
    if not (0 <= h <= 23 and 0 <= m <= 59 and 0 <= sec <= 59):
        return None
    return h, m, sec


def _parse_datetime(text):
    """解析 'YYYY-MM-DD HH:MM[:SS]' → datetime；失败返回 None。"""
    s = str(text or "").strip()
    if not s:
        return None
    for fmt in _DATE_FORMATS:
        try:
            return datetime.strptime(s, fmt)
        except ValueError:
            continue
    return None


def _fmt_duration(seconds):
    """秒数 → 显示串：< 24h 显示 HH:MM:SS；≥ 24h 显示 'X天 HH:MM:SS'。"""
    total = int(max(0, seconds))
    days, rem = divmod(total, 86400)
    hours, rem = divmod(rem, 3600)
    mins, secs = divmod(rem, 60)
    if days > 0:
        return "%d天 %02d:%02d:%02d" % (days, hours, mins, secs)
    return "%02d:%02d:%02d" % (hours, mins, secs)


# ========== 模块约定 ==========

def init(config):
    """注入 config.json 中本插件的 config 段（dict）。"""
    global _config
    _config = dict(config or {})


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    cfg = _config
    now = datetime.now()
    reached_color = str(cfg.get("reachedColor", ""))

    # mode 宽松匹配：非明确「指定日期」一律走每日（与 settings 的中文 enum 对齐，
    # 同时兼容手填英文 daily/once 的写法——只要不是「指定日期」就走每日）
    daily = str(cfg.get("mode", "每日重复")) != "指定日期"

    if daily:
        parsed = _try_parse_time(cfg.get("targetTime", ""))
        if parsed is None:
            return {"text": "⚠ 目标时间格式错误", "reached": False, "reachedColor": reached_color}
        h, m, s = parsed
        target = now.replace(hour=h, minute=m, second=s, microsecond=0)
    else:
        target = _parse_datetime(cfg.get("targetDateTime", ""))
        if target is None:
            return {"text": "⚠ 目标时间格式错误", "reached": False, "reachedColor": reached_color}

    reached = now >= target

    if reached:
        text = str(cfg.get("reachedText", "下班啦🎉"))
    else:
        prefix = str(cfg.get("prefix", ""))
        suffix = str(cfg.get("suffix", ""))
        body = _fmt_duration((target - now).total_seconds())
        # 自动拼接：空的前后缀不留多余空格
        text = " ".join(p for p in (prefix, body, suffix) if p)

    return {
        "text": text,
        "reached": reached,
        "reachedColor": reached_color,
    }
