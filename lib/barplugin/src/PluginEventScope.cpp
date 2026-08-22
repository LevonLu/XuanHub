// PluginEventScope.cpp — 插件 EventBus 订阅的所有者清理实现
#include "barplugin/PluginEventScope.h"

#include "barplugin/EventBus.h"

namespace xuanhub {

PluginEventScope::PluginEventScope(EventBus* bus, const QString& pluginId,
                                   const QStringList& permissions)
    : m_bus(bus), m_pluginId(pluginId) {
    if (m_bus) m_ownerToken = m_bus->createOwnerToken(pluginId, permissions);
}

PluginEventScope::~PluginEventScope() {
    clear();
}

bool PluginEventScope::subscribe(const QString& topic, QObject* receiver, const char* slot) {
    if (!m_active || !m_bus || m_ownerToken == 0 || !receiver || !slot) return false;
    m_bus->subscribeOwned(m_ownerToken, topic, receiver, slot);
    return true;
}

bool PluginEventScope::subscribeV2(const QString& topic, QObject* receiver,
                                   const char* slot, EventSubscriptionOptions options) {
    if (!m_active || !m_bus || m_ownerToken == 0 || !receiver || !slot) return false;
    return m_bus->subscribeV2Owned(m_ownerToken, topic, receiver, slot, options);
}

bool PluginEventScope::publish(const QString& topic, const QVariantMap& data) {
    return m_active && m_bus && m_ownerToken != 0
        && m_bus->publishV2Owned(m_ownerToken, topic, data);
}

bool PluginEventScope::setState(const QString& topic, const QVariantMap& data,
                                EventStateOptions options) {
    return m_active && m_bus && m_ownerToken != 0
        && m_bus->setStateOwned(m_ownerToken, topic, data, options);
}

QVariantMap PluginEventScope::state(const QString& topic) const {
    return m_active && m_bus ? m_bus->state(topic) : QVariantMap{};
}

QString PluginEventScope::request(const QString& topic, const QVariantMap& data,
                                  QObject* receiver, const char* resultSlot,
                                  int timeoutMs) {
    if (!m_active || !m_bus || m_ownerToken == 0) return {};
    return m_bus->requestOwned(m_ownerToken, topic, data, receiver, resultSlot,
                               timeoutMs);
}

bool PluginEventScope::respond(const QString& requestId, const QVariantMap& data,
                               const QString& errorCode,
                               const QString& errorMessage) {
    return m_active && m_bus && m_ownerToken != 0
        && m_bus->respondOwned(m_ownerToken, requestId, data,
                               errorCode, errorMessage);
}

bool PluginEventScope::cancelRequest(const QString& requestId) {
    return m_active && m_bus && m_ownerToken != 0
        && m_bus->cancelRequestOwned(m_ownerToken, requestId);
}

void PluginEventScope::clear() {
    if (!m_active) return;
    m_active = false;
    if (m_bus && m_ownerToken != 0) m_bus->unsubscribeOwner(m_ownerToken);
}

} // namespace xuanhub
