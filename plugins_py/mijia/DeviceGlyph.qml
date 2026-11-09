// DeviceGlyph.qml — 24 单位网格的矢量设备图标，随 DPI 重绘
import QtQuick

Canvas {
    id: glyph
    property string kind: "generic"
    property color tint: "#777777"
    implicitWidth: 24
    implicitHeight: 24
    onKindChanged: requestPaint()
    onTintChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        var p = getContext("2d")
        p.reset(); p.clearRect(0,0,width,height); p.scale(width/24,height/24)
        p.strokeStyle=tint; p.fillStyle=tint; p.lineWidth=1.8; p.lineCap="round"; p.lineJoin="round"
        function line(x1,y1,x2,y2) { p.beginPath(); p.moveTo(x1,y1); p.lineTo(x2,y2); p.stroke() }
        function circle(x,y,r,fill) { p.beginPath(); p.arc(x,y,r,0,Math.PI*2); fill?p.fill():p.stroke() }
        switch (kind) {
        case "home":
            p.beginPath(); p.moveTo(3,11); p.lineTo(12,3); p.lineTo(21,11); p.lineTo(18,11); p.lineTo(18,21)
            p.lineTo(14,21); p.lineTo(14,14); p.lineTo(10,14); p.lineTo(10,21); p.lineTo(6,21); p.lineTo(6,11); p.closePath(); p.fill(); break
        case "light":
            p.beginPath(); p.moveTo(9,16); p.bezierCurveTo(9,13,6,13,6,9); p.arc(12,9,6,Math.PI,0)
            p.bezierCurveTo(18,13,15,13,15,16); p.closePath(); p.fill(); line(10,19,14,19); line(11,22,13,22); break
        case "switch":
            line(8,3,8,9); line(16,3,16,9); p.beginPath(); p.moveTo(5,8); p.lineTo(19,8); p.lineTo(19,12)
            p.bezierCurveTo(19,17,15,19,12,19); p.bezierCurveTo(9,19,5,17,5,12); p.closePath(); p.fill(); line(12,18,12,23); break
        case "climate":
            p.save(); p.translate(12,12)
            for(var i=0;i<6;i++){p.rotate(Math.PI/3);line(0,0,0,-9);line(0,-5,-3,-7);line(0,-5,3,-7)} p.restore(); break
        case "fan":
            circle(12,12,2,true)
            p.save(); p.translate(12,12)
            for(var j=0;j<3;j++){p.rotate(2*Math.PI/3);p.beginPath();p.moveTo(1,-2);p.bezierCurveTo(8,-12,13,-2,4,1);p.closePath();p.fill()}p.restore();break
        case "cover":
            line(3,4,21,4); line(5,4,5,21);line(19,4,19,21);line(5,21,9,18);line(19,21,15,18);line(9,5,9,18);line(15,5,15,18);break
        case "air":
            for(var k=0;k<3;k++){p.beginPath();p.moveTo(3,7+k*5);p.bezierCurveTo(10,2+k*5,14,12+k*5,21,6+k*5);p.stroke()}break
        case "sensor": case "temperature":
            p.beginPath();p.moveTo(9,14);p.lineTo(9,5);p.arc(12,5,3,Math.PI,0);p.lineTo(15,14);p.arc(12,18,5,-0.85,Math.PI+0.85);p.closePath();p.stroke();line(12,8,12,18);circle(12,18,2,true);break
        case "humidity":
            p.beginPath();p.moveTo(12,2);p.bezierCurveTo(9,7,5,12,5,16);p.arc(12,16,7,Math.PI,0,true);p.bezierCurveTo(19,12,15,7,12,2);p.closePath();p.fill();break
        case "bolt":
            p.beginPath();p.moveTo(14,2);p.lineTo(5,14);p.lineTo(11,14);p.lineTo(9,23);p.lineTo(20,10);p.lineTo(13,10);p.closePath();p.fill();break
        case "search":circle(10,10,6,false);line(15,15,21,21);break
        case "refresh":
            p.beginPath();p.arc(12,12,8,-0.6,Math.PI*1.65);p.stroke();line(19,3,19,8);line(19,8,14,8);break
        case "power":
            p.beginPath();p.arc(12,13,8,-Math.PI*.3,Math.PI*1.3);p.stroke();line(12,2,12,12);break
        case "star":
            p.beginPath();for(var n=0;n<10;n++){var a=n*Math.PI/5-Math.PI/2;var r=n%2?4:9;var x=12+Math.cos(a)*r;var y=12+Math.sin(a)*r;n?p.lineTo(x,y):p.moveTo(x,y)}p.closePath();p.stroke();break
        case "grid":
            for(var gy=0;gy<2;gy++)for(var gx=0;gx<2;gx++)p.fillRect(4+gx*9,4+gy*9,6,6);break
        case "list":line(5,6,19,6);line(5,12,19,12);line(5,18,19,18);break
        case "plus":line(5,12,19,12);line(12,5,12,19);break
        case "minus":line(5,12,19,12);break
        case "close":line(6,6,18,18);line(18,6,6,18);break
        case "back":line(15,5,8,12);line(8,12,15,19);break
        case "down":line(6,9,12,15);line(12,15,18,9);break
        case "next":line(9,6,15,12);line(15,12,9,18);break
        default:circle(12,12,8,false);circle(12,12,2,true)
        }
    }
}
