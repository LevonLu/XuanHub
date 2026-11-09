// IslandArt.js — 云海浮岛、花园、瀑布与风车
.pragma library
.import "SceneArt.js" as A

function geometry(w,h,expanded) {
    var g=A.geometry(w,h,expanded)
    g.x=w*(w<100?0.50:0.57);g.y=h*0.59
    g.s=Math.max(0.01,Math.min(expanded?1:h/115,Math.max(1,w-2)/112,(g.y-2)/73,(h-g.y-2)/52))
    g.ground=Math.round(h*0.75)
    return g
}
function island(c,g,p,snow) {
    A.shape(c,g,p.trim,[[-56,0],[-45,22],[-27,33],[-16,51],[0,43],[16,48],[30,29],[46,18],[56,0]])
    A.shape(c,g,p.rock,[[-52,0],[-40,20],[-22,28],[-16,45],[-4,22],[8,5],[24,0]])
    A.shape(c,g,p.earth,[[4,4],[0,28],[16,40],[27,23],[48,7],[53,0]])
    A.shape(c,g,p.wood,[[-40,5],[-30,20],[-16,22],[-5,5]])
    A.shape(c,g,snow?p.snow:p.grass,[[-57,-2],[-40,-11],[29,-12],[57,-3],[49,6],[-43,7]])
    A.part(c,g,snow?p.cloud:p.grassLight,-40,-10,71,2)
    A.part(c,g,p.earth,-21,-6,54,3)
    // 风车的塔身静态缓存，四片叶轮在动态层中围绕同一轴心转动。
    A.shape(c,g,p.trim,[[-5,-9],[0,-47],[19,-47],[25,-9]])
    A.shape(c,g,p.wall,[[-2,-10],[2,-46],[17,-46],[22,-10]])
    A.shape(c,g,p.roof,[[-4,-47],[10,-61],[24,-47]])
    A.part(c,g,p.trim,6,-24,9,14);A.part(c,g,p.lamp,7,-23,7,12)
    A.part(c,g,p.trim,7,-42,7,7);A.part(c,g,p.lamp,8,-41,5,5)
    if(snow)A.shape(c,g,p.snow,[[-4,-48],[10,-62],[24,-48],[19,-49],[10,-58],[1,-49]])
    A.part(c,g,p.water,-31,4,6,39);A.part(c,g,p.ripple,-30,4,2,39)
    for(var fence=0;fence<5;fence++)A.part(c,g,p.wood,29+fence*4,-10,1,13)
    A.part(c,g,p.wood,28,-6,19,1)
    if(!snow)for(var flower=0;flower<9;flower++) {
        var fx=-43+A.noise(flower+15)*27,fy=-4+A.noise(flower+50)*6
        A.part(c,g,p.leaf,fx,fy-3,1,4);A.part(c,g,flower%2?p.glow:p.tile,fx-1,fy-4,3,2)
    }
}
function drawStatic(c,g,p,weather) {
    c.reset();A.drawSky(c,g,p,weather)
    if(!g.minimal) {
        // 小浮岛作为远景，使用更淡的山体色，避免与主岛抢轮廓。
        for(var distant=0;distant<3;distant++) {
            var dx=g.w*(0.1+distant*0.37),dy=g.h*(0.40+distant*0.055),ds=g.s*(0.5+distant*0.1)
            A.polygon(c,p.far,[[dx-17*ds,dy],[dx,dy+22*ds],[dx+20*ds,dy]])
            A.rect(c,p.ridge,dx-17*ds,dy-2*ds,37*ds,3*ds)
        }
        for(var bank=0;bank<9;bank++) {
            var bx=bank*g.w/8-15,by=g.h*(0.82+A.noise(bank+8)*0.10)
            A.cloud(c,p,bx,by,g.expanded?2.8:Math.max(0.4,g.h/45))
        }
    }
    island(c,g,p,weather==="snow")
}
function drawDynamic(c,g,p,weather,time,meteor) {
    c.reset();c.clearRect(0,0,g.w,g.h)
    if(!g.minimal)A.drawAmbience(c,g,p,weather,time,meteor)
    // 极窄图仍绘制静止叶轮轮廓；animatedDetails 保证不会逐帧刷新它。
    var angle=g.minimal?0.5:time*0.3
    for(var blade=0;blade<4;blade++) {
        var a=angle+blade*Math.PI/2,cos=Math.cos(a),sin=Math.sin(a)
        A.shape(c,g,p.cloud,[[10+cos*4,-43+sin*4],[10+cos*25-sin*2,-43+sin*25+cos*2],[10+cos*25+sin*3,-43+sin*25-cos*3],[10+cos*7+sin*2,-43+sin*7-cos*2]])
        A.shape(c,g,p.wood,[[10,-43],[10+cos*27,-43+sin*27],[10+cos*27+1,-42+sin*27],[11,-42]])
    }
    A.part(c,g,p.trim,8,-45,4,4)
    if(g.minimal)return
    for(var fall=0;fall<8;fall++) {
        var fy=4+(fall*6+time*9)%39
        A.part(c,g,p.cloud,-30,fy,2,2)
    }
    if(weather==="clear")for(var bird=0;bird<3;bird++) {
        var bx=(g.w*0.13+bird*13+time*0.9)%(g.w*0.34),by=g.h*0.28+Math.sin(time+bird)*2
        A.rect(c,p.cloud,bx,by,2,1);A.rect(c,p.cloud,bx+3,by+1,2,1)
    }
    A.drawPrecipitation(c,g,p,weather,time)
}
