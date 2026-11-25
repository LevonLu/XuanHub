// WestwardPilgrims.js — 西行师徒的像素造型、步态与桥上倒影
.pragma library
.import "SceneArt.js" as Art

// 每个字母是一种材质，点号留空。先把同行同色像素连成短线，再按当前画幅贴到整数像素上。
function sprite(rows) {
    var spans = []
    for (var y = 0; y < rows.length; ++y) {
        var row = rows[y]
        for (var x = 0; x < row.length;) {
            var end = x + 1
            while (end < row.length && row[end] === row[x]) ++end
            if (row[x] !== ".") spans.push([x, y, end - x, row[x]])
            x = end
        }
    }
    return spans
}

// 悟空的金箍、桃形面纹与虎皮围裙，在回头时仍各自可辨。
var monkeyHead = sprite([
    ".....nnn....", "...naaaann..", "..nayaaannn.", ".nyYYYYYayn.",
    "...SSKSSaPbn", ".SSSSSSaSPbn", "SSSSSaaaaann",
    ".nSSSaaann..", "...naann...."
])
var monkeyCoat = sprite([
    "....rRRr...", "..aaRRRRn..", ".aaayyyann.", ".aaayyyaan.",
    "..aayyyaan.", "..nrrrrrn..", "..yyaynay..", ".ynnayyan..",
    ".yyanynay..", "..nnaynn..."
])

// 白龙马的头颈、腰腹和鞍毯画在一张侧影上，四肢分别随步态落地。
var horseBody = sprite([
    "......w..w...........................",
    ".....Wi.wW.......nnn.................",
    ".....WiWWw.......nynn................",
    "....WiWiWuu.....ryyyrr...............",
    "...WiWWWWuuu...rRRRRRrr..............",
    "..WiWKWWWWuu..rRyyyRRRr..............",
    ".WiWWWWWWWuuuuURRRRRRRr..uuwww.......",
    "WWWWbnWWWWWWwwwRRyRyRRrwwWWWWWu......",
    "WWWWWWbWWWWWWWWRRRRRRRWWWWWWWWWu.....",
    ".wWuu..nWWiWWWWwyryrywWWWWWWWWWWu....",
    "......n.WiWWWWWWWwwwwwWWWWWWWWWWWu...",
    ".......nWiWWWWWWWwwwwwWWWWWWWWWwWu...",
    "........WWWWWWWWWWwWWWWWWWWWWWw.uuu..",
    ".........wWWWWWWWWwwwwwWWWWWWWw..uwu.",
    "..........wwWWWwwwwuuuwwwWWwww...uwu.",
    "..........uwwwwuu......uuuwwwu....uu.",
    "...........uuu............uuu......u."
])
var tang = sprite([
    ".......y..y......", ".....y.Yy.Yy.....", ".....YyYyyYy.....",
    ".....yRyRyRy.....", ".....YyyyyYy.....", "......SSSSSk.....",
    ".....SKSSSSk.....", "....SSSSSSSk.....", ".....sSSSsn......",
    "......sSsrr......", "....nYyRRRRr.....", "...nyyRRRyRRr....",
    "...nyRyyyRRRr....", "..nyRRyRRRyRRr...", "..nyRyyyRyyyRr...",
    ".nsSSRyyRRyRRr...", "..nsSSRRyRRRRr...", "....RRyyyyyRRr...",
    "....RyRRyRRyRRr..", "...rRyyyyyyyRRr..", "...rRRyRRyRRyRRr.",
    "..rRRyyyyyyyyRRr.", "..rrrrRRRyRRRrr..", "...nnbnrrRRrr...."
])

var pigHead = sprite([
    "......kkkkk......", ".....kUUUkkkk....", "....kUUUUUkkkk...",
    "...kUUkUUUUkkk...", "....ssPPSPPkppp..", "...SPKSPPPPkPPpp.",
    ".pPPSSSPPPP.pPpp.", "PPKPKSPPPPs..pp..", "pPPPPPPPss.......",
    "..pppPssss.......", ".....nnnn........"
])
var pigRobe = sprite([
    "...kk..kkkk....", "..kuukssUUk....", ".kuuusSSSUk....",
    ".uuusSSSSSUk...", ".uuusSSSSSSs...", ".uusSSSSSSSSs..",
    ".uusSSSSSSSSs..", "..uusSSSSSsSsk.", "..kuussSSSSsuk.",
    "...kUuuuuuuuuk.", "...kUUkuuuuUkk.", "..kUUk.kuuUUkk."
])

var shaHead = sprite([
    "....nnnnnn....", "...nnbbbnnn...", "..nyYYYYYyn...",
    "...sSSSSsbn...", "..SSKSSSSbnn..", ".SSSSSSSbbnn..",
    "..nbbnnnnn....", "...nnbbbnn....", "....nnbnn.....", ".....nnn......"
])
var shaCoat = sprite([
    "....TTTuuu....", "...TLTTTuuuk..", "..nTLLTLTuuk..",
    "..TnYnYnTuuuk.", "..TTynynTuuuk.", ".TTTLTTTTuuuk.",
    ".TTTLTTTTuuuk.", "..TrrrRRRrruk.", "...nTLLTTTuk..",
    "..TTTLLTTTTuk.", ".TTTTLTTTTTuk.", ".nuuuuTTTuuuk.",
    "..kkuuukkuuk.."
])

var fabric = {
    K:"27212a", k:"34303b", n:"503c42", b:"986b4b", a:"cc853e",
    y:"e2ae52", Y:"ffde87", s:"c08465", S:"f3bc91", p:"b77e83", P:"eab0a3",
    r:"843e47", R:"be4e48", E:"e66048", t:"39555a", T:"508080", L:"88a69a",
    u:"646976", U:"9194a0", w:"a4b6b5", W:"dedec8", i:"fff2ce"
}

function costume(p, reflected) {
    var inks = {}
    for (var material in fabric) {
        var color = Art.mix(fabric[material], p.sky, 0.06 + 0.32 * p.night)
        inks[material] = reflected ? Art.mix(color, p.water, 0.52) : color
    }
    return inks
}

function block(c, color, x, y, w, h) {
    var left = Math.round(x), top = Math.round(y)
    var width = Math.round(x + w) - left, height = Math.round(y + h) - top
    if (width <= 0 || height <= 0) return
    c.fillStyle = color
    c.fillRect(left, top, width, height)
}

function stamp(c, inks, pixels, x, y, scale, facingRight) {
    for (var i = 0; i < pixels.length; ++i) {
        var span = pixels[i]
        var dx = facingRight ? -span[0] - span[2] : span[0]
        block(c, inks[span[3]], x + dx * scale, y + span[1] * scale, span[2] * scale, scale)
    }
}

// 杖、腿和担绳共用点阵直线，缩小时仍至少留一像素；步态只改变落点。
function stick(c, color, x, y, scale, ax, ay, bx, by, weight) {
    var steps = Math.max(1, Math.ceil(Math.max(Math.abs(bx - ax), Math.abs(by - ay)) * scale))
    var size = Math.max(1, Math.round(scale * weight))
    c.fillStyle = color
    for (var j = 0; j <= steps; ++j)
        c.fillRect(Math.round(x + scale * (ax + (bx - ax) * j / steps)),
                   Math.round(y + scale * (ay + (by - ay) * j / steps)), size, size)
}

function legs(c, inks, x, y, s, step, cloth) {
    for (var i = 0; i < 2; ++i) {
        var z = i ? step : -step
        var hip = i ? -2 : 3, foot = hip + 4 * z
        var sole = -1 - 2 * Math.max(0, z), shade = i ? cloth : inks.k
        stick(c, shade, x, y, s, hip, -10, hip - 1 - z, -5, 3)
        stick(c, shade, x, y, s, hip - 1 - z, -5, foot, sole - 1, 2)
        stick(c, inks.K, x, y, s, foot - 2, sole, foot + 1, sole, 2)
    }
}

function monkey(c, q, x, y, s, time, looking) {
    var step = looking ? 0.5 : Math.sin(time * 4.8)
    var body = y - Math.round(Math.abs(step) * s)
    legs(c, q, x, y, s, step, q.a)
    // 尾尖朝上回勾，侧面缩小时仍保留猴形。
    stick(c, q.b, x, body, s, 4, -11, 9, -7, 1.5)
    stick(c, q.a, x, body, s, 9, -7, 12, -11, 1.5)
    stick(c, q.a, x, body, s, 12, -11, 10, -13, 1.5)
    // 金箍棒斜挑至正前方；朱巾两股向来路飘去。
    stick(c, q.r, x, body, s, -10, -32, 8, -4, 2)
    stick(c, q.Y, x, body, s, -11, -34, -8, -29, 2)
    stick(c, q.y, x, body, s, 7, -6, 9, -3, 2)
    var tip = Math.round(2 * Math.sin(time * 3.4))
    stick(c, q.E, x, body, s, 2, -19, 9, -18 + tip, 2)
    stick(c, q.R, x, body, s, 9, -18 + tip, 14, -21 + tip, 1)
    stamp(c, q, monkeyCoat, x - 5 * s, body - 20 * s, s)
    stamp(c, q, monkeyHead, x + (looking ? 5 : -8) * s, body - 28 * s, s, looking)
    stick(c, q.a, x, body, s, 0, -17, -5, -19, 2)
    block(c, q.S, x - 6 * s, body - 20 * s, 3 * s, 2 * s)
}

function horse(c, q, x, y, s, time) {
    var gait = Math.sin(time * 3.5), lift = Math.round(Math.abs(gait) * s)
    var saddle = y - lift
    for (var i = 0; i < 4; ++i) {
        var hip = [-8, 9, -6, 12][i]
        var step = Math.sin(time * 3.5 + [0.45, 3.55, 3.15, 0][i])
        var knee = hip - step * 3, foot = hip + step * 5, base = -2 - Math.max(0, -step * 2)
        var leg = i < 2 ? q.u : q.w
        stick(c, leg, x, y, s, hip, -13, knee, -7, 2)
        stick(c, leg, x, y, s, knee, -7, foot, base, 1.5)
        stick(c, q.k, x, y, s, foot - 1.5, base, foot, base, 1.5)
    }
    stamp(c, q, horseBody, x - 19 * s, saddle - 25 * s, s)
    stamp(c, q, tang, x - 6 * s, saddle - 44 * s, s)
    // 缰绳连着僧人的手和马衔，保持骑手与坐骑的接触关系。
    stick(c, q.n, x, saddle, s, -16, -16, -11, -14, 1)
    stick(c, q.n, x, saddle, s, -11, -14, -3, -26, 1)
    stick(c, q.Y, x, saddle, s, -7, -27, -3, -28, 1)
}

function bajie(c, q, x, y, s, time, peering) {
    var step = peering ? 0 : Math.sin(time * 4.0 + 2)
    var body = y - Math.round(Math.abs(step) * s)
    legs(c, q, x, y, s, step, q.u)
    // 九齿钉耙扛在肩后，尖齿与脸朝相反方向，缩小后仍有清楚的轮廓。
    stick(c, q.b, x, body, s, -7, -12, 10, -29, 1.5)
    stick(c, q.k, x, body, s, 6, -29, 19, -29, 2)
    for (var i = 0; i < 9; ++i)
        stick(c, q.w, x, body, s, 6 + i * 1.5, -30, 6 + i * 1.5, -33, 1)
    stamp(c, q, pigRobe, x - 7 * s, body - 20 * s, s)
    stamp(c, q, pigHead, x - (peering ? 15 : 11) * s, body - (peering ? 24 : 29) * s, s)
    stick(c, q.U, x, body, s, 4, -16, -3, -14, 3)
    block(c, q.P, x - 5 * s, body - 16 * s, 3 * s, 3 * s)
}

function sha(c, q, x, y, s, time) {
    var step = Math.sin(time * 4.1 + 1), body = y - Math.round(Math.abs(step) * s)
    legs(c, q, x, y, s, step, q.T)
    var sway = Math.round(step * 2)
    // 扁担两端分别挂藤箱和包袱，摆动随脚步反相。
    for (var i = 0; i < 2; ++i) {
        var side = i ? 1 : -1, end = side * 14, bag = end + side * sway
        stick(c, q.n, x, body, s, end, -24, bag - 3, -14, 1)
        stick(c, q.n, x, body, s, end, -24, bag + 3, -14, 1)
        block(c, i ? q.t : q.b, x + (bag - 4) * s, body - 14 * s, 9 * s, 7 * s)
        stick(c, i ? q.L : q.y, x, body, s, bag - 4, -13, bag + 4, -13, 1)
        stick(c, i ? q.T : q.a, x, body, s, bag - 2, -12, bag - 2, -8, 1)
    }
    stick(c, q.b, x, body, s, -16, -24, 16, -24, 2)
    stick(c, q.y, x, body, s, -16, -25, 16, -25, 1)
    stamp(c, q, shaCoat, x - 6 * s, body - 21 * s, s)
    stamp(c, q, shaHead, x - 7 * s, body - 30 * s, s)
    // 浅色念珠沿领口搭成弧，分得开胡须与青衫。
    for (var bead = 0; bead < 5; ++bead) {
        var beadY = -21 + (2 - Math.abs(bead - 2))
        block(c, q.Y, x + (-5 + bead * 2) * s, body + beadY * s, 1.6 * s, 2 * s)
    }
    stick(c, q.L, x, body, s, 4, -19, 9, -23, 2)
    block(c, q.S, x + 8 * s, body - 25 * s, 3 * s, 2 * s)
}

function draw(c, g, p, time, groundY, reflected, phase) {
    var q = costume(p, reflected)
    if (g.minimal) {
        // 极窄槽位只保留一匹白马和朱袈裟的色块，以免四个轮廓挤成一团。
        var s = Math.max(1, Math.min(2, Math.floor(Math.min(g.w / 10, g.h / 9))))
        var x = Math.max(0, Math.floor((g.w - 9 * s) / 2)), y = groundY - 6 * s
        c.fillStyle = q.W
        c.fillRect(x, y + s, 3*s, 2*s); c.fillRect(x + 2*s, y + 2*s, 6*s, 2*s)
        c.fillRect(x + 2*s, y + 4*s, s, 2*s); c.fillRect(x + 7*s, y + 4*s, s, 2*s)
        c.fillStyle = q.R; c.fillRect(x + 4*s, y, 2*s, 3*s)
        c.fillStyle = q.Y; c.fillRect(x + 4*s, y - 2*s, 2*s, 2*s)
        return
    }
    // 白马上方还要容纳僧冠与步态起伏，按脚下基线预留顶边。
    var size = Math.min(g.expanded ? 1.45 : 0.85, Math.max(1, groundY - 2) / 45, g.w / 225)
    // 镜头跟队伍同行；四人的脚步错开，前后距离轻微变化。
    var drift = Math.sin(time * 0.28) * 1.9 * size
    var beat = time % 20
    var looking = beat >= 8 && beat < 12
    var peering = phase === "dusk" && beat >= 8.5 && beat < 12.5
    monkey(c, q, g.w * 0.20 + drift + Math.sin(time * 0.66) * 1.9 * size, groundY, size, time, looking)
    horse(c, q, g.w * 0.416 + drift, groundY, size, time)
    bajie(c, q, g.w * 0.61 + drift + (peering ? 1 : Math.sin(time * 0.57) * 1.4) * size,
          groundY, size, time, peering)
    sha(c, q, g.w * 0.80 + drift + Math.sin(time * 0.50) * 1.8 * size, groundY, size, time)
}
