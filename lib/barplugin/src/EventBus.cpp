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
        it->removeIf([](const Sub& s) { return !s.receiver; });
        for (const auto& sub : *it) {
            QMetaObject::invokeMethod(
                sub.receiver.data(),
                sub.member.constData(),
                Qt::AutoConnection,
                Q_ARG(QVariantMap, payload));
        }
        if (it->isEmpty()) m_subs.erase(it);
    }
    emit dispatched(topic, payload);
}

} // namespace xuanhub
