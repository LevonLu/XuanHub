// PluginActionRegistry.cpp — 动作本地化、动态状态与受控调用
#include "PluginActionRegistry.h"

#include "LocaleManager.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QMetaMethod>

PluginActionRegistry::PluginActionRegistry(PluginManager* manager,
                                           LocaleManager* locale,
                                           QObject* parent)
    : QObject(parent), m_manager(manager), m_locale(locale) {
    if (m_manager) {
        connect(m_manager, &PluginManager::pluginsChanged,
                this, &PluginActionRegistry::syncPlugins);
    }
    if (m_locale) {
        // LocaleManager 已完成翻译器切换后才发 localeChanged，此处只需让现有
        // QML/原生菜单重新查询标签，不销毁任何插件实例。
        connect(m_locale, SIGNAL(localeChanged()), this, SLOT(refresh()));
    }
    syncPlugins();
}

QVariantMap PluginActionRegistry::actionState(QObject* viewModel,
                                              const QString& actionId) const {
    if (!viewModel) return {};
    QVariantMap states = viewModel->property("actionStates").toMap();
    if (states.isEmpty()) {
        states = viewModel->property("values").toMap()
                     .value(QStringLiteral("actionStates")).toMap();
    }
    return states.value(actionId).toMap();
}

QVariantList PluginActionRegistry::actions(const QString& placement) const {
    QVariantList result;
    if (!m_manager) return result;
    for (const auto& plugin : m_manager->loaded()) {
        for (const auto& action : plugin.actions) {
            if (!action.placements.contains(placement)) continue;
            const QByteArray context = QStringLiteral("plugin.%1.actions")
                                           .arg(plugin.id).toUtf8();
            const QByteArray source = action.label.toUtf8();
            const QString label = QCoreApplication::translate(
                context.constData(), source.constData());
            const QVariantMap state = actionState(plugin.viewModel, action.id);
            result.append(QVariantMap{
                {QStringLiteral("pluginId"), plugin.id},
                {QStringLiteral("actionId"), action.id},
                {QStringLiteral("label"), label},
                {QStringLiteral("icon"), action.icon},
                {QStringLiteral("defaultShortcut"), action.defaultShortcut},
                {QStringLiteral("checkable"), action.checkable},
                {QStringLiteral("enabled"),
                 state.value(QStringLiteral("enabled"), true).toBool()},
                {QStringLiteral("checked"),
                 state.value(QStringLiteral("checked"), false).toBool()},
            });
        }
    }
    return result;
}

QVariantMap PluginActionRegistry::invoke(const QString& pluginId,
                                         const QString& actionId) {
    if (!m_manager) {
        return {
            {QStringLiteral("accepted"), false},
            {QStringLiteral("pending"), false},
            {QStringLiteral("errorCode"), QStringLiteral("registry_unavailable")},
            {QStringLiteral("errorMessage"), QStringLiteral("action registry is unavailable")},
        };
    }
    return m_manager->invokeAction(pluginId, actionId);
}

void PluginActionRegistry::connectStateSignal(QObject* viewModel,
                                              const char* signature) {
    if (!viewModel) return;
    const int signalIndex = viewModel->metaObject()->indexOfSignal(signature);
    const int slotIndex = metaObject()->indexOfSlot("refresh()");
    if (signalIndex < 0 || slotIndex < 0) return;
    QObject::connect(viewModel, viewModel->metaObject()->method(signalIndex),
                     this, metaObject()->method(slotIndex), Qt::UniqueConnection);
}

void PluginActionRegistry::syncPlugins() {
    if (m_manager) {
        for (const auto& plugin : m_manager->loaded()) {
            // Python 代理通过 valuesChanged 暴露 actionStates；原生插件也可选择
            // 独立 actionStates 属性和通知信号，两种形式均保持热更新。
            connectStateSignal(plugin.viewModel, "valuesChanged()");
            connectStateSignal(plugin.viewModel, "actionStatesChanged()");
        }
    }
    refresh();
}

void PluginActionRegistry::refresh() {
    ++m_revision;
    Q_EMIT revisionChanged();
    Q_EMIT actionsChanged();
}
