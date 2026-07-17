// SettingsPluginModel.h — 设置窗口的插件全集模型（M8）
// 行 = config 条目 ∪ 磁盘发现（plugins/*/metadata.json + plugins_py/*/plugin.json，
// 只读 sidecar 文件、零副作用）；pybridge 基础设施条目不显示，
// 导出时按 py_* 启用情况自动托管其条目。
#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QJsonArray>
#include <QJsonObject>

#include "ConfigService.h"   // PluginConfigEntry

class SettingsPluginModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, VersionRole, AuthorRole, KindRole,
        EnabledRole, PositionRole, OrderRole, ConfigJsonRole, DiscoveredRole
    };

    explicit SettingsPluginModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 重建：config 条目 ∪ exeDir 下磁盘发现
    void reload(const QList<PluginConfigEntry>& entries, const QString& exeDir);

    // 导出保存用 plugins 数组（含 position/order/configJson 校验与 pybridge 托管）；
    // 校验失败 err 带原因、返回 false
    bool toPluginEntriesJson(QJsonArray* out, QString* err) const;

private:
    struct Row {
        QString id, name, version, author, kind;   // kind: "dll" | "python"
        bool enabled = false;
        bool discovered = false;   // 磁盘存在；config 有而磁盘缺失 → false（界面标"缺失"）
        QString position = QStringLiteral("right");
        int order = 0;
        QString configJson = QStringLiteral("{}");
    };
    void mergeDiscovered(const QJsonObject& meta, const QString& kind);

    QVector<Row> m_rows;
};
