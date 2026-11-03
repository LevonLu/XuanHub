// ThemeTest.cpp — Theme Schema v2 合并、钳制与可信 Bar 外壳路径回归测试
#include "ThemeManager.h"
#include "ThemeRegistry.h"
#include "barplugin/EventBus.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString& path, const QByteArray& content) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(content) == content.size();
}

QJsonObject palette(const QString& bg, const QString& fg, const QString& accent) {
    return {
        {QStringLiteral("bg"), bg},
        {QStringLiteral("fg"), fg},
        {QStringLiteral("accent"), accent},
    };
}
} // namespace

class ThemeTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void repositoryThemePackagesAreValid() {
        ThemeRegistry registry;
        registry.reload(QDir(QStringLiteral(XUANHUB_SOURCE_DIR))
                            .filePath(QStringLiteral("themes")));
        QCOMPARE(registry.packs().size(), 3);
        const QStringList ids{QStringLiteral("xuanhub-default"),
                              QStringLiteral("fluent-teal"), QStringLiteral("fluent-violet")};
        for (const QString& id : ids) {
            const auto* pack = registry.find(id);
            QVERIFY2(pack != nullptr, qPrintable(id));
            QCOMPARE(pack->dark.size(), 11);
            QCOMPARE(pack->light.size(), 11);
            QVERIFY(pack->chromeUrl.isEmpty());
        }

        // 磁盘色板与缺包时的内置兜底保持一致，避免默认主题在两种入口下变色。
        const auto* defaults = registry.find(QStringLiteral("xuanhub-default"));
        QCOMPARE(defaults->dark, registry.defaultPack().dark);
        QCOMPARE(defaults->light, registry.defaultPack().light);
    }

    void loadsAppearanceAndTrustedChrome() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString themeDir = temporary.filePath(QStringLiteral("glass"));
        QVERIFY(writeFile(QDir(themeDir).filePath(QStringLiteral("Chrome.qml")),
                          QByteArrayLiteral("import QtQuick\nItem {}\n")));

        const QJsonObject root{
            {QStringLiteral("id"), QStringLiteral("glass")},
            {QStringLiteral("name"), QStringLiteral("Glass")},
            {QStringLiteral("dark"), palette(QStringLiteral("#101010"),
                                               QStringLiteral("#f0f0f0"),
                                               QStringLiteral("#8080ff"))},
            {QStringLiteral("light"), palette(QStringLiteral("#f0f0f0"),
                                                QStringLiteral("#101010"),
                                                QStringLiteral("#4040aa"))},
            {QStringLiteral("appearance"), QJsonObject{
                {QStringLiteral("bar"), QJsonObject{
                    {QStringLiteral("radius"), 200},
                    {QStringLiteral("opacity"), 0.75},
                }},
                {QStringLiteral("items"), QJsonObject{
                    {QStringLiteral("decoration"), QStringLiteral("pill")},
                    {QStringLiteral("paddingX"), 8},
                }},
            }},
            {QStringLiteral("darkAppearance"), QJsonObject{
                {QStringLiteral("bar"), QJsonObject{
                    {QStringLiteral("background"), QStringLiteral("panel")},
                }},
            }},
            {QStringLiteral("extension"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("barChrome")},
                {QStringLiteral("apiVersion"), 1},
                {QStringLiteral("qml"), QStringLiteral("Chrome.qml")},
                {QStringLiteral("supports"), QJsonArray{QStringLiteral("appbar")}},
            }},
        };
        QVERIFY(writeFile(QDir(themeDir).filePath(QStringLiteral("theme.json")),
                          QJsonDocument(root).toJson(QJsonDocument::Compact)));

        ThemeRegistry registry;
        registry.reload(temporary.path());
        const ThemeRegistry::ThemePack* pack = registry.find(QStringLiteral("glass"));
        QVERIFY(pack != nullptr);
        QCOMPARE(pack->appearance.value(QStringLiteral("bar")).toMap()
                     .value(QStringLiteral("radius")).toDouble(), 64.0);
        QCOMPARE(pack->chromeModes, QStringList{QStringLiteral("appbar")});
        QVERIFY(pack->chromeUrl.isLocalFile());

        xuanhub::EventBus bus;
        ThemeManager manager(&bus, &registry, QStringLiteral("glass"), QStringLiteral("dark"));
        QCOMPARE(manager.appearance().value(QStringLiteral("bar")).toMap()
                     .value(QStringLiteral("background")).toString(), QStringLiteral("panel"));
        QCOMPARE(manager.appearance().value(QStringLiteral("items")).toMap()
                     .value(QStringLiteral("paddingX")).toDouble(), 8.0);
        QCOMPARE(manager.appearance().value(QStringLiteral("regions")).toMap()
                     .value(QStringLiteral("decoration")).toString(), QStringLiteral("none"));
        QVERIFY(manager.chromeSupportsMode(QStringLiteral("appbar")));
        QVERIFY(!manager.chromeSupportsMode(QStringLiteral("taskbar")));
    }

    void rejectsEscapingChromePath() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(writeFile(temporary.filePath(QStringLiteral("Outside.qml")),
                          QByteArrayLiteral("import QtQuick\nItem {}\n")));
        const QString themeDir = temporary.filePath(QStringLiteral("unsafe"));
        const QJsonObject root{
            {QStringLiteral("id"), QStringLiteral("unsafe")},
            {QStringLiteral("dark"), palette(QStringLiteral("#101010"),
                                               QStringLiteral("#f0f0f0"),
                                               QStringLiteral("#8080ff"))},
            {QStringLiteral("light"), palette(QStringLiteral("#f0f0f0"),
                                                QStringLiteral("#101010"),
                                                QStringLiteral("#4040aa"))},
            {QStringLiteral("extension"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("barChrome")},
                {QStringLiteral("apiVersion"), 1},
                {QStringLiteral("qml"), QStringLiteral("../Outside.qml")},
            }},
        };
        QVERIFY(writeFile(QDir(themeDir).filePath(QStringLiteral("theme.json")),
                          QJsonDocument(root).toJson(QJsonDocument::Compact)));

        ThemeRegistry registry;
        registry.reload(temporary.path());
        const ThemeRegistry::ThemePack* pack = registry.find(QStringLiteral("unsafe"));
        QVERIFY(pack != nullptr);
        QVERIFY(pack->chromeUrl.isEmpty());
    }
};

QTEST_GUILESS_MAIN(ThemeTest)
#include "ThemeTest.moc"
