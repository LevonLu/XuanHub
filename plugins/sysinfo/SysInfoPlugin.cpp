// SysInfoPlugin.cpp
#include "SysInfoPlugin.h"
#include "SysInfoWorker.h"
#include "SysInfoViewModel.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QDebug>

using namespace xuanhub;

static const PluginMetadata kMeta = {
    QStringLiteral("sysinfo"),
    QStringLiteral("System Info"),
    QStringLiteral("0.2.0"),
    QStringLiteral("XuanHub"),
};

// 默认布局（config 无 items 时播种）：CPU/RAM/GPU/下行/上行，单列
static QVariantList defaultLayout() {
    QVariantList items;
    auto add = [&items](const QString& metric, const QString& prefix) {
        QVariantMap m;
        m.insert(QStringLiteral("metric"), metric);
        m.insert(QStringLiteral("prefix"), prefix);
        m.insert(QStringLiteral("color"), QString());   // 空串=跟随主题
        items.append(m);
    };
    add(QStringLiteral("cpu"),     QStringLiteral("CPU "));
    add(QStringLiteral("mem"),     QStringLiteral("RAM "));
    add(QStringLiteral("gpu"),     QStringLiteral("GPU "));
    add(QStringLiteral("netDown"), QStringLiteral("↓ "));
    add(QStringLiteral("netUp"),   QStringLiteral("↑ "));
    return items;
}

PluginMetadata SysInfoPlugin::metadata() const { return kMeta; }

bool SysInfoPlugin::init(BarPluginContext* ctx) {
    const auto cfg = ctx ? ctx->pluginConfig : QVariantMap();
    // 钳制采样间隔：0 会让 worker 线程每事件循环都 tick（PDH collect + GetIfTable2）
    // 占满一个核并狂发跨线程信号；负值 QTimer 直接不启动。200ms 下限兼顾流畅与开销
    m_intervalMs = qBound(200, cfg.value(QStringLiteral("intervalMs"), 1000).toInt(), 60000);
    const int itemsPerColumn = cfg.value(QStringLiteral("itemsPerColumn"), 1).toInt();

    // 解析 items 布局（config 无则播种默认 5 项）；颜色校验非法→空串（跟随主题）
    QVariantList layout;
    const QVariant itemsVar = cfg.value(QStringLiteral("items"));
    if (itemsVar.isValid() && itemsVar.canConvert<QVariantList>()) {
        const QVariantList arr = itemsVar.toList();
        const auto colorOrEmpty = [](const QVariant& v) -> QString {
            const QString s = v.toString().trimmed();
            if (s.isEmpty()) return QString();
            if (!QColor::isValidColorName(s)) {
                qWarning() << "[SysInfo] invalid color in config:" << s << "-> fallback to theme";
                return QString();
            }
            return s;
        };
        for (const QVariant& v : arr) {
            const QVariantMap m = v.toMap();
            const QString metric = m.value(QStringLiteral("metric")).toString();
            // metric 非法跳过（ViewModel.setLayout 也会二次校验，这里提前过滤）
            if (metric != QStringLiteral("cpu") && metric != QStringLiteral("mem")
                && metric != QStringLiteral("gpu")
                && metric != QStringLiteral("netDown") && metric != QStringLiteral("netUp"))
                continue;
            QVariantMap item;
            item.insert(QStringLiteral("metric"), metric);
            item.insert(QStringLiteral("prefix"), m.value(QStringLiteral("prefix")).toString());
            item.insert(QStringLiteral("color"), colorOrEmpty(m.value(QStringLiteral("color"))));
            layout.append(item);
        }
    }
    if (layout.isEmpty()) layout = defaultLayout();

    m_worker = new SysInfoWorker();
    m_vm = new SysInfoViewModel();
    m_vm->setLayout(layout, itemsPerColumn);
    m_thread = new QThread();

    m_worker->moveToThread(m_thread.data());
    connect(m_worker.data(), &SysInfoWorker::metricsChanged,
            m_vm.data(),     &SysInfoViewModel::onMetricsChanged,
            Qt::AutoConnection);
    connect(m_thread.data(), &QThread::finished,
            m_worker.data(), &QObject::deleteLater);

    m_thread->start();
    QMetaObject::invokeMethod(m_worker.data(), "start",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_intervalMs));
    return true;
}

QObject* SysInfoPlugin::viewModel() { return m_vm; }

QUrl SysInfoPlugin::qmlUrl() const { return QUrl(QStringLiteral("SysInfo.qml")); }

void SysInfoPlugin::shutdown() {
    // 严格五步（同 netstat 范式，顺序不可颠倒）：
    // ① 投递 stop 到 worker 线程停 timer（必须 queued，QTimer 不能跨线程操作）
    // ② quit 让 worker 所在线程事件循环退出
    // ③ wait 阻塞到线程真的退出（保证 worker 不再 emit 信号）
    // ④ 删 ViewModel（GUI 线程对象）
    // ⑤ 删 QThread（worker 由 init 里连的 finished→deleteLater 兜底回收）
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
