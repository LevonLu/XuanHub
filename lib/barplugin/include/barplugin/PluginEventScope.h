// PluginEventScope.h — 插件拥有的 EventBus 订阅作用域
#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "barplugin/barplugin_export.h"
#include "barplugin/EventBus.h"

class QObject;

namespace xuanhub {

class EventBus;

class BARPLUGIN_EXPORT PluginEventScope final {
public:
    explicit PluginEventScope(EventBus* bus, const QString& pluginId = {},
                              const QStringList& permissions = {});
    ~PluginEventScope();

    PluginEventScope(const PluginEventScope&) = delete;
    PluginEventScope& operator=(const PluginEventScope&) = delete;

    bool subscribe(const QString& topic, QObject* receiver, const char* slot);
    bool subscribeV2(const QString& topic, QObject* receiver, const char* slot,
                     EventSubscriptionOptions options = {});
    bool publish(const QString& topic, const QVariantMap& data = {});
    bool setState(const QString& topic, const QVariantMap& data = {},
                  EventStateOptions options = {});
    QVariantMap state(const QString& topic) const;
    QString request(const QString& topic, const QVariantMap& data,
                    QObject* receiver, const char* resultSlot,
                    int timeoutMs = 15000);
    bool respond(const QString& requestId, const QVariantMap& data = {},
                 const QString& errorCode = {}, const QString& errorMessage = {});
    bool cancelRequest(const QString& requestId);
    // clear 后作用域永久关闭，拒绝新订阅；用于 shutdown 前切断跨 DLL 回调。
    void clear();
    bool active() const { return m_active; }
    QString pluginId() const { return m_pluginId; }

private:
    EventBus* m_bus = nullptr;
    quint64 m_ownerToken = 0;
    QString m_pluginId;
    bool m_active = true;
};

} // namespace xuanhub
