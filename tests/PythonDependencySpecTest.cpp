// PythonDependencySpecTest.cpp — Python Manifest 依赖声明解析回归测试
#include "PythonDependencySpec.h"

#include <QJsonArray>
#include <QtTest>

class PythonDependencySpecTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void keepsLegacyManifestCompatible() {
        PythonDependencySpec spec;
        QString error;
        QVERIFY(readPythonDependencySpec({}, &spec, &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!spec.declared);
        QCOMPARE(spec.versionConstraint, QStringLiteral(">=3.12"));
        QVERIFY(spec.packages.isEmpty());
    }

    void parsesRuntimeAndPackageRequirements() {
        const QJsonObject manifest{{QStringLiteral("python"), QJsonObject{
            {QStringLiteral("version"), QStringLiteral(">=3.12, <3.14")},
            {QStringLiteral("packages"), QJsonArray{
                QJsonObject{{QStringLiteral("name"), QStringLiteral("bleak")},
                            {QStringLiteral("version"), QStringLiteral(">=0.22")}},
                QJsonObject{{QStringLiteral("name"), QStringLiteral("orjson")},
                            {QStringLiteral("import"), QStringLiteral("orjson")},
                            {QStringLiteral("version"), QStringLiteral("==3.10.7")},
                            {QStringLiteral("native"), true}},
            }},
            {QStringLiteral("environment"), QJsonObject{
                {QStringLiteral("mode"), QStringLiteral("private")},
                {QStringLiteral("wheels"), QStringLiteral("wheels/win_amd64")},
            }},
        }}};

        PythonDependencySpec spec;
        QString error;
        QVERIFY2(readPythonDependencySpec(manifest, &spec, &error), qPrintable(error));
        QVERIFY(spec.declared);
        QCOMPARE(spec.versionConstraint, QStringLiteral(">=3.12, <3.14"));
        QCOMPARE(spec.packages.size(), 2);
        QCOMPARE(spec.packages[0].name, QStringLiteral("bleak"));
        QCOMPARE(spec.packages[0].importName, QStringLiteral("bleak"));
        QCOMPARE(spec.packages[0].versionConstraint, QStringLiteral(">=0.22"));
        QVERIFY(!spec.packages[0].native);
        QCOMPARE(spec.packages[1].name, QStringLiteral("orjson"));
        QVERIFY(spec.packages[1].native);
        QCOMPARE(spec.environmentMode, QStringLiteral("private"));
        QCOMPARE(spec.wheelDirectory, QStringLiteral("wheels/win_amd64"));
    }

    void rejectsMalformedDeclarations() {
        const QList<QJsonObject> invalid{
            {{QStringLiteral("python"), QStringLiteral(">=3.12")}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("version"), QStringLiteral("~=3.12")}}}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("packages"), QStringLiteral("bleak")}}}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("packages"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("bad package")}}}}}}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("packages"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("bleak")}},
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("BLEAK")}}}}}}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("packages"), QJsonArray{QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("bleak")},
                    {QStringLiteral("native"), QStringLiteral("yes")}}}}}}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("environment"), QJsonObject{
                    {QStringLiteral("mode"), QStringLiteral("private")},
                    {QStringLiteral("wheels"), QStringLiteral("../outside")}}}}}},
            {{QStringLiteral("python"), QJsonObject{
                {QStringLiteral("environment"), QJsonObject{
                    {QStringLiteral("mode"), QStringLiteral("shared")},
                    {QStringLiteral("wheels"), QStringLiteral("wheels")}}}}}},
        };

        for (const QJsonObject& manifest : invalid) {
            QString error;
            QVERIFY(!readPythonDependencySpec(manifest, nullptr, &error));
            QVERIFY(!error.isEmpty());
        }
    }

    void validatesConstraintSubset() {
        for (const QString& valid : {QStringLiteral("*"), QStringLiteral(">=3.12"),
                                     QStringLiteral(">=3.12,<3.14"),
                                     QStringLiteral("!=3.13.1, <=4.0.0.0")}) {
            QString error;
            QVERIFY2(isValidPythonVersionConstraint(valid, &error), qPrintable(error));
        }
        for (const QString& invalid : {QString{}, QStringLiteral("3.12"),
                                       QStringLiteral("~=3.12"), QStringLiteral(">=03.12"),
                                       QStringLiteral(">=3"), QStringLiteral(">=3.12,")}) {
            QString error;
            QVERIFY(!isValidPythonVersionConstraint(invalid, &error));
            QVERIFY(!error.isEmpty());
        }
    }
};

QTEST_MAIN(PythonDependencySpecTest)
#include "PythonDependencySpecTest.moc"
