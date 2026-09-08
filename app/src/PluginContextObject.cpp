// PluginContextObject.cpp — 将 HostServices 结果转换为 QML 可安全消费的值
#include "PluginContextObject.h"

#include "PluginHostServices.h"

PluginContextObject::PluginContextObject(PluginHostServices* services,
                                         QObject* localeNotifier,
                                         QObject* themeNotifier,
                                         QObject* parent)
    : QObject(parent), m_services(services), m_themeNotifier(themeNotifier) {
    if (localeNotifier)
        connect(localeNotifier, SIGNAL(localeChanged()), this, SIGNAL(localeChanged()));
    if (themeNotifier)
        connect(themeNotifier, SIGNAL(themeChanged()), this, SIGNAL(themeChanged()));
}

QString PluginContextObject::pluginId() const {
    return m_services ? m_services->pluginId() : QString{};
}

int PluginContextObject::apiVersion() const {
    return m_services ? m_services->apiVersion() : 0;
}

QString PluginContextObject::trustLevel() const {
    if (!m_services) return {};
    return m_services->trustLevel() == xuanhub::HostPluginTrustLevel::Isolated
        ? QStringLiteral("isolated") : QStringLiteral("trusted-in-process");
}

QStringList PluginContextObject::declaredPermissions() const {
    return m_services ? m_services->declaredPermissions() : QStringList{};
}

QStringList PluginContextObject::grantedPermissions() const {
    return m_services ? m_services->grantedPermissions() : QStringList{};
}

QStringList PluginContextObject::capabilities() const {
    return m_services ? m_services->capabilities() : QStringList{};
}

QString PluginContextObject::activeLocale() const {
    return m_services ? m_services->activeLocale() : QStringLiteral("en-US");
}

QVariantMap PluginContextObject::colors() const {
    return m_services ? m_services->currentPalette() : QVariantMap{};
}

QVariantMap PluginContextObject::appearance() const {
    return m_themeNotifier
        ? m_themeNotifier->property("appearance").toMap() : QVariantMap{};
}

void PluginContextObject::setScreenGeometry(const QVariantMap& geometry) {
    if (m_screenGeometry == geometry) return;
    m_screenGeometry = geometry;
    Q_EMIT screenGeometryChanged();
}

void PluginContextObject::setBarEdge(const QString& edge) {
    const QString normalized = edge == QLatin1String("bottom")
        ? QStringLiteral("bottom") : QStringLiteral("top");
    if (m_barEdge == normalized) return;
    m_barEdge = normalized;
    Q_EMIT barEdgeChanged();
}

bool PluginContextObject::hasPermission(const QString& permission) const {
    return m_services && m_services->hasPermission(permission);
}

bool PluginContextObject::hasCapability(const QString& capability) const {
    return m_services && m_services->hasCapability(capability);
}

void PluginContextObject::log(const QString& level, const QString& message,
                              const QVariantMap& fields) {
    if (!m_services) return;
    xuanhub::PluginLogLevel parsed = xuanhub::PluginLogLevel::Info;
    if (level == QLatin1String("debug")) parsed = xuanhub::PluginLogLevel::Debug;
    else if (level == QLatin1String("warning")) parsed = xuanhub::PluginLogLevel::Warning;
    else if (level == QLatin1String("error")) parsed = xuanhub::PluginLogLevel::Error;
    m_services->log(parsed, message, fields);
}

QVariantMap PluginContextObject::resultMap(const xuanhub::HostServiceResult& result) {
    return {{QStringLiteral("success"), result.success},
            {QStringLiteral("value"), result.value},
            {QStringLiteral("errorCode"), result.errorCode},
            {QStringLiteral("errorMessage"), result.errorMessage}};
}

QVariantMap PluginContextObject::notify(const QString& title, const QString& message) {
    return resultMap(m_services
        ? m_services->show(title, message)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::readText(const QString& relativePath) const {
    const auto result = m_services
        ? m_services->readBytes(relativePath)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable"));
    auto map = resultMap(result);
    if (result.success)
        map.insert(QStringLiteral("value"), QString::fromUtf8(result.value.toByteArray()));
    return map;
}

QVariantMap PluginContextObject::writeText(const QString& relativePath, const QString& text) {
    return resultMap(m_services
        ? m_services->writeBytes(relativePath, text.toUtf8())
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::removePath(const QString& relativePath) {
    return resultMap(m_services
        ? m_services->removePath(relativePath)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::list(const QString& relativeDirectory) const {
    return resultMap(m_services
        ? m_services->list(relativeDirectory)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::readSecret(const QString& key) const {
    return resultMap(m_services
        ? m_services->readSecret(key)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::writeSecret(const QString& key, const QString& value) {
    return resultMap(m_services
        ? m_services->writeSecret(key, value)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::removeSecret(const QString& key) {
    return resultMap(m_services
        ? m_services->removeSecret(key)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::openSettings() {
    return resultMap(m_services
        ? m_services->openSettings()
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::openUrl(const QString& url) {
    return resultMap(m_services
        ? m_services->openUrl(url)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

QVariantMap PluginContextObject::openFile(const QString& relativePath) {
    return resultMap(m_services
        ? m_services->openFile(relativePath)
        : xuanhub::HostServiceResult::failed(QStringLiteral("service_unavailable"),
                                             QStringLiteral("services unavailable")));
}

void PluginContextObject::notifyBarPressed() {
    Q_EMIT barPressed();
}

void PluginContextObject::setPopupService(QObject* service) {
    if (m_popupService == service) return;
    m_popupService = service;
    if (service) connect(service, &QObject::destroyed, this, &PluginContextObject::servicesChanged);
    Q_EMIT servicesChanged();
}

void PluginContextObject::setActions(QObject* service) {
    if (m_actions == service) return;
    m_actions = service;
    if (service) connect(service, &QObject::destroyed, this, &PluginContextObject::servicesChanged);
    Q_EMIT servicesChanged();
}
