// RailwayArt.js — 麦田、轨道、电线杆与偶尔经过的列车
.pragma library
.import "SceneArt.js" as A

function geometry(w,h,expanded) {
    var g=A.geometry(w,h,expanded)
    g.ground=Math.round(h*0.68);g.rail=Math.round(h*0.73)
    g.x=w*(w<100?0.50:0.64);g.y=g.rail;g.s=Math.max(0.01,Math.min(g.s,Math.max(1,w-2)/65,(g.ground-2)/60))
    return g
}
function signal(c,g,p,snow) {
    A.part(c,g,p.trim,-2,-48,3,49);A.part(c,g,p.rock,-5,0,9,3)
    A.part(c,g,p.trim,-7,-47,12,22);A.part(c,g,p.wood,-5,-45,8,18)
    A.part(c,g,p.tile,-3,-43,4,4);A.part(c,g,p.glow,-3,-33,4,4)
    A.part(c,g,p.trim,-5,-49,8,2);if(snow)A.part(c,g,p.snow,-7,-49,12,2)
}
function drawStatic(c,g,p,weather) {
    c.reset();A.drawSky(c,g,p,weather)
    if(!g.minimal) {
        A.mountain(c,g,p,g.h*0.60,g.h*0.26,1230,p.far)
        A.mountain(c,g,p,g.h*0.66,g.h*0.16,1300,p.ridge)
        A.mountain(c,g,p,g.h*0.70,g.h*0.10,1430,p.forest)
    }
    var wheat=A.mix(p.earth,p.glow,0.4),bright=A.mix(p.grassLight,p.glow,0.45)
    A.rect(c,weather==="snow"?p.snow:wheat,0,g.ground,g.w,g.h-g.ground)
    if(!g.minimal) {
        for(var furrow=0;furrow<12;furrow++) {
            A.polygon(c,weather==="snow"?p.cloud:p.earth,[[g.w*(0.15+furrow*0.045),g.ground],[g.w*(-0.2+furrow*0.16),g.h],[g.w*(-0.19+furrow*0.16),g.h]])
        }
        for(var grain=0;grain<100;grain++) {
            var gx=A.noise(grain+210)*g.w,gy=g.ground+A.noise(grain+330)*(g.h-g.ground)
            A.rect(c,weather==="snow"?p.cloud:bright,gx,gy,1,2*g.s)
        }
        // 小站在后景，不与移动列车一起重画。
        var bx=g.w*0.28,by=g.ground
        A.rect(c,p.wall,bx,by-16*g.s,26*g.s,16*g.s)
        A.polygon(c,weather==="snow"?p.snow:p.roof,[[bx-3*g.s,by-16*g.s],[bx+13*g.s,by-27*g.s],[bx+30*g.s,by-16*g.s]])
        A.rect(c,p.lamp,bx+4*g.s,by-12*g.s,5*g.s,7*g.s)
        A.rect(c,p.trim,bx+16*g.s,by-12*g.s,6*g.s,12*g.s)
    }
    A.rect(c,p.rock,0,g.rail+1,g.w,5*g.s)
    for(var sleeper=0;sleeper<g.w;sleeper+=Math.max(3,7*g.s))A.rect(c,p.wood,sleeper,g.rail,2*g.s,7*g.s)
    A.rect(c,p.trim,0,g.rail,g.w,1);A.rect(c,p.cloud,0,g.rail+4*g.s,g.w,1)
    signal(c,g,p,weather==="snow")
    if(!g.minimal) {
        for(var pole=0;pole<4;pole++) {
            var px=g.w*(0.05+pole*0.29),py=g.ground-5*g.s,top=py-53*g.s
            A.rect(c,p.wood,px,top,2*g.s,58*g.s)
            A.rect(c,p.trim,px-7*g.s,top+5*g.s,16*g.s,2*g.s)
            for(var insulator=0;insulator<3;insulator++)A.rect(c,p.cloud,px-5*g.s+insulator*5*g.s,top+2*g.s,2*g.s,3*g.s)
            for(var wire=0;wire<18;wire++) {
                var wx=px+wire*g.w*0.29/18,wy=top+5*g.s+Math.sin(wire/18*Math.PI)*4*g.s
                A.rect(c,p.trim,wx,wy,g.w*0.29/18+1,1)
            }
        }
    }
}
function drawDynamic(c,g,p,weather,time,meteor) {
    c.reset();c.clearRect(0,0,g.w,g.h);if(g.minimal)return
    A.drawAmbience(c,g,p,weather,time,meteor)
    // 每九十秒用十二秒穿过画面。只从共享时间计算位置，隐藏后不补播。
    var pass=(time+4)%90
    if(pass<12) {
        var carWidth=43*g.s,total=carWidth*3,x=-total+(g.w+total)*pass/12
        for(var car=0;car<3;car++) {
            var cx=x+car*carWidth,cy=g.rail-19*g.s
            A.rect(c,p.trim,cx,cy,40*g.s,19*g.s)
            A.rect(c,p.tile,cx+1,cy+1,38*g.s,15*g.s)
            A.rect(c,weather==="snow"?p.snow:p.cloud,cx,cy,40*g.s,3*g.s)
            A.rect(c,p.earth,cx+1,cy+11*g.s,38*g.s,2*g.s)
            for(var win=0;win<4;win++)A.rect(c,p.window,cx+(4+win*9)*g.s,cy+5*g.s,6*g.s,5*g.s)
            A.rect(c,p.trim,cx+6*g.s,g.rail-3*g.s,5*g.s,4*g.s)
            A.rect(c,p.trim,cx+28*g.s,g.rail-3*g.s,5*g.s,4*g.s)
        }
        A.rect(c,p.glow,x+total-4*g.s,g.rail-10*g.s,2*g.s,3*g.s)
    }
    if(weather!=="snow")for(var head=0;head<16;head++) {
        var hx=A.noise(head+333)*g.w,hy=g.h-2-A.noise(head+650)*g.h*0.12
        A.rect(c,p.glow,hx+Math.round(Math.sin(time*0.7+head)),hy,1,2*g.s)
    }
    A.drawPrecipitation(c,g,p,weather,time)
}
