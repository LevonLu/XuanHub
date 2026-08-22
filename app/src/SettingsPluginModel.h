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
        EnabledRole, PositionRole, OrderRole, WidthRole,
        ConfigRole,          // 读：QVariantMap，结构化 config 草稿（含 schema 未声明的键）
        SchemaRole,          // 读：QVariantList，sidecar 的 settings 分组声明（可空）
        SettingsQmlRole,     // 读：QUrl，sidecar 的 settingsQml 绝对路径（可空 → QVariant()）
        SetConfigValueRole,  // 写：value 为 {key, value}，写 config 草稿单字段
        DiscoveredRole,
        GroupKeyRole,        // 读："enabled-left"，供设置页分组标题使用
        ApiVersionRole,
        CapabilitiesRole,
        DeclaredPermissionsRole,
        GrantedPermissionsRole,
        TrustLevelRole,
        ConfigVersionRole,
        TargetConfigVersionRole,
        MigrationAvailableRole,
        MigrationErrorRole
    };

    explicit SettingsPluginModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 重建：config 条目 ∪ exeDir 下磁盘发现；rawEntries 为 config.json 的 plugins
    // 数组原文（行内存原始对象，导出以其为底覆盖已知键，手写未知键不丢）
    void reload(const QList<PluginConfigEntry>& entries, const QJsonArray& rawEntries,
                const QString& exeDir, const QString& configPath);

    // 按 id 读写 config 草稿单字段（SettingsPluginProxy 用，绕过 QModelIndex）
    QVariant configValue(const QString& id, const QString& key) const;
    bool setConfigValue(const QString& id, const QString& key, const QVariant& value);
    // PluginManager 在加载时只计算临时迁移建议；此处不标脏、不写盘。
    bool setMigrationSuggestion(const QString& id, int targetVersion,
                                const QVariantMap& config, const QString& error);
    // 用户明确接受后才把建议提升为当前草稿版本，最终仍由 apply() 原子保存。
    bool acceptMigration(const QString& id);

    // 在同一状态栏区域内移动插件。order 仍是持久化格式，但仅由上移/下移操作
    // 维护，避免让用户手动猜数字；sourceRow 是本模型的行号。
    bool canMoveWithinPosition(int sourceRow, int direction) const;
    bool moveWithinPosition(int sourceRow, int direction);

    // 导出保存用 plugins 数组（含 position/order/schema 字段校验与 pybridge 托管）；
    // 校验失败 err 带原因、返回 false
    bool toPluginEntriesJson(QJsonArray* out, QString* err) const;

    // 仅插件草稿被用户实际改动时为 true。语言、主题等宿主设置单独保存时，
    // SettingsViewModel 据此原样保留 plugins 数组，避免把兼容迁移或默认值整理
    // 误判成插件变更，进而无谓重启全部插件。
    bool isDirty() const { return m_dirty; }

    // 语言切换时只刷新显示角色，不重建行或 config 草稿，避免丢失未保存编辑。
    void retranslate();

private:
    struct Row {
        QString id, name, version, author, kind;   // kind: "dll" | "python"
        bool enabled = false;
        bool discovered = false;   // 磁盘存在；config 有而磁盘缺失 → false（界面标"缺失"）
        QString position = QStringLiteral("right");
        int order = 0;
        int width = 0;             // 插件宽度（像素，0=跟随内容自适应）
        QVariantMap config;        // 结构化 config 草稿（全量，未知键随之保留）
        QJsonObject raw;           // config 条目原始 JSON（导出以其为底，未知键保留；
                                   // 磁盘发现行无原始对象，为空则全新组装）
        QJsonArray schema;         // sidecar "settings"（可空）
        QUrl settingsQml;          // sidecar "settingsQml"（可空）
        int apiVersion = 1;
        int configVersion = 1;       // 当前 config.json 条目版本
        int targetConfigVersion = 1; // sidecar 声明的最新版本
        QVariantMap migrationConfig;
        QString migrationError;
        bool migrationReady = false;
        QStringList capabilities;
        QStringList declaredPermissions;
        QStringList grantedPermissions;
        QString trustLevel;        // trusted-in-process | isolated
    };
    // dir = 该插件 sidecar 所在目录（settingsQml 相对路径据此解析为绝对 URL）
    void mergeDiscovered(const QJsonObject& meta, const QString& kind, const QString& dir);
    QVector<int> orderedRowsForPosition(const QString& position) const;

    QVector<Row> m_rows;
    QString m_secretDir;  // secrets.json 与当前 config.json 同目录（便携模式也成立）
    bool m_dirty = false;
};
