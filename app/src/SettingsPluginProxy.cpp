// SettingsPluginProxy.cpp
#include "SettingsPluginProxy.h"
#include "SettingsPluginModel.h"

#include <QJSValue>
#include <QMetaType>

SettingsPluginProxy::SettingsPluginProxy(SettingsPluginModel* model, QObject* parent)
    : QObject(parent), m_model(model) {}

void SettingsPluginProxy::setPluginId(const QString& id) {
    if (m_pluginId == id) return;
    m_pluginId = id;
    emit pluginIdChanged();
}

QVariant SettingsPluginProxy::value(const QString& key) const {
    if (!m_model || m_pluginId.isEmpty()) return {};
    return m_model->configValue(m_pluginId, key);
}

void SettingsPluginProxy::setValue(const QString& key, const QVariant& v) {
    if (!m_model || m_pluginId.isEmpty()) return;
    // QML 传来的 JS 数组/对象可能是 QJSValue 包装（未深转），直接存草稿会在
    // 导出 JSON 时变成 null（launcher items 实测踩过）——先深转为 QVariantList/Map
    QVariant nv = v;
    if (nv.userType() == qMetaTypeId<QJSValue>())
        nv = nv.value<QJSValue>().toVariant();
    m_model->setConfigValue(m_pluginId, key, nv);
}
