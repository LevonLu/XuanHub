// PluginListModel.cpp
#include "PluginListModel.h"
#include "PluginFilterModel.h"

#include <QSet>

#include <algorithm>

namespace {
int positionRank(const QString& position) {
    if (position == QStringLiteral("left")) return 0;
    if (position == QStringLiteral("center")) return 1;
    return 2;
}

} // namespace

PluginListModel::PluginListModel(QObject* parent) : QAbstractListModel(parent) {
    m_leftModel   = new PluginFilterModel(QStringLiteral("left"),   this);
    m_centerModel = new PluginFilterModel(QStringLiteral("center"), this);
    m_rightModel  = new PluginFilterModel(QStringLiteral("right"),  this);
    m_overflowModel = new PluginFilterModel({}, this, true);
    m_leftModel->setSourceModel(this);
    m_centerModel->setSourceModel(this);
    m_rightModel->setSourceModel(this);
    m_overflowModel->setSourceModel(this);
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
        case PluginContextRole:return QVariant::fromValue(it.pluginContext);
        case WidthRole:    return it.width;
        case CapabilitiesRole: return it.capabilities;
        case HealthStateRole:        return xuanhub::pluginStateKey(it.health.state);
        case LastErrorRole:          return it.health.lastError;
        case LastErrorCodeRole:      return it.health.lastErrorCode;
        case LastErrorAtRole:        return it.health.lastErrorAt;
        case StartedAtRole:          return it.health.startedAt;
        case LastHealthyAtRole:      return it.health.lastHealthyAt;
        case RestartCountRole:       return it.health.restartCount;
        case ConsecutiveFailuresRole:return it.health.consecutiveFailures;
        case LoadDurationMsRole:     return it.health.loadDurationMs;
        case QueueDepthRole:         return it.health.queueDepth;
        case ProcessIdRole:          return it.health.processId;
        case EffectiveWidthRole:     return it.effectiveWidth;
        case LayoutVisibleRole:      return it.layoutVisible;
        case OverflowedRole:         return it.overflowed;
        case LayoutDeclaredRole:     return it.layout.declared;
        case LayoutMinWidthRole:     return it.layout.minWidth;
        case LayoutPreferredWidthRole:return it.layout.preferredWidth;
        case LayoutMaxWidthRole:     return it.layout.maxWidth;
        case LayoutFlexRole:         return it.layout.flex;
        case LayoutPriorityRole:     return it.layout.priority;
        case LayoutOverflowRole:     return it.layout.overflow;
    }
    return {};
}

QHash<int, QByteArray> PluginListModel::roleNames() const {
    return {
        { IdRole,        "id" },
        { PositionRole,  "position" },
        { QmlUrlRole,    "qmlUrl" },
        { ViewModelRole, "viewModel" },
        { PluginContextRole, "pluginContext" },
        { WidthRole,     "width" },
        { CapabilitiesRole, "capabilities" },
        { HealthStateRole,         "healthState" },
        { LastErrorRole,           "lastError" },
        { LastErrorCodeRole,       "lastErrorCode" },
        { LastErrorAtRole,         "lastErrorAt" },
        { StartedAtRole,           "startedAt" },
        { LastHealthyAtRole,       "lastHealthyAt" },
        { RestartCountRole,        "restartCount" },
        { ConsecutiveFailuresRole, "consecutiveFailures" },
        { LoadDurationMsRole,      "loadDurationMs" },
        { QueueDepthRole,          "queueDepth" },
        { ProcessIdRole,           "processId" },
        { EffectiveWidthRole,       "effectiveWidth" },
        { LayoutVisibleRole,        "layoutVisible" },
        { OverflowedRole,           "overflowed" },
        { LayoutDeclaredRole,       "layoutDeclared" },
        { LayoutMinWidthRole,       "layoutMinWidth" },
        { LayoutPreferredWidthRole, "layoutPreferredWidth" },
        { LayoutMaxWidthRole,       "layoutMaxWidth" },
        { LayoutFlexRole,           "layoutFlex" },
        { LayoutPriorityRole,       "layoutPriority" },
        { LayoutOverflowRole,       "layoutOverflow" },
    };
}

void PluginListModel::setPlugins(const QVector<PluginManager::LoadedPlugin>& items) {
    beginResetModel();
    m_items.clear();
    // 设置窗口的「自然宽度」只能代表当前已加载插件；保留旧 id 会让移除后
    // 再启用的插件继承过期尺寸。
    m_contentWidths.clear();

    for (const auto& lp : items) m_items.append(fromLoaded(lp));
    // stable_sort：同 position 同 order 的插件保持加载顺序。
    std::stable_sort(m_items.begin(), m_items.end(), layoutLess);
    endResetModel();
    recalculateResponsiveLayout();
}

bool PluginListModel::insertPlugin(const PluginManager::LoadedPlugin& loaded) {
    if (loaded.id.isEmpty() || rowForId(loaded.id) >= 0) return false;
    const Item item = fromLoaded(loaded);
    const int row = insertionRow(item);
    beginInsertRows({}, row, row);
    m_items.insert(row, item);
    endInsertRows();
    recalculateResponsiveLayout();
    return true;
}

bool PluginListModel::removePlugin(const QString& id) {
    const int row = rowForId(id);
    if (row < 0) return false;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    m_contentWidths.remove(id);
    endRemoveRows();
    recalculateResponsiveLayout();
    return true;
}

bool PluginListModel::replacePlugin(const PluginManager::LoadedPlugin& loaded) {
    const int oldRow = rowForId(loaded.id);
    if (oldRow < 0) return insertPlugin(loaded);
    beginRemoveRows({}, oldRow, oldRow);
    m_items.removeAt(oldRow);
    endRemoveRows();
    const Item item = fromLoaded(loaded);
    const int newRow = insertionRow(item);
    beginInsertRows({}, newRow, newRow);
    m_items.insert(newRow, item);
    endInsertRows();
    recalculateResponsiveLayout();
    return true;
}

bool PluginListModel::updateLayout(const QString& id, const QString& position,
                                   int order, int width) {
    const int oldRow = rowForId(id);
    if (oldRow < 0) return false;
    Item item = m_items.at(oldRow);
    const bool moves = item.position != position || item.order != order;
    const bool widthChanged = item.width != width;
    if (!moves && !widthChanged) return true;

    item.position = position;
    item.order = order;
    item.width = width;
    if (!moves) {
        m_items[oldRow] = item;
        const QModelIndex changed = index(oldRow);
        Q_EMIT dataChanged(changed, changed, {WidthRole});
        recalculateResponsiveLayout();
        return true;
    }

    // 跨三栏时代理模型会重建本插件 delegate，但 ViewModel 本身保持原地址；
    // 其他行不 reset，因此其弹窗和内部状态不受影响。
    beginRemoveRows({}, oldRow, oldRow);
    m_items.removeAt(oldRow);
    endRemoveRows();
    const int newRow = insertionRow(item);
    beginInsertRows({}, newRow, newRow);
    m_items.insert(newRow, item);
    endInsertRows();
    recalculateResponsiveLayout();
    return true;
}

void PluginListModel::setLoadErrors(const QHash<QString, QString>& errors) {
    QSet<QString> changedIds;
    for (auto it = m_loadErrors.cbegin(); it != m_loadErrors.cend(); ++it) {
        if (errors.value(it.key()) != it.value()) changedIds.insert(it.key());
    }
    for (auto it = errors.cbegin(); it != errors.cend(); ++it) {
        if (m_loadErrors.value(it.key()) != it.value()) changedIds.insert(it.key());
    }
    m_loadErrors = errors;
    for (const QString& id : changedIds) Q_EMIT loadErrorChanged(id);
}

PluginListModel::Item PluginListModel::fromLoaded(
    const PluginManager::LoadedPlugin& loaded) {
    Item item;
    item.id = loaded.id;
    item.position = loaded.position;
    item.order = loaded.order;
    item.qmlUrl = loaded.qmlUrl;
    item.vm = loaded.viewModel;
    item.pluginContext = loaded.pluginContext;
    item.width = loaded.width;
    item.capabilities = loaded.capabilities;
    item.layout = loaded.layout;
    item.health = loaded.health;
    return item;
}

bool PluginListModel::layoutLess(const Item& a, const Item& b) {
    const int pa = positionRank(a.position);
    const int pb = positionRank(b.position);
    return pa != pb ? pa < pb : a.order < b.order;
}

int PluginListModel::insertionRow(const Item& item) const {
    for (int row = 0; row < m_items.size(); ++row) {
        if (layoutLess(item, m_items.at(row))) return row;
    }
    return m_items.size();
}

int PluginListModel::rowForId(const QString& id) const {
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).id == id) return row;
    }
    return -1;
}

QAbstractItemModel* PluginListModel::leftPlugins()   const { return m_leftModel; }
QAbstractItemModel* PluginListModel::centerPlugins() const { return m_centerModel; }
QAbstractItemModel* PluginListModel::rightPlugins()  const { return m_rightModel; }
QAbstractItemModel* PluginListModel::overflowPluginModel() const {
    return m_overflowModel;
}

void PluginListModel::reportContentWidth(const QString& id, int w) {
    if (w <= 0) return;
    if (m_contentWidths.value(id) == w) return;
    m_contentWidths.insert(id, w);
    recalculateResponsiveLayout();
}

void PluginListModel::setAvailableWidth(int width) {
    const int normalized = qMax(0, width);
    if (normalized == m_availableWidth) return;
    m_availableWidth = normalized;
    recalculateResponsiveLayout();
}

void PluginListModel::recalculateResponsiveLayout() {
    QVector<ResponsiveLayoutEngine::Item> input;
    input.reserve(m_items.size());
    for (const Item& item : m_items) {
        input.append({item.id, m_contentWidths.value(item.id), item.width, item.layout});
    }
    const QVector<ResponsiveLayoutEngine::Result> allocated =
        ResponsiveLayoutEngine::allocate(m_availableWidth, input);
    QVariantList overflow;
    for (int row = 0; row < m_items.size(); ++row) {
        Item& item = m_items[row];
        const auto& result = allocated.at(row);
        const int effectiveWidth = m_availableWidth > 0 ? result.width : 0;
        const bool visible = m_availableWidth > 0 ? result.visible : true;
        const bool overflowed = m_availableWidth > 0 ? result.overflowed : false;
        if (item.effectiveWidth != effectiveWidth
            || item.layoutVisible != visible || item.overflowed != overflowed) {
            item.effectiveWidth = effectiveWidth;
            item.layoutVisible = visible;
            item.overflowed = overflowed;
            const QModelIndex changed = index(row);
            Q_EMIT dataChanged(changed, changed, {
                EffectiveWidthRole, LayoutVisibleRole, OverflowedRole});
        }
        if (overflowed) {
            overflow.append(QVariantMap{
                {QStringLiteral("id"), item.id},
                {QStringLiteral("position"), item.position},
            });
        }
    }
    m_overflowPlugins = overflow;
    ++m_layoutRevision;
    Q_EMIT responsiveLayoutChanged();
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
    if (update.contains(QStringLiteral("lastErrorCode")))
        health.lastErrorCode = update.value(QStringLiteral("lastErrorCode")).toString();
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
    if (update.contains(QStringLiteral("processId")))
        health.processId = update.value(QStringLiteral("processId")).toLongLong();

    const QModelIndex changed = index(row);
    Q_EMIT dataChanged(changed, changed, {
        HealthStateRole, LastErrorRole, LastErrorCodeRole, LastErrorAtRole, StartedAtRole,
        LastHealthyAtRole, RestartCountRole, ConsecutiveFailuresRole,
        LoadDurationMsRole, QueueDepthRole, ProcessIdRole,
    });
}

QVariantMap PluginListModel::health(const QString& id) const {
    for (const auto& item : m_items) {
        if (item.id != id) continue;
        return {
            {QStringLiteral("state"), xuanhub::pluginStateKey(item.health.state)},
            {QStringLiteral("lastError"), item.health.lastError},
            {QStringLiteral("lastErrorCode"), item.health.lastErrorCode},
            {QStringLiteral("lastErrorAt"), item.health.lastErrorAt},
            {QStringLiteral("startedAt"), item.health.startedAt},
            {QStringLiteral("lastHealthyAt"), item.health.lastHealthyAt},
            {QStringLiteral("restartCount"), item.health.restartCount},
            {QStringLiteral("consecutiveFailures"), item.health.consecutiveFailures},
            {QStringLiteral("loadDurationMs"), item.health.loadDurationMs},
            {QStringLiteral("queueDepth"), item.health.queueDepth},
            {QStringLiteral("processId"), item.health.processId},
        };
    }
    const QString loadError = m_loadErrors.value(id);
    if (!loadError.isEmpty()) {
        return {
            {QStringLiteral("state"), QStringLiteral("failed")},
            {QStringLiteral("lastError"), loadError},
        };
    }
    return {};
}

bool PluginListModel::restartPlugin(const QString& id) {
    for (const auto& item : m_items) {
        if (item.id != id || !item.vm) continue;
        bool result = false;
        const bool invoked = QMetaObject::invokeMethod(
            item.vm, "restart", Qt::DirectConnection, Q_RETURN_ARG(bool, result));
        return invoked && result;
    }
    return false;
}

int PluginListModel::contentWidth(const QString& id) const {
    return m_contentWidths.value(id, 0);
}
