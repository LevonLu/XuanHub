// PluginContractProbeMain.cpp — 向开发者 CLI 暴露宿主权威插件契约校验
#include "PluginContractValidator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QJsonArray strings(const QStringList& values) {
    QJsonArray output;
    for (const QString& value : values) output.append(value);
    return output;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(QStringLiteral(XUANHUB_VERSION));
    QJsonObject output{{QStringLiteral("schemaVersion"), 1}};
    if (app.arguments().size() != 2) {
        output.insert(QStringLiteral("ok"), false);
        output.insert(QStringLiteral("errors"),
                      QJsonArray{QStringLiteral("expected one plugin directory argument")});
        fputs(QJsonDocument(output).toJson(QJsonDocument::Compact).constData(), stdout);
        return 2;
    }

    const QString pluginDir = QFileInfo(app.arguments().at(1)).absoluteFilePath();
    const bool isPython = QFileInfo(QDir(pluginDir).filePath(
        QStringLiteral("plugin.json"))).isFile();
    const QString runtime = isPython ? QStringLiteral("python") : QStringLiteral("native");
    QString backend;
    if (!isPython) {
        QFile manifest(QDir(pluginDir).filePath(QStringLiteral("metadata.json")));
        if (manifest.open(QIODevice::ReadOnly)) {
            const QJsonObject object = QJsonDocument::fromJson(manifest.readAll()).object();
            backend = QFileInfo(object.value(QStringLiteral("entry")).toObject()
                                    .value(QStringLiteral("backend")).toString()).fileName();
        }
    }

    const PluginContractValidationResult result = PluginContractValidator::validate(
        pluginDir, runtime, backend);
    QStringList errors = result.errors;
    if (!result.manifest.ok && !result.manifest.error.isEmpty())
        errors.prepend(result.manifest.error);
    output.insert(QStringLiteral("ok"), result.ok());
    output.insert(QStringLiteral("runtime"), runtime);
    output.insert(QStringLiteral("id"), result.manifest.descriptor.id);
    output.insert(QStringLiteral("errors"), strings(errors));
    output.insert(QStringLiteral("warnings"), strings(result.warnings));
    fputs(QJsonDocument(output).toJson(QJsonDocument::Compact).constData(), stdout);
    return result.ok() ? 0 : 1;
}
