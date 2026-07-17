// NetStatViewModel.cpp
#include "NetStatViewModel.h"

NetStatViewModel::NetStatViewModel(QObject* parent)
    : QObject(parent), m_upRate(QStringLiteral("---")), m_downRate(QStringLiteral("---")) {}

void NetStatViewModel::onRatesChanged(quint64 upBps, quint64 downBps) {
    const QString u = formatRate(upBps);
    const QString d = formatRate(downBps);
    if (u != m_upRate) { m_upRate = u; emit upRateChanged(); }
    if (d != m_downRate) { m_downRate = d; emit downRateChanged(); }
}

QString NetStatViewModel::formatRate(quint64 bps) {
    if (bps < 1024ULL)
        return QString::number(bps) + QStringLiteral(" B/s");
    if (bps < 1024ULL * 1024ULL)
        return QString::number(bps / 1024.0, 'f', 1) + QStringLiteral(" KB/s");
    if (bps < 1024ULL * 1024ULL * 1024ULL)
        return QString::number(bps / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB/s");
    return QString::number(bps / (1024.0 * 1024.0 * 1024.0), 'f', 1) + QStringLiteral(" GB/s");
}
