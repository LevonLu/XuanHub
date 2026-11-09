// LandscapeLocation.h — 可替换的异步定位边界，测试不触发系统授权
#pragma once
#include <QObject>
#include <QDateTime>
#include <memory>

class LandscapeLocation : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    // 只读检查，不得弹出授权窗口。
    virtual QString access() = 0;
    virtual void request(quint64 requestId, bool interactive) = 0;
    virtual void cancel() = 0;
Q_SIGNALS:
    void finished(quint64 requestId, const QString& error, double latitude, double longitude,
                  const QDateTime& timestamp);
};

class WindowsLandscapeLocation final : public LandscapeLocation {
public:
    explicit WindowsLandscapeLocation(QObject* parent = nullptr);
    ~WindowsLandscapeLocation() override;
    QString access() override;
    void request(quint64 requestId, bool interactive) override;
    void cancel() override;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
