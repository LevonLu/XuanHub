// PluginListModel.h — QML 可用的插件列表模型
// 角色：插件内容、布局与运行时健康状态。
// 暴露 leftPlugins / centerPlugins / rightPlugins 三个过滤子模型。
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QVector>
#include <QUrl>
#include <QPointer>
#include <QVariantMap>

#include "PluginManager.h"
#include "ResponsiveLayoutEngine.h"

class PluginFilterModel;

class PluginListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* leftPlugins   READ leftPlugins   CONSTANT)
    Q_PROPERTY(QAbstractItemModel* centerPlugins READ centerPlugins CONSTANT)
    Q_PROPERTY(QAbstractItemModel* rightPlugins  READ rightPlugins  CONSTANT)
    Q_PROPERTY(QAbstractItemModel* overflowPluginModel READ overflowPluginModel CONSTANT)
    Q_PROPERTY(QVariantList overflowPlugins READ overflowPlugins
               NOTIFY responsiveLayoutChanged)
    Q_PROPERTY(int layoutRevision READ layoutRevision NOTIFY responsiveLayoutChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        PositionRole,
        QmlUrlRole,
        ViewModelRole,
        PluginContextRole,
        WidthRole,
        CapabilitiesRole,
        HealthStateRole,
        LastErrorRole,
        LastErrorCodeRole,
        LastErrorAtRole,
        StartedAtRole,
        LastHealthyAtRole,
        RestartCountRole,
        ConsecutiveFailuresRole,
        LoadDurationMsRole,
        QueueDepthRole,
        ProcessIdRole,
        EffectiveWidthRole,
        LayoutVisibleRole,
        OverflowedRole,
        LayoutDeclaredRole,
        LayoutMinWidthRole,
        LayoutPreferredWidthRole,
        LayoutMaxWidthRole,
        LayoutFlexRole,
        LayoutPriorityRole,
        LayoutOverflowRole,
    };

    explicit PluginListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlugins(const QVector<PluginManager::LoadedPlugin>& items);
    bool insertPlugin(const PluginManager::LoadedPlugin& item);
    bool removePlugin(const QString& id);
    // 重启后替换 ViewModel/QML 时只重建目标 delegate，不 reset 其他插件。
    bool replacePlugin(const PluginManager::LoadedPlugin& item);
    // position/order 变化只移动目标行；width 单独更新角色，不销毁插件实例。
    bool updateLayout(const QString& id, const QString& position, int order, int width);
    void setLoadErrors(const QHash<QString, QString>& errors);

    QAbstractItemModel* leftPlugins()   const;
    QAbstractItemModel* centerPlugins() const;
    QAbstractItemModel* rightPlugins()  const;
    QAbstractItemModel* overflowPluginModel() const;

    // 插件槽（PluginSlot.qml）上报的自然内容宽度（逻辑像素）；
    // 设置窗口切「手动」宽度时以此作初始值。与 WidthRole（config 定宽）无关。
    Q_INVOKABLE void reportContentWidth(const QString& id, int w);
    Q_INVOKABLE void setAvailableWidth(int width);
    // 插件代理或 PluginSlot 以稳定字段增量上报；未提供的指标保持原值。
    Q_INVOKABLE void reportHealth(const QString& id, const QVariantMap& update);
    Q_INVOKABLE QVariantMap health(const QString& id) const;
    Q_INVOKABLE bool restartPlugin(const QString& id);
    int contentWidth(const QString& id) const;   // 无记录返回 0
    QVariantList overflowPlugins() const { return m_overflowPlugins; }
    int layoutRevision() const { return m_layoutRevision; }

Q_SIGNALS:
    void loadErrorChanged(const QString& id);
    void responsiveLayoutChanged();

private:
    struct Item {
        QString id;
        QString position;
        int order = 0;
        QUrl qmlUrl;
        QObject* vm = nullptr;
        QObject* pluginContext = nullptr;
        int width = 0;
        QStringList capabilities;
        xuanhub::PluginLayoutPreference layout;
        int effectiveWidth = 0;
        bool layoutVisible = true;
        bool overflowed = false;
        xuanhub::PluginHealth health;
    };

    static Item fromLoaded(const PluginManager::LoadedPlugin& item);
    static bool layoutLess(const Item& a, const Item& b);
    int insertionRow(const Item& item) const;
    int rowForId(const QString& id) const;
    void recalculateResponsiveLayout();
    QVector<Item> m_items;
    QHash<QString, int> m_contentWidths;
    QHash<QString, QString> m_loadErrors;
    int m_availableWidth = 0;
    int m_layoutRevision = 0;
    QVariantList m_overflowPlugins;

    QPointer<PluginFilterModel> m_leftModel;
    QPointer<PluginFilterModel> m_centerModel;
    QPointer<PluginFilterModel> m_rightModel;
    QPointer<PluginFilterModel> m_overflowModel;
};
