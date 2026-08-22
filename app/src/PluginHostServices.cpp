// PluginHostServices.cpp — 执行插件身份绑定、权限校验和私有数据目录隔离
#include "PluginHostServices.h"

#include "SecretStore.h"
#include "barplugin/EventBus.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using Result = xuanhub::HostServiceResult;

const QStringList kCapabilities{
    QStringLiteral("logger.v1"), QStringLiteral("notifications.v1"),
    QStringLiteral("storage.private.v1"), QStringLiteral("secrets.own.v1"),
    QStringLiteral("settings.navigation.v1"), QStringLiteral("shell.v1"),
    QStringLiteral("locale.v1"), QStringLiteral("theme.v1")};

QString finalCanonicalPath(const QString& path) {
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    const DWORD required = GetFinalPathNameByHandleW(
        handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    QString resolved;
    if (required > 0) {
        resolved.resize(static_cast<qsizetype>(required));
        const DWORD written = GetFinalPathNameByHandleW(
            handle, reinterpret_cast<LPWSTR>(resolved.data()), required,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (written > 0 && written < required)
            resolved.resize(static_cast<qsizetype>(written));
        else
            resolved.clear();
    }
    CloseHandle(handle);
    if (resolved.startsWith(QStringLiteral("\\\\?\\UNC\\")))
        resolved = QStringLiteral("\\\\") + resolved.mid(8);
    else if (resolved.startsWith(QStringLiteral("\\\\?\\")))
        resolved.remove(0, 4);
    return QDir::cleanPath(QDir::fromNativeSeparators(resolved));
#else
    return QDir::cleanPath(QFileInfo(path).canonicalFilePath());
#endif
}

bool isWithin(const QString& root, const QString& candidate) {
    return candidate.compare(root, Qt::CaseInsensitive) == 0
        || candidate.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive);
}

bool isSafeSegment(const QString& segment) {
    if (segment.isEmpty() || segment == QLatin1String(".")
        || segment == QLatin1String("..") || segment.endsWith(QLatin1Char(' '))
        || segment.endsWith(QLatin1Char('.'))
        || segment.contains(QRegularExpression(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])")))) {
        return false;
    }
    const QString base = segment.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QRegularExpression reserved(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
    return !reserved.match(base).hasMatch();
}

Result invalidPath(const QString& message) {
    return Result::failed(QStringLiteral("invalid_path"), message);
}
} // namespace

PluginHostServices::PluginHostServices(
    QString pluginId, xuanhub::HostPluginTrustLevel trustLevel,
    QStringList declaredPermissions, QStringList grantedPermissions,
    Environment environment)
    : m_pluginId(std::move(pluginId)),
      m_trustLevel(trustLevel),
      m_declaredPermissions(std::move(declaredPermissions)),
      m_grantedPermissions(std::move(grantedPermissions)),
      m_environment(std::move(environment)),
      m_storageRoot(QDir(m_environment.configDir).absoluteFilePath(
          QStringLiteral("plugin-data/") + m_pluginId)),
      m_secretStore(std::make_unique<SecretStore>(m_environment.configDir)) {
    m_declaredPermissions.removeDuplicates();
    m_grantedPermissions.removeDuplicates();
}

PluginHostServices::~PluginHostServices() = default;

int PluginHostServices::apiVersion() const { return xuanhub::kHostServicesApiVersion; }
QString PluginHostServices::pluginId() const { return m_pluginId; }
xuanhub::HostPluginTrustLevel PluginHostServices::trustLevel() const { return m_trustLevel; }
QStringList PluginHostServices::declaredPermissions() const { return m_declaredPermissions; }
QStringList PluginHostServices::grantedPermissions() const { return m_grantedPermissions; }
QStringList PluginHostServices::capabilities() const { return kCapabilities; }
bool PluginHostServices::hasCapability(const QString& capability) const {
    return kCapabilities.contains(capability);
}
bool PluginHostServices::hasPermission(const QString& permission) const {
    return m_grantedPermissions.contains(permission);
}

xuanhub::IPluginLogger* PluginHostServices::logger() { return this; }
xuanhub::IPluginNotifications* PluginHostServices::notifications() { return this; }
xuanhub::IPluginStorage* PluginHostServices::storage() { return this; }
xuanhub::IPluginSecrets* PluginHostServices::secrets() { return this; }
xuanhub::IPluginNavigation* PluginHostServices::navigation() { return this; }
xuanhub::IPluginShell* PluginHostServices::shell() { return this; }
xuanhub::IPluginLocale* PluginHostServices::locale() { return this; }
xuanhub::IPluginTheme* PluginHostServices::theme() { return this; }

void PluginHostServices::log(xuanhub::PluginLogLevel level, const QString& message,
                             const QVariantMap& fields) {
    const QString prefix = QStringLiteral("[Plugin:%1]").arg(m_pluginId);
    switch (level) {
    case xuanhub::PluginLogLevel::Debug: qDebug() << prefix << message << fields; break;
    case xuanhub::PluginLogLevel::Info: qInfo() << prefix << message << fields; break;
    case xuanhub::PluginLogLevel::Warning: qWarning() << prefix << message << fields; break;
    case xuanhub::PluginLogLevel::Error: qCritical() << prefix << message << fields; break;
    }
}

Result PluginHostServices::requirePermission(const QString& permission) const {
    if (hasPermission(permission)) return Result::succeeded();
    qWarning() << "[HostServices] permission denied:" << m_pluginId << permission;
    return Result::failed(QStringLiteral("permission_denied"),
                          QStringLiteral("permission not granted: %1").arg(permission));
}

Result PluginHostServices::show(const QString& title, const QString& message) {
    const Result permission = requirePermission(QStringLiteral("notifications"));
    if (!permission.success) return permission;
    if (!m_environment.eventBus)
        return Result::failed(QStringLiteral("service_unavailable"),
                              QStringLiteral("notification service unavailable"));
    m_environment.eventBus->publish(QStringLiteral("app.notify"),
        {{QStringLiteral("title"), title}, {QStringLiteral("message"), message},
         {QStringLiteral("source"), m_pluginId}});
    return Result::succeeded();
}

Result PluginHostServices::resolveStoragePath(const QString& relativePath,
                                              bool allowDirectory,
                                              QString* resolved) const {
    QString normalized = QDir::fromNativeSeparators(relativePath.trimmed());
    if (normalized.isEmpty() && allowDirectory) normalized = QStringLiteral(".");
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized)
        || QRegularExpression(QStringLiteral(R"(^[A-Za-z]:)"))
               .match(normalized).hasMatch()) {
        return invalidPath(QStringLiteral("path must be relative"));
    }
    const QString clean = QDir::cleanPath(normalized);
    if (clean == QLatin1String("..") || clean.startsWith(QStringLiteral("../")))
        return invalidPath(QStringLiteral("path escapes plugin storage"));
    const QStringList segments = clean == QLatin1String(".")
        ? QStringList{} : clean.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString& segment : segments) {
        if (!isSafeSegment(segment))
            return invalidPath(QStringLiteral("path contains an illegal file name"));
    }

    if (!QDir().mkpath(m_storageRoot))
        return Result::failed(QStringLiteral("io_error"),
                              QStringLiteral("cannot create plugin storage"));
    const QString rootCanonical = finalCanonicalPath(m_storageRoot);
    if (rootCanonical.isEmpty())
        return Result::failed(QStringLiteral("io_error"),
                              QStringLiteral("cannot resolve plugin storage"));

    QString current = m_storageRoot;
    for (const QString& segment : segments) {
        current = QDir(current).absoluteFilePath(segment);
        const QFileInfo info(current);
        if (!info.exists()) break;
        const QString canonical = finalCanonicalPath(current);
        if (canonical.isEmpty() || !isWithin(rootCanonical, canonical))
            return invalidPath(QStringLiteral("path resolves outside plugin storage"));
    }
    const QString absolute = QDir(m_storageRoot).absoluteFilePath(clean);
    if (!isWithin(QDir::cleanPath(QFileInfo(m_storageRoot).absoluteFilePath()),
                  QDir::cleanPath(QFileInfo(absolute).absoluteFilePath()))) {
        return invalidPath(QStringLiteral("path escapes plugin storage"));
    }
    if (resolved) *resolved = absolute;
    return Result::succeeded(absolute);
}

Result PluginHostServices::readBytes(const QString& relativePath) const {
    const Result permission = requirePermission(QStringLiteral("storage.private"));
    if (!permission.success) return permission;
    QString path;
    const Result resolved = resolveStoragePath(relativePath, false, &path);
    if (!resolved.success) return resolved;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Result::failed(QStringLiteral("not_found"),
                              QStringLiteral("storage file not found"));
    return Result::succeeded(file.readAll());
}

Result PluginHostServices::writeBytes(const QString& relativePath, const QByteArray& data) {
    const Result permission = requirePermission(QStringLiteral("storage.private"));
    if (!permission.success) return permission;
    QString path;
    const Result resolved = resolveStoragePath(relativePath, false, &path);
    if (!resolved.success) return resolved;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return Result::failed(QStringLiteral("io_error"),
                              QStringLiteral("cannot create storage directory"));
    // 创建父目录后再次解析，阻断并发替换为 junction 的路径。
    const Result checked = resolveStoragePath(relativePath, false, &path);
    if (!checked.success) return checked;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Result::failed(QStringLiteral("io_error"),
                              QStringLiteral("cannot write storage file"));
    return Result::succeeded();
}

Result PluginHostServices::removePath(const QString& relativePath) {
    const Result permission = requirePermission(QStringLiteral("storage.private"));
    if (!permission.success) return permission;
    QString path;
    const Result resolved = resolveStoragePath(relativePath, false, &path);
    if (!resolved.success) return resolved;
    if (!QFileInfo::exists(path)) return Result::succeeded();
    return QFile::remove(path)
        ? Result::succeeded()
        : Result::failed(QStringLiteral("io_error"), QStringLiteral("cannot remove storage file"));
}

Result PluginHostServices::list(const QString& relativeDirectory) const {
    const Result permission = requirePermission(QStringLiteral("storage.private"));
    if (!permission.success) return permission;
    QString path;
    const Result resolved = resolveStoragePath(relativeDirectory, true, &path);
    if (!resolved.success) return resolved;
    QDir directory(path);
    if (!directory.exists()) return Result::succeeded(QStringList{});
    return Result::succeeded(directory.entryList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name));
}

Result PluginHostServices::validateSecretKey(const QString& key) const {
    if (!isSafeSegment(key))
        return Result::failed(QStringLiteral("invalid_key"),
                              QStringLiteral("secret key must be one safe file-name segment"));
    return Result::succeeded();
}

QString PluginHostServices::secretId(const QString& key) const {
    return SecretStore::idForPluginField(m_pluginId,
                                         QStringLiteral("host-service/") + key);
}

Result PluginHostServices::readSecret(const QString& key) const {
    const Result permission = requirePermission(QStringLiteral("secrets.own"));
    if (!permission.success) return permission;
    const Result valid = validateSecretKey(key);
    if (!valid.success) return valid;
    const auto value = m_secretStore->read(secretId(key));
    return value ? Result::succeeded(*value)
                 : Result::failed(QStringLiteral("not_found"),
                                  QStringLiteral("secret not found"));
}

Result PluginHostServices::writeSecret(const QString& key, const QString& value) {
    const Result permission = requirePermission(QStringLiteral("secrets.own"));
    if (!permission.success) return permission;
    const Result valid = validateSecretKey(key);
    if (!valid.success) return valid;
    return m_secretStore->write(secretId(key), value)
        ? Result::succeeded()
        : Result::failed(QStringLiteral("io_error"), QStringLiteral("cannot write secret"));
}

Result PluginHostServices::removeSecret(const QString& key) {
    return writeSecret(key, QString{});
}

Result PluginHostServices::openSettings() {
    if (!m_environment.eventBus)
        return Result::failed(QStringLiteral("service_unavailable"),
                              QStringLiteral("settings service unavailable"));
    m_environment.eventBus->publish(QStringLiteral("app.settings.open"),
        {{QStringLiteral("pluginId"), m_pluginId}});
    return Result::succeeded();
}

Result PluginHostServices::openUrl(const QString& url) {
    const Result permission = requirePermission(QStringLiteral("shell.openUrl"));
    if (!permission.success) return permission;
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.isRelative()
        || (parsed.scheme() != QLatin1String("https")
            && parsed.scheme() != QLatin1String("http"))) {
        return Result::failed(QStringLiteral("invalid_url"),
                              QStringLiteral("only absolute HTTP(S) URLs are allowed"));
    }
    return QDesktopServices::openUrl(parsed)
        ? Result::succeeded()
        : Result::failed(QStringLiteral("open_failed"), QStringLiteral("cannot open URL"));
}

Result PluginHostServices::openFile(const QString& relativePath) {
    const Result permission = requirePermission(QStringLiteral("shell.openFile"));
    if (!permission.success) return permission;
    QString path;
    const Result resolved = resolveStoragePath(relativePath, false, &path);
    if (!resolved.success) return resolved;
    if (!QFileInfo(path).isFile())
        return Result::failed(QStringLiteral("not_found"), QStringLiteral("file not found"));
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path))
        ? Result::succeeded()
        : Result::failed(QStringLiteral("open_failed"), QStringLiteral("cannot open file"));
}

QString PluginHostServices::activeLocale() const {
    return m_environment.activeLocale ? m_environment.activeLocale() : QStringLiteral("en-US");
}

QVariantMap PluginHostServices::currentPalette() const {
    return m_environment.currentPalette ? m_environment.currentPalette() : QVariantMap{};
}
