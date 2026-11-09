// LighthouseArt.js — 礁岸灯塔、海面层次与缓慢扫过的灯光
.pragma library
.import "SceneArt.js" as A

function geometry(w,h,expanded) {
    var g=A.geometry(w,h,expanded)
    g.horizon=Math.round(h*0.43);g.ground=Math.round(h*0.76)
    g.x=w*(w<100?0.51:0.69);g.y=g.ground;g.s=Math.max(0.01,Math.min(g.s,(g.y-2)/84))
    return g
}
function tower(c,g,p,snow) {
    A.shape(c,g,p.trim,[[-13,1],[-8,-56],[9,-56],[14,1]])
    A.shape(c,g,p.snow,[[-11,0],[-6,-55],[7,-55],[11,0]])
    A.shape(c,g,p.cloud,[[5,0],[3,-55],[7,-55],[11,0]])
    A.part(c,g,p.tile,-9,-23,19,9);A.part(c,g,p.roof,5,-23,6,9)
    A.part(c,g,p.trim,-13,-58,27,3);A.part(c,g,p.trim,-10,-70,21,12)
    A.part(c,g,p.lamp,-8,-68,17,8);A.part(c,g,p.trim,-1,-70,2,12)
    A.shape(c,g,p.roof,[[-14,-71],[0,-79],[14,-71]])
    A.part(c,g,p.trim,0,-82,1,5)
    A.part(c,g,p.trim,-17,-58,35,1);A.part(c,g,p.trim,-17,-54,35,2)
    for(var i=-17;i<=18;i+=7)A.part(c,g,p.trim,i,-58,1,5)
    A.part(c,g,p.trim,-3,-12,7,13);A.part(c,g,p.lamp,-2,-40,4,6)
    A.part(c,g,p.rock,-17,0,35,3)
    if(snow){A.part(c,g,p.snow,-14,-72,28,2);A.part(c,g,p.snow,-17,-59,35,1)}
}
function drawStatic(c,g,p,weather) {
    c.reset();A.drawSky(c,g,p,weather)
    A.rect(c,p.water,0,g.horizon,g.w,g.h-g.horizon)
    for(var band=0;band<9;band++) {
        var y=g.horizon+(g.h-g.horizon)*band/9
        A.rect(c,A.mix(p.water,p.sky,0.35*(1-band/9)),0,y,g.w,Math.max(1,(g.h-g.horizon)/9))
    }
    if(!g.minimal) {
        A.polygon(c,p.ridge,[[0,g.horizon],[g.w*0.06,g.horizon-4*g.s],[g.w*0.16,g.horizon-10*g.s],[g.w*0.29,g.horizon],[g.w*0.4,g.horizon+2]])
        for(var i=0;i<35;i++)A.rect(c,p.ripple,A.noise(i+60)*g.w,g.horizon+A.noise(i+160)*(g.h-g.horizon),2+A.noise(i)*6,1)
        // 远处帆船和岸边台阶保持静态，浪光在独立层中移动。
        var bx=g.w*0.25,by=g.horizon+g.h*0.18
        A.polygon(c,p.trim,[[bx-5*g.s,by],[bx+8*g.s,by],[bx+5*g.s,by+3*g.s],[bx-3*g.s,by+3*g.s]])
        A.rect(c,p.wood,bx,by-16*g.s,1,16*g.s)
        A.polygon(c,p.cloud,[[bx-1,by-16*g.s],[bx-1,by-2*g.s],[bx-10*g.s,by-2*g.s]])
    }
    A.polygon(c,p.rock,[[g.w*0.49,g.ground],[g.w*0.62,g.ground-5*g.s],[g.w,g.ground-7*g.s],[g.w,g.h],[g.w*0.59,g.h],[g.w*0.57,g.ground+7*g.s]])
    A.polygon(c,p.earth,[[g.w*0.49,g.ground],[g.w*0.62,g.ground-5*g.s],[g.w,g.ground-7*g.s],[g.w,g.ground+4*g.s],[g.w*0.56,g.ground+4*g.s]])
    A.polygon(c,weather==="snow"?p.snow:p.grassLight,[[g.w*0.5,g.ground-1],[g.w*0.62,g.ground-7*g.s],[g.w,g.ground-9*g.s],[g.w,g.ground-3*g.s],[g.w*0.56,g.ground+1]])
    tower(c,g,p,weather==="snow")
    if(!g.minimal) {
        for(var r=0;r<8;r++) {
            var rx=A.noise(r+2)*g.w*0.48,ry=g.h-3-A.noise(r+20)*g.h*0.13
            A.polygon(c,p.trim,[[rx-4,ry],[rx-2,ry-4*g.s],[rx+4,ry-5*g.s],[rx+7,ry]])
            A.rect(c,weather==="snow"?p.snow:p.rock,rx-2,ry-4*g.s,5,1)
        }
        for(var step=0;step<6;step++)A.rect(c,p.earth,g.x+14*g.s+step*3*g.s,g.y+3*g.s+step*2*g.s,7*g.s,1)
    }
}
function drawDynamic(c,g,p,weather,time,meteor) {
    c.reset();c.clearRect(0,0,g.w,g.h);if(g.minimal)return
    A.drawAmbience(c,g,p,weather,time,meteor)
    for(var wave=0;wave<24;wave++) {
        var y=g.horizon+((A.noise(wave+240)+time*0.012)%1)*(g.h-g.horizon)
        var x=A.noise(wave+550)*g.w*0.53+Math.sin(time*0.45+wave)*3
        c.globalAlpha=0.3+0.2*Math.sin(time+wave)
        A.rect(c,p.ripple,x,y,3+(y-g.horizon)*0.18,1)
    }
    if(p.warm>0) {
        var originX=g.x,originY=g.y-64*g.s,direction=Math.sin(time*0.2)
        c.globalAlpha=0.12*p.warm
        A.polygon(c,p.glow,[[originX,originY],[originX+direction*g.w*0.7,originY-g.h*0.13],[originX+direction*g.w*0.7,originY+g.h*0.08]])
    } else if(weather==="clear") {
        c.globalAlpha=0.8
        for(var bird=0;bird<3;bird++) {
            var bx=(g.w*0.12+bird*12+time*1.2)%(g.w*0.43),by=g.h*0.25+Math.sin(time*0.6+bird)*2
            A.rect(c,p.cloud,bx,by,2,1);A.rect(c,p.cloud,bx+3,by,2,1);A.rect(c,p.cloud,bx+2,by+1,1,1)
        }
    }
    c.globalAlpha=1;A.drawPrecipitation(c,g,p,weather,time)
}
