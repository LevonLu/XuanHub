// EventBus.h — 插件间松耦合通信
// subscribe(topic, receiver, slot) 按 topic 订阅；
// publish(topic, payload) 精准投递订阅者 + 广播 dispatched 信号。
// 注意用 Q_SIGNALS 而非 signals：pybridge 等定义了 QT_NO_KEYWORDS 的
// 插件也要包含本头（signals 宏不存在会语法错误）。
#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QHash>
#include <QVector>
#include <QPointer>
#include <QByteArray>
#include <QSet>

#include "barplugin/barplugin_export.h"

namespace xuanhub {

class PluginEventScope;

struct EventSubscriptionOptions {
    bool replayRetained = false;
};

struct EventStateOptions {
    bool clearOnOwnerRemoval = true;
};

class BARPLUGIN_EXPORT EventBus : public QObject {
    Q_OBJECT
public:
    explicit EventBus(QObject* parent = nullptr);

    // slot 形如 onPayload(QVariantMap)
    void subscribe(const QString& topic, QObject* receiver, const char* slot);
    void unsubscribe(const QString& topic, QObject* receiver);

    Q_INVOKABLE void publish(const QString& topic, const QVariantMap& payload = {});

    // V2 宿主接口。插件应优先通过 PluginEventScope 使用对应方法，使来源和
    // 生命周期由宿主绑定。V2 handler 仍接收 QVariantMap，但内容为标准 envelope。
    bool subscribeV2(const QString& topic, QObject* receiver, const char* slot,
                     EventSubscriptionOptions options = {});
    bool publishV2(const QString& topic, const QVariantMap& data = {});
    bool setState(const QString& topic, const QVariantMap& data = {},
                  EventStateOptions options = {false});
    QVariantMap state(const QString& topic) const;
    QString request(const QString& topic, const QVariantMap& data,
                    QObject* receiver, const char* resultSlot,
                    int timeoutMs = 15000);
    bool respond(const QString& requestId, const QVariantMap& data = {},
                 const QString& errorCode = {}, const QString& errorMessage = {});
    bool cancelRequest(const QString& requestId);

    static bool isValidTopic(const QString& topic);

Q_SIGNALS:
    void dispatched(const QString& topic, const QVariantMap& payload);
    // 只暴露元数据，不携带 Payload，避免诊断链路记录密钥或个人数据。
    void traceRecorded(const QString& topic, const QString& source,
                       int subscriberCount, qint64 maxDurationMs, int droppedCount);

private:
    struct Sub;
    friend class PluginEventScope;
    quint64 createOwnerToken(const QString& pluginId = {},
                             const QStringList& permissions = {});
    void subscribeOwned(quint64 owner, const QString& topic, QObject* receiver, const char* slot);
    bool subscribeV2Owned(quint64 owner, const QString& topic, QObject* receiver,
                          const char* slot, EventSubscriptionOptions options);
    bool publishV2Owned(quint64 owner, const QString& topic, const QVariantMap& data);
    bool setStateOwned(quint64 owner, const QString& topic, const QVariantMap& data,
                       EventStateOptions options);
    QString requestOwned(quint64 owner, const QString& topic, const QVariantMap& data,
                         QObject* receiver, const char* resultSlot, int timeoutMs);
    bool respondOwned(quint64 owner, const QString& requestId, const QVariantMap& data,
                      const QString& errorCode, const QString& errorMessage);
    bool cancelRequestOwned(quint64 owner, const QString& requestId);
    void unsubscribeOwner(quint64 owner);

    QVariantMap envelope(quint64 owner, const QVariantMap& data,
                         const QString& kind, const QString& requestId = {}) const;
    bool canPublish(quint64 owner, const QString& topic) const;
    bool canSubscribe(quint64 owner, const QString& topic) const;
    bool isOwnerActive(quint64 owner) const;
    void deliver(const QString& topic, const QVariantMap& rawPayload,
                 const QVariantMap& v2Envelope, bool emitLegacySignal,
                 bool deliverV1 = true);
    void queueV2(const QString& topic, const Sub& sub,
                 const QVariantMap& value);
    void finishRequest(const QString& requestId, bool success,
                       const QVariantMap& data, const QString& errorCode,
                       const QString& errorMessage);

    struct Sub {
        QPointer<QObject> receiver;
        QByteArray member;
        quint64 owner = 0;
        quint64 id = 0;
        bool v2 = false;
    };
    struct Owner {
        QString pluginId;
        QStringList permissions;
    };
    struct Retained {
        QVariantMap envelope;
        quint64 owner = 0;
        bool clearOnOwnerRemoval = false;
    };
    struct PendingRequest {
        quint64 callerOwner = 0;
        QPointer<QObject> receiver;
        QByteArray member;
        QSet<quint64> responders;
    };
    QHash<QString, QVector<Sub>> m_subs;
    QHash<quint64, Owner> m_owners;
    QHash<QString, Retained> m_retained;
    QHash<QString, PendingRequest> m_pendingRequests;
    QHash<quint64, int> m_queuedBySubscription;
    quint64 m_nextOwnerToken = 1;
    quint64 m_nextSubscriptionId = 1;
};

} // namespace xuanhub
