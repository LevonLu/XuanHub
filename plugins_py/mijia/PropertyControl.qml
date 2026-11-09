// PropertyControl.qml — 由 MIoT 标准属性生成编辑器，拖动结束后才提交
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.FluentWinUI3 as Fluent

Frame {
    id: row
    property var field: ({})
    property string label: ""
    property string formattedValue: "—"
    property bool editable: false
    property bool pinned: false
    readonly property color muted:palette.placeholderText
    signal writeRequested(var value)
    signal pinRequested()
    function optionIndex() {
        var values = field.options || []
        for (var i = 0; i < values.length; ++i)
            if (values[i].value === field.value) return i
        return -1
    }
    function trText(source) { return qsTranslate("plugin.py_mijia.main", source) }
    font.family:"Microsoft YaHei UI"
    padding: 12
    implicitHeight: body.implicitHeight + 24
    enabled: !field.pending
    background:Rectangle { radius:8;color:row.palette.base;border.color:Qt.rgba(row.palette.text.r,row.palette.text.g,row.palette.text.b,0.08) }
    ColumnLayout {
        id: body
        anchors.fill: parent
        spacing: 7
        RowLayout {
            Layout.fillWidth: true
            Label { font.family:row.font.family; text: row.label; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize:14;font.weight: Font.DemiBold }
            Label { font.family:row.font.family; text: row.formattedValue; font.pixelSize:14;opacity: row.field.error || row.field.stale ? 0.5 : 1 }
            Fluent.Button {
                objectName:"mijiaPinProperty"
                visible: row.field.readable === true
                flat:true;font.pixelSize:12
                text:row.pinned?row.trText("已在任务栏显示"):row.trText("显示到任务栏")
                onClicked: row.pinRequested()
            }
        }
        Label { font.family:row.font.family; visible: !row.field.primary; text: (row.field.service || "") + " · " + (row.field.id || "");color:row.muted;font.pixelSize:11 }
        Fluent.Switch {
            objectName:"mijiaSwitch"
            visible: row.field.writable && row.field.editor === "bool"
            enabled: row.editable
            checked: row.field.value === true
            text: row.field.value === null ? "—" : checked ? row.trText("已开启") : row.trText("已关闭")
            onClicked: {
                row.writeRequested(checked)
                // 提交或取消确认均以真实回读为准，不先把设备显示成已切换。
                checked = Qt.binding(function() { return row.field.value === true })
            }
        }
        Fluent.ComboBox {
            id: options
            visible: row.field.writable && row.field.editor === "enum"
            enabled: row.editable
            Layout.fillWidth: true
            model: row.field.options || []
            textRole: "label"
            currentIndex: row.optionIndex()
            displayText: currentIndex < 0 ? "—" : currentText
            popup.palette: options.palette
            onActivated: {
                row.writeRequested(model[index].value)
                currentIndex = Qt.binding(function() { return row.optionIndex() })
            }
        }
        RowLayout {
            visible: row.field.writable && row.field.editor === "number"
            Layout.fillWidth: true
            Fluent.Slider {
                id: slider
                property bool changedByUser: false
                Layout.fillWidth: true
                enabled: row.editable
                from: row.field.range && row.field.range.length === 3 ? row.field.range[0] : 0
                to: row.field.range && row.field.range.length === 3 ? row.field.range[1] : 100
                stepSize: row.field.range && row.field.range.length === 3 ? row.field.range[2] : 1
                value: row.field.value === null || row.field.value === undefined ? from : row.field.value
                // MIoT 的宽范围可能生成数万条刻度；只简化轨道，保留 Fluent 手柄和原生步进交互。
                background:Rectangle {
                    x:slider.leftPadding+slider.implicitHandleWidth/2
                    y:slider.topPadding+slider.availableHeight/2-height/2
                    width:Math.max(0,slider.availableWidth-slider.implicitHandleWidth)
                    height:4;radius:2
                    color:Qt.rgba(row.muted.r,row.muted.g,row.muted.b,0.25)
                    Rectangle { width:slider.position*parent.width;height:parent.height;radius:2;color:row.palette.accent }
                }
                onMoved: changedByUser = true
                onPressedChanged: {
                    if (!pressed && changedByUser) {
                        changedByUser = false
                        row.writeRequested(Number((from + Math.round((value - from) / stepSize) * stepSize).toFixed(8)))
                    }
                }
                Keys.onReleased: function(event) {
                    if (changedByUser) {
                        changedByUser = false
                        row.writeRequested(Number(value.toFixed(8)))
                    }
                }
            }
            Label { font.family:row.font.family; text: Number(slider.value.toFixed(2)) + " " + (row.field.unit || "");font.pixelSize:11;color:row.muted;Layout.preferredWidth:65;horizontalAlignment:Text.AlignRight }
        }
        Label { font.family:row.font.family;
            visible: !!row.field.pending || !!row.field.error || !!row.field.stale
            text: row.field.pending ? row.trText("执行中…") : row.trText("读数尚未更新")
            color:row.muted
            font.pixelSize: 12
        }
    }
}
