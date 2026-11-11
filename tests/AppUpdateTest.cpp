// AppUpdateTest.cpp — 验证发行清单、真实 ZIP 边界、更新事务与网络状态
#include "AppUpdateManifest.h"
#include "AppUpdatePackage.h"
#include "AppUpdateService.h"
#include "AppUpdateTransaction.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtCore/private/qzipwriter_p.h>
#include <QtTest>

namespace {
bool write(const QString& root, const QString& path, const QByteArray& bytes) {
    const QString full = QDir(root).filePath(path);
    if (!QDir().mkpath(QFileInfo(full).absolutePath())) return false;
    QFile file(full);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& root, const QString& path) {
    QFile file(QDir(root).filePath(path));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QMap<QString, QByteArray> baseFiles(const QByteArray& content) {
    return {{QStringLiteral("XuanHub.exe"), content},
            {QStringLiteral("XuanHubUpdater.exe"), content},
            {QStringLiteral("barplugin.dll"), content},
            {QStringLiteral("Qt6Core.dll"), content}};
}
QByteArray manifest(const QString& version, const QMap<QString, QByteArray>& files) {
    QJsonArray entries;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        entries.append(QJsonObject{{QStringLiteral("path"), it.key()},
            {QStringLiteral("size"), it.value().size()},
            {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(
                it.value(), QCryptographicHash::Sha256).toHex())}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("version"), version}, {QStringLiteral("files"), entries}}).toJson();
}
bool distribution(const QString& root, const QString& version,
                  const QMap<QString, QByteArray>& files) {
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        if (!write(root, it.key(), it.value())) return false;
    return write(root, AppUpdateManifest::fileName(), manifest(version, files));
}
bool createJob(const QString& directory, const QString& target) {
    QDir().mkpath(directory);
    return writeAppUpdateJson(QDir(directory).filePath(QStringLiteral("job.json")),
        {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("target"), target},
         {QStringLiteral("oldVersion"), QStringLiteral("0.4.4")},
         {QStringLiteral("version"), QStringLiteral("0.4.5")},
         {QStringLiteral("token"), QString(64, QLatin1Char('a'))}}, nullptr);
}
QByteArray releaseJson(const QByteArray& package = "abc", const QString& version = QStringLiteral("0.4.5")) {
    const QString name = QStringLiteral("XuanHub-v%1-portable-x64.zip").arg(version);
    return QJsonDocument(QJsonObject{{QStringLiteral("tag_name"), QStringLiteral("v") + version},
        {QStringLiteral("draft"), false}, {QStringLiteral("prerelease"), false},
        {QStringLiteral("body"), QStringLiteral("更新说明")},
        {QStringLiteral("assets"), QJsonArray{QJsonObject{
            {QStringLiteral("name"), name}, {QStringLiteral("size"), package.size()},
            {QStringLiteral("digest"), QStringLiteral("sha256:") + QString::fromLatin1(
                QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex())},
            {QStringLiteral("browser_download_url"), QStringLiteral(
                "https://github.com/LevonLu/XuanHub/releases/download/v%1/%2").arg(version, name)}}}}}).toJson();
}
struct Response { QByteArray bytes; int status = 200; QUrl redirect; };
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent)
        : QNetworkReply(parent), bytes(response.bytes) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        if (!response.redirect.isEmpty())
            setAttribute(QNetworkRequest::RedirectionTargetAttribute, response.redirect);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(10, this, [this] {
            if (isFinished()) return;
            emit readyRead();
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        QTimer::singleShot(0, this, [this] { emit finished(); });
    }
    qint64 bytesAvailable() const override { return bytes.size() - offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char* data, qint64 maximum) override {
        const qint64 count = qMin(maximum, bytes.size() - offset);
        if (count <= 0) return -1;
        memcpy(data, bytes.constData() + offset, static_cast<size_t>(count));
        offset += count;
        return count;
    }
private:
    QByteArray bytes;
    qint64 offset = 0;
};
class Network final : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QUrl> requests;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        requests.append(request.url());
        return new Reply(request, responses.isEmpty() ? Response{{}, 404, {}} : responses.takeFirst(), this);
    }
};
}

class AppUpdateTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QCoreApplication::setApplicationVersion(QStringLiteral("0.4.4")); }
    void manifestRejectsAliasesAndProtectedFiles() {
        const auto base = baseFiles("old");
        QString error;
        AppUpdateManifest parsed;
        QVERIFY(AppUpdateManifest::parse(manifest(QStringLiteral("0.4.4"), base), &parsed, &error));
        for (const QString& path : {QStringLiteral("../bad"), QStringLiteral("C:/bad"),
             QStringLiteral("a\\b"), QStringLiteral("/absolute"), QStringLiteral("nul.txt"),
             QStringLiteral("a./b"), QStringLiteral("a /b"), QStringLiteral("a//b"),
             QStringLiteral("config.json"), QStringLiteral("plugin-data/demo/file"),
             QStringLiteral("plugin-store/demo/file"), QStringLiteral("secrets.json")}) {
            auto files = base;
            files.insert(path, "bad");
            QVERIFY2(!AppUpdateManifest::parse(manifest(QStringLiteral("0.4.4"), files), &parsed, &error), qPrintable(path));
        }
        auto duplicate = base;
        duplicate.insert(QStringLiteral("xuanhub.exe"), "duplicate");
        QVERIFY(!AppUpdateManifest::parse(manifest(QStringLiteral("0.4.4"), duplicate), &parsed, &error));
    }
    void conflictAndJunctionAreRejected() {
        QTemporaryDir root;
        const QString target = root.filePath(QStringLiteral("target"));
        const QString stage = root.filePath(QStringLiteral("stage"));
        auto newer = baseFiles("new");
        newer.insert(QStringLiteral("plugins/custom/file.txt"), "official");
        QVERIFY(distribution(target, QStringLiteral("0.4.4"), baseFiles("old")));
        QVERIFY(distribution(stage, QStringLiteral("0.4.5"), newer));
        QVERIFY(write(target, QStringLiteral("plugins/custom/file.txt"), "user"));
        QString error;
        QVERIFY(!AppUpdateManifest::preflight(target, stage, &error));
        QVERIFY(error.contains(QStringLiteral("冲突")));
        QFile::remove(QDir(target).filePath(QStringLiteral("plugins/custom/file.txt")));
        QVERIFY(write(target, QStringLiteral("XuanHub.exe"), "modified"));
        QVERIFY(!AppUpdateManifest::preflight(target, stage, &error));
    }
    void transactionPreservesUsersAndRemovesOnlyOldOfficialFiles() {
        QTemporaryDir root;
        const QString target = root.filePath(QStringLiteral("target"));
        const QString job = root.filePath(QStringLiteral("job"));
        auto old = baseFiles("old"); old.insert(QStringLiteral("obsolete.txt"), "old-file");
        auto newer = baseFiles("new"); newer.insert(QStringLiteral("new/file.txt"), "new-file");
        QVERIFY(distribution(target, QStringLiteral("0.4.4"), old));
        QVERIFY(distribution(QDir(job).filePath(QStringLiteral("stage")), QStringLiteral("0.4.5"), newer));
        QVERIFY(write(target, QStringLiteral("config.json"), "my-config"));
        QVERIFY(write(target, QStringLiteral("secrets.json"), "my-secret"));
        QVERIFY(write(target, QStringLiteral("plugins/custom/main.qml"), "my-plugin"));
        QVERIFY(createJob(job, target));
        AppUpdateTransaction transaction(job);
        QString error;
        QVERIFY2(transaction.load(&error), qPrintable(error));
        QVERIFY2(transaction.apply(&error), qPrintable(error));
        QCOMPARE(read(target, QStringLiteral("XuanHub.exe")), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(QDir(target).filePath(QStringLiteral("obsolete.txt"))));
        QCOMPARE(read(target, QStringLiteral("config.json")), QByteArray("my-config"));
        QCOMPARE(read(target, QStringLiteral("secrets.json")), QByteArray("my-secret"));
        QCOMPARE(read(target, QStringLiteral("plugins/custom/main.qml")), QByteArray("my-plugin"));
        // 模拟进程中断后重新创建事务对象，依赖磁盘日志而非内存来恢复。
        AppUpdateTransaction recovery(job);
        QVERIFY(recovery.load(&error));
        QVERIFY2(recovery.rollback(&error), qPrintable(error));
        QCOMPARE(read(target, QStringLiteral("XuanHub.exe")), QByteArray("old"));
        QCOMPARE(read(target, QStringLiteral("obsolete.txt")), QByteArray("old-file"));
        QVERIFY(!QFileInfo::exists(QDir(target).filePath(QStringLiteral("new/file.txt"))));
    }
    void failureAfterPartialReplacementRollsBack() {
        QTemporaryDir root;
        const QString target = root.filePath(QStringLiteral("target"));
        const QString job = root.filePath(QStringLiteral("job"));
        QVERIFY(distribution(target, QStringLiteral("0.4.4"), baseFiles("old")));
        QVERIFY(distribution(QDir(job).filePath(QStringLiteral("stage")), QStringLiteral("0.4.5"), baseFiles("new")));
        QVERIFY(createJob(job, target));
        AppUpdateTransaction transaction(job);
        QString error;
        QVERIFY(transaction.load(&error));
        QVERIFY(!transaction.apply(&error, [](int count) { return count == 2; }));
        QVERIFY2(transaction.rollback(&error), qPrintable(error));
        AppUpdateManifest restored;
        QVERIFY(AppUpdateManifest::read(target, &restored, &error));
        QCOMPARE(restored.version, QStringLiteral("0.4.4"));
        QVERIFY(AppUpdateManifest::verify(target, restored, &error));
    }
    void realZipRejectsUnexpectedPayloadAndWrongVersion() {
        QTemporaryDir root;
        const auto files = baseFiles("new");
        const QString archive = root.filePath(QStringLiteral("package.zip"));
        {
            QZipWriter writer(archive);
            for (auto it = files.cbegin(); it != files.cend(); ++it) writer.addFile(it.key(), it.value());
            writer.addFile(AppUpdateManifest::fileName(), manifest(QStringLiteral("0.4.5"), files));
            writer.addFile(QStringLiteral("config.json"), QByteArray("default"));
            writer.addFile(QStringLiteral("portable"), QByteArray{});
            writer.close();
        }
        std::atomic_bool cancelled(false);
        QString error;
        QVERIFY2(AppUpdatePackage::extract(archive, root.filePath(QStringLiteral("good")),
            QStringLiteral("0.4.5"), cancelled, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("good/config.json"))));
        QVERIFY(!AppUpdatePackage::extract(archive, root.filePath(QStringLiteral("wrong")),
            QStringLiteral("0.4.6"), cancelled, &error));
        {
            QZipWriter writer(archive);
            for (auto it = files.cbegin(); it != files.cend(); ++it) writer.addFile(it.key(), it.value());
            writer.addFile(AppUpdateManifest::fileName(), manifest(QStringLiteral("0.4.5"), files));
            writer.addFile(QStringLiteral("../escape.txt"), QByteArray("evil"));
        }
        QVERIFY(!AppUpdatePackage::extract(archive, root.filePath(QStringLiteral("evil")),
            QStringLiteral("0.4.5"), cancelled, &error));
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("escape.txt"))));
    }
    void realZipRejectsLinksDuplicatesAndCorruptPayload() {
        QTemporaryDir root;
        const auto files = baseFiles("new");
        std::atomic_bool cancelled(false);
        for (const QString& kind : {QStringLiteral("link"), QStringLiteral("duplicate"),
                                    QStringLiteral("case"), QStringLiteral("hash")}) {
            const QString archive = root.filePath(kind + QStringLiteral(".zip"));
            {
                QZipWriter writer(archive);
                for (auto it = files.cbegin(); it != files.cend(); ++it)
                    writer.addFile(it.key(), kind == QLatin1String("hash")
                        && it.key() == QLatin1String("XuanHub.exe") ? QByteArray("bad") : it.value());
                writer.addFile(AppUpdateManifest::fileName(), manifest(QStringLiteral("0.4.5"), files));
                if (kind == QLatin1String("link")) writer.addSymLink(QStringLiteral("link"), QStringLiteral("XuanHub.exe"));
                if (kind == QLatin1String("duplicate")) writer.addFile(QStringLiteral("XuanHub.exe"), QByteArray("new"));
                if (kind == QLatin1String("case")) writer.addFile(QStringLiteral("xuanhub.exe"), QByteArray("new"));
            }
            QString error;
            QVERIFY2(!AppUpdatePackage::extract(archive, root.filePath(kind),
                QStringLiteral("0.4.5"), cancelled, &error), qPrintable(kind));
        }
    }
    void releaseAndRedirectRules() {
        AppUpdateRelease release;
        QString error;
        QVERIFY(AppUpdateRelease::parse(releaseJson(), &release, &error));
        QCOMPARE(release.version, QStringLiteral("0.4.5"));
        QJsonObject object = QJsonDocument::fromJson(releaseJson()).object();
        object.insert(QStringLiteral("prerelease"), true);
        QVERIFY(!AppUpdateRelease::parse(QJsonDocument(object).toJson(), &release, &error));
        QVERIFY(!AppUpdateService::permittedUrl(QUrl(QStringLiteral("http://github.com/LevonLu/XuanHub/releases/download/v1/file")), false));
        QVERIFY(!AppUpdateService::permittedUrl(QUrl(QStringLiteral("https://github.com/other/repo/releases/download/file")), false));
        QVERIFY(!AppUpdateService::permittedUrl(QUrl(QStringLiteral("https://github.com.evil.test/file")), false));
        QVERIFY(!AppUpdateService::permittedUrl(QUrl(QStringLiteral("https://user@release-assets.githubusercontent.com/file")), false));
    }
    void checkIsSeparateFromDownloadAndIgnoreCanBeRevisited() {
        QTemporaryDir root;
        Network network;
        network.responses = {{releaseJson()}, {releaseJson()}};
        AppUpdateService service(root.path(), root.path(), nullptr, &network);
        service.check();
        QTRY_COMPARE(service.state(), QStringLiteral("available"));
        QCOMPARE(network.requests.size(), 1);
        service.ignoreVersion();
        QCOMPARE(service.state(), QStringLiteral("ignored"));
        service.check();
        QTRY_COMPARE(service.state(), QStringLiteral("available"));
        service.setAutomaticChecks(false);
        AppUpdateService reopened(root.path(), root.path());
        QVERIFY(!reopened.automaticChecks());
    }
    void cancellationAndNetworkFailuresDoNotReportLatest() {
        QTemporaryDir root;
        Network network;
        network.responses = {{{}, 403, {}}, {releaseJson()}};
        AppUpdateService service(root.path(), root.path(), nullptr, &network);
        service.check();
        QTRY_COMPARE(service.state(), QStringLiteral("error"));
        QVERIFY(!service.error().isEmpty());
        service.check();
        service.cancel();
        QTRY_COMPARE(service.state(), QStringLiteral("idle"));
        QVERIFY(!service.busy());
    }
    void downloadHashMismatchLeavesInstalledFilesUntouched() {
        QTemporaryDir root;
        QVERIFY(distribution(root.path(), QStringLiteral("0.4.4"), baseFiles("old")));
        Network network;
        network.responses = {{releaseJson("abc")}, {QByteArray("bad")}};
        AppUpdateService service(root.path(), root.path(), nullptr, &network);
        service.check();
        QTRY_COMPARE(service.state(), QStringLiteral("available"));
        service.install();
        QTRY_COMPARE(service.state(), QStringLiteral("error"));
        QVERIFY(service.error().contains(QStringLiteral("SHA-256")));
        QCOMPARE(read(root.path(), QStringLiteral("XuanHub.exe")), QByteArray("old"));
    }
};
QTEST_GUILESS_MAIN(AppUpdateTest)
#include "AppUpdateTest.moc"
