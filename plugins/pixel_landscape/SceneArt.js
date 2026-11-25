// SceneArt.js — 原创山间小屋像素轮廓、调色板与确定性构图
.pragma library

var keys = ["sky", "skyLow", "horizon", "far", "ridge", "mountain", "forest", "tree", "leaf", "grass", "grassLight", "earth", "rock", "water", "ripple", "wall", "wood", "roof", "tile", "trim", "glow", "sun", "cloud", "snow"]
var palettes = {
    dawn: ["718ca6","b6b3b0","f2cfaa","929aaf","727f97","536e80","45676a","315e5a","628579","708778","9b9e7d","9e8c78","7a8583","789fa5","e4c8a7","ba967a","8e6c5d","715c62","9e7470","564c52","f8cf83","ffe0a1","e1d6c6","dfe5e8"],
    day: ["69a9c8","a5ced3","dbe2ce","97b1bb","7795a4","557b89","416b72","2c5854","5b8267","71906b","a4ae75","a6956f","7e8e81","5a9da8","b6d5c6","c8a47e","99745b","705a55","a98169","504e4b","f6d799","fff0bc","e8ebd9","e4eee9"],
    dusk: ["736b94","bf8c9c","f4bb97","9e819d","816d8b","655e7c","4e5966","374b51","5f6c69","6e7666","a08b70","9b7c67","787582","738594","e3ad99","c49772","916950","654c54","936262","453e4a","ffd280","ffcc8e","e6acaa","d6d5df"],
    night: ["16283f","29405b","506179","485a74","364a65","283e56","223e48","19353d","35514f","344e4d","5d6655","535b56","536576","36576d","7894a3","876f61","5b4e4b","343b4e","515163","28323f","ffd18a","dce4cd","708496","b2c6d5"]
}
function rgb(hex) {
    var start=hex.charAt(0)==="#"?1:0
    return [parseInt(hex.substr(start,2),16),parseInt(hex.substr(start+2,2),16),parseInt(hex.substr(start+4,2),16)]
}
function mix(a,b,t) {
    var x=rgb(a), y=rgb(b), s="#"
    for(var i=0;i<3;i++){var n=Math.round(x[i]+(y[i]-x[i])*t).toString(16);s+=(n.length<2?"0":"")+n}
    return s
}
function palette(phase, weather, previous, blend) {
    var a=palettes[previous]||palettes.day, b=palettes[phase]||palettes.day, p={}
    for(var i=0;i<keys.length;i++) {
        var c=mix(a[i],b[i],Math.max(0,Math.min(1,blend)))
        if(weather==="rain" && keys[i]!=="glow") c=mix(c,"53677a",0.30)
        if(weather==="snow" && keys[i]!=="glow") c=mix(c,"a3b5c6",0.12)
        p[keys[i]]=c
    }
    p.night=(previous==="night"?1:0)*(1-blend)+(phase==="night"?1:0)*blend
    p.warm=(previous==="night"||previous==="dusk"?1:0)*(1-blend)+(phase==="night"||phase==="dusk"?1:0)*blend
    p.mist=(phase==="dawn"?1:0)
    p.lamp=p.glow
    p.window=p.warm>0.2?p.glow:p.ripple
    return p
}
function solarPalette(key, weather) {
    var v=key.split("|"),p=palette(v[1],weather,v[0],Number(v[2]))
    // 冬季低太阳和极区正午仍可能处于晨昏高度，晨/昏色板也须连续融合。
    for(var i=0;i<keys.length;i++) {
        var twilight=mix(palettes.dawn[i],palettes.dusk[i],Number(v[6]))
        var color=v[0]==="night" ? mix(palettes.night[i],twilight,Number(v[2]))
                                  : mix(twilight,palettes.day[i],Number(v[2]))
        if(weather==="rain" && keys[i]!=="glow")color=mix(color,"53677a",0.30)
        if(weather==="snow" && keys[i]!=="glow")color=mix(color,"a3b5c6",0.12)
        p[keys[i]]=color
    }
    p.solar=true;p.night=Number(v[3]);p.warm=Number(v[4]);p.mist=Number(v[5])
    p.lamp=mix(p.ripple,p.glow,p.warm);p.window=p.lamp
    return p
}
function goldenPalette(p, strength, evening) {
    var amount=Math.max(0,Math.min(1,strength)), dusk=Math.max(0,Math.min(1,evening))
    if(amount===0)return p
    var dawn={sky:"526b91",skyLow:"d99a77",horizon:"ffd39a",cloud:"f7c495",sun:"ffe6ac",
        far:"b6a0a0",ridge:"867f91",mountain:"6b7789"}
    var sunset={sky:"685c9a",skyLow:"da7b76",horizon:"ffb66f",cloud:"ff9b85",sun:"ffd38a",
        far:"aa8d9f",ridge:"8b728d",mountain:"74697e"}
    for(var key in dawn)p[key]=mix(p[key],mix(dawn[key],sunset[key],dusk),amount)
    // 地物只加少量暖光，避免整幅画成为不分材质的橙色滤镜。
    for(var i=0;i<keys.length;i++) {
        var name=keys[i]
        if(!(name in dawn)&&name!=="glow")p[name]=mix(p[name],"eaa269",amount*0.12)
    }
    p.lamp=mix(p.ripple,p.glow,p.warm);p.window=p.lamp
    p.golden=amount;p.goldenEvening=dusk
    return p
}
function noise(i) { var n=Math.sin(i*127.1+311.7)*43758.5453; return n-Math.floor(n) }
function rect(c,color,x,y,w,h) {
    c.fillStyle=color
    c.fillRect(Math.floor(x),Math.floor(y),Math.max(1,Math.ceil(w)),Math.max(1,Math.ceil(h)))
}
// 逐扫描线填充轮廓，避免 Canvas 路径抗锯齿在放大后产生灰边。
function polygon(c,color,points) {
    var low=1e9, high=-1e9
    for(var i=0;i<points.length;i++){low=Math.min(low,points[i][1]);high=Math.max(high,points[i][1])}
    c.fillStyle=color
    for(var y=Math.floor(low);y<=Math.ceil(high);y++) {
        var intersections=[]
        for(var j=0;j<points.length;j++) {
            var a=points[j],b=points[(j+1)%points.length]
            if((a[1]<=y&&b[1]>y)||(b[1]<=y&&a[1]>y))
                intersections.push(a[0]+(y-a[1])*(b[0]-a[0])/(b[1]-a[1]))
        }
        intersections.sort(function(a,b){return a-b})
        for(var k=0;k+1<intersections.length;k+=2)
            c.fillRect(Math.floor(intersections[k]),y,Math.max(1,Math.ceil(intersections[k+1])-Math.floor(intersections[k])),1)
    }
}
function geometry(w,h,expanded) {
    var s=expanded?1:Math.max(0.25,Math.min(1.6,h/80))
    // 极窄槽位只改变构图，不把小屋横向压扁。
    s=Math.min(s,Math.max(0.15,(w-4)/64))
    var g={w:w,h:h,expanded:expanded,minimal:!expanded&&w<32,s:s,ground:Math.round(h*(expanded?0.70:0.76)),trees:[],stars:[],flecks:[]}
    g.houseX=Math.round(w*(w<100?0.5:0.61)-25*s)
    g.houseY=g.ground-2*s
    if(g.minimal)return g
    for(var i=0;i<38;i++) {
        var x=noise(i+2)*w
        if(x>g.houseX-8*s&&x<g.houseX+66*s) continue
        g.trees.push({x:x,y:g.ground-2+noise(i+11)*h*0.08,size:(10+noise(i+9)*18)*s})
    }
    for(var j=0;j<34;j++) g.stars.push({x:noise(j+43)*w,y:noise(j+80)*h*0.36,seed:j})
    for(var k=0;k<160;k++)g.flecks.push({x:noise(k+500)*w,y:g.ground+noise(k+800)*(h-g.ground),n:noise(k+30)})
    return g
}
function pine(c,p,x,y,size,snow) {
    var half=size*0.32
    rect(c,p.wood,x-1,y-size*0.45,2,size*0.45+1)
    polygon(c,p.tree,[[x,y-size],[x-half*0.55,y-size*0.46],[x-half*0.3,y-size*0.46],[x-half,y-2],[x+half,y-2],[x+half*0.3,y-size*0.46],[x+half*0.55,y-size*0.46]])
    polygon(c,p.leaf,[[x,y-size],[x-half*0.5,y-size*0.47],[x-1,y-size*0.55],[x-half*0.8,y-4],[x,y-5]])
    if(snow) {
        polygon(c,p.snow,[[x,y-size],[x-half*0.45,y-size*0.5],[x,y-size*0.59],[x+half*0.38,y-size*0.5]])
        rect(c,p.snow,x-half*0.65,y-5,half*0.7,1)
    }
}
function mountain(c,g,p,base,amplitude,offset,color) {
    var points=[[0,g.h]]
    for(var i=0;i<=18;i++) points.push([i*g.w/18,base-amplitude*(0.25+noise(i+offset)*0.75)])
    points.push([g.w,g.h]);polygon(c,color,points)
    return points
}
function streamX(g,y) {
    var t=(y-g.ground)/Math.max(1,g.h-g.ground)
    return g.w*(0.45-0.13*t)+Math.sin(t*5+0.8)*g.w*0.06
}
function streamHalf(g,y) {
    var t=(y-g.ground)/Math.max(1,g.h-g.ground)
    return Math.max(1,g.s*2+t*t*g.w*0.065)
}
function cabin(c,g,p,snow) {
    var x=g.houseX,y=g.houseY,s=g.s
    function r(color,dx,dy,w,h){rect(c,color,x+dx*s,y+dy*s,w*s,h*s)}
    function poly(color,pts){polygon(c,color,pts.map(function(v){return [x+v[0]*s,y+v[1]*s]}))}
    r(p.tree,-4,-1,64,4)
    r(p.trim,0,-28,54,29)
    r(p.wall,2,-27,36,26)
    r(p.wood,38,-27,14,26)
    for(var j=-24;j<-1;j+=4){r(p.wood,2,j,36,1);r(p.trim,39,j,13,1)}
    // 门、玻璃与细窗框在栏上仍保持可辨识的暖光。
    r(p.trim,22,-20,11,20);r(p.wood,24,-18,7,17);r(p.glow,29,-9,1,1)
    r(p.trim,6,-22,12,12);r(p.window,7,-21,10,10)
    r(p.trim,11,-21,1,10);r(p.trim,7,-16,10,1)
    r(p.trim,42,-20,7,10);r(p.lamp,43,-19,5,8)
    r(p.rock,20,-1,18,3);r(p.earth,18,2,23,2)
    r(p.trim,38,-49,6,16);r(p.wood,39,-48,4,14);r(p.rock,37,-50,8,3)
    poly(p.trim,[[-6,-27],[16,-47],[39,-47],[61,-27]])
    poly(p.roof,[[-3,-29],[17,-44],[38,-44],[57,-29]])
    poly(p.tile,[[-3,-29],[17,-44],[20,-44],[6,-29]])
    for(var k=0;k<4;k++)r(p.tile,10+k*4,-32-k*3,39-k*8,1)
    r(p.trim,-6,-27,67,3)
    if(snow){poly(p.snow,[[-6,-28],[16,-47],[39,-47],[61,-28],[53,-30],[36,-43],[18,-43],[0,-28]]);r(p.snow,37,-51,8,2)}
    // 木栅栏与花盆为展开图提供细节；窄条中自然降为几个像素。
    r(p.wood,59,-9,20,2)
    for(var f=0;f<4;f++)r(p.trim,60+f*6,-13,2,16)
    r(p.roof,1,-5,9,5);r(p.leaf,0,-7,10,3)
    if(!snow){r(p.glow,1,-9,2,2);r(p.tile,6,-8,2,2)}
}
function drawStatic(c,g,p,weather) {
    c.reset();c.clearRect(0,0,g.w,g.h)
    drawSky(c,g,p,weather)
    if(g.minimal) {
        // 极窄手动宽度使用完整小屋剪影，不裁切屋身，也不挤入树林和粒子。
        var hw=Math.max(1,g.w-2),hx=Math.floor((g.w-hw)/2),hy=g.ground
        var hh=Math.max(2,Math.min(g.h*0.32,hw*1.25)),roofHeight=Math.max(1,Math.floor(hh*0.45))
        rect(c,weather==="snow"?p.snow:p.grass,0,hy,g.w,g.h-hy)
        rect(c,p.water,g.w*0.5,hy+1,1,g.h-hy)
        polygon(c,weather==="snow"?p.snow:p.roof,[[hx-1,hy-hh],[g.w/2,hy-hh-roofHeight],[hx+hw+1,hy-hh]])
        rect(c,p.wood,hx,hy-hh,hw,hh)
        rect(c,p.glow,hx+Math.floor(hw/3),hy-Math.max(2,hh*0.7),Math.max(1,Math.floor(hw/4)),Math.max(1,Math.floor(hh/3)))
        return
    }
    var far=mountain(c,g,p,g.h*0.56,g.h*0.31,120,p.far)
    // 朝光面的折线山脊，山体的位置不随天气与时段漂移。
    for(var f=2;f<far.length-2;f+=3) {
        var pt=far[f];polygon(c,p.horizon,[[pt[0],pt[1]],[pt[0]-g.w/45,pt[1]+g.h*0.07],[pt[0]-g.w/90,pt[1]+g.h*0.06]])
    }
    mountain(c,g,p,g.h*0.64,g.h*0.22,234,p.ridge)
    mountain(c,g,p,g.h*0.72,g.h*0.17,359,p.mountain)
    for(var ti=0;ti<Math.ceil(g.w/5);ti++) {
        var tx=ti*5,ty=g.ground-2-noise(ti+66)*g.h*0.07
        pine(c,{tree:p.forest,leaf:p.forest,wood:p.forest},tx,ty,4+noise(ti+38)*g.h*0.12,false)
    }
    rect(c,weather==="snow"?p.snow:p.grass,0,g.ground,g.w,g.h-g.ground)
    for(var y=g.ground;y<g.h;y++) {
        var x=streamX(g,y),half=streamHalf(g,y)
        rect(c,p.earth,x-half-2,y,half*2+4,1)
        rect(c,p.water,x-half,y,half*2,1)
        if(y%4===0)rect(c,p.ripple,x-half+1,y,Math.max(1,half*0.5),1)
    }
    for(var i=0;i<g.flecks.length;i++) {
        var fl=g.flecks[i]
        if(Math.abs(fl.x-streamX(g,fl.y))<streamHalf(g,fl.y)+3)continue
        rect(c,weather==="snow"?p.cloud:(fl.n>0.55?p.grassLight:p.leaf),fl.x,fl.y,fl.n>0.8?3:1,1)
        if(fl.n>0.94&&weather!=="snow")rect(c,p.glow,fl.x,fl.y-1,1,1)
    }
    for(var j=0;j<g.trees.length;j++){var tree=g.trees[j];pine(c,p,tree.x,tree.y,tree.size,weather==="snow")}
    if(weather==="rain") {
        for(var puddle=0;puddle<3;puddle++) {
            var px=g.w*(0.16+puddle*0.31),py=g.ground+(g.h-g.ground)*(0.48+puddle*0.15)
            var pw=Math.max(3,8*g.s)
            rect(c,p.earth,px-pw/2-1,py-1,pw+2,3)
            rect(c,p.water,px-pw/2,py,pw,1)
        }
    }
    // 通往溪边的小径。
    polygon(c,p.earth,[[g.houseX+22*g.s,g.houseY+2],[g.houseX+34*g.s,g.houseY+2],[g.w*0.55,g.h],[g.w*0.48,g.h]])
    cabin(c,g,p,weather==="snow")
    if(g.expanded){pine(c,p,9,g.h+4,64,weather==="snow");pine(c,p,g.w-10,g.h+7,76,weather==="snow")}
    for(var rock=0;rock<7;rock++) {
        var ry=g.ground+(g.h-g.ground)*noise(rock+900),rx=streamX(g,ry)+streamHalf(g,ry)+3
        rect(c,p.tree,rx,ry,5*g.s,2*g.s);rect(c,p.rock,rx,ry-1,4*g.s,2*g.s)
    }
}
function cloud(c,p,x,y,s) {
    rect(c,p.cloud,x,y,20*s,3*s);rect(c,p.cloud,x+4*s,y-3*s,10*s,3*s);rect(c,p.cloud,x+8*s,y-5*s,4*s,3*s)
}
function drawDynamic(c,g,p,weather,time,meteor) {
    c.reset();c.clearRect(0,0,g.w,g.h)
    if(g.minimal)return
    drawAmbience(c,g,p,weather,time,meteor)
    // 烟的每一段都由时间和固定编号计算，不追加粒子或保留历史。
    for(var smoke=0;smoke<5;smoke++) {
        var age=(time*0.45+smoke*0.7)%3.5
        var smx=g.houseX+41*g.s+Math.sin(age+time*0.3)*2*g.s+age*2*g.s
        var smy=g.houseY-52*g.s-age*7*g.s
        c.globalAlpha=(1-age/3.5)*0.45
        rect(c,p.cloud,smx,smy,(2+age*1.1)*g.s,2*g.s)
    }
    c.globalAlpha=0.55
    for(var ripple=0;ripple<12;ripple++) {
        var ry=g.ground+((noise(ripple+16)+time*0.025)%1)*(g.h-g.ground)
        var rx=streamX(g,ry)+(noise(ripple+29)-0.5)*streamHalf(g,ry)
        rect(c,p.ripple,rx,ry,Math.max(1,3*g.s),1)
    }
    // 树梢只有一像素幅度的高光移动，不重画整片静态树林。
    for(var tr=0;tr<g.trees.length;tr+=4) {
        var t=g.trees[tr]
        rect(c,weather==="snow"?p.snow:p.leaf,t.x+Math.round(Math.sin(time*0.7+tr)),t.y-t.size+2,1,2)
    }
    if(weather!=="clear") {
        drawPrecipitation(c,g,p,weather,time)
        if(weather==="rain") {
            c.globalAlpha=0.45
            for(var drop=0;drop<6;drop++) {
                var dy=g.ground+noise(drop+760)*(g.h-g.ground),dx=streamX(g,dy)
                var radius=1+((time+noise(drop+550))%1)*3
                rect(c,p.ripple,dx-radius,dy,radius*2,1)
            }
            for(var puddle=0;puddle<3;puddle++) {
                var px=g.w*(0.16+puddle*0.31),py=g.ground+(g.h-g.ground)*(0.48+puddle*0.15)
                var pr=1+((time*0.8+puddle*0.3)%1)*Math.max(1,3*g.s)
                rect(c,p.ripple,px-pr,py,pr*2,1)
            }
        }
    }
    c.globalAlpha=1
}

// 所有场景共享天空和天气，独立地形只绘制自己的动态细节。
function drawSky(c,g,p,weather) {
    c.clearRect(0,0,g.w,g.h)
    if(!p.solar) {
        rect(c,p.horizon,0,0,g.w,g.h)
        // 十二层阶梯天空；真实太阳模式由画布下方的静态矩形绘制同一色带。
        for(var band=0;band<12;band++) {
            var t=band/11
            rect(c,t<0.6?mix(p.sky,p.skyLow,t/0.6):mix(p.skyLow,p.horizon,(t-0.6)/0.4),0,band*g.h/15,g.w,g.h/15+1)
        }
    }
    if(weather==="clear" && p.golden>0 && !g.minimal) {
        // 静态霞云先于地形绘制，所有场景自然遮挡；不跟随太阳坐标触发地形重绘。
        var side=p.solar ? 0.18+0.64*p.goldenEvening : 0.21
        for(var ribbon=0;ribbon<5;ribbon++) {
            var cx=g.w*(side+(ribbon%2?0.12:-0.08)), cy=g.h*(0.17+ribbon*0.055)
            var span=g.w*(0.22+noise(ribbon+1400)*0.26)
            c.globalAlpha=p.golden*(0.23+ribbon*0.06)
            rect(c,p.cloud,cx-span/2,cy,span,Math.max(1,g.h*0.012))
            rect(c,p.horizon,cx-span*0.3,cy+Math.max(1,g.h*0.012),span*0.7,1)
        }
        c.globalAlpha=1
    }
    drawCelestial(c,g,p,weather,g.w*0.21,g.h*0.16)
}
function drawCelestial(c,g,p,weather,sx,sy) {
    if(weather==="clear") {
        if(p.night>0) {
            c.globalAlpha=p.night
            for(var star=0;star<g.stars.length;star++){var st=g.stars[star];rect(c,p.sun,st.x,st.y,1,1)}
            var mx=g.w*0.79,my=g.h*0.14,ms=Math.max(3,g.h*0.075)
            rect(c,p.sun,mx,my,ms,ms);rect(c,p.sky,mx+ms*0.36,my-1,ms*0.75,ms*0.8)
            c.globalAlpha=1
        }
        if(!p.solar && p.night<1) {
            c.globalAlpha=1-p.night
            var sr=Math.max(2,g.h*0.045)
            if(p.golden>0) {
                for(var halo=3;halo>0;halo--) {
                    c.globalAlpha=p.golden*0.10
                    rect(c,p.horizon,sx-sr-halo*2,sy-sr-halo,sr*2+halo*4,sr*2+halo*2)
                }
                c.globalAlpha=1-p.night
            }
            rect(c,p.sun,sx-sr,sy-sr,sr*2,sr*2);rect(c,p.sun,sx-sr-1,sy-sr+1,sr*2+2,sr*2-2)
            c.globalAlpha=1
        }
    }
}
function drawAmbience(c,g,p,weather,time,meteor) {
    c.globalAlpha=weather==="clear"?0.5:0.25
    for(var i=0;i<3;i++) {
        var size=g.expanded?1.2:Math.max(0.3,g.h/120)
        var cx=((noise(i+50)*g.w+time*(0.3+i*0.12))%(g.w+35))-30
        cloud(c,p,cx,g.h*(0.09+i*0.06),size)
    }
    if(p.mist||weather==="rain") {
        c.globalAlpha=weather==="rain"?0.10:0.10*p.mist
        for(var fog=0;fog<3;fog++)rect(c,p.cloud,(time*0.8+fog*g.w/2)%(g.w*1.5)-g.w/2,g.h*(0.48+fog*0.055),g.w*0.65,Math.max(1,g.h*0.025))
    }
    c.globalAlpha=1
    drawNightAmbience(c,g,p,weather,time,meteor)
}
function drawNightAmbience(c,g,p,weather,time,meteor) {
    if(p.night>0&&weather==="clear") {
        for(var st=0;st<g.stars.length;st+=5){var star=g.stars[st];c.globalAlpha=p.night*(0.25+0.55*(Math.sin(time*0.7+st)*0.5+0.5));rect(c,p.sun,star.x,star.y,2,1)}
        if(meteor>=0&&meteor<=1) {
            c.globalAlpha=Math.sin(meteor*Math.PI)
            for(var tail=0;tail<10;tail++){c.globalAlpha=Math.sin(meteor*Math.PI)*(1-tail/10);rect(c,p.sun,g.w*(0.45+meteor*0.18)-tail,g.h*(0.08+meteor*0.16)-tail*0.4,2,1)}
        }
    }
    c.globalAlpha=1
}
function drawPrecipitation(c,g,p,weather,time) {
    if(weather==="clear")return
        var count=g.expanded?96:Math.min(48,Math.max(8,Math.ceil(g.w/6)))
        for(var particle=0;particle<count;particle++) {
            var py=(noise(particle+190)*g.h+time*(weather==="rain"?36:7)*(0.8+noise(particle+210)))%(g.h+6)-3
            var px=(noise(particle+340)*g.w+(weather==="rain"?-time*9:Math.sin(time*0.4+particle)*4)+g.w*100)%g.w
            c.globalAlpha=weather==="rain"?0.32:0.72
            rect(c,weather==="rain"?p.ripple:p.snow,px,py,1,weather==="rain"?Math.max(2,4*g.s):1)
        }
    c.globalAlpha=1
}

// 地标按等比局部坐标绘制，再在最终像素网格取整；不会横向压扁。
function part(c,g,color,x,y,w,h) { rect(c,color,g.x+x*g.s,g.y+y*g.s,w*g.s,h*g.s) }
function shape(c,g,color,points) {
    polygon(c,color,points.map(function(v){return [g.x+v[0]*g.s,g.y+v[1]*g.s]}))
}
