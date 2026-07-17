// SettingsPluginModel.cpp
#include "SettingsPluginModel.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

namespace {
QJsonObject readJsonFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
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
        case ConfigJsonRole: return r.configJson;
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
        case ConfigJsonRole: r.configJson = value.toString(); break;
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
        { IdRole, "id" },               { NameRole, "name" },
        { VersionRole, "version" },     { AuthorRole, "author" },
        { KindRole, "kind" },           { EnabledRole, "enabled" },
        { PositionRole, "position" },   { OrderRole, "order" },
        { ConfigJsonRole, "configJson" }, { DiscoveredRole, "discovered" },
    };
}

void SettingsPluginModel::reload(const QList<PluginConfigEntry>& entries, const QString& exeDir) {
    beginResetModel();
    m_rows.clear();

    // 1) config 条目（跳过 pybridge：基础设施总开关，导出时自动托管）
    for (const auto& e : entries) {
        if (e.id == QStringLiteral("pybridge")) continue;
        Row r;
        r.id = e.id;
        r.enabled = true;
        r.position = e.position.isEmpty() ? QStringLiteral("right") : e.position;
        r.order = e.order;
        r.configJson = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(e.config)).toJson(QJsonDocument::Compact));
        // kind 先按 id 前缀猜，磁盘发现命中后会被纠正
        r.kind = e.id.startsWith(QStringLiteral("py_")) ? QStringLiteral("python")
                                                        : QStringLiteral("dll");
        m_rows.append(r);
    }

    // 2) 磁盘发现：只读 sidecar 元数据（不加载 DLL、不起 Python 解释器）
    const QDir dllDir(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins")));
    for (const QString& sub : dllDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        mergeDiscovered(readJsonFile(dllDir.filePath(sub + QStringLiteral("/metadata.json"))),
                        QStringLiteral("dll"));
    const QDir pyDir(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins_py")));
    for (const QString& sub : pyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        mergeDiscovered(readJsonFile(pyDir.filePath(sub + QStringLiteral("/plugin.json"))),
                        QStringLiteral("python"));

    endResetModel();
}

void SettingsPluginModel::mergeDiscovered(const QJsonObject& meta, const QString& kind) {
    const QString id = meta.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || id == QStringLiteral("pybridge")) return;
    for (auto& r : m_rows) {
        if (r.id == id) {   // config 已有：回填元数据，标记磁盘存在
            r.discovered = true;
            r.kind = kind;
            r.name = meta.value(QStringLiteral("name")).toString();
            r.version = meta.value(QStringLiteral("version")).toString();
            r.author = meta.value(QStringLiteral("author")).toString();
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
    m_rows.append(r);
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
        QJsonParseError perr{};
        const auto doc = QJsonDocument::fromJson(r.configJson.toUtf8(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            *err = r.id + QStringLiteral("：config 不是合法 JSON 对象（")
                   + perr.errorString() + QStringLiteral("）");
            return false;
        }
        QJsonObject o;
        o[QStringLiteral("id")] = r.id;
        o[QStringLiteral("position")] = r.position;
        o[QStringLiteral("order")] = r.order;
        o[QStringLiteral("config")] = doc.object();
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
