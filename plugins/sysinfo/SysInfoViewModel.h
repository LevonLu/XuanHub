// SysInfoViewModel.h — 系统信息数据模型（GUI 线程）
// 双重身份：
//  1) QAbstractListModel —— 布局模型，把 config 的 items 数组解析为行，
//     role 暴露 metric/prefix/color/text（text 按 metric 路由到当前指标值）。
//     QML 用 Repeater { model: plugin } 渲染每个指标项。
//  2) 指标值容器 —— 接收 worker 跨线程快照，维护当前文本、详情与最近 60 个采样点；
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

#include "SysInfoWorker.h"

class SysInfoViewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int itemsPerColumn READ itemsPerColumn NOTIFY itemsPerColumnChanged)
    Q_PROPERTY(QString cpuText READ cpuText NOTIFY detailsChanged)
    Q_PROPERTY(QString memoryText READ memoryText NOTIFY detailsChanged)
    Q_PROPERTY(QString gpuText READ gpuText NOTIFY detailsChanged)
    Q_PROPERTY(QString netDownText READ netDownText NOTIFY detailsChanged)
    Q_PROPERTY(QString netUpText READ netUpText NOTIFY detailsChanged)
    Q_PROPERTY(QString cpuCurrentFrequencyText READ cpuCurrentFrequencyText NOTIFY detailsChanged)
    Q_PROPERTY(QString cpuMaxFrequencyText READ cpuMaxFrequencyText NOTIFY detailsChanged)
    Q_PROPERTY(int logicalProcessorCount READ logicalProcessorCount NOTIFY detailsChanged)
    Q_PROPERTY(QString memoryUsedText READ memoryUsedText NOTIFY detailsChanged)
    Q_PROPERTY(QString memoryTotalText READ memoryTotalText NOTIFY detailsChanged)
    Q_PROPERTY(QString memoryAvailableText READ memoryAvailableText NOTIFY detailsChanged)
    Q_PROPERTY(QString memoryCachedText READ memoryCachedText NOTIFY detailsChanged)
    Q_PROPERTY(QString memoryCommitText READ memoryCommitText NOTIFY detailsChanged)
    Q_PROPERTY(QString gpuName READ gpuName NOTIFY detailsChanged)
    Q_PROPERTY(QString gpuMemoryText READ gpuMemoryText NOTIFY detailsChanged)
    Q_PROPERTY(QString networkReceivedText READ networkReceivedText NOTIFY detailsChanged)
    Q_PROPERTY(QString networkSentText READ networkSentText NOTIFY detailsChanged)
    Q_PROPERTY(int activeNetworkInterfaces READ activeNetworkInterfaces NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY historyChanged)
    Q_PROPERTY(QVariantList memoryHistory READ memoryHistory NOTIFY historyChanged)
    Q_PROPERTY(QVariantList gpuHistory READ gpuHistory NOTIFY historyChanged)
    Q_PROPERTY(QVariantList netDownHistory READ netDownHistory NOTIFY historyChanged)
    Q_PROPERTY(QVariantList netUpHistory READ netUpHistory NOTIFY historyChanged)

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
    QString cpuText() const { return m_cpuText; }
    QString memoryText() const { return m_memText; }
    QString gpuText() const { return m_gpuText; }
    QString netDownText() const { return m_netDownText; }
    QString netUpText() const { return m_netUpText; }
    QString cpuCurrentFrequencyText() const { return m_cpuCurrentFrequencyText; }
    QString cpuMaxFrequencyText() const { return m_cpuMaxFrequencyText; }
    int logicalProcessorCount() const { return m_logicalProcessorCount; }
    QString memoryUsedText() const { return m_memoryUsedText; }
    QString memoryTotalText() const { return m_memoryTotalText; }
    QString memoryAvailableText() const { return m_memoryAvailableText; }
    QString memoryCachedText() const { return m_memoryCachedText; }
    QString memoryCommitText() const { return m_memoryCommitText; }
    QString gpuName() const { return m_gpuName; }
    QString gpuMemoryText() const { return m_gpuMemoryText; }
    QString networkReceivedText() const { return m_networkReceivedText; }
    QString networkSentText() const { return m_networkSentText; }
    int activeNetworkInterfaces() const { return m_activeNetworkInterfaces; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    QVariantList memoryHistory() const { return m_memoryHistory; }
    QVariantList gpuHistory() const { return m_gpuHistory; }
    QVariantList netDownHistory() const { return m_netDownHistory; }
    QVariantList netUpHistory() const { return m_netUpHistory; }
    Q_INVOKABLE QVariant invokeAction(const QString& actionId);

    // 由 Plugin::init 调用：layoutData 是 [{metric, prefix, color}, ...]（来自 config 解析）
    // itemsPerColumn 为每列显示项数（决定 Grid 的行数，多余项向右另起一列）。
    // 整体替换，begin/endResetModel 驱动 QML Repeater 重建。
    void setLayout(const QVariantList& layoutData, int columns);

signals:
    void itemsPerColumnChanged();
    void refreshRequested();
    void detailsChanged();
    void historyChanged();

    // ── 指标值接收（worker 跨线程信号，AutoConnection 排队到 GUI 线程）──
public slots:
    // cpu/mem/gpu 为 0.0-100.0；<0 表示本次未能采集 → 保留上次值（不回退到 ---）
    // netDownBps/netUpBps 为每秒字节数（恒 >=0）
    void onMetricsChanged(const SysInfoSnapshot& snapshot);

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
    static QString formatBytes(quint64 bytes);       // → "12.8 GB"
    static QString formatFrequency(int mhz);         // → "3.9 GHz"
    static void appendHistory(QVariantList& history, double value);

    QVector<LayoutItem> m_items;
    int m_itemsPerColumn = 1;

    // 5 项指标当前文本（初值 ---，直到首帧数据到达）
    QString m_cpuText{QStringLiteral("---")};
    QString m_memText{QStringLiteral("---")};
    QString m_gpuText{QStringLiteral("---")};
    QString m_netDownText{QStringLiteral("---")};
    QString m_netUpText{QStringLiteral("---")};

    QString m_cpuCurrentFrequencyText{QStringLiteral("---")};
    QString m_cpuMaxFrequencyText{QStringLiteral("---")};
    int m_logicalProcessorCount = 0;
    QString m_memoryUsedText{QStringLiteral("---")};
    QString m_memoryTotalText{QStringLiteral("---")};
    QString m_memoryAvailableText{QStringLiteral("---")};
    QString m_memoryCachedText{QStringLiteral("---")};
    QString m_memoryCommitText{QStringLiteral("---")};
    QString m_gpuName{QStringLiteral("---")};
    QString m_gpuMemoryText{QStringLiteral("---")};
    QString m_networkReceivedText{QStringLiteral("---")};
    QString m_networkSentText{QStringLiteral("---")};
    int m_activeNetworkInterfaces = 0;

    QVariantList m_cpuHistory;
    QVariantList m_memoryHistory;
    QVariantList m_gpuHistory;
    QVariantList m_netDownHistory;
    QVariantList m_netUpHistory;
};
