// PopupService.h — 统一计算插件弹窗位置并协调跨插件互斥
#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QVariantMap>

class PopupService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* activePopup READ activePopup NOTIFY activePopupChanged)

public:
    struct Placement {
        QPointF topLeft;
        bool opensAbove = false;
    };

    explicit PopupService(QObject* parent = nullptr);

    QObject* activePopup() const { return m_activePopup.data(); }

    static Placement calculatePlacement(const QRectF& anchor, const QRectF& screen,
                                        const QSizeF& popupSize, qreal gap,
                                        const QString& barEdge);

    Q_INVOKABLE QVariantMap calculate(const QVariantMap& anchor,
                                      const QVariantMap& screen,
                                      qreal popupWidth, qreal popupHeight,
                                      qreal gap, const QString& barEdge) const;
    Q_INVOKABLE void activate(QObject* popup);
    Q_INVOKABLE void release(QObject* popup);

Q_SIGNALS:
    void activePopupChanged();

private:
    QPointer<QObject> m_activePopup;
    QMetaObject::Connection m_destroyedConnection;
};
