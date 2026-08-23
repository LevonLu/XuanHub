// SysInfoViewModel.cpp
#include "SysInfoViewModel.h"

#include <QVariantMap>

SysInfoViewModel::SysInfoViewModel(QObject* parent) : QAbstractListModel(parent) {}

QVariant SysInfoViewModel::invokeAction(const QString& actionId) {
    if (actionId != QStringLiteral("refresh")) {
        return QVariantMap{{QStringLiteral("accepted"), false}};
    }
    emit refreshRequested();
    return QVariantMap{{QStringLiteral("accepted"), true}};
}

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

void SysInfoViewModel::onMetricsChanged(const SysInfoSnapshot& snapshot) {
    // 仅当本次成功采集（>=0）才更新；失败项保留上次值（不回退 ---）
    // —— GPU 在 PDH 不可用且 DXGI 也失败时会持续 <0，UI 始终显示首次的 "---"
    if (snapshot.cpuPct >= 0.0) {
        const QString t = formatPct(snapshot.cpuPct);
        if (t != m_cpuText) { m_cpuText = t; emitTextChanged(QStringLiteral("cpu")); }
    }
    if (snapshot.memPct >= 0.0) {
        const QString t = formatPct(snapshot.memPct);
        if (t != m_memText) { m_memText = t; emitTextChanged(QStringLiteral("mem")); }
    }
    if (snapshot.gpuPct >= 0.0) {
        const QString t = formatPct(snapshot.gpuPct);
        if (t != m_gpuText) { m_gpuText = t; emitTextChanged(QStringLiteral("gpu")); }
    }
    // 网络项无失败语义（恒 >=0），始终更新
    {
        const QString t = formatRate(snapshot.netDownBps);
        if (t != m_netDownText) { m_netDownText = t; emitTextChanged(QStringLiteral("netDown")); }
    }
    {
        const QString t = formatRate(snapshot.netUpBps);
        if (t != m_netUpText) { m_netUpText = t; emitTextChanged(QStringLiteral("netUp")); }
    }

    m_cpuCurrentFrequencyText = formatFrequency(snapshot.cpuCurrentMhz);
    m_cpuMaxFrequencyText = formatFrequency(snapshot.cpuMaxMhz);
    m_logicalProcessorCount = snapshot.logicalProcessors;
    m_memoryUsedText = formatBytes(snapshot.memoryUsedBytes);
    m_memoryTotalText = formatBytes(snapshot.memoryTotalBytes);
    m_memoryAvailableText = formatBytes(snapshot.memoryAvailableBytes);
    m_memoryCachedText = formatBytes(snapshot.memoryCachedBytes);
    m_memoryCommitText = snapshot.memoryCommitLimitBytes > 0
        ? formatBytes(snapshot.memoryCommitUsedBytes) + QStringLiteral(" / ")
            + formatBytes(snapshot.memoryCommitLimitBytes)
        : QStringLiteral("---");
    m_gpuName = snapshot.gpuName.isEmpty() ? QStringLiteral("---") : snapshot.gpuName;
    m_gpuMemoryText = snapshot.gpuMemoryBudgetBytes > 0
        ? formatBytes(snapshot.gpuMemoryUsedBytes) + QStringLiteral(" / ")
            + formatBytes(snapshot.gpuMemoryBudgetBytes)
        : QStringLiteral("---");
    m_networkReceivedText = formatBytes(snapshot.networkReceivedBytes);
    m_networkSentText = formatBytes(snapshot.networkSentBytes);
    m_activeNetworkInterfaces = snapshot.activeNetworkInterfaces;

    appendHistory(m_cpuHistory, snapshot.cpuPct);
    appendHistory(m_memoryHistory, snapshot.memPct);
    appendHistory(m_gpuHistory, snapshot.gpuPct);
    appendHistory(m_netDownHistory, static_cast<double>(snapshot.netDownBps));
    appendHistory(m_netUpHistory, static_cast<double>(snapshot.netUpBps));
    emit detailsChanged();
    emit historyChanged();
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

QString SysInfoViewModel::formatBytes(quint64 bytes) {
    if (bytes == 0) return QStringLiteral("0 B");
    if (bytes < 1024ULL)
        return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024ULL * 1024ULL)
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    if (bytes < 1024ULL * 1024ULL * 1024ULL)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB");
}

QString SysInfoViewModel::formatFrequency(int mhz) {
    if (mhz <= 0) return QStringLiteral("---");
    if (mhz < 1000) return QString::number(mhz) + QStringLiteral(" MHz");
    return QString::number(mhz / 1000.0, 'f', 2) + QStringLiteral(" GHz");
}

void SysInfoViewModel::appendHistory(QVariantList& history, double value) {
    // 采样失败时延续上一点，避免图线突然掉到零；尚无有效点时从零开始。
    if (value < 0.0)
        value = history.isEmpty() ? 0.0 : history.constLast().toDouble();
    history.append(value);
    constexpr int kHistoryLimit = 60;
    while (history.size() > kHistoryLimit)
        history.removeFirst();
}
