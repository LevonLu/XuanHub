// SysInfoViewModel.cpp
#include "SysInfoViewModel.h"

#include <QVariantMap>

SysInfoViewModel::SysInfoViewModel(QObject* parent) : QAbstractListModel(parent) {}

int SysInfoViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant SysInfoViewModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const auto& item = m_items.at(index.row());
    switch (role) {
        case MetricRole: return item.metric;
        case PrefixRole: return item.prefix;
        case ColorRole:  return item.color;
        case TextRole:   return textForMetric(item.metric);
    }
    return {};
}

QHash<int, QByteArray> SysInfoViewModel::roleNames() const {
    return {
        { MetricRole, "metric" },
        { PrefixRole, "prefix" },
        { ColorRole,  "color" },
        { TextRole,   "text" },
    };
}

void SysInfoViewModel::setLayout(const QVariantList& layoutData, int itemsPerColumn) {
    const int oldItemsPerColumn = m_itemsPerColumn;
    beginResetModel();
    m_items.clear();
    m_items.reserve(layoutData.size());
    for (const QVariant& v : layoutData) {
        const QVariantMap m = v.toMap();
        LayoutItem item;
        item.metric = m.value(QStringLiteral("metric")).toString();
        item.prefix = m.value(QStringLiteral("prefix")).toString();
        item.color  = m.value(QStringLiteral("color")).toString();
        // metric 非法值跳过（防 config 手写错）；未知 metric 不显示
        if (item.metric == QStringLiteral("cpu") || item.metric == QStringLiteral("mem")
            || item.metric == QStringLiteral("gpu")
            || item.metric == QStringLiteral("netDown") || item.metric == QStringLiteral("netUp")) {
            m_items.append(item);
        }
    }
    // itemsPerColumn 钳到合理范围（至少 1，至多项数）
    m_itemsPerColumn = qBound(1, itemsPerColumn, qMax(1, m_items.size()));
    endResetModel();
    if (m_itemsPerColumn != oldItemsPerColumn) emit itemsPerColumnChanged();
}

void SysInfoViewModel::onMetricsChanged(double cpuPct, double memPct, double gpuPct,
                                        quint64 netDownBps, quint64 netUpBps) {
    // 仅当本次成功采集（>=0）才更新；失败项保留上次值（不回退 ---）
    // —— GPU 在 PDH 不可用且 DXGI 也失败时会持续 <0，UI 始终显示首次的 "---"
    if (cpuPct >= 0.0) {
        const QString t = formatPct(cpuPct);
        if (t != m_cpuText) { m_cpuText = t; emitTextChanged(QStringLiteral("cpu")); }
    }
    if (memPct >= 0.0) {
        const QString t = formatPct(memPct);
        if (t != m_memText) { m_memText = t; emitTextChanged(QStringLiteral("mem")); }
    }
    if (gpuPct >= 0.0) {
        const QString t = formatPct(gpuPct);
        if (t != m_gpuText) { m_gpuText = t; emitTextChanged(QStringLiteral("gpu")); }
    }
    // 网络项无失败语义（恒 >=0），始终更新
    {
        const QString t = formatRate(netDownBps);
        if (t != m_netDownText) { m_netDownText = t; emitTextChanged(QStringLiteral("netDown")); }
    }
    {
        const QString t = formatRate(netUpBps);
        if (t != m_netUpText) { m_netUpText = t; emitTextChanged(QStringLiteral("netUp")); }
    }
}

QString SysInfoViewModel::textForMetric(const QString& metric) const {
    if (metric == QStringLiteral("cpu"))     return m_cpuText;
    if (metric == QStringLiteral("mem"))     return m_memText;
    if (metric == QStringLiteral("gpu"))     return m_gpuText;
    if (metric == QStringLiteral("netDown")) return m_netDownText;
    if (metric == QStringLiteral("netUp"))   return m_netUpText;
    return QStringLiteral("---");
}

void SysInfoViewModel::emitTextChanged(const QString& metric) {
    // 找所有该 metric 的行，emit dataChanged（仅 TextRole）驱动 QML 刷新对应行
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].metric == metric) {
            const QModelIndex mi = index(i);
            emit dataChanged(mi, mi, {TextRole});
        }
    }
}

QString SysInfoViewModel::formatPct(double pct) {
    // 四舍五入到整数百分比（状态栏空间有限，小数无意义）
    const int v = qRound(pct);
    return QString::number(v) + QStringLiteral("%");
}

QString SysInfoViewModel::formatRate(quint64 bytesPerSec) {
    // 迁自 NetStatViewModel：B/KB/MB/GB 自适应单位，保留 1 位小数
    if (bytesPerSec < 1024ULL)
        return QString::number(bytesPerSec) + QStringLiteral(" B/s");
    if (bytesPerSec < 1024ULL * 1024ULL)
        return QString::number(bytesPerSec / 1024.0, 'f', 1) + QStringLiteral(" KB/s");
    if (bytesPerSec < 1024ULL * 1024ULL * 1024ULL)
        return QString::number(bytesPerSec / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB/s");
    return QString::number(bytesPerSec / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB/s");
}
