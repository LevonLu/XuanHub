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
_locale = "zh-CN"

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


# ========== 模块约定 ==========

def init(config):
    """注入 config.json 中本插件的 config 段（dict）。"""
    global _config
    _config = dict(config or {})


def set_locale(locale):
    """可选运行时回调：只影响缺省文案，不改写用户已经保存的文字。"""
    global _locale
    _locale = str(locale or "en-US")


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    cfg = _config
    now = datetime.now()
    reached_color = str(cfg.get("reachedColor", ""))

    # 新配置持久化稳定 id；继续识别旧版中文值和曾支持的 once 写法。
    mode = str(cfg.get("mode", "daily"))
    daily = mode not in ("date", "once", "指定日期")

    if daily:
        raw = str(cfg.get("targetTime", "")).strip()
        if not raw:
            raw = "18:00"                         # 空配置兜底：默认 18:00
        parsed = _try_parse_time(raw)
        if parsed is None:
            return {"textKey": "invalidDailyTime", "reached": False,
                    "reachedColor": reached_color}
        h, m, s = parsed
        target = now.replace(hour=h, minute=m, second=s, microsecond=0)
    else:
        raw = str(cfg.get("targetDateTime", "")).strip()
        target = _parse_datetime(raw)
        if target is None:
            return {"textKey": "invalidDateTime", "reached": False,
                    "reachedColor": reached_color}

    reached = now >= target

    if reached:
        text = str(cfg.get("reachedText",
                           "下班啦🎉" if _locale == "zh-CN" else "Time's up 🎉"))
        return {
            "text": text,
            "reached": True,
            "reachedColor": reached_color,
        }
    # 前缀/后缀：去首尾空白，纯空白串视为空（与 settings 中留空语义一致）
    prefix = str(cfg.get("prefix", "")).strip()
    suffix = str(cfg.get("suffix", "")).strip()
    return {
        "remainingSeconds": int(max(0, (target - now).total_seconds())),
        "prefix": prefix,
        "suffix": suffix,
        "reached": False,
        "reachedColor": reached_color,
    }
