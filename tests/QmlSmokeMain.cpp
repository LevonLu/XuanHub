// QmlSmokeMain.cpp — 在最小宿主上下文中加载插件 QML 并验证根契约与尺寸
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QUrl>
#include <QtMath>

class SmokeObject final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap colors READ colors CONSTANT)
    Q_PROPERTY(QVariantMap screenGeometry READ screenGeometry CONSTANT)
    Q_PROPERTY(QVariantMap values READ values CONSTANT)
    Q_PROPERTY(int contextVersion READ contextVersion CONSTANT)
    Q_PROPERTY(QObject* popupService READ emptyService CONSTANT)
    Q_PROPERTY(QObject* actions READ emptyService CONSTANT)
    Q_PROPERTY(QString activeLocale READ activeLocale CONSTANT)
    Q_PROPERTY(QString barEdge READ barEdge CONSTANT)
public:
    explicit SmokeObject(QObject* parent = nullptr) : QObject(parent) {
        // 常见插件只读值使用动态属性补齐；未知属性仍会作为非致命 QML warning 暴露。
        const QVariantMap defaults{
            {QStringLiteral("text"), QStringLiteral("Smoke")},
            {QStringLiteral("timeText"), QStringLiteral("12:34:56")},
            {QStringLiteral("cpuPercent"), 25},
            {QStringLiteral("memoryPercent"), 50},
            {QStringLiteral("networkDown"), QStringLiteral("1 KB/s")},
            {QStringLiteral("networkUp"), QStringLiteral("1 KB/s")},
            {QStringLiteral("state"), QVariantMap{}},
        };
        for (auto it = defaults.begin(); it != defaults.end(); ++it)
            setProperty(it.key().toUtf8().constData(), it.value());
    }

    QVariantMap colors() const {
        return {
            {QStringLiteral("bg"), QStringLiteral("#1e1e2e")},
            {QStringLiteral("panel"), QStringLiteral("#313244")},
            {QStringLiteral("fg"), QStringLiteral("#cdd6f4")},
            {QStringLiteral("muted"), QStringLiteral("#7f849c")},
            {QStringLiteral("accent"), QStringLiteral("#89b4fa")},
            {QStringLiteral("danger"), QStringLiteral("#f38ba8")},
            {QStringLiteral("success"), QStringLiteral("#a6e3a1")},
            {QStringLiteral("warning"), QStringLiteral("#f9e2af")},
            {QStringLiteral("hover"), QStringLiteral("#45475a")},
            {QStringLiteral("border"), QStringLiteral("#45475a")},
            {QStringLiteral("onAccent"), QStringLiteral("#1e1e2e")},
        };
    }
    QVariantMap screenGeometry() const {
        return {{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                {QStringLiteral("width"), 1920}, {QStringLiteral("height"), 1080}};
    }
    QVariantMap values() const {
        return {{QStringLiteral("text"), QStringLiteral("Smoke")}};
    }
    int contextVersion() const { return 1; }
    QObject* emptyService() const { return nullptr; }
    QString activeLocale() const { return QStringLiteral("en-US"); }
    QString barEdge() const { return QStringLiteral("top"); }

    Q_INVOKABLE bool call(const QString&, const QVariantList& = {}) { return true; }
    Q_INVOKABLE QVariant value(const QString&) const { return {}; }
    Q_INVOKABLE void setValue(const QString&, const QVariant&) {}

Q_SIGNALS:
    void barPressed();
    void valuesChanged();
};

namespace {
QJsonObject failure(const QString& code, const QString& message,
                    const QString& qmlPath) {
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("command"), QStringLiteral("qml-smoke")},
            {QStringLiteral("ok"), false},
            {QStringLiteral("qml"), qmlPath},
            {QStringLiteral("diagnostics"), QJsonArray{QJsonObject{
                {QStringLiteral("severity"), QStringLiteral("error")},
                {QStringLiteral("code"), code},
                {QStringLiteral("message"), message}}}}};
}

int finish(const QJsonObject& result) {
    const QByteArray output = QJsonDocument(result).toJson(QJsonDocument::Indented);
    fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    fflush(stdout);
    return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}
} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));
    QGuiApplication application(argc, argv);
    if (argc < 3)
        return finish(failure(QStringLiteral("qml.arguments"),
                              QStringLiteral("usage: qml-smoke <qml> <PluginPopup.qml>"), {}));

    const QString qmlPath = QString::fromLocal8Bit(argv[1]);
    const QString popupPath = QString::fromLocal8Bit(argv[2]);
    if (qmlRegisterType(QUrl::fromLocalFile(popupPath), "XuanHub", 1, 0,
                        "PluginPopup") < 0) {
        return finish(failure(QStringLiteral("qml.popup_registration"),
                              QStringLiteral("cannot register PluginPopup"), qmlPath));
    }

    QQmlEngine engine;
    SmokeObject plugin;
    SmokeObject context;
    SmokeObject globals;
    engine.rootContext()->setContextProperty(QStringLiteral("themeManager"), &globals);
    engine.rootContext()->setContextProperty(QStringLiteral("barWindow"), &globals);
    engine.rootContext()->setContextProperty(QStringLiteral("popupService"), &globals);
    engine.rootContext()->setContextProperty(QStringLiteral("barConfig"), &globals);
    engine.rootContext()->setContextProperty(
        QStringLiteral("barScreenGeometry"), context.screenGeometry());

    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlPath));
    if (component.status() == QQmlComponent::Loading) {
        QElapsedTimer timer;
        timer.start();
        while (component.status() == QQmlComponent::Loading
               && timer.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(1);
        }
        if (component.status() == QQmlComponent::Loading) {
            return finish(failure(QStringLiteral("qml.load_timeout"),
                                  QStringLiteral("QML component did not finish loading in 5 seconds"),
                                  qmlPath));
        }
    }
    if (component.isError()) {
        QStringList errors;
        for (const QQmlError& error : component.errors()) errors.append(error.toString());
        return finish(failure(QStringLiteral("qml.load_failed"), errors.join(QLatin1Char('\n')),
                              qmlPath));
    }

    QVariantMap initial{{QStringLiteral("plugin"), QVariant::fromValue(&plugin)},
                        {QStringLiteral("pluginContext"), QVariant::fromValue(&context)}};
    std::unique_ptr<QObject> root(component.createWithInitialProperties(initial));
    if (!root) {
        QStringList errors;
        for (const QQmlError& error : component.errors()) errors.append(error.toString());
        return finish(failure(QStringLiteral("qml.create_failed"), errors.join(QLatin1Char('\n')),
                              qmlPath));
    }
    if (root->metaObject()->indexOfProperty("plugin") < 0) {
        return finish(failure(QStringLiteral("qml.plugin_property_missing"),
                              QStringLiteral("root object has no plugin property"), qmlPath));
    }
    auto* item = qobject_cast<QQuickItem*>(root.get());
    if (!item) {
        return finish(failure(QStringLiteral("qml.root_not_item"),
                              QStringLiteral("root object is not a QQuickItem"), qmlPath));
    }
    QCoreApplication::processEvents();
    if (!qIsFinite(item->width()) || !qIsFinite(item->height())
        || item->width() <= 0 || item->height() <= 0) {
        return finish(failure(QStringLiteral("qml.invalid_size"),
                              QStringLiteral("root width and height must be positive finite values"),
                              qmlPath));
    }
    QString screenshotPath;
    if (argc >= 4) {
        screenshotPath = QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath();
        QQuickWindow window;
        window.setColor(Qt::transparent);
        window.resize(qCeil(item->width()), qCeil(item->height()));
        item->setParentItem(window.contentItem());
        window.show();
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }
        const QImage image = window.grabWindow();
        if (image.isNull() || !image.save(screenshotPath, "PNG")) {
            return finish(failure(QStringLiteral("qml.screenshot_failed"),
                                  QStringLiteral("cannot render requested PNG screenshot"),
                                  qmlPath));
        }
    }
    QJsonObject success{{QStringLiteral("schemaVersion"), 1},
                        {QStringLiteral("command"), QStringLiteral("qml-smoke")},
                        {QStringLiteral("ok"), true},
                        {QStringLiteral("qml"), qmlPath},
                        {QStringLiteral("width"), item->width()},
                        {QStringLiteral("height"), item->height()},
                        {QStringLiteral("diagnostics"), QJsonArray{}}};
    if (!screenshotPath.isEmpty())
        success.insert(QStringLiteral("screenshot"), screenshotPath);
    return finish(success);
}

#include "QmlSmokeMain.moc"
