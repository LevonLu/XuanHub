// SysInfoViewModel.h — 系统信息数据模型（GUI 线程）
// 双重身份：
//  1) QAbstractListModel —— 布局模型，把 config 的 items 数组解析为行，
//     role 暴露 metric/prefix/color/text（text 按 metric 路由到当前指标值）。
//     QML 用 Repeater { model: plugin } 渲染每个指标项。
//  2) 指标值容器 —— 接收 worker 跨线程信号 metricsChanged，维护 5 项指标的当前文本，
//     某项文本变化时 emit 对应行的 dataChanged（TextRole）驱动 QML 刷新。
//
// metric 固定 5 种：cpu / mem / gpu / netDown / netUp。每项的显示文本由本类按 metric
// 路由（百分比 vs 速率格式化），前缀与颜色来自 config items（用户可自定义）。
#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QHash>

class SysInfoViewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int itemsPerColumn READ itemsPerColumn NOTIFY itemsPerColumnChanged)

public:
    enum Roles {
        MetricRole = Qt::UserRole + 1,   // metric id（cpu/mem/gpu/netDown/netUp）
        PrefixRole,                       // 用户配置的前缀
        ColorRole,                         // 用户配置的颜色（空串=QML 跟随主题）
        TextRole                           // 当前指标文本（路由自 5 个内部值）
    };
    Q_ENUM(Roles)

    explicit SysInfoViewModel(QObject* parent = nullptr);

    // ── 布局模型接口（QAbstractListModel）──
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int itemsPerColumn() const { return m_itemsPerColumn; }
    Q_INVOKABLE QVariant invokeAction(const QString& actionId);

    // 由 Plugin::init 调用：layoutData 是 [{metric, prefix, color}, ...]（来自 config 解析）
    // itemsPerColumn 为每列显示项数（决定 Grid 的行数，多余项向右另起一列）。
    // 整体替换，begin/endResetModel 驱动 QML Repeater 重建。
    void setLayout(const QVariantList& layoutData, int columns);

signals:
    void itemsPerColumnChanged();
    void refreshRequested();

    // ── 指标值接收（worker 跨线程信号，AutoConnection 排队到 GUI 线程）──
public slots:
    // cpu/mem/gpu 为 0.0-100.0；<0 表示本次未能采集 → 保留上次值（不回退到 ---）
    // netDownBps/netUpBps 为每秒字节数（恒 >=0）
    void onMetricsChanged(double cpuPct, double memPct, double gpuPct,
                          quint64 netDownBps, quint64 netUpBps);

private:
    // 布局条目：metric 决定路由到哪个指标值，prefix/color 是显示定制
    struct LayoutItem {
        QString metric;   // cpu/mem/gpu/netDown/netUp
        QString prefix;
        QString color;     // 空串=跟随主题
    };

    // 按 metric 路由取当前指标文本（供 data(TextRole) 用）
    QString textForMetric(const QString& metric) const;
    // 某指标文本变化时，找所有该 metric 的行 emit dataChanged（TextRole）
    void emitTextChanged(const QString& metric);

    // 格式化工具
    static QString formatPct(double pct);            // 45.0 → "45%"
    static QString formatRate(quint64 bytesPerSec);  // → "1.2 MB/s"

    QVector<LayoutItem> m_items;
    int m_itemsPerColumn = 1;

    // 5 项指标当前文本（初值 ---，直到首帧数据到达）
    QString m_cpuText{QStringLiteral("---")};
    QString m_memText{QStringLiteral("---")};
    QString m_gpuText{QStringLiteral("---")};
    QString m_netDownText{QStringLiteral("---")};
    QString m_netUpText{QStringLiteral("---")};
};
