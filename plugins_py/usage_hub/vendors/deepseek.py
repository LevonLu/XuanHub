# vendors/deepseek.py — DeepSeek 余额查询厂商
"""DeepSeek 余额模式厂商。

查询 https://api.deepseek.com/user/balance，显示今日用量 + 当前余额。
支持每日起始时间设置，跨重启状态持久化。
"""

import json
import time
import urllib.error
import urllib.request
from datetime import datetime

from vendors import VendorBase


class DeepseekVendor(VendorBase):
    """DeepSeek 余额模式厂商。

    配置项（来自 config.json 的 vendors[] 条目）：
      - apiKey: DeepSeek API Key (sk-...)
      - apiUrl: 余额 API 地址（默认 https://api.deepseek.com/user/balance）
      - startHour / startMin: 每日起始时间（默认 8:30）
      - intervalSec: 查询间隔秒数（默认 300）
    """

    def __init__(self, config: dict):
        super().__init__(config)
        self.mode = "balance"

        self.api_key: str = str(config.get("apiKey", "")).strip()
        self.api_url: str = str(config.get("apiUrl", "https://api.deepseek.com/user/balance")).strip()
        self.start_hour: int = int(config.get("startHour", 8))
        self.start_min: int = int(config.get("startMin", 30))

        # 余额状态（持久化）
        self.start_balance: float | None = None
        self.current_balance: float | None = None
        self.today_usage: float = 0.0
        self.start_date: str = ""
        self.start_time: str = ""

    # ---- 查询 ----

    def query(self) -> dict:
        """调用 DeepSeek 余额 API。

        返回:
            {"today": str, "balance": str}
        """
        self._last_query_ts = time.time()

        if not self.api_key:
            self.status = "error"
            self.error_code = "apiKeyMissing"
            self.error_detail = ""
            return {"today": "N/A", "balance": "N/A"}

        balance, error_code, error_detail = self._fetch_balance()
        if error_code:
            self.status = "error"
            self.error_code = error_code
            self.error_detail = error_detail
            # 保留旧数据
            return {
                "today": "%.2f" % abs(self.today_usage) if self.current_balance is not None else "ERR",
                "balance": "%.2f" % self.current_balance if self.current_balance is not None else "ERR",
            }

        self.status = "ok"
        self.error_code = ""
        self.error_detail = ""
        self.last_update_ts = time.time()

        # 判断是否需要记录今日起始余额
        if self._need_new_start():
            self.start_balance = balance
            self.start_date = self._today_str()
            self.start_time = self._now_hhmm()
            self.today_usage = 0.0
        elif self.start_balance is not None:
            self.today_usage = self.start_balance - balance

        self.current_balance = balance
        return {
            "today": "%.2f" % abs(self.today_usage),
            "balance": "%.2f" % self.current_balance,
        }

    def _fetch_balance(self):
        """HTTP GET 余额 API。返回 (balance_float, error_str)。"""
        req = urllib.request.Request(
            self.api_url,
            headers={
                "Accept": "application/json",
                "Authorization": "Bearer " + self.api_key,
            },
            method="GET",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout_sec) as r:
                code = r.getcode()
                body = r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            return None, "httpError", str(e.code)
        except Exception as e:
            return None, "networkError", str(e)

        if code != 200:
            return None, "httpError", str(code)
        if not body:
            return None, "emptyResponse", ""

        try:
            obj = json.loads(body)
        except ValueError as e:
            return None, "invalidJson", str(e)

        infos = obj.get("balance_infos")
        if not infos:
            return None, "balanceInfoMissing", ""
        balance_str = infos[0].get("total_balance")
        if balance_str is None:
            return None, "balanceFieldMissing", ""
        try:
            return float(balance_str), "", ""
        except (TypeError, ValueError):
            return None, "invalidBalance", str(balance_str)

    # ---- 起始余额判定 ----

    def _need_new_start(self) -> bool:
        """是否需要记录新的今日起始余额。"""
        if self.start_date != self._today_str():
            return True
        if not self.start_time:
            return True
        return not self._is_past_start()

    @staticmethod
    def _today_str():
        return datetime.now().strftime("%Y-%m-%d")

    @staticmethod
    def _now_hhmm():
        return datetime.now().strftime("%H:%M")

    def _is_past_start(self) -> bool:
        n = datetime.now()
        return n.hour * 60 + n.minute >= self.start_hour * 60 + self.start_min

    # ---- 状态持久化 ----

    def load_state(self, state: dict):
        super().load_state(state)
        self.start_balance = state.get("start_balance")
        self.current_balance = state.get("current_balance")
        self.today_usage = state.get("today_usage", 0.0)
        self.start_date = state.get("start_date", "")
        self.start_time = state.get("start_time", "")

    def save_state(self) -> dict:
        s = super().save_state()
        s.update({
            "start_balance": self.start_balance,
            "current_balance": self.current_balance,
            "today_usage": self.today_usage,
            "start_date": self.start_date,
            "start_time": self.start_time,
        })
        return s
