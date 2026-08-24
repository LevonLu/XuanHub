// PyWorkerViewModel.h — Python 子插件的进程隔离代理 ViewModel
// 每个子插件独占一个 python.exe worker。宿主仅维护异步本机 IPC，绝不导入用户 Python 代码。
#pragma once

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QHash>
#include <QPair>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include "PyBridgeProtocol.h"
#include "barplugin/IBarPluginHostConfigValidation.h"
#include "barplugin/PluginHealth.h"

class QTcpServer;
class QTcpSocket;
class PyWorkerViewModelTest;
namespace xuanhub {
class EventBus;
class PluginEventScope;
class IHostServices;
}

class PyWorkerViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool circuitOpen READ circuitOpen NOTIFY circuitOpenChanged)
    Q_PROPERTY(int consecutiveFailures READ consecutiveFailures NOTIFY consecutiveFailuresChanged)
    Q_PROPERTY(QVariantMap health READ health NOTIFY healthChanged)
public:
    PyWorkerViewModel(QString id, QString dirPath, QVariantMap cfg, QString pythonExe,
                      QString workerScript, xuanhub::EventBus* bus,
                      xuanhub::PluginEventScope* eventScope = nullptr,
                      QObject* parent = nullptr);
    ~PyWorkerViewModel() override;

    QVariantMap values() const { return m_values; }
    bool running() const;
    bool circuitOpen() const { return m_circuitOpen; }
    int consecutiveFailures() const { return m_failureTimesMs.size(); }
    int updateIntervalMs() const { return m_updateIntervalMs; }
    bool supportsHotConfig() const { return m_supportsHotConfig; }
    bool supportsConfigValidation() const { return m_supportsConfigValidation; }
    bool usesPushValues() const { return m_usesPushValues; }
    int protocolVersion() const { return m_protocolVersion; }
    QStringList protocolCapabilities() const { return m_protocolCapabilities; }
    QVariantMap health() const;

    // 启动监听端与 worker。真正的 import/init 在 worker 连接并通过令牌认证后异步进行。
    bool start();

    // 保持既有 QML API；调用改为排队异步执行，返回值仅表示是否已入队。
    Q_INVOKABLE QVariant call(const QString& name, const QVariantList& args);
    Q_INVOKABLE QVariantMap cancelCall(const QString& callId);
    // 只重建当前插件的 worker；同时解除熔断，供设置页和插件错误界面手动恢复。
    Q_INVOKABLE bool restart();
    xuanhub::HostedPluginConfigValidationResult validateConfig(
        const QVariantMap& config, int timeoutMs);
    bool applyConfig(const QVariantMap& config);
    void setConfigVersions(int currentVersion, int targetVersion);
    void setHostServices(xuanhub::IHostServices* services) { m_hostServices = services; }
    void setEventScope(xuanhub::PluginEventScope* eventScope) {
        m_pluginEventScope = eventScope;
    }

public slots:
    void onLocaleChanged(QVariantMap payload);
    void onEventBusEnvelope(QVariantMap envelope);
    void onEventBusRequestResult(QVariantMap result);

Q_SIGNALS:
    void valuesChanged();
    void runningChanged();
    void circuitOpenChanged();
    void consecutiveFailuresChanged();
    void healthChanged();
    void configMigrationReady(const QString& id, int currentVersion,
                              int targetVersion, const QVariantMap& config);
    void configMigrationFailed(const QString& id, int currentVersion,
                               int targetVersion, const QString& error);
    void configValidationFinished(bool valid, const QString& error);
    void callFinished(const QString& callId, bool ok, const QVariant& result,
                      const QVariantMap& error);
    void callProgress(const QString& callId, double progress,
                      const QVariantMap& details);
    void pluginEvent(const QString& event, const QVariantMap& data);

private:
    friend class PyWorkerViewModelTest;

    struct Request {
        QString op;
        QJsonObject data;
        QString callId;
    };

    void acceptConnection();
    void readMessages();
    void handleMessage(const QJsonObject& message);
    void handleHostServiceRequest(const QJsonObject& message);
    void handleEventBusRequest(const QJsonObject& message);
    void handleValuesPatch(const QJsonObject& message);
    void handlePluginEvent(const QJsonObject& message);
    bool acceptPushSequence(const QJsonObject& message, const QString& capability);
    void flushValuePatch();
    void flushPluginEvents();
    void flushEventBusEvents();
    bool sendMessage(const QJsonObject& message);
    void enqueue(Request request, bool front = false);
    void dispatchNext();
    // delayMs < 0 使用插件配置的刷新周期；0 表示交互后立即刷新。
    void scheduleUpdate(int delayMs = -1);
    void handleRequestTimeout();
    void handleProcessError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    void failWorker(const QString& reason, bool retry,
                    const QString& callErrorCode = QStringLiteral("worker_failed"));
    bool recordWorkerFailure(qint64 elapsedMs);
    void markWorkerHealthy();
    void setHealthState(xuanhub::PluginState state);
    void scheduleRestart();
    void stopWorker();
    void detachProcess(QProcess* process, int gracefulMs = 0);
    QString nextToken() const;

    QString m_id;
    QString m_dirPath;
    QVariantMap m_cfg;
    int m_configVersion = 1;
    int m_targetConfigVersion = 1;
    QString m_pythonExe;
    QString m_workerScript;
    QString m_locale = QStringLiteral("en-US");
    QVariantMap m_values;
    QPointer<QTcpServer> m_server;
    QPointer<QTcpSocket> m_socket;
    QPointer<QProcess> m_process;
    QTimer m_startTimer;
    QTimer m_requestTimer;
    QTimer m_updateTimer;
    QTimer m_valuePatchTimer;
    QTimer m_pluginEventTimer;
    QElapsedTimer m_failureClock;
    // 从拉起 python.exe 到首次成功 update()；完成后失效，避免运行期更新继续累加。
    QElapsedTimer m_startupClock;
    xuanhub::pybridge::FrameCodec m_frameCodec;
    QQueue<Request> m_queue;
    QQueue<QJsonObject> m_eventQueue;
    QQueue<QPair<QString, QVariantMap>> m_pluginEventQueue;
    QVariantMap m_pendingValuePatch;
    QVariantMap m_pushedValues;
    QHash<QString, QString> m_eventRequestIds; // EventBus request id -> Worker request id
    Request m_inFlight;
    quint64 m_inFlightId = 0;
    quint64 m_nextRequestId = 1;
    quint64 m_nextCallId = 1;
    quint64 m_nextEventDeliveryId = 1;
    quint64 m_lastPushSequence = 0;
    QString m_eventDeliveryInFlight;
    QString m_token;
    bool m_authenticated = false;
    bool m_stopping = false;
    bool m_circuitOpen = false;
    bool m_supportsHotConfig = false;
    bool m_supportsConfigValidation = false;
    bool m_usesPushValues = false;
    int m_protocolVersion = 0;
    QStringList m_protocolCapabilities;
    bool m_validationWaiting = false;
    QQueue<qint64> m_failureTimesMs;
    xuanhub::PluginHealth m_health;
    int m_restartAttempt = 0;
    int m_updateIntervalMs = 1000;
    xuanhub::IHostServices* m_hostServices = nullptr; // 核心按子插件 ID 绑定，Worker 不参与选择
    xuanhub::PluginEventScope* m_pluginEventScope = nullptr; // 核心拥有，销毁 VM 前先 clear
};
