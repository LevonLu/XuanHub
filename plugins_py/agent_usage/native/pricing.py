# pricing.py — 模型价格匹配、长上下文及快速模式费用计算
from __future__ import annotations

from datetime import datetime, timedelta, timezone
from functools import lru_cache
import json
import os
from pathlib import Path
import re
import time

from .common import COMPONENTS, number, obj, read_json, timestamp

PRICE_FIELDS = {"input": "input_cost_per_token", "output": "output_cost_per_token",
                "cache_read": "cache_read_input_token_cost", "cache_write": "cache_creation_input_token_cost"}
PRICE_URL = "https://raw.githubusercontent.com/BerriAI/litellm/main/model_prices_and_context_window.json"
SUBSCRIPTION = {"pi-github-copilot", "pi-copilot", "prime-agent-github-copilot", "prime-agent-copilot"}
REASONING_INCLUDED = {"codex", "acode", "every-code", "omo", "cline", "muse"}


def normalize_claude(model):
    value = re.sub(r"[^a-z0-9.]+", "-", re.sub(r"\([^)]*\)", " ", model.rsplit("/", 1)[-1]).lower()).strip("-")
    value = re.sub(r"^(sonnet|opus|haiku)-(\d+[.-]\d+)", r"claude-\1-\2", value)
    value = re.sub(r"^(claude-(?:sonnet|opus|haiku)-\d+)\.(\d+)", r"\1-\2", value)
    return re.sub(r"^claude-([4-9]|\d{2,})[.-](\d+)-(sonnet|opus|haiku)", r"claude-\3-\1-\2", value)


def normalize(model, source):
    if source in {"claude", "pi-anthropic", "prime-agent-anthropic"}:
        return normalize_claude(model)
    if source == "workbuddy" and model.strip().lower() == "auto":
        return "hy3-preview-agent"
    if source == "acode" and model.lower() == "xsparkx2agent":
        return "xsparkx2"
    if source == "unsloth" and model.lower().startswith(("local/", "unpriced/")):
        return "__unpriced_local_model__"
    if source in {"antigravity", "zed", "cursor"}:
        value = re.sub(r"\([^)]*\)", " ", model).lower()
        if source == "antigravity":
            value = re.sub(r"\b(thinking|xhigh|high|medium|low|fast)\b", " ", value)
        value = re.sub(r"[^a-z0-9./]+", "-", value).strip("-")
        if source == "cursor":
            grok = re.match(r"(?:cursor-)?grok-4[.-]([567])(?:-|$)", value)
            if grok:
                return "cursor-grok-4." + grok[1] + ("-fast" if "fast" in value.split("-") else "")
            value = "-".join(v for v in value.split("-") if v not in {"thinking", "xhigh", "high", "medium", "low", "minimal"})
        if source == "antigravity":
            if value.startswith(("gemini-claude-", "gemini-gpt-")):
                value = value[7:]
            for family in ("flash-lite", "flash", "pro"):
                if re.match(r"gemini-3\.\d+-" + family, value):
                    return "gemini-2.5-" + family
            if value.startswith("gpt-oss-120b"):
                return "antigravity-gpt-oss-120b"
        return normalize_claude(value) if value.startswith("claude-") else value
    return model


class Pricing:
    def __init__(self, cache_path=None, fetch=None):
        root = Path(__file__).parent / "prices"
        self.curated = read_json(root / "overrides.json", {})
        raw = read_json(cache_path, {}) if cache_path else {}
        if fetch and cache_path and (not raw or time.time() - Path(cache_path).stat().st_mtime > 86400):
            try:
                fresh = fetch(PRICE_URL)
                slim = {name: {key: value[key] for key in PRICE_FIELDS.values() if isinstance(value.get(key), (int, float))}
                        for name, value in fresh.items() if not name.startswith("_") and isinstance(value, dict)}
                slim = {key: value for key, value in slim.items() if value}
                if slim:
                    cache_path = Path(cache_path)
                    cache_path.parent.mkdir(parents=True, exist_ok=True)
                    temporary = cache_path.with_suffix(".tmp")
                    temporary.write_text(json.dumps(slim), encoding="utf-8")
                    os.replace(temporary, cache_path)
                    raw = slim
            except (OSError, ValueError, RuntimeError):
                pass
        raw = raw or read_json(root / "seed.json", {})
        self.rates = {name: {target: round(number(value[field]) * 1_000_000, 10)
                           for target, field in PRICE_FIELDS.items() if field in value}
                      for name, value in raw.items() if not name.startswith("_") and isinstance(value, dict)}
        self.rates = {key: value for key, value in self.rates.items() if value}
        self.sorted_keys = sorted(self.rates, key=lambda x: -len(x))

    @lru_cache(maxsize=4096)
    def lookup(self, model, source):
        if source == "lmstudio" or source in SUBSCRIPTION:
            return {}
        if not model:
            return None
        model = normalize(model, source)
        lower = model.lower()
        dotted = re.sub(r"(\d+)-(\d+)", r"\1.\2", lower)
        exact = self.curated.get("exact", {})
        if (source == "workbuddy-ai" and lower.strip() == "auto") or (source == "acode" and (lower == "auto" or lower.endswith("-auto"))):
            return None
        if source == "cline" and lower.endswith(":free"):
            return {}

        def insensitive(table, key):
            return next((value for name, value in table.items() if name.lower() == key.lower()), None)

        value = insensitive(self.curated.get("source_exact", {}).get(source, {}), model)
        if value is not None:
            return value
        for table in (exact, self.rates):
            if model in table:
                return table[model]
            if dotted != lower:
                value = insensitive(table, dotted)
                if value is not None:
                    return value
            if table is exact:
                for name, suffix in (("qwen3.8-flash", r"(?:-\d{4}-\d{2}-\d{2})?"),
                                     ("qwen3.8-max", r"(?:-0902|-\d{4}-\d{2}-\d{2})?")):
                    if re.search(r"(?:^|/)" + re.escape(name).replace(r"\.", "[.-]") + suffix + "$", lower) and name in exact:
                        return exact[name]
        if lower.endswith("-free"):
            return {}
        if re.search(r"(?:^|/)(?:kimicoding(?:/k2\.8)?|kimi-k2\.8|k2\.8)(?:-|\.|$)", lower):
            return None
        if dotted != lower:
            for name in sorted(exact, key=lambda x: -len(x)):
                position = dotted.find(name.lower())
                if position < 0:
                    continue
                suffix = dotted[position + len(name):]
                if not re.match(r"[.\d]", suffix) and not re.search(r"(?:^|-)(preview|next|exp|experimental|beta|alpha)(?:-|$)", suffix):
                    return exact[name]
        alias = self.curated.get("alias", {}).get(model)
        if alias in exact:
            return exact[alias]
        for rule in self.curated.get("fuzzy", []):
            if rule.get("ref") in exact and rule.get("match") and (rule["match"].lower() in lower or rule["match"].lower() in dotted):
                return exact[rule["ref"]]
        stripped = re.sub(r"-(?:(?:xhigh|high|medium|low)-fast|xhigh|high|medium|low|fast)$", "", model)
        if stripped != model and stripped in self.rates:
            return self.rates[stripped]
        matches = sorted(key for key in self.rates if key.lower().endswith("/" + lower))
        if matches:
            return self.rates[matches[0]]
        return next((self.rates[key] for key in self.sorted_keys if key.lower() in lower), None)

    def cost(self, row):
        source, model = row.get("source", ""), row.get("model", "")
        if source == "lmstudio" or source in SUBSCRIPTION:
            return 0, False
        if source in {"grok", "cline"} and number(row.get("total_cost_usd")) > 0:
            return number(row["total_cost_usd"]), False
        rates = self.lookup(model, source)
        if rates is None:
            return 0, number(row.get("total_tokens")) > 0
        rates = dict(rates)
        if source != "acode" and any(key in model.lower() for key in ("deepseek-v4-flash", "deepseek-v4.1-flash", "deepseek-flash", "deepseek-v4-pro")):
            stamp = timestamp(row.get("hour_start"))
            moment = datetime.fromtimestamp(stamp / 1000, timezone.utc) if stamp else None
            off_peak = row.get("pricing_tier") == "off_peak"
            if moment and row.get("pricing_tier") not in {"peak", "off_peak"}:
                off_peak = (not (1 <= moment.hour < 4 or 6 <= moment.hour < 10) or
                            (stamp >= 1787414400000 and (moment + timedelta(hours=8)).weekday() >= 5))
            if off_peak:
                rates = {key: number(value) * .5 if key in PRICE_FIELDS else value for key, value in rates.items()}
        prices = [number(rates.get(k)) for k in ("input", "output", "cache_read", "cache_write", "output")]
        if source in REASONING_INCLUDED:
            prices[-1] = 0
        base = [number(row.get(k)) for k in COMPONENTS]
        unpriced_input = min(base[0], number(row.get("unpriced_input_tokens")))
        base[0] -= unpriced_input
        long = [min(base[i], number(row.get("long_context_" + k))) for i, k in enumerate(COMPONENTS)]
        has_long = model == "gpt-5.6" or any(k in model.lower() for k in ("gpt-5.6-sol", "gpt-6-astra", "gpt-6-sol", "gpt-6.1-sol", "gpt-6.1-terra"))
        long_rates = [1, .5, 1, 1, .5] if has_long else [0] * 5
        cost = sum(p * (b + l * factor) for p, b, l, factor in zip(prices, base, long, long_rates))
        for tier in ("priority", "ultrafast"):
            multiplier = number(rates.get(tier + "_multiplier"))
            if multiplier <= 1:
                continue
            usage = [min(base[i], number(row.get(tier + "_" + k))) for i, k in enumerate(COMPONENTS)]
            overlap = [min(usage[i], long[i], number(row.get(tier + "_long_context_" + k))) for i, k in enumerate(COMPONENTS)]
            cost += (multiplier - 1) * sum(p * (b + l * factor) for p, b, l, factor in zip(prices, usage, overlap, long_rates))
        return cost / 1_000_000, unpriced_input > 0
