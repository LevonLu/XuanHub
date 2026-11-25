// BelowSurfaceArt.js — 湖面浮标与水下沉船共用一段鱼群游动的时间
.pragma library
.import "SceneArt.js" as A

function geometry(w, h, expanded) {
    var g = A.geometry(w, h, expanded)
    g.surfaceY = Math.round(h * (expanded ? 0.22 : 0.66))
    g.horizon = Math.round(g.surfaceY * 0.70)
    g.floatX = Math.round(w * (g.minimal ? 0.5 : 0.41))
    // 剖面只给天空留一小条，云、星和固定时段的太阳也要投影到这段天空里。
    var skyHeight = Math.max(1, Math.round(g.horizon / 0.54))
    g.sky = { w: w, h: skyHeight, expanded: expanded, minimal: g.minimal, s: g.s,
        stars: g.stars.map(function(star) {
            return { x: star.x, y: star.y * skyHeight / h, seed: star.seed }
        }) }
    return g
}

function ease(t) { return t * t * (3 - 2 * t) }

function encounter(time) {
    var t = time % 18
    var fishX, depth
    if (t < 4) {
        var approach = ease(t / 4)
        fishX = 0.20 + 0.21 * approach
        depth = 0.39 - 0.305 * approach
    } else if (t < 10) {
        var escape = (t - 4) / 6
        fishX = 0.41 + 0.82 * escape
        depth = 0.085 + 0.33 * ease(Math.min(1, escape * 1.8))
    } else {
        // 鱼尾出画后才从另一侧游回；循环边界的位置和深度连续。
        var returning = ease((t - 10) / 8)
        fishX = -0.16 + 0.36 * returning
        depth = 0.52 - 0.13 * returning
    }
    var dip = t < 3.6 || t > 5.2 ? 0 : t <= 4 ? ease((t - 3.6) / 0.4) : 1 - ease((t - 4) / 1.2)
    return { fishX: fishX, fishDepth: depth, dip: dip, ripple: t >= 4 && t < 7 ? (t - 4) / 3 : -1 }
}

function shore(c, g, p, weather) {
    A.drawSky(c, g.sky, p, weather)
    var h = g.horizon, w = g.w
    A.polygon(c, p.far, [[0,h*.56],[w*.10,h*.32],[w*.15,h*.47],[w*.22,h*.26],[w*.30,h*.59],
        [w*.39,h*.48],[w*.50,h*.75],[w*.66,h*.43],[w*.71,h*.52],[w*.81,h*.23],[w*.96,h*.56],[w,h*.48],[w,h+1],[0,h+1]])
    A.polygon(c, p.ridge, [[0,h*.77],[w*.10,h*.59],[w*.22,h*.81],[w*.38,h*.68],
        [w*.49,h*.88],[w*.65,h*.70],[w*.80,h*.75],[w*.91,h*.55],[w,h*.69],[w,h+1],[0,h+1]])
    var bank = weather === "snow" ? p.rock : p.forest
    A.polygon(c, bank, [[0,h*.78],[w*.10,h*.81],[w*.21,h*.95],[w*.34,h],[w*.63,h],
        [w*.79,h*.92],[w*.90,h*.76],[w,h*.72],[w,h+1],[0,h+1]])
    if (!g.minimal) {
        // 松林沿两岸收束，中间的开口让浮标有一块干净的水面作背景。
        for (var i = 0; i < Math.min(92, w / 3); ++i) {
            var x = A.noise(i+601) * w
            var edge = Math.max(0, Math.abs(x / w - 0.48) * 2 - 0.40)
            var size = (0.11 + A.noise(i+317) * 0.35) * h * edge
            if (size < 2) continue
            A.polygon(c, bank, [[x-size*.30,h],[x,h-size],[x+size*.30,h]])
        }
    }
    var water = A.mix(p.water, p.horizon, 0.22)
    A.rect(c, water, 0, h + 1, w, g.h - h)
    for (var j = 0; j < Math.min(110, w/2); ++j) {
        var y = A.noise(j+48)
        var end = g.expanded ? g.surfaceY : g.h
        A.rect(c, A.mix(water, j%4 ? p.horizon : bank, 0.13+0.29*A.noise(j+9)),
            A.noise(j+191)*w, h+1+y*(end-h-1), 1+A.noise(j+122)*(3+10*y), 1)
    }
}

// 水草的折叶与种穗沿同一根茎生长，缩为栏上剪影时仍能辨认根部。
function reeds(c, x, y, size, direction, dark, light, cattails) {
    var local = { x: x, y: y, s: size }
    for (var i = 0; i < 5; ++i) {
        var reach = direction * (i-1) * 3, height = 12 + (i*11)%17
        A.shape(c, local, i%2 ? dark : light, [[0,0],[reach*.48,-height*.54],
            [reach,-height],[reach+1,-height*.46],[2,0]])
        if (cattails && (i === 1 || i === 3))
            A.part(c,local,dark,reach-1,-height-3,2,5)
    }
}

function wreck(c, g, p) {
    // 用弯曲的船首、断桅和半埋入沙的龙骨，让剪影在夜间也能读成一条旧船。
    var ship = { x: g.w * .69, y: g.h * .75, s: Math.min(g.w/320, g.h/180) }
    var rim = A.mix(p.wood, "7f9680", .48), hull = A.mix(p.wood, "194653", .60)
    var shade = A.mix(p.trim, "103443", .68), plank = A.mix(hull, rim, .30)
    A.shape(c,ship,shade,[[-52,-1],[-44,2],[-12,1],[20,-4],[58,-15],[51,1],[39,12],[20,18],[-27,22],[-43,12]])
    A.shape(c,ship,hull,[[-47,1],[-13,3],[18,-2],[54,-12],[49,0],[35,12],[-27,18],[-40,10]])
    A.shape(c,ship,rim,[[-51,-2],[-32,0],[-3,-1],[25,-6],[57,-16],[54,-12],[24,-3],[-4,2],[-33,3],[-48,1]])
    A.shape(c,ship,plank,[[-38,7],[-10,8],[24,2],[47,-5],[43,-2],[22,4],[-14,11],[-37,9]])
    A.shape(c,ship,plank,[[-25,15],[2,11],[29,7],[35,5],[30,9],[-22,17]])
    A.shape(c,ship,shade,[[-16,3],[-11,4],[-10,8],[-6,8],[-5,14],[-13,12]])
    for (var rivet = 0; rivet < 7; ++rivet)
        A.part(c,ship,rim,1+rivet*5,6-rivet,1,1)
    A.shape(c,ship,shade,[[-21,0],[-19,-14],[15,-20],[20,-5]])
    A.shape(c,ship,plank,[[-17,-1],[-16,-12],[12,-17],[16,-5]])
    A.shape(c,ship,rim,[[-22,-13],[-20,-16],[13,-22],[17,-19]])
    A.part(c,ship,shade,-13,-11,6,7)
    A.part(c,ship,shade,-4,-13,5,7)
    A.part(c,ship,rim,4,-15,10,8)
    A.part(c,ship,shade,5,-14,8,6)
    A.shape(c,ship,rim,[[-32,0],[-39,-35],[-38,-40],[-36,-37],[-35,-41],[-30,-2]])
    A.shape(c,ship,plank,[[-47,-19],[-47,-21],[-21,-29],[-20,-27]])
    A.shape(c,ship,shade,[[-38,-34],[-37,-34],[-48,-2],[-49,-2]])
    for (var i = 0; i < 6; ++i)
        A.part(c,ship,shade,20+5*i,-5-1.4*i,1,5)
}

function underwater(c, g, p, weather) {
    var top = g.surfaceY + 1, depth = g.h-top
    var shallow = A.mix(p.water, "3ba59c", 0.45-0.27*p.night)
    var deep = A.mix(p.forest, "102e40", 0.71)
    for (var i = 0; i < 30; ++i) {
        var t = i/29
        A.rect(c, A.mix(shallow, deep, t*(1.65-.65*t)), 0, top+i*depth/30, g.w, depth/30+1)
    }
    // 光束分段衰减，只在静态画布重画时计算；细颗粒另随水流缓动。
    for (var beam = 0; beam < 5; ++beam) {
        var source = g.w * ([.19,.24,.57,.63,.80][beam])
        var length = depth * (.38 + .44*A.noise(beam+76))
        var wide = g.w * (.025 + .05*A.noise(beam+53))
        for (var band = 0; band < 9; ++band) {
            var a = band/9, b = (band+1)/9
            c.globalAlpha = (1-a)*(.10-.06*p.night)*(weather === "clear" ? 1 : .38)
            A.polygon(c,p.sun,[[source-length*a*.48,top+length*a],[source+wide-length*a*.29,top+length*a],
                [source+wide-length*b*.29,top+length*b],[source-length*b*.48,top+length*b]])
        }
    }
    c.globalAlpha = 1
    var s = Math.min(g.w/320, g.h/180), w = g.w, h = g.h
    var silt = A.mix(deep,p.grassLight,.46), slate = A.mix(deep,p.rock,.36)
    var vegetation = A.mix(deep,p.leaf,.57)
    A.polygon(c,slate,[[0,h*.67],[w*.13,h*.76],[w*.28,h*.84],[w*.48,h*.80],
        [w*.66,h*.76],[w*.84,h*.79],[w,h*.65],[w,h],[0,h]])
    for (var j = 0; j < 16; ++j) {
        var x = j*w/15, bottom = h*(.82+.07*Math.sin(j))
        reeds(c,x,bottom,s*(.4+A.noise(j+23)*.37),j%2?1:-1,slate,vegetation,false)
    }
    A.polygon(c,deep,[[w*.51,h*.84],[w*.58,h*.82],[w*.68,h*.82],[w*.79,h*.76],
        [w*.82,h*.78],[w*.75,h*.86],[w*.57,h*.91]])
    wreck(c,g,p)
    A.polygon(c,silt,[[0,h*.80],[w*.11,h*.85],[w*.26,h*.93],[w*.42,h*.86],[w*.57,h*.90],
        [w*.73,h*.84],[w*.86,h*.86],[w,h*.74],[w,h],[0,h]])
    var glint = A.mix(silt,p.ripple,.40)
    for (var k = 0; k < 190; ++k) {
        var sandX = A.noise(k+161), sandY = (.79+A.noise(k+234)*.22)*h
        if (sandY < h*(.86+.075*Math.sin(Math.PI*sandX))) continue
        A.rect(c,k%4 ? slate : glint,sandX*w,sandY,(1+A.noise(k+331)*4)*s,1)
    }
    // 明暗交错的弧线让湖底的砂纹与上方的斜光束接得上。
    var sand = { x:w*.28, y:h*.94, s:s }
    A.shape(c,sand,glint,[[-24,-3],[-12,-5],[1,-4],[-7,-4],[-15,-3]])
    A.shape(c,sand,glint,[[6,-3],[17,-7],[33,-7],[22,-6],[14,-5]])
    A.shape(c,sand,slate,[[-5,5],[6,2],[23,1],[15,3],[7,3]])
    var stone = A.mix(deep,p.rock,.22)
    for (var n = 0; n < 8; ++n) {
        var rock = { x: w*[.01,.08,.32,.49,.51,.79,.90,.97][n], y:h*(.94+.04*A.noise(n+48)), s:s*(.35+A.noise(n+8)) }
        A.shape(c,rock,stone,[[-13,1],[-8,-6],[-1,-9],[7,-7],[12,0],[7,3],[-9,3]])
        A.shape(c,rock,slate,[[-10,-2],[-7,-6],[-1,-9],[6,-7],[0,-6]])
    }
    reeds(c,w*.046,h,s*1.45,-1,deep,vegetation,false)
    reeds(c,w*.925,h*.94,s*1.35,1,deep,vegetation,false)
    reeds(c,w*.98,h*.9,s*1.65,-1,stone,deep,false)
    A.rect(c,p.ripple,0,g.surfaceY,w,1)
}

function drawStatic(c, g, p, weather) {
    c.reset()
    c.clearRect(0,0,g.w,g.h)
    shore(c,g,p,weather)
    if (g.expanded) underwater(c,g,p,weather)
    else if (!g.minimal) {
        var s = Math.min(g.h/50, g.w/110)
        reeds(c,g.w*.047,g.h+1,s,1,p.tree,p.leaf,true)
        reeds(c,g.w*.97,g.h+3,s*.75,-1,p.forest,p.grass,true)
    }
}

function fish(c, g, p, motion, time) {
    var swimmer = { x: Math.round(g.w*motion.fishX),
        y: Math.round(g.surfaceY+(g.h-g.surfaceY)*motion.fishDepth), s:Math.min(g.w/320,g.h/180) }
    var dark = A.mix(p.forest,"153c49",.65), fin = A.mix(p.wood,"bf8358",.60)
    var scales = A.mix("ecad70",p.wood,.53*p.night)
    var pearl = A.mix(p.snow,"d2c39c",.63)
    var beat = Math.round(2*Math.sin(time*4.8))
    A.shape(c,swimmer,dark,[[-1,-2],[-8,-6],[-16,-7],[-22,-5],[-27,-1],[-33,-6+beat],
        [-32,beat],[-34,6+beat],[-26,2],[-18,6],[-9,6],[-2,3],[1,1]])
    A.shape(c,swimmer,fin,[[-32,-4+beat],[-26,0],[-32,4+beat],[-31,beat]])
    A.shape(c,swimmer,fin,[[-20,-4],[-19,-11],[-17,-8],[-15,-10],[-12,-6]])
    A.shape(c,swimmer,scales,[[-26,0],[-21,-4],[-14,-5],[-8,-4],[-2,-1],[-1,1],[-10,4],[-18,3]])
    A.shape(c,swimmer,pearl,[[-23,1],[-16,2],[-6,1],[-2,0],[-3,3],[-10,5],[-17,4]])
    for (var i = 0; i < 3; ++i)
        A.shape(c,swimmer,fin,[[-22+i*5,-4],[-19+i*5,-5],[-18+i*5,-1],[-20+i*5,0]])
    A.shape(c,swimmer,fin,[[-12,2],[-9,2],[-13,8],[-15,7]])
    A.part(c,swimmer,pearl,-6,-4,3,3)
    A.part(c,swimmer,dark,-5,-4,2,2)
}

function floatAndRipple(c, g, p, motion, time) {
    var scale = g.expanded ? 1 : Math.max(.46,Math.min(1.2,g.h/57,g.w/110))
    var wave = Math.sin(time*1.6) * scale
    var y = Math.round(g.surfaceY + 5*scale*motion.dip + wave)
    var x = g.floatX, red = A.mix("ed6550",p.tile,.43*p.night)
    var ivory = A.mix(p.sun,p.snow,.56)
    if (!g.minimal) {
        A.rect(c,p.water,x-6*scale,y+scale,13*scale,1)
        if (motion.ripple >= 0) {
            var span = (6+27*motion.ripple)*scale
            c.globalAlpha = (1-motion.ripple)*.92
            for (var side = -1; side <= 1; side += 2) {
                A.rect(c,p.ripple,x+side*span-3*scale,g.surfaceY+1,6*scale,1)
                A.rect(c,p.ripple,x+side*span*.77-2*scale,g.surfaceY+2*scale,4*scale,1)
            }
            c.globalAlpha = 1
        }
    }
    var f = { x:x, y:y, s:scale }
    if (g.expanded) {
        c.globalAlpha = .52
        A.part(c,f,p.ripple,0,2,1,9)
        A.part(c,f,p.sun,-2,10,3,1)
        c.globalAlpha = 1
    }
    A.part(c,f,red,-1,-12,2,6)
    A.part(c,f,ivory,-1,-7,2,4)
    A.shape(c,f,p.trim,[[-2,-5],[2,-5],[4,-2],[3,1],[1,3],[-2,2],[-4,-1]])
    A.part(c,f,red,-2,-4,4,3)
    A.part(c,f,ivory,-3,-1,6,2)
    A.part(c,f,p.wood,-1,1,2,2)
}

function drawDynamic(c, g, p, weather, time, meteor) {
    c.reset()
    c.clearRect(0,0,g.w,g.h)
    if (g.minimal) {
        floatAndRipple(c,g,p,encounter(0),0)
        return
    }
    c.save()
    c.beginPath()
    c.rect(0,0,g.w,g.horizon)
    c.clip()
    A.drawAmbience(c,g.sky,p,weather,time,meteor)
    A.drawPrecipitation(c,g.sky,p,weather,time)
    c.restore()
    var motion = encounter(time)
    if (g.expanded) {
        fish(c,g,p,motion,time)
        // 银鱼群只在中层洄游，与靠近浮标的大鱼保持不同的体型和速度。
        var shoal = A.mix(p.water,p.ripple,.42)
        for (var i = 0; i < 7; ++i) {
            var sx = g.w*(.23+.13*A.noise(i+14)) + 8*Math.sin(time*.23+i*.18)
            var sy = g.h*(.59+.09*A.noise(i+63)) + Math.sin(time+i)*1.5
            A.rect(c,shoal,sx,sy,4+i%2,1)
            A.rect(c,shoal,sx-1,sy-1,1,1)
            A.rect(c,p.water,sx+1,sy+1,2,1)
        }
        // 舷窗中的小鱼逆流悬停，身体始终留在窗口内。
        var x = Math.round(g.w*.69+8+Math.sin(time*1.3)), y = Math.round(g.h*.75)-11
        A.rect(c,p.glow,x-2,y,4,1)
        A.rect(c,p.ripple,x-3,y-1,1,2)
        for (var mote = 0; mote < 24; ++mote) {
            var my = g.surfaceY+5+(A.noise(mote+71)*(g.h-g.surfaceY)+time*.45)%(g.h-g.surfaceY)
            c.globalAlpha = .19+.20*A.noise(mote+70)
            A.rect(c,p.snow,(A.noise(mote+208)*g.w+time*.63)%g.w,my,1,1)
        }
        c.globalAlpha = 1
    }
    floatAndRipple(c,g,p,motion,time)
}
