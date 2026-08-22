// PluginUpdateService.cpp — 检查与下载分离，下载后仍进入统一离线包校验器
#include "PluginUpdateService.h"

#include "PluginPackageValidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTemporaryFile>

#include <utility>

namespace {
constexpr qsizetype kMaximumIndexBytes = 4 * 1024 * 1024;

QNetworkRequest safeRequest(const QUrl& url, int timeoutMs) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(timeoutMs);
    request.setRawHeader("Accept", "application/json, application/octet-stream");
    request.setRawHeader("User-Agent", "XuanHub-PluginUpdater/1");
    return request;
}
} // namespace

PluginUpdateService::PluginUpdateService(QString configDirectory, QObject* parent)
    : QObject(parent), m_configDirectory(std::move(configDirectory)) {}

QString PluginUpdateService::candidateKey(const QString& id, const QString& version) {
    return id + QLatin1Char('\n') + version;
}

void PluginUpdateService::check(const QStringList& sourceUrls,
                                const QVariantMap& installedPlugins) {
    if (m_checking || downloading()) return;
    m_checking = true;
    emit checkingChanged();
    m_installedPlugins = installedPlugins;
    m_indexCandidates.clear();
    m_checkErrors.clear();
    m_available.clear();
    m_pendingIndexes = 0;

    QSet<QString> seen;
    for (const QString& sourceText : sourceUrls) {
        const QUrl source(sourceText.trimmed());
        QString error;
        if (!PluginUpdateIndex::validateSourceUrl(source, &error)) {
            m_checkErrors.append(sourceText + QStringLiteral(": ") + error);
            continue;
        }
        const QString canonical = source.toString(QUrl::FullyEncoded);
        if (seen.contains(canonical)) continue;
        seen.insert(canonical);
        QNetworkReply* reply = m_network.get(safeRequest(source, 30000));
        m_indexDownloads.insert(reply, {});
        ++m_pendingIndexes;
        connect(reply, &QIODevice::readyRead, this, [this, reply] {
            auto it = m_indexDownloads.find(reply);
            if (it == m_indexDownloads.end()) return;
            it->bytes += reply->readAll();
            if (it->bytes.size() > kMaximumIndexBytes && it->error.isEmpty()) {
                it->error = QStringLiteral("update index exceeds 4 MiB");
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this,
                [this, reply] { finishIndex(reply); });
    }
    if (m_pendingIndexes == 0) finalizeCheck();
}

void PluginUpdateService::finishIndex(QNetworkReply* reply) {
    auto it = m_indexDownloads.find(reply);
    if (it == m_indexDownloads.end()) return;
    it->bytes += reply->readAll();
    const QUrl finalUrl = reply->url();
    if (it->error.isEmpty() && reply->error() != QNetworkReply::NoError)
        it->error = reply->errorString();
    if (it->error.isEmpty()) {
        const PluginUpdateIndexResult parsed =
            PluginUpdateIndex::parse(it->bytes, finalUrl);
        if (parsed.ok) {
            m_indexCandidates.append(parsed.packages);
        } else {
            it->error = parsed.error;
        }
    }
    if (!it->error.isEmpty()) {
        m_checkErrors.append(finalUrl.toString(QUrl::FullyEncoded)
                             + QStringLiteral(": ") + it->error);
    }
    m_indexDownloads.erase(it);
    reply->deleteLater();
    --m_pendingIndexes;
    if (m_pendingIndexes == 0) finalizeCheck();
}

void PluginUpdateService::finalizeCheck() {
    QHash<QString, QString> versions;
    QSet<QString> blocked;
    QHash<QString, PluginUpdateCandidate> unique;
    for (auto it = m_installedPlugins.cbegin(); it != m_installedPlugins.cend(); ++it) {
        versions.insert(it.key(), it.value().toMap().value(QStringLiteral("version")).toString());
    }
    for (const PluginUpdateCandidate& candidate : std::as_const(m_indexCandidates)) {
        const QVariantMap installed = m_installedPlugins.value(candidate.id).toMap();
        if (installed.isEmpty()) continue;
        if (installed.value(QStringLiteral("runtime")).toString() != candidate.runtime) {
            m_checkErrors.append(QStringLiteral("%1 %2: runtime does not match installed plugin")
                                     .arg(candidate.id, candidate.version));
            continue;
        }
        const QString pinnedKey = installed.value(QStringLiteral("signerKeyId")).toString();
        if (!pinnedKey.isEmpty() && candidate.signerKeyId != pinnedKey) {
            m_checkErrors.append(QStringLiteral("%1 %2: signer key differs from installed version")
                                     .arg(candidate.id, candidate.version));
            continue;
        }
        const QString key = candidateKey(candidate.id, candidate.version);
        const auto existing = unique.constFind(key);
        if (existing != unique.cend()
            && (existing->packageSha256 != candidate.packageSha256
                || existing->packageUrl != candidate.packageUrl
                || existing->signerKeyId != candidate.signerKeyId)) {
            blocked.insert(key);
            m_checkErrors.append(QStringLiteral("%1 %2: conflicting update entries")
                                     .arg(candidate.id, candidate.version));
            continue;
        }
        unique.insert(key, candidate);
    }
    QList<PluginUpdateCandidate> candidates;
    for (auto it = unique.cbegin(); it != unique.cend(); ++it) {
        if (!blocked.contains(it.key())) candidates.append(it.value());
    }
    const QList<PluginUpdateCandidate> updates =
        PluginUpdateIndex::newerCandidates(candidates, versions);
    QVariantList maps;
    for (const PluginUpdateCandidate& update : updates) {
        maps.append(update.toVariantMap());
        m_available.insert(candidateKey(update.id, update.version), update);
    }
    m_checking = false;
    emit checkingChanged();
    emit checkFinished(maps, m_checkErrors);
}

void PluginUpdateService::download(const QString& id, const QString& version) {
    if (m_checking || downloading()) return;
    const auto candidateIt = m_available.constFind(candidateKey(id, version));
    if (candidateIt == m_available.cend()) {
        emit downloadFinished({}, {}, QStringLiteral("update candidate is no longer available"));
        return;
    }
    const QString cacheDirectory = QDir(m_configDirectory).filePath(
        QStringLiteral("plugin-update-cache"));
    if (!QDir().mkpath(cacheDirectory)) {
        emit downloadFinished({}, candidateIt->toVariantMap(),
                              QStringLiteral("cannot create plugin update cache"));
        return;
    }
    m_downloadFile = new QTemporaryFile(
        QDir(cacheDirectory).filePath(QStringLiteral(".download-XXXXXX.xuanplugin")), this);
    if (!m_downloadFile->open()) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit downloadFinished({}, candidateIt->toVariantMap(),
                              QStringLiteral("cannot create temporary update file"));
        return;
    }
    m_downloadHash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_downloadingCandidate = candidateIt.value();
    m_downloadedBytes = 0;
    m_downloadError.clear();
    m_downloadReply = m_network.get(safeRequest(candidateIt->packageUrl, 120000));
    emit downloadingChanged();
    connect(m_downloadReply, &QIODevice::readyRead, this, [this] {
        if (!m_downloadReply || !m_downloadFile || !m_downloadHash) return;
        const QByteArray bytes = m_downloadReply->readAll();
        m_downloadedBytes += bytes.size();
        if (m_downloadedBytes > m_downloadingCandidate.packageSize) {
            m_downloadError = QStringLiteral("download exceeds the size declared by the update index");
            m_downloadReply->abort();
            return;
        }
        if (m_downloadFile->write(bytes) != bytes.size()) {
            m_downloadError = QStringLiteral("cannot write downloaded plugin package");
            m_downloadReply->abort();
            return;
        }
        m_downloadHash->addData(bytes);
    });
    connect(m_downloadReply, &QNetworkReply::finished,
            this, &PluginUpdateService::finishPackageDownload);
}

void PluginUpdateService::cancelDownload() {
    if (!m_downloadReply) return;
    m_downloadError = QStringLiteral("plugin update download was canceled");
    m_downloadReply->abort();
}

void PluginUpdateService::finishPackageDownload() {
    if (!m_downloadReply || !m_downloadFile || !m_downloadHash) return;
    const QByteArray finalBytes = m_downloadReply->readAll();
    if (!finalBytes.isEmpty() && m_downloadError.isEmpty()) {
        m_downloadedBytes += finalBytes.size();
        if (m_downloadedBytes > m_downloadingCandidate.packageSize
            || m_downloadFile->write(finalBytes) != finalBytes.size()) {
            m_downloadError = QStringLiteral("downloaded plugin package has an invalid size or cannot be written");
        } else {
            m_downloadHash->addData(finalBytes);
        }
    }
    if (m_downloadError.isEmpty()
        && m_downloadReply->error() != QNetworkReply::NoError) {
        m_downloadError = m_downloadReply->errorString();
    }
    const QString actualHash = QStringLiteral("sha256:")
        + QString::fromLatin1(m_downloadHash->result().toHex());
    if (m_downloadError.isEmpty()
        && (m_downloadedBytes != m_downloadingCandidate.packageSize
            || actualHash != m_downloadingCandidate.packageSha256)) {
        m_downloadError = QStringLiteral("downloaded plugin package does not match index size or SHA-256");
    }
    m_downloadFile->flush();
    m_downloadFile->close();

    QString finalPath;
    if (m_downloadError.isEmpty()) {
        const QString cacheDirectory = QFileInfo(m_downloadFile->fileName()).absolutePath();
        finalPath = QDir(cacheDirectory).filePath(
            m_downloadingCandidate.packageSha256.mid(7) + QStringLiteral(".xuanplugin"));
        // 缓存不是信任根：即使同名文件已存在也用本次已校验下载覆盖，避免复用被本地
        // 修改过但文件名仍伪装成旧哈希的内容。
        if ((QFileInfo::exists(finalPath) && !QFile::remove(finalPath))
            || !QFile::rename(m_downloadFile->fileName(), finalPath)) {
            m_downloadError = QStringLiteral("cannot commit downloaded plugin package");
        }
    }
    if (m_downloadError.isEmpty()) {
        const PluginPackageValidationResult package =
            PluginPackageValidator::validateAndExtract(finalPath);
        if (!package.ok) {
            m_downloadError = package.error;
        } else if (package.descriptor.id != m_downloadingCandidate.id
                   || package.descriptor.version != m_downloadingCandidate.version
                   || package.runtime != m_downloadingCandidate.runtime
                   || (!m_downloadingCandidate.signerKeyId.isEmpty()
                       && package.signerKeyId != m_downloadingCandidate.signerKeyId)) {
            m_downloadError = QStringLiteral("downloaded package identity or signer does not match update index");
        }
    }
    const QVariantMap update = m_downloadingCandidate.toVariantMap();
    const QString error = m_downloadError;
    if (!error.isEmpty() && !finalPath.isEmpty()) QFile::remove(finalPath);
    resetPackageDownload(true);
    emit downloadFinished(error.isEmpty() ? finalPath : QString(), update, error);
}

void PluginUpdateService::resetPackageDownload(bool removeTemporary) {
    if (m_downloadReply) m_downloadReply->deleteLater();
    m_downloadReply = nullptr;
    if (m_downloadFile) {
        if (removeTemporary) QFile::remove(m_downloadFile->fileName());
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    delete m_downloadHash;
    m_downloadHash = nullptr;
    m_downloadedBytes = 0;
    m_downloadError.clear();
    emit downloadingChanged();
}
