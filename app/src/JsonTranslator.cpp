// JsonTranslator.cpp — 校验、合并并查询 JSON 语言包
#include "JsonTranslator.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

JsonTranslator::JsonTranslator(QObject* parent) : QTranslator(parent) {}

bool JsonTranslator::mergeCatalog(const QString& path,
                                  const QString& requiredPrefix,
                                  bool required) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (required)
            qWarning() << "[I18n] cannot open required catalog:" << path;
        return false;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "[I18n] invalid catalog:" << path << error.errorString();
        return false;
    }

    const QJsonObject root = document.object();
    const QString declaredLocale = root.value(QStringLiteral("locale")).toString();
    if (!declaredLocale.isEmpty() && declaredLocale != m_locale) {
        qWarning() << "[I18n] catalog locale mismatch:" << path
                   << declaredLocale << "!=" << m_locale;
        return false;
    }

    const QJsonObject contexts = root.value(QStringLiteral("contexts")).toObject();
    if (contexts.isEmpty()) {
        qWarning() << "[I18n] catalog has no contexts:" << path;
        return false;
    }

    bool merged = false;
    for (auto contextIt = contexts.begin(); contextIt != contexts.end(); ++contextIt) {
        const QString context = contextIt.key();
        if (!requiredPrefix.isEmpty() && !context.startsWith(requiredPrefix)) {
            qWarning() << "[I18n] reject foreign context" << context << "from" << path
                       << "required prefix:" << requiredPrefix;
            continue;
        }
        if (!contextIt.value().isObject()) {
            qWarning() << "[I18n] context is not an object:" << context << "in" << path;
            continue;
        }

        auto& target = m_contexts[context];
        const QJsonObject messages = contextIt.value().toObject();
        for (auto messageIt = messages.begin(); messageIt != messages.end(); ++messageIt) {
            if (!messageIt.value().isString()) {
                qWarning() << "[I18n] translation is not a string:" << context
                           << messageIt.key() << "in" << path;
                continue;
            }
            if (target.contains(messageIt.key())) {
                qWarning() << "[I18n] duplicate translation ignored:" << context
                           << messageIt.key() << "from" << path;
                continue;
            }
            target.insert(messageIt.key(), messageIt.value().toString());
            merged = true;
        }
    }
    return merged;
}

void JsonTranslator::mergePluginRoot(const QString& rootPath) {
    const QDir root(rootPath);
    if (!root.exists()) return;

    const QStringList directories = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                   QDir::Name);
    for (const QString& directoryName : directories) {
        const QDir pluginDir(root.absoluteFilePath(directoryName));
        QString pluginId = directoryName;
        for (const QString& sidecar : {QStringLiteral("metadata.json"),
                                       QStringLiteral("plugin.json")}) {
            QFile metadata(pluginDir.absoluteFilePath(sidecar));
            if (!metadata.open(QIODevice::ReadOnly)) continue;
            const QJsonDocument document = QJsonDocument::fromJson(metadata.readAll());
            const QString declaredId = document.object().value(QStringLiteral("id")).toString();
            if (!declaredId.isEmpty()) pluginId = declaredId;
            break;
        }

        const QString catalog = pluginDir.absoluteFilePath(
            QStringLiteral("i18n/%1.json").arg(m_locale));
        if (!QFile::exists(catalog)) continue;
        mergeCatalog(catalog, QStringLiteral("plugin.%1.").arg(pluginId), false);
    }
}

bool JsonTranslator::loadLocale(const QString& locale, const QString& exeDir) {
    m_locale = locale;
    m_contexts.clear();

    const QString coreCatalog = QStringLiteral(
        ":/qt/qml/XuanHub/i18n/%1.json").arg(locale);
    const bool coreLoaded = mergeCatalog(coreCatalog, QStringLiteral("xuanhub."), true);
    mergePluginRoot(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins")));
    mergePluginRoot(QDir(exeDir).absoluteFilePath(QStringLiteral("plugins_py")));

    qDebug() << "[I18n] loaded" << locale << "contexts=" << m_contexts.size();
    return coreLoaded;
}

QString JsonTranslator::translate(const char* context, const char* sourceText,
                                  const char*, int) const {
    if (!context || !sourceText) return {};
    const auto contextIt = m_contexts.constFind(QString::fromUtf8(context));
    if (contextIt == m_contexts.constEnd()) return {};
    return contextIt->value(QString::fromUtf8(sourceText));
}
