// PluginDependencyGraphTest.cpp — 插件依赖错误和稳定加载顺序回归测试
#include "PluginDependencyGraph.h"

#include <QtTest>

namespace {
xuanhub::PluginDescriptor plugin(const QString& id, const QString& version = QStringLiteral("1.0.0")) {
    xuanhub::PluginDescriptor value;
    value.id = id;
    value.displayName = id;
    value.version = version;
    return value;
}

xuanhub::PluginDependency dependency(const QString& id,
                                     const QString& version = QStringLiteral("*"),
                                     bool optional = false) {
    return {id, version, optional};
}
} // namespace

class PluginDependencyGraphTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void ordersRequiredDependenciesBeforeConsumers() {
        auto consumer = plugin(QStringLiteral("consumer"));
        consumer.dependencies.append(dependency(QStringLiteral("base"),
                                                QStringLiteral(">=1.0.0")));
        const auto plan = PluginDependencyGraph::build(
            {consumer, plugin(QStringLiteral("base"), QStringLiteral("1.2.0")),
             plugin(QStringLiteral("alpha"))});
        QVERIFY(plan.errors.isEmpty());
        QCOMPARE(plan.loadOrder,
                 QStringList({QStringLiteral("alpha"), QStringLiteral("base"),
                              QStringLiteral("consumer")}));
    }

    void reportsMissingAndIncompatibleRequiredDependencies() {
        auto missing = plugin(QStringLiteral("missing-consumer"));
        missing.dependencies.append(dependency(QStringLiteral("absent")));
        auto incompatible = plugin(QStringLiteral("incompatible"));
        incompatible.dependencies.append(
            dependency(QStringLiteral("base"), QStringLiteral(">=2.0.0")));
        const auto plan = PluginDependencyGraph::build(
            {missing, incompatible, plugin(QStringLiteral("base"), QStringLiteral("1.0.0"))});
        QVERIFY(plan.errors.value(missing.id).contains(QStringLiteral("missing")));
        QVERIFY(plan.errors.value(incompatible.id).contains(QStringLiteral("does not match")));
        QCOMPARE(plan.loadOrder, QStringList({QStringLiteral("base")}));
    }

    void ignoresUnavailableOptionalDependency() {
        auto standalone = plugin(QStringLiteral("standalone"));
        standalone.dependencies.append(dependency(
            QStringLiteral("optional"), QStringLiteral(">=9.0.0"), true));
        const auto plan = PluginDependencyGraph::build({standalone});
        QVERIFY(plan.errors.isEmpty());
        QCOMPARE(plan.loadOrder, QStringList({standalone.id}));
    }

    void reportsDuplicatesAndRequiredCycles() {
        auto a = plugin(QStringLiteral("a"));
        auto b = plugin(QStringLiteral("b"));
        a.dependencies.append(dependency(QStringLiteral("b")));
        b.dependencies.append(dependency(QStringLiteral("a")));
        auto needsDuplicate = plugin(QStringLiteral("consumer"));
        needsDuplicate.dependencies.append(dependency(QStringLiteral("duplicate")));
        const auto plan = PluginDependencyGraph::build(
            {a, b, plugin(QStringLiteral("duplicate")),
             plugin(QStringLiteral("duplicate")), needsDuplicate});
        QVERIFY(plan.errors.value(QStringLiteral("a")).contains(QStringLiteral("cycle")));
        QVERIFY(plan.errors.value(QStringLiteral("b")).contains(QStringLiteral("cycle")));
        QVERIFY(plan.errors.value(QStringLiteral("duplicate")).contains(QStringLiteral("duplicate")));
        QVERIFY(plan.errors.value(QStringLiteral("consumer")).contains(QStringLiteral("duplicated")));
        QVERIFY(plan.loadOrder.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PluginDependencyGraphTest)
#include "PluginDependencyGraphTest.moc"
