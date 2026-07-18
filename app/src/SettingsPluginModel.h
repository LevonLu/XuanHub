// SettingsPluginModel.h — 设置窗口的插件全集模型（M8）
// 行 = config 条目 ∪ 磁盘发现（plugins/*/metadata.json + plugins_py/*/plugin.json，
// 只读 sidecar 文件、零副作用）；pybridge 基础设施条目不显示，
// 导出时按 py_* 启用情况自动托管其条目。
// M10：config 草稿结构化（QVariantMap，未知键随之保留）；sidecar 的 settings
//（分组字段 schema）与 settingsQml（自绘设置页）随磁盘发现入行，
// 供右侧「类型化表单 / 插件自绘页」两种设置区消费。
#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include "ConfigService.h"   // PluginConfigEntry

class SettingsPluginModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, VersionRole, AuthorRole, KindRole,
        EnabledRole, PositionRole, OrderRole,
        ConfigRole,          // 读：QVariantMap，结构化 config 草稿（含 schema 未声明的键）
        SchemaRole,          // 读：QVariantList，sidecar 的 settings 分组声明（可空）
        SettingsQmlRole,     // 读：QUrl，sidecar 的 settingsQml 绝对路径（可空 → QVariant()）
        SetConfigValueRole,  // 写：value 为 {key, value}，写 config 草稿单字段
        DiscoveredRole
    };

    explicit SettingsPluginModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 重建：config 条目 ∪ exeDir 下磁盘发现
    void reload(const QList<PluginConfigEntry>& entries, const QString& exeDir);

    // 按 id 读写 config 草稿单字段（SettingsPluginProxy 用，绕过 QModelIndex）
    QVariant configValue(const QString& id, const QString& key) const;
    bool setConfigValue(const QString& id, const QString& key, const QVariant& value);

    // 导出保存用 plugins 数组（含 position/order/schema 字段校验与 pybridge 托管）；
    // 校验失败 err 带原因、返回 false
    bool toPluginEntriesJson(QJsonArray* out, QString* err) const;

private:
    struct Row {
        QString id, name, version, author, kind;   // kind: "dll" | "python"
        bool enabled = false;
        bool discovered = false;   // 磁盘存在；config 有而磁盘缺失 → false（界面标"缺失"）
        QString position = QStringLiteral("right");
        int order = 0;
        QVariantMap config;        // 结构化 config 草稿（全量，未知键随之保留）
        QJsonArray schema;         // sidecar "settings"（可空）
        QUrl settingsQml;          // sidecar "settingsQml"（可空）
    };
    // dir = 该插件 sidecar 所在目录（settingsQml 相对路径据此解析为绝对 URL）
    void mergeDiscovered(const QJsonObject& meta, const QString& kind, const QString& dir);

    QVector<Row> m_rows;
};
