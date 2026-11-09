// CampsiteArt.js — 湖畔帐篷、篝火与固定数量的萤火虫
.pragma library
.import "SceneArt.js" as A

function geometry(w,h,expanded) {
    var g=A.geometry(w,h,expanded)
    g.ground=Math.round(h*0.76);g.lake=Math.round(h*0.49)
    g.x=w*(w<100?0.46:0.65);g.y=g.ground;g.s=Math.min(g.s,Math.max(1,w-2)/82)
    return g
}
function tent(c,g,p,snow) {
    A.shape(c,g,p.trim,[[-36,1],[-12,-40],[29,-36],[45,1]])
    A.shape(c,g,p.tile,[[-33,-1],[-12,-38],[25,-35],[41,-1]])
    A.shape(c,g,p.glow,[[-33,-1],[-12,-38],[11,-1]])
    A.shape(c,g,p.wood,[[-25,-1],[-12,-28],[3,-1]])
    A.shape(c,g,p.lamp,[[-21,-1],[-12,-23],[-5,-1]])
    A.part(c,g,p.trim,-13,-43,2,44)
    A.part(c,g,p.earth,-38,1,86,3)
    A.shape(c,g,p.cloud,[[28,-34],[29,-34],[51,2],[50,2]])
    A.part(c,g,p.trim,50,0,2,5)
    if(snow)A.shape(c,g,p.snow,[[-34,-1],[-12,-40],[29,-36],[32,-30],[-11,-36],[-29,-1]])
    A.part(c,g,p.trim,28,-23,2,24);A.part(c,g,p.lamp,25,-21,8,10)
    A.part(c,g,p.trim,25,-22,8,2);A.part(c,g,p.trim,25,-11,8,2)
    // 火盆和柴堆是静态地形；暂停时动态火焰保持最后一帧。
    A.part(c,g,p.rock,-66,5,19,3);A.part(c,g,p.wood,-63,2,14,3)
    A.part(c,g,p.trim,-67,7,21,1)
}
function drawStatic(c,g,p,weather) {
    c.reset();A.drawSky(c,g,p,weather)
    if(!g.minimal) {
        A.mountain(c,g,p,g.h*0.48,g.h*0.24,410,p.far)
        A.mountain(c,g,p,g.h*0.55,g.h*0.17,504,p.mountain)
        for(var ti=0;ti<Math.ceil(g.w/6);ti++)A.pine(c,{tree:p.forest,leaf:p.forest,wood:p.forest},ti*6,g.lake,7+A.noise(ti+81)*g.h*0.13,false)
    }
    A.rect(c,p.water,0,g.lake,g.w,g.h-g.lake)
    for(var line=0;line<12;line++) {
        var yy=g.lake+(g.ground-g.lake)*line/12
        A.rect(c,A.mix(p.forest,p.water,0.35+line*0.05),0,yy,g.w,1)
    }
    A.polygon(c,weather==="snow"?p.snow:p.grass,[[0,g.h*0.92],[g.w*0.23,g.h*0.86],[g.w*0.44,g.ground],[g.w*0.65,g.ground-4*g.s],[g.w,g.ground-2*g.s],[g.w,g.h],[0,g.h]])
    A.polygon(c,p.earth,[[g.w*0.39,g.ground+4*g.s],[g.w*0.58,g.ground-2*g.s],[g.w,g.ground-1],[g.w,g.ground+2],[g.w*0.58,g.ground+1]])
    if(!g.minimal) {
        for(var f=0;f<65;f++) {
            var fx=g.w*(0.49+A.noise(f+10)*0.51),fy=g.ground+4+A.noise(f+300)*(g.h-g.ground-4)
            A.rect(c,weather==="snow"?p.cloud:p.grassLight,fx,fy,2,1)
        }
        for(var t=0;t<5;t++)A.pine(c,p,g.w*(0.82+t*0.045),g.ground,22*g.s+A.noise(t+8)*20*g.s,weather==="snow")
        // 短木栈桥伸向湖水，与帐篷构成两处视觉锚点。
        for(var plank=0;plank<8;plank++)A.rect(c,p.wood,g.w*0.24+plank*3*g.s,g.ground-3*g.s+plank*g.s,9*g.s,2*g.s)
        A.rect(c,p.trim,g.w*0.26,g.ground-2*g.s,2*g.s,8*g.s)
    }
    tent(c,g,p,weather==="snow")
    if(g.expanded){A.pine(c,p,10,g.h+5,60,weather==="snow");A.pine(c,p,g.w-5,g.h+8,74,weather==="snow")}
}
function drawDynamic(c,g,p,weather,time,meteor) {
    c.reset();c.clearRect(0,0,g.w,g.h);if(g.minimal)return
    A.drawAmbience(c,g,p,weather,time,meteor)
    for(var ripple=0;ripple<20;ripple++) {
        var ry=g.lake+((A.noise(ripple+150)+time*0.01)%1)*(g.ground-g.lake)
        A.rect(c,p.ripple,A.noise(ripple+44)*g.w*0.49,ry,2+A.noise(ripple)*9,1)
    }
    var flicker=Math.floor(time*4)%3
    A.shape(c,g,p.tile,[[-64,2],[-62,-8-flicker],[-57,-4],[-54,-15+flicker],[-50,-5],[-49,2]])
    A.shape(c,g,p.glow,[[-61,2],[-57,-8+flicker],[-52,2]])
    if(weather!=="rain") {
        for(var spark=0;spark<3;spark++) {
            var age=(time*0.45+spark*0.6)%2
            c.globalAlpha=0.6*(1-age/2)
            A.part(c,g,p.glow,-57+Math.sin(time+spark)*3,-12-age*9,1,1)
        }
    }
    if(p.warm>0&&weather==="clear") {
        for(var fly=0;fly<8;fly++) {
            c.globalAlpha=(0.2+0.6*Math.max(0,Math.sin(time*0.7+fly)))*p.warm
            A.rect(c,p.glow,g.w*(0.40+A.noise(fly+8)*0.45)+Math.sin(time*0.3+fly)*3,g.ground-2-A.noise(fly+99)*g.h*0.19+Math.cos(time*0.4+fly)*2,1,1)
        }
    }
    c.globalAlpha=1;A.drawPrecipitation(c,g,p,weather,time)
}
