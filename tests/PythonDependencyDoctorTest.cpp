// PythonDependencyDoctorTest.cpp — 真实解释器依赖 doctor 集成测试
#include "PythonDependencyDoctor.h"

#include <QFileInfo>
#include <QtTest>

class PythonDependencyDoctorTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void acceptsCompatibleRuntimeWithoutPackages() {
        const QString python = qEnvironmentVariable("XUANHUB_TEST_PYTHON");
        const QString doctor = qEnvironmentVariable("XUANHUB_TEST_PYTHON_DOCTOR");
        QVERIFY(QFileInfo::exists(python));
        QVERIFY(QFileInfo::exists(doctor));

        PythonDependencySpec spec;
        spec.declared = true;
        spec.versionConstraint = QStringLiteral(">=3.12,<4.0");
        const PythonDependencyReport report = PythonDependencyDoctor::run(
            python, doctor, spec);
        QVERIFY2(report.failure.isEmpty(), qPrintable(report.failure));
        QVERIFY2(report.ok, qPrintable(report.summary()));
        QVERIFY(!report.runtime.value(QStringLiteral("version")).toString().isEmpty());
        QCOMPARE(report.runtime.value(QStringLiteral("pointerBits")).toInt(), 64);
        QVERIFY(report.issues.isEmpty());
    }

    void reportsStableMissingPackageSuggestion() {
        PythonDependencySpec spec;
        spec.declared = true;
        spec.versionConstraint = QStringLiteral(">=3.12");
        spec.packages.append({QStringLiteral("xuanhub-package-that-does-not-exist"),
                              QStringLiteral("xuanhub_missing"),
                              QStringLiteral(">=1.0"), false});
        const PythonDependencyReport report = PythonDependencyDoctor::run(
            qEnvironmentVariable("XUANHUB_TEST_PYTHON"),
            qEnvironmentVariable("XUANHUB_TEST_PYTHON_DOCTOR"), spec);
        QVERIFY2(report.failure.isEmpty(), qPrintable(report.failure));
        QVERIFY(!report.ok);
        QCOMPARE(report.issues.size(), 1);
        const QVariantMap issue = report.issues.first().toMap();
        QCOMPARE(issue.value(QStringLiteral("code")).toString(),
                 QStringLiteral("package_missing"));
        QVERIFY(!issue.value(QStringLiteral("suggestion")).toString().isEmpty());
        QVERIFY(!report.summary().isEmpty());
    }
};

QTEST_MAIN(PythonDependencyDoctorTest)
#include "PythonDependencyDoctorTest.moc"
