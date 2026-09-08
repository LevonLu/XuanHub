// SysInfoPlugin.cpp — V2 系统采集插件的配置校验、线程初始化与回收
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

static QVariantList parseLayout(const QVariantMap& config) {
    QVariantList layout;
    const QVariant itemsVar = config.value(QStringLiteral("items"));
    if (itemsVar.isValid() && itemsVar.canConvert<QVariantList>()) {
        const QVariantList arr = itemsVar.toList();
        const auto colorOrEmpty = [](const QVariant& value) -> QString {
            const QString color = value.toString().trimmed();
            if (color.isEmpty()) return {};
            if (!QColor::isValidColorName(color)) {
                qWarning() << "[SysInfo] invalid color in config:" << color
                           << "-> fallback to theme";
                return {};
            }
            return color;
        };
        for (const QVariant& value : arr) {
            const QVariantMap itemConfig = value.toMap();
            const QString metric = itemConfig.value(QStringLiteral("metric")).toString();
            if (metric != QStringLiteral("cpu") && metric != QStringLiteral("mem")
                && metric != QStringLiteral("gpu") && metric != QStringLiteral("netDown")
                && metric != QStringLiteral("netUp")) continue;
            layout.append(QVariantMap{
                {QStringLiteral("metric"), metric},
                {QStringLiteral("prefix"), itemConfig.value(QStringLiteral("prefix")).toString()},
                {QStringLiteral("color"), colorOrEmpty(itemConfig.value(QStringLiteral("color")))},
            });
        }
    }
    return layout.isEmpty() ? defaultLayout() : layout;
}

xuanhub::PluginDescriptor SysInfoPlugin::descriptor() const {
    xuanhub::PluginDescriptor value;
    value.id = QStringLiteral("sysinfo");
    value.displayName = QStringLiteral("System Info");
    value.version = QStringLiteral("0.4.0");
    value.apiVersion = 2;
    value.configVersion = 1;
    value.minHostVersion = QStringLiteral("0.4.1");
    value.maxHostVersion = QStringLiteral("0.x");
    value.author = QStringLiteral("XuanHub");
    value.entry.backend = QStringLiteral("sysinfo.dll");
    value.entry.qml = QStringLiteral("SysInfo.qml");
    value.entry.settingsQml = QStringLiteral("SysInfoSettings.qml");
    value.capabilities = {QStringLiteral("barItem"), QStringLiteral("settings"),
                          QStringLiteral("popup")};
    value.actions = {{QStringLiteral("refresh"), QStringLiteral("立即刷新"),
                      {QStringLiteral("quickMenu"), QStringLiteral("tray"),
                       QStringLiteral("pluginMenu")}, QStringLiteral("refresh"), {}, false}};
    value.layout.declared = true;
    value.layout.minWidth = 80;
    value.layout.priority = 30;
    value.layout.overflow = QStringLiteral("menu");
    value.compatibility.architecture = QStringLiteral("x64");
    value.compatibility.qtVersion = QStringLiteral("6.9.3");
    value.compatibility.compiler = QStringLiteral("msvc2022");
    value.compatibility.crt = QStringLiteral("dynamic");
    return value;
}

PluginInitResult SysInfoPlugin::initialize(const PluginContext2& context) {
    if (m_vm || m_worker || m_thread)
        return PluginInitResult::failed(QStringLiteral("plugin is already initialized"));
    const auto validation = validateConfig(context.pluginConfig);
    if (!validation.valid) return PluginInitResult::failed(validation.error);
    const auto& cfg = context.pluginConfig;
    // 钳制采样间隔：0 会让 worker 线程每事件循环都 tick（PDH collect + GetIfTable2）
    // 占满一个核并狂发跨线程信号；负值 QTimer 直接不启动。200ms 下限兼顾流畅与开销
    m_intervalMs = qBound(200, cfg.value(QStringLiteral("intervalMs"), 1000).toInt(), 60000);
    qRegisterMetaType<SysInfoSnapshot>("SysInfoSnapshot");
    m_worker = new SysInfoWorker();
    m_vm = new SysInfoViewModel();
    m_vm->setLayout(parseLayout(cfg), cfg.value(QStringLiteral("itemsPerColumn"), 1).toInt());
    m_thread = new QThread();

    m_worker->moveToThread(m_thread.data());
    connect(m_worker.data(), &SysInfoWorker::metricsChanged,
            m_vm.data(),     &SysInfoViewModel::onMetricsChanged,
            Qt::AutoConnection);
    connect(m_vm.data(), &SysInfoViewModel::refreshRequested,
            m_worker.data(), &SysInfoWorker::refresh,
            Qt::QueuedConnection);
    connect(m_thread.data(), &QThread::finished,
            m_worker.data(), &QObject::deleteLater);

    m_thread->start();
    QMetaObject::invokeMethod(m_worker.data(), "start",
                              Qt::QueuedConnection,
                              Q_ARG(int, m_intervalMs));
    return PluginInitResult::succeeded();
}

PluginConfigValidationResult SysInfoPlugin::validateConfig(const QVariantMap& config) const {
    bool intervalOk = false;
    config.value(QStringLiteral("intervalMs"), 1000).toInt(&intervalOk);
    if (!intervalOk) return PluginConfigValidationResult::rejected(
        QStringLiteral("intervalMs must be an integer"));
    bool columnsOk = false;
    config.value(QStringLiteral("itemsPerColumn"), 1).toInt(&columnsOk);
    if (!columnsOk) return PluginConfigValidationResult::rejected(
        QStringLiteral("itemsPerColumn must be an integer"));
    return PluginConfigValidationResult::accepted();
}

PluginConfigApplyResult SysInfoPlugin::applyConfig(const QVariantMap& config) {
    if (!m_vm || !m_worker)
        return PluginConfigApplyResult::rejected(QStringLiteral("plugin is not initialized"));
    const auto validation = validateConfig(config);
    if (!validation.valid) return PluginConfigApplyResult::rejected(validation.error);
    const int interval = qBound(200,
        config.value(QStringLiteral("intervalMs"), 1000).toInt(), 60000);
    m_vm->setLayout(parseLayout(config),
                    config.value(QStringLiteral("itemsPerColumn"), 1).toInt());
    if (interval != m_intervalMs) {
        m_intervalMs = interval;
        QMetaObject::invokeMethod(m_worker.data(), "start", Qt::QueuedConnection,
                                  Q_ARG(int, m_intervalMs));
    }
    return PluginConfigApplyResult::applied();
}

QObject* SysInfoPlugin::viewModel() { return m_vm; }

QUrl SysInfoPlugin::qmlUrl() const { return QUrl(QStringLiteral("SysInfo.qml")); }

void SysInfoPlugin::shutdown() {
    // 严格五步（同 netstat 范式，顺序不可颠倒）：
    // ① 投递 stop 到 worker 线程停 timer（必须 queued，QTimer 不能跨线程操作）
    // ② quit 让 worker 所在线程事件循环退出
    // ③ wait 阻塞到线程真的退出（保证 worker 不再 emit 信号）
    // ④ 删 ViewModel（GUI 线程对象）
    // ⑤ 删 QThread（worker 由 initialize 里连的 finished→deleteLater 兜底回收）
    if (m_worker && m_thread && m_thread->isRunning()) {
        // 确认 stop 已在 worker 线程执行后才退出事件循环；只投递 QueuedConnection
        // 后立即 quit 会让退出事件抢在 stop 前处理，资源释放时序不可证明。
        QMetaObject::invokeMethod(m_worker.data(), "stop", Qt::BlockingQueuedConnection);
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        if (!m_thread->wait(2000)) {
            // 绝不能在 QThread 仍运行时 delete：插件 DLL 随后被卸载会让 worker
            // 落入已卸载代码。先报告异常，再等待真实退出，优先保证宿主不崩溃。
            qCritical() << "[SysInfo] worker shutdown exceeded 2s; waiting for safe exit";
            m_thread->wait();
        }
    }
    if (m_vm) { delete m_vm; m_vm = nullptr; }
    // worker deleted by QThread::finished → deleteLater
    if (m_thread) { delete m_thread; m_thread = nullptr; }
}
