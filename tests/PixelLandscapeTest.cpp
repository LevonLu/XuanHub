// PixelLandscapeTest.cpp — 离线风景状态、真实 QML 渲染与槽位宽度回归
#include <QtTest>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickView>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QPluginLoader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QTranslator>
#include <memory>
#include "PixelLandscapeViewModel.h"
#include "barplugin/IBarPlugin2.h"
#include "PopupService.h"

class PixelContext final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors CONSTANT)
    Q_PROPERTY(QVariantMap screenGeometry READ screenGeometry CONSTANT)
    Q_PROPERTY(QString barEdge READ barEdge CONSTANT)
    Q_PROPERTY(QObject* popupService MEMBER popups CONSTANT)
public:
    QObject* popups = nullptr;
    int screenWidth = 1280;
    QVariantMap colors() const { return {{"panel", "#20252e"}, {"fg", "#eef0f4"}, {"border", "#495464"}, {"accent", "#b1c8e5"}}; }
    QVariantMap screenGeometry() const { return {{"x", 0}, {"y", 0}, {"width", screenWidth}, {"height", 900}}; }
    QString barEdge() const { return QStringLiteral("top"); }
    Q_INVOKABLE void openSettings() { Q_EMIT settingsRequested(); }
Q_SIGNALS:
    void barPressed();
    void settingsRequested();
};

class PixelSlotModel final : public QObject {
    Q_OBJECT
public:
    int naturalWidth = 0;
    Q_INVOKABLE void reportHealth(const QString&, const QVariantMap&) {}
    Q_INVOKABLE void reportContentWidth(const QString&, int width) { naturalWidth = width; }
};

class PixelUiMetrics final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int radiusS READ radiusS CONSTANT)
public:
    int radiusS() const { return 6; }
};

class PixelTheme final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors CONSTANT)
    Q_PROPERTY(QVariantMap appearance READ appearance CONSTANT)
public:
    QVariantMap colors() const { return {{"danger", "#ff0000"}}; }
    QVariantMap appearance() const { return {{"items", QVariantMap{{"paddingX",6},{"paddingY",2}}}}; }
};

static QString pluginSource(const QString& name) {
    return QStringLiteral(XUANHUB_SOURCE_DIR "/plugins/pixel_landscape/") + name;
}

class PixelTranslator final : public QTranslator {
public:
    QJsonObject contexts;
    bool isEmpty() const override { return false; }
    QString translate(const char* context, const char* source, const char*, int) const override {
        return contexts.value(QString::fromUtf8(context)).toObject().value(QString::fromUtf8(source)).toString();
    }
};

class PixelLandscapeTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void phasesAndClockJumps() {
        const QList<QPair<QTime, QString>> boundaries = {
            {QTime(0,0), "night"}, {QTime(4,59,59), "night"}, {QTime(5,0), "dawn"},
            {QTime(7,59,59), "dawn"}, {QTime(8,0), "day"}, {QTime(16,59,59), "day"},
            {QTime(17,0), "dusk"}, {QTime(19,59,59), "dusk"}, {QTime(20,0), "night"}};
        for (const auto& item : boundaries) QCOMPARE(PixelLandscapeViewModel::phaseAt(item.first), item.second);
        PixelLandscapeViewModel vm;
        const QDate date(2026,11,8);
        vm.refreshAt(QDateTime(date,QTime(16,59,45)), true);
        vm.refreshAt(QDateTime(date,QTime(17,0,15)));
        QCOMPARE(vm.phase(), QStringLiteral("dusk"));
        QVERIFY(vm.transitionAllowed());
        vm.refreshAt(QDateTime(date,QTime(21,0)));
        QCOMPARE(vm.phase(), QStringLiteral("night"));
        QVERIFY(!vm.transitionAllowed());
        vm.refreshAt(QDateTime(date,QTime(6,0)));
        QCOMPARE(vm.phase(), QStringLiteral("dawn"));
        QVERIFY(!vm.transitionAllowed());
        QVERIFY(vm.applyConfig({{"timeMode", "dusk"}}));
        vm.refreshAt(QDateTime(date,QTime(12,0)));
        QCOMPARE(vm.phase(), QStringLiteral("dusk"));
    }

    void invalidConfigPreservesState() {
        PixelLandscapeViewModel vm;
        QVERIFY(vm.applyConfig({{"timeMode", "night"}, {"weather", "snow"}, {"frameStyle", "metal"}, {"scene", "island"}}));
        vm.setPaused(true);
        for (const QVariantMap& invalid : QList<QVariantMap>{{{"weather","storm"}}, {{"timeMode",42}},
                {{"animationsEnabled","false"}}, {{"rareEventsEnabled",0}}, {{"frameStyle","unknown"}}, {{"frameStyle",true}},
                {{"scene","unknown"}}, {{"scene",42}}, {{"scene",false}}, {{"scene",QVariant()}}}) {
            QVERIFY(!vm.applyConfig(invalid));
            QCOMPARE(vm.weather(), QStringLiteral("snow"));
            QCOMPARE(vm.phase(), QStringLiteral("night"));
            QCOMPARE(vm.frameStyle(), QStringLiteral("metal"));
            QCOMPARE(vm.scene(), QStringLiteral("island"));
        }
        QVERIFY(vm.applyConfig({}));
        QCOMPARE(vm.weather(), QStringLiteral("clear"));
        QCOMPARE(vm.frameStyle(), QStringLiteral("wood"));
        QCOMPARE(vm.scene(), QStringLiteral("cabin"));
        QVERIFY(vm.animationsEnabled());
        QVERIFY(vm.paused());
    }

    void nativeLifecycle() {
        QPluginLoader loader(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins/pixel_landscape/pixel_landscape.dll"));
        loader.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        auto* plugin = qobject_cast<xuanhub::IBarPlugin2*>(loader.instance());
        QVERIFY2(plugin,qPrintable(loader.errorString()));
        QCOMPARE(plugin->descriptor().layout.minWidth,160);
        xuanhub::PluginContext2 context;
        for (int i=0;i<3;++i) {
            context.pluginConfig = {{"timeMode","night"}};
            plugin->initialize(context);
            QVERIFY(plugin->viewModel());
            QCOMPARE(plugin->viewModel()->property("phase").toString(),QStringLiteral("night"));
            QVERIFY(!plugin->validateConfig({{"weather","invalid"}}).valid);
            QVERIFY(!plugin->validateConfig({{"frameStyle",42}}).valid);
            QVERIFY(!plugin->validateConfig({{"scene","invalid"}}).valid);
            plugin->applyConfig({{"timeMode","night"},{"frameStyle","mat"},{"scene","railway"}});
            QCOMPARE(plugin->viewModel()->property("frameStyle").toString(),QStringLiteral("mat"));
            QCOMPARE(plugin->viewModel()->property("scene").toString(),QStringLiteral("railway"));
            plugin->shutdown();
            QVERIFY(!plugin->viewModel());
            plugin->shutdown();
        }
    }

    void popupAnimationAndRelease() {
        PixelLandscapeViewModel vm;
        vm.applyConfig({{"timeMode","night"},{"weather","rain"}});
        PopupService popups;
        PixelContext context; context.popups=&popups;
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        QCOMPARE(view.status(), QQuickView::Ready);
        QSignalSpy warnings(view.engine(), &QQmlEngine::warnings);
        auto* root=view.rootObject();
        root->setProperty("pluginContext", QVariant::fromValue<QObject*>(&context));
        root->setProperty("plugin", QVariant::fromValue<QObject*>(&vm));
        view.resize(320,40);view.show();
        auto* scene=root->findChild<QObject*>("barScene");
        auto* controller=root->findChild<QObject*>("landscapeController");
        auto* popup=root->findChild<QQuickWindow*>("landscapePopup");
        auto* loader=root->findChild<QObject*>("landscapePopupLoader");
        QVERIFY(scene&&controller&&popup&&loader);
        QTRY_VERIFY(scene->property("dynamicPaintCount").toInt()>2);
        QVERIFY(!loader->property("item").value<QObject*>());
        vm.setPaused(true);QTest::qWait(150);
        const int frozen=scene->property("dynamicPaintCount").toInt();
        QTest::qWait(350);QCOMPARE(scene->property("dynamicPaintCount").toInt(),frozen);
        vm.setPaused(false);
        QTRY_VERIFY(scene->property("dynamicPaintCount").toInt()>frozen);
        view.hide();QTest::qWait(150);
        const double hiddenTime=controller->property("animationTime").toDouble();
        QTest::qWait(350);QCOMPARE(controller->property("animationTime").toDouble(),hiddenTime);
        view.show();
        popup->setProperty("closeOnDeactivate",false);
        for(int i=0;i<3;i++) {
            Q_EMIT context.barPressed();
            QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,QPoint(100,20));
            QTRY_VERIFY(popup->isVisible());
            QTRY_VERIFY(loader->property("item").value<QObject*>());
            Q_EMIT context.barPressed();
            QTest::mouseClick(&view,Qt::LeftButton,Qt::NoModifier,QPoint(100,20));
            QTRY_VERIFY(!popup->isVisible());
            QTRY_VERIFY(!loader->property("item").value<QObject*>());
        }
        QSignalSpy settings(&context,&PixelContext::settingsRequested);
        QTest::mouseClick(&view,Qt::RightButton,Qt::NoModifier,QPoint(100,20));
        QCOMPARE(settings.count(),1);
        QVERIFY2(warnings.isEmpty(),"QML 运行中不得有绑定循环或属性错误");
    }

    void slotWidthOptInAndLegacy() {
        QTemporaryDir dir;
        const QString templateText=QStringLiteral("import QtQuick\nItem { property var plugin; %1 implicitWidth: 320; width: %2; height: 40 }");
        for (const bool optIn : {false,true}) {
            QFile file(dir.filePath("Probe.qml"));QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(templateText.arg(optIn?"property real hostContentWidth: -1;":"",
                optIn?"hostContentWidth >= 0 ? hostContentWidth : implicitWidth":"320").toUtf8());file.close();
            QQmlEngine engine;QSignalSpy warnings(&engine,&QQmlEngine::warnings);
            PixelSlotModel model;
            PixelTheme theme;
            engine.rootContext()->setContextProperty("themeManager",&theme);
            engine.rootContext()->setContextProperty("pluginModel",&model);
            QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/PluginSlot.qml")));
            QVariantMap row{{"id","probe"},{"width",200},{"effectiveWidth",200},{"viewModel",QVariant::fromValue<QObject*>(nullptr)},
                {"pluginContext",QVariant::fromValue<QObject*>(nullptr)},{"qmlUrl",QUrl::fromLocalFile(file.fileName())},{"loadDurationMs",0}};
            std::unique_ptr<QObject> slot(component.createWithInitialProperties({{"model",row}}));
            QVERIFY2(slot,qPrintable(component.errorString()));
            QTRY_COMPARE(model.naturalWidth,332);
            QObject* probe=nullptr;
            for(QObject* object:slot->findChildren<QObject*>())
                if(object->metaObject()->indexOfProperty("plugin")>=0) {probe=object;break;}
            QVERIFY(probe);
            QCOMPARE(probe->property("width").toInt(),optIn?188:320);
            row["width"]=480;row["effectiveWidth"]=480;slot->setProperty("model",row);
            QTRY_COMPARE(probe->property("width").toInt(),optIn?468:320);
            QCOMPARE(model.naturalWidth,332);
            row["width"]=8;row["effectiveWidth"]=8;slot->setProperty("model",row);
            QTRY_COMPARE(probe->property("width").toInt(),optIn?0:320);
            QVERIFY2(warnings.isEmpty(),"宽度注入不得形成绑定循环");
        }
    }

    void sharedControllerAndRareEvents() {
        QQmlEngine engine;
        QQmlComponent component(&engine,QUrl::fromLocalFile(pluginSource("LandscapeController.qml")));
        std::unique_ptr<QObject> controller(component.create());
        QVERIFY2(controller,qPrintable(component.errorString()));
        controller->setProperty("phase","night");
        controller->setProperty("weather","clear");
        controller->setProperty("lastTick",1000.0);
        controller->setProperty("nextMeteor",0.05);
        QVERIFY(QMetaObject::invokeMethod(controller.get(),"advance",Q_ARG(QVariant,1100.0)));
        QCOMPARE(controller->property("animationTime").toDouble(),0.1);
        QVERIFY(controller->property("meteor").toDouble()>=0);
        const double wait=controller->property("nextMeteor").toDouble()-0.1;
        QVERIFY(wait>=480 && wait<720);
        // 大幅校时/休眠恢复不累计时间，也不补播流星。
        QVERIFY(QMetaObject::invokeMethod(controller.get(),"advance",Q_ARG(QVariant,100000.0)));
        QCOMPARE(controller->property("animationTime").toDouble(),0.1);
        QCOMPARE(controller->property("meteor").toDouble(),-1.0);
        controller->setProperty("rareEventsEnabled",false);
        controller->setProperty("nextMeteor",0.15);
        QVERIFY(QMetaObject::invokeMethod(controller.get(),"advance",Q_ARG(QVariant,100100.0)));
        QCOMPARE(controller->property("meteor").toDouble(),-1.0);

        PixelLandscapeViewModel vm;
        controller->setProperty("backend",QVariant::fromValue<QObject*>(&vm));
        controller->setProperty("shown",true);
        controller->setProperty("enabled",true);
        vm.refreshAt(QDateTime(QDate(2026,11,8),QTime(19,59,45)),true);
        vm.refreshAt(QDateTime(QDate(2026,11,8),QTime(20,0,15)));
        QCOMPARE(controller->property("blend").toDouble(),0.0);
        const double tick=controller->property("lastTick").toDouble();
        for(int i=1;i<=20;++i)
            QVERIFY(QMetaObject::invokeMethod(controller.get(),"advance",Q_ARG(QVariant,tick+i*1000)));
        QCOMPARE(controller->property("blend").toDouble(),1.0);
        controller->setProperty("backend",QVariant::fromValue<QObject*>(nullptr));
    }

    void englishPopupAndPausedReopen() {
        PixelTranslator translator;
        QFile english(pluginSource("i18n/en-US.json"));QVERIFY(english.open(QIODevice::ReadOnly));
        translator.contexts=QJsonDocument::fromJson(english.readAll()).object()["contexts"].toObject();
        QFile chinese(pluginSource("i18n/zh-CN.json"));QVERIFY(chinese.open(QIODevice::ReadOnly));
        const auto chineseContexts=QJsonDocument::fromJson(chinese.readAll()).object()["contexts"].toObject();
        QCOMPARE(translator.contexts.keys(),chineseContexts.keys());
        for(const QString& key:chineseContexts.keys())
            QCOMPARE(translator.contexts[key].toObject().keys(),chineseContexts[key].toObject().keys());
        QCoreApplication::installTranslator(&translator);
        PixelLandscapeViewModel vm;vm.applyConfig({{"timeMode","dusk"},{"weather","rain"}});vm.setPaused(true);
        PopupService popups;PixelContext context;context.popups=&popups;context.screenWidth=400;
        QQuickView view;view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        QCOMPARE(view.status(),QQuickView::Ready);
        QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        auto* root=view.rootObject();
        root->setProperty("pluginContext",QVariant::fromValue<QObject*>(&context));
        root->setProperty("plugin",QVariant::fromValue<QObject*>(&vm));
        view.show();
        auto* popup=root->findChild<QQuickWindow*>("landscapePopup");
        QVERIFY(popup);popup->setProperty("closeOnDeactivate",false);
        for(int i=0;i<2;++i) {
            QVERIFY(QMetaObject::invokeMethod(root,"openLandscape"));
            QTRY_VERIFY(popup->isVisible());
            auto* loader=root->findChild<QObject*>("landscapePopupLoader");
            QTRY_VERIFY(loader->property("item").value<QObject*>());
            QTest::qWait(100);
            QCOMPARE(root->property("statusText").toString(),QStringLiteral("Dusk · Rain"));
            auto* actions=loader->property("item").value<QObject*>()->findChild<QQuickItem*>("landscapeActions");
            QVERIFY(actions);QVERIFY(actions->width()+24<=popup->width());
            QCOMPARE(vm.paused(),true);
            popup->hide();QTest::qWait(20);
        }
        vm.applyConfig({{"animationsEnabled",false},{"weather","snow"}});
        QVERIFY(QMetaObject::invokeMethod(root,"togglePause"));
        QVERIFY(vm.paused());QCOMPARE(vm.weather(),QStringLiteral("snow"));
        const QList<QPair<QString,QString>> sceneNames = {
            {"cabin","Mountain cabin"},{"lighthouse","Seaside lighthouse"},{"street","Rainy street corner"},
            {"campsite","Lakeside campsite"},{"railway","Country railway"},{"island","Floating island"}};
        for(const auto& pair:sceneNames) {
            vm.applyConfig({{"scene",pair.first},{"animationsEnabled",false}});
            QCOMPARE(root->property("sceneLabel").toString(),pair.second);
        }
        QCoreApplication::removeTranslator(&translator);
        QVERIFY2(warnings.isEmpty(),"窄屏英文弹窗不得出现尺寸绑定循环");
    }

    void frameStylesAndHotUpdate() {
        PixelLandscapeViewModel vm;
        vm.setPaused(true);
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        QCOMPARE(view.status(),QQuickView::Ready);
        QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        auto* root=view.rootObject();
        root->setProperty("plugin",QVariant::fromValue<QObject*>(&vm));
        auto* frame=root->findChild<QQuickItem*>("landscapeFrame");
        auto* scene=root->findChild<QQuickItem*>("barScene");
        auto* controller=root->findChild<QObject*>("landscapeController");
        QVERIFY(frame&&scene&&controller);
        view.resize(320,40);view.show();
        QFile manifest(pluginSource("metadata.json"));QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto groups=QJsonDocument::fromJson(manifest.readAll()).object()["settings"].toArray();
        QJsonObject field;
        for(const auto group:groups)for(const auto value:group.toObject()["fields"].toArray())
            if(value.toObject()["key"].toString()==QStringLiteral("frameStyle"))field=value.toObject();
        QCOMPARE(field["default"].toString(),vm.frameStyle());
        QCOMPARE(field["options"].toArray().size(),5);
        for(const auto value:field["options"].toArray()) {
            const QString style=value.toObject()["value"].toString();
            QVERIFY(vm.applyConfig({{"timeMode","dusk"},{"frameStyle",style}}));
            QTRY_COMPARE(root->property("frameStyle").toString(),style);
            QTest::qWait(20);
            const int inset=frame->property("inset").toInt();
            QVERIFY(style==QStringLiteral("none") ? inset==0 : inset>0);
            QCOMPARE(scene->width(),320.0-2*inset);
            QCOMPARE(root->implicitWidth(),320.0);
            QVERIFY(vm.paused());
            QCOMPARE(controller->property("animationTime").toDouble(),0.0);
            view.resize(24,28);
            QTRY_COMPARE(frame->property("inset").toInt(),0);
            QTRY_COMPARE(scene->width(),24.0);
            view.resize(320,5);
            QTRY_COMPARE(frame->property("inset").toInt(),0);
            QTRY_COMPARE(scene->height(),5.0);
            view.resize(320,40);
        }
        QVERIFY(warnings.isEmpty());
    }

    void renderFrameStyles() {
        QTemporaryDir dir;
        QFile file(dir.filePath("Frames.qml"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral(R"(
import QtQuick
import "%1" as Landscape
Rectangle {
 width: 1040; height: 520; color: "#182330"
 Row {
  Repeater { model: 2
   delegate: Rectangle {
    id: column
    required property int index
    width: 520; height: 520; color: index ? "#f1efe9" : "#20252e"
    readonly property var tones: index ? ({ panel: "#ffffff", border: "#b8bcc4", accent: "#32689a" })
                                      : ({ panel: "#20252e", border: "#495464", accent: "#b1c8e5" })
    Text { x: 20; y: 12; text: column.index ? "Light · 320 × 40 / 160 × 28" : "Dark · 320 × 40 / 160 × 28"; color: column.index ? "#343b45" : "#e6decd"; font.pixelSize: 14 }
    Column { x: 20; y: 42; spacing: 8
     Repeater { model: ["none","thin","wood","mat","metal"]
      delegate: Item {
       id: row
       required property string modelData
       required property int index
       width: 480; height: 86
       Text { text: ["无画框 / None","极简细框 / Thin","暖木框 / Wood","白色卡纸框 / Mat","像素金属框 / Metal"][row.index]; color: column.index ? "#343b45" : "#e6decd"; font.pixelSize: 12 }
       Item { y: 25; width: 320; height: 40
        Landscape.LandscapeFrame { id: largeFrame; anchors.fill: parent; frameStyle: row.modelData; colors: column.tones }
        Landscape.LandscapeScene { anchors.fill: parent; anchors.margins: largeFrame.inset; phase: "dusk"; previousPhase: phase; animationTime: 17 }
       }
       Item { x: 332; y: 31; width: 160; height: 28
        Landscape.LandscapeFrame { id: smallFrame; anchors.fill: parent; frameStyle: row.modelData; colors: column.tones }
        Landscape.LandscapeScene { anchors.fill: parent; anchors.margins: smallFrame.inset; phase: "day"; previousPhase: phase; animationTime: 17 }
       }
      }
     }
    }
   }
  }
 }
})").arg(QUrl::fromLocalFile(pluginSource("")).toString()).toUtf8());file.close();
        QQuickView view;
        QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        view.setSource(QUrl::fromLocalFile(file.fileName()));
        QCOMPARE(view.status(),QQuickView::Ready);view.show();QTest::qWait(300);
        const QImage image=view.grabWindow();QVERIFY(!image.isNull());
        const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(image.save(output+"/landscape-frames.png"));}
        QVERIFY(warnings.isEmpty());
    }

    void scenesAndHotUpdate() {
        PixelLandscapeViewModel vm;vm.setPaused(true);
        PopupService popups;PixelContext context;context.popups=&popups;
        QQuickView view;
        QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        QCOMPARE(view.status(),QQuickView::Ready);
        auto* root=view.rootObject();
        root->setProperty("pluginContext",QVariant::fromValue<QObject*>(&context));
        root->setProperty("plugin",QVariant::fromValue<QObject*>(&vm));view.show();
        auto* popup=root->findChild<QQuickWindow*>("landscapePopup");
        auto* bar=root->findChild<QObject*>("barScene");
        auto* controller=root->findChild<QObject*>("landscapeController");
        auto* loader=root->findChild<QObject*>("landscapePopupLoader");
        QVERIFY(popup&&bar&&controller&&loader);popup->setProperty("closeOnDeactivate",false);
        QVERIFY(QMetaObject::invokeMethod(root,"openLandscape"));
        QTRY_VERIFY(loader->property("item").value<QObject*>());
        auto* body=loader->property("item").value<QObject*>();
        auto* expanded=body->findChild<QObject*>("expandedScene");QVERIFY(expanded);
        QFile file(pluginSource("metadata.json"));QVERIFY(file.open(QIODevice::ReadOnly));
        const auto groups=QJsonDocument::fromJson(file.readAll()).object()["settings"].toArray();
        QJsonObject field;
        for(const auto group:groups)for(const auto value:group.toObject()["fields"].toArray())
            if(value.toObject()["key"].toString()==QStringLiteral("scene"))field=value.toObject();
        QCOMPARE(field["default"].toString(),vm.scene());QCOMPARE(field["options"].toArray().size(),6);
        for(const auto value:field["options"].toArray()) {
            const QString scene=value.toObject()["value"].toString();
            QVERIFY(vm.applyConfig({{"scene",scene},{"timeMode","night"},{"weather","snow"},{"frameStyle","metal"}}));
            QTRY_COMPARE(bar->property("sceneType").toString(),scene);
            QTRY_COMPARE(expanded->property("sceneType").toString(),scene);
            QCOMPARE(root->property("sceneLabel").toString(),value.toObject()["label"].toString());
            QCOMPARE(vm.weather(),QStringLiteral("snow"));QCOMPARE(vm.frameStyle(),QStringLiteral("metal"));
            QVERIFY(vm.paused());QCOMPARE(controller->property("animationTime").toDouble(),0.0);
            QCOMPARE(loader->property("item").value<QObject*>(),body);
            QTest::qWait(80);
            const int frozen=bar->property("dynamicPaintCount").toInt();
            QTest::qWait(120);QCOMPARE(bar->property("dynamicPaintCount").toInt(),frozen);
        }
        popup->hide();QTRY_VERIFY(!loader->property("item").value<QObject*>());
        QVERIFY(QMetaObject::invokeMethod(root,"openLandscape"));
        QTRY_VERIFY(loader->property("item").value<QObject*>());
        QCOMPARE(loader->property("item").value<QObject*>()->findChild<QObject*>("expandedScene")->property("sceneType").toString(),QStringLiteral("island"));
        QVERIFY(vm.paused());
        QVERIFY2(warnings.isEmpty(),"切换场景不得重建弹窗、重置暂停或产生 QML 警告");
    }

    void renderSceneOverview() {
        QTemporaryDir dir;
        QFile file(dir.filePath("Scenes.qml"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral(R"(
import QtQuick
import "%1" as Landscape
Rectangle {
 width: 1008; height: 1068; color: "#182330"
 Grid { columns: 2
  Repeater { model: ["cabin","lighthouse","street","campsite","railway","island"]
   delegate: Item {
    required property int index
    required property string modelData
    width: 504; height: 356
    readonly property string timeOfDay: ["dusk","night","night","dusk","day","dawn"][index]
    readonly property string climate: index === 2 ? "rain" : "clear"
    Text { x: 12; y: 9; color: "#e6decd"; font.pixelSize: 16; text: ["山间小屋","海边灯塔","夜雨街角","湖畔营地","田野铁道","空中浮岛"][parent.index] }
    Landscape.LandscapeScene { x: 12; y: 36; width: 480; height: 270; expanded: true; sceneType: parent.modelData; phase: parent.timeOfDay; previousPhase: phase; weather: parent.climate; animationTime: 3 }
    Landscape.LandscapeFrame { x: 12; y: 314; width: 480; height: 32; frameStyle: "wood" }
    Landscape.LandscapeScene { x: 16; y: 318; width: 472; height: 24; sceneType: parent.modelData; phase: parent.timeOfDay; previousPhase: phase; weather: parent.climate; animationTime: 3 }
   }
  }
 }
})").arg(QUrl::fromLocalFile(pluginSource("")).toString()).toUtf8());file.close();
        QQuickView view;QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        view.setSource(QUrl::fromLocalFile(file.fileName()));QCOMPARE(view.status(),QQuickView::Ready);
        view.show();QTest::qWait(400);
        const QImage image=view.grabWindow();QVERIFY(!image.isNull());
        const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(image.save(output+"/landscape-scenes.png"));}
        QVERIFY(warnings.isEmpty());
    }

    void renderAllCombinations() {
        const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        QTemporaryDir dir;
        QFile file(dir.filePath("Gallery.qml"));QVERIFY(file.open(QIODevice::WriteOnly));
        const QString qml=QStringLiteral(R"(
import QtQuick
import "%1" as Landscape
Rectangle {
 id: gallery
 property string sceneName: "cabin"
 width: 960; height: 864; color: "#182330"
 Grid {
  columns: 3
  Repeater {
   model: 12
   delegate: Item {
    required property int index
    width: 320; height: 216
    readonly property string phaseName: ["dawn","day","dusk","night"][Math.floor(index/3)]
    readonly property string weatherName: ["clear","rain","snow"][index%3]
    Landscape.LandscapeScene { y: 30; width: 320; height: 180; expanded: true; sceneType: gallery.sceneName; phase: parent.phaseName; previousPhase: phase; weather: parent.weatherName; animationTime: 3 }
    Text { x: 12; y: 8; text: parent.phaseName + " / " + parent.weatherName; color: "#e6decd"; font.pixelSize: 12 }
   }
  }
 }
})").arg(QUrl::fromLocalFile(pluginSource("")).toString());
        file.write(qml.toUtf8());file.close();
        QQuickView view;QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        view.setSource(QUrl::fromLocalFile(file.fileName()));
        QCOMPARE(view.status(),QQuickView::Ready);
        view.show();QTest::qWait(500);
        QImage previous;
        for(const auto& name:{"cabin","lighthouse","street","campsite","railway","island"}) {
            view.rootObject()->setProperty("sceneName",name);QTest::qWait(160);
            const QImage image=view.grabWindow();QVERIFY(!image.isNull());
            QVERIFY(image.pixelColor(100,100)!=image.pixelColor(100,748));
            QVERIFY(image!=previous);previous=image;
            if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(image.save(output+"/landscape-gallery-"+name+".png"));}
        }
        QVERIFY(warnings.isEmpty());
    }

    void renderAdaptiveWidths() {
        QTemporaryDir dir;
        QFile file(dir.filePath("Widths.qml"));QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral(R"(
import QtQuick
import "%1" as Landscape
Rectangle {
 id: gallery
 property string sceneName: "cabin"
 width: 520; height: 720; color: "#182330"
 Column { x: 20; y: 12; spacing: 12
  Repeater { model: [160,320,480,64,24,8]
   delegate: Item { required property int modelData; width: 480; height: 104
    Text { text: modelData + " px"; color: "#e6decd"; font.pixelSize: 13 }
    Landscape.LandscapeScene { y: 26; width: parent.modelData; height: 40; sceneType: gallery.sceneName; phase: "dusk"; previousPhase: phase; animationTime: 3 }
    Landscape.LandscapeScene { y: 70; width: parent.modelData; height: 28; sceneType: gallery.sceneName; phase: "night"; previousPhase: phase; weather: "snow"; animationTime: 3 }
   }
  }
 }
})").arg(QUrl::fromLocalFile(pluginSource("")).toString()).toUtf8());file.close();
        QQuickView view;QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        view.setSource(QUrl::fromLocalFile(file.fileName()));
        QCOMPARE(view.status(),QQuickView::Ready);view.show();QTest::qWait(300);
        const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        for(const auto& name:{"cabin","lighthouse","street","campsite","railway","island"}) {
            view.rootObject()->setProperty("sceneName",name);QTest::qWait(160);
            const QImage image=view.grabWindow();QVERIFY(!image.isNull());
            if(!output.isEmpty())QVERIFY(image.save(output+"/landscape-widths-"+name+".png"));
        }
        QVERIFY(warnings.isEmpty());
    }
};

// 独立的性能/长运行入口与 CTest 分离，避免常规测试等待半小时。
static int runProbe(QGuiApplication& app, const QStringList& args) {
    const int at=args.indexOf(QStringLiteral("--probe"));
    const QString scenario=args.value(at+1,QStringLiteral("bar"));
    const int seconds=qMax(1,args.value(at+2,QStringLiteral("60")).toInt());
    const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
    QTemporaryDir dir;
    PixelLandscapeViewModel vm;
    vm.applyConfig({{"timeMode","night"},{"weather","rain"},
        {"scene",qEnvironmentVariable("XUANHUB_PIXEL_SCENE",QStringLiteral("cabin"))}});vm.start();
    PopupService popups;PixelContext context;context.popups=&popups;
    QQuickView view;
    view.setTitle(QStringLiteral("Pixel Landscape verification"));
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    if(scenario==QStringLiteral("baseline") || scenario==QStringLiteral("shell")) {
        QFile file(dir.filePath("Baseline.qml"));file.open(QIODevice::WriteOnly);
        file.write(R"(import QtQuick
import QtQuick.Controls
import XuanHub
Rectangle {
 property var pluginContext
 width: 320; height: 40; color: "#20252e"
 function openLandscape() { popup.toggleFromBar() }
 PluginPopup { id: popup; objectName: "landscapePopup"; anchorItem: parent; pluginContext: parent.pluginContext
  width: 640; height: 408
  Rectangle { anchors.fill: parent; color: "#20252e" }
 }
})");file.close();
        view.setSource(QUrl::fromLocalFile(file.fileName()));
        view.rootObject()->setProperty("pluginContext",QVariant::fromValue<QObject*>(&context));
    } else {
        view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        if(view.status()!=QQuickView::Ready)return 2;
        view.rootObject()->setProperty("pluginContext",QVariant::fromValue<QObject*>(&context));
        view.rootObject()->setProperty("plugin",QVariant::fromValue<QObject*>(&vm));
    }
    view.setGeometry(100,100,320,40);view.show();
    auto* popup=view.rootObject()->findChild<QQuickWindow*>("landscapePopup");
    if(popup)popup->setProperty("closeOnDeactivate",false);
    if(scenario==QStringLiteral("popup") || scenario==QStringLiteral("shell"))
        QTimer::singleShot(300,view.rootObject(),[&]{QMetaObject::invokeMethod(view.rootObject(),"openLandscape");});
    int cycle=0;
    QTimer cycles;
    if(scenario==QStringLiteral("cycles")) {
        cycles.setInterval(500);
        QObject::connect(&cycles,&QTimer::timeout,&app,[&]{
            if(++cycle>60){cycles.stop();return;}
            if(cycle%2)QMetaObject::invokeMethod(view.rootObject(),"openLandscape");
            else if(popup)popup->hide();
        });
        cycles.start();
    }
    QTimer::singleShot(seconds*1000,&app,[&]{
        if(!output.isEmpty()) {
            QDir().mkpath(output);
            view.grabWindow().save(output+"/probe-"+scenario+".png");
            if(popup&&popup->isVisible())popup->grabWindow().save(output+"/probe-popup-expanded.png");
            const auto* scene=view.rootObject()->findChild<QObject*>("barScene");
            QJsonObject report{{"scenario",scenario},{"scene",vm.scene()},{"seconds",seconds},{"cycles",qMin(60,cycle)/2},
                {"staticPaintCount",scene?scene->property("staticPaintCount").toInt():0},
                {"dynamicPaintCount",scene?scene->property("dynamicPaintCount").toInt():0}};
            QFile file(output+"/probe-"+scenario+".json");file.open(QIODevice::WriteOnly);file.write(QJsonDocument(report).toJson());
        }
        app.quit();
    });
    return app.exec();
}

int main(int argc,char** argv) {
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc,argv);
    // 离屏平台不枚举 Windows 字体；只在测试中显式载入系统字体以生成可读检查图。
    if(QGuiApplication::platformName()==QStringLiteral("offscreen")) {
        const QString fonts=qEnvironmentVariable("WINDIR")+QStringLiteral("/Fonts/");
        QFontDatabase::addApplicationFont(fonts+QStringLiteral("segoeui.ttf"));
        QFontDatabase::addApplicationFont(fonts+QStringLiteral("msyh.ttc"));
    }
    qputenv("QT_QUICK_CONTROLS_STYLE","Basic");
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/PluginPopup.qml")),"XuanHub",1,0,"PluginPopup");
    qmlRegisterSingletonType<PixelUiMetrics>("XuanHub",1,0,"UiMetrics",[](QQmlEngine*,QJSEngine*) -> QObject* {return new PixelUiMetrics;});
    if(app.arguments().contains(QStringLiteral("--probe")))return runProbe(app,app.arguments());
    PixelLandscapeTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "PixelLandscapeTest.moc"
