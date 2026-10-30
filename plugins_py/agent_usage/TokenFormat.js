// TokenFormat.js — 状态栏、总览与详情共用的十进制 Token 单位格式
.pragma library

function compact(value) {
    var number = Number(value || 0)
    if (!isFinite(number)) return "—"
    var units = ["", "K", "M", "B", "T"]
    var unit = 0
    while (Math.abs(number) >= 1000 && unit < units.length - 1) {
        number /= 1000
        ++unit
    }
    // 四舍五入跨过千位时同步进位，避免出现 1000K 或 1000M。
    var rounded = Number(number.toFixed(unit === 0 ? 0 : 2))
    if (Math.abs(rounded) >= 1000 && unit < units.length - 1) {
        rounded /= 1000
        ++unit
    }
    return String(rounded) + units[unit]
}
