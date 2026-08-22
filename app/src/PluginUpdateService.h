// PluginUpdateService.h — 异步检查静态索引并安全下载待确认插件包
#pragma once

#include "PluginUpdateIndex.h"

#include <QCryptographicHash>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantList>

class QNetworkReply;
class QTemporaryFile;

class PluginUpdateService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)

public:
    explicit PluginUpdateService(QString configDirectory,
                                 QObject* parent = nullptr);

    bool checking() const { return m_checking; }
    bool downloading() const { return m_downloadReply != nullptr; }

    Q_INVOKABLE void check(const QStringList& sourceUrls,
                           const QVariantMap& installedPlugins);
    Q_INVOKABLE void download(const QString& id, const QString& version);
    Q_INVOKABLE void cancelDownload();

signals:
    void checkingChanged();
    void downloadingChanged();
    void checkFinished(const QVariantList& updates, const QStringList& errors);
    void downloadFinished(const QString& packagePath,
                          const QVariantMap& update,
                          const QString& error);

private:
    struct IndexDownload {
        QByteArray bytes;
        QString error;
    };

    void finishIndex(QNetworkReply* reply);
    void finalizeCheck();
    void finishPackageDownload();
    void resetPackageDownload(bool removeTemporary);
    static QString candidateKey(const QString& id, const QString& version);

    QString m_configDirectory;
    QNetworkAccessManager m_network;
    bool m_checking = false;
    int m_pendingIndexes = 0;
    QVariantMap m_installedPlugins;
    QList<PluginUpdateCandidate> m_indexCandidates;
    QStringList m_checkErrors;
    QHash<QNetworkReply*, IndexDownload> m_indexDownloads;
    QHash<QString, PluginUpdateCandidate> m_available;

    QPointer<QNetworkReply> m_downloadReply;
    QTemporaryFile* m_downloadFile = nullptr;
    QCryptographicHash* m_downloadHash = nullptr;
    PluginUpdateCandidate m_downloadingCandidate;
    qint64 m_downloadedBytes = 0;
    QString m_downloadError;
};
