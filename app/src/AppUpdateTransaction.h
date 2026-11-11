// AppUpdateTransaction.h — 更新器的可恢复文件事务，不依赖界面与网络
#pragma once
#include <QString>
#include <QJsonObject>
#include <functional>

class AppUpdateTransaction final {
public:
    using FaultInjector = std::function<bool(int)>;
    explicit AppUpdateTransaction(QString jobDirectory);
    bool load(QString* error);
    bool apply(QString* error, const FaultInjector& fault = {});
    bool rollback(QString* error);
    bool complete(const QString& status, const QString& message, QString* error);
    QString target() const { return m_target; }
    QString stage() const;
    QString directory() const { return m_directory; }
    QJsonObject job() const { return m_job; }

private:
    QString m_directory;
    QString m_target;
    QJsonObject m_job;
};
