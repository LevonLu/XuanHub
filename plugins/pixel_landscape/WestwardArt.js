// WestwardArt.js — 西行长卷的村落、竹径、石桥与山寺
.pragma library
.import "SceneArt.js" as A
.import "WestwardPilgrims.js" as Party

function geometry(w, h, expanded) {
    var g = A.geometry(w,h,expanded)
    g.roadY = Math.round(h*(expanded ? .68 : .78))
    g.surfaceY = Math.round(h*.85)
    g.horizon = Math.round(h*.49)
    // 栏很宽而矮，房屋和树干仍按高度等比缩放，山体与道路才铺满横幅。
    g.unit = Math.min(w/120, h/(expanded ? 180 : 100))
    return g
}

function silhouette(c,g,color,points) {
    A.polygon(c,color,points.map(function(pt) { return [pt[0]*g.w/320, pt[1]*g.h/180] }))
}

function sky(c,g,p,weather,phase) {
    c.clearRect(0,0,g.w,g.h)
    if (!p.solar) {
        var low = phase === "dusk" ? A.mix(p.horizon,"ffb578",.45) : p.horizon
        for (var band = 0; band < 22; ++band) {
            var t = band/21
            var color = t < .55 ? A.mix(p.sky,p.skyLow,t/.55) : A.mix(p.skyLow,low,(t-.55)/.45)
            A.rect(c,color,0,band*g.h/37,g.w,g.h/37+1)
        }
    }
    if (!p.solar && phase==="dusk" && weather==="clear") {
        var sx=Math.round(g.w*.19), sy=Math.round(g.h*.30), r=Math.max(2,Math.round(g.h*.076))
        // 晴晚的日轮嵌在西边两峰的垭口，逐行取整的圆弧保留像素边界。
        for (var ring=2;ring>=0;--ring) {
            var radius=r+ring*Math.max(1,Math.round(r*.32))
            c.globalAlpha=ring===0?1:ring===1?.16:.09
            for (var dy=-radius;dy<=radius;++dy) {
                var half=Math.floor(Math.sqrt(radius*radius-dy*dy))
                A.rect(c,ring?p.horizon:A.mix(p.sun,"ffdb89",.52),sx-half,sy+dy,2*half+1,1)
            }
        }
        c.globalAlpha=.56
        A.rect(c,p.horizon,sx-r-3,sy+Math.round(r*.54),r*1.36,1)
        c.globalAlpha=1
    } else {
        A.drawCelestial(c,g,p,weather,g.w*(phase==="dawn"?.78:.29),g.h*(phase==="dawn"?.26:.16))
    }
    if (phase === "night" || weather !== "clear") return
    for (var i = 0; i < 15; ++i) {
        var x = A.noise(i+63)*g.w, y = (.08+.32*A.noise(i+42))*g.h
        var span = (.03+.14*A.noise(i+145))*g.w
        c.globalAlpha = .20+.22*A.noise(i+17)
        A.rect(c,p.cloud,x-span/2,y,span,1)
        if (g.expanded) A.rect(c,p.cloud,x-span*.24,y-1,span*.48,1)
    }
    c.globalAlpha = 1
}

function bamboo(c,x,y,scale,lean,stem,leaf) {
    var b = {x:x,y:y,s:scale}
    A.shape(c,b,stem,[[-2,0],[-1,-30],[lean*.48,-59],[lean,-87],
        [lean+2,-87],[lean*.48+2,-58],[2,-29],[2,0]])
    for (var n = 1; n <= 5; ++n) {
        var yy = -n*14, xx = lean*(n/6)*(n/6), side = n%2 ? 1 : -1
        A.part(c,b,leaf,xx-1,yy,4,1)
        A.shape(c,b,stem,[[xx,yy],[xx+side*9,yy-8],[xx+side*17,yy-10],[xx+side*10,yy-6]])
        A.shape(c,b,leaf,[[xx+side*7,yy-6],[xx+side*14,yy-15],[xx+side*10,yy-7]])
        A.shape(c,b,leaf,[[xx+side*11,yy-8],[xx+side*24,yy-10],[xx+side*16,yy-6]])
        A.shape(c,b,leaf,[[xx+side*14,yy-8],[xx+side*21,yy-2],[xx+side*16,yy-4]])
    }
}

function eaves(c,b,roof,edge,y,span) {
    A.shape(c,b,roof,[[-span,y-3],[-span+3,y-1],[-span*.58,y-2],[-span*.22,y-8],
        [0,y-13],[span*.18,y-8],[span*.59,y-2],[span-4,y-1],[span,y-4],
        [span-1,y+1],[span*.58,y+4],[-span*.58,y+4],[-span+1,y+1]])
    A.shape(c,b,edge,[[-span,y-3],[-span+5,y],[-span*.56,y+1],[0,y],[span*.61,y+1],
        [span-3,y-1],[span,y-4],[span-1,y],[span*.61,y+3],[-span*.55,y+3],[-span+1,y]])
}

function house(c,b,p,lanterns,snow) {
    var plaster = A.mix(p.wall,p.snow,.52), timber = A.mix(p.wood,p.trim,.53)
    var tiles = A.mix(p.roof,p.forest,.38), edge = snow ? p.snow : A.mix(tiles,p.cloud,.43)
    A.part(c,b,plaster,-23,-23,46,23)
    A.part(c,b,timber,-24,-22,3,23)
    A.part(c,b,timber,20,-22,3,23)
    A.part(c,b,timber,-21,-17,41,2)
    A.part(c,b,timber,-5,-16,9,16)
    for (var i = 0; i < 2; ++i) {
        var x = i ? 8 : -17
        A.part(c,b,timber,x,-14,9,8)
        A.part(c,b,p.glow,x+1,-13,7,5)
        A.part(c,b,timber,x+4,-13,1,6)
        A.part(c,b,timber,x,-11,8,1)
    }
    eaves(c,b,tiles,edge,-24,32)
    if (lanterns) for (var side = -1; side <= 1; side += 2) {
        var lx = side*26
        A.part(c,b,timber,lx,-23,1,5)
        A.part(c,b,timber,lx-2,-19,5,1)
        A.part(c,b,"#bd4e39",lx-3,-18,7,6)
        A.part(c,b,p.glow,lx-1,-17,3,4)
        A.part(c,b,timber,lx-2,-12,5,1)
        A.part(c,b,p.glow,lx,-11,1,3)
    }
}

function road(c,g,face,top) {
    var y = g.roadY, h = g.h, w = g.w
    A.rect(c,top,0,y-h*.025,w,h*.12)
    A.rect(c,face,0,y+h*.08,w,h)
    A.rect(c,A.mix(face,top,.55),0,y+h*.08,w,Math.max(1,h*.015))
}

function verge(c,g,p,phase) {
    var y=g.roadY, h=g.h, w=g.w, rock=p.rock, moss=p.leaf
    var darkness=phase==="night"
    if (darkness) {
        rock=A.mix(p.rock,"273e53",.62)
        moss=A.mix(p.tree,"142e3c",.54)
    }
    var bottom=A.mix(rock,moss,.59), soil=A.mix(bottom,p.earth,darkness?.14:.47)
    // 近坡在画面两端升高；路肩有可读的厚度，中间露出远一点的地块。
    var contour=[[0,.13],[.07,.10],[.18,.16],[.29,.18],[.36,.23],[.46,.25],
        [.62,.22],[.73,.18],[.80,.20],[.90,.12],[1,.09]]
    var rim=contour.map(function(v){return [w*v[0],y+h*v[1]]})
    A.polygon(c,soil,rim.concat([[w,h],[0,h]]))
    A.polygon(c,bottom,rim.map(function(v,i){return [v[0],v[1]+h*(.02+.019*(i%3))]}).concat([[w,h],[0,h]]))
    for (var i=0;i<62;++i) {
        var position=A.noise(i+578), level=.12+A.noise(i+79)*.30
        var at=0
        while (at+2<contour.length && position>contour[at+1][0]) ++at
        var a=contour[at], b=contour[at+1]
        var bank=a[1]+(b[1]-a[1])*(position-a[0])/(b[0]-a[0])
        if (level<bank) continue
        var x=position*w, yy=y+h*level, size=g.unit*(.4+.8*A.noise(i+412))
        A.rect(c,i%3?A.mix(soil,rock,.43):p.forest,x,yy,(2+4*A.noise(i+11))*size,1)
        if (i%6===0) {
            var tuft={x:x,y:yy,s:size}
            A.shape(c,tuft,moss,[[-3,0],[-5,-5],[-1,-2],[0,-8],[1,-4],[5,-6],[2,0]])
            if (phase==="dawn") A.part(c,tuft,p.glow,-1,-7,2,2)
        }
    }
}

function dawnVillage(c,g,p,weather) {
    var w=g.w,h=g.h,y=g.roadY,s=g.unit
    silhouette(c,g,p.far,[[0,64],[16,47],[28,52],[49,27],[58,33],[63,47],[76,60],
        [96,44],[112,38],[121,56],[145,64],[166,39],[177,46],[190,67],[217,58],[230,45],
        [250,51],[270,29],[280,43],[300,53],[320,42],[320,180],[0,180]])
    var sunward=A.mix(p.far,p.horizon,.36)
    silhouette(c,g,sunward,[[49,27],[58,33],[63,47],[74,58],[61,54],[55,44],[58,45],[52,33]])
    silhouette(c,g,sunward,[[166,39],[177,46],[190,67],[179,61],[174,51],[176,54]])
    silhouette(c,g,sunward,[[270,29],[280,43],[300,53],[281,48],[277,43],[274,42]])
    var mist=A.mix(p.horizon,p.far,.30)
    silhouette(c,g,mist,[[0,74],[43,69],[75,73],[106,65],[142,68],[191,61],
        [218,68],[265,63],[320,69],[320,180],[0,180]])
    silhouette(c,g,p.mountain,[[0,84],[24,76],[53,81],[75,92],[120,88],[153,75],
        [187,80],[218,87],[254,78],[279,66],[300,74],[320,62],[320,180],[0,180]])
    var greenery=A.mix(p.grass,p.grassLight,.49)
    A.rect(c,greenery,0,y-h*.15,w,h)
    for (var i=0;i<5;++i) {
        var level=y-h*(.14-i*.022)
        A.rect(c,i%2 ? p.grass : p.grassLight,0,level,w,1)
    }
    house(c,{x:w*.14,y:y-h*.06,s:s*.65},p,false,weather==="snow")
    house(c,{x:w*.065,y:y-h*.055,s:s*.69},p,false,weather==="snow")
    house(c,{x:w*.80,y:y-h*.10,s:s*.80},p,false,weather==="snow")
    house(c,{x:w*.89,y:y-h*.034,s:s*1.15},p,true,weather==="snow")
    bamboo(c,w*.95,y-h*.07,s*.82,-9,p.forest,p.leaf)
    bamboo(c,w*.995,y,s*.97,6,p.tree,p.forest)
    road(c,g,A.mix(p.earth,p.wood,.55),weather==="snow"?p.snow:A.mix(p.earth,p.horizon,.58))
    verge(c,g,p,"dawn")
}

function bambooDay(c,g,p,weather) {
    var w=g.w,h=g.h,y=g.roadY,s=g.unit
    var haze=A.mix(p.horizon,"a4bf9e",.53), far=A.mix(p.ridge,haze,.51)
    silhouette(c,g,far,[[0,49],[23,60],[42,48],[66,66],[102,77],[140,65],[159,79],[190,66],
        [215,69],[241,56],[269,64],[292,40],[320,51],[320,180],[0,180]])
    var sage=A.mix(p.leaf,haze,.57)
    var stem=A.mix(p.tree,"416e52",.43)
    for (var i=0;i<(g.expanded?23:13);++i) {
        var x=A.noise(i+303)*w
        var size=(.48+.47*A.noise(i+302))*s
        bamboo(c,x,y-h*.06,size,-9+17*A.noise(i+305),sage,A.mix(sage,p.leaf,.30))
    }
    silhouette(c,g,p.leaf,[[0,100],[28,95],[60,98],[100,108],[150,105],[194,112],
        [238,103],[290,91],[320,94],[320,180],[0,180]])
    for (var j=0;j<8;++j) {
        var slot=[.025,.065,.132,.25,.72,.82,.92,.98][j]
        bamboo(c,w*slot,y-h*(.015+.04*(j%3)),s*(.87+.45*A.noise(j+88)),
            (j%2 ? -1:1)*(6+j),stem,A.mix(p.forest,p.leaf,.44))
    }
    var rock=A.mix(p.rock,p.forest,.36)
    road(c,g,rock,weather==="snow"?p.snow:A.mix(p.rock,p.horizon,.48))
    verge(c,g,p,"day")
    // 平台延伸成前景山阶，行脚处仍是连续平坦的一段。
    for (var step=0;step<4;++step) {
        var stairY=y+h*(.11+step*.065), start=w*(.08-step*.052), end=w*(.54+step*.062)
        A.rect(c,A.mix(rock,p.forest,.25),start,stairY,end-start,Math.max(1,h*.059))
        A.rect(c,A.mix(rock,p.horizon,.34),start,stairY,end-start,Math.max(1,h*.021))
        for (var joint=0;joint<4+step;++joint) {
            var seam=start+(joint+.28+.12*(step%2))*(end-start)/(4+step)
            A.rect(c,rock,seam,stairY,1,Math.max(1,h*.035))
            A.rect(c,p.leaf,seam+1,stairY+1,2,1)
        }
    }
}

function duskBridge(c,g,p,weather) {
    var w=g.w,h=g.h,y=g.roadY,waterY=g.surfaceY
    var distant=A.mix(p.far,p.horizon,.27), cliff=A.mix(p.mountain,p.roof,.28)
    silhouette(c,g,distant,[[0,79],[22,66],[44,70],[58,76],[72,69],[83,52],[89,58],[98,77],[126,71],[145,81],
        [175,66],[191,73],[216,69],[246,54],[260,61],[274,73],[295,69],[320,60],[320,180],[0,180]])
    silhouette(c,g,cliff,[[0,92],[26,85],[43,92],[75,89],[98,97],[141,90],[174,97],
        [198,93],[216,99],[233,87],[255,88],[278,69],[292,72],[309,61],[320,65],[320,180],[0,180]])
    var water=A.mix(p.water,p.horizon,.31)
    for (var band=0;band<10;++band)
        A.rect(c,A.mix(water,p.ridge,.02+.24*band/9),0,h*(.55+.045*band),w,h*.047+1)
    for (var i=0;i<(g.expanded?105:42);++i) {
        var xx=A.noise(i+435), yy=A.noise(i+611)
        var sunlit=weather==="clear" && (p.solar ? i%7===0 : Math.abs(xx-.19)<.035+yy*.12)
        A.rect(c,sunlit?A.mix(p.sun,water,.34):A.mix(p.ridge,water,.59),xx*w,(.56+.44*yy)*h,1+yy*A.noise(i+42)*14,1)
    }
    var stone=A.mix(p.rock,p.roof,.52), edge=A.mix(stone,p.horizon,.54), shade=A.mix(stone,p.trim,.63)
    A.rect(c,stone,0,y,w,waterY-y+1)
    var brickW=Math.max(5,12*g.unit), brickH=Math.max(3,5*g.unit)
    var grout=A.mix(stone,shade,.48), lip=A.mix(stone,edge,.36)
    for (var course=0;y+course*brickH<=waterY;++course) {
        var seamY=y+course*brickH
        A.rect(c,grout,0,seamY,w,1)
        for (var seam=(course%2)*brickW/2;seam<w;seam+=brickW) {
            A.rect(c,grout,seam,seamY,1,brickH)
            A.rect(c,lip,seam+1,seamY+1,brickW*.48,1)
        }
    }
    // 在石身上按扫描线画出三个拱的水面，再沿弧顶逐块镶上拱石。
    for (var arch=0;arch<3;++arch) {
        var cx=w*(.5+(arch-1)*.349), rx=w*(arch===1?.208:.114)
        var ry=Math.max(1,(waterY-y)*(arch===1?.8:.52))
        for (var row=0;row<=Math.ceil(ry);++row) {
            var reach=rx*Math.sqrt(Math.max(0,1-(row/ry)*(row/ry)))
            A.rect(c,water,cx-reach,waterY-row,2*reach,1)
            if (row>1) {
                A.rect(c,shade,cx-reach-2,waterY-row,2,1)
                A.rect(c,shade,cx+reach,waterY-row,2,1)
            }
        }
        for (var brick=0;brick<14;++brick) {
            var angle=(brick+.5)*Math.PI/14, delta=Math.PI/33
            var inner=ry+1, outer=ry+Math.max(2,g.unit*4)
            A.polygon(c,brick%3?lip:edge,[[cx-(rx+1)*Math.cos(angle-delta),waterY-inner*Math.sin(angle-delta)],
                [cx-(rx+1)*Math.cos(angle+delta),waterY-inner*Math.sin(angle+delta)],
                [cx-(rx+3)*Math.cos(angle+delta),waterY-outer*Math.sin(angle+delta)],
                [cx-(rx+3)*Math.cos(angle-delta),waterY-outer*Math.sin(angle-delta)]])
        }
    }
    A.rect(c,shade,0,y-1,w,2)
    A.rect(c,weather==="snow"?p.snow:edge,0,y-2,w,2)
    var postH=Math.max(2,h*.049), foot=y-2
    A.rect(c,edge,0,foot-postH*.70,w,Math.max(1,h*.011))
    for (var n=0;n<=12;++n) {
        var px=(n+.15)*w/12, sw=Math.max(1,g.unit*2)
        A.rect(c,stone,px,foot-postH,sw,postH)
        A.rect(c,edge,px-1,foot-postH-1,sw+2,2)
    }
}

function nightTemple(c,g,p,weather) {
    var w=g.w,h=g.h,y=g.roadY,s=g.unit
    var blue=A.mix(p.far,"516581",.42), ridge=A.mix(p.mountain,"304762",.56)
    silhouette(c,g,blue,[[0,67],[16,51],[25,54],[42,33],[48,36],[66,64],[76,54],[90,61],
        [111,39],[119,33],[128,52],[137,54],[148,70],[166,61],[187,74],[200,45],[210,46],
        [220,34],[235,65],[260,55],[285,69],[302,46],[320,40],[320,180],[0,180]])
    silhouette(c,g,ridge,[[0,74],[23,65],[51,66],[70,77],[98,86],[120,74],[144,81],
        [169,70],[187,83],[205,75],[220,84],[267,65],[281,71],[301,53],[320,43],[320,180],[0,180]])
    var moonface=A.mix(blue,p.snow,.20)
    silhouette(c,g,moonface,[[111,39],[119,33],[128,52],[137,54],[143,65],[130,59],[124,47],[121,48],[119,39]])
    silhouette(c,g,moonface,[[202,46],[210,46],[220,34],[235,65],[226,53],[224,54],[220,40],[213,48]])
    var temple={x:w*.30,y:h*.38,s:s*.70}
    c.globalAlpha=.08
    A.part(c,temple,p.glow,-25,-37,50,32)
    A.part(c,temple,p.glow,-30,-28,61,19)
    c.globalAlpha=1
    A.part(c,temple,p.trim,-15,-14,30,15)
    for (var level=0;level<3;++level) {
        var breadth=level===0?23:level===1?17:11, yy=-12-level*11
        A.part(c,temple,p.wood,-breadth*.38,yy-8,breadth*.76,9)
        eaves(c,temple,p.tree,A.mix(blue,p.glow,.26),yy+4,breadth)
        A.part(c,temple,p.glow,-breadth*.38,yy+7,breadth*.76,3)
        A.part(c,temple,p.trim,-1,yy+7,2,3)
    }
    A.part(c,temple,p.trim,-1,-48,2,11)
    A.part(c,temple,p.glow,-1,-49,2,2)
    // 山道自寺前迂回几折，接入近处队伍的路面。
    var track=A.mix(blue,p.grassLight,.28)
    silhouette(c,g,track,[[94,68],[87,72],[80,74],[72,76],[99,78],[108,81],[95,85],[81,87],[84,89],[129,91],[151,96],
        [140,102],[102,108],[69,116],[32,136],[0,139],[0,131],[60,116],[97,106],[139,99],
        [142,97],[126,94],[79,91],[74,88],[80,85],[100,81],[95,79],[66,77],[72,74],[87,71]])
    var nightGrass=A.mix(p.forest,"183341",.55)
    silhouette(c,g,nightGrass,[[0,112],[21,107],[46,116],[66,120],[102,112],[130,115],
        [168,103],[194,104],[230,99],[252,110],[285,105],[320,92],[320,180],[0,180]])
    road(c,g,A.mix(p.earth,nightGrass,.73),weather==="snow"?p.snow:A.mix(p.rock,ridge,.52))
    verge(c,g,p,"night")
}

function drawStatic(c,g,p,weather,phase) {
    // 宿主用裁剪边界揭开相邻时段，需继承外层 clip。
    c.save()
    sky(c,g,p,weather,phase)
    if (phase==="dawn") dawnVillage(c,g,p,weather)
    else if (phase==="dusk") duskBridge(c,g,p,weather)
    else if (phase==="night") nightTemple(c,g,p,weather)
    else bambooDay(c,g,p,weather)
    c.restore()
}

function foreground(c,g,p,phase,time) {
    // 西行时近岸与路肩向右掠过；每株草留出整幅宽度的余量后才从左边重入。
    var w=g.w,h=g.h,s=g.unit, dark=A.mix(p.tree,p.trim,.34)
    var spacing=Math.max(30*s,w/5), cycle=spacing*5
    for (var i=0;i<5;++i) {
        var x=((i*spacing+time*s*9)%(cycle+30*s))-15*s
        var base=h*(.96+.04*A.noise(i+51))
        var sprig={x:x,y:base,s:s*(.6+A.noise(i+30)*.45)}
        if (phase==="dusk") {
            if (i%2) continue
            A.shape(c,sprig,dark,[[-2,5],[-1,-2],[-6,-11],[0,-6],[2,-11],[1,4]])
        } else {
            A.shape(c,sprig,dark,[[-7,2],[-5,-2],[-1,-4],[4,-3],[7,1],[4,3]])
            A.part(c,sprig,p.rock,-4,-2,5,1)
            A.shape(c,sprig,p.forest,[[2,2],[1,-5],[4,-2],[7,-8],[5,2]])
        }
    }
}

function passingRoad(c,g,p,phase,time) {
    var w=g.w,h=g.h,y=g.roadY,s=g.unit
    var travel=time*s*15, step=Math.max(8,26*s)
    if (phase==="dusk") {
        // 桥体的三孔是远景地标；脚下这排石板伸向画外，接缝越过行脚点时可读出西行方向。
        var roadLight=A.mix(p.rock,p.horizon,.42), joint=A.mix(p.trim,p.rock,.35)
        var top=y-2, bottom=y+Math.max(1,Math.round(s*3))
        A.rect(c,roadLight,0,top,w,bottom-top)
        for (var bx=travel%step-step;bx<w+step;bx+=step) {
            A.polygon(c,joint,[[bx,top],[bx+1,top],[bx+6*s,bottom],[bx+4*s,bottom]])
            A.rect(c,p.rock,bx+5*s,bottom-1,Math.max(1,step*.48),1)
        }
    } else {
        var grit=A.mix(p.rock,p.earth,.4)
        for (var i=0;i<25;++i) {
            var x=(A.noise(i+111)*w+travel)%(w+12)-6
            A.rect(c,i%3?grit:p.rock,x,y+h*(.015+.053*A.noise(i+91)),1+A.noise(i+12)*5*s,1)
        }
    }
}

function wayside(c,g,p,phase,time) {
    if (phase==="dusk") return
    // 道旁的物体每次随镜头从左掠向右，在两端完全出画后才回绕。
    var s=g.unit, pad=30*s, cycle=g.w+2*pad
    var speed=(g.expanded?21:26)*s, x=(time*speed+pad*.30)%cycle-pad
    var b={x:x, y:g.roadY+g.h*.09, s:s}
    var ink=A.mix(p.trim,p.tree,.43), lit=A.mix(p.rock,p.horizon,.4)
    if (phase==="day") {
        // 近竹只占少量横向空间，清晰的竹节偶尔从队伍前划过。
        bamboo(c,x,b.y,s*.90,13,A.mix(p.tree,"264b45",.48),p.forest)
        bamboo(c,x-8*s,b.y,s*.57,-6,p.forest,p.leaf)
    } else if (phase==="dawn") {
        A.shape(c,b,ink,[[-5,0],[-5,-14],[-2,-18],[3,-18],[5,-14],[5,0]])
        A.shape(c,b,lit,[[-4,-1],[-4,-14],[-1,-17],[2,-17],[3,-14],[3,-1]])
        A.part(c,b,p.wood,-3,-13,7,3)
        A.part(c,b,p.tile,-1,-9,2,4)
        A.part(c,b,ink,-7,-1,14,3)
    } else {
        A.part(c,b,ink,-7,-2,14,3)
        A.part(c,b,p.rock,-3,-11,6,10)
        A.part(c,b,ink,-5,-17,10,8)
        c.globalAlpha=.12
        A.part(c,b,p.glow,-8,-19,16,11)
        c.globalAlpha=1
        A.part(c,b,p.glow,-3,-16,6,5)
        A.part(c,b,p.wood,0,-16,1,5)
        eaves(c,b,ink,lit,-19,8)
    }
}

function drawDynamic(c,g,p,weather,time,meteor,phase) {
    c.save()
    c.clearRect(0,0,g.w,g.h)
    if (g.minimal) {
        Party.draw(c,g,p,0,g.roadY,false,phase)
        c.restore()
        return
    }
    // 云与星在山峦后面，只把动态天空叠在开阔的上方。
    c.save()
    c.beginPath(); c.rect(0,0,g.w,g.h*.23); c.clip()
    A.drawAmbience(c,g,p,weather,time,meteor)
    c.restore()
    if (phase==="dusk") {
        c.save()
        c.beginPath(); c.rect(0,g.surfaceY+1,g.w,g.h-g.surfaceY); c.clip()
        var squash=.49
        c.translate(0,g.surfaceY+1+g.roadY*squash)
        c.scale(1,-squash)
        Party.draw(c,g,p,time,g.roadY,true,phase)
        c.restore()
        for (var i=0;i<14;++i) {
            var t=A.noise(i+22), xx=(t*g.w+time*(.7+(i%3)))%(g.w+18)-9
            A.rect(c,i%3?p.ripple:p.horizon,xx,g.surfaceY+(g.h-g.surfaceY)*A.noise(i+56),
                2+6*A.noise(i+15),1)
        }
    }
    passingRoad(c,g,p,phase,time)
    Party.draw(c,g,p,time,g.roadY,false,phase)
    if (phase==="day" || phase==="night" && weather==="clear") {
        for (var j=0;j<(g.expanded?8:4);++j) {
            var xx2=(A.noise(j+115)*g.w+time*(1.1+j*.20))%(g.w+12)-6
            var yy2=g.h*(.31+.27*A.noise(j+48)) + Math.sin(time*.7+j)*2
            c.globalAlpha=phase==="day"?.68:.2+.5*(.5+.5*Math.sin(time*1.5+j))
            A.rect(c,phase==="day"?p.leaf:p.glow,xx2,yy2,phase==="day"?3:1,1)
        }
        c.globalAlpha=1
    }
    wayside(c,g,p,phase,time)
    foreground(c,g,p,phase,time)
    A.drawPrecipitation(c,g,p,weather,time)
    c.restore()
}
