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
        case WidthRole:    return it.width;
        case HealthStateRole:        return xuanhub::pluginStateKey(it.health.state);
        case LastErrorRole:          return it.health.lastError;
        case LastErrorAtRole:        return it.health.lastErrorAt;
        case StartedAtRole:          return it.health.startedAt;
        case LastHealthyAtRole:      return it.health.lastHealthyAt;
        case RestartCountRole:       return it.health.restartCount;
        case ConsecutiveFailuresRole:return it.health.consecutiveFailures;
        case LoadDurationMsRole:     return it.health.loadDurationMs;
        case QueueDepthRole:         return it.health.queueDepth;
    }
    return {};
}

QHash<int, QByteArray> PluginListModel::roleNames() const {
    return {
        { IdRole,        "id" },
        { PositionRole,  "position" },
        { QmlUrlRole,    "qmlUrl" },
        { ViewModelRole, "viewModel" },
        { WidthRole,     "width" },
        { HealthStateRole,         "healthState" },
        { LastErrorRole,           "lastError" },
        { LastErrorAtRole,         "lastErrorAt" },
        { StartedAtRole,           "startedAt" },
        { LastHealthyAtRole,       "lastHealthyAt" },
        { RestartCountRole,        "restartCount" },
        { ConsecutiveFailuresRole, "consecutiveFailures" },
        { LoadDurationMsRole,      "loadDurationMs" },
        { QueueDepthRole,          "queueDepth" },
    };
}

void PluginListModel::setPlugins(const QVector<PluginManager::LoadedPlugin>& items) {
    beginResetModel();
    m_items.clear();
    // 设置窗口的「自然宽度」只能代表当前已加载插件；保留旧 id 会让移除后
    // 再启用的插件继承过期尺寸。
    m_contentWidths.clear();

    auto sorted = items;
    // stable_sort：同 position 同 order 的插件保持加载顺序——std::sort 对等价
    // 元素相对顺序不定，热重载（重新加载）后同 order 插件排位会随机互换
    std::stable_sort(sorted.begin(), sorted.end(),
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

    for (const auto& lp : sorted) {
        Item item;
        item.id = lp.id;
        item.position = lp.position;
        item.qmlUrl = lp.qmlUrl;
        item.vm = lp.viewModel;
        item.width = lp.width;
        item.health = lp.health;
        m_items.append(item);
    }
    endResetModel();
}

QAbstractItemModel* PluginListModel::leftPlugins()   const { return m_leftModel; }
QAbstractItemModel* PluginListModel::centerPlugins() const { return m_centerModel; }
QAbstractItemModel* PluginListModel::rightPlugins()  const { return m_rightModel; }

void PluginListModel::reportContentWidth(const QString& id, int w) {
    if (w <= 0) return;
    m_contentWidths.insert(id, w);
}

void PluginListModel::reportHealth(const QString& id, const QVariantMap& update) {
    int row = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            row = i;
            break;
        }
    }
    if (row < 0 || update.isEmpty()) return;

    auto& health = m_items[row].health;
    if (update.contains(QStringLiteral("state"))) {
        health.state = xuanhub::pluginStateFromKey(
            update.value(QStringLiteral("state")).toString(), health.state);
    }
    if (update.contains(QStringLiteral("lastError")))
        health.lastError = update.value(QStringLiteral("lastError")).toString();
    if (update.contains(QStringLiteral("lastErrorAt")))
        health.lastErrorAt = update.value(QStringLiteral("lastErrorAt")).toDateTime();
    if (update.contains(QStringLiteral("startedAt")))
        health.startedAt = update.value(QStringLiteral("startedAt")).toDateTime();
    if (update.contains(QStringLiteral("lastHealthyAt")))
        health.lastHealthyAt = update.value(QStringLiteral("lastHealthyAt")).toDateTime();
    if (update.contains(QStringLiteral("restartCount")))
        health.restartCount = update.value(QStringLiteral("restartCount")).toInt();
    if (update.contains(QStringLiteral("consecutiveFailures")))
        health.consecutiveFailures = update.value(QStringLiteral("consecutiveFailures")).toInt();
    if (update.contains(QStringLiteral("loadDurationMs")))
        health.loadDurationMs = update.value(QStringLiteral("loadDurationMs")).toLongLong();
    if (update.contains(QStringLiteral("queueDepth")))
        health.queueDepth = update.value(QStringLiteral("queueDepth")).toInt();

    const QModelIndex changed = index(row);
    Q_EMIT dataChanged(changed, changed, {
        HealthStateRole, LastErrorRole, LastErrorAtRole, StartedAtRole,
        LastHealthyAtRole, RestartCountRole, ConsecutiveFailuresRole,
        LoadDurationMsRole, QueueDepthRole,
    });
}

int PluginListModel::contentWidth(const QString& id) const {
    return m_contentWidths.value(id, 0);
}
