// SettingsPluginSortModel.cpp
#include "SettingsPluginSortModel.h"
#include "SettingsPluginModel.h"

SettingsPluginSortModel::SettingsPluginSortModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    // 注意：sort(0) 由 SettingsViewModel 在 setSourceModel 之后调用
    //（sortColumn 缺省 -1 不排序；dynamicSortFilter 只在排序已启用时维持）
}

int SettingsPluginSortModel::rowForId(const QString& id) const {
    const int n = rowCount();
    for (int i = 0; i < n; ++i) {
        if (data(index(i, 0), SettingsPluginModel::IdRole).toString() == id) return i;
    }
    return -1;
}

QVariant SettingsPluginSortModel::roleValue(int row, const QString& roleName) const {
    if (row < 0 || row >= rowCount() || !sourceModel()) return {};
    const int role = sourceModel()->roleNames().key(roleName.toUtf8(), -1);
    if (role < 0) return {};
    return data(index(row, 0), role);
}

bool SettingsPluginSortModel::setRoleValue(int row, const QString& roleName,
                                           const QVariant& value) {
    if (row < 0 || row >= rowCount() || !sourceModel()) return false;
    const int role = sourceModel()->roleNames().key(roleName.toUtf8(), -1);
    if (role < 0) return false;
    return setData(index(row, 0), value, role);
}

bool SettingsPluginSortModel::lessThan(const QModelIndex& left,
                                       const QModelIndex& right) const {
    const bool le = left.data(SettingsPluginModel::EnabledRole).toBool();
    const bool re = right.data(SettingsPluginModel::EnabledRole).toBool();
    if (le != re) return le > re;   // 启用在前
    const int lo = left.data(SettingsPluginModel::OrderRole).toInt();
    const int ro = right.data(SettingsPluginModel::OrderRole).toInt();
    if (lo != ro) return lo < ro;
    return left.data(SettingsPluginModel::IdRole).toString()
           < right.data(SettingsPluginModel::IdRole).toString();
}
