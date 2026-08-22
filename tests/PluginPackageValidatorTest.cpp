// PluginPackageValidatorTest.cpp — .xuanplugin 离线安全边界与预检回归测试
#include "PluginPackageValidator.h"
#include "PluginPackageStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QtCore/private/qzipwriter_p.h>
#include <QtTest>

#include <windows.h>
#include <bcrypt.h>

namespace {
PluginValidationEnvironment testEnvironment() {
    return {QStringLiteral("0.3.0"), QStringLiteral("x64"),
            QStringLiteral("6.9.3"), QStringLiteral("msvc2022"),
            QStringLiteral("dynamic")};
}

QJsonObject nativeManifest(const QString& version = QStringLiteral("1.2.0")) {
    return {
        {QStringLiteral("id"), QStringLiteral("com.example.demo")},
        {QStringLiteral("name"), QStringLiteral("Demo")},
        {QStringLiteral("version"), version},
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

QJsonObject packageManifest(
    const QString& runtime = QStringLiteral("native"),
    const QString& version = QStringLiteral("1.2.0")) {
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), runtime == QLatin1String("native")
            ? QStringLiteral("com.example.demo") : QStringLiteral("py_demo")},
        {QStringLiteral("version"), version},
        {QStringLiteral("runtime"), runtime},
        {QStringLiteral("manifest"), runtime == QLatin1String("native")
            ? QStringLiteral("payload/metadata.json")
            : QStringLiteral("payload/plugin.json")},
        {QStringLiteral("publisher"), QStringLiteral("Example Publisher")},
    };
}

class TestSigner final {
public:
    TestSigner() {
        if (BCryptOpenAlgorithmProvider(
                &m_algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0)
            return;
        if (BCryptGenerateKeyPair(m_algorithm, &m_key, 256, 0) < 0
            || BCryptFinalizeKeyPair(m_key, 0) < 0) {
            if (m_key) BCryptDestroyKey(m_key);
            m_key = nullptr;
            return;
        }
        ULONG size = 0;
        if (BCryptExportKey(m_key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                            nullptr, 0, &size, 0) < 0)
            return;
        QByteArray blob(int(size), Qt::Uninitialized);
        if (BCryptExportKey(m_key, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                            reinterpret_cast<PUCHAR>(blob.data()), size,
                            &size, 0) < 0
            || size != sizeof(BCRYPT_ECCKEY_BLOB) + 64) {
            return;
        }
        m_publicKey.reserve(65);
        m_publicKey.append(char(0x04));
        m_publicKey.append(blob.constData() + sizeof(BCRYPT_ECCKEY_BLOB), 64);
    }

    ~TestSigner() {
        if (m_key) BCryptDestroyKey(m_key);
        if (m_algorithm) BCryptCloseAlgorithmProvider(m_algorithm, 0);
    }

    bool isValid() const { return m_key && m_publicKey.size() == 65; }
    QByteArray publicKey() const { return m_publicKey; }
    QString keyId() const {
        return QStringLiteral("sha256:") + QString::fromLatin1(
            QCryptographicHash::hash(m_publicKey, QCryptographicHash::Sha256).toHex());
    }
    QByteArray sign(const QByteArray& bytes) const {
        const QByteArray digest = QCryptographicHash::hash(
            bytes, QCryptographicHash::Sha256);
        ULONG size = 0;
        if (BCryptSignHash(
                m_key, nullptr,
                reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                ULONG(digest.size()), nullptr, 0, &size, 0) < 0) {
            return {};
        }
        QByteArray result(int(size), Qt::Uninitialized);
        if (BCryptSignHash(
                m_key, nullptr,
                reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
                ULONG(digest.size()), reinterpret_cast<PUCHAR>(result.data()),
                size, &size, 0) < 0) {
            return {};
        }
        result.resize(int(size));
        return result;
    }

private:
    BCRYPT_ALG_HANDLE m_algorithm = nullptr;
    BCRYPT_KEY_HANDLE m_key = nullptr;
    QByteArray m_publicKey;
};

QMap<QString, QByteArray> nativeFiles(
    const QString& version = QStringLiteral("1.2.0"),
    const QByteArray& backend = QByteArray("stub-dll")) {
    return {
        {QStringLiteral("plugin.json"),
         QJsonDocument(packageManifest(QStringLiteral("native"), version))
             .toJson(QJsonDocument::Compact)},
        {QStringLiteral("payload/Demo.qml"), QByteArray("import QtQuick\nItem {}\n")},
        {QStringLiteral("payload/demo.dll"), backend},
        {QStringLiteral("payload/metadata.json"),
         QJsonDocument(nativeManifest(version)).toJson(QJsonDocument::Compact)},
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
                     const QMap<QString, QString>& links = {},
                     const QByteArray& signatureJson = {}) {
    const QString path = QDir(directory).filePath(QStringLiteral("demo.xuanplugin"));
    QZipWriter writer(path);
    writer.setCompressionPolicy(compression);
    const QByteArray hashes = hashManifest(files, corruptPath);
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        writer.addFile(it.key(), it.value());
    writer.addFile(QStringLiteral("manifest.sha256"), hashes);
    if (!signatureJson.isEmpty())
        writer.addFile(QStringLiteral("signature.json"), signatureJson);
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
        QCOMPARE(result.signatureStatus, QStringLiteral("unsigned"));
        QVERIFY(result.signerKeyId.isEmpty());
        QCOMPARE(result.declaredPermissions,
                 QStringList{QStringLiteral("native.anything")});
        QCOMPARE(result.grantedPermissions, result.declaredPermissions);
        QVERIFY(result.unsupportedPermissions.isEmpty());
        QVERIFY(QFileInfo(QDir(result.payloadDirectory)
            .filePath(QStringLiteral("demo.dll"))).isFile());
    }

    void verifiesSignatureAndRejectsTamperingOrRemoval() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        TestSigner signer;
        QVERIFY(signer.isValid());

        auto files = nativeFiles();
        QJsonObject package = packageManifest();
        package.insert(QStringLiteral("signature"), QJsonObject{
            {QStringLiteral("algorithm"), QStringLiteral("ecdsa-p256-sha256")},
            {QStringLiteral("keyId"), signer.keyId()},
        });
        files[QStringLiteral("plugin.json")] =
            QJsonDocument(package).toJson(QJsonDocument::Compact);
        const QByteArray hashes = hashManifest(files);
        const QByteArray rawSignature = signer.sign(hashes);
        QCOMPARE(rawSignature.size(), 64);
        QJsonObject signature{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("algorithm"), QStringLiteral("ecdsa-p256-sha256")},
            {QStringLiteral("keyId"), signer.keyId()},
            {QStringLiteral("publicKey"),
             QString::fromLatin1(signer.publicKey().toBase64())},
            {QStringLiteral("signed"), QStringLiteral("manifest.sha256")},
            {QStringLiteral("signature"),
             QString::fromLatin1(rawSignature.toBase64())},
        };
        const QByteArray signatureJson =
            QJsonDocument(signature).toJson(QJsonDocument::Compact);
        const auto valid = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), files, QZipWriter::AutoCompress,
                         {}, {}, {}, signatureJson),
            testEnvironment());
        QVERIFY2(valid.ok, qPrintable(valid.error));
        QCOMPARE(valid.signatureStatus, QStringLiteral("verified"));
        QCOMPARE(valid.signatureAlgorithm, QStringLiteral("ecdsa-p256-sha256"));
        QCOMPARE(valid.signerKeyId, signer.keyId());

        QByteArray modifiedSignature = rawSignature;
        modifiedSignature[0] = char(uchar(modifiedSignature[0]) ^ 0x01);
        signature[QStringLiteral("signature")] =
            QString::fromLatin1(modifiedSignature.toBase64());
        const auto tampered = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), files, QZipWriter::AutoCompress,
                         {}, {}, {},
                         QJsonDocument(signature).toJson(QJsonDocument::Compact)),
            testEnvironment());
        QVERIFY(!tampered.ok);
        QVERIFY(tampered.error.contains(QStringLiteral("signature"),
                                         Qt::CaseInsensitive));

        const auto removed = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), files), testEnvironment());
        QVERIFY(!removed.ok);
        QVERIFY(removed.error.contains(QStringLiteral("both")));

        auto undeclaredFiles = nativeFiles();
        const auto undeclared = PluginPackageValidator::validateAndExtract(
            writePackage(directory.path(), undeclaredFiles, QZipWriter::AutoCompress,
                         {}, {}, {}, signatureJson),
            testEnvironment());
        QVERIFY(!undeclared.ok);
        QVERIFY(undeclared.error.contains(QStringLiteral("both")));
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

    void referencePackagerCreatesDeterministicHostVerifiedSignature() {
#if defined(XUANHUB_TEST_PYTHON) && defined(XUANHUB_SOURCE_DIR)
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourceDirectory = directory.filePath(QStringLiteral("plugin"));
        QVERIFY(QDir().mkpath(sourceDirectory));
        auto write = [&sourceDirectory](const QString& name, const QByteArray& data) {
            QFile file(QDir(sourceDirectory).filePath(name));
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
                && file.write(data) == data.size();
        };
        const QJsonObject plugin{
            {QStringLiteral("id"), QStringLiteral("py_signed_demo")},
            {QStringLiteral("name"), QStringLiteral("Signed Demo")},
            {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("apiVersion"), 2},
            {QStringLiteral("minHostVersion"), QStringLiteral("0.3.0")},
            {QStringLiteral("maxHostVersion"), QStringLiteral("0.x")},
            {QStringLiteral("qml"), QStringLiteral("Demo.qml")},
            {QStringLiteral("permissions"), QJsonArray{QStringLiteral("notifications")}},
        };
        QVERIFY(write(QStringLiteral("plugin.json"),
                      QJsonDocument(plugin).toJson(QJsonDocument::Indented)));
        QVERIFY(write(QStringLiteral("main.py"),
                      QByteArray("def update(): return {}\n")));
        QVERIFY(write(QStringLiteral("Demo.qml"),
                      QByteArray("import QtQuick\nItem {}\n")));

        const QDir repository(QStringLiteral(XUANHUB_SOURCE_DIR));
        const QString keyPath = directory.filePath(QStringLiteral("publisher.json"));
        QProcess keyProcess;
        keyProcess.start(QStringLiteral(XUANHUB_TEST_PYTHON), {
            repository.filePath(QStringLiteral("scripts/generate_plugin_signing_key.py")),
            QStringLiteral("--output"), keyPath,
            QStringLiteral("--publisher"), QStringLiteral("Test Publisher"),
            QStringLiteral("--json")});
        QVERIFY(keyProcess.waitForFinished(30000));
        QCOMPARE(keyProcess.exitStatus(), QProcess::NormalExit);
        QCOMPARE(keyProcess.exitCode(), 0);

        auto runPackager = [&](const QString& output) {
            QProcess process;
            process.start(QStringLiteral(XUANHUB_TEST_PYTHON), {
                repository.filePath(QStringLiteral("scripts/package_plugin.py")),
                sourceDirectory,
                QStringLiteral("--runtime"), QStringLiteral("python"),
                QStringLiteral("--signing-key"), keyPath,
                QStringLiteral("--output"), output,
                QStringLiteral("--json")});
            if (!process.waitForFinished(30000)
                || process.exitStatus() != QProcess::NormalExit
                || process.exitCode() != 0) {
                qWarning().noquote() << process.readAllStandardError()
                                     << process.readAllStandardOutput();
                return false;
            }
            return true;
        };
        const QString first = directory.filePath(QStringLiteral("first.xuanplugin"));
        const QString second = directory.filePath(QStringLiteral("second.xuanplugin"));
        QVERIFY(runPackager(first));
        QVERIFY(runPackager(second));
        QFile firstFile(first);
        QFile secondFile(second);
        QVERIFY(firstFile.open(QIODevice::ReadOnly));
        QVERIFY(secondFile.open(QIODevice::ReadOnly));
        QCOMPARE(firstFile.readAll(), secondFile.readAll());

        const auto result = PluginPackageValidator::validateAndExtract(
            first, testEnvironment());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.descriptor.id, QStringLiteral("py_signed_demo"));
        QCOMPARE(result.publisher, QStringLiteral("Test Publisher"));
        QCOMPARE(result.signatureStatus, QStringLiteral("verified"));
        QCOMPARE(result.signatureAlgorithm, QStringLiteral("ecdsa-p256-sha256"));
        QVERIFY(result.signerKeyId.startsWith(QStringLiteral("sha256:")));

        const QString unsafeKey = QDir(sourceDirectory).filePath(
            QStringLiteral("publisher-private.json"));
        QVERIFY(QFile::copy(keyPath, unsafeKey));
        QProcess unsafeProcess;
        unsafeProcess.start(QStringLiteral(XUANHUB_TEST_PYTHON), {
            repository.filePath(QStringLiteral("scripts/package_plugin.py")),
            sourceDirectory,
            QStringLiteral("--runtime"), QStringLiteral("python"),
            QStringLiteral("--signing-key"), unsafeKey,
            QStringLiteral("--output"),
            directory.filePath(QStringLiteral("unsafe.xuanplugin")),
            QStringLiteral("--json")});
        QVERIFY(unsafeProcess.waitForFinished(30000));
        QCOMPARE(unsafeProcess.exitStatus(), QProcess::NormalExit);
        QVERIFY(unsafeProcess.exitCode() != 0);
        QVERIFY(unsafeProcess.readAllStandardOutput().contains(
            "outside the plugin source directory"));

        QProcess scanProcess;
        scanProcess.start(QStringLiteral(XUANHUB_TEST_PYTHON), {
            repository.filePath(QStringLiteral("scripts/package_plugin.py")),
            sourceDirectory,
            QStringLiteral("--runtime"), QStringLiteral("python"),
            QStringLiteral("--output"),
            directory.filePath(QStringLiteral("leaked.xuanplugin")),
            QStringLiteral("--json")});
        QVERIFY(scanProcess.waitForFinished(30000));
        QCOMPARE(scanProcess.exitStatus(), QProcess::NormalExit);
        QVERIFY(scanProcess.exitCode() != 0);
        QVERIFY(scanProcess.readAllStandardOutput().contains(
            "credential-like content"));
#else
        QSKIP("Python 3.12 interpreter was not found while configuring tests");
#endif
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

    void installsIntoImmutableVersionStore() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString package = writePackage(
            directory.path(), nativeFiles(QStringLiteral("1.0.0")));
        const QString configDirectory = QDir(directory.path()).filePath(QStringLiteral("config"));
        PluginPackageStore store(configDirectory);
        const auto installed = store.install(package, {}, {}, testEnvironment());
        QVERIFY2(installed.ok, qPrintable(installed.error));
        QCOMPARE(installed.id, QStringLiteral("com.example.demo"));
        QCOMPARE(installed.version, QStringLiteral("1.0.0"));
        QVERIFY(QFileInfo(QDir(installed.payloadDirectory)
            .filePath(QStringLiteral("demo.dll"))).isFile());

        const ManagedPluginVersion current = store.current(installed.id);
        QCOMPARE(current.version, QStringLiteral("1.0.0"));
        QCOMPARE(current.runtime, QStringLiteral("native"));
        QCOMPARE(current.installRecord.value(QStringLiteral("source")).toString(),
                 QStringLiteral("local-file"));
        QVERIFY(current.installRecord.value(QStringLiteral("packageHash"))
                    .toString().startsWith(QStringLiteral("sha256:")));
        QCOMPARE(store.currentVersions(QStringLiteral("native")).size(), 1);
        QCOMPARE(PluginPackageStore::storeRootForConfigDirectory(configDirectory),
                 QDir(configDirectory).filePath(QStringLiteral("plugin-store")));
    }

    void upgradesActivePluginAndSupportsExplicitRollback() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PluginPackageStore store(QDir(directory.path()).filePath(QStringLiteral("config")));
        const QString package = writePackage(
            directory.path(), nativeFiles(QStringLiteral("1.0.0")));
        QVERIFY(store.install(package, {}, {}, testEnvironment()).ok);

        const QString upgradeDirectory = QDir(directory.path()).filePath(QStringLiteral("upgrade"));
        QVERIFY(QDir().mkpath(upgradeDirectory));
        const QString upgrade = writePackage(
            upgradeDirectory, nativeFiles(QStringLiteral("2.0.0"), QByteArray("v2")));
        QStringList lifecycle;
        PluginPackageStore::ActivationHooks hooks;
        hooks.isActive = [](const QString&) { return true; };
        hooks.stop = [&lifecycle](const QString&, QString*) {
            lifecycle.append(QStringLiteral("stop"));
            return true;
        };
        hooks.start = [&lifecycle, &store](const QString& id, QString*) {
            lifecycle.append(QStringLiteral("start:") + store.current(id).version);
            return true;
        };
        const auto upgraded = store.install(upgrade, hooks, {}, testEnvironment());
        QVERIFY2(upgraded.ok, qPrintable(upgraded.error));
        QCOMPARE(store.current(upgraded.id).version, QStringLiteral("2.0.0"));
        QCOMPARE(store.current(upgraded.id).previousVersion, QStringLiteral("1.0.0"));
        QCOMPARE(lifecycle, QStringList({QStringLiteral("stop"),
                                         QStringLiteral("start:2.0.0")}));

        lifecycle.clear();
        const auto rolledBack = store.rollback(upgraded.id, hooks);
        QVERIFY2(rolledBack.ok, qPrintable(rolledBack.error));
        QCOMPARE(store.current(upgraded.id).version, QStringLiteral("1.0.0"));
        QCOMPARE(store.current(upgraded.id).previousVersion, QStringLiteral("2.0.0"));
        QCOMPARE(lifecycle, QStringList({QStringLiteral("stop"),
                                         QStringLiteral("start:1.0.0")}));
    }

    void restoresOldVersionWhenActivationOrTransactionFails() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PluginPackageStore store(QDir(directory.path()).filePath(QStringLiteral("config")));
        QVERIFY(store.install(
            writePackage(directory.path(), nativeFiles(QStringLiteral("1.0.0"))),
            {}, {}, testEnvironment()).ok);
        const QString upgradeDirectory = QDir(directory.path()).filePath(QStringLiteral("upgrade"));
        QVERIFY(QDir().mkpath(upgradeDirectory));
        const QString upgrade = writePackage(
            upgradeDirectory, nativeFiles(QStringLiteral("2.0.0"), QByteArray("v2")));

        QStringList starts;
        PluginPackageStore::ActivationHooks hooks;
        hooks.isActive = [](const QString&) { return true; };
        hooks.stop = [](const QString&, QString*) { return true; };
        hooks.start = [&store, &starts](const QString& id, QString*) {
            const QString version = store.current(id).version;
            starts.append(version);
            return version == QLatin1String("1.0.0");
        };
        const auto failedActivation = store.install(
            upgrade, hooks, {}, testEnvironment());
        QVERIFY(!failedActivation.ok);
        QVERIFY(failedActivation.rolledBack);
        QCOMPARE(starts, QStringList({QStringLiteral("2.0.0"),
                                      QStringLiteral("1.0.0")}));
        QCOMPARE(store.current(failedActivation.id).version, QStringLiteral("1.0.0"));
        QVERIFY(!QFileInfo(QDir(store.storeRoot()).filePath(
            QStringLiteral("com.example.demo/versions/2.0.0"))).exists());

        const auto injectedFailure = store.install(
            upgrade, hooks,
            [](PluginPackageStore::FaultPoint point) {
                return point == PluginPackageStore::FaultPoint::AfterStaging;
            },
            testEnvironment());
        QVERIFY(!injectedFailure.ok);
        QCOMPARE(store.current(QStringLiteral("com.example.demo")).version,
                 QStringLiteral("1.0.0"));
        QVERIFY(!QFileInfo(QDir(store.storeRoot()).filePath(
            QStringLiteral("com.example.demo/versions/2.0.0"))).exists());
    }

    void rejectsImmutableVersionHashCollision() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PluginPackageStore store(QDir(directory.path()).filePath(QStringLiteral("config")));
        QVERIFY(store.install(
            writePackage(directory.path(), nativeFiles(QStringLiteral("1.0.0"))),
            {}, {}, testEnvironment()).ok);
        const QString collisionDirectory =
            QDir(directory.path()).filePath(QStringLiteral("collision"));
        QVERIFY(QDir().mkpath(collisionDirectory));
        const QString collision = writePackage(
            collisionDirectory,
            nativeFiles(QStringLiteral("1.0.0"), QByteArray("different")));
        const auto result = store.install(collision, {}, {}, testEnvironment());
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("immutable")));
        QCOMPARE(store.current(QStringLiteral("com.example.demo")).version,
                 QStringLiteral("1.0.0"));
    }

    void uninstallRetainsUserDataUnlessExplicitlyDeleted() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PluginPackageStore store(QDir(directory.path()).filePath(QStringLiteral("config")));
        const QString package = writePackage(
            directory.path(), nativeFiles(QStringLiteral("1.0.0")));
        QVERIFY(store.install(package, {}, {}, testEnvironment()).ok);
        const QString dataDirectory = QDir(store.dataRoot()).filePath(
            QStringLiteral("com.example.demo"));
        QVERIFY(QDir().mkpath(dataDirectory));
        QFile data(QDir(dataDirectory).filePath(QStringLiteral("state.json")));
        QVERIFY(data.open(QIODevice::WriteOnly));
        data.write("{}");
        data.close();

        const auto retained = store.uninstall(
            QStringLiteral("com.example.demo"), false);
        QVERIFY2(retained.ok, qPrintable(retained.error));
        QVERIFY(QFileInfo(dataDirectory).isDir());
        QVERIFY(store.current(QStringLiteral("com.example.demo")).id.isEmpty());

        QVERIFY(store.install(package, {}, {}, testEnvironment()).ok);
        const auto removed = store.uninstall(
            QStringLiteral("com.example.demo"), true);
        QVERIFY2(removed.ok, qPrintable(removed.error));
        QVERIFY(!QFileInfo(dataDirectory).exists());
    }

    void refusesToDeleteUserDataThroughJunction() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        PluginPackageStore store(QDir(directory.path()).filePath(QStringLiteral("config")));
        QVERIFY(store.install(
            writePackage(directory.path(), nativeFiles(QStringLiteral("1.0.0"))),
            {}, {}, testEnvironment()).ok);
        QVERIFY(QDir().mkpath(store.dataRoot()));
        const QString outside = QDir(directory.path()).filePath(QStringLiteral("outside"));
        QVERIFY(QDir().mkpath(outside));
        QFile marker(QDir(outside).filePath(QStringLiteral("keep.txt")));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("keep");
        marker.close();
        const QString junction = QDir(store.dataRoot()).filePath(
            QStringLiteral("com.example.demo"));
        const int exitCode = QProcess::execute(
            QStringLiteral("cmd.exe"),
            {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
             QDir::toNativeSeparators(junction), QDir::toNativeSeparators(outside)});
        if (exitCode != 0) QSKIP("当前环境不允许创建目录联接");

        const auto result = store.uninstall(
            QStringLiteral("com.example.demo"), true);
        QVERIFY(result.ok);
        QVERIFY(result.cleanupPending);
        QVERIFY(QFileInfo(QDir(outside).filePath(QStringLiteral("keep.txt"))).isFile());
    }
};

QTEST_GUILESS_MAIN(PluginPackageValidatorTest)
#include "PluginPackageValidatorTest.moc"
