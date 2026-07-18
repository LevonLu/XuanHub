// NetStatPlugin.cpp
#include "NetStatPlugin.h"
#include "NetStatWorker.h"
#include "NetStatViewModel.h"

#include <QColor>
#include <QThread>
#include <QVariantMap>
#include <QDebug>

using namespace xuanhub;

static const PluginMetadata kMeta = {
    QStringLiteral("netstat"),
    QStringLiteral("Network"),
    QStringLiteral("0.1.0"),
    QStringLiteral("XuanHub"),
};

PluginMetadata NetStatPlugin::metadata() const { return kMeta; }

bool NetStatPlugin::init(BarPluginContext* ctx) {
    const auto cfg = ctx ? ctx->pluginConfig : QVariantMap();
    m_intervalMs = cfg.value(QStringLiteral("intervalMs"), 1000).toInt();

    // 两行显示的前缀（各自独立配置，默认箭头）；颜色缺省/非法 → 空串 = QML 侧跟随主题
    const QString downPrefix = cfg.value(QStringLiteral("downPrefix"), QStringLiteral("↓ ")).toString();
    const QString upPrefix   = cfg.value(QStringLiteral("upPrefix"),   QStringLiteral("↑ ")).toString();
    const auto colorOrEmpty = [&cfg](const QString& key) -> QString {
        const QString v = cfg.value(key).toString().trimmed();
        if (v.isEmpty()) return QString();
        if (!QColor::isValidColorName(v)) {
            qWarning() << "[NetStat] invalid color in config:" << key << "=" << v
                       << "-> fallback to theme";
            return QString();
        }
        return v;
    };

    m_worker = new NetStatWorker();
    m_vm     = new NetStatViewModel(downPrefix, upPrefix,
                                    colorOrEmpty(QStringLiteral("downColor")),
                                    colorOrEmpty(QStringLiteral("upColor")));
    m_thread = new QThread();

    m_worker->moveToThread(m_thread.data());
    connect(m_worker.data(), &NetStatWorker::ratesChanged,
            m_vm.data(),     &NetStatViewModel::onRatesChanged,
            Qt::AutoConnection);
    connect(m_thread.data(), &QThread::finished,
            m_worker.data(), &QObject::deleteLater);

    m_thread->start();
    QMetaObject::invokeMethod(m_worker.data(), "start",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_intervalMs));
    return true;
}

QObject* NetStatPlugin::viewModel() { return m_vm; }

QUrl NetStatPlugin::qmlUrl() const { return QUrl(QStringLiteral("NetStat.qml")); }

void NetStatPlugin::shutdown() {
    if (m_worker)
        QMetaObject::invokeMethod(m_worker.data(), "stop", Qt::QueuedConnection);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
    }
    if (m_vm) { delete m_vm; m_vm = nullptr; }
    // worker deleted by QThread::finished → deleteLater
    if (m_thread) { delete m_thread; m_thread = nullptr; }
}
