// PluginEventScope.cpp — 插件 EventBus 订阅的所有者清理实现
#include "barplugin/PluginEventScope.h"

#include "barplugin/EventBus.h"

namespace xuanhub {

PluginEventScope::PluginEventScope(EventBus* bus) : m_bus(bus) {
    if (m_bus) m_ownerToken = m_bus->createOwnerToken();
}

PluginEventScope::~PluginEventScope() {
    clear();
}

bool PluginEventScope::subscribe(const QString& topic, QObject* receiver, const char* slot) {
    if (!m_active || !m_bus || m_ownerToken == 0 || !receiver || !slot) return false;
    m_bus->subscribeOwned(m_ownerToken, topic, receiver, slot);
    return true;
}

void PluginEventScope::clear() {
    if (!m_active) return;
    m_active = false;
    if (m_bus && m_ownerToken != 0) m_bus->unsubscribeOwner(m_ownerToken);
}

} // namespace xuanhub
