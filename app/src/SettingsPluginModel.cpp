// SettingsPluginModel.cpp
#include "SettingsPluginModel.h"
#include "ConfigSecretProcessor.h"
#include "SecretStore.h"
#include "barplugin/HostServices.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>

namespace {
QString pluginTranslation(const QString& pluginId, const QString& section,
                          const QString& source) {
    if (source.isEmpty()) return source;
    const QByteArray context = QStringLiteral("plugin.%1.%2")
                                   .arg(pluginId, section).toUtf8();
    const QByteArray sourceUtf8 = source.toUtf8();
    return QCoreApplication::translate(context.constData(), sourceUtf8.constData());
}

QString settingsError(const char* source) {
    return QCoreApplication::translate("xuanhub.settings.errors", source);
}

QJsonArray localizedSchema(const QString& pluginId, const QJsonArray& schema) {
    QJsonArray result;
    for (const QJsonValue& groupValue : schema) {
        QJsonObject group = groupValue.toObject();
        if (group.contains(QStringLiteral("group"))) {
            group[QStringLiteral("group")] = pluginTranslation(
                pluginId, QStringLiteral("settings"),
                group.value(QStringLiteral("group")).toString());
        }
        QJsonArray fields;
        for (const QJsonValue& fieldValue : group.value(QStringLiteral("fields")).toArray()) {
            QJsonObject field = fieldValue.toObject();
            for (const QString& key : {QStringLiteral("label"),
                                       QStringLiteral("description"),
                                       QStringLiteral("placeholder")}) {
                if (field.contains(key)) {
                    field[key] = pluginTranslation(pluginId, QStringLiteral("settings"),
                                                   field.value(key).toString());
                }
            }

            // 对外统一为 {value,label}：旧字符串数组仍保存原始稳定值，只翻译显示标签。
            if (field.value(QStringLiteral("type")).toString() == QStringLiteral("enum")) {
                QJsonArray options;
                for (const QJsonValue& optionValue : field.value(QStringLiteral("options")).toArray()) {
                    QJsonObject option;
                    if (optionValue.isObject()) {
                        option = optionValue.toObject();
                    } else {
                        option[QStringLiteral("value")] = optionValue;
                        option[QStringLiteral("label")] = optionValue.toString();
                    }
                    const QString label = option.value(QStringLiteral("label"))
                                              .toString(option.value(QStringLiteral("value")).toString());
                    option[QStringLiteral("label")] = pluginTranslation(
                        pluginId, QStringLiteral("settings"), label);
                    options.append(option);
                }
                field[QStringLiteral("options")] = options;
            }
            fields.append(field);
        }
        group[QStringLiteral("fields")] = fields;
        result.append(group);
    }
    return result;
}

QJsonObject readJsonFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QVariantMap migrateBuiltInConfig(const QString& pluginId, QVariantMap config) {
    // 内置插件旧版曾把中文显示标签直接写进配置；设置草稿加载时归一化为稳定值。
    if (pluginId == QLatin1String("py_countdown")) {
        const QString mode = config.value(QStringLiteral("mode")).toString();
        if (mode == QStringLiteral("每日重复")) config[QStringLiteral("mode")] = QStringLiteral("daily");
        else if (mode == QStringLiteral("指定日期") || mode == QStringLiteral("once"))
            config[QStringLiteral("mode")] = QStringLiteral("date");
    } else if (pluginId == QLatin1String("py_todo")) {
        const QString mode = config.value(QStringLiteral("barMode")).toString();
        if (mode == QStringLiteral("数量")) config[QStringLiteral("barMode")] = QStringLiteral("count");
        else if (mode == QStringLiteral("最近一项"))
            config[QStringLiteral("barMode")] = QStringLiteral("latest");
    }
    return config;
}

// 导出时的 schema 字段校验（双保险；QML 控件是第一道）。失败原因写入 why。
bool validateField(const QJsonObject& f, const QVariant& v, QString* why) {
    const QString type = f.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("int")) {
        const int lo = f.value(QStringLiteral("min")).toInt(INT_MIN);
        const int hi = f.value(QStringLiteral("max")).toInt(INT_MAX);
        // double 经 toInt 会静默截断且 ok=true（1.9→1），非整数 double 直接判非法；
        // 字符串数字（如 "12"）维持 toInt 解析可过
        if (v.metaType().id() == QMetaType::Double) {
            const double dv = v.toDouble();
            if (qFloor(dv) != dv) {
                *why = settingsError("应为 %1-%2 的整数").arg(lo).arg(hi);
                return false;
            }
        }
        bool ok = false;
        const int n = v.toInt(&ok);
        if (!ok || n < lo || n > hi) {
            *why = settingsError("应为 %1-%2 的整数").arg(lo).arg(hi);
            return false;
        }
    } else if (type == QStringLiteral("double")) {
        bool ok = false;
        const double d = v.toDouble(&ok);
        const double lo = f.value(QStringLiteral("min")).toDouble(-1e300);
        const double hi = f.value(QStringLiteral("max")).toDouble(1e300);
        if (!ok || d < lo || d > hi) {
            *why = settingsError("应为 %1-%2 的数字").arg(lo).arg(hi);
            return false;
        }
    } else if (type == QStringLiteral("bool")) {
        if (v.metaType().id() != QMetaType::Bool) {
            *why = settingsError("应为布尔值");
            return false;
        }
    } else if (type == QStringLiteral("color")) {
        const QString s = v.toString();
        if (!s.isEmpty() && !QColor::isValidColorName(s)) {   // 空串合法（= 插件内默认）
            *why = settingsError("不是合法颜色");
            return false;
        }
    } else if (type == QStringLiteral("enum")) {
        const QString s = v.toString();
        const auto opts = f.value(QStringLiteral("options")).toArray();
        bool found = false;
        for (const auto& o : opts) {
            const QString value = o.isObject()
                ? o.toObject().value(QStringLiteral("value")).toString()
                : o.toString();
            if (value == s) { found = true; break; }
        }
        if (!found) {
            *why = settingsError("不在可选值内");
            return false;
        }
    } else if (type == QStringLiteral("string")) {
        if (v.metaType().id() != QMetaType::QString) {
            *why = settingsError("应为文本");
            return false;
        }
        const QString pattern = f.value(QStringLiteral("regex")).toString();
        if (!pattern.isEmpty()) {
            const QRegularExpression expression(pattern);
            if (!expression.isValid()) {
                *why = settingsError("插件声明的正则表达式无效");
                return false;
            }
            if (!expression.match(v.toString()).hasMatch()) {
                *why = settingsError("格式不符合要求");
                return false;
            }
        }
    } else if (type == QStringLiteral("object")) {
        if (v.metaType().id() != QMetaType::QVariantMap
            && v.metaType().id() != QMetaType::QVariantHash) {
            *why = settingsError("应为 JSON 对象");
            return false;
        }
    } else if (type == QStringLiteral("array")) {
        if (v.metaType().id() != QMetaType::QVariantList
            && v.metaType().id() != QMetaType::QStringList) {
            *why = settingsError("应为 JSON 数组");
            return false;
        }
    }
    // json：任意可由 QVariant 表示的 JSON 值皆可。
    return true;
}

} // namespace

SettingsPluginModel::SettingsPluginModel(QObject* parent) : QAbstractListModel(parent) {}

int SettingsPluginModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant SettingsPluginModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto& r = m_rows.at(index.row());
    switch (role) {
        case IdRole:         return r.id;
        case NameRole:       return pluginTranslation(
            r.id, QStringLiteral("metadata"), r.name);
        case VersionRole:    return r.version;
        case AuthorRole:     return r.author;
        case KindRole:       return r.kind;
        case EnabledRole:    return r.enabled;
        case PositionRole:   return r.position;
        case OrderRole:      return r.order;
        case WidthRole:      return r.width;
        case ConfigRole:     return r.config;
        case SchemaRole:     return localizedSchema(r.id, r.schema).toVariantList();
        case SettingsQmlRole:
            return r.settingsQml.isEmpty() ? QVariant() : QVariant(r.settingsQml);
        case DiscoveredRole: return r.discovered;
        case GroupKeyRole:
            return (r.enabled ? QStringLiteral("enabled-") : QStringLiteral("disabled-"))
                   + r.position;
        case ApiVersionRole:             return r.apiVersion;
        case CapabilitiesRole:           return r.capabilities;
        case DeclaredPermissionsRole:    return r.declaredPermissions;
        case GrantedPermissionsRole:     return r.grantedPermissions;
        case TrustLevelRole:             return r.trustLevel;
        case ConfigVersionRole:          return r.configVersion;
        case TargetConfigVersionRole:    return r.targetConfigVersion;
        case MigrationAvailableRole:     return r.migrationReady;
        case MigrationErrorRole:         return r.migrationError;
    }
    return {};
}

void SettingsPluginModel::retranslate() {
    if (m_rows.isEmpty()) return;
    emit dataChanged(index(0), index(m_rows.size() - 1), {NameRole, SchemaRole});
}

bool SettingsPluginModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    auto& r = m_rows[index.row()];
    switch (role) {
        case EnabledRole:
            if (r.enabled == value.toBool()) return true;
            r.enabled = value.toBool();
            break;
        case PositionRole:
            if (r.position == value.toString()) return true;
            r.position = value.toString();
            break;
        case OrderRole:
            if (r.order == value.toInt()) return true;
            r.order = value.toInt();
            break;
        case WidthRole:
            if (r.width == value.toInt()) return true;
            r.width = value.toInt();
            break;
        case SetConfigValueRole: {
            // value 形如 {key: "...", value: ...}，写 config 草稿单字段
            const QVariantMap m = value.toMap();
            const QString key = m.value(QStringLiteral("key")).toString();
            const QVariant newValue = m.value(QStringLiteral("value"));
            if (key.isEmpty()) return false;
            if (r.migrationReady) {
                r.config = r.migrationConfig;
                r.configVersion = r.targetConfigVersion;
                r.migrationReady = false;
                m_dirty = true;
                emit dataChanged(index, index,
                                 {ConfigRole, ConfigVersionRole,
                                  MigrationAvailableRole});
            }
            if (r.config.contains(key) && r.config.value(key) == newValue) return true;
            r.config.insert(key, newValue);
            m_dirty = true;
            emit dataChanged(index, index, {ConfigRole});
            return true;
        }
        default: return false;
    }
    m_dirty = true;
    // 启用状态与区域会改变 QML ListView 的分组键，必须连同派生 role 一并通知，
    // 否则代理排序虽更新，分隔标题仍可能保留旧状态。
    if (role == EnabledRole || role == PositionRole)
        emit dataChanged(index, index, {role, GroupKeyRole});
    else
        emit dataChanged(index, index, {role});
    return true;
}

Qt::ItemFlags SettingsPluginModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> SettingsPluginModel::roleNames() const {
    return {
        { IdRole, "id" },                 { NameRole, "name" },
        { VersionRole, "version" },       { AuthorRole, "author" },
        { KindRole, "kind" },             { EnabledRole, "enabled" },
        { PositionRole, "position" },     { OrderRole, "order" },
        { WidthRole, "width" },
        { ConfigRole, "config" },         { SchemaRole, "schema" },
        { SettingsQmlRole, "settingsQml" },
        { SetConfigValueRole, "setConfigValue" },
        { DiscoveredRole, "discovered" },
        { GroupKeyRole, "groupKey" },
        { ApiVersionRole, "apiVersion" },
        { CapabilitiesRole, "capabilities" },
        { DeclaredPermissionsRole, "declaredPermissions" },
        { GrantedPermissionsRole, "grantedPermissions" },
        { TrustLevelRole, "trustLevel" },
        { ConfigVersionRole, "configVersion" },
        { TargetConfigVersionRole, "targetConfigVersion" },
        { MigrationAvailableRole, "migrationAvailable" },
        { MigrationErrorRole, "migrationError" },
    };
}

void SettingsPluginModel::reload(const QList<PluginConfigEntry>& entries,
                                  const QJsonArray& rawEntries, const QString& exeDir,
                                  const QString& configPath) {
    beginResetModel();
    m_rows.clear();
    m_secretDir = QFileInfo(configPath).absolutePath();

    // 原始条目按 id 索引：导出时以其为底只覆盖已知键，手写未知键（如 "note"）不丢
    QHash<QString, QJsonObject> rawById;
    for (const auto& v : rawEntries) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) rawById.insert(id, o);
    }

    // 1) config 条目（跳过 pybridge：基础设施总开关，导出时自动托管）
    for (const auto& e : entries) {
        if (e.id == QStringLiteral("pybridge")) continue;
        Row r;
        r.id = e.id;
        r.configVersion = e.configVersion;
        r.targetConfigVersion = e.configVersion;
        r.enabled = true;
        r.position = e.position.isEmpty() ? QStringLiteral("right") : e.position;
        r.order = e.order;
        r.width = e.width;
        r.config = migrateBuiltInConfig(e.id, e.config); // 未知键保留，旧中文枚举转稳定值
        r.raw = rawById.value(e.id);
        // kind 先按 id 前缀猜，磁盘发现命中后会被纠正
        r.kind = e.id.startsWith(QStringLiteral("py_")) ? QStringLiteral("python")
                                                        : QStringLiteral("dll");
        m_rows.append(r);
    }

    // 2) 磁盘发现：只读 sidecar 元数据（不加载 DLL、不起 Python 解释器）
    const QDir dllDir(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins")));
    for (const QString& sub : dllDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        mergeDiscovered(readJsonFile(dllDir.filePath(sub + QStringLiteral("/metadata.json"))),
                        QStringLiteral("dll"), dllDir.absoluteFilePath(sub));
    const QDir pyDir(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins_py")));
    for (const QString& sub : pyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        mergeDiscovered(readJsonFile(pyDir.filePath(sub + QStringLiteral("/plugin.json"))),
                        QStringLiteral("python"), pyDir.absoluteFilePath(sub));

    endResetModel();
    m_dirty = false;
}

void SettingsPluginModel::mergeDiscovered(const QJsonObject& meta, const QString& kind,
                                          const QString& dir) {
    const QString id = meta.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == QStringLiteral("pybridge")) return;

    // sidecar 声明的设置 schema 与自绘设置页（均可空）
    const QJsonArray schema = meta.value(QStringLiteral("settings")).toArray();
    const int apiVersion = meta.value(QStringLiteral("apiVersion")).toInt(1);
    const int targetConfigVersion = qMax(
        1, meta.value(QStringLiteral("configVersion")).toInt(1));
    QStringList capabilities;
    for (const QJsonValue& value : meta.value(QStringLiteral("capabilities")).toArray())
        if (value.isString()) capabilities.append(value.toString());
    if (kind == QLatin1String("python") && capabilities.isEmpty())
        capabilities.append(QStringLiteral("barItem"));
    QStringList declaredPermissions;
    for (const QJsonValue& value : meta.value(QStringLiteral("permissions")).toArray())
        if (value.isString()) declaredPermissions.append(value.toString());
    const QString trustLevel = kind == QLatin1String("python")
        ? QStringLiteral("isolated") : QStringLiteral("trusted-in-process");
    const QStringList grantedPermissions = xuanhub::grantedHostPermissions(
        trustLevel == QLatin1String("trusted-in-process")
            ? xuanhub::HostPluginTrustLevel::TrustedInProcess
            : xuanhub::HostPluginTrustLevel::Isolated,
        declaredPermissions);
    QUrl settingsQml;
    const QString rel = meta.value(QStringLiteral("settingsQml")).toString();
    if (!rel.isEmpty())
        settingsQml = QUrl::fromLocalFile(QDir(dir).absoluteFilePath(rel));

    for (auto& r : m_rows) {
        if (r.id == id) {   // config 已有：回填元数据，标记磁盘存在
            r.discovered = true;
            r.kind = kind;
            r.name = meta.value(QStringLiteral("name")).toString();
            r.version = meta.value(QStringLiteral("version")).toString();
            r.author = meta.value(QStringLiteral("author")).toString();
            r.schema = schema;
            r.settingsQml = settingsQml;
            r.apiVersion = apiVersion;
            r.targetConfigVersion = targetConfigVersion;
            r.capabilities = capabilities;
            r.declaredPermissions = declaredPermissions;
            r.grantedPermissions = grantedPermissions;
            r.trustLevel = trustLevel;
            return;
        }
    }
    Row r;   // 磁盘有、config 无：未启用，勾选即加入
    r.id = id;
    r.kind = kind;
    r.discovered = true;
    r.name = meta.value(QStringLiteral("name")).toString();
    r.version = meta.value(QStringLiteral("version")).toString();
    r.author = meta.value(QStringLiteral("author")).toString();
    r.schema = schema;
    r.settingsQml = settingsQml;
    r.apiVersion = apiVersion;
    r.configVersion = targetConfigVersion;
    r.targetConfigVersion = targetConfigVersion;
    r.capabilities = capabilities;
    r.declaredPermissions = declaredPermissions;
    r.grantedPermissions = grantedPermissions;
    r.trustLevel = trustLevel;
    m_rows.append(r);
}

QVariant SettingsPluginModel::configValue(const QString& id, const QString& key) const {
    for (const auto& r : m_rows) {
        if (r.id == id) return r.config.value(key);
    }
    return {};
}

bool SettingsPluginModel::setConfigValue(const QString& id, const QString& key,
                                         const QVariant& value) {
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].id != id) continue;
        if (m_rows[i].migrationReady) {
            m_rows[i].config = m_rows[i].migrationConfig;
            m_rows[i].configVersion = m_rows[i].targetConfigVersion;
            m_rows[i].migrationReady = false;
            m_dirty = true;
            const QModelIndex modelIndex = index(i);
            emit dataChanged(modelIndex, modelIndex,
                             {ConfigRole, ConfigVersionRole,
                              MigrationAvailableRole});
        }
        if (m_rows[i].config.contains(key) && m_rows[i].config.value(key) == value)
            return true;
        m_rows[i].config.insert(key, value);
        m_dirty = true;
        const QModelIndex mi = index(i);
        emit dataChanged(mi, mi, {ConfigRole});
        return true;
    }
    return false;
}

bool SettingsPluginModel::setMigrationSuggestion(
    const QString& id, int targetVersion, const QVariantMap& config,
    const QString& error) {
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (row.id != id) continue;
        row.targetConfigVersion = qMax(row.targetConfigVersion, targetVersion);
        row.migrationConfig = config;
        row.migrationError = error;
        row.migrationReady = error.isEmpty()
            && targetVersion > row.configVersion;
        const QModelIndex modelIndex = index(i);
        emit dataChanged(modelIndex, modelIndex,
                         {TargetConfigVersionRole, MigrationAvailableRole,
                          MigrationErrorRole});
        return true;
    }
    return false;
}

bool SettingsPluginModel::acceptMigration(const QString& id) {
    for (int i = 0; i < m_rows.size(); ++i) {
        Row& row = m_rows[i];
        if (row.id != id) continue;
        if (!row.migrationReady) return false;
        row.config = row.migrationConfig;
        row.configVersion = row.targetConfigVersion;
        row.migrationReady = false;
        m_dirty = true;
        const QModelIndex modelIndex = index(i);
        emit dataChanged(modelIndex, modelIndex,
                         {ConfigRole, ConfigVersionRole, MigrationAvailableRole,
                          MigrationErrorRole});
        return true;
    }
    return false;
}

QVector<int> SettingsPluginModel::orderedRowsForPosition(const QString& position) const {
    QVector<int> rows;
    rows.reserve(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).position == position) rows.append(i);
    }
    // 与排序代理在同一分区内的比较规则保持一致；旧配置存在相同 order 时，
    // 用 id 给出稳定顺序，用户首次点上移/下移后会被归一化为连续序号。
    std::sort(rows.begin(), rows.end(), [this](int left, int right) {
        const Row& a = m_rows.at(left);
        const Row& b = m_rows.at(right);
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return rows;
}

bool SettingsPluginModel::canMoveWithinPosition(int sourceRow, int direction) const {
    if (sourceRow < 0 || sourceRow >= m_rows.size() || (direction != -1 && direction != 1))
        return false;
    const QVector<int> rows = orderedRowsForPosition(m_rows.at(sourceRow).position);
    const int current = rows.indexOf(sourceRow);
    return current >= 0 && current + direction >= 0 && current + direction < rows.size();
}

bool SettingsPluginModel::moveWithinPosition(int sourceRow, int direction) {
    if (!canMoveWithinPosition(sourceRow, direction)) return false;
    QVector<int> rows = orderedRowsForPosition(m_rows.at(sourceRow).position);
    const int current = rows.indexOf(sourceRow);
    std::swap(rows[current], rows[current + direction]);

    // 只交换两个旧数字会在重复 order 的旧配置里留下不确定顺序；整组规范化后，
    // order 仍是兼容的磁盘格式，而界面则完全由可见的上移/下移控制。
    for (int i = 0; i < rows.size(); ++i)
        m_rows[rows[i]].order = i;
    m_dirty = true;
    emit dataChanged(index(0), index(m_rows.size() - 1), {OrderRole});
    return true;
}

bool SettingsPluginModel::toPluginEntriesJson(
    QJsonArray* out, QString* err,
    SecretStore::Transaction* secretTransaction) const {
    QJsonArray arr;
    bool anyPython = false;
    const SecretStore secrets(m_secretDir);
    auto localTransaction = secrets.beginTransaction();
    SecretStore::Transaction* transaction = secretTransaction
        ? secretTransaction : &localTransaction;
    for (const auto& r : m_rows) {
        if (!r.enabled) continue;
        if (r.position != QStringLiteral("left") && r.position != QStringLiteral("center")
            && r.position != QStringLiteral("right")) {
            *err = settingsError("%1：position 非法（left/center/right）").arg(r.id);
            return false;
        }
        if (r.order < 0) {
            *err = settingsError("%1：order 不能为负").arg(r.id);
            return false;
        }
        if (r.width < 0 || r.width > 800) {
            *err = settingsError("%1：width 需在 0-800 之间").arg(r.id);
            return false;
        }
        // schema 声明字段的导出校验（双保险；QML 控件是第一道）
        for (const auto& gv : r.schema) {
            const auto fields = gv.toObject().value(QStringLiteral("fields")).toArray();
            for (const auto& fv : fields) {
                const auto f = fv.toObject();
                const QString key = f.value(QStringLiteral("key")).toString();
                if (key.isEmpty() || !r.config.contains(key)) continue;
                QString why;
                if (!validateField(f, r.config.value(key), &why)) {
                    *err = r.id + QStringLiteral("：")
                           + f.value(QStringLiteral("label")).toString(key)
                           + QStringLiteral(" ") + why;
                    return false;
                }
            }
        }
        // 以原始条目为底（手写未知键如 "note" 保留），只覆盖框架已知键；
        // 磁盘发现行无原始对象（raw 为空），退化为全新组装
        QJsonObject o = r.raw;
        QVariantMap config = r.config;
        const QJsonObject rawConfig = r.raw.value(QStringLiteral("config")).toObject();
        // 顶层、自绘 object 和稳定 itemKey 数组统一走 Schema 遍历；插件 ID 与
        // RFC 6901 JSON Pointer 共同限定密钥身份，删除项会在同一事务清理孤儿密文。
        if (!ConfigSecretProcessor::protect(
                r.id, r.schema, &config, rawConfig, transaction, err)) return false;
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("configVersion")] = r.configVersion;
        o[QStringLiteral("position")] = r.position;
        o[QStringLiteral("order")] = r.order;
        o[QStringLiteral("width")] = r.width;   // 0=自适应，>0 固定像素宽
        o[QStringLiteral("config")] = QJsonObject::fromVariantMap(config);
        arr.append(o);
        if (r.kind == QStringLiteral("python")) anyPython = true;
    }
    // pybridge 总开关托管：有任一 Python 子插件启用则自动带上该条目
    //（其 config 段当前无内容，合成即可）；全不启用则不写，PluginManager 会整个跳过桥
    if (anyPython) {
        QJsonObject pb;
        pb[QStringLiteral("id")] = QStringLiteral("pybridge");
        pb[QStringLiteral("position")] = QStringLiteral("right");
        pb[QStringLiteral("order")] = 9;
        pb[QStringLiteral("config")] = QJsonObject();
        arr.append(pb);
    }
    if (!secretTransaction) {
        if (!localTransaction.commit()) {
            *err = settingsError("无法安全保存插件凭据");
            return false;
        }
        localTransaction.finalize();
    }
    *out = arr;
    return true;
}
