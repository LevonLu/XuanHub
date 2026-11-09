// StreetArt.js — 暖色街角小店、雨棚和路灯倒影
.pragma library
.import "SceneArt.js" as A

function geometry(w,h,expanded) {
    var g=A.geometry(w,h,expanded)
    g.ground=Math.round(h*0.77);g.x=w*(w<100?0.5:0.58);g.y=g.ground
    g.s=Math.max(0.01,Math.min(g.s,Math.max(1,w-2)/100,(g.y-2)/72))
    return g
}
function shop(c,g,p,snow) {
    A.part(c,g,p.trim,-44,-67,88,68)
    A.part(c,g,p.wall,-42,-65,58,64);A.part(c,g,p.wood,16,-65,26,64)
    for(var brick=0;brick<8;brick++) {
        A.part(c,g,p.wood,-42,-60+brick*7,58,1)
        for(var joint=0;joint<4;joint++)A.part(c,g,p.tile,-38+joint*14+(brick%2)*5,-59+brick*7,1,5)
    }
    A.part(c,g,p.trim,-47,-69,94,4);A.part(c,g,snow?p.snow:p.tile,-47,-70,94,2)
    for(var win=0;win<3;win++) {
        A.part(c,g,p.trim,-34+win*25,-57,17,20)
        A.part(c,g,p.warm>0.2?p.glow:p.ripple,-32+win*25,-55,13,16)
        A.part(c,g,p.wood,-26+win*25,-55,1,16);A.part(c,g,p.wood,-32+win*25,-48,13,1)
        A.part(c,g,p.rock,-35+win*25,-37,19,2)
    }
    A.part(c,g,p.trim,-38,-29,49,27);A.part(c,g,p.glow,-36,-27,45,23)
    A.part(c,g,p.tile,-36,-12,45,2);A.part(c,g,p.trim,-14,-27,2,24)
    // 橱窗内的花盆、杯子和书架只用少量轮廓表达。
    for(var item=0;item<4;item++){A.part(c,g,p.wood,-31+item*10,-9,5,5);A.part(c,g,p.leaf,-32+item*10,-13,6,4)}
    A.part(c,g,p.trim,18,-30,19,31);A.part(c,g,p.glow,20,-27,14,20)
    A.part(c,g,p.wood,26,-27,1,20);A.part(c,g,p.glow,31,-3,1,1)
    A.shape(c,g,p.roof,[[-47,-32],[43,-32],[48,-24],[-51,-24]])
    for(var stripe=0;stripe<10;stripe++)A.part(c,g,stripe%2?p.cloud:p.tile,-49+stripe*10,-29,8,7)
    A.part(c,g,p.trim,-51,-22,101,2)
    if(snow)A.part(c,g,p.snow,-47,-33,91,2)
    A.part(c,g,p.trim,-7,-43,28,10);A.part(c,g,p.glow,-5,-41,24,6)
    for(var mark=0;mark<4;mark++)A.part(c,g,p.wood,-2+mark*5,-40,3,3)
    A.part(c,g,p.rock,-48,0,96,3)
    A.part(c,g,p.trim,57,-58,2,60);A.part(c,g,p.trim,51,-59,14,2)
    A.part(c,g,p.glow,52,-56,12,7);A.part(c,g,p.trim,51,-49,14,2)
    A.part(c,g,p.rock,53,0,10,2)
}
function drawStatic(c,g,p,weather) {
    c.reset();A.drawSky(c,g,p,weather)
    if(!g.minimal) {
        for(var block=0;block<12;block++) {
            var bw=g.w/9,bx=block*bw-bw/2,bh=g.h*(0.23+A.noise(block+13)*0.25)
            A.rect(c,block%2?p.far:p.ridge,bx,g.ground-bh,bw-2,bh)
            for(var win=0;win<4;win++)A.rect(c,p.horizon,bx+bw*0.25,g.ground-bh+4+win*6,2,2)
        }
        A.rect(c,p.mountain,0,g.ground-15*g.s,g.w,15*g.s)
        for(var x=0;x<g.w;x+=12)A.rect(c,p.rock,x,g.ground-14*g.s,1,14*g.s)
    }
    A.rect(c,p.trim,0,g.ground,g.w,g.h-g.ground)
    A.rect(c,weather==="snow"?p.snow:p.rock,0,g.ground,g.w,4*g.s)
    for(var y=g.ground+6*g.s;y<g.h;y+=Math.max(3,8*g.s)) {
        A.rect(c,p.rock,0,y,g.w,1)
        for(var tile=0;tile<g.w;tile+=Math.max(6,18*g.s))A.rect(c,p.rock,tile+(y%2)*4,y,1,4*g.s)
    }
    if(weather==="rain") {
        for(var reflection=0;reflection<12;reflection++) {
            var ry=g.ground+5+reflection*1.8*g.s
            A.rect(c,A.mix(p.glow,p.water,0.45+reflection*0.035),g.x-34*g.s+Math.sin(reflection)*3,ry,35*g.s,1)
            A.rect(c,A.mix(p.glow,p.water,0.55),g.x+55*g.s,ry,5*g.s,1)
        }
    }
    shop(c,g,p,weather==="snow")
    if(g.expanded) {
        A.part(c,g,p.wood,-66,-14,12,14);A.part(c,g,p.leaf,-71,-27,22,14)
        A.part(c,g,p.grassLight,-67,-32,14,9);if(weather==="snow")A.part(c,g,p.snow,-67,-32,14,2)
        A.part(c,g,p.trim,-85,-7,22,2);A.part(c,g,p.wood,-84,-5,2,6);A.part(c,g,p.wood,-65,-5,2,6)
    }
}
function drawDynamic(c,g,p,weather,time,meteor) {
    c.reset();c.clearRect(0,0,g.w,g.h);if(g.minimal)return
    // 街区只在屋顶之上保留云和星光，避免天气以外的覆盖层穿过建筑。
    c.save();c.beginPath();c.rect(0,0,g.w,Math.max(1,g.y-71*g.s));c.clip()
    A.drawAmbience(c,g,p,weather,time,meteor);c.restore()
    c.globalAlpha=0.13*(0.8+Math.sin(time*0.35)*0.2)
    A.shape(c,g,p.glow,[[52,-49],[64,-49],[77,1],[39,1]])
    c.globalAlpha=1
    A.part(c,g,p.trim,48,-38,15,1)
    A.part(c,g,p.wood,60+Math.round(Math.sin(time*0.8)),-37,2,8)
    A.part(c,g,p.tile,56+Math.round(Math.sin(time*0.8)),-32,10,13)
    A.part(c,g,p.glow,58+Math.round(Math.sin(time*0.8)),-29,6,2)
    if(weather==="rain") {
        c.globalAlpha=0.5
        for(var ripple=0;ripple<9;ripple++) {
            var rx=A.noise(ripple+670)*g.w,ry=g.ground+4+A.noise(ripple+94)*(g.h-g.ground-4)
            var radius=1+((time*0.65+ripple*0.33)%1)*4*g.s
            A.rect(c,p.ripple,rx-radius,ry,radius*2,1)
        }
    }
    c.globalAlpha=1;A.drawPrecipitation(c,g,p,weather,time)
}
