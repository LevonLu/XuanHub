// PyWorkerViewModel.h — Python 子插件的进程隔离代理 ViewModel
// 每个子插件独占一个 python.exe worker。宿主仅维护异步本机 IPC，绝不导入用户 Python 代码。
#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class QTcpServer;
class QTcpSocket;
namespace xuanhub { class EventBus; }

class PyWorkerViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
public:
    PyWorkerViewModel(QString id, QString dirPath, QVariantMap cfg, QString pythonExe,
                      QString workerScript, xuanhub::EventBus* bus,
                      QObject* parent = nullptr);
    ~PyWorkerViewModel() override;

    QVariantMap values() const { return m_values; }
    bool running() const;
    int updateIntervalMs() const { return m_updateIntervalMs; }

    // 启动监听端与 worker。真正的 import/init 在 worker 连接并通过令牌认证后异步进行。
    bool start();

    // 保持既有 QML API；调用改为排队异步执行，返回值仅表示是否已入队。
    Q_INVOKABLE QVariant call(const QString& name, const QVariantList& args);

public slots:
    void onLocaleChanged(QVariantMap payload);

Q_SIGNALS:
    void valuesChanged();
    void runningChanged();

private:
    struct Request {
        QString op;
        QJsonObject data;
    };

    void acceptConnection();
    void readMessages();
    void handleMessage(const QJsonObject& message);
    bool sendMessage(const QJsonObject& message);
    void enqueue(Request request, bool front = false);
    void dispatchNext();
    // delayMs < 0 使用插件配置的刷新周期；0 表示交互后立即刷新。
    void scheduleUpdate(int delayMs = -1);
    void handleRequestTimeout();
    void handleProcessError();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);
    void failWorker(const QString& reason, bool retry);
    void scheduleRestart();
    void stopWorker();
    void detachProcess(QProcess* process, int gracefulMs = 0);
    QString nextToken() const;

    QString m_id;
    QString m_dirPath;
    QVariantMap m_cfg;
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
    QByteArray m_readBuffer;
    QQueue<Request> m_queue;
    Request m_inFlight;
    quint64 m_inFlightId = 0;
    quint64 m_nextRequestId = 1;
    QString m_token;
    bool m_authenticated = false;
    bool m_stopping = false;
    int m_restartAttempt = 0;
    int m_updateIntervalMs = 1000;
};
