// MijiaPanel.qml — 沿用宿主 FluentWinUI3 的家庭筛选、平面设备卡片与详情
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.FluentWinUI3 as Fluent

Pane {
    id: view
    required property var hub
    readonly property var snapshot:hub.snapshot || ({})
    readonly property var detail:hub.detail || ({})
    property bool expandedProperties:false
    property bool singleColumn:false
    padding:16
    font.family:"Microsoft YaHei UI"
    font.pixelSize:14
    palette.window:hub.panelColor
    palette.base:hub.cardColor
    palette.button:hub.cardColor
    palette.windowText:hub.foreground
    palette.text:hub.foreground
    palette.buttonText:hub.foreground
    palette.highlight:hub.accent
    palette.accent:hub.accent
    palette.highlightedText:hub.colors.onAccent || "white"
    palette.placeholderText:hub.muted
    function trText(text) { return qsTranslate("plugin.py_mijia.main",text) }
    function closeDialogs() { confirmDialog.close();logoutDialog.close();more.close();displaySettings.close() }
    function openConfirmation() { confirmDialog.open() }
    function openBarSettings() { displaySettings.open() }
    function visibleDeviceIds() {
        if(hub.selected || !hub.ready || displaySettings.visible)return []
        var ids=[]
        for(var i=0;i<grid.children.length;i++) {
            var child=grid.children[i]
            if(child.device && child.y+child.height>=devices.contentY && child.y<=devices.contentY+devices.height)
                ids.push(child.device.id)
        }
        return ids
    }
    background:Rectangle {
        radius:8
        color:hub.panelColor
        border.width:1;border.color:hub.alpha(hub.foreground,0.1)
    }
    ColumnLayout {
        anchors.fill:parent
        spacing:12
        RowLayout {
            Layout.fillWidth:true;spacing:10
            MijiaIconButton {
                glyph:hub.selected?"back":"home";tint:hub.foreground
                hint:hub.selected?view.trText("返回设备"):view.trText("家庭")
                onClicked:{hub.selected="";view.expandedProperties=false}
            }
            ColumnLayout {
                Layout.fillWidth:true;spacing:3
                Label {
                    Layout.fillWidth:true
                    text:hub.selected?(view.detail.name || view.trText("加载中…")):view.trText("米家")
                    textFormat:Text.PlainText;font.pixelSize:18;font.weight:Font.DemiBold;color:hub.foreground;elide:Text.ElideRight
                }
                Label { font.family:view.font.family;
                    text:hub.selected?hub.roomName(view.detail)+" · "+hub.kindLabel(view.detail.kind)
                        :hub.ready?view.trText("%1 个家庭 · %2 台在线").arg((view.snapshot.homes || []).length).arg((view.snapshot.catalog || []).filter(d=>d.online).length)
                        :view.trText("扫码连接你的米家设备")
                    font.pixelSize:12;color:hub.muted;Layout.fillWidth:true;elide:Text.ElideRight
                }
            }
            MijiaIconButton {
                visible:hub.ready
                glyph:"star";hint:view.trText("收藏")
                tint:(hub.selected?hub.favorite(hub.selected):hub.favoritesOnly)?hub.accent:hub.muted
                checked:hub.selected?hub.favorite(hub.selected):hub.favoritesOnly
                onClicked:{if(hub.selected)hub.toggleFavorite(hub.selected);else hub.favoritesOnly=!hub.favoritesOnly}
            }
            MijiaIconButton {
                glyph:"down";hint:view.trText("更多操作");tint:hub.foreground
                onClicked:more.popup()
                Menu {
                    id:more
                    palette:view.palette
                    MenuItem { text:view.trText("刷新");enabled:hub.ready;onTriggered:hub.call("refresh") }
                    MenuItem { text:view.trText("清除状态栏读数");enabled:!!hub.preferences.pin.deviceId;onTriggered:hub.clearPin() }
                    MenuSeparator {}
                    MenuItem { text:view.trText("退出登录");onTriggered:logoutDialog.open() }
                }
            }
        }
        RowLayout {
            visible:hub.ready && !hub.selected
            Layout.fillWidth:true;spacing:12
            Label { text:view.trText("家庭");font.pixelSize:12;color:hub.muted }
            Fluent.ComboBox {
                id:homePicker
                Layout.fillWidth:true
                model:hub.homeOptions();textRole:"name";font.pixelSize:14
                currentIndex:{for(var i=0;i<count;i++)if(model[i].id===hub.preferences.home)return i;return 0}
                displayText:currentIndex===0 && (view.snapshot.homes || []).length===1?view.snapshot.homes[0].name:currentText
                popup.palette:homePicker.palette
                onActivated:{var p=JSON.parse(JSON.stringify(hub.preferences));p.home=model[index].id;hub.room="";hub.savePreferences(p)}
            }
            Label { text:"MIoT";font.pixelSize:12;color:hub.muted }
        }
        RowLayout {
            visible:hub.ready && !hub.selected
            Layout.fillWidth:true;spacing:5
            DeviceGlyph { visible:hub.overview.temperature.length>0;kind:"temperature";tint:hub.muted;Layout.preferredWidth:14;Layout.preferredHeight:16 }
            Label { font.family:view.font.family; text:hub.overview.temperature;visible:text.length>0;font.pixelSize:12;color:hub.foreground }
            DeviceGlyph { visible:hub.overview.humidity.length>0;kind:"humidity";tint:hub.muted;Layout.preferredWidth:14;Layout.preferredHeight:16;Layout.leftMargin:12 }
            Label { font.family:view.font.family; text:hub.overview.humidity;visible:text.length>0;font.pixelSize:12;color:hub.muted }
            DeviceGlyph { visible:hub.overview.power.length>0;kind:"bolt";tint:hub.muted;Layout.preferredWidth:14;Layout.preferredHeight:16;Layout.leftMargin:12 }
            Label { text:hub.overview.power;visible:text.length>0;font.pixelSize:12;color:hub.foreground }
            Label { font.family:view.font.family; visible:!hub.overview.temperature && !hub.overview.power && !hub.overview.humidity;text:view.trText("选择设备，查看实时状态");font.pixelSize:12;color:hub.muted }
            Item { Layout.fillWidth:true }
        }
        Rectangle {
            visible:!!hub.feedback || !!view.snapshot.error
            Layout.fillWidth:true
            implicitHeight:notice.implicitHeight+16
            radius:4;color:hub.alpha(hub.colors.warning || "#e8a020",0.09)
            Label { font.family:view.font.family; id:notice;anchors.fill:parent;anchors.margins:8;text:hub.feedback || hub.errorText(view.snapshot.error);wrapMode:Text.Wrap;font.pixelSize:12;color:hub.foreground }
        }
        ColumnLayout {
            visible:!hub.ready
            Layout.fillWidth:true;Layout.fillHeight:true;Layout.maximumWidth:Number.POSITIVE_INFINITY;spacing:14
            Item { Layout.fillHeight:true;Layout.minimumHeight:0 }
            Label { text:view.trText("扫码连接米家");font.pixelSize:20;font.weight:Font.DemiBold;color:hub.foreground;Layout.alignment:Qt.AlignHCenter }
            Label { font.family:view.font.family; text:view.trText("使用米家 App 扫码并确认登录");color:hub.muted;font.pixelSize:12;Layout.alignment:Qt.AlignHCenter }
            Rectangle {
                Layout.preferredWidth:230;Layout.preferredHeight:230;Layout.maximumHeight:230;Layout.alignment:Qt.AlignHCenter
                radius:8;color:"white";border.color:hub.alpha(hub.foreground,0.08)
                Image { anchors.fill:parent;anchors.margins:14;source:view.snapshot.qrImage || "";fillMode:Image.PreserveAspectFit;cache:false }
                DeviceGlyph { visible:!view.snapshot.qrImage;kind:"home";tint:"#e5e8ec";width:64;height:64;anchors.centerIn:parent }
                BusyIndicator { anchors.centerIn:parent;visible:running;running:hub.status==="login_loading" || hub.status==="restoring" || hub.status==="saving_session" }
            }
            Fluent.ComboBox {
                id:region
                Layout.preferredWidth:230;Layout.alignment:Qt.AlignHCenter
                enabled:hub.status!=="waiting_scan" && hub.status!=="login_loading"
                model:[view.trText("中国大陆"),view.trText("新加坡"),view.trText("德国"),view.trText("美国"),view.trText("俄罗斯")]
                popup.palette:region.palette
            }
            Fluent.Button {
                Layout.preferredWidth:230;Layout.preferredHeight:36;Layout.alignment:Qt.AlignHCenter
                text:view.trText("获取或刷新二维码")
                enabled:hub.status!=="host_upgrade_required" && hub.status!=="saving_session"
                highlighted:true
                onClicked:hub.call("start_login",[["cn","sg","de","us","ru"][region.currentIndex]])
            }
            Fluent.Button { visible:hub.status==="waiting_scan" || hub.status==="login_loading";text:view.trText("取消");flat:true;Layout.alignment:Qt.AlignHCenter;onClicked:hub.call("cancel_login") }
            Item { Layout.fillHeight:true;Layout.minimumHeight:0 }
        }
        ColumnLayout {
            visible:hub.ready && !hub.selected
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:10
            Fluent.TextField {
                id:search;objectName:"mijiaSearch"
                Layout.fillWidth:true
                font.pixelSize:14;placeholderText:view.trText("搜索设备名称或型号")
                color:hub.foreground;placeholderTextColor:hub.muted
                leftPadding:34;rightPadding:clearSearch.visible?34:12
                selectByMouse:true
                onTextChanged:hub.query=text
                DeviceGlyph { anchors.left:parent.left;anchors.leftMargin:12;anchors.verticalCenter:parent.verticalCenter;width:15;height:15;kind:"search";tint:hub.muted }
                MijiaIconButton { id:clearSearch;anchors.right:parent.right;anchors.rightMargin:2;anchors.verticalCenter:parent.verticalCenter;glyph:"close";visible:search.text.length>0;tint:hub.muted;hint:view.trText("清除搜索");onClicked:search.clear() }
            }
            RowLayout {
                Layout.fillWidth:true;spacing:8
                Flickable {
                    Layout.fillWidth:true;implicitHeight:32
                    contentWidth:tabs.implicitWidth;contentHeight:32;clip:true;boundsBehavior:Flickable.StopAtBounds
                    Row {
                        id:tabs;spacing:4
                        Repeater {
                            model:hub.roomOptions()
                            Fluent.Button {
                                required property var modelData
                                objectName:"mijiaRoom_"+modelData.id
                                height:32;leftPadding:12;rightPadding:12
                                text:modelData.id?modelData.name:view.trText("全部")
                                font.pixelSize:12
                                checked:hub.room===modelData.id
                                flat:!checked
                                onClicked:{hub.room=modelData.id;hub.scheduleView()}
                            }
                        }
                    }
                }
                MijiaIconButton { glyph:view.singleColumn?"list":"grid";hint:view.trText("切换卡片布局");tint:hub.muted;onClicked:{view.singleColumn=!view.singleColumn;hub.scheduleView()} }
            }
            Label { text:view.trText("设备");font.pixelSize:14;font.weight:Font.DemiBold;color:hub.foreground }
            RowLayout {
                visible:hub.favoritesOnly
                Label { font.family:view.font.family; text:view.trText("我的收藏");color:hub.accent;font.pixelSize:11;Layout.fillWidth:true }
                Fluent.Button { text:view.trText("显示全部");flat:true;font.pixelSize:10;onClicked:hub.favoritesOnly=false }
            }
            Flickable {
                id:devices;objectName:"mijiaDevices"
                Layout.fillWidth:true;Layout.fillHeight:true
                contentWidth:width;contentHeight:grid.implicitHeight+8;clip:true;boundsBehavior:Flickable.StopAtBounds
                onContentYChanged:hub.scheduleView()
                ScrollBar.vertical:ScrollBar { policy:ScrollBar.AsNeeded }
                GridLayout {
                    id:grid;width:parent.width
                    columns:view.singleColumn || width<360?1:2
                    columnSpacing:8;rowSpacing:8
                    Repeater {
                        model:hub.filtered
                        DeviceCard {
                            required property var modelData
                            objectName:"mijiaCard_"+modelData.id
                            device:modelData
                            roomName:hub.roomName(modelData)
                            Layout.fillWidth:true
                            Layout.columnSpan:modelData.kind==="climate"?grid.columns:1
                            Layout.minimumWidth:0
                            surface:hub.cardColor;foreground:hub.foreground;muted:hub.muted;accent:hub.accent;dark:hub.dark
                            busy:(modelData.preview || []).some(c=>c.pending || hub.awaiting[hub.controlKey(modelData.id,c.id)])
                            onOpened:{hub.selected=modelData.id;view.expandedProperties=false}
                            onWriteRequested:function(field,value){hub.requestDeviceWrite(modelData,field,value)}
                        }
                    }
                }
                Label { font.family:view.font.family; anchors.centerIn:parent;visible:hub.filtered.length===0;text:view.trText("没有匹配设备，或目录仍在加载");color:hub.muted;font.pixelSize:12 }
            }
        }
        ScrollView {
            visible:hub.ready && !!hub.selected
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth
            ColumnLayout {
                width:parent.width;spacing:10
                DeviceCard {
                    Layout.fillWidth:true
                    device:Object.assign({},view.detail,{preview:view.detail.controls || []})
                    roomName:hub.roomName(view.detail)
                    surface:hub.cardColor;foreground:hub.foreground;muted:hub.muted;accent:hub.accent;dark:hub.dark
                    busy:(view.detail.controls || []).some(c=>c.pending || hub.awaiting[hub.controlKey(view.detail.id,c.id)])
                    onWriteRequested:function(field,value){hub.requestDeviceWrite(view.detail,field,value)}
                }
                Label { font.family:view.font.family;
                    visible:view.detail.support!=="ready";Layout.fillWidth:true;wrapMode:Text.Wrap;color:hub.muted;font.pixelSize:12
                    text:view.detail.support==="no_spec"?view.trText("该型号尚无可用规格，无法生成控件")
                        :view.detail.support==="no_properties"?view.trText("该设备没有开放可用属性")
                        :view.detail.support==="spec_error"?view.trText("规格获取失败，请稍后刷新"):view.trText("正在解析设备能力…")
                }
                Repeater {
                    model:(view.detail.controls || []).filter(c=>c.primary || view.expandedProperties || !(view.detail.controls || []).some(f=>f.primary))
                    PropertyControl {
                        required property var modelData
                        Layout.fillWidth:true
                        field:modelData;label:hub.label(modelData);formattedValue:hub.valueText(modelData);palette:view.palette
                        enabled:!modelData.pending && !hub.awaiting[hub.controlKey(view.detail.id,modelData.id)]
                        editable:hub.ready && view.detail.online
                        pinned:hub.preferences.pin.deviceId===view.detail.id && hub.preferences.pin.propertyId===modelData.id
                        onWriteRequested:function(value){hub.requestWrite(modelData,value)}
                        onPinRequested:hub.pinField(modelData)
                    }
                }
                Fluent.Button {
                    visible:(view.detail.controls || []).some(c=>c.primary) && (view.detail.controls || []).some(c=>!c.primary)
                    text:view.expandedProperties?view.trText("收起更多属性"):view.trText("更多属性")
                    flat:true;Layout.fillWidth:true
                    onClicked:view.expandedProperties=!view.expandedProperties
                }
                Repeater {
                    model:view.detail.actions || []
                    Fluent.Button {
                        required property var modelData
                        text:hub.label(modelData)
                        enabled:view.detail.online && !hub.awaiting[hub.controlKey(view.detail.id,modelData.id)]
                        onClicked:hub.call("curtain_action",[{deviceId:view.detail.id,actionId:modelData.id}],hub.controlKey(view.detail.id,modelData.id))
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:hub.alpha(hub.foreground,0.08) }
        RowLayout {
            Layout.fillWidth:true;Layout.preferredHeight:32;spacing:8
            Rectangle { width:6;height:6;radius:3;color:hub.ready?(hub.colors.success || hub.accent):hub.muted }
            Label { text:hub.ready?view.trText("米家已连接 · %1 台设备").arg((view.snapshot.catalog || []).length):view.trText("米家未连接");font.pixelSize:12;color:hub.muted;Layout.fillWidth:true;elide:Text.ElideRight }
            Fluent.Button { objectName:"mijiaDisplaySettingsButton";text:view.trText("任务栏显示");font.pixelSize:12;flat:true;enabled:hub.ready;onClicked:view.openBarSettings() }
            MijiaIconButton { glyph:"refresh";tint:hub.foreground;enabled:hub.ready;hint:view.trText("刷新");onClicked:hub.call("refresh") }
            MijiaIconButton { glyph:"power";tint:hub.foreground;hint:view.trText("退出登录");onClicked:logoutDialog.open() }
        }
    }
    BarDisplaySettings { id:displaySettings;objectName:"mijiaDisplaySettings";hub:view.hub;palette:view.palette }
    Dialog {
        id:confirmDialog;anchors.centerIn:parent;width:Math.min(380,view.width-32);modal:true
        title:view.trText("确认通用属性操作");standardButtons:Dialog.Ok | Dialog.Cancel;palette:view.palette
        contentItem:Label { font.family:view.font.family; text:hub.confirmation.text || "";textFormat:Text.PlainText;wrapMode:Text.Wrap;palette:view.palette }
        onAccepted:{var args=hub.confirmation.args;args.confirmed=true;hub.call("write_property",[args],hub.controlKey(args.deviceId,args.propertyId))}
    }
    Dialog {
        id:logoutDialog;anchors.centerIn:parent;width:Math.min(380,view.width-32);modal:true
        title:view.trText("退出登录");standardButtons:Dialog.Ok | Dialog.Cancel;palette:view.palette
        contentItem:Label { font.family:view.font.family; text:view.trText("删除本机保存的米家会话？");wrapMode:Text.Wrap;palette:view.palette }
        onAccepted:hub.call("logout")
    }
}
