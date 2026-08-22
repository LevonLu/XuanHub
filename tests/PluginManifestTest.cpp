// PluginManifestTest.cpp — 插件代码加载前的 Manifest 安全校验回归测试
#include "PluginManifest.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString& path, const QByteArray& content = QByteArray("stub")) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

PluginValidationEnvironment testEnvironment() {
    return {QStringLiteral("0.3.0"), QStringLiteral("x64"),
            QStringLiteral("6.9.3"), QStringLiteral("msvc2022"),
            QStringLiteral("dynamic")};
}

QJsonObject validV2Manifest() {
    return {
        {QStringLiteral("id"), QStringLiteral("com.example.demo")},
        {QStringLiteral("name"), QStringLiteral("Demo")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("apiVersion"), 2},
        {QStringLiteral("minHostVersion"), QStringLiteral("0.3.0")},
        {QStringLiteral("maxHostVersion"), QStringLiteral("0.x")},
        {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("barItem")}},
        {QStringLiteral("entry"), QJsonObject{
            {QStringLiteral("backend"), QStringLiteral("demo.dll")},
            {QStringLiteral("qml"), QStringLiteral("Demo.qml")}}},
        {QStringLiteral("compatibility"), QJsonObject{
            {QStringLiteral("architecture"), QStringLiteral("x64")},
            {QStringLiteral("qtVersion"), QStringLiteral("6.9.3")},
            {QStringLiteral("compiler"), QStringLiteral("msvc2022")},
            {QStringLiteral("crt"), QStringLiteral("dynamic")}}}
    };
}

PluginManifestResult loadManifest(const QJsonObject& object) {
    static QTemporaryDir directory;
    Q_ASSERT(directory.isValid());
    writeFile(QDir(directory.path()).filePath(QStringLiteral("demo.dll")));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("Demo.qml")));
    writeFile(QDir(directory.path()).filePath(QStringLiteral("metadata.json")),
              QJsonDocument(object).toJson(QJsonDocument::Compact));
    return PluginManifest::load(directory.path(), QStringLiteral("demo.dll"),
                                testEnvironment());
}
} // namespace

class PluginManifestTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void acceptsLegacyAndV2Manifests() {
        QTemporaryDir legacyDir;
        QVERIFY(legacyDir.isValid());
        QVERIFY(writeFile(QDir(legacyDir.path()).filePath(QStringLiteral("legacy.dll"))));
        const QJsonObject legacy{
            {QStringLiteral("id"), QStringLiteral("legacy")},
            {QStringLiteral("name"), QStringLiteral("Legacy")},
            {QStringLiteral("version"), QStringLiteral("0.1.0")}};
        QVERIFY(writeFile(QDir(legacyDir.path()).filePath(QStringLiteral("metadata.json")),
                          QJsonDocument(legacy).toJson(QJsonDocument::Compact)));
        const auto legacyResult = PluginManifest::load(
            legacyDir.path(), QStringLiteral("legacy.dll"), testEnvironment());
        QVERIFY2(legacyResult.ok, qPrintable(legacyResult.error));
        QCOMPARE(legacyResult.descriptor.apiVersion, 1);

        const auto v2Result = loadManifest(validV2Manifest());
        QVERIFY2(v2Result.ok, qPrintable(v2Result.error));
        QCOMPARE(v2Result.descriptor.id, QStringLiteral("com.example.demo"));
        QCOMPARE(v2Result.descriptor.apiVersion, 2);
        QCOMPARE(v2Result.descriptor.configVersion, 1);

        auto versioned = validV2Manifest();
        versioned[QStringLiteral("configVersion")] = 4;
        const auto versionedResult = loadManifest(versioned);
        QVERIFY2(versionedResult.ok, qPrintable(versionedResult.error));
        QCOMPARE(versionedResult.descriptor.configVersion, 4);
    }

    void rejectsInvalidIdentityAndVersion() {
        auto manifest = validV2Manifest();
        manifest[QStringLiteral("id")] = QStringLiteral("bad id");
        QVERIFY(!loadManifest(manifest).ok);
        manifest = validV2Manifest();
        manifest[QStringLiteral("version")] = QStringLiteral("1.2");
        QVERIFY(!loadManifest(manifest).ok);
        manifest = validV2Manifest();
        manifest[QStringLiteral("apiVersion")] = 3;
        QVERIFY(!loadManifest(manifest).ok);
        manifest = validV2Manifest();
        manifest[QStringLiteral("configVersion")] = 0;
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("configVersion")));
    }

    void rejectsHostAndNativeAbiMismatch() {
        auto manifest = validV2Manifest();
        manifest[QStringLiteral("minHostVersion")] = QStringLiteral("0.4.0");
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("below minimum")));
        manifest = validV2Manifest();
        auto compatibility = manifest[QStringLiteral("compatibility")].toObject();
        compatibility[QStringLiteral("architecture")] = QStringLiteral("arm64");
        manifest[QStringLiteral("compatibility")] = compatibility;
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("architecture")));
    }

    void rejectsEscapingAndMissingPaths() {
        auto manifest = validV2Manifest();
        auto entry = manifest[QStringLiteral("entry")].toObject();
        entry[QStringLiteral("qml")] = QStringLiteral("../outside.qml");
        manifest[QStringLiteral("entry")] = entry;
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("escapes")));

        manifest = validV2Manifest();
        entry = manifest[QStringLiteral("entry")].toObject();
        entry[QStringLiteral("qml")] = QStringLiteral("C:/outside.qml");
        manifest[QStringLiteral("entry")] = entry;
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("relative")));

        manifest = validV2Manifest();
        entry = manifest[QStringLiteral("entry")].toObject();
        entry[QStringLiteral("qml")] = QStringLiteral("Missing.qml");
        manifest[QStringLiteral("entry")] = entry;
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("existing file")));
    }

    void rejectsJunctionEscape() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString pluginDir = QDir(root.path()).filePath(QStringLiteral("plugin"));
        const QString outsideDir = QDir(root.path()).filePath(QStringLiteral("outside"));
        QVERIFY(QDir().mkpath(pluginDir));
        QVERIFY(QDir().mkpath(outsideDir));
        QVERIFY(writeFile(QDir(pluginDir).filePath(QStringLiteral("demo.dll"))));
        QVERIFY(writeFile(QDir(outsideDir).filePath(QStringLiteral("Outside.qml"))));

        const QString junction = QDir(pluginDir).filePath(QStringLiteral("linked"));
        const int exitCode = QProcess::execute(
            QStringLiteral("cmd.exe"),
            {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
             QDir::toNativeSeparators(junction), QDir::toNativeSeparators(outsideDir)});
        if (exitCode != 0) QSKIP("当前环境不允许创建目录联接");

        auto manifest = validV2Manifest();
        auto entry = manifest[QStringLiteral("entry")].toObject();
        entry[QStringLiteral("qml")] = QStringLiteral("linked/Outside.qml");
        manifest[QStringLiteral("entry")] = entry;
        QVERIFY(writeFile(QDir(pluginDir).filePath(QStringLiteral("metadata.json")),
                          QJsonDocument(manifest).toJson(QJsonDocument::Compact)));
        const auto result = PluginManifest::load(
            pluginDir, QStringLiteral("demo.dll"), testEnvironment());
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("outside")),
                 qPrintable(result.error));
    }

    void rejectsMalformedDependencies() {
        auto manifest = validV2Manifest();
        manifest[QStringLiteral("dependencies")] = QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("base")},
                        {QStringLiteral("version"), QStringLiteral("^1.0.0")}}};
        QVERIFY(loadManifest(manifest).error.contains(QStringLiteral("invalid version")));
    }

    void parsesActionDeclarations() {
        auto manifest = validV2Manifest();
        manifest[QStringLiteral("actions")] = QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("refresh")},
            {QStringLiteral("label"), QStringLiteral("立即刷新")},
            {QStringLiteral("placements"),
             QJsonArray{QStringLiteral("quickMenu"), QStringLiteral("tray")}},
            {QStringLiteral("icon"), QStringLiteral("refresh")},
            {QStringLiteral("checkable"), true}}};
        const auto result = loadManifest(manifest);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.descriptor.actions.size(), 1);
        const auto& action = result.descriptor.actions.first();
        QCOMPARE(action.id, QStringLiteral("refresh"));
        QCOMPARE(action.label, QStringLiteral("立即刷新"));
        QCOMPARE(action.placements,
                 QStringList({QStringLiteral("quickMenu"), QStringLiteral("tray")}));
        QCOMPARE(action.icon, QStringLiteral("refresh"));
        QVERIFY(action.checkable);
    }

    void rejectsMalformedActionDeclarations() {
        auto duplicate = validV2Manifest();
        duplicate[QStringLiteral("actions")] = QJsonArray{
            QJsonObject{{QStringLiteral("id"), QStringLiteral("refresh")},
                        {QStringLiteral("label"), QStringLiteral("刷新")},
                        {QStringLiteral("placements"), QJsonArray{QStringLiteral("tray")}}},
            QJsonObject{{QStringLiteral("id"), QStringLiteral("refresh")},
                        {QStringLiteral("label"), QStringLiteral("再次刷新")},
                        {QStringLiteral("placements"), QJsonArray{QStringLiteral("tray")}}}};
        QVERIFY(loadManifest(duplicate).error.contains(QStringLiteral("duplicate action")));

        auto badPlacement = validV2Manifest();
        badPlacement[QStringLiteral("actions")] = QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("refresh")},
            {QStringLiteral("label"), QStringLiteral("刷新")},
            {QStringLiteral("placements"), QJsonArray{QStringLiteral("desktop")}}}};
        QVERIFY(loadManifest(badPlacement).error.contains(QStringLiteral("invalid placement")));
    }

    void parsesResponsiveLayoutPreferences() {
        auto manifest = validV2Manifest();
        manifest[QStringLiteral("layout")] = QJsonObject{
            {QStringLiteral("region"), QStringLiteral("right")},
            {QStringLiteral("minWidth"), 48},
            {QStringLiteral("preferredWidth"), 160},
            {QStringLiteral("maxWidth"), 320},
            {QStringLiteral("flex"), 1},
            {QStringLiteral("priority"), 70},
            {QStringLiteral("overflow"), QStringLiteral("menu")},
        };
        const auto result = loadManifest(manifest);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.descriptor.layout.declared);
        QCOMPARE(result.descriptor.layout.region, QStringLiteral("right"));
        QCOMPARE(result.descriptor.layout.minWidth, 48);
        QCOMPARE(result.descriptor.layout.preferredWidth, 160);
        QCOMPARE(result.descriptor.layout.maxWidth, 320);
        QCOMPARE(result.descriptor.layout.flex, 1);
        QCOMPARE(result.descriptor.layout.priority, 70);
        QCOMPARE(result.descriptor.layout.overflow, QStringLiteral("menu"));
    }

    void rejectsMalformedResponsiveLayoutPreferences() {
        auto invalidRange = validV2Manifest();
        invalidRange[QStringLiteral("layout")] = QJsonObject{
            {QStringLiteral("minWidth"), 120},
            {QStringLiteral("preferredWidth"), 80},
        };
        QVERIFY(loadManifest(invalidRange).error.contains(QStringLiteral("preferredWidth")));

        auto invalidOverflow = validV2Manifest();
        invalidOverflow[QStringLiteral("layout")] = QJsonObject{
            {QStringLiteral("overflow"), QStringLiteral("overlap")},
        };
        QVERIFY(loadManifest(invalidOverflow).error.contains(QStringLiteral("overflow")));
    }

    void rejectsMalformedJson() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(writeFile(QDir(directory.path()).filePath(QStringLiteral("demo.dll"))));
        QVERIFY(writeFile(QDir(directory.path()).filePath(QStringLiteral("metadata.json")),
                          QByteArray("{")));
        const auto result = PluginManifest::load(
            directory.path(), QStringLiteral("demo.dll"), testEnvironment());
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("JSON")));
    }
};

QTEST_GUILESS_MAIN(PluginManifestTest)
#include "PluginManifestTest.moc"
