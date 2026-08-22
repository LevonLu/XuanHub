// HostServicesTest.cpp — 固定 HostServices V1 的接口和结果语义
#include <QtTest>

#include "barplugin/HostServices.h"

class HostServicesTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void resultFactoriesKeepStableFields();
    void trustLevelsRemainDistinct();
};

void HostServicesTest::resultFactoriesKeepStableFields() {
    const auto success = xuanhub::HostServiceResult::succeeded(QStringLiteral("value"));
    QVERIFY(success.success);
    QCOMPARE(success.value.toString(), QStringLiteral("value"));
    QVERIFY(success.errorCode.isEmpty());

    const auto failure = xuanhub::HostServiceResult::failed(
        QStringLiteral("permission_denied"), QStringLiteral("not granted"));
    QVERIFY(!failure.success);
    QCOMPARE(failure.errorCode, QStringLiteral("permission_denied"));
    QCOMPARE(failure.errorMessage, QStringLiteral("not granted"));
}

void HostServicesTest::trustLevelsRemainDistinct() {
    QCOMPARE(xuanhub::kHostServicesApiVersion, 1);
    QVERIFY(xuanhub::HostPluginTrustLevel::TrustedInProcess
            != xuanhub::HostPluginTrustLevel::Isolated);
}

QTEST_MAIN(HostServicesTest)
#include "HostServicesTest.moc"
