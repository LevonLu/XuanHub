// PluginListModel.cpp
#include "PluginListModel.h"
#include "PluginFilterModel.h"

#include <algorithm>

PluginListModel::PluginListModel(QObject* parent) : QAbstractListModel(parent) {
    m_leftModel   = new PluginFilterModel(QStringLiteral("left"),   this);
    m_centerModel = new PluginFilterModel(QStringLiteral("center"), this);
    m_rightModel  = new PluginFilterModel(QStringLiteral("right"),  this);
    m_leftModel->setSourceModel(this);
    m_centerModel->setSourceModel(this);
    m_rightModel->setSourceModel(this);
}

int PluginListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant PluginListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const auto& it = m_items.at(index.row());
    switch (role) {
        case IdRole:       return it.id;
        case PositionRole: return it.position;
        case QmlUrlRole:   return it.qmlUrl;
        case ViewModelRole:return QVariant::fromValue(it.vm);
    }
    return {};
}

QHash<int, QByteArray> PluginListModel::roleNames() const {
    return {
        { IdRole,        "id" },
        { PositionRole,  "position" },
        { QmlUrlRole,    "qmlUrl" },
        { ViewModelRole, "viewModel" },
    };
}

void PluginListModel::setPlugins(const QVector<PluginManager::LoadedPlugin>& items) {
    beginResetModel();
    m_items.clear();

    auto sorted = items;
    std::sort(sorted.begin(), sorted.end(),
              [](const PluginManager::LoadedPlugin& a,
                 const PluginManager::LoadedPlugin& b) {
                  static const QString posOrder[] = {
                      QStringLiteral("left"),
                      QStringLiteral("center"),
                      QStringLiteral("right")
                  };
                  int pa = 0, pb = 0;
                  for (int i = 0; i < 3; ++i) {
                      if (posOrder[i] == a.position) pa = i;
                      if (posOrder[i] == b.position) pb = i;
                  }
                  if (pa != pb) return pa < pb;
                  return a.order < b.order;
    });

    for (const auto& lp : sorted)
        m_items.append({ lp.id, lp.position, lp.qmlUrl, lp.viewModel });
    endResetModel();
}

QAbstractItemModel* PluginListModel::leftPlugins()   const { return m_leftModel; }
QAbstractItemModel* PluginListModel::centerPlugins() const { return m_centerModel; }
QAbstractItemModel* PluginListModel::rightPlugins()  const { return m_rightModel; }
