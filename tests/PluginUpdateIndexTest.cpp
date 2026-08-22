// PluginUpdateIndexTest.cpp — 静态更新索引与版本选择安全规则回归测试
#include "PluginUpdateIndex.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {
QByteArray indexJson(const QJsonArray& packages) {
    return QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("example")},
        {QStringLiteral("packages"), packages},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject package(const QString& id, const QString& version,
                    const QString& url = QStringLiteral("packages/demo.xuanplugin")) {
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("version"), version},
        {QStringLiteral("runtime"), QStringLiteral("python")},
        {QStringLiteral("url"), url},
        {QStringLiteral("sha256"), QStringLiteral("sha256:") + QString(64, QLatin1Char('a'))},
        {QStringLiteral("size"), 1024},
        {QStringLiteral("signerKeyId"),
         QStringLiteral("sha256:") + QString(64, QLatin1Char('b'))},
    };
}
} // namespace

class PluginUpdateIndexTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parsesHttpsIndexAndResolvesRelativePackageUrl() {
        const QUrl source(QStringLiteral("https://example.test/releases/index.json"));
        const auto result = PluginUpdateIndex::parse(
            indexJson({package(QStringLiteral("py_demo"), QStringLiteral("1.2.0"))}),
            source);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.sourceId, QStringLiteral("example"));
        QCOMPARE(result.packages.size(), 1);
        QCOMPARE(result.packages.first().packageUrl,
                 QUrl(QStringLiteral("https://example.test/releases/packages/demo.xuanplugin")));
    }

    void rejectsInsecureUrlsMalformedMetadataAndDuplicates() {
        const QByteArray valid = indexJson({package(QStringLiteral("py_demo"),
                                                    QStringLiteral("1.2.0"))});
        QVERIFY(!PluginUpdateIndex::parse(
            valid, QUrl(QStringLiteral("http://example.test/index.json"))).ok);

        auto badPackage = package(QStringLiteral("py_demo"), QStringLiteral("1.2.0"));
        badPackage[QStringLiteral("sha256")] = QStringLiteral("sha256:bad");
        QVERIFY(!PluginUpdateIndex::parse(
            indexJson({badPackage}), QUrl(QStringLiteral("https://example.test/index.json"))).ok);

        const QJsonObject duplicate = package(QStringLiteral("py_demo"),
                                              QStringLiteral("1.2.0"));
        QVERIFY(!PluginUpdateIndex::parse(
            indexJson({duplicate, duplicate}),
            QUrl(QStringLiteral("https://example.test/index.json"))).ok);

        QVERIFY(!PluginUpdateIndex::parse(
            indexJson({package(QStringLiteral("py_demo"), QStringLiteral("1.2.0"),
                               QStringLiteral("http://example.test/demo.xuanplugin"))}),
            QUrl(QStringLiteral("https://example.test/index.json"))).ok);
    }

    void selectsOnlyNewestVersionForInstalledPlugins() {
        const auto parsed = PluginUpdateIndex::parse(indexJson({
            package(QStringLiteral("a"), QStringLiteral("1.1.0")),
            package(QStringLiteral("a"), QStringLiteral("2.0.0")),
            package(QStringLiteral("b"), QStringLiteral("1.0.0")),
            package(QStringLiteral("c"), QStringLiteral("1.0.0")),
        }), QUrl(QStringLiteral("file:///C:/updates/index.json")));
        QVERIFY2(parsed.ok, qPrintable(parsed.error));
        const auto updates = PluginUpdateIndex::newerCandidates(
            parsed.packages,
            {{QStringLiteral("a"), QStringLiteral("1.0.0")},
             {QStringLiteral("b"), QStringLiteral("1.0.0")}});
        QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.first().id, QStringLiteral("a"));
        QCOMPARE(updates.first().version, QStringLiteral("2.0.0"));
    }
};

QTEST_GUILESS_MAIN(PluginUpdateIndexTest)
#include "PluginUpdateIndexTest.moc"
