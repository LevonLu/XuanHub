# vendors/glm.py — GLM Coding Plan 限额查询厂商
"""GLM Plan 模式厂商。

查询 {base_domain}/api/monitor/usage/quota/limit，显示 5 小时窗口和每周窗口
的 token 用量进度条。

认证信息（来自 config.json 的 vendors[] 条目）：
  - apiKey: ANTHROPIC_AUTH_TOKEN
  - apiUrl: ANTHROPIC_BASE_URL（如 https://open.bigmodel.cn）
"""

import json
import time
import urllib.error
import urllib.request
from datetime import datetime

from vendors import VendorBase


class GLMVendor(VendorBase):
    """GLM Coding Plan 限额查询厂商。

    配置项（来自 config.json 的 vendors[] 条目）：
      - apiKey: 认证令牌（ANTHROPIC_AUTH_TOKEN）
      - apiUrl: 基础 URL（如 https://open.bigmodel.cn）
      - intervalSec: 查询间隔秒数（默认 600）
    """

    def __init__(self, config: dict):
        super().__init__(config)
        self.mode = "plan"

        self.auth_token: str = str(config.get("apiKey", "")).strip()
        self.base_url: str = str(config.get("apiUrl", "")).strip()

    # ---- 查询 ----

    def query(self) -> dict:
        """查询 GLM Coding Plan 限额 API。

        返回:
            {
                "plan_5h_pct": float,        # 5h 窗口使用百分比
                "plan_5h_detail": str,       # "已用 / 总量"
                "plan_5h_reset": str,        # 刷新时间
                "plan_weekly_pct": float,    # 每周窗口使用百分比
                "plan_weekly_detail": str,
                "plan_weekly_reset": str,
            }
        """
        self._last_query_ts = time.time()

        if not self.auth_token or not self.base_url:
            self.status = "error"
            self.error_message = "认证信息未配置"
            return self._empty_result()

        # 解析 base_url 获取 base_domain
        from urllib.parse import urlparse
        parsed = urlparse(self.base_url)
        base_domain = f"{parsed.scheme}://{parsed.netloc}"

        quota_url = f"{base_domain}/api/monitor/usage/quota/limit"

        data, err = self._fetch_json(quota_url)
        if err:
            self.status = "error"
            self.error_message = err
            return self._empty_result()

        self.status = "ok"
        self.error_message = ""
        self.last_update_ts = time.time()

        result = {}
        tokens_count = 0
        for item in data.get("limits", []):
            if item.get("type") != "TOKENS_LIMIT":
                continue
            tokens_count += 1
            unit = item.get("unit")
            number = item.get("number")
            pct = item.get("percentage", 0)

            # 用量详情
            current_val = item.get("currentValue", "-")
            usage_val = item.get("usage", "-")
            detail = f"{self._fmt_num(current_val)} / {self._fmt_num(usage_val)}"

            # 刷新时间
            reset_ts = item.get("nextResetTime")
            reset_str = self._fmt_reset(reset_ts)

            if unit == 3 and number == 5:
                prefix = "plan_5h"
            elif unit == 6 and number == 1:
                prefix = "plan_weekly"
            else:
                continue

            result[prefix + "_pct"] = pct
            result[prefix + "_detail"] = detail
            result[prefix + "_reset"] = reset_str

        return result if result else self._empty_result()

    def _fetch_json(self, url: str) -> tuple:
        """HTTP GET 并解析 JSON。返回 (data_dict, error_str)。"""
        req = urllib.request.Request(
            url,
            headers={
                "Authorization": self.auth_token,
                "Accept-Language": "en-US,en",
                "Content-Type": "application/json",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout_sec) as resp:
                body = resp.read().decode("utf-8")
                data = json.loads(body)
                return (data.get("data", data), None)
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "replace")
            return ({}, f"HTTP {e.code}: {body[:200]}")
        except Exception as e:
            return ({}, "网络错误: " + str(e))

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
            "plan_5h_pct": 0,
            "plan_5h_detail": "-",
            "plan_5h_reset": "-",
            "plan_weekly_pct": 0,
            "plan_weekly_detail": "-",
            "plan_weekly_reset": "-",
        }

    @staticmethod
    def _fmt_num(val) -> str:
        """格式化数字（支持 int/float/str 输入）。"""
        if val is None or val == "-":
            return "-"
        try:
            n = int(val)
            return f"{n:,}"
        except (TypeError, ValueError):
            return str(val)

    @staticmethod
    def _fmt_reset(ts) -> str:
        """格式化刷新时间戳为可读字符串。"""
        if not ts:
            return "-"
        try:
            dt = datetime.fromtimestamp(ts / 1000)
            return dt.strftime("%m-%d %H:%M")
        except (OSError, ValueError, OverflowError):
            return "-"
