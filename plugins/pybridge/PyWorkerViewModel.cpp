// PyWorkerViewModel.cpp — Python worker 的异步本机 IPC 实现
#include "PyWorkerViewModel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

#include <utility>

#include "barplugin/ProcessReaper.h"
#include "barplugin/EventBus.h"

namespace {
constexpr quint32 kMaxMessageBytes = 4 * 1024 * 1024;
constexpr int kStartTimeoutMs = 10000;
constexpr int kRequestTimeoutMs = 15000;

QString processStatusText(QProcess::ExitStatus status) {
    return status == QProcess::CrashExit ? QStringLiteral("crash") : QStringLiteral("normal");
}
} // namespace

PyWorkerViewModel::PyWorkerViewModel(QString id, QString dirPath, QVariantMap cfg, QString pythonExe,
                                     QString workerScript, xuanhub::EventBus* bus,
                                     QObject* parent)
    : QObject(parent)
    , m_id(std::move(id))
    , m_dirPath(std::move(dirPath))
    , m_cfg(std::move(cfg))
    , m_pythonExe(std::move(pythonExe))
    , m_workerScript(std::move(workerScript)) {
    if (bus) {
        bus->subscribe(QStringLiteral("app.locale.changed"), this,
                       "onLocaleChanged(QVariantMap)");
        bus->publish(QStringLiteral("app.locale.request"));
    }
    m_startTimer.setSingleShot(true);
    m_requestTimer.setSingleShot(true);
    m_updateTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, [this] {
        failWorker(QStringLiteral("worker connection timed out"), true);
    });
    connect(&m_requestTimer, &QTimer::timeout, this, &PyWorkerViewModel::handleRequestTimeout);
    connect(&m_updateTimer, &QTimer::timeout, this, [this] {
        enqueue({QStringLiteral("update"), {}});
    });
}

void PyWorkerViewModel::onLocaleChanged(QVariantMap payload) {
    const QString locale = payload.value(QStringLiteral("locale")).toString();
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

bool PyWorkerViewModel::start() {
    if (m_stopping || m_process || m_server) return false;
    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "[PyBridge]" << m_id << ": cannot listen for worker:" << server->errorString();
        server->deleteLater();
        return false;
    }
    m_server = server;
    m_token = nextToken();
    m_authenticated = false;
    m_readBuffer.clear();
    m_queue.clear();
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
    if (!running()) return QVariantMap{{QStringLiteral("queued"), false}};
    enqueue({QStringLiteral("invoke"), {
        {QStringLiteral("name"), name},
        {QStringLiteral("args"), QJsonArray::fromVariantList(args)},
    }});
    return QVariantMap{{QStringLiteral("queued"), true}};
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
    m_readBuffer += m_socket->readAll();
    while (m_readBuffer.size() >= static_cast<int>(sizeof(quint32))) {
        const auto* bytes = reinterpret_cast<const uchar*>(m_readBuffer.constData());
        const quint32 size = qFromBigEndian<quint32>(bytes);
        if (size > kMaxMessageBytes) {
            failWorker(QStringLiteral("worker IPC message exceeds limit"), false);
            return;
        }
        const int frameSize = static_cast<int>(sizeof(quint32) + size);
        if (m_readBuffer.size() < frameSize) return;
        const QByteArray payload = m_readBuffer.mid(sizeof(quint32), size);
        m_readBuffer.remove(0, frameSize);
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            failWorker(QStringLiteral("worker IPC returned invalid JSON"), false);
            return;
        }
        handleMessage(document.object());
    }
}

void PyWorkerViewModel::handleMessage(const QJsonObject& message) {
    if (!m_authenticated) {
        if (message.value(QStringLiteral("type")).toString() != QLatin1String("hello")
            || message.value(QStringLiteral("token")).toString() != m_token) {
            // 端口只绑定 loopback，但仍拒绝意外本机连接；真正 worker 仍可在超时前接入。
            qWarning() << "[PyBridge]" << m_id << ": rejected unauthenticated worker connection";
            if (m_socket) {
                QTcpSocket* socket = m_socket;
                m_socket = nullptr;
                socket->disconnectFromHost();
                socket->deleteLater();
            }
            return;
        }
        m_authenticated = true;
        m_startTimer.stop();
        if (m_server) m_server->close();
        Q_EMIT runningChanged();
        enqueue({QStringLiteral("init"), {
            {QStringLiteral("config"), QJsonObject::fromVariantMap(m_cfg)},
            {QStringLiteral("locale"), m_locale}
        }});
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
    m_inFlight = {};
    m_inFlightId = 0;
    m_requestTimer.stop();

    const bool ok = message.value(QStringLiteral("ok")).toBool(false);
    if (!ok) {
        qWarning().noquote() << "[PyBridge]" << m_id << op << "failed:"
                             << message.value(QStringLiteral("error")).toString();
        if (op == QLatin1String("init")) {
            failWorker(QStringLiteral("worker failed during plugin initialization"), false);
            return;
        }
    } else if (op == QLatin1String("update")) {
        const QJsonValue result = message.value(QStringLiteral("result"));
        if (!result.isObject()) {
            qWarning() << "[PyBridge]" << m_id << ": update() must return a dict";
        } else {
            const QVariantMap next = result.toObject().toVariantMap();
            if (next != m_values) {
                m_values = next;
                Q_EMIT valuesChanged();
            }
            // 仅在真正完成一次 update 后认定 worker 已恢复，避免初始化成功但随即
            // 崩溃的插件绕过退避；健康运行后下一次故障从 1 秒重新尝试。
            m_restartAttempt = 0;
        }
    }

    if (op == QLatin1String("init") || op == QLatin1String("invoke")
        || op == QLatin1String("set_locale")) scheduleUpdate(0);
    if (op == QLatin1String("update")) scheduleUpdate();
    dispatchNext();
}

bool PyWorkerViewModel::sendMessage(const QJsonObject& message) {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return false;
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > static_cast<int>(kMaxMessageBytes)) return false;
    QByteArray frame(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()),
                          reinterpret_cast<uchar*>(frame.data()));
    frame += payload;
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
    dispatchNext();
}

void PyWorkerViewModel::dispatchNext() {
    if (!m_authenticated || m_stopping || m_inFlightId != 0 || m_queue.isEmpty()) return;
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
    if (delayMs <= 0) {
        enqueue({QStringLiteral("update"), {}});
        return;
    }
    if (!m_updateTimer.isActive()) m_updateTimer.start(delayMs);
}

void PyWorkerViewModel::handleRequestTimeout() {
    const QString operation = m_inFlight.op;
    failWorker(QStringLiteral("worker %1 request timed out").arg(operation), true);
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

void PyWorkerViewModel::failWorker(const QString& reason, bool retry) {
    if (m_stopping) return;
    qWarning() << "[PyBridge]" << m_id << ":" << reason;
    m_startTimer.stop();
    m_requestTimer.stop();
    m_updateTimer.stop();
    m_authenticated = false;
    m_queue.clear();
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
    if (retry) scheduleRestart();
}

void PyWorkerViewModel::scheduleRestart() {
    if (m_stopping) return;
    const int shift = qMin(m_restartAttempt++, 5);
    const int delay = qMin(30000, 1000 * (1 << shift));
    QTimer::singleShot(delay, this, [this] {
        if (!m_stopping) start();
    });
}

void PyWorkerViewModel::stopWorker() {
    if (m_stopping) return;
    m_stopping = true;
    m_startTimer.stop();
    m_requestTimer.stop();
    m_updateTimer.stop();
    m_queue.clear();
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
