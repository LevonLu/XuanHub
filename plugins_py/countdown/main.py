# main.py — py_countdown 子插件：下班 / 事件倒计时
#
# 两种目标模式（settings 表单里选 mode）：
#   - 每日重复：每天到达 targetTime（如 18:00）的时刻归零，显示「到点文字」；
#     次日 0 点后 target 自然重新落在本日未来，自动恢复倒计时。
#   - 指定日期：倒数到 targetDateTime（一次性），到点后永久显示「到点文字」。
#
# 纯 datetime 差值计算，update() 极轻量，无后台线程、无需 stop()。
# 时间格式解析失败不抛异常——返回提示文字，让用户在栏上直接看到问题。
import calendar
from datetime import date, datetime, timedelta
from functools import lru_cache
import json
from pathlib import Path
import re

CONFIG_VERSION = 2
_config = {}
_locale = "zh-CN"

# 「指定日期」模式接受的输入格式（带秒优先，缺秒容错）
_DATE_FORMATS = ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M")


def _date_list(text):
    """公司日历覆盖项只接受完整日期，避免把输入错误当作普通工作日。"""
    if not isinstance(text, str) or len(text) > 8192:
        raise ValueError("must be a date list string (at most 8192 characters)")
    values = set()
    for token in re.split(r"[\s,;，；]+", text.strip()):
        if not token:
            continue
        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", token):
            raise ValueError("dates must use YYYY-MM-DD")
        values.add(date.fromisoformat(token))
    return frozenset(values)


@lru_cache(maxsize=1)
def _workday_calendar():
    """本地读取一次，不在每秒刷新路径联网；缺失日历时明确返回未知。"""
    try:
        raw = json.loads(Path(__file__).with_name("calendar_cn.json").read_text(encoding="utf-8"))
        result = {}
        for year, entry in raw["years"].items():
            holidays = set()
            for start, end in entry["holidays"]:
                first, last = date.fromisoformat(start), date.fromisoformat(end)
                holidays.update(first + timedelta(days=i)
                                for i in range((last - first).days + 1))
            result[int(year)] = (holidays, {date.fromisoformat(d) for d in entry["workdays"]})
        return result
    except (OSError, ValueError, KeyError, TypeError):
        return {}


@lru_cache(maxsize=32)
def _next_payday(today, day, extra_holidays, extra_workdays):
    """按实际发薪日选最近一次；发薪当天保留到本地午夜，之后再切下月。"""
    calendars = _workday_calendar()
    for offset in range(14):
        year, month = divmod(today.year * 12 + today.month - 1 + offset, 12)
        month += 1
        candidate = date(year, month, min(day, calendar.monthrange(year, month)[1]))
        if candidate < today:
            continue
        for _ in range(366):
            # 已经回退到今天之前，本次发薪必然已过；不必查询过去年份的日历。
            if candidate < today:
                break
            # 公司覆盖优先于国家日历；补班周末是工作日，不可简单跳过所有周末。
            if candidate in extra_workdays:
                break
            if candidate in extra_holidays:
                candidate -= timedelta(days=1)
                continue
            if candidate.year not in calendars:
                return {"paydayStatus": "calendarUnavailable", "paydayCalendarYear": candidate.year}
            holidays, workdays = calendars[candidate.year]
            if candidate in workdays or (candidate.weekday() < 5 and candidate not in holidays):
                break
            candidate -= timedelta(days=1)
        else:
            return {"paydayStatus": "noWorkday"}
        if candidate >= today:
            return {"paydayStatus": "ready", "paydayDate": candidate.isoformat(),
                    "paydayDays": (candidate - today).days}
    return {"paydayStatus": "noWorkday"}


def _tooltip_values(today):
    template = _config.get("tooltipText", "")
    values = {"tooltipTemplate": template, "paydayStatus": "unused",
              "paydayDate": "", "paydayDays": 0, "paydayCalendarYear": 0}
    if "{payday}" in template or "{paydayDays}" in template:
        values.update(_next_payday(today, _config.get("paydayDay", 10),
                                   _date_list(_config.get("paydayHolidays", "")),
                                   _date_list(_config.get("paydayWorkdays", ""))))
    return values


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

def migrate_config(old_version, config):
    """V1 曾把中文枚举标签直接持久化；V2 只保存稳定值。"""
    if old_version != 1:
        raise ValueError(f"unsupported config version: {old_version}")
    migrated = dict(config or {})
    mode = str(migrated.get("mode", "daily"))
    if mode == "每日重复":
        migrated["mode"] = "daily"
    elif mode in ("指定日期", "once"):
        migrated["mode"] = "date"
    return migrated


def validate_config(config):
    config = config or {}
    mode = str(config.get("mode", "daily"))
    errors = [] if mode in ("daily", "date") else ["mode must be daily or date"]
    text = config.get("tooltipText", "")
    if not isinstance(text, str) or len(text) > 4000:
        errors.append("tooltipText must be a string (at most 4000 characters)")
    day = config.get("paydayDay", 10)
    if type(day) is not int or not 1 <= day <= 31:
        errors.append("paydayDay must be an integer between 1 and 31")
    dates = {}
    for key in ("paydayHolidays", "paydayWorkdays"):
        try:
            dates[key] = _date_list(config.get(key, ""))
        except ValueError as error:
            errors.append(f"{key}: {error}")
    if dates.get("paydayHolidays", frozenset()) & dates.get("paydayWorkdays", frozenset()):
        errors.append("paydayHolidays and paydayWorkdays must not overlap")
    return {"ok": not errors, "errors": errors}

def init(config):
    """注入 config.json 中本插件的 config 段（dict）。"""
    set_config(config)


def set_config(config):
    """先验证再替换，失败保留当前倒计时和悬浮提示。"""
    global _config
    validation = validate_config(config)
    if not validation["ok"]:
        raise ValueError("; ".join(validation["errors"]))
    _config = dict(config or {})


def set_locale(locale):
    """可选运行时回调：只影响缺省文案，不改写用户已经保存的文字。"""
    global _locale
    _locale = str(locale or "en-US")


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    now = datetime.now()
    return {**_countdown_values(now), **_tooltip_values(now.date())}


def _countdown_values(now):
    cfg = _config
    reached_color = str(cfg.get("reachedColor", ""))

    # 配置迁移后只消费稳定 id；旧标签兼容集中在 migrate_config()。
    mode = str(cfg.get("mode", "daily"))
    daily = mode == "daily"

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
