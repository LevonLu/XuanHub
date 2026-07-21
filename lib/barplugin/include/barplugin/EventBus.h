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

#include "barplugin/barplugin_export.h"

namespace xuanhub {

class BARPLUGIN_EXPORT EventBus : public QObject {
    Q_OBJECT
public:
    explicit EventBus(QObject* parent = nullptr);

    // slot 形如 onPayload(QVariantMap)
    void subscribe(const QString& topic, QObject* receiver, const char* slot);
    void unsubscribe(const QString& topic, QObject* receiver);

    Q_INVOKABLE void publish(const QString& topic, const QVariantMap& payload = {});

Q_SIGNALS:
    void dispatched(const QString& topic, const QVariantMap& payload);

private:
    struct Sub { QPointer<QObject> receiver; QByteArray member; };
    QHash<QString, QVector<Sub>> m_subs;
};

} // namespace xuanhub
