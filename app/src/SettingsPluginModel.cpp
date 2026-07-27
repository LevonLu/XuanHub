// SettingsPluginModel.cpp
#include "SettingsPluginModel.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QtMath>

namespace {
QJsonObject readJsonFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
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
                *why = QStringLiteral("应为 %1-%2 的整数").arg(lo).arg(hi);
                return false;
            }
        }
        bool ok = false;
        const int n = v.toInt(&ok);
        if (!ok || n < lo || n > hi) {
            *why = QStringLiteral("应为 %1-%2 的整数").arg(lo).arg(hi);
            return false;
        }
    } else if (type == QStringLiteral("double")) {
        bool ok = false;
        const double d = v.toDouble(&ok);
        const double lo = f.value(QStringLiteral("min")).toDouble(-1e300);
        const double hi = f.value(QStringLiteral("max")).toDouble(1e300);
        if (!ok || d < lo || d > hi) {
            *why = QStringLiteral("应为 %1-%2 的数字").arg(lo).arg(hi);
            return false;
        }
    } else if (type == QStringLiteral("bool")) {
        if (v.metaType().id() != QMetaType::Bool) {
            *why = QStringLiteral("应为布尔值");
            return false;
        }
    } else if (type == QStringLiteral("color")) {
        const QString s = v.toString();
        if (!s.isEmpty() && !QColor::isValidColorName(s)) {   // 空串合法（= 插件内默认）
            *why = QStringLiteral("不是合法颜色");
            return false;
        }
    } else if (type == QStringLiteral("enum")) {
        const QString s = v.toString();
        const auto opts = f.value(QStringLiteral("options")).toArray();
        bool found = false;
        for (const auto& o : opts) {
            if (o.toString() == s) { found = true; break; }
        }
        if (!found) {
            *why = QStringLiteral("不在可选值内");
            return false;
        }
    }
    // string/json：任意结构化值皆可
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
        case NameRole:       return r.name;
        case VersionRole:    return r.version;
        case AuthorRole:     return r.author;
        case KindRole:       return r.kind;
        case EnabledRole:    return r.enabled;
        case PositionRole:   return r.position;
        case OrderRole:      return r.order;
        case WidthRole:      return r.width;
        case ConfigRole:     return r.config;
        case SchemaRole:     return r.schema.toVariantList();
        case SettingsQmlRole:
            return r.settingsQml.isEmpty() ? QVariant() : QVariant(r.settingsQml);
        case DiscoveredRole: return r.discovered;
    }
    return {};
}

bool SettingsPluginModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    auto& r = m_rows[index.row()];
    switch (role) {
        case EnabledRole:    r.enabled = value.toBool(); break;
        case PositionRole:   r.position = value.toString(); break;
        case OrderRole:      r.order = value.toInt(); break;
        case WidthRole:      r.width = value.toInt(); break;
        case SetConfigValueRole: {
            // value 形如 {key: "...", value: ...}，写 config 草稿单字段
            const QVariantMap m = value.toMap();
            r.config.insert(m.value(QStringLiteral("key")).toString(),
                            m.value(QStringLiteral("value")));
            emit dataChanged(index, index, {ConfigRole});
            return true;
        }
        default: return false;
    }
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
    };
}

void SettingsPluginModel::reload(const QList<PluginConfigEntry>& entries,
                                 const QJsonArray& rawEntries, const QString& exeDir) {
    beginResetModel();
    m_rows.clear();

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
        r.enabled = true;
        r.position = e.position.isEmpty() ? QStringLiteral("right") : e.position;
        r.order = e.order;
        r.width = e.width;
        r.config = e.config;   // 结构化草稿直接入行（未知键随之保留）
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
}

void SettingsPluginModel::mergeDiscovered(const QJsonObject& meta, const QString& kind,
                                          const QString& dir) {
    const QString id = meta.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == QStringLiteral("pybridge")) return;

    // sidecar 声明的设置 schema 与自绘设置页（均可空）
    const QJsonArray schema = meta.value(QStringLiteral("settings")).toArray();
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
        m_rows[i].config.insert(key, value);
        const QModelIndex mi = index(i);
        emit dataChanged(mi, mi, {ConfigRole});
        return true;
    }
    return false;
}

bool SettingsPluginModel::toPluginEntriesJson(QJsonArray* out, QString* err) const {
    QJsonArray arr;
    bool anyPython = false;
    for (const auto& r : m_rows) {
        if (!r.enabled) continue;
        if (r.position != QStringLiteral("left") && r.position != QStringLiteral("center")
            && r.position != QStringLiteral("right")) {
            *err = r.id + QStringLiteral("：position 非法（left/center/right）");
            return false;
        }
        if (r.order < 0) {
            *err = r.id + QStringLiteral("：order 不能为负");
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
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("position")] = r.position;
        o[QStringLiteral("order")] = r.order;
        o[QStringLiteral("width")] = r.width;   // 0=自适应，>0 固定像素宽
        o[QStringLiteral("config")] = QJsonObject::fromVariantMap(r.config);
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
    *out = arr;
    return true;
}
