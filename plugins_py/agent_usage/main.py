# main.py — 两条 Python 后台采集通道，响应配置变更、历史查询与退出
from __future__ import annotations
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

from model import compose, period_bounds, detail_query, default_detail_dates, POPUP_AGENT_KEYS, BAR_QUOTA_NAMES, BAR_QUOTA_MODES
from process_job import Job
from configuration import migrate, validate_layout, validate_accounts

CONFIG_VERSION = 2

def migrate_config(old_version, config):
    if old_version != 1:
        raise ValueError("unsupported_config_version")
    return migrate(config)

def validate_config(config):
    try:
        normalize_config(config)
        return {"ok": True, "errors": []}
    except (TypeError, ValueError) as error:
        return {"ok": False, "errors": [str(error)]}


_service = None


def normalize_config(config):
    config = migrate(config)
    period = config.get("period", "today")
    if period not in {"today", "7d", "30d", "all"}:
        raise ValueError("invalid_period")
    popup_mode = config.get("popupAgentMode", "all")
    if popup_mode not in {"all", "active", "custom"}:
        raise ValueError("invalid_popup_agent_mode")
    bar_prefix = config.get("barPrefix", "")
    if not isinstance(bar_prefix, str):
        raise ValueError("invalid_bar_prefix")
    selection = {"popupAgent_" + key: config.get("popupAgent_" + key, False) for key in POPUP_AGENT_KEYS}
    if any(not isinstance(value, bool) for value in selection.values()):
        raise ValueError("invalid_popup_agent_selection")
    bar_quota = {"barQuota_" + key: config.get("barQuota_" + key, "none") for key in BAR_QUOTA_NAMES}
    if any(not isinstance(value, str) or value not in BAR_QUOTA_MODES for value in bar_quota.values()):
        raise ValueError("invalid_bar_quota_mode")
    return {**config, "barColumns": validate_layout(config), "accounts": validate_accounts(config), "period": period, "showAll": bool(config.get("showAll", False)),
            "barPrefix": " ".join(bar_prefix.splitlines()).strip(),
            "popupAgentMode": popup_mode, **selection, **bar_quota,
            "includeAccount": bool(config.get("includeAccount", False)),
            "devinEnabled": bool(config.get("devinEnabled", False)),
            "quotaEnabled": bool(config.get("quotaEnabled", True)),
            "refreshSeconds": max(60, min(3600, int(config.get("refreshSeconds", 300)))),
            "proxyMode": str(config.get("proxyMode", "auto")).strip(),
            "proxyUrl": str(config.get("proxyUrl", "")).strip()}


def resolve_runtime(config):
    # 与 pybridge 使用同一解释器及依赖环境，不再探测或启动 Node。
    if not sys.executable or not Path(sys.executable).is_file():
        raise RuntimeError("python_missing")
    return str(Path(sys.executable).resolve())


class Monitor:
    def __init__(self, config, cache_dir=None):
        self.lock = threading.RLock()
        self.config = normalize_config(config)
        self.generation = 0
        self.stopping = threading.Event()
        self.events = {lane: threading.Event() for lane in ("usage", "quota")}
        self.processes = {}
        self.jobs = {}
        self.payload = {"usage": {}, "quota": {}}
        self.status = {"usage": "loading", "quota": "loading"}
        self.captured = {"usage": "", "quota": ""}
        self.version = ""
        self.force_accounts = ["*"]
        self.detail_event = threading.Event()
        self.detail_revision = 0
        self.detail_request = None
        self.detail = {"status": "idle"}
        self.cache_dir = Path(cache_dir) if cache_dir else Path(os.environ.get("LOCALAPPDATA", str(Path.home()))) / "XuanHub" / "agent-usage"
        self.threads = [threading.Thread(target=self.loop, args=(lane,), daemon=True, name=f"agent-{lane}") for lane in self.events]
        self.threads.append(threading.Thread(target=self.detail_loop, daemon=True, name="agent-detail"))

    def start(self):
        for thread in self.threads:
            thread.start()

    def snapshot(self):
        with self.lock:
            result = compose(self.payload["usage"], self.payload["quota"], self.config)
            result.update(usageStatus=self.status["usage"], quotaStatus=self.status["quota"],
                          usageUpdated=self.captured["usage"], quotaUpdated=self.captured["quota"], engineVersion=self.version,
                          detail=self.detail, defaultRange=default_detail_dates(self.config["period"]))
            result = copy.deepcopy(result)
            if self.status["quota"] not in {"ok", "loading", "disabled"}:
                for account in result["accounts"]:
                    account["stale"] = True
                for col in result["barColumns"]:
                    for slot in ("single", "top", "bottom"):
                        if col[slot]["metric"] not in {"tokens", "empty"}:
                            col[slot]["stale"] = True
                for agent in result["agents"]:
                    if agent["quota"].get("windows"):
                        agent["quota"]["stale"] = True
                for entry in result["barQuotas"]:
                    if any(w["used"] is not None for w in entry["windows"]):
                        entry["stale"] = True
            return result

    def configure(self, config):
        validated = normalize_config(config)
        with self.lock:
            previous = self.config
            self.config = validated
            if previous["accounts"] != validated["accounts"]:
                old_accounts = {a["id"]: a for a in previous["accounts"]}
                identity_keys = ("credential", "providerId", "apiUrl", "authMode", "workspace")
                retained = {a["id"] for a in validated["accounts"] if a.get("enabled", True)
                            and all(a.get(k) == old_accounts.get(a["id"], {}).get(k) for k in identity_keys)}
                self.payload["quota"]["accounts"] = [a for a in self.payload["quota"].get("accounts", []) if a["id"] in retained]
            # 修改展示选项立即重算快照，不中断正在进行的采集或日期查询。
            collection_keys = ("period", "devinEnabled", "quotaEnabled", "refreshSeconds", "proxyMode", "proxyUrl", "accounts")
            if all(previous[k] == validated[k] for k in collection_keys):
                return
            self.generation += 1
            self.detail_revision += 1
            self.detail_request = None
            self.detail = {**self.detail, "status": "cancelled", "data": None}
            # 周期或数据源变更时不能把旧周期的数字套上新标签。
            if any(previous[k] != validated[k] for k in ("period", "proxyMode", "proxyUrl")):
                self.payload["usage"] = {}
                self.captured["usage"] = ""
            if any(previous[k] != validated[k] for k in ("proxyMode", "proxyUrl")) or not validated["quotaEnabled"]:
                self.payload["quota"] = {}
                self.captured["quota"] = ""
            self.cancel_processes()
        self.refresh()

    def refresh(self):
        self.force_accounts = ["*"]
        for event in self.events.values():
            event.set()

    def cancel_processes(self):
        for job in self.jobs.values():
            job.close()
        for process in self.processes.values():
            if process.poll() is None:
                try:
                    process.terminate()
                except OSError:
                    pass

    def run(self, lane, config, generation, query=None):
        python = resolve_runtime(config)
        request = {"cacheDir": str(self.cache_dir), "lane": lane, "proxyMode": config["proxyMode"], "proxyUrl": config["proxyUrl"],
                   "devinEnabled": config["devinEnabled"], "accounts": config["accounts"], "quotaEnabled": config["quotaEnabled"], **period_bounds(config["period"])}
        if lane == "quota":
            with self.lock:
                request["forceAccounts"], self.force_accounts = self.force_accounts, []
        if query:
            request.update(query)
        with self.lock:
            if self.stopping.is_set() or generation != self.generation or (query and query["revision"] != self.detail_revision):
                raise RuntimeError("cancelled")
            job = Job()
            try:
                process = subprocess.Popen([python, "-I", "-B", str(Path(__file__).with_name("collector.py"))],
                                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                try:
                    job.attach(process)
                except OSError:
                    process.kill()
                    process.communicate()
                    raise
            except OSError:
                job.close()
                raise
            self.processes[lane] = process
            self.jobs[lane] = job
        try:
            stdout, _ = process.communicate(json.dumps(request).encode("utf-8"), timeout={"usage": 240, "quota": 50, "detail": 30}[lane])
            if len(stdout) > 2 * 1024 * 1024:
                raise RuntimeError("collector_failed")
            try:
                result = json.loads(stdout)
            except (ValueError, UnicodeError):
                raise RuntimeError("collector_failed") from None
            if not result.get("ok"):
                raise RuntimeError(result.get("error", "collector_failed"))
            return result["data"]
        except subprocess.TimeoutExpired:
            raise RuntimeError("timeout") from None
        finally:
            with self.lock:
                job.close()
            if process.poll() is None:
                process.kill()
            process.communicate()
            with self.lock:
                self.processes.pop(lane, None)
                self.jobs.pop(lane, None)

    def request_detail(self, source_id, from_date, to_date, request_id):
        query = detail_query(source_id, from_date, to_date, request_id)
        with self.lock:
            self.detail_revision += 1
            query["revision"] = self.detail_revision
            self.detail_request = query
            self.detail = {**query, "status": "loading", "data": None, "usageUpdated": self.captured["usage"]}
            # 快速切换工具或日期时，只保留最后一次请求，旧进程结果不能回写。
            if "detail" in self.jobs:
                self.jobs["detail"].close()
            if "detail" in self.processes and self.processes["detail"].poll() is None:
                try:
                    self.processes["detail"].terminate()
                except OSError:
                    pass
            self.detail_event.set()
        return {"queued": True, "requestId": request_id}

    def detail_loop(self):
        while not self.stopping.is_set():
            self.detail_event.wait()
            with self.lock:
                self.detail_event.clear()
                if self.stopping.is_set():
                    break
                if not self.detail_request:
                    continue
                query = dict(self.detail_request)
                config, generation = dict(self.config), self.generation
            try:
                data = self.run("detail", config, generation, query)
                state = {"status": "ok", "data": data}
            except (RuntimeError, OSError, ValueError) as error:
                code = str(error)
                state = {"status": code if code in {"python_missing", "timeout", "data_not_ready", "proxy_failed"} else "collector_failed", "data": None}
            with self.lock:
                if query["revision"] == self.detail_revision and generation == self.generation and not self.stopping.is_set():
                    self.detail = {**query, **state, "usageUpdated": self.captured["usage"]}

    def loop(self, lane):
        event = self.events[lane]
        while not self.stopping.is_set():
            event.clear()
            with self.lock:
                config, generation = dict(self.config), self.generation
                enabled = lane != "quota" or config["quotaEnabled"] or any(a.get("enabled", True) and a["providerId"] == "deepseek" for a in config["accounts"])
                self.status[lane] = "loading" if enabled else "disabled"
            if enabled:
                try:
                    data = self.run(lane, config, generation)
                    with self.lock:
                        if generation == self.generation and not self.stopping.is_set():
                            if lane == "quota":
                                old = self.payload[lane]
                                if data.get("partial"):
                                    data["providers"] = {**old.get("providers", {}), **data.get("providers", {})}
                                if data.get("partial") or data.get("accountError"):
                                    current = {a["id"]: a for a in old.get("accounts", [])}
                                    if data.get("accountError"):
                                        current = {key: {**a, "stale": True} for key, a in current.items()}
                                    current.update({a["id"]: a for a in data.get("accounts", [])})
                                    data["accounts"] = list(current.values())
                            self.payload[lane] = data
                            self.status[lane] = "ok"
                            self.captured[lane] = time.strftime("%Y-%m-%d %H:%M:%S")
                            self.version = data.get("version", "")
                except (RuntimeError, OSError, ValueError) as error:
                    code = str(error)
                    allowed = {"python_missing", "timeout", "collector_failed", "sync_busy", "proxy_failed"}
                    with self.lock:
                        if generation == self.generation:
                            self.status[lane] = code if code in allowed else "collector_failed"
            # 从完成后计时，手动刷新和热更新只唤醒一次，不积压任务。
            event.wait(config["refreshSeconds"] if lane == "usage" else min([max(120, config["refreshSeconds"])] + [a.get("intervalSec", 300) for a in config["accounts"] if a.get("enabled", True)]))

    def stop(self):
        self.stopping.set()
        with self.lock:
            self.cancel_processes()
        self.refresh()
        self.detail_event.set()
        for thread in self.threads:
            if thread.ident is not None:
                thread.join(timeout=2)


def init(config):
    global _service
    stop()
    _service = Monitor(config)
    _service.start()


def set_config(config):
    if _service:
        _service.configure(config)
    else:
        init(config)


def update():
    return _service.snapshot() if _service else {"usageStatus": "loading", "quotaStatus": "loading", "agents": []}


def invoke(name, args):
    if name == "query_detail":
        if not _service or not isinstance(args, (list, tuple)) or len(args) != 4:
            raise ValueError("invalid_request")
        return _service.request_detail(*args)
    if name == "refresh_account" and _service and len(args) == 1:
        with _service.lock:
            _service.force_accounts.append(str(args[0]))
        _service.events["quota"].set()
        return {"queued": True}
    if name != "refresh":
        raise ValueError("unknown_action")
    if _service:
        _service.refresh()
    return {"queued": True}


def set_locale(locale):
    pass


def stop():
    global _service
    if _service:
        _service.stop()
        _service = None
