// PluginContextObject.h — 向插件 QML 暴露绑定身份后的主题、语言与宿主服务
#pragma once

#include "barplugin/HostServices.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class PluginHostServices;

class PluginContextObject final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(int contextVersion READ contextVersion CONSTANT)
    Q_PROPERTY(QObject* popupService READ popupService WRITE setPopupService NOTIFY servicesChanged)
    Q_PROPERTY(QObject* actions READ actions WRITE setActions NOTIFY servicesChanged)
    Q_PROPERTY(int apiVersion READ apiVersion CONSTANT)
    Q_PROPERTY(QString trustLevel READ trustLevel CONSTANT)
    Q_PROPERTY(QStringList declaredPermissions READ declaredPermissions CONSTANT)
    Q_PROPERTY(QStringList grantedPermissions READ grantedPermissions CONSTANT)
    Q_PROPERTY(QStringList capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QString activeLocale READ activeLocale NOTIFY localeChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap appearance READ appearance NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap screenGeometry READ screenGeometry WRITE setScreenGeometry
               NOTIFY screenGeometryChanged)
    Q_PROPERTY(QString barEdge READ barEdge WRITE setBarEdge NOTIFY barEdgeChanged)

public:
    PluginContextObject(PluginHostServices* services, QObject* localeNotifier,
                        QObject* themeNotifier, QObject* parent = nullptr);

    int contextVersion() const { return 1; }
    QObject* popupService() const { return m_popupService; }
    QObject* actions() const { return m_actions; }
    void setPopupService(QObject* service);
    void setActions(QObject* service);
    QString pluginId() const;
    int apiVersion() const;
    QString trustLevel() const;
    QStringList declaredPermissions() const;
    QStringList grantedPermissions() const;
    QStringList capabilities() const;
    QString activeLocale() const;
    QVariantMap colors() const;
    QVariantMap appearance() const;
    QVariantMap screenGeometry() const { return m_screenGeometry; }
    QString barEdge() const { return m_barEdge; }

    void setScreenGeometry(const QVariantMap& geometry);
    void setBarEdge(const QString& edge);

    Q_INVOKABLE bool hasPermission(const QString& permission) const;
    Q_INVOKABLE bool hasCapability(const QString& capability) const;
    Q_INVOKABLE void log(const QString& level, const QString& message,
                         const QVariantMap& fields = {});
    Q_INVOKABLE QVariantMap notify(const QString& title, const QString& message);
    Q_INVOKABLE QVariantMap readText(const QString& relativePath) const;
    Q_INVOKABLE QVariantMap writeText(const QString& relativePath, const QString& text);
    Q_INVOKABLE QVariantMap removePath(const QString& relativePath);
    Q_INVOKABLE QVariantMap list(const QString& relativeDirectory = {}) const;
    Q_INVOKABLE QVariantMap readSecret(const QString& key) const;
    Q_INVOKABLE QVariantMap writeSecret(const QString& key, const QString& value);
    Q_INVOKABLE QVariantMap removeSecret(const QString& key);
    Q_INVOKABLE QVariantMap openSettings();
    Q_INVOKABLE QVariantMap openUrl(const QString& url);
    Q_INVOKABLE QVariantMap openFile(const QString& relativePath);
    Q_INVOKABLE void notifyBarPressed();

Q_SIGNALS:
    void servicesChanged();
    void localeChanged();
    void themeChanged();
    void screenGeometryChanged();
    void barEdgeChanged();
    void barPressed();

private:
    static QVariantMap resultMap(const xuanhub::HostServiceResult& result);

    PluginHostServices* m_services = nullptr;
    QPointer<QObject> m_themeNotifier;
    QPointer<QObject> m_popupService;
    QPointer<QObject> m_actions;
    QVariantMap m_screenGeometry;
    QString m_barEdge = QStringLiteral("top");
};
