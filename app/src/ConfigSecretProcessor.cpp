// ConfigSecretProcessor.cpp — 通用 JSON Pointer 与稳定数组 itemKey 密钥遍历
#include "ConfigSecretProcessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

namespace {
QString secretError(const char* source) {
    return QCoreApplication::translate("xuanhub.settings.errors", source);
}

QString escapePointerSegment(QString segment) {
    segment.replace(QLatin1Char('~'), QStringLiteral("~0"));
    segment.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return segment;
}

QString childPointer(const QString& parent, const QString& segment) {
    return parent + QLatin1Char('/') + escapePointerSegment(segment);
}

QString legacyDottedId(const QString& pluginId, const QString& pointer) {
    QStringList segments = pointer.mid(1).split(QLatin1Char('/'));
    for (QString& segment : segments) {
        segment.replace(QStringLiteral("~1"), QStringLiteral("/"));
        segment.replace(QStringLiteral("~0"), QStringLiteral("~"));
    }
    return SecretStore::idForPluginField(pluginId, segments.join(QLatin1Char('.')));
}

bool hasSecretDescendant(const QJsonArray& fields) {
    for (const QJsonValue& value : fields) {
        const QJsonObject field = value.toObject();
        if (field.value(QStringLiteral("secret")).toBool()) return true;
        if (hasSecretDescendant(field.value(QStringLiteral("fields")).toArray())) return true;
        if (hasSecretDescendant(field.value(QStringLiteral("items")).toObject()
                                    .value(QStringLiteral("fields")).toArray())) return true;
    }
    return false;
}

void collectOwnedReferences(const QJsonValue& value, const QString& prefix,
                            QSet<QString>* references) {
    const QString reference = SecretStore::referenceId(value);
    if (!reference.isEmpty()) {
        if (reference.startsWith(prefix)) references->insert(reference);
        return;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            collectOwnedReferences(it.value(), prefix, references);
    } else if (value.isArray()) {
        for (const QJsonValue& item : value.toArray())
            collectOwnedReferences(item, prefix, references);
    }
}

bool protectFields(const QString& pluginId, const QJsonArray& fields,
                   QVariantMap* current, const QJsonObject& raw,
                   const QString& pointer, SecretStore::Transaction* transaction,
                   QString* error) {
    for (const QJsonValue& fieldValue : fields) {
        const QJsonObject field = fieldValue.toObject();
        const QString key = field.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) continue;
        const QString fieldPointer = childPointer(pointer, key);

        if (field.value(QStringLiteral("secret")).toBool()) {
            const QString expected = SecretStore::idForPluginPointer(pluginId, fieldPointer);
            const QString legacy = legacyDottedId(pluginId, fieldPointer);
            if (current->contains(key)) {
                const QJsonValue candidate = QJsonValue::fromVariant(current->value(key));
                const QString reference = SecretStore::referenceId(candidate);
                if (!reference.isEmpty()) {
                    if (reference != expected && reference != legacy) {
                        *error = secretError("%1：密钥引用与字段路径不匹配").arg(pluginId);
                        return false;
                    }
                } else {
                    if (current->value(key).metaType().id() != QMetaType::QString) {
                        *error = secretError("%1：%2 应为文本密钥").arg(pluginId, key);
                        return false;
                    }
                    const QString plaintext = current->value(key).toString();
                    if (!transaction->stageWrite(expected, plaintext)) {
                        *error = secretError("%1：无法安全保存 %2").arg(pluginId, key);
                        return false;
                    }
                    if (plaintext.isEmpty()) current->remove(key);
                    else current->insert(key, SecretStore::referenceValue(expected).toVariant());
                }
            } else if (const QString reference = SecretStore::referenceId(raw.value(key));
                       reference == expected || reference == legacy) {
                // 解密失败时保留原引用，避免保存其他字段时静默清掉仍可恢复的密钥。
                current->insert(key, SecretStore::referenceValue(reference).toVariant());
            }
            continue;
        }

        const QString type = field.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("object") && current->contains(key)) {
            QVariantMap object = current->value(key).toMap();
            if (!protectFields(pluginId, field.value(QStringLiteral("fields")).toArray(),
                               &object, raw.value(key).toObject(), fieldPointer,
                               transaction, error)) return false;
            current->insert(key, object);
        } else if (type == QLatin1String("array") && current->contains(key)) {
            QVariantList items = current->value(key).toList();
            const QJsonObject itemSchema = field.value(QStringLiteral("items")).toObject();
            const QJsonArray itemFields = itemSchema.value(QStringLiteral("fields")).toArray();
            if (itemFields.isEmpty()) continue;
            const QString itemKey = field.value(QStringLiteral("itemKey")).toString();
            if (itemKey.isEmpty() && hasSecretDescendant(itemFields)) {
                *error = secretError("%1：数组 %2 的嵌套密钥需要 itemKey")
                             .arg(pluginId, key);
                return false;
            }

            QHash<QString, QJsonObject> rawByKey;
            for (const QJsonValue& rawValue : raw.value(key).toArray()) {
                const QJsonObject rawItem = rawValue.toObject();
                const QString stableKey = rawItem.value(itemKey).toString();
                if (!stableKey.isEmpty() && !rawByKey.contains(stableKey))
                    rawByKey.insert(stableKey, rawItem);
            }
            QSet<QString> usedKeys;
            for (qsizetype index = 0; index < items.size(); ++index) {
                QVariantMap item = items.at(index).toMap();
                QString stableKey = item.value(itemKey).toString();
                if (stableKey.isEmpty() || usedKeys.contains(stableKey)) {
                    stableKey = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    item.insert(itemKey, stableKey);
                }
                usedKeys.insert(stableKey);
                if (!protectFields(pluginId, itemFields, &item, rawByKey.value(stableKey),
                                   childPointer(fieldPointer, stableKey),
                                   transaction, error)) return false;
                items[index] = item;
            }
            current->insert(key, items);
        }
    }
    return true;
}

QVariant scrubReferences(const QVariant& value) {
    if (!SecretStore::referenceId(QJsonValue::fromVariant(value)).isEmpty()) return {};
    if (value.metaType().id() == QMetaType::QVariantMap) {
        QVariantMap result;
        const QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            const QVariant scrubbed = scrubReferences(it.value());
            if (scrubbed.isValid()) result.insert(it.key(), scrubbed);
        }
        return result;
    }
    if (value.metaType().id() == QMetaType::QVariantList) {
        QVariantList result;
        for (const QVariant& item : value.toList()) result.append(scrubReferences(item));
        return result;
    }
    return value;
}

void hydrateFields(const QString& pluginId, const QJsonArray& fields,
                   const QJsonObject& raw, QVariantMap* current,
                   const QString& pointer, const SecretStore& secrets) {
    for (const QJsonValue& fieldValue : fields) {
        const QJsonObject field = fieldValue.toObject();
        const QString key = field.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) continue;
        const QString fieldPointer = childPointer(pointer, key);
        if (field.value(QStringLiteral("secret")).toBool()) {
            const QString reference = SecretStore::referenceId(raw.value(key));
            const QString expected = SecretStore::idForPluginPointer(pluginId, fieldPointer);
            const QString legacy = legacyDottedId(pluginId, fieldPointer);
            if (reference == expected || reference == legacy) {
                if (const auto plaintext = secrets.read(reference)) current->insert(key, *plaintext);
                else current->remove(key);
            } else if (!reference.isEmpty()) {
                current->remove(key);
            }
            continue;
        }

        const QString type = field.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("object") && current->contains(key)) {
            QVariantMap object = current->value(key).toMap();
            hydrateFields(pluginId, field.value(QStringLiteral("fields")).toArray(),
                          raw.value(key).toObject(), &object, fieldPointer, secrets);
            current->insert(key, object);
        } else if (type == QLatin1String("array") && current->contains(key)) {
            QVariantList items = current->value(key).toList();
            const QJsonArray rawItems = raw.value(key).toArray();
            const QJsonArray itemFields = field.value(QStringLiteral("items")).toObject()
                                              .value(QStringLiteral("fields")).toArray();
            const QString itemKey = field.value(QStringLiteral("itemKey")).toString();
            for (qsizetype index = 0; index < items.size(); ++index) {
                QVariantMap item = items.at(index).toMap();
                const QJsonObject rawItem = index < rawItems.size()
                    ? rawItems.at(index).toObject() : QJsonObject{};
                const QString stableKey = rawItem.value(itemKey).toString();
                if (stableKey.isEmpty() || item.value(itemKey).toString() != stableKey) {
                    item = scrubReferences(item).toMap();
                } else {
                    hydrateFields(pluginId, itemFields, rawItem, &item,
                                  childPointer(fieldPointer, stableKey), secrets);
                }
                items[index] = item;
            }
            current->insert(key, items);
        }
    }
}

QJsonObject readSidecar(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        ? document.object() : QJsonObject{};
}
} // namespace

bool ConfigSecretProcessor::protect(
    const QString& pluginId, const QJsonArray& schema, QVariantMap* config,
    const QJsonObject& rawConfig, SecretStore::Transaction* transaction,
    QString* error) {
    if (!config || !transaction) return false;
    for (const QJsonValue& group : schema) {
        if (!protectFields(pluginId, group.toObject().value(QStringLiteral("fields")).toArray(),
                           config, rawConfig, {}, transaction, error)) return false;
    }

    const QString prefix = QStringLiteral("plugin/") + pluginId + QLatin1Char('/');
    QSet<QString> oldReferences;
    QSet<QString> activeReferences;
    collectOwnedReferences(rawConfig, prefix, &oldReferences);
    collectOwnedReferences(QJsonObject::fromVariantMap(*config), prefix, &activeReferences);
    for (const QString& orphan : oldReferences - activeReferences) {
        if (!transaction->stageWrite(orphan, {})) {
            *error = secretError("%1：无法清除孤儿凭据").arg(pluginId);
            return false;
        }
    }
    return true;
}

void ConfigSecretProcessor::hydrate(
    const QString& pluginId, const QJsonArray& schema, const QJsonObject& rawConfig,
    QVariantMap* config, const SecretStore& secrets) {
    if (!config) return;
    *config = scrubReferences(*config).toMap();
    for (const QJsonValue& group : schema) {
        hydrateFields(pluginId, group.toObject().value(QStringLiteral("fields")).toArray(),
                      rawConfig, config, {}, secrets);
    }
}

QJsonArray ConfigSecretProcessor::loadSchema(const QString& exeDir,
                                             const QString& pluginId) {
    const QStringList roots{QDir(exeDir).filePath(QStringLiteral("plugins")),
                            QDir(exeDir).filePath(QStringLiteral("plugins_py"))};
    for (const QString& root : roots) {
        QDir directory(root);
        const QStringList names = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& name : names) {
            const QString fileName = root.endsWith(QStringLiteral("plugins_py"))
                ? QStringLiteral("plugin.json") : QStringLiteral("metadata.json");
            const QJsonObject sidecar = readSidecar(directory.filePath(name + QLatin1Char('/')
                                                                        + fileName));
            if (sidecar.value(QStringLiteral("id")).toString() == pluginId)
                return sidecar.value(QStringLiteral("settings")).toArray();
        }
    }
    return {};
}
