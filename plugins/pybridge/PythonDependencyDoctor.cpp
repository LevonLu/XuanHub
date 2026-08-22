// PythonDependencyDoctor.cpp — 通过真实 Worker 解释器执行只读依赖检查
#include "PythonDependencyDoctor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

QString PythonDependencyReport::summary() const {
    if (!failure.isEmpty()) return failure;
    QStringList messages;
    for (const QVariant& value : issues) {
        const QVariantMap issue = value.toMap();
        const QString message = issue.value(QStringLiteral("message")).toString();
        const QString suggestion = issue.value(QStringLiteral("suggestion")).toString();
        if (!message.isEmpty())
            messages.append(suggestion.isEmpty() ? message
                                                  : message + QStringLiteral("；") + suggestion);
    }
    return messages.join(QStringLiteral("；"));
}

PythonDependencyReport PythonDependencyDoctor::run(
    const QString& pythonExecutable, const QString& doctorScript,
    const PythonDependencySpec& spec, int timeoutMs) {
    PythonDependencyReport report;
    if (pythonExecutable.isEmpty() || doctorScript.isEmpty()) {
        report.failure = QStringLiteral("Python doctor executable or script is missing");
        return report;
    }

    QJsonArray packages;
    for (const PythonPackageRequirement& requirement : spec.packages) {
        packages.append(QJsonObject{
            {QStringLiteral("name"), requirement.name},
            {QStringLiteral("import"), requirement.importName},
            {QStringLiteral("version"), requirement.versionConstraint},
            {QStringLiteral("native"), requirement.native},
        });
    }
    const QByteArray input = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), spec.versionConstraint},
        {QStringLiteral("packages"), packages},
    }).toJson(QJsonDocument::Compact);

    QProcess process;
    process.setProgram(pythonExecutable);
    process.setArguments({QStringLiteral("-B"), doctorScript});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(qMin(timeoutMs, 3000))) {
        report.failure = QStringLiteral("Python dependency doctor failed to start: %1")
                             .arg(process.errorString());
        return report;
    }
    process.write(input);
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        report.failure = QStringLiteral("Python dependency doctor timed out");
        return report;
    }
    const QByteArray output = process.readAllStandardOutput();
    if (output.size() > 1024 * 1024) {
        report.failure = QStringLiteral("Python dependency doctor output exceeds 1 MiB");
        return report;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        report.failure = QStringLiteral("Python dependency doctor returned invalid JSON: %1")
                             .arg(parseError.errorString());
        return report;
    }
    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("ok")).isBool()
        || !object.value(QStringLiteral("runtime")).isObject()
        || !object.value(QStringLiteral("issues")).isArray()) {
        report.failure = QStringLiteral("Python dependency doctor returned an invalid result");
        return report;
    }
    report.ok = object.value(QStringLiteral("ok")).toBool();
    report.runtime = object.value(QStringLiteral("runtime")).toObject().toVariantMap();
    report.issues = object.value(QStringLiteral("issues")).toArray().toVariantList();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        report.failure = report.summary();
        if (report.failure.isEmpty())
            report.failure = QStringLiteral("Python dependency doctor exited abnormally");
        report.ok = false;
    }
    return report;
}
