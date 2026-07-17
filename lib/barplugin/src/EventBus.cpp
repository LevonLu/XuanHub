// EventBus.cpp
#include "barplugin/EventBus.h"
#include <QMetaObject>
#include <QDebug>

namespace xuanhub {

EventBus::EventBus(QObject* parent) : QObject(parent) {}

void EventBus::subscribe(const QString& topic, QObject* receiver, const char* slot) {
    if (!receiver || !slot) return;
    m_subs[topic].removeIf([](const Sub& s) { return !s.receiver; });
    m_subs[topic].append({receiver, slot});
}

void EventBus::unsubscribe(const QString& topic, QObject* receiver) {
    auto it = m_subs.find(topic);
    if (it == m_subs.end()) return;
    it->removeIf([receiver](const Sub& s) { return s.receiver == receiver || !s.receiver; });
    if (it->isEmpty()) m_subs.erase(it);
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
            QMetaObject::invokeMethod(
                sub.receiver.data(),
                sub.member.constData(),
                Qt::AutoConnection,
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
