// PluginPackageValidatorTest.cpp — .xuanplugin 离线安全边界与预检回归测试
#include "PluginPackageValidator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
#include <QtTest>

namespace {
PluginValidationEnvironment testEnvironment() {
    return {QStringLiteral("0.3.0"), QStringLiteral("x64"),
            QStringLiteral("6.9.3"), QStringLiteral("msvc2022"),
            QStringLiteral("dynamic")};
}

QJsonObject nativeManifest() {
    return {
        {QStringLiteral("id"), QStringLiteral("com.example.demo")},
        {QStringLiteral("name"), QStringLiteral("Demo")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("apiVersion"), 2},
        {QStringLiteral("minHostVersion"), QStringLiteral("0.3.0")},
        {QStringLiteral("maxHostVersion"), QStringLiteral("0.x")},
        {QStringLiteral("author"), QStringLiteral("Example")},
        {QStringLiteral("entry"), QJsonObject{
            {QStringLiteral("backend"), QStringLiteral("demo.dll")},
            {QStringLiteral("qml"), QStringLiteral("Demo.qml")}}},
        {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("barItem")}},
        {QStringLiteral("permissions"), QJsonArray{QStringLiteral("native.anything")}},
        {QStringLiteral("compatibility"), QJsonObject{
            {QStringLiteral("architecture"), QStringLiteral("x64")},
            {QStringLiteral("qtVersion"), QStringLiteral("6.9.3")},
            {QStringLiteral("compiler"), QStringLiteral("msvc2022")},
            {QStringLiteral("crt"), QStringLiteral("dynamic")}}}
    };
}

QJsonObject packageManifest(const QString& runtime = QStringLiteral("native")) {
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), runtime == QLatin1String("native")
            ? QStringLiteral("com.example.demo") : QStringLiteral("py_demo")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("runtime"), runtime},
        {QStringLiteral("manifest"), runtime == QLatin1String("native")
            ? QStringLiteral("payload/metadata.json")
            : QStringLiteral("payload/plugin.json")},
        {QStringLiteral("publisher"), QStringLiteral("Example Publisher")},
    };
}

QMap<QString, QByteArray> nativeFiles() {
    return {
        {QStringLiteral("plugin.json"),
         QJsonDocument(packageManifest()).toJson(QJsonDocument::Compact)},
        {QStringLiteral("payload/Demo.qml"), QByteArray("import QtQuick\nItem {}\n")},
        {QStringLiteral("payload/demo.dll"), QByteArray("stub-dll")},
        {QStringLiteral("payload/metadata.json"),
         QJsonDocument(nativeManifest()).toJson(QJsonDocument::Compact)},
    };
}

QByteArray hashManifest(const QMap<QString, QByteArray>& files,
                        const QString& corruptPath = {}) {
    QByteArray result;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        QByteArray digest = QCryptographicHash::hash(
            it.value(), QCryptographicHash::Sha256).toHex();
        if (it.key() == corruptPath) digest.fill('0');
        result += digest + "  " + it.key().toUtf8() + '\n';
    }
    return result;
}

QString writePackage(const QString& directory,
                     QMap<QString, QByteArray> files,
                     QZipWriter::CompressionPolicy compression =
                         QZipWriter::AutoCompress,
                     const QString& corruptPath = {},
                     const QStringList& duplicatePaths = {},
                     const QMap<QString, QString>& links = {}) {
    const QString path = QDir(directory).filePath(QStringLiteral("demo.xuanplugin"));
    QZipWriter writer(path);
    writer.setCompressionPolicy(compression);
    const QByteArray hashes = hashManifest(files, corruptPath);
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        writer.addFile(it.key(), it.value());
    writer.addFile(QStringLiteral("manifest.sha256"), hashes);
    for (const QString& duplicate : duplicatePaths)
        writer.addFile(duplicate, files.value(duplicate, QByteArray("duplicate")));
    for (auto it = links.cbegin(); it != links.cend(); ++it)
        writer.addSymLink(it.key(), it.value());
    writer.close();
    return path;
}
} // namespace

class PluginPackageValidatorTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void validatesNativePackageAndKeepsTemporaryPayloadAlive() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto result = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), nativeFiles()), testEnvironment());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.runtime, QStringLiteral("native"));
        QCOMPARE(result.descriptor.id, QStringLiteral("com.example.demo"));
        QCOMPARE(result.publisher, QStringLiteral("Example Publisher"));
        QVERIFY(result.packageHash.startsWith(QStringLiteral("sha256:")));
        QCOMPARE(result.packageHash.size(), 71);
        QCOMPARE(result.declaredPermissions,
                 QStringList{QStringLiteral("native.anything")});
        QCOMPARE(result.grantedPermissions, result.declaredPermissions);
        QVERIFY(result.unsupportedPermissions.isEmpty());
        QVERIFY(QFileInfo(QDir(result.payloadDirectory)
            .filePath(QStringLiteral("demo.dll"))).isFile());
    }

    void validatesPythonCompatibilityAndPermissionPreview() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QJsonObject python{
            {QStringLiteral("id"), QStringLiteral("py_demo")},
            {QStringLiteral("name"), QStringLiteral("Python Demo")},
            {QStringLiteral("version"), QStringLiteral("1.2.0")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("minHostVersion"), QStringLiteral("0.3.0")},
            {QStringLiteral("maxHostVersion"), QStringLiteral("0.x")},
            {QStringLiteral("qml"), QStringLiteral("Demo.qml")},
            {QStringLiteral("permissions"), QJsonArray{
                QStringLiteral("notifications"), QStringLiteral("process.spawn")}},
        };
        QMap<QString, QByteArray> files{
            {QStringLiteral("plugin.json"),
             QJsonDocument(packageManifest(QStringLiteral("python")))
                 .toJson(QJsonDocument::Compact)},
            {QStringLiteral("payload/Demo.qml"), QByteArray("import QtQuick\nItem {}\n")},
            {QStringLiteral("payload/main.py"), QByteArray("def update(): return {}\n")},
            {QStringLiteral("payload/plugin.json"),
             QJsonDocument(python).toJson(QJsonDocument::Compact)},
        };
        const auto result = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), files), testEnvironment());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.grantedPermissions,
                 QStringList{QStringLiteral("notifications")});
        QCOMPARE(result.unsupportedPermissions,
                 QStringList{QStringLiteral("process.spawn")});

        python[QStringLiteral("minHostVersion")] = QStringLiteral("0.4.0");
        files[QStringLiteral("payload/plugin.json")] =
            QJsonDocument(python).toJson(QJsonDocument::Compact);
        const auto incompatible = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), files), testEnvironment());
        QVERIFY(!incompatible.ok);
        QVERIFY(incompatible.error.contains(QStringLiteral("below minimum")));
    }

    void rejectsTraversalDevicesAndUnexpectedRoots() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (const QString& path : {QStringLiteral("payload/../escape.txt"),
                                    QStringLiteral("payload/CON.txt"),
                                    QStringLiteral("unexpected.txt")}) {
            auto files = nativeFiles();
            files.insert(path, QByteArray("bad"));
            const auto result = PluginPackageValidator::validateAndExtract(
                writePackage(directory.path(), files), testEnvironment());
            QVERIFY2(!result.ok, qPrintable(path));
            QVERIFY2(result.error.contains(QStringLiteral("path"))
                         || result.error.contains(QStringLiteral("top-level")),
                     qPrintable(result.error));
        }
    }

    void rejectsDuplicatePathsAndSymbolicLinks() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto duplicate = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), nativeFiles(), QZipWriter::AutoCompress,
                         {}, {QStringLiteral("payload/demo.dll")}),
            testEnvironment());
        QVERIFY(!duplicate.ok);
        QVERIFY(duplicate.error.contains(QStringLiteral("duplicate")));

        auto link = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), nativeFiles(), QZipWriter::AutoCompress,
                         {}, {}, {{QStringLiteral("payload/link"),
                                   QStringLiteral("demo.dll")}}),
            testEnvironment());
        QVERIFY(!link.ok);
        QVERIFY(link.error.contains(QStringLiteral("Symbolic"), Qt::CaseInsensitive));
    }

    void rejectsHashMismatchAndUnlistedFiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto mismatch = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), nativeFiles(), QZipWriter::AutoCompress,
                         QStringLiteral("payload/demo.dll")),
            testEnvironment());
        QVERIFY(!mismatch.ok);
        QVERIFY(mismatch.error.contains(QStringLiteral("SHA-256")));

        auto files = nativeFiles();
        const QString path = QDir(directory.path()).filePath(QStringLiteral("unlisted.xuanplugin"));
        QZipWriter writer(path);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writer.addFile(it.key(), it.value());
        writer.addFile(QStringLiteral("payload/unlisted.txt"), QByteArray("surprise"));
        writer.addFile(QStringLiteral("manifest.sha256"), hashManifest(files));
        writer.close();
        const auto unlisted = PluginPackageValidator::validateAndExtract(
            path, testEnvironment());
        QVERIFY(!unlisted.ok);
        QVERIFY(unlisted.error.contains(QStringLiteral("file set mismatch")));
    }

    void rejectsCompressionBomb() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto files = nativeFiles();
        files.insert(QStringLiteral("payload/bomb.bin"), QByteArray(1024 * 1024, '\0'));
        const auto result = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), files, QZipWriter::AlwaysCompress),
            testEnvironment());
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("compression ratio")));
    }
};

QTEST_GUILESS_MAIN(PluginPackageValidatorTest)
#include "PluginPackageValidatorTest.moc"
