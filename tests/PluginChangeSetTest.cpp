// PluginChangeSetTest.cpp — 插件配置指纹与差异分类回归测试
#include "PluginChangeSet.h"
#include "PluginConfigTracker.h"

#include <QtTest>

namespace {
PluginConfigEntry entry(const QString& id, const QString& position = QStringLiteral("right"),
                        int order = 0, int width = 0, const QVariantMap& config = {}) {
    PluginConfigEntry value;
    value.id = id;
    value.position = position;
    value.order = order;
    value.width = width;
    value.config = config;
    return value;
}
} // namespace

class PluginChangeSetTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void failedConfigKeepsConfirmedSnapshotAndCanRetry() {
        PluginConfigTracker tracker;
        auto old = entry(QStringLiteral("clock"));
        tracker.applied(old);
        auto next = old;
        next.config.insert(QStringLiteral("format"), QStringLiteral("HH:mm"));
        tracker.begin(next);
        QVERIFY(PluginConfigDiff::calculate(tracker.entries(), {next}).isEmpty());
        tracker.failed(next.id, QStringLiteral("rejected"));
        QCOMPARE(PluginConfigDiff::calculate(tracker.entries(), {next}).configChanged,
                 QStringList{next.id});
        tracker.begin(next);
        tracker.applied(next);
        QVERIFY(PluginConfigDiff::calculate(tracker.entries(), {next}).isEmpty());
        tracker.removed(next.id);
        QVERIFY(tracker.entries().isEmpty());
        QVERIFY(tracker.states().isEmpty());
    }

    void failedInitialConfigDoesNotBecomeApplied() {
        PluginConfigTracker tracker;
        const auto next = entry(QStringLiteral("py_test"));
        tracker.begin(next);
        tracker.failed(next.id, QStringLiteral("worker timed out"));
        QCOMPARE(PluginConfigDiff::calculate(tracker.entries(), {next}).added,
                 QStringList{next.id});
    }
    void fingerprintIsStableAcrossInputOrder() {
        QVariantMap firstConfig;
        firstConfig.insert(QStringLiteral("z"), 2);
        firstConfig.insert(QStringLiteral("a"), 1);
        QVariantMap secondConfig;
        secondConfig.insert(QStringLiteral("a"), 1);
        secondConfig.insert(QStringLiteral("z"), 2);

        const auto first = QList<PluginConfigEntry>{
            entry(QStringLiteral("beta")),
            entry(QStringLiteral("alpha"), QStringLiteral("left"), 3, 120, firstConfig),
        };
        const auto second = QList<PluginConfigEntry>{
            entry(QStringLiteral("alpha"), QStringLiteral("left"), 3, 120, secondConfig),
            entry(QStringLiteral("beta")),
        };
        QCOMPARE(PluginConfigDiff::entriesFingerprint(first),
                 PluginConfigDiff::entriesFingerprint(second));
    }

    void separatesLayoutAndBackendChanges() {
        const auto before = QList<PluginConfigEntry>{
            entry(QStringLiteral("removed")),
            entry(QStringLiteral("layout")),
            entry(QStringLiteral("config"), QStringLiteral("right"), 2, 0,
                  {{QStringLiteral("intervalMs"), 1000}}),
            entry(QStringLiteral("both"), QStringLiteral("left"), 1, 80,
                  {{QStringLiteral("value"), 1}}),
            entry(QStringLiteral("same")),
        };
        const auto after = QList<PluginConfigEntry>{
            entry(QStringLiteral("added")),
            entry(QStringLiteral("layout"), QStringLiteral("center"), 7, 160),
            entry(QStringLiteral("config"), QStringLiteral("right"), 2, 0,
                  {{QStringLiteral("intervalMs"), 2000}}),
            entry(QStringLiteral("both"), QStringLiteral("right"), 4, 100,
                  {{QStringLiteral("value"), 2}}),
            entry(QStringLiteral("same")),
        };

        const PluginChangeSet changes = PluginConfigDiff::calculate(before, after);
        QCOMPARE(changes.added, QStringList{QStringLiteral("added")});
        QCOMPARE(changes.removed, QStringList{QStringLiteral("removed")});
        QCOMPARE(changes.layoutChanged,
                 (QStringList{QStringLiteral("both"), QStringLiteral("layout")}));
        QCOMPARE(changes.configChanged,
                 (QStringList{QStringLiteral("both"), QStringLiteral("config")}));
        QCOMPARE(changes.unchanged, QStringList{QStringLiteral("same")});
        QVERIFY(!changes.isEmpty());
    }

    void identicalEntriesProduceEmptyChangeSet() {
        const auto entries = QList<PluginConfigEntry>{entry(QStringLiteral("clock"))};
        const PluginChangeSet changes = PluginConfigDiff::calculate(entries, entries);
        QVERIFY(changes.isEmpty());
        QCOMPARE(changes.unchanged, QStringList{QStringLiteral("clock")});
    }

    void configVersionChangeIsABackendChange() {
        PluginConfigEntry before = entry(QStringLiteral("versioned"));
        PluginConfigEntry after = before;
        after.configVersion = 2;
        const PluginChangeSet changes = PluginConfigDiff::calculate({before}, {after});
        QCOMPARE(changes.configChanged, QStringList{QStringLiteral("versioned")});
        QVERIFY(changes.unchanged.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PluginChangeSetTest)
#include "PluginChangeSetTest.moc"
