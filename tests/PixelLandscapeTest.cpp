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
#include <QTimeZone>
#include <QPainter>
#include <cmath>
#include <limits>
#include <windows.h>
#include "PixelLandscapeViewModel.h"
#include "SolarCalculator.h"
#include "barplugin/HostServices.h"
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
    int screenHeight = 900;
    QVariantMap colors() const { return {{"panel", "#20252e"}, {"fg", "#eef0f4"}, {"border", "#495464"}, {"accent", "#b1c8e5"}}; }
    QVariantMap screenGeometry() const { return {{"x", 0}, {"y", 0}, {"width", screenWidth}, {"height", screenHeight}}; }
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

class FakeLandscapeLocation final : public LandscapeLocation {
public:
    QString permission = QStringLiteral("prompt");
    quint64 current = 0;
    int requests = 0;
    bool interactive = false;
    QString access() override { return permission; }
    void request(quint64 id, bool prompt) override { current=id; ++requests; interactive=prompt; }
    void cancel() override {}
    void finish(const QString& error = {}, QDateTime time = QDateTime::currentDateTimeUtc()) {
        Q_EMIT finished(current,error,39.9,116.4,time);
    }
};

class PixelSettingsDraft final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY valuesChanged)
public:
    QVariantMap values;
    int count = 0;
    QString pluginId() const { return QStringLiteral("pixel_landscape"); }
    int revision() const { return count; }
    Q_INVOKABLE QVariant value(const QString& key) const { return values.value(key); }
    Q_INVOKABLE void setValue(const QString& key,const QVariant& value) { values[key]=value; ++count; Q_EMIT valuesChanged(); }
Q_SIGNALS:
    void valuesChanged();
};

class PixelLocationStorage final : public xuanhub::IPluginStorage {
public:
    QByteArray bytes;
    bool writable = true;
    xuanhub::HostServiceResult readBytes(const QString&) const override { return xuanhub::HostServiceResult::succeeded(bytes); }
    xuanhub::HostServiceResult writeBytes(const QString&, const QByteArray& value) override {
        if(!writable)return xuanhub::HostServiceResult::failed("test_error",{});
        bytes=value; return xuanhub::HostServiceResult::succeeded();
    }
    xuanhub::HostServiceResult removePath(const QString&) override { bytes.clear();return xuanhub::HostServiceResult::succeeded(); }
    xuanhub::HostServiceResult list(const QString&) const override { return xuanhub::HostServiceResult::succeeded(); }
};

class PixelLandscapeTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void goldenHourSelectionAndConditions() {
        PixelLandscapeViewModel vm(nullptr,new FakeLandscapeLocation);
        QVERIFY(vm.applyConfig({}));
        QCOMPARE(vm.m_goldenHourMode,QStringLiteral("random"));
        const QTimeZone zone=QTimeZone::fromSecondsAheadOfUtc(8*3600);
        const QDate day(2026,6,21);
        const QVariantMap sun{{"active",true},{"elevation",1.0},{"twilightMix",0.0}};
        int selectedDays=0;
        for(int i=0;i<3650;++i) {
            const QDateTime morning(day.addDays(i),QTime(5,0),zone);
            vm.refreshGoldenHour(morning,sun);
            const bool selected=vm.goldenHourState().value("selected").toBool();
            selectedDays+=selected;
            vm.refreshGoldenHour(morning.addSecs(14*3600),sun);
            QCOMPARE(vm.goldenHourState().value("selected").toBool(),selected);
        }
        QVERIFY(selectedDays>1100 && selectedDays<1330);
        vm.refreshGoldenHour(QDateTime(day,QTime(5,0),zone),sun);
        PixelLandscapeViewModel restarted(nullptr,new FakeLandscapeLocation);restarted.applyConfig({});
        restarted.refreshGoldenHour(QDateTime(day,QTime(19,0),zone),sun);
        QCOMPARE(restarted.goldenHourState().value("selected"),vm.goldenHourState().value("selected"));
        for(const QVariant& mode:{QVariant("unknown"),QVariant(42),QVariant(true)})
            QVERIFY(!vm.applyConfig({{"goldenHourMode",mode}}));
        QVariantMap config{{"locationSource","manual"},{"manualLocationEnabled",true},
            {"latitude",39.9},{"longitude",116.4},{"goldenHourMode","always"},{"animationsEnabled",false}};
        QVERIFY(vm.applyConfig(config));vm.setPaused(true);
        for(const QTime& time:{QTime(5,0),QTime(19,30),QTime(12,0),QTime(23,0)}) {
            vm.refreshAt(QDateTime(day,time,zone));
            const double strength=vm.goldenHourState().value("strength").toDouble();
            QVERIFY(std::isfinite(strength) && strength>=0 && strength<=1);
            if(time.hour()==5 || time.hour()==19) QVERIFY(strength>0.7);
            else QCOMPARE(strength,0.0);
            QVERIFY(vm.paused());
        }
        // 精确覆盖光照包络边界，不用本实现的太阳公式生成时间预期值。
        for(double elevation:{-10.0,-4.0,0.0,1.0,2.0,6.0,40.0}) {
            auto sample=sun;sample["elevation"]=elevation;
            vm.refreshGoldenHour(QDateTime(day,QTime(5,0),zone),sample);
            QCOMPARE(vm.goldenHourState().value("strength").toDouble(),
                elevation>=0 && elevation<=2 ? 1.0 : 0.0);
        }
        for(const QString& weather:{"rain","snow"}) {
            config["weather"]=weather;QVERIFY(vm.applyConfig(config));
            vm.refreshAt(QDateTime(day,QTime(5,0),zone));
            QCOMPARE(vm.goldenHourState().value("strength").toDouble(),0.0);
        }
        QVERIFY(vm.applyConfig({{"timeMode","dawn"},{"goldenHourMode","always"}}));
        QCOMPARE(vm.goldenHourState().value("strength").toDouble(),1.0);
        QCOMPARE(vm.goldenHourState().value("evening").toDouble(),0.0);
        QVERIFY(vm.applyConfig({{"timeMode","dusk"},{"goldenHourMode","always"}}));
        QCOMPARE(vm.goldenHourState().value("evening").toDouble(),1.0);
        QVERIFY(vm.applyConfig({{"timeMode","dusk"},{"goldenHourMode","off"}}));
        QCOMPARE(vm.goldenHourState().value("strength").toDouble(),0.0);
        QVERIFY(vm.applyConfig({{"goldenHourMode","always"}}));
        QCOMPARE(vm.goldenHourState().value("strength").toDouble(),0.0);
    }

    void goldenHourRenderingAndBudget() {
        QQuickView view;view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl::fromLocalFile(pluginSource("LandscapeScene.qml")));
        QCOMPARE(view.status(),QQuickView::Ready);
        QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        auto* root=view.rootObject();root->setProperty("expanded",true);view.resize(320,180);view.show();
        const QTimeZone zone=QTimeZone::fromSecondsAheadOfUtc(8*3600);
        const auto dawn=landscape::solarState(QDateTime(QDate(2026,6,21),QTime(5,0),zone),39.9,116.4);
        const auto dusk=landscape::solarState(QDateTime(QDate(2026,6,21),QTime(19,30),zone),39.9,116.4);
        const QVariantMap morning{{"strength",1.0},{"evening",0.0}};
        const QVariantMap evening{{"strength",1.0},{"evening",1.0}};
        QTRY_VERIFY(root->property("staticPaintCount").toInt()>0);
        QImage gallery(960,6*208,QImage::Format_RGB32);gallery.fill(QColor("#182330"));QPainter painter(&gallery);
        const QStringList scenes{"cabin","lighthouse","street","campsite","railway","island"};
        for(int row=0;row<scenes.size();++row)for(int col=0;col<3;++col) {
            root->setProperty("sceneType",scenes[row]);root->setProperty("solarState",col==2?dusk:dawn);
            root->setProperty("goldenHourState",col==0?QVariantMap{}:col==1?morning:evening);
            QTest::qWait(40);const auto image=view.grabWindow();QVERIFY(!image.isNull());
            QVERIFY(image.pixelColor(20,20)!=image.pixelColor(20,160));
            painter.drawImage(col*320,row*208+28,image);painter.setPen(Qt::white);
            painter.drawText(col*320+8,row*208+19,scenes[row]+QStringList{" normal"," sunrise glow"," sunset colors"}[col]);
        }
        painter.end();
        root->setProperty("solarState",dawn);root->setProperty("goldenHourState",morning);QTest::qWait(40);
        const int paints=root->property("staticPaintCount").toInt();
        auto moved=dawn;moved["sunX"]=0.6;root->setProperty("solarState",moved);QTest::qWait(40);
        QCOMPARE(root->property("staticPaintCount").toInt(),paints);
        for(const QString& weather:{"rain","snow"}) {
            root->setProperty("goldenHourState",morning);
            root->setProperty("weather",weather);QTest::qWait(40);const auto withEffect=view.grabWindow();
            root->setProperty("goldenHourState",QVariantMap{});QTest::qWait(40);
            QCOMPARE(view.grabWindow(),withEffect);
        }
        const auto output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(gallery.save(output+"/golden-hour-gallery.png"));}
        QVERIFY(warnings.isEmpty());
    }

    void citySettingsBilingualRendering() {
        for(const QString& locale:{"zh-CN","en-US"}) {
            PixelTranslator translator;
            QFile file(pluginSource("i18n/"+locale+".json"));QVERIFY(file.open(QIODevice::ReadOnly));
            translator.contexts=QJsonDocument::fromJson(file.readAll()).object()["contexts"].toObject();
            QCoreApplication::installTranslator(&translator);
            for(int width:{360,620}) {
                PixelSettingsDraft draft;draft.values={{"manualLocationEnabled",true},{"latitude",39.9075},{"longitude",116.39723}};
                QQuickView view;QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
                view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscapeSettings.qml")));
                QCOMPARE(view.status(),QQuickView::Ready);
                auto* root=view.rootObject();root->setWidth(width);
                root->setProperty("plugin",QVariant::fromValue<QObject*>(&draft));
                QTest::qWait(30);view.resize(width,qCeil(root->implicitHeight()));view.show();QTest::qWait(50);
                auto* city=root->findChild<QObject*>("landscapeCity");QVERIFY(city);
                QCOMPARE(city->property("currentIndex").toInt(),1);
                QCOMPARE(city->property("currentText").toString(),locale=="zh-CN"?QStringLiteral("北京"):QStringLiteral("Beijing"));
                const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
                if(!output.isEmpty())QVERIFY(view.grabWindow().save(output+"/city-settings-"+locale+"-"+QString::number(width)+".png"));
                QCOMPARE(draft.count,0);QVERIFY(warnings.isEmpty());
            }
            QCoreApplication::removeTranslator(&translator);
        }
    }

    void nativeLocationLive() {
        if(!qEnvironmentVariableIsSet("XUANHUB_LIVE_LOCATION_TEST"))QSKIP("真实位置服务由独立验收显式启用");
        WindowsLandscapeLocation location;
        if(location.access()!=QStringLiteral("allowed"))QSKIP("未授予定位权限，不在测试中触发授权");
        QSignalSpy replies(&location,&LandscapeLocation::finished);
        location.request(1,false);
        QTRY_COMPARE_WITH_TIMEOUT(replies.size(),1,22000);
        const auto reply=replies.takeFirst();
        const QString error=reply.at(1).toString();
        qInfo()<<"Windows live location result:"<<(error.isEmpty()?QStringLiteral("success"):error);
        if(error.isEmpty()) {
            QVERIFY(landscape::validCoordinates(reply.at(2).toDouble(),reply.at(3).toDouble()));
            QVERIFY(reply.at(4).toDateTime().isValid());
        } else QVERIFY(QStringList({"denied","timeout","unavailable","prompt"}).contains(error));
        location.request(2,false);location.cancel();QTest::qWait(300);
        QVERIFY(replies.isEmpty());
    }

    void nativeLocationAccessReadOnly() {
        WindowsLandscapeLocation location;
        const QString status=location.access();
        QVERIFY(QStringList({"allowed","denied","prompt","unavailable"}).contains(status));
        qInfo()<<"Windows read-only location access:"<<status;
        location.cancel();
    }

    void solarEnglishFallbackPopup() {
        PixelTranslator translator;
        QFile file(pluginSource("i18n/en-US.json"));QVERIFY(file.open(QIODevice::ReadOnly));
        translator.contexts=QJsonDocument::fromJson(file.readAll()).object()["contexts"].toObject();
        QCoreApplication::installTranslator(&translator);
        auto* location=new FakeLandscapeLocation;location->permission="denied";
        PixelLandscapeViewModel vm(nullptr,location);vm.applyConfig({});vm.start();
        PopupService popups;PixelContext context;context.popups=&popups;context.screenWidth=700;context.screenHeight=450;
        QQuickView view;view.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        QCOMPARE(view.status(),QQuickView::Ready);QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        auto* root=view.rootObject();root->setProperty("pluginContext",QVariant::fromValue<QObject*>(&context));
        root->setProperty("plugin",QVariant::fromValue<QObject*>(&vm));view.show();
        auto* popup=root->findChild<QQuickWindow*>("landscapePopup");popup->setProperty("closeOnDeactivate",false);
        QVERIFY(QMetaObject::invokeMethod(root,"openLandscape"));QTRY_VERIFY(popup->isVisible());QTest::qWait(60);
        QVERIFY(popup->height()<=context.screenHeight-24);QVERIFY(popup->width()<=context.screenWidth-24);
        auto* info=root->findChild<QQuickItem*>("landscapeLocationInfo");QVERIFY(info);
        QVERIFY(info->y()+info->implicitHeight()<=popup->height());
        auto* button=root->findChild<QQuickItem*>("landscapeLocateButton");QVERIFY(button);
        QTest::mouseClick(popup,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(10,15)).toPoint());
        QCOMPARE(location->requests,1);QVERIFY(location->interactive);
        location->finish("denied");QTest::qWait(30);
        const auto output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        if(!output.isEmpty())QVERIFY(popup->grabWindow().save(output+"/solar-popup-english-fallback.png"));
        QVERIFY(warnings.isEmpty());QCoreApplication::removeTranslator(&translator);
    }

    void solarIndependentReference() {
        QFile file(QStringLiteral(XUANHUB_SOURCE_DIR "/tests/fixtures/pixel-solar-usno.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto cases=QJsonDocument::fromJson(file.readAll()).array();
        QVERIFY(cases.size()>=6);
        int maximumError=0;
        for(const auto item:cases) {
            const auto row=item.toObject();
            const auto zone=QTimeZone::fromSecondsAheadOfUtc(row["offset"].toInt()*3600);
            const auto date=QDate::fromString(row["date"].toString(),Qt::ISODate);
            const auto result=landscape::solarState(QDateTime(date,QTime(12,0),zone),row["latitude"].toDouble(),row["longitude"].toDouble());
            const QMap<QString,QString> names{{"Rise","sunrise"},{"Set","sunset"},{"Upper Transit","solarNoon"},
                {"Begin Civil Twilight","dawn"},{"End Civil Twilight","dusk"}};
            for(const auto event:row["events"].toArray()) {
                const auto e=event.toObject();
                const QString key=names.value(e["phen"].toString());
                if(key.isEmpty())continue;
                const auto actual=result.value(key).toDateTime();
                const auto expected=QDateTime(date,QTime::fromString(e["time"].toString(),"hh:mm"),zone);
                QVERIFY(actual.isValid());
                const int error=std::abs(actual.secsTo(expected));
                maximumError=qMax(maximumError,error);
                QVERIFY2(error<=300,qPrintable(row["name"].toString()+" "+key+" error="+QString::number(error)));
            }
        }
        qInfo()<<"USNO maximum event error (seconds):"<<maximumError;
    }

    void solarPolesClockAndContinuity() {
        const auto utc=QTimeZone::utc();
        for(double latitude:{-90.0,-78.0,78.0,90.0})for(int month:{6,12}) {
            const bool summer=(latitude>0)==(month==6);
            const auto state=landscape::solarState(QDateTime(QDate(2026,month,21),QTime(12,0),utc),latitude,0);
            QCOMPARE(state.value("dayKind").toString(),summer?QStringLiteral("polarDay"):QStringLiteral("polarNight"));
            QVERIFY(!state.contains("sunrise"));QVERIFY(!state.contains("sunset"));
            for(const QString& key:{"elevation","mix","sunX","sunHeight","sunOpacity"})QVERIFY(std::isfinite(state.value(key).toDouble()));
            if(!summer)QCOMPARE(state.value("sunOpacity").toDouble(),0.0);
        }
        const QTimeZone newYork("America/New_York");QVERIFY(newYork.isValid());
        for(const QDate date:{QDate(2026,3,8),QDate(2026,11,1)}) {
            const QDateTime now(date,QTime(12,0),newYork);
            const auto local=landscape::solarState(now,40.71,-74.01);
            const auto universal=landscape::solarState(now.toUTC(),40.71,-74.01);
            QCOMPARE(local.value("elevation"),universal.value("elevation"));
            QCOMPARE(local.value("sunrise").toDateTime().date(),date);
            QCOMPARE(local.value("sunset").toDateTime().date(),date);
        }
        // 跨国际日期线只改变显示日期，不改变同一瞬间的光照。
        const QDateTime instant(QDate(2026,6,21),QTime(22,30),utc);
        QCOMPARE(landscape::solarState(instant,0,179.9).value("elevation"),
                 landscape::solarState(instant.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(14*3600)),0,179.9).value("elevation"));
        for(double latitude:{0.0,39.9,65.0,78.0})for(int month:{6,12}) {
            auto previous=landscape::solarState(QDateTime(QDate(2026,month,21),QTime(0,0),utc),latitude,0);
            for(int minute=2;minute<=1440;minute+=2) {
                const auto next=landscape::solarState(QDateTime(QDate(2026,month,21),QTime(0,0),utc).addSecs(minute*60),latitude,0);
                QVERIFY(std::abs(next.value("elevation").toDouble()-previous.value("elevation").toDouble())<1.1);
                QVERIFY(std::abs(next.value("warm").toDouble()-previous.value("warm").toDouble())<0.10);
                QVERIFY(std::abs(next.value("twilightMix").toDouble()-previous.value("twilightMix").toDouble())<0.04);
                previous=next;
            }
        }
        QVERIFY(!landscape::solarState(instant,std::numeric_limits<double>::quiet_NaN(),0).value("active").toBool());
    }

    void solarLocationLifecycle() {
        PixelLocationStorage storage;
        auto* location=new FakeLandscapeLocation;
        PixelLandscapeViewModel vm(nullptr,location);
        vm.setStorage(&storage);
        vm.applyConfig({});vm.start();
        QCOMPARE(location->requests,0);QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("clock"));
        vm.requestLocation();QCOMPARE(location->requests,1);QVERIFY(location->interactive);
        vm.requestLocation();QCOMPARE(location->requests,1);
        location->permission="allowed";location->finish();
        QVERIFY(vm.solarState().value("active").toBool());QVERIFY(!storage.bytes.isEmpty());
        QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("system"));
        vm.requestLocation();location->finish("timeout");
        QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("cache"));
        QCOMPARE(vm.locationState().value("error").toString(),QStringLiteral("timeout"));
        const QVariantMap manual{{"manualLocationEnabled",true},{"latitude",-33.87},{"longitude",151.21}};
        vm.applyConfig(manual);
        vm.refreshAt(QDateTime::currentDateTime().addDays(2));
        QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("manual"));
        vm.requestLocation();const quint64 previous=location->current;
        QVariantMap manualOnly=manual;manualOnly["locationSource"]="manual";
        vm.applyConfig(manualOnly);
        Q_EMIT location->finished(previous,{},20,30,QDateTime::currentDateTimeUtc());
        QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("manual"));
        location->permission="denied";vm.applyConfig(manual);
        QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("manual"));
        QVERIFY(storage.bytes.isEmpty());
        vm.applyConfig({});QCOMPARE(vm.locationState().value("source").toString(),QStringLiteral("clock"));
        vm.requestLocation();location->permission="allowed";location->finish();
        vm.checkLocation(QDateTime::currentDateTime().addSecs(1801));
        QVERIFY(!location->interactive);QVERIFY(vm.m_pending);
        const auto before=vm.solarState();vm.stop();location->finish();QCOMPARE(vm.solarState(),before);
        vm.requestLocation();QVERIFY(!vm.m_pending);
        // 重新启动复用缓存；拒绝授权时不允许缓存继续伪装当前位置。
        auto* cachedLocation=new FakeLandscapeLocation;cachedLocation->permission="allowed";
        PixelLandscapeViewModel restored(nullptr,cachedLocation);restored.setStorage(&storage);restored.start();
        QCOMPARE(restored.locationState().value("source").toString(),QStringLiteral("cache"));
        cachedLocation->permission="denied";restored.resync();
        QCOMPARE(restored.locationState().value("source").toString(),QStringLiteral("clock"));
        QVERIFY(storage.bytes.isEmpty());
    }

    void solarNativeClockResyncWhilePaused() {
        PixelLandscapeViewModel vm(nullptr,new FakeLandscapeLocation);
        vm.applyConfig({{"locationSource","manual"},{"manualLocationEnabled",true},{"latitude",39.9},{"longitude",116.4},
                        {"animationsEnabled",false}});
        vm.start();vm.setPaused(true);
        const auto old=QDateTime::currentDateTime().addSecs(-6*3600);
        for(const UINT message:{UINT(WM_TIMECHANGE),UINT(WM_POWERBROADCAST)}) {
            vm.refreshAt(old,true);QSignalSpy changes(&vm,&PixelLandscapeViewModel::solarStateChanged);
            MSG msg{};msg.message=message;msg.wParam=PBT_APMRESUMEAUTOMATIC;
            QVERIFY(!vm.nativeEventFilter("windows_generic_MSG",&msg,nullptr));
            vm.nativeEventFilter("windows_generic_MSG",&msg,nullptr);
            QCOMPARE(changes.size(),0);QTRY_COMPARE(changes.size(),1);
            QVERIFY(std::abs(vm.m_lastCheck.secsTo(QDateTime::currentDateTime()))<2);
        }
        MSG msg{};msg.message=WM_TIMECHANGE;
        vm.nativeEventFilter("windows_generic_MSG",&msg,nullptr);vm.stop();
        QSignalSpy changes(&vm,&PixelLandscapeViewModel::solarStateChanged);QTest::qWait(20);QVERIFY(changes.isEmpty());
    }

    void solarConfigAndCacheValidation() {
        PixelLandscapeViewModel vm;
        const QVariantMap manual{{"locationSource","manual"},{"manualLocationEnabled",true},{"latitude",0.0},{"longitude",0.0}};
        QVERIFY(vm.applyConfig(manual));QVERIFY(vm.solarState().value("active").toBool());
        for(const QVariantMap& invalid:QList<QVariantMap>{
                {{"latitude",20}},{{"latitude","20"},{"longitude",30}},{{"latitude",91.0},{"longitude",0.0}},
                {{"latitude",0.0},{"longitude",181.0}},{{"manualLocationEnabled",true}},{{"locationSource","manual"}},
                {{"latitude",std::numeric_limits<double>::infinity()},{"longitude",0.0}},{{"manualLocationEnabled",1}}}) {
            QVERIFY(!vm.applyConfig(invalid));QVERIFY(vm.solarState().value("active").toBool());
        }
        QVERIFY(vm.applyConfig({{"latitude",0.0},{"longitude",0.0}}));QVERIFY(!vm.solarState().value("active").toBool());
        PixelLocationStorage storage;storage.bytes="{broken";
        auto* location=new FakeLandscapeLocation;
        PixelLandscapeViewModel restored(nullptr,location);restored.setStorage(&storage);restored.start();
        QVERIFY(!restored.solarState().value("active").toBool());
        restored.requestLocation();location->finish({},QDateTime::currentDateTimeUtc().addDays(1));
        QCOMPARE(restored.locationState().value("error").toString(),QStringLiteral("invalid_position"));
        restored.requestLocation();storage.writable=false;location->finish();
        QCOMPARE(restored.locationState().value("error").toString(),QStringLiteral("cache_write_failed"));
        QVERIFY(restored.solarState().value("active").toBool());
    }

    void solarRenderingAndStaticBudget() {
        QQuickView view;view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl::fromLocalFile(pluginSource("LandscapeScene.qml")));
        QCOMPARE(view.status(),QQuickView::Ready);
        QSignalSpy warnings(view.engine(),&QQmlEngine::warnings);
        auto* root=view.rootObject();root->setProperty("expanded",true);
        view.resize(320,180);view.show();
        const QTimeZone zone=QTimeZone::fromSecondsAheadOfUtc(8*3600);
        const auto day=QDate(2026,6,21);
        auto noon=landscape::solarState(QDateTime(day,QTime(12,0),zone),39.9,116.4);
        root->setProperty("solarState",noon);QTest::qWait(40);
        const int paints=root->property("staticPaintCount").toInt();
        auto* sun=root->findChild<QQuickItem*>("solarDisc");QVERIFY(sun);
        const double x=sun->x();noon["sunX"]=0.75;root->setProperty("solarState",noon);
        QTest::qWait(40);QVERIFY(sun->x()!=x);
        QCOMPARE(root->property("staticPaintCount").toInt(),paints);
        root->setProperty("solarState",landscape::solarState(QDateTime(day,QTime(13,0),zone),39.9,116.4));
        QTest::qWait(40);QCOMPARE(root->property("staticPaintCount").toInt(),paints);
        const QStringList scenes{"cabin","lighthouse","street","campsite","railway","island"};
        const QList<QTime> moments{QTime(4,10),QTime(4,46),QTime(7,0),QTime(12,16),QTime(19,46),QTime(22,0)};
        QImage gallery(6*320,6*204,QImage::Format_RGB32);gallery.fill(QColor("#182330"));QPainter painter(&gallery);
        for(int row=0;row<scenes.size();++row)for(int col=0;col<moments.size();++col) {
            root->setProperty("sceneType",scenes[row]);
            root->setProperty("solarState",landscape::solarState(QDateTime(day,moments[col],zone),39.9,116.4));
            QTest::qWait(25);const auto frame=view.grabWindow();QVERIFY(!frame.isNull());
            painter.drawImage(QRect(col*320,row*204+24,320,180),frame);
            painter.setPen(Qt::white);painter.drawText(col*320+8,row*204+17,scenes[row]+" "+moments[col].toString("hh:mm"));
        }
        painter.end();
        const QString output=qEnvironmentVariable("XUANHUB_PIXEL_EVIDENCE");
        if(!output.isEmpty()){QDir().mkpath(output);QVERIFY(gallery.save(output+"/solar-day-gallery.png"));}
        // 关闭装饰动画后，现实光照仍驱动两处画面，且不推进动画时钟。
        PixelLandscapeViewModel vm;vm.applyConfig({{"manualLocationEnabled",true},{"latitude",39.9},{"longitude",116.4},
            {"animationsEnabled",false},{"goldenHourMode","always"}});
        PopupService popups;PixelContext context;context.popups=&popups;
        QQuickView plugin;plugin.setSource(QUrl::fromLocalFile(pluginSource("PixelLandscape.qml")));
        QCOMPARE(plugin.status(),QQuickView::Ready);
        QSignalSpy pluginWarnings(plugin.engine(),&QQmlEngine::warnings);
        auto* body=plugin.rootObject();body->setProperty("pluginContext",QVariant::fromValue<QObject*>(&context));
        body->setProperty("plugin",QVariant::fromValue<QObject*>(&vm));plugin.show();
        auto* popup=body->findChild<QQuickWindow*>("landscapePopup");popup->setProperty("closeOnDeactivate",false);
        QVERIFY(QMetaObject::invokeMethod(body,"openLandscape"));QTRY_VERIFY(popup->isVisible());
        const auto night=QDateTime(day,QTime(22,0),zone);vm.refreshAt(night,true);QTest::qWait(40);
        const auto* bar=body->findChild<QObject*>("barScene");const auto* expanded=body->findChild<QObject*>("expandedScene");
        QVERIFY(expanded);QCOMPARE(bar->property("lightKey"),expanded->property("lightKey"));
        const auto previousKey=bar->property("lightKey");
        vm.refreshAt(QDateTime(day,QTime(12,0),zone),true);QTest::qWait(40);
        QVERIFY(previousKey!=bar->property("lightKey"));QCOMPARE(bar->property("lightKey"),expanded->property("lightKey"));
        vm.refreshAt(QDateTime(day,QTime(5,0),zone),true);QTest::qWait(40);
        QVERIFY(vm.goldenHourState().value("strength").toDouble()>0);
        QCOMPARE(bar->property("goldenHourState"),expanded->property("goldenHourState"));
        QCOMPARE(bar->property("goldenHourState").toMap(),vm.goldenHourState());
        QCOMPARE(body->findChild<QObject*>("landscapeController")->property("animationTime").toDouble(),0.0);
        if(!output.isEmpty())QVERIFY(popup->grabWindow().save(output+"/solar-popup.png"));
        QVERIFY(warnings.isEmpty());QVERIFY(pluginWarnings.isEmpty());
    }

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
    const bool solarProbe=qEnvironmentVariableIsSet("XUANHUB_PIXEL_SOLAR_PROBE");
    if(solarProbe)vm.applyConfig({{"locationSource","manual"},{"manualLocationEnabled",true},
        {"latitude",39.9},{"longitude",116.4},{"scene",vm.scene()}});
    QTimer solarClock;
    int solarTick=0;
    if(solarProbe) {
        solarClock.setInterval(1000);
        QObject::connect(&solarClock,&QTimer::timeout,&app,[&]{
            vm.refreshAt(QDateTime(QDate(2026,6,21),QTime(4,15),QTimeZone::fromSecondsAheadOfUtc(8*3600)).addSecs(++solarTick*120),true);
        });
        solarClock.start();
    }
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
                {"solarProbe",solarProbe},{"solarUpdates",solarTick},
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
        app.setFont(QFont(QStringLiteral("Segoe UI"),9));
    }
    qputenv("QT_QUICK_CONTROLS_STYLE","Basic");
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/PluginPopup.qml")),"XuanHub",1,0,"PluginPopup");
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/UiMetrics.qml")),"XuanHub",1,0,"UiMetrics");
    for(const QString& name:{"UiCard","UiComboBox","UiTextField"})
        qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/")+name+".qml"),"XuanHub",1,0,name.toUtf8().constData());
    if(app.arguments().contains(QStringLiteral("--probe")))return runProbe(app,app.arguments());
    PixelLandscapeTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "PixelLandscapeTest.moc"
