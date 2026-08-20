// SettingsPluginSortModel.h — 插件列表排序代理（M10）
// 排序键：启用状态 → 状态栏区域（左/中/右）→ order 升序 → id 升序。order 由上移/下移维护，
// 代理同时为 QML 提供可移动性查询与操作，避免暴露内部 source 行号。
// 另向 QML 暴露按 id 查行、按角色名读写行的辅助（右侧详情面板用——
// QAbstractItemModel 的 data/setData 不是 Q_INVOKABLE，QML 无法直接调）。
#pragma once
#include <QSortFilterProxyModel>

class SettingsPluginSortModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit SettingsPluginSortModel(QObject* parent = nullptr);

    Q_INVOKABLE int count() const { return rowCount(); }
    Q_INVOKABLE int rowForId(const QString& id) const;
    Q_INVOKABLE QVariant roleValue(int row, const QString& roleName) const;
    Q_INVOKABLE bool setRoleValue(int row, const QString& roleName, const QVariant& value);
    Q_INVOKABLE bool canMoveUp(int row) const;
    Q_INVOKABLE bool canMoveDown(int row) const;
    Q_INVOKABLE bool moveUp(int row);
    Q_INVOKABLE bool moveDown(int row);

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};
