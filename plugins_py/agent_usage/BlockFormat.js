// BlockFormat.js — 状态栏与设置预览共用的显示块格式
.pragma library
.import "TokenFormat.js" as Tokens

function text(block, tr, now) {
    if (!block || block.metric === "empty") return ""
    var metric = block.metric
    var label = block.label !== undefined ? block.label : block.name || ""
    var value = block.value === null || block.value === undefined ? "—" : block.value
    if (metric === "tokens" && value !== "—") value = Tokens.compact(value)
    if (metric === "quota5h") label += (label ? " " : "") + "5h"
    if (metric === "quota7d") label += (label ? " " : "") + "7d"
    if (metric === "window") label += (label ? " " : "") + tr(block.windowLabel || block.windowId || "额度")
    if (["quota5h", "quota7d", "window"].indexOf(metric) >= 0 && value !== "—") value = Number(value).toFixed(1).replace(/\.0$/, "") + "%"
    if (metric === "balance" || metric === "spendToday") {
        label += (label ? " " : "") + tr(metric === "balance" ? "余额" : "今日")
        if (value !== "—") value = (metric === "spendToday" ? "≈" : "") + (block.currency === "CNY" ? "¥" : block.currency === "USD" ? "$" : block.currency || "") + Number(value).toFixed(2)
    }
    if (metric === "reset") {
        label += (label ? " " : "") + tr("重置")
        var seconds = Math.max(0, Math.ceil((Date.parse(block.resetAt || "") - now) / 1000))
        value = !block.resetAt ? "—" : seconds === 0 ? tr("待刷新") : seconds >= 86400 ? Math.floor(seconds / 86400) + "d " + Math.floor(seconds % 86400 / 3600) + "h" : seconds >= 3600 ? Math.floor(seconds / 3600) + "h " + Math.floor(seconds % 3600 / 60) + "m" : Math.ceil(seconds / 60) + "m"
    }
    return (label ? label + " " : "") + value + (block.stale || block.affected ? " *" : "")
}
