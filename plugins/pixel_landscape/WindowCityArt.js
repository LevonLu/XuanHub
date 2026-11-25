// WindowCityArt.js — 从卷帘下望向暮色中的住宅与厂房
.pragma library
.import "SceneArt.js" as A

function geometry(w, h, expanded) {
    var g = A.geometry(w, h, expanded)
    g.horizon = Math.round(h * 0.68)
    g.jamb = Math.min(Math.floor(w / 5), Math.max(1, Math.round(h * 0.042)))
    g.blind = Math.max(1, Math.round(h * (expanded ? 0.079 : 0.045)))
    g.sill = Math.floor(h * 0.91)
    return g
}

// 薄云以长短错落的横笔收尾，右侧天光从楼群背后透出。
function sky(c, g, p, weather) {
    var w = g.w, h = g.h
    var sunset = Math.max(0, p.warm - p.night)
    if (weather === "clear") {
        var glow = c.createRadialGradient(w * 0.93, g.horizon, 0,
                                         w * 0.93, g.horizon, Math.max(1, w * 0.58))
        glow.addColorStop(0, "#ffdfa3")
        glow.addColorStop(1, "transparent")
        c.globalAlpha = sunset * 0.66
        c.fillStyle = glow
        c.fillRect(0, 0, w, h)
        c.globalAlpha = 1
    }
    A.drawCelestial(c, g, p, weather, w * (0.25 + sunset * 0.80),
                    h * (0.22 + sunset * 0.44))
    if (g.minimal) return
    for (var i = 0; i < 15; ++i) {
        var x = A.noise(i+29) * w
        var y = h * (0.49 + A.noise(i+82) * 0.17)
        var length = (0.04 + A.noise(i+61) * 0.14) * w
        c.globalAlpha = (0.09 + A.noise(i+19) * 0.13) * (1 - 0.6*p.night)
        A.rect(c, p.ridge, x, y, length, 1)
        A.rect(c, p.cloud, x + length*0.13, y-1, length*0.58, 1)
    }
    c.globalAlpha = 1
}

// 远处住宅楼成簇分布，往右渐高；中间留出可看清地平线的空隙。
function skyline(c, g, p) {
    var w = g.w, h = g.h
    var base = g.horizon + Math.round(h * 0.047)
    var haze = A.mix(p.far, p.horizon, 0.35)
    A.rect(c, haze, 0, g.horizon, w, h - g.horizon)
    var count = Math.max(5, Math.min(90, Math.round(w / Math.max(3, h*0.045))))
    for (var i = 0; i < count; ++i) {
        var farH = (0.014 + A.noise(i+212)*0.044) * h
        A.rect(c, haze, w*i/count, g.horizon-farH, w/count*0.88, farH+1)
    }
    var shadow = A.mix(p.ridge, p.water, 0.27)
    var light = A.mix(shadow, p.horizon, 0.36)
    var clusters = [[0.10,2,0.06], [0.37,2,0.069], [0.61,3,0.092],
                    [0.79,1,0.092], [0.89,3,0.17]]
    for (var group = 0; group < clusters.length; ++group) {
        var cluster = clusters[group]
        var bw = Math.max(1, Math.min(w*0.036, h*(0.033 + group*0.007)))
        for (var j = 0; j < cluster[1]; ++j) {
            var x = w*cluster[0] + (j-1)*bw*1.18
            var bh = h*cluster[2]*(0.72+0.28*A.noise(group*9+j))
            var y = base-bh
            A.rect(c, shadow, x, y, bw, bh)
            A.rect(c, light, x+bw*0.70, y, bw*0.30, bh)
            A.rect(c, shadow, x+bw*0.20, y-h*0.012, bw*0.46, h*0.016)
            if (g.expanded) {
                for (var floor = 3; floor < bh-1; floor += 3) {
                    A.rect(c, p.far, x+1, y+floor, Math.max(1,bw*0.19), 1)
                    if (bw >= 6) A.rect(c, p.far, x+bw*0.48, y+floor, 1, 1)
                }
            }
        }
    }
    A.rect(c, shadow, 0, base-1, w, h-base+1)
}

function waterTower(c, g, p) {
    var x = Math.round(g.w*0.29), y = Math.round(g.h*0.76)
    var s = Math.min(g.h/180, g.w/120)
    var iron = A.mix(p.mountain, p.ridge, 0.45)
    function r(dx, dy, w, h) { A.rect(c, iron, x+dx*s, y+dy*s, w*s, h*s) }
    r(-4,-23,1,24); r(3,-23,1,24); r(-4,-11,8,1)
    r(-5,-26,10,6); r(-3,-28,6,2); r(1,-33,1,6)
    A.rect(c, p.far, x+3*s, y-25*s, 2*s, 5*s)
}

function powerLines(c, g, p) {
    var x = Math.round(g.w*0.715), y = Math.round(g.h*0.76)
    var s = Math.min(g.h/180, g.w/160)
    var iron = A.mix(p.ridge, p.mountain, 0.38)
    function beam(ax, ay, bx, by) {
        var steps = Math.max(1, Math.ceil(s*Math.max(Math.abs(ax-bx),Math.abs(ay-by))))
        for (var k = 0; k <= steps; ++k)
            A.rect(c, iron, x+s*(ax+(bx-ax)*k/steps), y+s*(ay+(by-ay)*k/steps), 1, 1)
    }
    beam(0,-30,-4,0); beam(0,-30,4,0)
    beam(-5,-22,5,-22); beam(-6,-18,6,-18)
    beam(-3,-8,2,-17); beam(3,-8,-2,-17)
    if (g.expanded) {
        c.globalAlpha = 0.50
        for (var i = -32; i < 42; ++i) {
            var sag = Math.sin(Math.abs(i)/42*Math.PI)*3
            A.rect(c, iron, x+i, y-21+sag, 1, 1)
        }
        c.globalAlpha = 1
    }
}

// 大跨度厂房的斜屋面和背光墙面交错，积雪只落在朝天的屋顶。
function workshops(c, g, p, weather) {
    var w = g.w, h = g.h
    var wall = A.mix(p.mountain, p.rock, 0.38)
    var eave = A.mix(p.mountain, p.trim, 0.48)
    var sunWall = A.mix(wall, p.horizon, 0.17 + 0.12*(p.warm-p.night))
    var roof = weather === "snow" ? p.snow : A.mix(p.water, p.snow, 0.30)
    var pane = A.mix(wall, p.trim, 0.60)
    var pitch = Math.max(3, h*0.028)
    for (var row = 0; row < 2; ++row) {
        var count = Math.max(2, Math.min(24, Math.ceil(w/(h*(row ? 0.69 : 0.35)))))
        for (var i = -1; i < count; ++i) {
            var seed = i + 31*row
            var x = (i + (row ? 0.33 : 0.13))*w/count
            var span = w/count*(0.76 + A.noise(seed+359)*0.32)
            var y = h*((row ? 0.83 : 0.75) + A.noise(seed+322)*0.058)
            var depth = Math.min(span*0.23, h*0.078)
            var ridge = y-pitch*(row ? 1.7 : 0.8)
            var facade = A.mix(sunWall, row ? p.trim : p.ridge, A.noise(seed+55)*0.48)
            var top = A.mix(roof, weather === "snow" ? p.cloud : p.rock, A.noise(seed+77)*0.48)
            A.rect(c, facade, x, y, span, h-y)
            A.rect(c, wall, x+span-depth, ridge, depth, h-ridge)
            A.polygon(c, top, [[x,y],[x+depth,ridge],[x+span,ridge],
                              [x+span-depth,y+1],[x,y+1]])
            A.rect(c, eave, x, y+1, span, 1)
            if (g.minimal) continue
            var spacing = Math.max(4, Math.round(h*(row ? 0.051 : 0.032)))
            if (g.expanded && row === 1) {
                // 厂房采光带随屋面退向远端，墙面窗列隐在挑檐阴影下。
                for (var rib = depth; rib < span-depth; rib += spacing*1.3)
                    A.polygon(c, roof, [[x+rib,ridge+1],[x+rib+3,ridge+1],
                                       [x+rib-depth*0.61+3,y-2],[x+rib-depth*0.61,y-2]])
                A.rect(c, wall, x+2, y+pitch-1, span-depth-4, pitch)
            }
            for (var win = spacing/2; win < span-depth-1; win += spacing) {
                var lit = A.noise(Math.floor(win)+seed+200) < (p.warm*0.14 + p.night*0.30)
                A.rect(c, lit ? p.lamp : pane, x+win, y+pitch, spacing*0.56, pitch*0.52)
            }
            if (g.expanded && span > 20 && i%2 === 0) {
                A.rect(c, wall, x+span*0.41, ridge-3, span*0.13, 3)
                A.rect(c, top, x+span*0.41-1, ridge-4, span*0.13+2, 1)
            }
        }
    }
}

function windowFrame(c, g, p) {
    var w = g.w, h = g.h, edge = g.jamb
    var metal = A.mix(p.trim, p.rock, 0.22)
    var bevel = A.mix(metal, p.horizon, 0.52)
    var dark = A.mix(p.trim, "171d2b", 0.50)
    A.rect(c, dark, 0, 0, edge, h)
    A.rect(c, bevel, edge-1, g.blind, 1, h)
    A.rect(c, metal, w-edge, 0, edge, h)
    A.rect(c, dark, w-edge, g.blind, 1, h)
    A.rect(c, metal, 0, 0, w, g.blind)
    A.rect(c, dark, 0, g.blind-1, w, 1)
    if (g.expanded) {
        A.rect(c, dark, 0, 0, w, g.blind*0.5)
        A.rect(c, bevel, 0, g.blind-3, w, 1)
        A.rect(c, metal, 1, g.blind, edge*0.42, h-g.blind)
        // 拉珠垂在右窗框的阴影里。玻璃上的三盏顶灯沿视线渐次缩小。
        for (var bead = g.blind+1; bead < h*0.78; bead += 4)
            A.rect(c, bevel, w-edge/2, bead, 1, 2)
        for (var i = 0; i < 3; ++i) {
            var width = w*(0.111 - 0.029*i), y = h*(0.23 + 0.094*i)
            var x = w*0.535 - width/2
            c.globalAlpha = (0.20+0.15*p.night) - 0.043*i
            A.polygon(c, p.snow, [[x,y],[x+width,y],[x+width-3,y+3-i],[x+2,y+3-i]])
        }
        c.globalAlpha = 1
    }
    A.rect(c, metal, 0, g.sill, w, h-g.sill)
    if (g.expanded) {
        var gleam = c.createLinearGradient(0, 0, w, 0)
        gleam.addColorStop(0, dark)
        gleam.addColorStop(0.60, A.mix(metal, p.horizon, 0.22))
        gleam.addColorStop(1, metal)
        c.fillStyle = gleam
        c.fillRect(0, g.sill+1, w, h-g.sill-2)
    }
    A.rect(c, bevel, edge, g.sill, w-2*edge, 1)
    A.rect(c, dark, 0, h-1, w, 1)
    if (g.minimal) return
    // 把手朝右，底座和投影落在窗台上，栏高变化时仍留出天空。
    var s = Math.min(h/180, w/90), cx = Math.round(w*0.48), cy = g.sill
    A.rect(c, dark, cx-5*s, cy+4*s, 30*s, 2*s)
    A.rect(c, bevel, cx-7*s, cy+2*s, 16*s, 2*s)
    A.rect(c, metal, cx-2*s, cy-3*s, 5*s, 7*s)
    A.polygon(c, bevel, [[cx-2*s,cy],[cx-2*s,cy-4*s],[cx+2*s,cy-7*s],
                         [cx+20*s,cy-7*s],[cx+21*s,cy-5*s],[cx+4*s,cy-4*s],[cx+2*s,cy]])
}

function drawStatic(c, g, p, weather) {
    c.reset()
    c.clearRect(0, 0, g.w, g.h)
    sky(c, g, p, weather)
    skyline(c, g, p)
    if (!g.minimal) {
        waterTower(c, g, p)
        powerLines(c, g, p)
    }
    workshops(c, g, p, weather)
    windowFrame(c, g, p)
}

function drawDynamic(c, g, p, weather, time, meteor) {
    c.reset()
    c.clearRect(0, 0, g.w, g.h)
    if (g.minimal) return
    // 天气只出现在玻璃内侧的视野中，窗台、拉珠和把手保持干燥。
    c.save()
    c.beginPath()
    c.rect(g.jamb, g.blind, g.w-2*g.jamb, Math.max(1,g.sill-g.blind-Math.ceil(g.h*0.04)))
    c.clip()
    A.drawNightAmbience(c, g, p, weather, time, meteor)
    for (var i = 0; i < 3; ++i) {
        var length = Math.max(4, g.w*(0.06 + 0.025*i))
        var x = (A.noise(i+99)*g.w + time*(0.32+i*0.12)) % (g.w+length) - length
        var y = g.h*(0.35+i*0.064)
        c.globalAlpha = 0.18*(1-0.65*p.night)
        A.rect(c, p.cloud, x, y, length, 1)
        A.rect(c, p.cloud, x+length*0.23, y-1, length*0.45, 1)
    }
    c.globalAlpha = 1
    A.drawPrecipitation(c, g, p, weather, time)
    c.restore()
}
