# main.py — py_hello 子插件（M6 示例）
# 模块级三约定：update() -> dict 必需；init(config)、invoke(name, args) 可选。
# 注意：update/invoke 在隔离 worker 的主线程内串行执行，必须轻量、禁止阻塞。
from datetime import datetime

_config = {}
_count = 0


def init(config):
    """注入 config.json 中本子插件的 config 段（dict）。"""
    set_config(config)


def set_config(config):
    """运行中原地应用新配置；未实现此函数的旧插件会由宿主单独重启。"""
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
