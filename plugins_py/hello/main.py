# main.py — py_hello 子插件（M6 示例）
# 模块级三约定：update() -> dict 必需；init(config)、invoke(name, args) 可选。
# 注意：update/invoke 在宿主 GUI 线程内执行（持 GIL），必须轻量、禁止阻塞与起线程。
from datetime import datetime

_config = {}
_count = 0


def init(config):
    """注入 config.json 中本子插件的 config 段（dict）。"""
    global _config
    _config = dict(config or {})


def update():
    """每个 tick 被调用，返回的 dict 原样填入 QML 的 plugin.values。"""
    prefix = _config.get("prefix", "Py")
    return {"text": "%s [%d] %s" % (prefix, _count, datetime.now().strftime("%H:%M:%S"))}


def invoke(name, args):
    """QML 侧 plugin.call(name, args) 的入口。"""
    global _count
    if name == "bump":
        _count += 1
