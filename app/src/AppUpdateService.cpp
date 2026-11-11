// AppUpdateService.cpp — 固定官方 GitHub 来源，完整校验后才允许退出宿主
#include "AppUpdateService.h"
#include "AppUpdateManifest.h"
#include "AppUpdatePackage.h"
#include "AppUpdateTransaction.h"
#include "SemanticVersion.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <windows.h>

namespace {
const QString kRepository = QStringLiteral("https://github.com/LevonLu/XuanHub");
const QString kApi = QStringLiteral("https://api.github.com/repos/LevonLu/XuanHub/releases/latest");
QString pathIn(const QString& root, const QString& name) { return QDir(root).filePath(name); }
bool failure(QString* error, const QString& text) {
    if (error) *error = text;
    return false;
}
QString updateJobArgument() {
    const QStringList arguments = QCoreApplication::arguments();
    const int index = arguments.indexOf(QStringLiteral("--app-update-job"));
    return index >= 0 && index + 1 < arguments.size() ? arguments[index + 1] : QString{};
}
}

bool AppUpdateRelease::parse(const QByteArray& json, AppUpdateRelease* result, QString* error) {
    const QJsonDocument document = QJsonDocument::fromJson(json);
    const QJsonObject object = document.object();
    if (!document.isObject() || object.value(QStringLiteral("draft")) != false
        || object.value(QStringLiteral("prerelease")) != false)
        return failure(error, appUpdateText("GitHub 未返回正式发行版"));
    AppUpdateRelease release;
    const QString tag = object.value(QStringLiteral("tag_name")).toString();
    release.version = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    SemanticVersion version;
    if (!SemanticVersion::parse(release.version, &version) || !version.prerelease.isEmpty())
        return failure(error, appUpdateText("Release 版本号无效"));
    release.notes = object.value(QStringLiteral("body")).toString().left(128 * 1024);
    release.publishedAt = object.value(QStringLiteral("published_at")).toString();
    const QString name = QStringLiteral("XuanHub-v%1-portable-x64.zip").arg(release.version);
    int matchingAssets = 0;
    for (const QJsonValue& value : object.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString() != name) continue;
        ++matchingAssets;
        release.packageUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        const QString expected = kRepository + QStringLiteral("/releases/download/") + tag
            + QLatin1Char('/') + name;
        const double size = asset.value(QStringLiteral("size")).toDouble(-1);
        const QString digest = asset.value(QStringLiteral("digest")).toString();
        static const QRegularExpression hash(QStringLiteral("^sha256:[0-9a-f]{64}$"));
        if (release.packageUrl.toString() != expected
            || !AppUpdateService::permittedUrl(release.packageUrl, false)
            || !hash.match(digest).hasMatch() || !std::isfinite(size)
            || size <= 0 || size > AppUpdateManifest::MaximumPackageBytes || std::floor(size) != size)
            return failure(error, appUpdateText("Release 附件缺少可信的下载地址、大小或 SHA-256"));
        release.sha256 = digest.mid(7);
        release.size = static_cast<qint64>(size);
    }
    if (matchingAssets != 1)
        return failure(error, appUpdateText("Release 缺少唯一的 Windows x64 便携包"));
    if (result) *result = release;
    return true;
}

void AppUpdatePrepareWorker::run() {
    QString error;
    const QString stage = pathIn(directory, QStringLiteral("stage"));
    if (AppUpdatePackage::extract(pathIn(directory, QStringLiteral("package.zip")),
                                  stage, version, *cancelled, &error)
        && !cancelled->load()) {
        if (AppUpdateManifest::preflight(target, stage, &error)) {
            AppUpdateManifest oldManifest, newManifest;
            AppUpdateManifest::read(target, &oldManifest, &error);
            AppUpdateManifest::read(stage, &newManifest, &error);
            qint64 oldSize = 0, newSize = 0;
            for (const auto& file : oldManifest.files) oldSize += file.size;
            for (const auto& file : newManifest.files) newSize += file.size;
            if (QStorageInfo(directory).bytesAvailable() < oldSize + 16 * 1024 * 1024
                || QStorageInfo(target).bytesAvailable() < newSize + 16 * 1024 * 1024)
                error = appUpdateText("可用磁盘空间不足以备份和替换发行文件");
            QTemporaryFile probe(pathIn(target, QStringLiteral(".xuanhub-update-probe-XXXXXX")));
            if (error.isEmpty() && !probe.open()) error = appUpdateText("发行目录不可写，无法一键更新");
        }
    }
    if (cancelled->load()) error = appUpdateText("更新已取消");
    emit finished(error);
}

AppUpdateService::AppUpdateService(QString targetDirectory, QString configDirectory,
                                   QObject* parent, QNetworkAccessManager* network)
    : QObject(parent), m_target(QFileInfo(targetDirectory).canonicalFilePath()),
      m_configDirectory(std::move(configDirectory)),
      m_network(network ? network : new QNetworkAccessManager(this)),
      m_cancelled(std::make_shared<std::atomic_bool>(false)) {
    m_preferences = readAppUpdateJson(pathIn(m_configDirectory, QStringLiteral("app-update-state.json")));
    const QString lastJob = m_preferences.value(QStringLiteral("lastJob")).toString();
    if (!lastJob.isEmpty()) {
        const QJsonObject result = readAppUpdateJson(pathIn(lastJob, QStringLiteral("result.json")));
        if (result.value(QStringLiteral("status")) == QLatin1String("rolled-back")
            || result.value(QStringLiteral("status")) == QLatin1String("recovery-required")) {
            m_state = QStringLiteral("error");
            m_error = appUpdateText("上次更新未成功：") + result.value(QStringLiteral("message")).toString();
        }
    }
}

AppUpdateService::~AppUpdateService() {
    m_cancelled->store(true);
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
    if (m_thread) { m_thread->quit(); m_thread->wait(); }
}

bool AppUpdateService::busy() const {
    return m_state == QLatin1String("checking") || m_state == QLatin1String("downloading")
        || m_state == QLatin1String("preparing") || m_state == QLatin1String("cancelling")
        || m_state == QLatin1String("restarting");
}

bool AppUpdateService::automaticChecks() const {
    return m_preferences.value(QStringLiteral("automaticChecks")).toBool(true);
}

bool AppUpdateService::savePreferences() {
    QString error;
    if (!writeAppUpdateJson(pathIn(m_configDirectory, QStringLiteral("app-update-state.json")),
                            m_preferences, &error)) { fail(error); return false; }
    return true;
}

void AppUpdateService::setAutomaticChecks(bool enabled) {
    m_preferences.insert(QStringLiteral("automaticChecks"), enabled);
    savePreferences();
    emit changed();
}

void AppUpdateService::scheduleAutomaticCheck() {
    auto* timer = new QTimer(this);
    timer->setInterval(60 * 60 * 1000);
    const auto checkIfDue = [this] {
        const QDateTime last = QDateTime::fromString(
            m_preferences.value(QStringLiteral("lastCheck")).toString(), Qt::ISODate);
        if (automaticChecks() && !busy() && (!last.isValid()
            || last.secsTo(QDateTime::currentDateTimeUtc()) >= 24 * 60 * 60)) {
            check();
            m_backgroundCheck = true;
        }
    };
    connect(timer, &QTimer::timeout, this, checkIfDue);
    QTimer::singleShot(10000, this, checkIfDue);
    timer->start();
}

bool AppUpdateService::permittedUrl(const QUrl& url, bool api) {
    if (!url.isValid() || url.scheme() != QLatin1String("https")
        || !url.userInfo().isEmpty() || url.hasFragment() || (url.port(-1) != -1 && url.port() != 443))
        return false;
    if (api) return url.toString() == kApi;
    if (url.host() == QLatin1String("github.com"))
        return url.path().startsWith(QStringLiteral("/LevonLu/XuanHub/releases/download/"));
    return url.host() == QLatin1String("release-assets.githubusercontent.com")
        || url.host() == QLatin1String("objects.githubusercontent.com")
        || url.host() == QLatin1String("github-releases.githubusercontent.com");
}

void AppUpdateService::check() {
    if (busy()) return;
    m_backgroundCheck = false;
    m_cancelRequested = false;
    m_state = QStringLiteral("checking");
    m_error.clear();
    m_apiBytes.clear();
    m_release = {};
    m_progress = 0;
    m_preferences.insert(QStringLiteral("lastCheck"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!savePreferences()) return;
    emit changed();
    startRequest(QUrl(kApi), true);
}

void AppUpdateService::startRequest(const QUrl& url, bool api, int redirects) {
    if (!permittedUrl(url, api) || redirects > 5) { fail(appUpdateText("更新地址或重定向不受信任")); return; }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(api ? 20000 : 60000);
    request.setRawHeader("User-Agent", "XuanHub-AppUpdater/1");
    request.setRawHeader("Accept", api ? "application/vnd.github+json" : "application/octet-stream");
    if (api) request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    QNetworkReply* reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QIODevice::readyRead, this, [this, reply, api] { consume(reply, api); });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, api, redirects] { finishRequest(reply, api, redirects); });
}

void AppUpdateService::consume(QNetworkReply* reply, bool api) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) { reply->readAll(); return; }
    const QByteArray bytes = reply->readAll();
    if (api) {
        m_apiBytes += bytes;
        if (m_apiBytes.size() > 2 * 1024 * 1024) {
            m_error = appUpdateText("GitHub 响应超过大小限制");
            reply->abort();
        }
    } else {
        m_received += bytes.size();
        if (!m_download || m_received > m_release.size || m_download->write(bytes) != bytes.size()) {
            m_error = appUpdateText("下载超过声明大小，或无法写入磁盘");
            reply->abort();
            return;
        }
        m_hash->addData(bytes);
        m_progress = static_cast<double>(m_received) / m_release.size;
        emit changed();
    }
}

void AppUpdateService::finishRequest(QNetworkReply* reply, bool api, int redirects) {
    if (m_reply != reply) { reply->deleteLater(); return; }
    consume(reply, api);
    m_reply.clear();
    reply->deleteLater();
    if (m_cancelRequested) {
        m_download.reset();
        m_state = QStringLiteral("idle");
        m_error.clear();
        emit changed();
        return;
    }
    if (!m_error.isEmpty()) { fail(m_error); return; }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
        const QUrl target = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        if (api) m_apiBytes.clear();
        startRequest(target, api, redirects + 1);
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        fail(appUpdateText("GitHub 请求失败（%1）：%2").arg(status).arg(reply->errorString()));
        return;
    }
    if (api) {
        QString error;
        if (!AppUpdateRelease::parse(m_apiBytes, &m_release, &error)) { fail(error); return; }
        SemanticVersion current, latest;
        if (!SemanticVersion::parse(QCoreApplication::applicationVersion(), &current)
            || !SemanticVersion::parse(m_release.version, &latest)) {
            fail(appUpdateText("无法比较当前软件版本")); return;
        }
        if (latest.compare(current) <= 0) m_state = QStringLiteral("up-to-date");
        else {
            m_state = QStringLiteral("available");
            if (m_backgroundCheck && m_preferences.value(QStringLiteral("ignoredVersion")) != m_release.version
                && m_preferences.value(QStringLiteral("notifiedVersion")) != m_release.version) {
                m_preferences.insert(QStringLiteral("notifiedVersion"), m_release.version);
                if (savePreferences()) emit updateAvailable(m_release.version);
            }
        }
        qInfo() << "[AppUpdate] release:" << m_release.version << "state:" << m_state;
        emit changed();
    } else {
        m_download->close();
        m_download.reset();
        if (m_received != m_release.size || QString::fromLatin1(m_hash->result().toHex()) != m_release.sha256) {
            fail(appUpdateText("下载包大小或 SHA-256 校验失败")); return;
        }
        prepare();
    }
}

void AppUpdateService::install() {
    if (busy() || m_release.version.isEmpty() || m_state != QLatin1String("available")) return;
    QString error;
    AppUpdateManifest installed;
    if (!AppUpdateManifest::read(m_target, &installed, &error)
        || installed.version != QCoreApplication::applicationVersion()) {
        fail(error.isEmpty() ? appUpdateText("当前发行清单与运行版本不一致") : error);
        return;
    }
    if (QFileInfo::exists(pathIn(m_target, QStringLiteral(".xuanhub-update.json")))) {
        fail(appUpdateText("存在未完成的更新，请先重启以恢复")); return;
    }
    QTemporaryDir temporary(pathIn(QDir::tempPath(), QStringLiteral("XuanHub-update-XXXXXX")));
    if (!temporary.isValid()) { fail(appUpdateText("无法创建更新事务目录")); return; }
    temporary.setAutoRemove(false);
    m_directory = temporary.path();
    m_download = std::make_unique<QFile>(pathIn(m_directory, QStringLiteral("package.zip")));
    if (!m_download->open(QIODevice::WriteOnly)) { fail(appUpdateText("无法创建更新下载文件")); return; }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_received = 0;
    m_progress = 0;
    m_cancelRequested = false;
    m_cancelled->store(false);
    m_error.clear();
    m_state = QStringLiteral("downloading");
    emit changed();
    startRequest(m_release.packageUrl, false);
}

void AppUpdateService::prepare() {
    m_state = QStringLiteral("preparing");
    emit changed();
    auto* thread = new QThread(this);
    m_thread = thread;
    auto* worker = new AppUpdatePrepareWorker;
    worker->directory = m_directory;
    worker->target = m_target;
    worker->version = m_release.version;
    worker->cancelled = m_cancelled;
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &AppUpdatePrepareWorker::run);
    connect(worker, &AppUpdatePrepareWorker::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &AppUpdatePrepareWorker::finished, this, [this, thread](const QString& error) {
        thread->wait();
        m_thread = nullptr;
        thread->deleteLater();
        if (m_cancelRequested) {
            m_state = QStringLiteral("idle");
            m_error.clear();
            emit changed();
        } else if (!error.isEmpty()) fail(error);
        else startHelper();
    });
    thread->start();
}

void AppUpdateService::startHelper() {
    const QString helperDirectory = pathIn(m_directory, QStringLiteral("helper"));
    if (!QDir().mkpath(helperDirectory)) { fail(appUpdateText("无法准备独立更新器")); return; }
    QStringList dependencies = {QStringLiteral("XuanHubUpdater.exe"), QStringLiteral("Qt6Core.dll")};
    const QStringList runtimes = QDir(m_target).entryList(
        {QStringLiteral("vcruntime*.dll"), QStringLiteral("msvcp*.dll"), QStringLiteral("concrt*.dll")}, QDir::Files);
    dependencies.append(runtimes);
    for (const QString& dependency : dependencies) {
        if (!QFile::copy(pathIn(m_target, dependency), pathIn(helperDirectory, dependency))) {
            fail(appUpdateText("无法复制更新器运行文件：%1").arg(dependency)); return;
        }
    }
    QByteArray randomBytes;
    for (int index = 0; index < 8; ++index) {
        const quint32 number = QRandomGenerator::system()->generate();
        randomBytes.append(reinterpret_cast<const char*>(&number), sizeof(number));
    }
    const QString token = QString::fromLatin1(randomBytes.toHex());
    const QString executable = pathIn(helperDirectory, QStringLiteral("XuanHubUpdater.exe"));
    QJsonObject job{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("target"), m_target}, {QStringLiteral("version"), m_release.version},
        {QStringLiteral("oldVersion"), QCoreApplication::applicationVersion()},
        {QStringLiteral("parentPid"), QCoreApplication::applicationPid()},
        {QStringLiteral("token"), token},
        {QStringLiteral("helperSha256"), AppUpdateManifest::hashFile(executable)}};
    QString error;
    if (!writeAppUpdateJson(pathIn(m_directory, QStringLiteral("job.json")), job, &error)) {
        fail(error); return;
    }
    m_preferences.insert(QStringLiteral("lastJob"), m_directory);
    if (!savePreferences()) return;
    if (!QProcess::startDetached(executable, {QStringLiteral("--apply"), m_directory}, helperDirectory)) {
        fail(appUpdateText("无法启动独立更新器，当前版本继续运行")); return;
    }
    m_state = QStringLiteral("restarting");
    emit changed();
    auto* timer = new QTimer(this);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer, elapsed, token] {
        const QJsonObject receipt = readAppUpdateJson(pathIn(m_directory, QStringLiteral("helper-ready.json")));
        if (receipt.value(QStringLiteral("token")) == token) {
            timer->stop(); timer->deleteLater();
            QCoreApplication::quit();
            return;
        }
        const QJsonObject result = readAppUpdateJson(pathIn(m_directory, QStringLiteral("result.json")));
        if (!result.isEmpty() || elapsed->elapsed() >= 60000) {
            timer->stop(); timer->deleteLater();
            fail(result.value(QStringLiteral("message")).toString(
                appUpdateText("更新器没有就绪，当前版本继续运行")));
        }
    });
    timer->start();
}

void AppUpdateService::cancel() {
    if (m_state == QLatin1String("restarting") || !busy()) return;
    m_cancelRequested = true;
    m_cancelled->store(true);
    if (m_reply) m_reply->abort();
    else { m_state = QStringLiteral("cancelling"); emit changed(); }
}

void AppUpdateService::ignoreVersion() {
    if (busy() || m_release.version.isEmpty()) return;
    m_preferences.insert(QStringLiteral("ignoredVersion"), m_release.version);
    if (savePreferences()) { m_state = QStringLiteral("ignored"); emit changed(); }
}

void AppUpdateService::fail(const QString& message) {
    qWarning() << "[AppUpdate]" << message;
    m_download.reset();
    m_state = QStringLiteral("error");
    m_error = message;
    emit changed();
}

bool AppUpdateService::handlePendingUpdate(const QString& exeDirectory) {
    const QString jobArgument = updateJobArgument();
    if (!jobArgument.isEmpty()) {
        AppUpdateTransaction transaction(jobArgument);
        QString error;
        if (transaction.load(&error) && transaction.target().compare(
            QFileInfo(exeDirectory).canonicalFilePath(), Qt::CaseInsensitive) == 0) return false;
        return true;
    }
    const QString marker = pathIn(exeDirectory, QStringLiteral(".xuanhub-update.json"));
    if (!QFileInfo::exists(marker)) return false;
    const QJsonObject pending = readAppUpdateJson(marker);
    const QString directory = pending.value(QStringLiteral("directory")).toString();
    AppUpdateTransaction transaction(directory);
    QString error;
    if (transaction.load(&error) && transaction.target().compare(
        QFileInfo(exeDirectory).canonicalFilePath(), Qt::CaseInsensitive) == 0
        && transaction.job().value(QStringLiteral("token")) == pending.value(QStringLiteral("token"))) {
        const QJsonObject result = readAppUpdateJson(pathIn(directory, QStringLiteral("result.json")));
        const QString status = result.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("updated") || status == QLatin1String("rolled-back")) {
            AppUpdateManifest current;
            const QString expected = transaction.job().value(status == QLatin1String("updated")
                ? QStringLiteral("version") : QStringLiteral("oldVersion")).toString();
            if (AppUpdateManifest::read(exeDirectory, &current, &error)
                && current.version == expected && QFile::remove(marker)) return false;
        }
        const QString identity = QString::fromLatin1(QCryptographicHash::hash(
            transaction.target().toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex());
        const QString mutexName = QStringLiteral("Local\\XuanHubUpdate-") + identity;
        HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, reinterpret_cast<LPCWSTR>(mutexName.utf16()));
        if (mutex) { CloseHandle(mutex); return true; }
        const QString helper = pathIn(directory, QStringLiteral("helper/XuanHubUpdater.exe"));
        if (AppUpdateManifest::hashFile(helper) == transaction.job().value(QStringLiteral("helperSha256")).toString()
            && QProcess::startDetached(helper, {QStringLiteral("--recover"), directory,
                QString::number(QCoreApplication::applicationPid())}, QFileInfo(helper).absolutePath())) return true;
    }
    const QString message = appUpdateText("上次软件更新尚未完成。请保留事务目录并运行其中的更新器恢复。\n%1").arg(directory);
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()), L"XuanHub 更新恢复", MB_OK | MB_ICONERROR);
    return true;
}

void AppUpdateService::reportReady(const QString& exeDirectory) {
    const QString directory = updateJobArgument();
    if (directory.isEmpty()) return;
    AppUpdateTransaction transaction(directory);
    QString error;
    if (!transaction.load(&error) || transaction.target().compare(
            QFileInfo(exeDirectory).canonicalFilePath(), Qt::CaseInsensitive) != 0
        || transaction.job().value(QStringLiteral("version")) != QCoreApplication::applicationVersion()) return;
    writeAppUpdateJson(pathIn(directory, QStringLiteral("app-ready.json")),
        {{QStringLiteral("token"), transaction.job().value(QStringLiteral("token"))},
         {QStringLiteral("version"), QCoreApplication::applicationVersion()},
         {QStringLiteral("pid"), QCoreApplication::applicationPid()}}, &error);
}
