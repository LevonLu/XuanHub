// PluginFilterModel.cpp
#include "PluginFilterModel.h"
#include "PluginListModel.h"

PluginFilterModel::PluginFilterModel(const QString& position, QObject* parent)
    : QSortFilterProxyModel(parent), m_position(position) {
    setDynamicSortFilter(true);
}

bool PluginFilterModel::filterAcceptsRow(int srcRow, const QModelIndex& srcParent) const {
    QModelIndex idx = sourceModel()->index(srcRow, 0, srcParent);
    return sourceModel()->data(idx, PluginListModel::PositionRole).toString() == m_position;
}
