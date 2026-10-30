# discovery.py — 按工具数据契约发现本机与正在运行的 WSL 安装
from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys

from .common import obj, read_json

SOURCES = frozenset("acode antigravity anythingllm claude claude-science cline codebuddy codex command-code copilot craft cursor devin droid dumate dsh every-code gemini goose grok hermes kilo-cli kilocode kiro kimi kimi-code lmstudio mimo minimax-code muse omo omp opencode openclaw opensquilla pi prime-agent qoder qoder-cn reasonix roocode trae-cn trae unsloth workbuddy workbuddy-ai zcode zed".split())


def is_file(path):
    try:
        return Path(path).is_file()
    except OSError:
        return False


def is_dir(path):
    try:
        return Path(path).is_dir()
    except OSError:
        return False


def paths(path, pattern=None):
    # WSL root 可枚举但子目录常无读取权限；一次失败只隔离该目录。
    try:
        yield from (Path(path).glob(pattern) if pattern is not None else Path(path).iterdir())
    except OSError:
        return


def modified(path):
    try:
        return Path(path).stat().st_mtime_ns
    except OSError:
        return -1


def canonical(path):
    return os.path.normcase(str(Path(path).resolve()))


def expand(value, home, env):
    value = str(value).strip()
    value = re.sub(r"%([^%]+)%|\$\{([^}]+)\}|\$([A-Za-z_][A-Za-z_0-9]*)",
                   lambda m: env.get(next(x for x in m.groups() if x), m[0]), value)
    if value == "~":
        return Path(home)
    if value.startswith(("~/", "~\\")):
        return Path(home) / value[2:]
    path = Path(value)
    return path if path.is_absolute() else Path(home) / path


def running_wsl_homes(env=None):
    # --list 只读状态；不执行 wsl -d 或探测已停止发行版的 UNC。
    if sys.platform != "win32":
        return []
    try:
        result = subprocess.run(["wsl.exe", "--list", "--running", "--quiet"],
                                capture_output=True, timeout=4,
                                creationflags=subprocess.CREATE_NO_WINDOW, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return []
    if result.returncode:
        return []
    raw = result.stdout
    names = raw.decode("utf-16-le" if b"\x00" in raw else "utf-8", errors="replace").splitlines()
    homes = []
    for name in names:
        name = name.strip().lstrip("\ufeff")
        if not name or any(c in name for c in "\\/:\x00"):
            continue
        for prefix in ("\\\\wsl.localhost\\", "\\\\wsl$\\"):
            root = Path(prefix + name)
            if not is_dir(root / "home"):
                continue
            homes.extend(p for p in paths(root / "home") if is_dir(p))
            if is_dir(root / "root"):
                homes.append(root / "root")
            break
    return homes


def discover(home, env=None, *, wsl_homes=None):
    """返回 (source, module.function, Path)，仅返回存在的文件，物理路径只归属一次。"""
    home = Path(home)
    env = dict(os.environ if env is None else env)
    env.setdefault("HOME", str(home))
    env.setdefault("USERPROFILE", str(home))
    seen = set()
    mode = env.get("TOKENTRACKER_WSL_MODE", "both").lower()
    wsl_homes = ([] if mode == "native-only" else running_wsl_homes(env)) if wsl_homes is None else list(wsl_homes)
    contexts = []
    if mode != "wsl-only":
        contexts.append((home, env, sys.platform))
    if mode != "native-only":
        for candidate in wsl_homes:
            # 显式路径仍只在原生上下文解释，不能误映射到其他发行版。
            linux_env = {"HOME": str(candidate), "USERPROFILE": str(candidate)}
            contexts.append((Path(candidate), linux_env, "linux"))

    def emit(source, parser, path):
        path = Path(path)
        try:
            key = canonical(path)
            if is_file(path) and key not in seen:
                seen.add(key)
                yield source, parser, path
        except OSError:
            return

    def scan(source, parser, root, pattern):
        try:
            for path in sorted(paths(root, pattern)):
                yield from emit(source, parser, path)
        except OSError:
            return

    for h, e, platform in contexts:
        def root(key, fallback):
            return expand(e.get(key) or fallback, h, e)
        config = root("XDG_CONFIG_HOME", ".config")
        data = root("XDG_DATA_HOME", ".local/share")
        app = root("APPDATA", "AppData/Roaming") if platform == "win32" else (h / "Library/Application Support" if platform == "darwin" else config)
        local = root("LOCALAPPDATA", "AppData/Local") if platform == "win32" else data
        tracker = obj(read_json(h / ".tokentracker/tracker/config.json", {}))
        scan_roots = obj(tracker.get("scanRoots"))
        for source, variable, fallback in (("codex", "CODEX_HOME", ".codex"), ("acode", "TOKENTRACKER_ACODE_HOME", ".acode"), ("every-code", "CODE_HOME", ".code")):
            roots = [root(variable, fallback)]
            if source == "codex":
                roots += [expand(p, h, e) for p in scan_roots.get("codex", []) if isinstance(p, str)]
            for base in roots:
                for dirname in ("sessions", "archived_sessions"):
                    for pattern in ("**/rollout-*.jsonl", "**/rollout-*.jsonl.zst", "**/rollout-*.jsonl.zstd"):
                        yield from scan(source, "transcripts.codex", base / dirname, pattern)
        claude_roots = [h / ".claude", root("CLAUDE_CONFIG_DIR", ".claude")]
        claude_roots += [expand(p, h, e) for p in scan_roots.get("claude", []) if isinstance(p, str)]
        for base in claude_roots:
            yield from scan("claude", "transcripts.claude", base / "projects", "**/*.jsonl")
        gemini = root("GEMINI_HOME", ".gemini")
        yield from scan("gemini", "transcripts.gemini", gemini / "tmp", "*/chats/session-*.json")
        for variant in ("antigravity", "antigravity-ide", "antigravity-cli"):
            yield from scan("antigravity", "extended.antigravity", gemini / variant / "brain", "*/.system_generated/logs/transcript.jsonl")
        opencode = root("OPENCODE_HOME", data / "opencode")
        yield from emit("opencode", "databases.opencode", opencode / "opencode.db")
        yield from scan("opencode", "transcripts.opencode", opencode / "storage/message", "**/*.json")
        kilo = root("KILO_HOME", data / "kilo")
        yield from emit("kilo-cli", "databases.opencode", (app / "kilo" if platform == "win32" and not e.get("KILO_HOME") else kilo) / "kilo.db")
        mimo = root("MIMO_HOME", data / "mimocode") / "mimocode.db"
        if not is_file(mimo) and not e.get("MIMO_HOME") and platform == "win32":
            mimo = app / "mimocode/mimocode.db"
        yield from emit("mimo", "databases.opencode", mimo)
        zcode = root("ZCODE_HOME", ".zcode") / "cli/db/db.sqlite"
        if not is_file(zcode) and not e.get("ZCODE_HOME") and platform == "win32":
            zcode = app / ".zcode/cli/db/db.sqlite"
        yield from emit("zcode", "extended.zcode", zcode)
        dumate = root("TOKENTRACKER_DUMATE_HOME", app / "qianfan-desktop-app/qianfan_desk_xdg")
        for pattern in ("opencode.db", "data/opencode/opencode.db", "*/data/opencode/opencode.db"):
            yield from scan("dumate", "databases.opencode", dumate, pattern)
        for source, prefix, appname, dotdir in (("qoder", "QODER", "Qoder", ".qoder"), ("qoder-cn", "QODER_CN", "QoderCN", ".qoder-cn")):
            base = root(prefix + "_HOME", app / appname)
            yield from emit(source, "databases.qoder", root(prefix + "_DB_PATH", base / "SharedClientCache/cache/db/local.db"))
            yield from scan(source, "formats.qoder", root(prefix + "_PROJECTS_DIR", h / dotdir / "projects"), "**/*.jsonl")
        science = root("CLAUDE_SCIENCE_HOME", ".claude-science")
        if e.get("CLAUDE_SCIENCE_DB_PATH"):
            yield from emit("claude-science", "databases.claude_science", root("CLAUDE_SCIENCE_DB_PATH", ""))
        else:
            for pattern in ("operon-cli.db", "operon.db", "orgs/*/operon-cli.db", "orgs/*/operon.db"):
                yield from scan("claude-science", "databases.claude_science", science, pattern)
        kiro = app / "Kiro/User/globalStorage/kiro.kiroagent/dev_data"
        if is_file(kiro / "devdata.sqlite"):
            yield from emit("kiro", "extended.kiro_ide", kiro / "devdata.sqlite")
        else:
            yield from emit("kiro", "extended.kiro_ide", kiro / "tokens_generated.jsonl")
        kiro_cli = root("KIRO_CLI_DB_PATH", (local if platform == "win32" else data) / "kiro-cli/data.sqlite3")
        yield from emit("kiro", "extended.kiro_cli_db", kiro_cli)
        kiro_sessions = root("KIRO_HOME", ".kiro") / "sessions"
        for item in sorted(paths(kiro_sessions / "cli", "*.json")):
            if re.fullmatch(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\.json", item.name, re.I):
                yield from emit("kiro", "extended.kiro_cli", item)
        yield from scan("kiro", "extended.kiro_cli", kiro_sessions, "*/sess_*/messages.jsonl")
        hermes = root("TOKENTRACKER_HERMES_HOME", local / "hermes" if platform == "win32" else h / ".hermes")
        for pattern in ("state.db", "profiles/*/state.db"):
            yield from scan("hermes", "databases.hermes", hermes, pattern)
        for source, variable, fallback in (("kimi", "KIMI_HOME", ".kimi"), ("kimi-code", "KIMI_CODE_HOME", ".kimi-code")):
            yield from scan(source, "formats.kimi", root(variable, fallback) / "sessions", "**/wire.jsonl")
        openclaw = root("TOKENTRACKER_OPENCLAW_HOME", e.get("OPENCLAW_HOME") or e.get("OPENCLAW_STATE_DIR") or ".openclaw")
        for sub in ("sessions", "session-sqlite-import-archive"):
            for item in paths(openclaw / "agents", "*/" + sub + "/*.jsonl*"):
                if not item.name.endswith(".json"):
                    yield from emit("openclaw", "transcripts.openclaw", item)
        agents = openclaw / "agents"
        for agent in sorted(paths(agents)):
            for item in (agent / "agent/openclaw-agent.sqlite", agent / "openclaw-agent.sqlite"):
                if is_file(item):
                    yield from emit("openclaw", "extended.openclaw_db", item)
                    break
        for source, variable, fallback in (("codebuddy", "CODEBUDDY_HOME", ".codebuddy"), ("workbuddy", "WORKBUDDY_HOME", ".workbuddy"), ("workbuddy-ai", "WORKBUDDY_AI_HOME", ".workbuddy-ai")):
            base = root(variable, fallback)
            yield from scan(source, "extended.codebuddy" if source == "codebuddy" else "transcripts.workbuddy", base / "projects", "**/*.jsonl")
            if source != "codebuddy":
                yield from scan(source, "formats.workbuddy_trace", base / "traces", "**/trace_*.json")
                yield from emit(source, "databases.workbuddy", base / "workbuddy.db")
        if e.get("TOKENTRACKER_CODEBUDDY_LOG_FALLBACK") != "0":
            for ide in ("CodeBuddy CN", "Code"):
                for item in paths(app / ide / "logs", "**/*.log"):
                    if "tencent-cloud.coding-copilot" in str(item).lower():
                        yield from emit("codebuddy", "extended.codebuddy", item)
            for ide in ("CodeBuddyIDE", "VSCode"):
                yield from scan("codebuddy", "extended.codebuddy", local / "CodeBuddyExtension/Logs" / ide, "**/*.log")
        pi_owner = "pi" if is_dir(h / ".pi") else "omp"
        pi_generic = e.get("PI_CODING_AGENT_DIR")
        for source, variable, fallback in (("omp", "TOKENTRACKER_OMP_AGENT_DIR", root("OMP_HOME", e.get("PI_CONFIG_DIR") or ".omp") / "agent"),
                                            ("omo", "TOKENTRACKER_OMO_AGENT_DIR", root("TOKENTRACKER_OMO_HOME", e.get("OMO_HOME") or ".omo") / "agent"),
                                            ("pi", "TOKENTRACKER_PI_AGENT_DIR", h / ".pi/agent"),
                                            ("prime-agent", "TOKENTRACKER_PRIME_AGENT_DIR", root("TOKENTRACKER_PRIME_AGENT_HOME", ".prime") / "agent")):
            base = root(variable, pi_generic if pi_generic and pi_owner == source else fallback)
            yield from scan(source, "transcripts.pi_style", base / "sessions", "**/*.jsonl")
        yield from scan("pi", "transcripts.pi_style", h / ".multica/pi-sessions", "*.jsonl")
        yield from scan("minimax-code", "transcripts.pi_style", root("TOKENTRACKER_MINIMAX_HOME", ".minimax") / "v2/sessions", "**/messages.jsonl")
        craft = root("CRAFT_CONFIG_DIR", ".craft-agent")
        craft_config = obj(read_json(craft / "config.json", {}))
        workspaces = list(paths(craft / "workspaces", "*"))
        workspaces += [expand(p["rootPath"], h, e) for p in craft_config.get("workspaces", []) if isinstance(p, dict) and isinstance(p.get("rootPath"), str)]
        for workspace in workspaces:
            yield from scan("craft", "formats.craft", workspace / "sessions", "*/session.jsonl")
        reasonix_roots = [root("TOKENTRACKER_REASONIX_HOME", e.get("REASONIX_STATE_HOME") or ".reasonix")]
        if not e.get("TOKENTRACKER_REASONIX_HOME") and not e.get("REASONIX_STATE_HOME"):
            reasonix_roots.append(app / "reasonix")
        for base in reasonix_roots:
            yield from scan("reasonix", "formats.reasonix", base, "**/*.jsonl.telemetry.json")
        muse_sessions = root("TOKENTRACKER_MUSE_HOME", data / "muse") / "sessions"
        for item in paths(muse_sessions, "**/session.jsonl"):
            if not any(part.startswith(".") for part in item.relative_to(muse_sessions).parts):
                yield from emit("muse", "formats.muse", item)
        for dirname in (".copilot/otel", ".copilot-otel"):
            yield from scan("copilot", "extended.copilot_otel", h / dirname, "*.jsonl")
        if e.get("COPILOT_OTEL_FILE_EXPORTER_PATH"):
            yield from emit("copilot", "extended.copilot_otel", root("COPILOT_OTEL_FILE_EXPORTER_PATH", ""))
        for parser, filename, override in (("copilot_store", "session-store.db", "TOKENTRACKER_COPILOT_SESSION_STORE_DB"), ("copilot_app", "data.db", "TOKENTRACKER_COPILOT_APP_DB")):
            for item in (root(override, h / ".copilot" / filename), root("COPILOT_HOME", ".copilot") / filename):
                yield from emit("copilot", "extended." + parser, item)
        ide_roots = [expand(x.strip(), h, e) for x in e.get("TOKENTRACKER_KILOCODE_ROOTS", "").split(";" if platform == "win32" else ":") if x.strip()]
        ide_roots = ide_roots or [app / name for name in ("Code", "Code - Insiders", "Cursor", "CodeBuddy", "Windsurf", "VSCodium", "Trae", "Trae CN")]
        for ide in ide_roots:
            for source, extension in (("kilocode", "kilocode.kilo-code"), ("roocode", "rooveterinaryinc.roo-cline")):
                yield from scan(source, "formats.roo", ide / "User/globalStorage" / extension / "tasks", "*/ui_messages.json")
        cline_base = root("TOKENTRACKER_CLINE_HOME", e.get("CLINE_DIR") or ".cline")
        cline_data = root("TOKENTRACKER_CLINE_DATA_DIR", e.get("CLINE_DATA_DIR") or cline_base / "data")
        cline_sessions = root("TOKENTRACKER_CLINE_SESSIONS_DIR", e.get("CLINE_SESSION_DATA_DIR") or cline_data / "sessions")
        yield from scan("cline", "formats.cline", cline_sessions, "*/*.messages.json")
        droid_dirs = [expand(x.strip(), h, e) for x in e.get("DROID_SESSIONS_DIR", "").split(",") if x.strip()] or [root("FACTORY_DIR", ".factory") / "sessions"]
        for base in droid_dirs:
            yield from scan("droid", "formats.droid", base, "**/*.settings.json")
        dsh = root("TOKENTRACKER_DSH_HOME", e.get("DSH_HOME") or ".dsh")
        for directory in paths(dsh / "sessions", "*/*"):
            candidates = [p for p in paths(directory, "session*") if re.fullmatch(r"session(?:\.v\d+)?\.jsonl(?:\.zstd)?", p.name)]
            if candidates:
                def rank(p):
                    match = re.search(r"\.v(\d+)\.", p.name)
                    return modified(p), int(match[1]) if match else 0, p.suffix == ".zstd"
                yield from emit("dsh", "formats.dsh", max(candidates, key=rank))
        for item in paths(root("TOKENTRACKER_COMMANDCODE_HOME", ".commandcode") / "projects", "*/*.jsonl"):
            if ".prompts." not in item.name and ".checkpoint." not in item.name:
                yield from emit("command-code", "formats.command_code", item)
        yield from scan("lmstudio", "formats.lmstudio", root("TOKENTRACKER_LMSTUDIO_HOME", e.get("LM_STUDIO_HOME") or ".lmstudio") / "server-logs", "**/*.log")
        grok = root("TOKENTRACKER_GROK_HOME", e.get("GROK_HOME") or ".grok")
        for directory in paths(grok / "sessions", "*/*"):
            item = directory / "updates.jsonl"
            if not is_file(item):
                item = directory / "signals.json"
            yield from emit("grok", "formats.grok", item)
        db_specs = (("unsloth", "TOKENTRACKER_UNSLOTH_DB", root("UNSLOTH_STUDIO_HOME", ".unsloth/studio") / "studio.db"),
                    ("anythingllm", "TOKENTRACKER_ANYTHINGLLM_DB", app / "anythingllm-desktop/storage/anythingllm.db"),
                    ("devin", "TOKENTRACKER_DEVIN_DB", data / "devin/cli/sessions.db"),
                    ("goose", "TOKENTRACKER_GOOSE_DB", root("GOOSE_PATH_ROOT", h) / "data/sessions/sessions.db" if e.get("GOOSE_PATH_ROOT") else (app if platform in {"win32", "darwin"} else data) / "goose/sessions/sessions.db"),
                    ("zed", "TOKENTRACKER_ZED_DB", (local / "Zed" if platform == "win32" else data / "zed") / "threads/threads.db"))
        for source, variable, fallback in db_specs:
            yield from emit(source, "databases." + source, root(variable, fallback))
        if platform == "linux" and not e.get("TOKENTRACKER_GOOSE_DB") and not e.get("GOOSE_PATH_ROOT"):
            yield from emit("goose", "databases.goose", data / "Block/goose/sessions/sessions.db")
        if e.get("TOKENTRACKER_OPENSQUILLA_DB"):
            squilla = [root("TOKENTRACKER_OPENSQUILLA_DB", "")]
        elif e.get("OPENSQUILLA_GATEWAY_STATE_DIR"):
            squilla = [root("OPENSQUILLA_GATEWAY_STATE_DIR", "") / "sessions.db"]
        elif e.get("OPENSQUILLA_STATE_DIR"):
            squilla = [root("OPENSQUILLA_STATE_DIR", "") / "state/sessions.db"]
        elif e.get("OPENSQUILLA_HOME") or e.get("OPENSQUILLA_PROFILE"):
            profile = e.get("OPENSQUILLA_PROFILE") or "default"
            squilla = [root("OPENSQUILLA_HOME", ".opensquilla/profiles") / profile / "state/sessions.db"] if re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", profile) else []
        else:
            squilla = [h / ".opensquilla/state/sessions.db", app / "@opensquilla/desktop-electron/opensquilla/state/sessions.db"]
        for item in squilla:
            yield from emit("opensquilla", "databases.opensquilla", item)
        if e.get("TOKENTRACKER_TRAE_DB"):
            yield from emit("trae", "extended.trae", root("TOKENTRACKER_TRAE_DB", ""))
        else:
            trae_roots = [root("TOKENTRACKER_TRAE_HOME", "")] if e.get("TOKENTRACKER_TRAE_HOME") else [app / "Trae", app / "TRAE SOLO"]
            for base in trae_roots:
                for module in ("ai-agent", "ai-chat"):
                    item = base / "ModularData" / module / "database.db"
                    if is_file(item):
                        yield from emit("trae", "extended.trae", item)
                        break
