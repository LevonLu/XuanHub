// PluginActionRegistry.h — 汇总已加载插件的声明式动作并提供统一 QML/托盘入口
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class LocaleManager;
class PluginManager;

class PluginActionRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
public:
    explicit PluginActionRegistry(PluginManager* manager, LocaleManager* locale,
                                  QObject* parent = nullptr);

    int revision() const { return m_revision; }
    Q_INVOKABLE QVariantList actions(const QString& placement) const;
    Q_INVOKABLE QVariantMap invoke(const QString& pluginId,
                                   const QString& actionId);

Q_SIGNALS:
    void actionsChanged();
    void revisionChanged();

private Q_SLOTS:
    void syncPlugins();
    void refresh();

private:
    QVariantMap actionState(QObject* viewModel, const QString& actionId) const;
    void connectStateSignal(QObject* viewModel, const char* signature);

    PluginManager* m_manager = nullptr;
    LocaleManager* m_locale = nullptr;
    int m_revision = 0;
};
