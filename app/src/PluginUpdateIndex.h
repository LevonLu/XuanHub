// PluginUpdateIndex.h — 解析用户显式配置的静态插件更新索引
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantMap>

struct PluginUpdateCandidate {
    QString sourceId;
    QUrl sourceUrl;
    QString id;
    QString version;
    QString runtime;
    QUrl packageUrl;
    QString packageSha256;
    qint64 packageSize = 0;
    QString signerKeyId;

    QVariantMap toVariantMap() const;
};

struct PluginUpdateIndexResult {
    bool ok = false;
    QString error;
    QString sourceId;
    QList<PluginUpdateCandidate> packages;
};

class PluginUpdateIndex final {
public:
    static PluginUpdateIndexResult parse(const QByteArray& json,
                                         const QUrl& sourceUrl);
    static QList<PluginUpdateCandidate> newerCandidates(
        const QList<PluginUpdateCandidate>& candidates,
        const QHash<QString, QString>& installedVersions);
};
