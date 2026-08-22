// ResponsiveLayoutEngineTest.cpp — 响应式宽度、优先级和溢出回归测试
#include "ResponsiveLayoutEngine.h"

#include <QtTest>

namespace {
xuanhub::PluginLayoutPreference responsive(int minWidth, int preferredWidth,
                                            int priority,
                                            const QString& overflow = QStringLiteral("clip"),
                                            int flex = 0, int maxWidth = 0) {
    xuanhub::PluginLayoutPreference value;
    value.declared = true;
    value.minWidth = minWidth;
    value.preferredWidth = preferredWidth;
    value.priority = priority;
    value.overflow = overflow;
    value.flex = flex;
    value.maxWidth = maxWidth;
    return value;
}
} // namespace

class ResponsiveLayoutEngineTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void preservesLegacyAndFixedWidths() {
        const QVector<ResponsiveLayoutEngine::Item> items{
            {QStringLiteral("legacy"), 80, 0, {}},
            {QStringLiteral("fixed"), 90, 120, responsive(20, 60, 1)},
            {QStringLiteral("adaptive"), 100, 0, responsive(40, 100, 50)},
        };
        const auto result = ResponsiveLayoutEngine::allocate(250, items);
        QCOMPARE(result.at(0).width, 80);
        QCOMPARE(result.at(1).width, 120);
        QCOMPARE(result.at(2).width, 50);
    }

    void shrinksLowerPriorityFirst() {
        const QVector<ResponsiveLayoutEngine::Item> items{
            {QStringLiteral("important"), 100, 0, responsive(50, 100, 90)},
            {QStringLiteral("secondary"), 100, 0, responsive(50, 100, 10)},
        };
        const auto result = ResponsiveLayoutEngine::allocate(150, items);
        QCOMPARE(result.at(0).width, 100);
        QCOMPARE(result.at(1).width, 50);
    }

    void sendsExplicitMenuOverflowOutOfLayout() {
        const QVector<ResponsiveLayoutEngine::Item> items{
            {QStringLiteral("legacy"), 180, 0, {}},
            {QStringLiteral("optional"), 100, 0,
             responsive(60, 100, 10, QStringLiteral("menu"))},
        };
        const auto result = ResponsiveLayoutEngine::allocate(200, items);
        QVERIFY(result.at(0).visible);
        QVERIFY(!result.at(1).visible);
        QVERIFY(result.at(1).overflowed);
        QCOMPARE(result.at(1).width, 0);
    }

    void distributesExtraSpaceByFlexWithinMaxWidth() {
        const QVector<ResponsiveLayoutEngine::Item> items{
            {QStringLiteral("one"), 50, 0, responsive(40, 50, 50, {}, 1, 80)},
            {QStringLiteral("two"), 50, 0, responsive(40, 50, 50, {}, 2, 100)},
        };
        const auto result = ResponsiveLayoutEngine::allocate(180, items);
        QCOMPARE(result.at(0).width + result.at(1).width, 180);
        QVERIFY(result.at(1).width > result.at(0).width);
        QVERIFY(result.at(0).width <= 80);
        QVERIFY(result.at(1).width <= 100);
    }
};

QTEST_GUILESS_MAIN(ResponsiveLayoutEngineTest)
#include "ResponsiveLayoutEngineTest.moc"
