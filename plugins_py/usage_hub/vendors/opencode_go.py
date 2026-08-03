# vendors/opencode_go.py — OpenCode Go 套餐用量查询厂商
"""OpenCode Go 模式厂商。

请求 https://opencode.ai/workspace/{workspace}/go 页面（SSR 内嵌数据），
解析滚动（rolling）/每周（weekly）/每月（monthly）三个窗口的用量百分比，
以及订阅状态与 Zen 余额兜底开关；美元金额按官方额度 ($12/$30/$60) 推算，
仅供参考。逻辑移植自 go_usage.py，适配 VendorBase 契约。

认证信息（来自 config.json 的 vendors[] 条目）：
  - apiKey: cookie（浏览器登录 opencode.ai 后复制的完整 Cookie 头）
  - workspace: workspace ID（留空用默认值）
  - apiUrl: 站点基础 URL（默认 https://opencode.ai）
  - intervalSec: 查询间隔秒数（默认 600）
"""

import re
import time
import urllib.error
import urllib.request

from vendors import VendorBase

# 官方文档公布的 Go 套餐限额（美元）
_LIMITS = {
    "rolling": 12.0,   # 5 小时窗口
    "weekly": 30.0,    # 每周窗口
    "monthly": 60.0,   # 每月窗口
}

# 默认 workspace（go_usage.py 中官方示例 ID）
_DEFAULT_WORKSPACE = "wrk_01KZ1FCPM03BZ5TS2W57KACSBD"

# 页面 URL 模板：SSR 内嵌数据
_PAGE_URL = "https://opencode.ai/workspace/{workspace}/go"

# 内嵌数据模式，如: rollingUsage:$R[33]={status:"ok",resetInSec:4211,usagePercent:0}
_USAGE_RE = re.compile(
    r'(\w+)Usage:\$R\[\d+\]=\{status:"(\w+)",resetInSec:(\d+),usagePercent:(\d+)\}'
)
_USE_BALANCE_RE = re.compile(r"useBalance:(\!\d|\!\w)")
_SUBSCRIBED_RE = re.compile(r"您已订阅 OpenCode Go|You're subscribed to OpenCode Go")


class OpenCodeGoVendor(VendorBase):
    """OpenCode Go 套餐用量查询厂商。

    配置项（来自 config.json 的 vendors[] 条目）：
      - apiKey: cookie
      - workspace: workspace ID（留空用默认）
      - apiUrl: 站点基础 URL
      - intervalSec: 查询间隔秒数（默认 600）
    """

    def __init__(self, config: dict):
        super().__init__(config)
        self.mode = "go"

        self.cookie: str = str(config.get("apiKey", "")).strip()
        self.workspace: str = str(config.get("workspace", "")).strip()
        self.base_url: str = str(config.get("apiUrl", "https://opencode.ai")).strip()

    # ---- 查询 ----

    def query(self) -> dict:
        """请求 opencode.ai 用量页并解析三个窗口。

        返回:
            {
                "go_rolling_pct": int,  "go_rolling_detail": str,  "go_rolling_reset": str,
                "go_weekly_pct": int,   "go_weekly_detail": str,   "go_weekly_reset": str,
                "go_monthly_pct": int,  "go_monthly_detail": str,  "go_monthly_reset": str,
                "go_subscribed": bool,  "go_use_balance": bool,
            }
        """
        self._last_query_ts = time.time()

        if not self.cookie:
            self.status = "error"
            self.error_message = "Cookie 未配置"
            return self._empty_result()

        html_text, err = self._fetch_page()
        if err:
            self.status = "error"
            self.error_message = err
            return self._empty_result()

        usage, use_balance, subscribed, parse_err = self._parse(html_text)
        if parse_err:
            self.status = "error"
            self.error_message = parse_err
            return self._empty_result()

        self.status = "ok"
        self.error_message = ""
        self.last_update_ts = time.time()

        result = {
            "go_subscribed": subscribed,
            "go_use_balance": use_balance,
        }
        for name, u in usage.items():
            prefix = "go_" + name
            result[prefix + "_pct"] = u["usagePercent"]
            if u["status"] == "ok":
                result[prefix + "_reset"] = self._fmt_countdown(u["resetInSec"])
            else:
                result[prefix + "_reset"] = u["status"]
            limit = _LIMITS.get(name)
            if limit:
                used = limit * u["usagePercent"] / 100
                result[prefix + "_detail"] = "$%.2f / $%.2f" % (used, limit)

        # 个别窗口缺失时给占位（页面结构变化或权限不足时保 UI 稳定）
        for name in ("rolling", "weekly", "monthly"):
            prefix = "go_" + name
            result.setdefault(prefix + "_pct", 0)
            result.setdefault(prefix + "_reset", "-")
            result.setdefault(prefix + "_detail", "-")
        return result

    def _fetch_page(self):
        """请求用量页，带 3 次重试（服务器偶尔提前断开连接）。"""
        req = urllib.request.Request(
            self._page_url(),
            headers={
                "Cookie": self.cookie,
                "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) go-usage-script",
                "Accept": "text/html,application/xhtml+xml",
                "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
                "Accept-Encoding": "identity",
            },
        )
        last_err = ""
        for _ in range(3):
            try:
                with urllib.request.urlopen(req, timeout=self.timeout_sec) as resp:
                    return resp.read().decode("utf-8", "replace"), None
            except urllib.error.HTTPError as e:
                if e.code in (401, 403):
                    return "", "HTTP %d —— cookie 可能已失效，请重新从浏览器复制" % e.code
                last_err = "HTTP %d" % e.code
            except Exception as e:
                last_err = "网络错误: " + str(e)
        return "", last_err or "请求失败"

    def _page_url(self):
        ws = self.workspace or _DEFAULT_WORKSPACE
        base = self.base_url.rstrip("/")
        return "%s/workspace/%s/go" % (base, ws)

    def _parse(self, html_text):
        """从 HTML 提取用量数据。返回 (usage, use_balance, subscribed, err)。"""
        usage = {}
        for m in _USAGE_RE.finditer(html_text):
            name, status, reset_sec, percent = m.groups()
            usage[name] = {
                "status": status,
                "resetInSec": int(reset_sec),
                "usagePercent": int(percent),
            }
        if not usage:
            return {}, False, False, "未解析到用量数据（页面结构可能已变化，或 cookie 无权限访问该 workspace）"

        use_balance = False
        m = _USE_BALANCE_RE.search(html_text)
        if m:
            # JS 序列化: !1 = false, !0 = true
            use_balance = m.group(1) == "!0"

        subscribed = bool(_SUBSCRIBED_RE.search(html_text))
        return usage, use_balance, subscribed, None

    # ---- 状态持久化 ----

    def load_state(self, state: dict) -> None:
        super().load_state(state)
        self.data = state.get("data", {})

    def save_state(self) -> dict:
        s = super().save_state()
        s["data"] = self.data
        return s

    @staticmethod
    def _empty_result() -> dict:
        return {
            "go_rolling_pct": 0, "go_rolling_detail": "-", "go_rolling_reset": "-",
            "go_weekly_pct": 0, "go_weekly_detail": "-", "go_weekly_reset": "-",
            "go_monthly_pct": 0, "go_monthly_detail": "-", "go_monthly_reset": "-",
            "go_subscribed": False, "go_use_balance": False,
        }

    @staticmethod
    def _fmt_countdown(seconds: int) -> str:
        """把重置倒计时秒格式化成「X天 X时 X分」（不足 1 分钟显示「即将重置」）。"""
        d, rem = divmod(int(seconds), 86400)
        h, rem = divmod(rem, 3600)
        m = divmod(rem, 60)[0]
        parts = []
        if d:
            parts.append("%d天" % d)
        if h:
            parts.append("%d时" % h)
        if m:
            parts.append("%d分" % m)
        return " ".join(parts) if parts else "即将重置"
