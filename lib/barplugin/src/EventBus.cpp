// EventBus.cpp
#include "barplugin/EventBus.h"
#include <QMetaObject>
#include <QMetaMethod>
#include <QDebug>

namespace xuanhub {

EventBus::EventBus(QObject* parent) : QObject(parent) {}

void EventBus::subscribe(const QString& topic, QObject* receiver, const char* slot) {
    subscribeOwned(0, topic, receiver, slot);
}

quint64 EventBus::createOwnerToken() {
    return m_nextOwnerToken++;
}

void EventBus::subscribeOwned(quint64 owner, const QString& topic,
                              QObject* receiver, const char* slot) {
    if (!receiver || !slot) return;
    m_subs[topic].removeIf([](const Sub& s) { return !s.receiver; });
    // 去重：同 receiver 同 slot 重复订阅会导致同一事件重复投递；
    // 订阅记录是 receiver + 方法签名字符串，逐项比较即可
    const QByteArray member(slot);
    for (const auto& s : m_subs[topic]) {
        if (s.receiver == receiver && s.member == member) {
            qDebug() << "[EventBus] duplicate subscription ignored:" << topic
                     << "->" << receiver->metaObject()->className()
                     << "::" << member.constData();
            return;
        }
    }
    m_subs[topic].append({receiver, member, owner});
}

void EventBus::unsubscribe(const QString& topic, QObject* receiver) {
    auto it = m_subs.find(topic);
    if (it == m_subs.end()) return;
    it->removeIf([receiver](const Sub& s) { return s.receiver == receiver || !s.receiver; });
    if (it->isEmpty()) m_subs.erase(it);
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
}

void EventBus::publish(const QString& topic, const QVariantMap& payload) {
    auto it = m_subs.find(topic);
    if (it != m_subs.end()) {
        // 快照后再投递：订阅者槽函数是同步执行的，其间可能
        // subscribe/unsubscribe/publish，直接迭代会因 QHash rehash
        // 或 QVector 变更导致迭代器失效（与 Qt 信号发射的快照语义一致：
        // 本次发射期间新增的订阅者不接收本次事件）
        const QVector<Sub> snapshot = it.value();
        for (const auto& sub : snapshot) {
            if (!sub.receiver) continue;   // 快照后接收者可能已被销毁
            // 先 indexOfMethod 定位再 QMetaMethod::invoke：
            // QMetaObject::invokeMethod(obj, member, ...) 的 member 只按
            // 方法名匹配，带参数列表的签名串会查无此法（实测踩过）
            const QMetaObject* mo = sub.receiver->metaObject();
            const int idx = mo->indexOfMethod(sub.member.constData());
            if (idx < 0) {
                qWarning() << "[EventBus] no such method" << mo->className()
                           << "::" << sub.member.constData() << ", skip";
                continue;
            }
            mo->method(idx).invoke(sub.receiver.data(), Qt::AutoConnection,
                                   Q_ARG(QVariantMap, payload));
        }
        // 迭代外顺手清理悬空订阅（重新 find，哈希可能已 rehash）
        it = m_subs.find(topic);
        if (it != m_subs.end()) {
            it->removeIf([](const Sub& s) { return !s.receiver; });
            if (it->isEmpty()) m_subs.erase(it);
        }
    }
    emit dispatched(topic, payload);
}

} // namespace xuanhub
