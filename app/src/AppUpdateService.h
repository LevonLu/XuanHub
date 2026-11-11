// AppUpdateService.h — GitHub 宿主版本发现、下载和独立更新器交接
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <QUrl>
#include <atomic>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QThread;
class QFile;
class QCryptographicHash;
class QTimer;

struct AppUpdateRelease {
    QString version;
    QString notes;
    QString publishedAt;
    QUrl packageUrl;
    QString sha256;
    qint64 size = 0;
    static bool parse(const QByteArray& json, AppUpdateRelease* result, QString* error);
};

class AppUpdatePrepareWorker final : public QObject {
    Q_OBJECT
public:
    QString directory;
    QString target;
    QString version;
    std::shared_ptr<std::atomic_bool> cancelled;
public slots:
    void run();
signals:
    void finished(const QString& error);
};

class AppUpdateService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(QString publishedAt READ publishedAt NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool automaticChecks READ automaticChecks WRITE setAutomaticChecks NOTIFY changed)
public:
    AppUpdateService(QString targetDirectory, QString configDirectory,
                     QObject* parent = nullptr, QNetworkAccessManager* network = nullptr);
    ~AppUpdateService() override;
    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QString latestVersion() const { return m_release.version; }
    QString releaseNotes() const { return m_release.notes; }
    QString publishedAt() const { return m_release.publishedAt; }
    double progress() const { return m_progress; }
    bool busy() const;
    bool automaticChecks() const;
    void setAutomaticChecks(bool enabled);
    void scheduleAutomaticCheck();
    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void ignoreVersion();

    // 启动保护早于单实例锁；更新回执仅在主 QML 完成加载后发出。
    static bool handlePendingUpdate(const QString& exeDirectory);
    static void reportReady(const QString& exeDirectory);
    static bool permittedUrl(const QUrl& url, bool api);
signals:
    void changed();
    void updateAvailable(const QString& version);
private:
    void startRequest(const QUrl& url, bool api, int redirects = 0);
    void consume(QNetworkReply* reply, bool api);
    void finishRequest(QNetworkReply* reply, bool api, int redirects);
    void prepare();
    void startHelper();
    void fail(const QString& message);
    bool savePreferences();
    QString m_target;
    QString m_configDirectory;
    QString m_directory;
    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QThread* m_thread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    std::unique_ptr<QFile> m_download;
    std::unique_ptr<QCryptographicHash> m_hash;
    QJsonObject m_preferences;
    AppUpdateRelease m_release;
    QByteArray m_apiBytes;
    QString m_state = QStringLiteral("idle");
    QString m_error;
    double m_progress = 0;
    qint64 m_received = 0;
    bool m_backgroundCheck = false;
    bool m_cancelRequested = false;
};
