# vendors/__init__.py — VendorBase 抽象基类 + 厂商注册
"""厂商抽象层：定义所有厂商必须实现的接口和公共逻辑。

每个厂商子类封装其 API 查询逻辑、状态管理和持久化钩子。
main.py 通过 VendorBase 接口统一编排所有厂商的查询与缓存更新。
"""

from abc import ABC, abstractmethod
import time
from datetime import datetime


class VendorBase(ABC):
    """所有厂商的抽象基类。

    子类必须实现：
      - query() -> dict: 执行 API 查询，返回厂商专属展示数据

    子类可选覆盖：
      - load_state(state): 从持久化文件恢复状态
      - save_state() -> dict: 返回需要持久化的状态
    """

    def __init__(self, config: dict):
        # 身份标识
        self.id: str = str(config.get("id", ""))
        self.name: str = str(config.get("name", self.id))
        self.mode: str = str(config.get("mode", "balance"))
        self.enabled: bool = bool(config.get("enabled", True))

        # 查询节流
        self.interval_sec: int = max(10, int(config.get("intervalSec", 300)))
        self.timeout_sec: int = max(1, int(config.get("timeoutSec", 10)))

        # 运行时状态（由调用方在锁内访问）
        self.status: str = "pending"  # "pending" | "ok" | "error"
        self.error_message: str = ""
        self.last_update_ts: float = 0.0

        # 内部节流计时（非持久化）
        self._last_query_ts: float = 0.0

    # ---- 查询控制 ----

    def should_query(self) -> bool:
        """当前时间距上次查询是否已超过 interval_sec。"""
        return (time.time() - self._last_query_ts) >= self.interval_sec

    def force_refresh(self) -> None:
        """重置计时器，下一次轮询立即查询。"""
        self._last_query_ts = 0.0

    # ---- 查询接口 ----

    @abstractmethod
    def query(self) -> dict:
        """执行 API 查询（在后台线程中调用）。

        子类负责：
          1. HTTP 请求（urllib）
          2. 设置 self.status / self.error_message / self.last_update_ts
          3. 返回供 QML 展示的数据 dict（只含纯 Python 对象）

        返回的 dict 将被扁平化到 _cache 中，key 格式为 <vendor_id>_<field>。
        """
        ...

    # ---- 展示工具 ----

    @property
    def last_update_str(self) -> str:
        """人类可读的上次更新时间字符串。"""
        if self.last_update_ts <= 0:
            return "从未"
        dt = datetime.fromtimestamp(self.last_update_ts)
        if dt.date() == datetime.now().date():
            return dt.strftime("%H:%M:%S")
        return dt.strftime("%m-%d %H:%M")

    # ---- 状态持久化钩子 ----

    def load_state(self, state: dict) -> None:
        """从持久化文件恢复状态（init 时调用）。"""
        self._last_query_ts = float(state.get("last_query_ts", 0))

    def save_state(self) -> dict:
        """返回需要持久化的状态字典。"""
        return {"last_query_ts": self._last_query_ts}
