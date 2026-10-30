# model.py — 汇总 Agent 列表、统计周期与套餐窗口，区分未知值与零用量
from datetime import datetime, timedelta
import re

NAMES = {
    "acode": "AStudio", "antigravity": "Antigravity", "anythingllm": "AnythingLLM",
    "claude": "Claude Code", "claude-science": "Claude Science", "cline": "Cline",
    "codebuddy": "CodeBuddy", "codex": "Codex", "command-code": "Command Code",
    "copilot": "GitHub Copilot", "craft": "Craft Agents", "cursor": "Cursor",
    "devin": "Devin CLI", "droid": "Droid", "dumate": "DuMate", "dsh": "DeepSeek Harness",
    "every-code": "Every Code", "gemini": "Gemini CLI", "goose": "Goose", "grok": "Grok Build",
    "hermes": "Hermes Agent", "kilo-cli": "Kilo CLI", "kilo-code": "Kilo Code", "kiro": "Kiro",
    "kimi": "Kimi", "kimi-code": "Kimi Code", "lmstudio": "LM Studio", "mimo": "Mimo Code",
    "minimax-code": "MiniMax Code", "muse": "Muse Code", "omo": "OmO", "omp": "oh-my-pi",
    "opencode": "OpenCode", "openclaw": "OpenClaw", "opensquilla": "OpenSquilla",
    "pi": "pi", "dots": "Dots", "prime-agent": "Prime Agent", "qoder": "Qoder",
    "qoder-cn": "Qoder CN", "reasonix": "Reasonix", "roocode": "Roo Code", "trae-cn": "TRAE Work CN",
    "trae": "TRAE", "unsloth": "Unsloth Studio", "workbuddy": "WorkBuddy",
    "workbuddy-ai": "WorkBuddy AI", "zcode": "ZCode", "zed": "Zed Agent",
    "opencode-go": "OpenCode Go", "coding-plan": "Ark Coding Plan", "agent-plan": "Ark Agent Plan",
}
QUOTA_IDS = {"commandCode": "command-code", "opencodeGo": "opencode-go", "qoderCn": "qoder-cn",
             "codingPlan": "coding-plan", "agentPlan": "agent-plan"}
QUOTA_SOURCES = {"claude", "codex", "cursor", "kimi", "gemini", "kiro", "antigravity", "copilot",
                 "grok", "zcode", "opencode-go", "command-code", "devin", "qoder", "qoder-cn",
                 "coding-plan", "agent-plan", "workbuddy"}
# 原生日志将 Kimi Code 归并到 Kimi；设置中只提供一个对应开关。
POPUP_AGENT_KEYS = tuple(key for key in NAMES if key != "kimi-code") + ("other",)


def popup_agent_visible(source_id, config, stats=None):
    mode = config.get("popupAgentMode", "all")
    if mode == "all":
        return True
    if mode == "active":
        # stats 已按当前统计周期汇总；历史来源或套餐额度不能代表本周期 Token 消耗。
        return ((stats or {}).get("total_tokens") or 0) > 0
    # Pi/Prime Agent 按后端动态生成来源，设置按工具统一控制这些子来源。
    key = {"kilocode": "kilo-code", "kimi-code": "kimi"}.get(source_id, source_id)
    for family in ("pi", "prime-agent"):
        if key.startswith(family + "-"):
            key = family
            break
    if key not in NAMES:
        key = "other"
    return config.get("popupAgent_" + key, False) is True


def period_bounds(period, now=None):
    now = now or datetime.now()
    midnight = now.replace(hour=0, minute=0, second=0, microsecond=0)
    start = None if period == "all" else midnight - timedelta(days={"today": 0, "7d": 6, "30d": 29}.get(period, 0))
    return {"fromMs": int(start.timestamp() * 1000) if start else 0,
            "toMs": int((midnight + timedelta(days=1)).timestamp() * 1000)}


def detail_query(source_id, from_date, to_date, request_id):
    if not isinstance(source_id, str) or not source_id or len(source_id) > 128:
        raise ValueError("invalid_source")
    if not isinstance(request_id, str) or not request_id or len(request_id) > 100:
        raise ValueError("invalid_request")
    dates = []
    try:
        for value in (from_date, to_date):
            if not isinstance(value, str) or not re.fullmatch(r"\d{4}-\d{2}-\d{2}", value):
                raise ValueError("invalid_date")
            dates.append(datetime.strptime(value, "%Y-%m-%d"))
        if dates[0] > dates[1]:
            raise ValueError("invalid_range")
        # 截止日期包含全天，以次日零点为右开边界，兼容本地夏令时变化。
        return {"sourceId": source_id, "fromDate": from_date, "toDate": to_date, "requestId": request_id,
                "fromMs": int(dates[0].timestamp() * 1000),
                "toMs": int((dates[1] + timedelta(days=1)).timestamp() * 1000)}
    except (OverflowError, OSError):
        raise ValueError("invalid_date") from None


def default_detail_dates(period, now=None):
    today = (now or datetime.now()).date()
    days = {"today": 0, "7d": 6, "30d": 29, "all": 29}.get(period, 0)
    return {"from": (today - timedelta(days=days)).isoformat(), "to": today.isoformat()}


def compose(usage, quota, config):
    sources = {s["id"]: dict(s) for s in usage.get("sources", [])}
    providers = {QUOTA_IDS.get(k, k): v for k, v in quota.get("providers", {}).items()}
    keys = set(sources) | set(usage.get("knownSources", [])) | {k for k, v in providers.items() if v.get("configured") or v.get("status") == "reauth"}
    if config.get("showAll"):
        keys |= set(POPUP_AGENT_KEYS) - {"other"}
    agents = []
    hidden = []
    for key in keys:
        stats, limits = sources.get(key), providers.get(key)
        if not popup_agent_visible(key, config, stats):
            hidden.append(key)
            continue
        agents.append({"id": key, "name": NAMES.get(key, key), "stats": stats,
                       "sourceError": usage.get("sourceErrors", {}).get(key, ""),
                       "quota": limits or {"status": "pending" if key in QUOTA_SOURCES else "unsupported", "windows": [], "configured": False},
                       "scope": (stats or {}).get("scope", "account" if key in {"cursor", "trae-cn"} else "local")})
    agents.sort(key=lambda a: (-((a["stats"] or {}).get("total_tokens", 0)), a["name"].casefold()))
    selected = [s for s in sources.values() if config.get("includeAccount") or s.get("scope") != "account"]
    # 列表选择仅控制展示，隐藏工具仍继续采集并计入原有总量口径。
    popup_mode = config.get("popupAgentMode", "all")
    return {"agents": agents, "barPrefix": config.get("barPrefix", ""),
            "popupAgentMode": popup_mode, "popupFiltered": popup_mode != "all",
            "hiddenAgentIds": sorted(hidden), "totalTokens": sum(s.get("total_tokens", 0) for s in selected),
            "totalCost": sum(s.get("cost", 0) for s in selected),
            "unpriced": any(s.get("unpriced") for s in selected), "period": config.get("period", "today"),
            "includeAccount": config.get("includeAccount", False), "agentCount": len(sources),
            "malformed": usage.get("malformed", 0), "sourceErrorCount": len(usage.get("sourceErrors", {}))}
