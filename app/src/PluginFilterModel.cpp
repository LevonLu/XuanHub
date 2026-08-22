// PluginFilterModel.cpp
#include "PluginFilterModel.h"
#include "PluginListModel.h"

PluginFilterModel::PluginFilterModel(const QString& position, QObject* parent,
                                     bool overflowOnly)
    : QSortFilterProxyModel(parent), m_position(position),
      m_overflowOnly(overflowOnly) {
    setDynamicSortFilter(true);
}

bool PluginFilterModel::filterAcceptsRow(int srcRow, const QModelIndex& srcParent) const {
    QModelIndex idx = sourceModel()->index(srcRow, 0, srcParent);
    if (m_overflowOnly)
        return sourceModel()->data(idx, PluginListModel::OverflowedRole).toBool();
    return sourceModel()->data(idx, PluginListModel::PositionRole).toString() == m_position
        && sourceModel()->data(idx, PluginListModel::LayoutVisibleRole).toBool();
}
