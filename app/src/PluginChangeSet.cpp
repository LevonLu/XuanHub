// PluginChangeSet.cpp — 插件配置稳定指纹与按 ID 差异分类实现
#include "PluginChangeSet.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

namespace {
QString compactHash(const QJsonValue& value) {
    const QByteArray bytes = value.isArray()
        ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
        : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject layoutObject(const PluginConfigEntry& entry) {
    return {
        {QStringLiteral("position"), entry.position},
        {QStringLiteral("order"), entry.order},
        {QStringLiteral("width"), entry.width},
    };
}

QMap<QString, PluginConfigEntry> byId(const QList<PluginConfigEntry>& entries) {
    QMap<QString, PluginConfigEntry> result;
    for (const auto& entry : entries) result.insert(entry.id, entry);
    return result;
}
} // namespace

namespace PluginConfigDiff {

QString backendFingerprint(const PluginConfigEntry& entry) {
    return compactHash(QJsonObject{
        {QStringLiteral("configVersion"), entry.configVersion},
        {QStringLiteral("config"), QJsonObject::fromVariantMap(entry.config)},
    });
}

QString layoutFingerprint(const PluginConfigEntry& entry) {
    return compactHash(layoutObject(entry));
}

QString entriesFingerprint(const QList<PluginConfigEntry>& entries) {
    QJsonArray normalized;
    const auto indexed = byId(entries);
    for (auto it = indexed.cbegin(); it != indexed.cend(); ++it) {
        normalized.append(QJsonObject{
            {QStringLiteral("id"), it.key()},
            {QStringLiteral("layout"), layoutObject(it.value())},
            {QStringLiteral("configVersion"), it.value().configVersion},
            {QStringLiteral("config"), QJsonObject::fromVariantMap(it.value().config)},
        });
    }
    return compactHash(normalized);
}

PluginChangeSet calculate(const QList<PluginConfigEntry>& before,
                          const QList<PluginConfigEntry>& after) {
    PluginChangeSet changes;
    const auto oldById = byId(before);
    const auto newById = byId(after);

    for (auto it = oldById.cbegin(); it != oldById.cend(); ++it) {
        if (!newById.contains(it.key())) changes.removed.append(it.key());
    }

    for (auto it = newById.cbegin(); it != newById.cend(); ++it) {
        const auto oldIt = oldById.constFind(it.key());
        if (oldIt == oldById.cend()) {
            changes.added.append(it.key());
            continue;
        }

        const bool layoutDiffers = layoutFingerprint(oldIt.value())
            != layoutFingerprint(it.value());
        const bool configDiffers = backendFingerprint(oldIt.value())
            != backendFingerprint(it.value());
        if (layoutDiffers) changes.layoutChanged.append(it.key());
        if (configDiffers) changes.configChanged.append(it.key());
        if (!layoutDiffers && !configDiffers) changes.unchanged.append(it.key());
    }
    return changes;
}

} // namespace PluginConfigDiff
