// PopupServiceTest.cpp — 标准弹窗定位与跨插件互斥回归
#include "PopupService.h"

#include <QtTest>

class FakePopup final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)

public:
    bool visible() const { return m_visible; }
    void setVisible(bool visible) {
        if (m_visible == visible) return;
        m_visible = visible;
        Q_EMIT visibleChanged();
    }

Q_SIGNALS:
    void visibleChanged();

private:
    bool m_visible = false;
};

class PopupServiceTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void topBarPlacesBelowAndClampsHorizontally() {
        const auto placement = PopupService::calculatePlacement(
            QRectF(1880, 0, 40, 40), QRectF(0, 0, 1920, 1080),
            QSizeF(420, 300), 6, QStringLiteral("top"));
        QCOMPARE(placement.topLeft, QPointF(1500, 46));
        QVERIFY(!placement.opensAbove);
    }

    void bottomBarPlacesAbove() {
        const auto placement = PopupService::calculatePlacement(
            QRectF(-1180, 1040, 80, 40), QRectF(-1280, 0, 1280, 1080),
            QSizeF(360, 240), 6, QStringLiteral("bottom"));
        QCOMPARE(placement.topLeft, QPointF(-1280, 794));
        QVERIFY(placement.opensAbove);
    }

    void placementIsScaleIndependent_data() {
        QTest::addColumn<qreal>("scale");
        QTest::newRow("100-percent") << 1.0;
        QTest::newRow("150-percent") << 1.5;
        QTest::newRow("200-percent") << 2.0;
    }

    void placementIsScaleIndependent() {
        QFETCH(qreal, scale);
        const auto placement = PopupService::calculatePlacement(
            QRectF(400 * scale, 0, 80 * scale, 40 * scale),
            QRectF(0, 0, 1280 * scale, 720 * scale),
            QSizeF(320 * scale, 240 * scale), 6 * scale, QStringLiteral("top"));
        QCOMPARE(placement.topLeft, QPointF(280 * scale, 46 * scale));
        QVERIFY(!placement.opensAbove);
    }

    void activatingAnotherPopupClosesPrevious() {
        PopupService service;
        FakePopup first;
        FakePopup second;
        first.setVisible(true);
        second.setVisible(true);

        service.activate(&first);
        QCOMPARE(service.activePopup(), &first);
        service.activate(&second);
        QVERIFY(!first.visible());
        QVERIFY(second.visible());
        QCOMPARE(service.activePopup(), &second);

        service.release(&second);
        QVERIFY(service.activePopup() == nullptr);
    }
};

QTEST_GUILESS_MAIN(PopupServiceTest)
#include "PopupServiceTest.moc"
