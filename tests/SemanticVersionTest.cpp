// SemanticVersionTest.cpp — SemVer 与插件版本约束回归测试
#include "SemanticVersion.h"

#include <QtTest>

class SemanticVersionTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parsesAndOrdersSemanticVersions() {
        SemanticVersion prerelease;
        SemanticVersion release;
        QVERIFY(SemanticVersion::parse(QStringLiteral("1.2.3-alpha.1+build.7"),
                                       &prerelease));
        QVERIFY(SemanticVersion::parse(QStringLiteral("1.2.3"), &release));
        QCOMPARE(prerelease.toString(), QStringLiteral("1.2.3-alpha.1"));
        QVERIFY(prerelease.compare(release) < 0);
        QVERIFY(!SemanticVersion::parse(QStringLiteral("01.2.3"), nullptr));
        QVERIFY(!SemanticVersion::parse(QStringLiteral("1.2"), nullptr));
        QVERIFY(!SemanticVersion::parse(QStringLiteral("1.2.3-01"), nullptr));
        QVERIFY(!SemanticVersion::parse(QStringLiteral("1.2.3-00"), nullptr));
        QVERIFY(!SemanticVersion::parse(QStringLiteral("999999999999.2.3"), nullptr));
    }

    void matchesSupportedConstraintSubset() {
        QVERIFY(SemanticVersion::matches(QStringLiteral("1.4.2"), QStringLiteral(">=1.2.0")));
        QVERIFY(!SemanticVersion::matches(QStringLiteral("1.4.2"), QStringLiteral("<1.0.0")));
        QVERIFY(SemanticVersion::matches(QStringLiteral("0.9.0"), QStringLiteral("0.x")));
        QVERIFY(!SemanticVersion::matches(QStringLiteral("1.0.0"), QStringLiteral("0.x")));
        QVERIFY(SemanticVersion::matches(QStringLiteral("2.0.0"), QStringLiteral("*")));
        QVERIFY(!SemanticVersion::isValidConstraint(QStringLiteral("^1.2.0")));
    }
};

QTEST_GUILESS_MAIN(SemanticVersionTest)
#include "SemanticVersionTest.moc"
