// PyWorkerViewModel.cpp — Python worker 的异步本机 IPC 实现
#include "PyWorkerViewModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <utility>

#include "barplugin/ProcessReaper.h"
#include "barplugin/PluginLog.h"
#include "barplugin/EventBus.h"
#include "barplugin/HostServices.h"
#include "barplugin/IBarPluginHostConfigValidation.h"
#include "barplugin/PluginEventScope.h"

namespace {
constexpr int kStartTimeoutMs = 10000;
constexpr int kRequestTimeoutMs = 15000;
constexpr int kDefaultUpdateIntervalMs = 1000;
constexpr int kMinUpdateIntervalMs = 100;
constexpr int kMaxUpdateIntervalMs = 60000;
constexpr int kCircuitFailureLimit = 5;
constexpr qint64 kCircuitFailureWindowMs = 10 * 60 * 1000;
constexpr int kMaxQueuedEvents = 128;
constexpr int kMaxQueuedPluginEvents = 128;
constexpr int kValuePatchFrameMs = 16;
constexpr int kPluginEventBatchSize = 16;
constexpr int kMaxEventRequestTimeoutMs = 10000;

QString processStatusText(QProcess::ExitStatus status) {
    return status == QProcess::CrashExit ? QStringLiteral("crash") : QStringLiteral("normal");
}

QString workerFailureCode(const QString& reason, const QString& callErrorCode) {
    if (callErrorCode == QLatin1String("timeout")) return QStringLiteral("worker.timeout");
    if (!callErrorCode.isEmpty())
        return QStringLiteral("worker.%1").arg(callErrorCode.toLower());
    if (reason.contains(QStringLiteral("connection timed out")))
        return QStringLiteral("worker.start_timeout");
    if (reason.contains(QStringLiteral("IPC disconnected")))
        return QStringLiteral("worker.ipc_disconnected");
    if (reason.contains(QStringLiteral("cannot write")))
        return QStringLiteral("worker.ipc_write_failed");
    if (reason.contains(QStringLiteral("cannot start")))
        return QStringLiteral("worker.start_failed");
    if (reason.contains(QStringLiteral("exited")))
        return QStringLiteral("worker.exited");
    if (reason.contains(QStringLiteral("undeclared push capability")))
        return QStringLiteral("worker.capability_violation");
    if (reason.contains(QStringLiteral("frame rejected")))
        return QStringLiteral("worker.frame_rejected");
    if (reason.contains(QStringLiteral("mismatch"))
        || reason.contains(QStringLiteral("invalid"))
        || reason.contains(QStringLiteral("unexpected")))
        return QStringLiteral("worker.protocol_error");
    return QStringLiteral("worker.failure");
}
} // namespace

PyWorkerViewModel::PyWorkerViewModel(QString id, QString dirPath, QVariantMap cfg,
                                     QString pythonExe,
                                     QString workerScript, xuanhub::EventBus* bus,
                                     xuanhub::PluginEventScope* eventScope,
                                     QObject* parent)
    : QObject(parent)
    , m_id(std::move(id))
    , m_dirPath(std::move(dirPath))
    , m_cfg(std::move(cfg))
    , m_pythonExe(std::move(pythonExe))
    , m_workerScript(std::move(workerScript)) {
    bool intervalOk = false;
    const int configuredInterval = m_cfg.value(
        QStringLiteral("intervalMs"), kDefaultUpdateIntervalMs).toInt(&intervalOk);
    m_updateIntervalMs = qBound(kMinUpdateIntervalMs,
                                intervalOk ? configuredInterval : kDefaultUpdateIntervalMs,
                                kMaxUpdateIntervalMs);
    m_failureClock.start();
    xuanhub::EventSubscriptionOptions localeOptions;
    localeOptions.replayRetained = true;
    // Worker 可能在下一轮事件循环前立即启动；先同步读取一次 retained state，
    // 确保 init/set_locale 使用当前语言，再订阅后续热切换。
    if (bus) onLocaleChanged(bus->state(QStringLiteral("app.locale")));
    if (eventScope) {
        eventScope->subscribeV2(QStringLiteral("app.locale"), this,
                                "onLocaleChanged(QVariantMap)", localeOptions);
    } else if (bus) {
        bus->subscribeV2(QStringLiteral("app.locale"), this,
                         "onLocaleChanged(QVariantMap)", localeOptions);
    }
    m_startTimer.setSingleShot(true);
    m_requestTimer.setSingleShot(true);
    m_updateTimer.setSingleShot(true);
    m_valuePatchTimer.setSingleShot(true);
    m_pluginEventTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        failWorker(QStringLiteral("worker connection timed out"), true);
    });
    connect(&m_requestTimer, &QTimer::timeout, this, &PyWorkerViewModel::handleRequestTimeout);
    connect(&m_updateTimer, &QTimer::timeout, this, [this] {
        enqueue({QStringLiteral("update"), {}});
    });
    connect(&m_valuePatchTimer, &QTimer::timeout,
            this, &PyWorkerViewModel::flushValuePatch);
    connect(&m_pluginEventTimer, &QTimer::timeout,
            this, &PyWorkerViewModel::flushPluginEvents);
}

void PyWorkerViewModel::onLocaleChanged(QVariantMap payload) {
    const QVariantMap data = payload.value(QStringLiteral("data")).toMap();
    const QString locale = (data.isEmpty() ? payload : data)
                               .value(QStringLiteral("locale")).toString();
    if (locale.isEmpty() || locale == m_locale) return;
    m_locale = locale;
    if (m_authenticated) {
        enqueue({QStringLiteral("set_locale"), {
            {QStringLiteral("locale"), m_locale}
        }}, true);
    }
}

PyWorkerViewModel::~PyWorkerViewModel() {
    stopWorker();
}

bool PyWorkerViewModel::running() const {
    return m_authenticated && m_process && m_process->state() != QProcess::NotRunning;
}

QVariantMap PyWorkerViewModel::health() const {
    return {
        {QStringLiteral("state"), xuanhub::pluginStateKey(m_health.state)},
        {QStringLiteral("lastError"), m_health.lastError},
        {QStringLiteral("lastErrorCode"), m_health.lastErrorCode},
        {QStringLiteral("lastErrorAt"), m_health.lastErrorAt},
        {QStringLiteral("startedAt"), m_health.startedAt},
        {QStringLiteral("lastHealthyAt"), m_health.lastHealthyAt},
        {QStringLiteral("restartCount"), m_health.restartCount},
        {QStringLiteral("consecutiveFailures"), consecutiveFailures()},
        {QStringLiteral("loadDurationMs"), m_health.loadDurationMs},
        {QStringLiteral("queueDepth"), m_queue.size() + (m_inFlightId != 0 ? 1 : 0)},
        {QStringLiteral("processId"), m_process ? m_process->processId() : 0},
        {QStringLiteral("protocolVersion"), m_protocolVersion},
        {QStringLiteral("protocolCapabilities"), m_protocolCapabilities},
        {QStringLiteral("usesPushValues"), m_usesPushValues},
    };
}

bool PyWorkerViewModel::start() {
    if (m_stopping || m_circuitOpen || m_process || m_server) return false;
    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 0)) {
        m_health.lastError = QStringLiteral("cannot listen for worker: %1")
                                 .arg(server->errorString());
        m_health.lastErrorAt = QDateTime::currentDateTimeUtc();
        setHealthState(xuanhub::PluginState::Failed);
        qWarning() << "[PyBridge]" << m_id << ":" << m_health.lastError;
        server->deleteLater();
        return false;
    }
    m_health.startedAt = QDateTime::currentDateTimeUtc();
    m_health.loadDurationMs = 0;
    m_startupClock.start();
    setHealthState(xuanhub::PluginState::Starting);
    m_server = server;
    m_token = nextToken();
    m_authenticated = false;
    m_frameCodec.clear();
    m_protocolVersion = 0;
    m_protocolCapabilities.clear();
    m_usesPushValues = false;
    m_configInitialized = false;
    m_queue.clear();
    m_eventQueue.clear();
    m_pluginEventQueue.clear();
    m_pendingValuePatch.clear();
    m_pushedValues.clear();
    m_valuePatchTimer.stop();
    m_pluginEventTimer.stop();
    m_lastPushSequence = 0;
    m_eventRequestIds.clear();
    m_eventDeliveryInFlight.clear();
    m_inFlight = {};
    m_inFlightId = 0;
    connect(server, &QTcpServer::newConnection, this, &PyWorkerViewModel::acceptConnection);

    auto* process = new QProcess;
    m_process = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // worker 使用自身解释器定位标准库，避免宿主的 PYTHONHOME/PYTHONPATH 污染子进程。
    environment.remove(QStringLiteral("PYTHONHOME"));
    environment.remove(QStringLiteral("PYTHONPATH"));
    environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    process->setProcessEnvironment(environment);
    const QString port = QString::number(server->serverPort());
    process->setProgram(m_pythonExe);
    process->setArguments({
        QStringLiteral("-E"), QStringLiteral("-u"), m_workerScript,
        QStringLiteral("--port"), port,
        QStringLiteral("--token"), m_token,
        QStringLiteral("--plugin-dir"), m_dirPath,
        QStringLiteral("--plugin-id"), m_id,
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (process != m_process) return;
        const QString output = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (!output.isEmpty()) qWarning().noquote() << "[PyWorker]" << m_id << output;
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        if (process != m_process) return;
        handleProcessError();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        if (process != m_process) {
            process->deleteLater();
            return;
        }
        handleProcessFinished(exitCode, status);
        process->deleteLater();
    });
    process->start();
    m_startTimer.start(kStartTimeoutMs);
    Q_EMIT runningChanged();
    return true;
}

QVariant PyWorkerViewModel::call(const QString& name, const QVariantList& args) {
    if (!running()) {
        return QVariantMap{
            {QStringLiteral("queued"), false},
            {QStringLiteral("id"), QString{}},
            {QStringLiteral("errorCode"), QStringLiteral("worker_not_running")},
        };
    }
    const QString callId = QStringLiteral("call-%1").arg(m_nextCallId++);
    enqueue({QStringLiteral("invoke"), {
        {QStringLiteral("name"), name},
        {QStringLiteral("args"), QJsonArray::fromVariantList(args)},
    }, callId});
    return QVariantMap{
        {QStringLiteral("queued"), true},
        {QStringLiteral("id"), callId},
        {QStringLiteral("errorCode"), QString{}},
    };
}

QVariantMap PyWorkerViewModel::cancelCall(const QString& callId) {
    if (callId.isEmpty()) {
        return {{QStringLiteral("canceled"), false},
                {QStringLiteral("errorCode"), QStringLiteral("invalid_call_id")}};
    }
    if (m_inFlight.callId == callId) {
        // CPython 运行中无法安全注入异步异常；强杀会破坏插件资源清理和状态一致性。
        return {{QStringLiteral("canceled"), false},
                {QStringLiteral("errorCode"), QStringLiteral("not_interruptible")}};
    }
    for (qsizetype index = 0; index < m_queue.size(); ++index) {
        if (m_queue.at(index).callId != callId) continue;
        m_queue.removeAt(index);
        const QVariantMap error{
            {QStringLiteral("code"), QStringLiteral("canceled")},
            {QStringLiteral("message"), QStringLiteral("call canceled before execution")},
        };
        Q_EMIT callFinished(callId, false, {}, error);
        Q_EMIT healthChanged();
        return {{QStringLiteral("canceled"), true},
                {QStringLiteral("errorCode"), QString{}}};
    }
    return {{QStringLiteral("canceled"), false},
            {QStringLiteral("errorCode"), QStringLiteral("call_not_found")}};
}

bool PyWorkerViewModel::restart() {
    if (m_stopping) return false;

    // stopWorker 会解除所有旧 IPC/进程回调，再恢复可启动状态。旧进程即使仍在
    // ProcessReaper 的宽限期内也无法连接新端口或携带新令牌。
    stopWorker();
    m_stopping = false;
    m_restartAttempt = 0;
    if (!m_failureTimesMs.isEmpty()) {
        m_failureTimesMs.clear();
        Q_EMIT consecutiveFailuresChanged();
    }
    if (m_circuitOpen) {
        m_circuitOpen = false;
        Q_EMIT circuitOpenChanged();
    }
    return start();
}

xuanhub::HostedPluginConfigValidationResult PyWorkerViewModel::validateConfig(
    const QVariantMap& config, int timeoutMs) {
    if (!m_authenticated || !m_supportsConfigValidation || m_stopping)
        return {};
    if (m_validationWaiting) {
        return {true, false,
                QStringLiteral("another config validation is already running")};
    }

    m_validationWaiting = true;
    xuanhub::HostedPluginConfigValidationResult result{
        true, false, QStringLiteral("config validation timed out")};
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &PyWorkerViewModel::configValidationFinished, &loop,
            [&result, &loop](bool valid, const QString& error) {
                result.valid = valid;
                result.error = error;
                loop.quit();
            });
    enqueue({QStringLiteral("validate_config"), {
        {QStringLiteral("config"), QJsonObject::fromVariantMap(config)}
    }}, true);
    timeout.start(qBound(100, timeoutMs, kRequestTimeoutMs));
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    m_validationWaiting = false;
    return result;
}

bool PyWorkerViewModel::applyConfig(const QVariantMap& config) {
    if (!m_authenticated || !m_supportsHotConfig || m_stopping) return false;
    enqueue({QStringLiteral("set_config"), {
        {QStringLiteral("config"), QJsonObject::fromVariantMap(config)}
    }});
    return true;
}

void PyWorkerViewModel::setConfigVersions(int currentVersion,
                                          int targetVersion) {
    if (m_process || m_server) return;
    m_configVersion = qMax(1, currentVersion);
    m_targetConfigVersion = qMax(1, targetVersion);
}

void PyWorkerViewModel::acceptConnection() {
    if (!m_server) return;
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        if (m_socket) {
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        connect(socket, &QTcpSocket::readyRead, this, &PyWorkerViewModel::readMessages);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            if (socket != m_socket) return;
            m_socket = nullptr;
            if (!m_stopping) failWorker(QStringLiteral("worker IPC disconnected"), true);
        });
    }
}

void PyWorkerViewModel::readMessages() {
    if (!m_socket) return;
    m_frameCodec.append(m_socket->readAll());
    while (true) {
        QJsonObject message;
        QString error;
        const auto result = m_frameCodec.takeNext(&message, &error);
        if (result == xuanhub::pybridge::DecodeResult::NeedMore) return;
        if (result == xuanhub::pybridge::DecodeResult::Error) {
            failWorker(QStringLiteral("worker IPC frame rejected: %1").arg(error), false);
            return;
        }
        handleMessage(message);
        if (!m_socket) return;
    }
}

void PyWorkerViewModel::handleMessage(const QJsonObject& message) {
    if (!m_authenticated) {
        xuanhub::pybridge::WorkerHello hello;
        QString helloError;
        if (!xuanhub::pybridge::parseWorkerHello(
                message, m_token, m_id, &hello, &helloError)) {
            // 端口只绑定 loopback，但仍拒绝意外本机连接；真正 worker 仍可在超时前接入。
            qWarning() << "[PyBridge]" << m_id
                       << ": rejected worker hello:" << helloError;
            if (m_socket) {
                QTcpSocket* socket = m_socket;
                m_socket = nullptr;
                socket->disconnectFromHost();
                socket->deleteLater();
            }
            return;
        }
        m_protocolVersion = hello.protocolVersion;
        m_protocolCapabilities = hello.capabilities;
        m_authenticated = true;
        m_startTimer.stop();
        if (m_server) m_server->close();
        Q_EMIT runningChanged();
        enqueue({QStringLiteral("init"), {
            {QStringLiteral("config"), QJsonObject::fromVariantMap(m_cfg)},
            {QStringLiteral("configVersion"), m_configVersion},
            {QStringLiteral("targetConfigVersion"), m_targetConfigVersion},
            {QStringLiteral("locale"), m_locale},
            {QStringLiteral("protocol"), m_protocolVersion},
            {QStringLiteral("hostCapabilities"), QJsonArray{
                QStringLiteral("requestResponseV1"),
                QStringLiteral("callResults"),
                QStringLiteral("callProgress"),
                QStringLiteral("cancelQueued"),
                QStringLiteral("pushValues"),
                QStringLiteral("pushEvents"),
                QStringLiteral("hostServicesV1"),
                QStringLiteral("eventBusV2")}}
        }});
        qDebug() << "[PyBridge]" << m_id << ": protocol" << m_protocolVersion
                 << "capabilities=" << m_protocolCapabilities;
        return;
    }

    if (message.value(QStringLiteral("type")).toString()
        == QLatin1String("host_service")) {
        handleHostServiceRequest(message);
        return;
    }
    if (message.value(QStringLiteral("type")).toString()
        == QLatin1String("values_patch")) {
        handleValuesPatch(message);
        return;
    }
    if (message.value(QStringLiteral("type")).toString()
        == QLatin1String("plugin_event")) {
        handlePluginEvent(message);
        return;
    }
    if (message.value(QStringLiteral("type")).toString()
        == QLatin1String("event_bus")) {
        handleEventBusRequest(message);
        return;
    }
    if (message.value(QStringLiteral("type")).toString()
        == QLatin1String("event_bus_event_ack")) {
        if (message.value(QStringLiteral("id")).toString() != m_eventDeliveryInFlight) {
            failWorker(QStringLiteral("worker event acknowledgement mismatch"), false);
            return;
        }
        m_eventDeliveryInFlight.clear();
        flushEventBusEvents();
        dispatchNext();
        return;
    }
    if (message.value(QStringLiteral("type")).toString()
        == QLatin1String("call_progress")) {
        bool progressOk = false;
        const double progress = message.value(QStringLiteral("progress")).toVariant()
                                    .toDouble(&progressOk);
        if (m_inFlight.op != QLatin1String("invoke")
            || m_inFlight.callId.isEmpty()
            || message.value(QStringLiteral("id")).toString()
                != QString::number(m_inFlightId)
            || !progressOk || progress < 0.0 || progress > 1.0
            || !message.value(QStringLiteral("details")).isObject()) {
            failWorker(QStringLiteral("worker call progress message is invalid"), false);
            return;
        }
        Q_EMIT callProgress(
            m_inFlight.callId, progress,
            message.value(QStringLiteral("details")).toObject().toVariantMap());
        return;
    }

    if (message.value(QStringLiteral("type")).toString() != QLatin1String("response")) {
        failWorker(QStringLiteral("worker IPC returned an unexpected message"), false);
        return;
    }
    bool okId = false;
    const quint64 id = message.value(QStringLiteral("id")).toString().toULongLong(&okId);
    if (!okId || id != m_inFlightId) {
        failWorker(QStringLiteral("worker IPC response id mismatch"), false);
        return;
    }
    const QString op = m_inFlight.op;
    const QJsonObject requestData = m_inFlight.data;
    const QString callId = m_inFlight.callId;
    m_inFlight = {};
    m_inFlightId = 0;
    m_requestTimer.stop();
    Q_EMIT healthChanged();

    const bool ok = message.value(QStringLiteral("ok")).toBool(false);
    if (!ok) {
        qWarning().noquote() << "[PyBridge]" << m_id << op << "failed:"
                             << message.value(QStringLiteral("error")).toString();
        if (op == QLatin1String("set_config")) {
            Q_EMIT configApplied(m_id,
                requestData.value(QStringLiteral("config")).toObject().toVariantMap(),
                false, message.value(QStringLiteral("error")).toString());
        } else if (op == QLatin1String("validate_config")) {
            Q_EMIT configValidationFinished(
                false, message.value(QStringLiteral("error")).toString());
        } else if (op == QLatin1String("init")) {
            m_configInitialized = true;
            Q_EMIT configInitialized(m_id, false, message.value(QStringLiteral("error")).toString());
            if (m_configVersion != m_targetConfigVersion) {
                Q_EMIT configMigrationFailed(
                    m_id, m_configVersion, m_targetConfigVersion,
                    message.value(QStringLiteral("error")).toString());
            }
            failWorker(QStringLiteral("worker failed during plugin initialization"), false);
            return;
        } else if (op == QLatin1String("invoke") && !callId.isEmpty()) {
            const QVariantMap error{
                {QStringLiteral("code"),
                 message.value(QStringLiteral("errorCode"))
                     .toString(QStringLiteral("plugin_error"))},
                {QStringLiteral("message"),
                 message.value(QStringLiteral("error")).toString()},
                {QStringLiteral("details"),
                 message.value(QStringLiteral("details")).toObject().toVariantMap()},
            };
            Q_EMIT callFinished(callId, false, {}, error);
        }
    } else if (op == QLatin1String("init")) {
        const QJsonObject initResult = message.value(QStringLiteral("result")).toObject();
        const QJsonArray capabilities = initResult
                                            .value(QStringLiteral("capabilities"))
                                            .toArray();
        m_supportsHotConfig = capabilities.contains(QStringLiteral("set_config"));
        m_supportsConfigValidation = capabilities.contains(
            QStringLiteral("validate_config"));
        m_usesPushValues = capabilities.contains(QStringLiteral("push_values"));
        const QJsonObject migration = initResult.value(QStringLiteral("migration")).toObject();
        if (migration.value(QStringLiteral("required")).toBool(false)) {
            const QVariantMap migratedConfig = migration.value(QStringLiteral("config"))
                                                   .toObject().toVariantMap();
            m_cfg = migratedConfig;
            Q_EMIT configMigrationReady(
                m_id, m_configVersion, m_targetConfigVersion, migratedConfig);
        }
        m_configInitialized = true;
        Q_EMIT configInitialized(m_id, true, {});
    } else if (op == QLatin1String("validate_config")) {
        const QJsonObject validation = message.value(QStringLiteral("result")).toObject();
        Q_EMIT configValidationFinished(
            validation.value(QStringLiteral("valid")).toBool(false),
            validation.value(QStringLiteral("error")).toString());
    } else if (op == QLatin1String("set_config")) {
        m_cfg = requestData.value(QStringLiteral("config")).toObject().toVariantMap();
        bool intervalOk = false;
        const int configuredInterval = m_cfg.value(
            QStringLiteral("intervalMs"), kDefaultUpdateIntervalMs).toInt(&intervalOk);
        m_updateIntervalMs = qBound(kMinUpdateIntervalMs,
                                    intervalOk ? configuredInterval : kDefaultUpdateIntervalMs,
                                    kMaxUpdateIntervalMs);
        Q_EMIT configApplied(m_id, m_cfg, true, {});
    } else if (op == QLatin1String("update")) {
        const QJsonValue result = message.value(QStringLiteral("result"));
        if (!result.isObject()) {
            qWarning() << "[PyBridge]" << m_id << ": update() must return a dict";
        } else {
            QVariantMap next = result.toObject().toVariantMap();
            // V2 push 是独立覆盖层；兼容周期 pull 不得删掉只由 push 提供的字段。
            for (auto it = m_pushedValues.constBegin();
                 it != m_pushedValues.constEnd(); ++it) {
                next.insert(it.key(), it.value());
            }
            if (next != m_values) {
                m_values = next;
                Q_EMIT valuesChanged();
            }
            // 仅在真正完成一次 update 后认定 worker 已恢复，避免初始化成功但随即
            // 崩溃的插件绕过退避；健康运行后下一次故障从 1 秒重新尝试。
            markWorkerHealthy();
        }
    } else if (op == QLatin1String("invoke") && !callId.isEmpty()) {
        Q_EMIT callFinished(callId, true,
                            message.value(QStringLiteral("result")).toVariant(), {});
    }

    flushEventBusEvents();
    if (op == QLatin1String("init") || op == QLatin1String("invoke")
        || op == QLatin1String("set_locale") || op == QLatin1String("set_config"))
        scheduleUpdate(0);
    if (op == QLatin1String("update") && !m_usesPushValues) scheduleUpdate();
    dispatchNext();
}

bool PyWorkerViewModel::acceptPushSequence(const QJsonObject& message,
                                           const QString& capability) {
    if (m_protocolVersion < 2 || !m_protocolCapabilities.contains(capability)) {
        failWorker(QStringLiteral("worker used undeclared push capability: %1")
                       .arg(capability), false);
        return false;
    }
    bool ok = false;
    const quint64 sequence = message.value(QStringLiteral("sequence"))
                                 .toVariant().toULongLong(&ok);
    if (!ok || sequence == 0) {
        failWorker(QStringLiteral("worker push sequence is invalid"), false);
        return false;
    }
    if (sequence <= m_lastPushSequence) {
        qWarning() << "[PyBridge]" << m_id << ": ignored stale push sequence"
                   << sequence << "after" << m_lastPushSequence;
        return false;
    }
    m_lastPushSequence = sequence;
    return true;
}

void PyWorkerViewModel::handleValuesPatch(const QJsonObject& message) {
    if (!message.value(QStringLiteral("patch")).isObject()) {
        failWorker(QStringLiteral("worker values patch must be an object"), false);
        return;
    }
    if (!acceptPushSequence(message, QStringLiteral("pushValues"))) return;
    const QVariantMap patch = message.value(QStringLiteral("patch"))
                                  .toObject().toVariantMap();
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        m_pendingValuePatch.insert(it.key(), it.value());
        m_pushedValues.insert(it.key(), it.value());
    }
    if (!m_valuePatchTimer.isActive()) m_valuePatchTimer.start(kValuePatchFrameMs);
}

void PyWorkerViewModel::handlePluginEvent(const QJsonObject& message) {
    const QString event = message.value(QStringLiteral("event")).toString();
    if (event.isEmpty() || event.size() > 128
        || !message.value(QStringLiteral("data")).isObject()) {
        failWorker(QStringLiteral("worker plugin event is invalid"), false);
        return;
    }
    if (!acceptPushSequence(message, QStringLiteral("pushEvents"))) return;
    if (m_pluginEventQueue.size() >= kMaxQueuedPluginEvents) {
        qWarning() << "[PyBridge]" << m_id
                   << ": plugin event queue full, dropping newest event";
        return;
    }
    m_pluginEventQueue.enqueue({
        event, message.value(QStringLiteral("data")).toObject().toVariantMap()});
    if (!m_pluginEventTimer.isActive()) m_pluginEventTimer.start(0);
}

void PyWorkerViewModel::flushValuePatch() {
    if (m_pendingValuePatch.isEmpty()) return;
    QVariantMap next = m_values;
    for (auto it = m_pendingValuePatch.constBegin();
         it != m_pendingValuePatch.constEnd(); ++it) {
        next.insert(it.key(), it.value());
    }
    m_pendingValuePatch.clear();
    if (next == m_values) return;
    m_values = std::move(next);
    Q_EMIT valuesChanged();
}

void PyWorkerViewModel::flushPluginEvents() {
    int delivered = 0;
    while (!m_pluginEventQueue.isEmpty() && delivered < kPluginEventBatchSize) {
        const auto event = m_pluginEventQueue.dequeue();
        Q_EMIT pluginEvent(event.first, event.second);
        ++delivered;
    }
    if (!m_pluginEventQueue.isEmpty()) m_pluginEventTimer.start(0);
}

void PyWorkerViewModel::onEventBusEnvelope(QVariantMap envelope) {
    if (m_eventQueue.size() >= kMaxQueuedEvents) {
        qWarning() << "[PyBridge]" << m_id
                   << ": EventBus queue full, dropping newest event";
        return;
    }
    const QString deliveryId = QStringLiteral("event-%1").arg(m_nextEventDeliveryId++);
    m_eventQueue.enqueue({
        {QStringLiteral("type"), QStringLiteral("event_bus_event")},
        {QStringLiteral("id"), deliveryId},
        {QStringLiteral("topic"), envelope.value(QStringLiteral("topic")).toString()},
        {QStringLiteral("envelope"), QJsonObject::fromVariantMap(envelope)},
    });
    flushEventBusEvents();
}

void PyWorkerViewModel::onEventBusRequestResult(QVariantMap result) {
    const QString busRequestId = result.value(QStringLiteral("requestId")).toString();
    const QString workerRequestId = m_eventRequestIds.take(busRequestId);
    if (workerRequestId.isEmpty() || !m_authenticated) return;
    if (!sendMessage({
            {QStringLiteral("type"), QStringLiteral("event_bus_response")},
            {QStringLiteral("id"), workerRequestId},
            {QStringLiteral("ok"), true},
            {QStringLiteral("result"), QJsonObject::fromVariantMap(result)},
        })) {
        failWorker(QStringLiteral("cannot write EventBus request result"), true);
    }
}

void PyWorkerViewModel::handleEventBusRequest(const QJsonObject& message) {
    const QString workerRequestId = message.value(QStringLiteral("id")).toString();
    auto respond = [this, &workerRequestId](bool ok, const QVariantMap& result,
                                            const QString& errorCode = {},
                                            const QString& error = {}) {
        QJsonObject response{
            {QStringLiteral("type"), QStringLiteral("event_bus_response")},
            {QStringLiteral("id"), workerRequestId},
            {QStringLiteral("ok"), ok},
        };
        if (ok) response.insert(QStringLiteral("result"),
                                QJsonObject::fromVariantMap(result));
        else {
            response.insert(QStringLiteral("errorCode"), errorCode);
            response.insert(QStringLiteral("error"), error);
        }
        if (!sendMessage(response))
            failWorker(QStringLiteral("cannot write EventBus response"), true);
    };

    if (workerRequestId.isEmpty() || workerRequestId.size() > 64
        || message.contains(QStringLiteral("pluginId"))) {
        respond(false, {}, QStringLiteral("invalid_request"),
                QStringLiteral("invalid EventBus request identity"));
        return;
    }
    if (!m_pluginEventScope) {
        respond(false, {}, QStringLiteral("service_unavailable"),
                QStringLiteral("plugin EventBus scope is not bound"));
        return;
    }
    const QString operation = message.value(QStringLiteral("operation")).toString();
    const QJsonObject args = message.value(QStringLiteral("args")).toObject();
    const QString topic = args.value(QStringLiteral("topic")).toString();
    const QVariantMap data = args.value(QStringLiteral("data")).toObject().toVariantMap();

    if (operation == QLatin1String("publish")) {
        const bool ok = m_pluginEventScope->publish(topic, data);
        respond(ok, {{QStringLiteral("published"), ok}},
                ok ? QString{} : QStringLiteral("permission_denied"),
                ok ? QString{} : QStringLiteral("EventBus publish rejected"));
        return;
    }
    if (operation == QLatin1String("subscribe")) {
        xuanhub::EventSubscriptionOptions options;
        options.replayRetained = args.value(QStringLiteral("replayRetained")).toBool(false);
        const bool ok = m_pluginEventScope->subscribeV2(
            topic, this, "onEventBusEnvelope(QVariantMap)", options);
        respond(ok, {{QStringLiteral("subscribed"), ok}},
                ok ? QString{} : QStringLiteral("permission_denied"),
                ok ? QString{} : QStringLiteral("EventBus subscription rejected"));
        return;
    }
    if (operation == QLatin1String("state")) {
        respond(true, {{QStringLiteral("envelope"),
                        m_pluginEventScope->state(topic)}});
        return;
    }
    if (operation == QLatin1String("request")) {
        const int timeoutMs = std::clamp(
            args.value(QStringLiteral("timeoutMs")).toInt(5000), 1,
            kMaxEventRequestTimeoutMs);
        const QString busRequestId = m_pluginEventScope->request(
            topic, data, this, "onEventBusRequestResult(QVariantMap)", timeoutMs);
        if (busRequestId.isEmpty()) {
            respond(false, {}, QStringLiteral("request_rejected"),
                    QStringLiteral("EventBus request rejected"));
        } else {
            m_eventRequestIds.insert(busRequestId, workerRequestId);
        }
        return;
    }
    if (operation == QLatin1String("respond")) {
        const bool ok = m_pluginEventScope->respond(
            args.value(QStringLiteral("requestId")).toString(), data,
            args.value(QStringLiteral("errorCode")).toString(),
            args.value(QStringLiteral("errorMessage")).toString());
        respond(ok, {{QStringLiteral("responded"), ok}},
                ok ? QString{} : QStringLiteral("response_rejected"),
                ok ? QString{} : QStringLiteral("EventBus response rejected"));
        return;
    }
    respond(false, {}, QStringLiteral("unsupported_operation"),
            QStringLiteral("unsupported EventBus operation"));
}

void PyWorkerViewModel::flushEventBusEvents() {
    if (!m_authenticated || m_stopping || m_inFlightId != 0
        || !m_eventDeliveryInFlight.isEmpty() || m_eventQueue.isEmpty()) {
        return;
    }
    const QJsonObject event = m_eventQueue.dequeue();
    m_eventDeliveryInFlight = event.value(QStringLiteral("id")).toString();
    if (!sendMessage(event)) {
        m_eventDeliveryInFlight.clear();
        failWorker(QStringLiteral("cannot write EventBus event"), true);
    }
}

void PyWorkerViewModel::handleHostServiceRequest(const QJsonObject& message) {
    const QString requestId = message.value(QStringLiteral("id")).toString();
    auto respond = [this, &requestId](const xuanhub::HostServiceResult& result) {
        QJsonObject response{
            {QStringLiteral("type"), QStringLiteral("host_service_response")},
            {QStringLiteral("id"), requestId},
            {QStringLiteral("ok"), result.success},
        };
        if (result.success) {
            response.insert(QStringLiteral("result"),
                            QJsonValue::fromVariant(result.value));
        } else {
            response.insert(QStringLiteral("errorCode"), result.errorCode);
            response.insert(QStringLiteral("error"), result.errorMessage);
        }
        if (!sendMessage(response))
            failWorker(QStringLiteral("cannot write host service response"), true);
    };

    if (requestId.isEmpty() || requestId.size() > 64 || m_inFlightId == 0) {
        respond(xuanhub::HostServiceResult::failed(
            QStringLiteral("invalid_request"),
            QStringLiteral("host service request is outside an active plugin call")));
        return;
    }
    // 身份只来自本 ViewModel 的 m_id 和核心绑定的服务对象，Worker 不得自报 ID。
    if (message.contains(QStringLiteral("pluginId"))) {
        respond(xuanhub::HostServiceResult::failed(
            QStringLiteral("identity_not_allowed"),
            QStringLiteral("worker must not provide a plugin identity")));
        return;
    }
    if (!m_hostServices) {
        respond(xuanhub::HostServiceResult::failed(
            QStringLiteral("service_unavailable"),
            QStringLiteral("host services are not bound")));
        return;
    }

    const QString service = message.value(QStringLiteral("service")).toString();
    const QString method = message.value(QStringLiteral("method")).toString();
    const QJsonObject args = message.value(QStringLiteral("args")).toObject();
    if (service == QLatin1String("logger") && method == QLatin1String("log")) {
        const QString level = args.value(QStringLiteral("level")).toString();
        const QString text = args.value(QStringLiteral("message")).toString();
        if (text.size() > 8192) {
            respond(xuanhub::HostServiceResult::failed(
                QStringLiteral("invalid_argument"),
                QStringLiteral("log message exceeds 8192 characters")));
            return;
        }
        xuanhub::PluginLogLevel parsed = xuanhub::PluginLogLevel::Info;
        if (level == QLatin1String("debug")) parsed = xuanhub::PluginLogLevel::Debug;
        else if (level == QLatin1String("warning")) parsed = xuanhub::PluginLogLevel::Warning;
        else if (level == QLatin1String("error")) parsed = xuanhub::PluginLogLevel::Error;
        m_hostServices->logger()->log(
            parsed, text,
            args.value(QStringLiteral("fields")).toObject().toVariantMap());
        respond(xuanhub::HostServiceResult::succeeded());
        return;
    }
    if (service == QLatin1String("notifications")
        && method == QLatin1String("show")) {
        const QString title = args.value(QStringLiteral("title")).toString();
        const QString text = args.value(QStringLiteral("message")).toString();
        if (title.size() > 256 || text.size() > 4096) {
            respond(xuanhub::HostServiceResult::failed(
                QStringLiteral("invalid_argument"),
                QStringLiteral("notification text exceeds the protocol limit")));
            return;
        }
        respond(m_hostServices->notifications()->show(title, text));
        return;
    }
    respond(xuanhub::HostServiceResult::failed(
        QStringLiteral("unsupported_service"),
        QStringLiteral("unsupported host service operation: %1.%2")
            .arg(service, method)));
}

bool PyWorkerViewModel::sendMessage(const QJsonObject& message) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return false;
    QString error;
    const QByteArray frame = xuanhub::pybridge::FrameCodec::encode(message, &error);
    if (frame.isEmpty()) {
        qWarning() << "[PyBridge]" << m_id << ": cannot encode IPC frame:" << error;
        return false;
    }
    return m_socket->write(frame) == frame.size();
}

void PyWorkerViewModel::enqueue(Request request, bool front) {
    if (!m_authenticated || m_stopping) return;
    if (request.op == QLatin1String("update")) {
        if (m_inFlight.op == QLatin1String("update")) return;
        for (const Request& queued : std::as_const(m_queue)) {
            if (queued.op == QLatin1String("update")) return;
        }
    }
    if (front) m_queue.prepend(std::move(request));
    else m_queue.enqueue(std::move(request));
    Q_EMIT healthChanged();
    dispatchNext();
}

void PyWorkerViewModel::dispatchNext() {
    if (!m_authenticated || m_stopping || m_inFlightId != 0
        || !m_eventDeliveryInFlight.isEmpty() || m_queue.isEmpty()) return;
    flushEventBusEvents();
    if (!m_eventQueue.isEmpty() || !m_eventDeliveryInFlight.isEmpty()) return;
    m_inFlight = m_queue.dequeue();
    m_inFlightId = m_nextRequestId++;
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("request")},
                        {QStringLiteral("id"), QString::number(m_inFlightId)},
                        {QStringLiteral("op"), m_inFlight.op},
                        {QStringLiteral("data"), m_inFlight.data}};
    if (!sendMessage(message)) {
        failWorker(QStringLiteral("cannot write worker IPC request"), true);
        return;
    }
    m_requestTimer.start(kRequestTimeoutMs);
}

void PyWorkerViewModel::scheduleUpdate(int delayMs) {
    if (!m_authenticated || m_stopping) return;
    if (delayMs < 0) delayMs = m_updateIntervalMs;
    if (delayMs == 0) {
        // init/invoke/语言切换后需要立即刷新；取消旧周期，从这次更新完成后
        // 重新计时，避免旧定时器随后再插入一次重复 update。
        m_updateTimer.stop();
        enqueue({QStringLiteral("update"), {}});
        return;
    }
    // 周期从上一次 update 完成后开始计算，因此慢请求不会积累追赶任务。
    m_updateTimer.start(delayMs);
}

void PyWorkerViewModel::handleRequestTimeout() {
    const QString operation = m_inFlight.op;
    failWorker(QStringLiteral("worker %1 request timed out").arg(operation), true,
               operation == QLatin1String("invoke")
                   ? QStringLiteral("timeout") : QStringLiteral("worker_failed"));
}

void PyWorkerViewModel::handleProcessError() {
    if (!m_process) return;
    if (m_process->error() == QProcess::FailedToStart) {
        QProcess* process = m_process;
        m_process = nullptr;
        process->deleteLater();
        failWorker(QStringLiteral("cannot start python worker: %1").arg(process->errorString()), false);
    }
}

void PyWorkerViewModel::handleProcessFinished(int exitCode, QProcess::ExitStatus status) {
    QProcess* process = m_process;
    m_process = nullptr;
    m_startTimer.stop();
    if (!m_stopping) {
        failWorker(QStringLiteral("worker exited (%1, code %2)")
                       .arg(processStatusText(status)).arg(exitCode),
                   true);
    }
    Q_EMIT runningChanged();
}

void PyWorkerViewModel::failWorker(const QString& reason, bool retry,
                                   const QString& callErrorCode) {
    if (m_stopping) return;
    failConfigRequests(reason);
    m_health.lastErrorCode = workerFailureCode(reason, callErrorCode);
    xuanhub::PluginLog::warning(m_id, m_health.lastErrorCode, reason);
    m_health.lastError = reason;
    m_health.lastErrorAt = QDateTime::currentDateTimeUtc();
    m_startTimer.stop();
    m_requestTimer.stop();
    m_updateTimer.stop();
    m_valuePatchTimer.stop();
    m_pluginEventTimer.stop();
    m_authenticated = false;
    m_supportsHotConfig = false;
    m_supportsConfigValidation = false;
    m_usesPushValues = false;
    const QVariantMap callError{
        {QStringLiteral("code"), callErrorCode},
        {QStringLiteral("message"), reason},
    };
    if (!m_inFlight.callId.isEmpty())
        Q_EMIT callFinished(m_inFlight.callId, false, {}, callError);
    for (const Request& request : std::as_const(m_queue)) {
        if (!request.callId.isEmpty())
            Q_EMIT callFinished(request.callId, false, {}, callError);
    }
    m_queue.clear();
    m_eventQueue.clear();
    m_pluginEventQueue.clear();
    m_pendingValuePatch.clear();
    m_pushedValues.clear();
    m_eventRequestIds.clear();
    m_eventDeliveryInFlight.clear();
    m_inFlight = {};
    m_inFlightId = 0;
    if (m_socket) {
        QTcpSocket* socket = m_socket;
        m_socket = nullptr;
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
    if (m_server) {
        QTcpServer* server = m_server;
        m_server = nullptr;
        server->close();
        server->deleteLater();
    }
    if (m_process) {
        QProcess* process = m_process;
        m_process = nullptr;
        detachProcess(process);
    }
    Q_EMIT runningChanged();
    if (retry) {
        if (!recordWorkerFailure(m_failureClock.elapsed())) {
            setHealthState(xuanhub::PluginState::Restarting);
            scheduleRestart();
        }
    } else {
        setHealthState(xuanhub::PluginState::Failed);
    }
    Q_EMIT healthChanged();
}

bool PyWorkerViewModel::recordWorkerFailure(qint64 elapsedMs) {
    while (!m_failureTimesMs.isEmpty()
           && elapsedMs - m_failureTimesMs.head() > kCircuitFailureWindowMs) {
        m_failureTimesMs.dequeue();
    }
    m_failureTimesMs.enqueue(elapsedMs);
    Q_EMIT consecutiveFailuresChanged();
    if (m_failureTimesMs.size() < kCircuitFailureLimit) return false;

    if (!m_circuitOpen) {
        m_circuitOpen = true;
        qWarning() << "[PyBridge]" << m_id
                   << ": worker circuit opened after" << m_failureTimesMs.size()
                   << "failures within" << kCircuitFailureWindowMs << "ms";
        Q_EMIT circuitOpenChanged();
        setHealthState(xuanhub::PluginState::CircuitOpen);
    }
    return true;
}

void PyWorkerViewModel::markWorkerHealthy() {
    if (m_startupClock.isValid()) {
        // Python 插件真正可用的边界是 import/init 后首次 update() 成功，而不是
        // 代理对象创建完成。只在这里采样一次，后续周期更新保持同一个启动耗时。
        m_health.loadDurationMs = qMax<qint64>(1, m_startupClock.elapsed());
        m_startupClock.invalidate();
    }
    m_restartAttempt = 0;
    if (!m_failureTimesMs.isEmpty()) {
        m_failureTimesMs.clear();
        Q_EMIT consecutiveFailuresChanged();
    }
    m_health.lastHealthyAt = QDateTime::currentDateTimeUtc();
    setHealthState(xuanhub::PluginState::Running);
    Q_EMIT healthChanged();
}

void PyWorkerViewModel::setHealthState(xuanhub::PluginState state) {
    if (m_health.state == state) return;
    m_health.state = state;
    Q_EMIT healthChanged();
}

void PyWorkerViewModel::scheduleRestart() {
    if (m_stopping) return;
    ++m_health.restartCount;
    Q_EMIT healthChanged();
    const int shift = qMin(m_restartAttempt++, 5);
    const int delay = qMin(30000, 1000 * (1 << shift));
    QTimer::singleShot(delay, this, [this] {
        if (!m_stopping) start();
    });
}

void PyWorkerViewModel::failConfigRequests(const QString& reason) {
    if (!m_configInitialized) {
        m_configInitialized = true;
        Q_EMIT configInitialized(m_id, false, reason);
    }
    const auto fail = [this, &reason](const Request& request) {
        if (request.op == QLatin1String("set_config"))
            Q_EMIT configApplied(m_id,
                request.data.value(QStringLiteral("config")).toObject().toVariantMap(),
                false, reason);
    };
    fail(m_inFlight);
    for (const auto& request : std::as_const(m_queue)) fail(request);
}

void PyWorkerViewModel::stopWorker() {
    if (m_stopping) return;
    m_stopping = true;
    failConfigRequests(QStringLiteral("plugin stopped before config acknowledgement"));
    m_startupClock.invalidate();
    m_startTimer.stop();
    m_requestTimer.stop();
    m_updateTimer.stop();
    m_valuePatchTimer.stop();
    m_pluginEventTimer.stop();
    m_supportsHotConfig = false;
    m_supportsConfigValidation = false;
    m_usesPushValues = false;
    m_queue.clear();
    m_eventQueue.clear();
    m_pluginEventQueue.clear();
    m_pendingValuePatch.clear();
    m_pushedValues.clear();
    m_eventRequestIds.clear();
    m_eventDeliveryInFlight.clear();
    if (m_socket) {
        QTcpSocket* socket = m_socket;
        m_socket = nullptr;
        disconnect(socket, nullptr, this, nullptr);
        // worker 在 EOF 的 finally 中调用可选 stop()；最多给它 750ms，再由 reaper 终止。
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    if (m_server) {
        QTcpServer* server = m_server;
        m_server = nullptr;
        server->close();
        server->deleteLater();
    }
    if (m_process) {
        QProcess* process = m_process;
        m_process = nullptr;
        detachProcess(process, 750);
    }
    m_authenticated = false;
    Q_EMIT runningChanged();
}

void PyWorkerViewModel::detachProcess(QProcess* process, int gracefulMs) {
    // 精确移除本 DLL 中的三个接收回调。不能使用 process->disconnect() 的
    // wildcard 形式，它会尝试断开 QObject::destroyed 并产生运行时警告。
    QObject::disconnect(process, &QProcess::readyReadStandardError,
                        this, nullptr);
    QObject::disconnect(process, &QProcess::errorOccurred,
                        this, nullptr);
    QObject::disconnect(process,
                        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                        this, nullptr);
    // 回收器位于不会随 pybridge 卸载的 barplugin.dll。不能把带 lambda 的 QTimer/
    // finished 回调留在本 DLL：热重载后再触发会跳进已卸载机器码而崩溃。
    xuanhub::reapProcess(process, gracefulMs);
}

QString PyWorkerViewModel::nextToken() const {
    QByteArray bytes(32, Qt::Uninitialized);
    for (qsizetype i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xffU);
    return QString::fromLatin1(bytes.toHex());
}
