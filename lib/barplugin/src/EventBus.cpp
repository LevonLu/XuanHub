// EventBus.cpp — V1 兼容广播与 V2 作用域事件、状态和请求响应
#include "barplugin/EventBus.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace {
constexpr int kMaxPayloadDepth = 32;
constexpr int kMaxQueuedPerSubscription = 256;
constexpr qint64 kSlowSubscriberMs = 50;

bool isSafePayloadValue(const QVariant& value, int depth = 0) {
    if (depth > kMaxPayloadDepth) return false;
    if (!value.isValid() || value.isNull()) return true;
    switch (value.metaType().id()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
    case QMetaType::QString:
        return true;
    case QMetaType::QStringList:
        return true;
    case QMetaType::QVariantList:
        for (const QVariant& item : value.toList()) {
            if (!isSafePayloadValue(item, depth + 1)) return false;
        }
        return true;
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (!isSafePayloadValue(it.value(), depth + 1)) return false;
        }
        return true;
    }
    default:
        return false;
    }
}

bool permissionMatches(const QStringList& permissions, const QString& action,
                       const QString& topic) {
    const QString prefix = action + QLatin1Char(':');
    for (const QString& permission : permissions) {
        if (!permission.startsWith(prefix)) continue;
        const QString pattern = permission.mid(prefix.size());
        if (pattern == topic) return true;
        if (pattern.endsWith(QStringLiteral(".*"))
            && topic.startsWith(pattern.first(pattern.size() - 1))) {
            return true;
        }
    }
    return false;
}

bool hasMethod(QObject* receiver, const QByteArray& member) {
    return receiver && receiver->metaObject()->indexOfMethod(member.constData()) >= 0;
}
} // namespace

namespace xuanhub {

EventBus::EventBus(QObject* parent) : QObject(parent) {}

bool EventBus::isValidTopic(const QString& topic) {
    static const QRegularExpression pattern(QStringLiteral(
        R"(^(?:app|service|plugin)\.[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*$)"));
    return topic.size() <= 256 && pattern.match(topic).hasMatch();
}

void EventBus::subscribe(const QString& topic, QObject* receiver, const char* slot) {
    subscribeOwned(0, topic, receiver, slot);
}

quint64 EventBus::createOwnerToken(const QString& pluginId,
                                   const QStringList& permissions) {
    const quint64 token = m_nextOwnerToken++;
    m_owners.insert(token, {pluginId, permissions});
    return token;
}

void EventBus::subscribeOwned(quint64 owner, const QString& topic,
                              QObject* receiver, const char* slot) {
    if (!receiver || !slot) return;
    auto& subscriptions = m_subs[topic];
    subscriptions.removeIf([](const Sub& sub) { return !sub.receiver; });
    const QByteArray member(slot);
    for (const Sub& sub : std::as_const(subscriptions)) {
        if (sub.receiver == receiver && sub.member == member) {
            qDebug() << "[EventBus] duplicate subscription ignored:" << topic
                     << "->" << receiver->metaObject()->className()
                     << "::" << member.constData();
            return;
        }
    }
    subscriptions.append({receiver, member, owner, m_nextSubscriptionId++, false});
}

bool EventBus::subscribeV2(const QString& topic, QObject* receiver, const char* slot,
                           EventSubscriptionOptions options) {
    return subscribeV2Owned(0, topic, receiver, slot, options);
}

bool EventBus::subscribeV2Owned(quint64 owner, const QString& topic,
                                QObject* receiver, const char* slot,
                                EventSubscriptionOptions options) {
    if (!isValidTopic(topic) || !receiver || !slot || !hasMethod(receiver, slot)
        || !canSubscribe(owner, topic)) {
        qWarning() << "[EventBus] rejected V2 subscription:" << topic;
        return false;
    }
    auto& subscriptions = m_subs[topic];
    subscriptions.removeIf([](const Sub& sub) { return !sub.receiver; });
    const QByteArray member(slot);
    for (const Sub& sub : std::as_const(subscriptions)) {
        if (sub.receiver == receiver && sub.member == member) return true;
    }
    const Sub sub{receiver, member, owner, m_nextSubscriptionId++, true};
    subscriptions.append(sub);
    if (options.replayRetained) {
        const auto retained = m_retained.constFind(topic);
        if (retained != m_retained.cend()) queueV2(topic, sub, retained->envelope);
    }
    return true;
}

void EventBus::unsubscribe(const QString& topic, QObject* receiver) {
    auto it = m_subs.find(topic);
    if (it == m_subs.end()) return;
    it->removeIf([receiver](const Sub& sub) {
        return sub.receiver == receiver || !sub.receiver;
    });
    if (it->isEmpty()) m_subs.erase(it);
}

bool EventBus::canPublish(quint64 owner, const QString& topic) const {
    if (owner == 0) return true;
    const auto ownerIt = m_owners.constFind(owner);
    if (ownerIt == m_owners.cend() || ownerIt->pluginId.isEmpty()) return false;
    const QString ownPrefix = QStringLiteral("plugin.") + ownerIt->pluginId + QLatin1Char('.');
    return topic.startsWith(ownPrefix)
        || permissionMatches(ownerIt->permissions, QStringLiteral("events.publish"), topic);
}

bool EventBus::canSubscribe(quint64 owner, const QString& topic) const {
    if (owner == 0) return true;
    const auto ownerIt = m_owners.constFind(owner);
    if (ownerIt == m_owners.cend() || ownerIt->pluginId.isEmpty()) return false;
    const QString ownPrefix = QStringLiteral("plugin.") + ownerIt->pluginId + QLatin1Char('.');
    return topic.startsWith(QStringLiteral("app.")) || topic.startsWith(ownPrefix)
        || permissionMatches(ownerIt->permissions, QStringLiteral("events.subscribe"), topic);
}

bool EventBus::isOwnerActive(quint64 owner) const {
    return owner == 0 || m_owners.contains(owner);
}

QVariantMap EventBus::envelope(quint64 owner, const QVariantMap& data,
                               const QString& kind, const QString& requestId) const {
    QString source = QStringLiteral("app");
    if (owner != 0) {
        const auto it = m_owners.constFind(owner);
        source = it == m_owners.cend() || it->pluginId.isEmpty()
            ? QStringLiteral("unknown")
            : QStringLiteral("plugin.") + it->pluginId;
    }
    QVariantMap value{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("source"), source},
        {QStringLiteral("timestamp"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("data"), data},
    };
    if (!requestId.isEmpty()) value.insert(QStringLiteral("requestId"), requestId);
    return value;
}

void EventBus::publish(const QString& topic, const QVariantMap& payload) {
    QVariantMap value = envelope(0, payload, QStringLiteral("event"));
    value[QStringLiteral("source")] = QStringLiteral("legacy");
    deliver(topic, payload, value, true);
}

bool EventBus::publishV2(const QString& topic, const QVariantMap& data) {
    return publishV2Owned(0, topic, data);
}

bool EventBus::publishV2Owned(quint64 owner, const QString& topic,
                              const QVariantMap& data) {
    if (!isValidTopic(topic) || !canPublish(owner, topic)
        || !isSafePayloadValue(data)) {
        qWarning() << "[EventBus] rejected V2 publish:" << topic;
        return false;
    }
    deliver(topic, data, envelope(owner, data, QStringLiteral("event")), true);
    return true;
}

bool EventBus::setState(const QString& topic, const QVariantMap& data,
                        EventStateOptions options) {
    return setStateOwned(0, topic, data, options);
}

bool EventBus::setStateOwned(quint64 owner, const QString& topic,
                             const QVariantMap& data, EventStateOptions options) {
    if (!isValidTopic(topic) || !canPublish(owner, topic)
        || !isSafePayloadValue(data)) {
        qWarning() << "[EventBus] rejected retained state:" << topic;
        return false;
    }
    const QVariantMap value = envelope(owner, data, QStringLiteral("state"));
    m_retained.insert(topic, {value, owner, options.clearOnOwnerRemoval});
    deliver(topic, data, value, true);
    return true;
}

QVariantMap EventBus::state(const QString& topic) const {
    const auto it = m_retained.constFind(topic);
    return it == m_retained.cend() ? QVariantMap{} : it->envelope;
}

void EventBus::deliver(const QString& topic, const QVariantMap& rawPayload,
                       const QVariantMap& v2Envelope, bool emitLegacySignal,
                       bool deliverV1) {
    int subscriberCount = 0;
    qint64 maxDurationMs = 0;
    auto it = m_subs.find(topic);
    if (it != m_subs.end()) {
        const QVector<Sub> snapshot = it.value();
        for (const Sub& sub : snapshot) {
            if (!sub.receiver) continue;
            if (!sub.v2 && !deliverV1) continue;
            ++subscriberCount;
            if (sub.v2) {
                queueV2(topic, sub, v2Envelope);
                continue;
            }
            const QMetaObject* metaObject = sub.receiver->metaObject();
            const int methodIndex = metaObject->indexOfMethod(sub.member.constData());
            if (methodIndex < 0) {
                qWarning() << "[EventBus] no such method" << metaObject->className()
                           << "::" << sub.member.constData() << ", skip";
                continue;
            }
            QElapsedTimer timer;
            timer.start();
            metaObject->method(methodIndex).invoke(sub.receiver.data(), Qt::AutoConnection,
                                                   Q_ARG(QVariantMap, rawPayload));
            const qint64 durationMs = timer.elapsed();
            maxDurationMs = std::max(maxDurationMs, durationMs);
            if (durationMs >= kSlowSubscriberMs) {
                qWarning() << "[EventBus] slow V1 subscriber:" << topic
                           << durationMs << "ms";
            }
        }
        it = m_subs.find(topic);
        if (it != m_subs.end()) {
            it->removeIf([](const Sub& sub) { return !sub.receiver; });
            if (it->isEmpty()) m_subs.erase(it);
        }
    }
    const QString source = v2Envelope.value(QStringLiteral("source")).toString();
    emit traceRecorded(topic, source, subscriberCount, maxDurationMs, 0);
    if (emitLegacySignal) emit dispatched(topic, rawPayload);
}

void EventBus::queueV2(const QString& topic, const Sub& sub,
                       const QVariantMap& value) {
    QVariantMap routedValue = value;
    routedValue.insert(QStringLiteral("topic"), topic);
    int& queued = m_queuedBySubscription[sub.id];
    if (queued >= kMaxQueuedPerSubscription) {
        qWarning() << "[EventBus] V2 subscriber queue full, drop:" << topic;
        emit traceRecorded(topic, routedValue.value(QStringLiteral("source")).toString(),
                           1, 0, 1);
        return;
    }
    ++queued;
    QMetaObject::invokeMethod(this, [this, topic, sub, routedValue] {
        auto queuedIt = m_queuedBySubscription.find(sub.id);
        if (queuedIt != m_queuedBySubscription.end()) {
            if (--queuedIt.value() <= 0) m_queuedBySubscription.erase(queuedIt);
        }
        const auto topicIt = m_subs.constFind(topic);
        if (topicIt == m_subs.cend()) return;
        const auto active = std::find_if(topicIt->cbegin(), topicIt->cend(),
            [&sub](const Sub& candidate) {
                return candidate.id == sub.id && candidate.receiver;
            });
        if (active == topicIt->cend() || !active->receiver) return;
        const QMetaObject* metaObject = active->receiver->metaObject();
        const int methodIndex = metaObject->indexOfMethod(active->member.constData());
        if (methodIndex < 0) return;
        QElapsedTimer timer;
        timer.start();
        metaObject->method(methodIndex).invoke(active->receiver.data(), Qt::AutoConnection,
                                               Q_ARG(QVariantMap, routedValue));
        const qint64 durationMs = timer.elapsed();
        if (durationMs >= kSlowSubscriberMs) {
            qWarning() << "[EventBus] slow V2 subscriber:" << topic
                       << durationMs << "ms";
        }
        emit traceRecorded(topic, routedValue.value(QStringLiteral("source")).toString(),
                           1, durationMs, 0);
    }, Qt::QueuedConnection);
}

QString EventBus::request(const QString& topic, const QVariantMap& data,
                          QObject* receiver, const char* resultSlot, int timeoutMs) {
    return requestOwned(0, topic, data, receiver, resultSlot, timeoutMs);
}

QString EventBus::requestOwned(quint64 owner, const QString& topic,
                               const QVariantMap& data, QObject* receiver,
                               const char* resultSlot, int timeoutMs) {
    if (!isValidTopic(topic) || !canPublish(owner, topic) || !isSafePayloadValue(data)
        || !receiver || !resultSlot || !hasMethod(receiver, resultSlot)) {
        qWarning() << "[EventBus] rejected request:" << topic;
        return {};
    }
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSet<quint64> responders;
    const auto subscriptions = m_subs.value(topic);
    for (const Sub& sub : subscriptions) {
        if (sub.v2 && sub.receiver) responders.insert(sub.owner);
    }
    m_pendingRequests.insert(requestId,
        {owner, receiver, QByteArray(resultSlot), responders});
    deliver(topic, data, envelope(owner, data, QStringLiteral("request"), requestId),
            false, false);

    if (responders.isEmpty()) {
        QTimer::singleShot(0, this, [this, requestId] {
            finishRequest(requestId, false, {}, QStringLiteral("no_responder"),
                          QStringLiteral("no active responder"));
        });
    } else {
        const int boundedTimeout = std::clamp(timeoutMs, 1, 60000);
        QTimer::singleShot(boundedTimeout, this, [this, requestId] {
            finishRequest(requestId, false, {}, QStringLiteral("timeout"),
                          QStringLiteral("request timed out"));
        });
    }
    return requestId;
}

bool EventBus::respond(const QString& requestId, const QVariantMap& data,
                       const QString& errorCode, const QString& errorMessage) {
    return respondOwned(0, requestId, data, errorCode, errorMessage);
}

bool EventBus::respondOwned(quint64 owner, const QString& requestId,
                            const QVariantMap& data, const QString& errorCode,
                            const QString& errorMessage) {
    const auto it = m_pendingRequests.constFind(requestId);
    if (it == m_pendingRequests.cend() || !it->responders.contains(owner)
        || !isSafePayloadValue(data)) {
        return false;
    }
    finishRequest(requestId, errorCode.isEmpty(), data, errorCode, errorMessage);
    return true;
}

bool EventBus::cancelRequest(const QString& requestId) {
    return cancelRequestOwned(0, requestId);
}

bool EventBus::cancelRequestOwned(quint64 owner, const QString& requestId) {
    const auto it = m_pendingRequests.constFind(requestId);
    if (it == m_pendingRequests.cend() || it->callerOwner != owner) return false;
    finishRequest(requestId, false, {}, QStringLiteral("canceled"),
                  QStringLiteral("request canceled"));
    return true;
}

void EventBus::finishRequest(const QString& requestId, bool success,
                             const QVariantMap& data, const QString& errorCode,
                             const QString& errorMessage) {
    auto it = m_pendingRequests.find(requestId);
    if (it == m_pendingRequests.end()) return;
    const PendingRequest pending = it.value();
    m_pendingRequests.erase(it);
    const QVariantMap result{
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("success"), success},
        {QStringLiteral("data"), data},
        {QStringLiteral("errorCode"), errorCode},
        {QStringLiteral("errorMessage"), errorMessage},
    };
    QMetaObject::invokeMethod(this, [this, pending, result] {
        if (!isOwnerActive(pending.callerOwner) || !pending.receiver) return;
        const QMetaObject* metaObject = pending.receiver->metaObject();
        const int methodIndex = metaObject->indexOfMethod(pending.member.constData());
        if (methodIndex < 0) return;
        metaObject->method(methodIndex).invoke(pending.receiver.data(), Qt::AutoConnection,
                                               Q_ARG(QVariantMap, result));
    }, Qt::QueuedConnection);
}

void EventBus::unsubscribeOwner(quint64 owner) {
    if (owner == 0) return;
    for (auto it = m_subs.begin(); it != m_subs.end();) {
        it->removeIf([owner](const Sub& sub) {
            return sub.owner == owner || !sub.receiver;
        });
        if (it->isEmpty()) it = m_subs.erase(it);
        else ++it;
    }
    for (auto it = m_retained.begin(); it != m_retained.end();) {
        if (it->owner == owner && it->clearOnOwnerRemoval) it = m_retained.erase(it);
        else ++it;
    }

    QStringList abandon;
    QStringList noResponders;
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        if (it->callerOwner == owner) {
            abandon.append(it.key());
            continue;
        }
        it->responders.remove(owner);
        if (it->responders.isEmpty()) noResponders.append(it.key());
    }
    for (const QString& requestId : abandon) m_pendingRequests.remove(requestId);
    for (const QString& requestId : noResponders) {
        finishRequest(requestId, false, {}, QStringLiteral("responder_unloaded"),
                      QStringLiteral("all responders unloaded"));
    }
    m_owners.remove(owner);
}

} // namespace xuanhub
