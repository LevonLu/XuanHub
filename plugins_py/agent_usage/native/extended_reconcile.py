# extended_reconcile.py — 多种存储同时存在时按上游事件身份抑制镜像记录
from __future__ import annotations

from collections import Counter, defaultdict


def reconcile(events):
    """对当前完整事件集去重；保留同一来源的两次真实请求，不按总量猜测。"""
    unique = {}
    for item in events:
        if item:
            unique[item["event_id"]] = item
    values = list(unique.values())
    copilot = defaultdict(list)
    buddy = Counter()
    migrated_kiro = set()

    def copilot_key(match):
        return match.get("session"), match.get("model"), tuple(match.get("tokens", []))

    for item in values:
        lane = item.get("copilot_lane")
        match = item.get("copilot_match", {})
        if lane == "store" and match.get("session") and match.get("time", 0) > 0:
            copilot[copilot_key(match)].append(match["time"])
        if item.get("codebuddy_lane") == "jsonl" and item.get("codebuddy_match"):
            buddy[item["codebuddy_match"]] += 1
        if item.get("kiro_lane") == "db":
            migrated_kiro.update(mid for mid in item.get("kiro_message_ids", []) if isinstance(mid, str) and mid)
    for candidates in copilot.values():
        candidates.sort()

    result = []
    for item in values:
        if item.get("kiro_lane") == "session" and any(mid in migrated_kiro for mid in item.get("kiro_message_ids", [])):
            continue
        if item.get("codebuddy_lane") == "log":
            match = item.get("codebuddy_match")
            if buddy[match] > 0:
                buddy[match] -= 1
                continue
        if item.get("copilot_lane") == "otel-cli":
            match = item.get("copilot_match", {})
            candidates = copilot[copilot_key(match)]
            if candidates and match.get("time", 0) > 0:
                closest = min(range(len(candidates)), key=lambda i: abs(candidates[i] - match["time"]))
                # 上游容差为 2000 ms，只消耗一次最接近的精确六列 Token 匹配。
                if abs(candidates[closest] - match["time"]) <= 2000:
                    candidates.pop(closest)
                    continue
        result.append(item)
    return result
