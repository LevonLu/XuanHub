// PluginUpdateServiceTest.cpp — 更新检查/下载分离与索引固定规则回归测试
#include "PluginUpdateService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size();
}

QByteArray index(const QString& signerKeyId = {}) {
    QJsonObject package{
        {QStringLiteral("id"), QStringLiteral("py_demo")},
        {QStringLiteral("version"), QStringLiteral("2.0.0")},
        {QStringLiteral("runtime"), QStringLiteral("python")},
        {QStringLiteral("url"), QStringLiteral("demo.xuanplugin")},
        {QStringLiteral("sha256"),
         QStringLiteral("sha256:") + QString(64, QLatin1Char('0'))},
        {QStringLiteral("size"), 3},
    };
    if (!signerKeyId.isEmpty())
        package.insert(QStringLiteral("signerKeyId"), signerKeyId);
    return QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("local-test")},
        {QStringLiteral("packages"), QJsonArray{package}},
    }).toJson(QJsonDocument::Compact);
}

QVariantMap installed(const QString& signerKeyId = {}) {
    QVariantMap plugin{
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("runtime"), QStringLiteral("python")},
    };
    if (!signerKeyId.isEmpty())
        plugin.insert(QStringLiteral("signerKeyId"), signerKeyId);
    return {{QStringLiteral("py_demo"), plugin}};
}
} // namespace

class PluginUpdateServiceTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void checkDoesNotDownloadPackageAndExplicitDownloadRejectsHashMismatch() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString indexPath = directory.filePath(QStringLiteral("index.json"));
        const QString packagePath = directory.filePath(QStringLiteral("demo.xuanplugin"));
        QVERIFY(writeFile(indexPath, index()));
        QVERIFY(writeFile(packagePath, QByteArray("bad")));

        PluginUpdateService service(directory.filePath(QStringLiteral("config")));
        QSignalSpy checkSpy(&service, &PluginUpdateService::checkFinished);
        service.check({QUrl::fromLocalFile(indexPath).toString()}, installed());
        QTRY_COMPARE_WITH_TIMEOUT(checkSpy.count(), 1, 5000);
        const QVariantList updates = checkSpy.takeFirst().at(0).toList();
        QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.first().toMap().value(QStringLiteral("version")).toString(),
                 QStringLiteral("2.0.0"));
        QVERIFY(!QFileInfo(directory.filePath(
            QStringLiteral("config/plugin-update-cache"))).exists());

        QSignalSpy downloadSpy(&service, &PluginUpdateService::downloadFinished);
        service.download(QStringLiteral("py_demo"), QStringLiteral("2.0.0"));
        QTRY_COMPARE_WITH_TIMEOUT(downloadSpy.count(), 1, 5000);
        const QList<QVariant> result = downloadSpy.takeFirst();
        QVERIFY(result.at(0).toString().isEmpty());
        QVERIFY(result.at(2).toString().contains(QStringLiteral("SHA-256")));
    }

    void pinnedSignerMismatchIsNotOfferedAsAnUpdate() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString currentKey = QStringLiteral("sha256:")
            + QString(64, QLatin1Char('a'));
        const QString otherKey = QStringLiteral("sha256:")
            + QString(64, QLatin1Char('b'));
        const QString indexPath = directory.filePath(QStringLiteral("index.json"));
        QVERIFY(writeFile(indexPath, index(otherKey)));

        PluginUpdateService service(directory.filePath(QStringLiteral("config")));
        QSignalSpy spy(&service, &PluginUpdateService::checkFinished);
        service.check({QUrl::fromLocalFile(indexPath).toString()}, installed(currentKey));
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
        const QList<QVariant> result = spy.takeFirst();
        QVERIFY(result.at(0).toList().isEmpty());
        QVERIFY(result.at(1).toStringList().join(QLatin1Char('\n'))
                    .contains(QStringLiteral("signer key differs")));
    }
};

QTEST_GUILESS_MAIN(PluginUpdateServiceTest)
#include "PluginUpdateServiceTest.moc"
