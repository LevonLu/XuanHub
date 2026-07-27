# main.py — py_rest_reminder 子插件：定时休息提醒
#
# 循环模式：工作时间 → 休息时间 → 工作时间 → ...
# 休息时间到 → QML 遮罩从 bar 向下滑落覆盖全屏，显示倒计时和自定义文字，
# ESC 提前结束休息。
#
# update() 在 GUI 线程执行，必须轻量——所有计时基于 time.time() 的差值计算。
import threading
import time

# ========== 模块级状态 ==========
_work_seconds = 25 * 60       # 工作秒数
_rest_seconds = 5 * 60        # 休息秒数

_status = "working"           # "working" | "resting"
_phase_start = 0.0            # 当前阶段开始时间戳
_overlay_active = False       # 遮罩是否应该显示
_stop_flag = False
_lock = threading.Lock()

# 配置项（由 init 注入）
_mask_type = "default"        # "default"(跟随主题) | "color" | "image"，与设置页缺省一致
_mask_color = "#1e1e2e"
_mask_image = ""
_mask_opacity = 0.92
_mask_text = "休息一下"
_text_size = 24
_progress_style = "none"      # 锁屏进度条："none" | "bar" | "ring"


def _fmt_time(seconds: int) -> str:
    """秒数 → MM:SS 格式"""
    m = seconds // 60
    s = seconds % 60
    return f"{m:02d}:{s:02d}"


# ========== 模块约定 ==========

def init(config: dict):
    """注入 config.json 中本插件的 config 段。"""
    global _work_seconds, _rest_seconds, _mask_type, _mask_color
    global _mask_image, _mask_opacity, _mask_text, _text_size
    global _progress_style
    global _phase_start, _status, _overlay_active

    if isinstance(config, dict):
        _work_seconds = max(10, min(86400, int(config.get("workSeconds", 1500))))
        _rest_seconds = max(10, min(3600, int(config.get("restSeconds", 300))))
        _mask_type = str(config.get("maskType", "default"))
        _mask_color = str(config.get("maskColor", "#1e1e2e"))
        _mask_image = str(config.get("maskImage", ""))
        _mask_opacity = max(0.1, min(1.0, float(config.get("maskOpacity", 0.92))))
        _mask_text = str(config.get("maskText", "休息一下"))
        _text_size = max(12, min(72, int(config.get("textSize", 24))))
        ps = str(config.get("progressStyle", "none"))
        _progress_style = ps if ps in ("none", "bar", "ring") else "none"

    # 重置状态：总是从工作阶段开始
    _status = "working"
    _phase_start = time.time()
    _overlay_active = False


def update():
    """返回当前状态快照。"""
    global _status, _phase_start, _overlay_active

    now = time.time()
    elapsed = int(now - _phase_start)

    if _status == "working":
        remaining = max(0, _work_seconds - elapsed)
        if remaining <= 0:
            # 切换到休息
            _status = "resting"
            _phase_start = now
            _overlay_active = True
            remaining = _rest_seconds
            total = _rest_seconds
            label = "休息"
        else:
            total = _work_seconds
            label = "工作中"
    else:  # resting
        remaining = max(0, _rest_seconds - elapsed)
        total = _rest_seconds
        label = "休息"
        if remaining <= 0:
            # 休息结束 → 回到工作
            _status = "working"
            _phase_start = now
            _overlay_active = False
            remaining = _work_seconds
            total = _work_seconds
            label = "工作中"

    return {
        "status": _status,
        "label": label,
        "remaining": remaining,
        "total": total,
        "timeStr": _fmt_time(remaining),
        "overlayActive": _overlay_active,
        "maskType": _mask_type,
        "maskColor": _mask_color,
        "maskImage": _mask_image,
        "maskOpacity": _mask_opacity,
        "maskText": _mask_text,
        "textSize": _text_size,
        "progressStyle": _progress_style,
    }


def invoke(name, args):
    """QML 交互入口。"""
    global _status, _phase_start, _overlay_active
    if name == "end_rest":
        if _status == "resting":
            _status = "working"
            _phase_start = time.time()
            _overlay_active = False


def stop():
    """卸载时调用。"""
    global _stop_flag
    _stop_flag = True
