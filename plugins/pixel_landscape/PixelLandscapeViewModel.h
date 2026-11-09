// PixelLandscapeViewModel.h — 定位、太阳光照、配置与实例级暂停状态
#pragma once

#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QDateTime>
#include <QTimer>
#include <QVariantMap>
#include "LandscapeLocation.h"

namespace xuanhub { class IPluginStorage; }

class PixelLandscapeViewModel final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QString previousPhase READ previousPhase NOTIFY phaseChanged)
    Q_PROPERTY(bool transitionAllowed READ transitionAllowed NOTIFY phaseChanged)
    Q_PROPERTY(QString weather READ weather NOTIFY configChanged)
    Q_PROPERTY(QString scene READ scene NOTIFY configChanged)
    Q_PROPERTY(QString frameStyle READ frameStyle NOTIFY configChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY configChanged)
    Q_PROPERTY(bool rareEventsEnabled READ rareEventsEnabled NOTIFY configChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(QVariantMap solarState READ solarState NOTIFY solarStateChanged)
    Q_PROPERTY(QVariantMap locationState READ locationState NOTIFY locationStateChanged)

public:
    explicit PixelLandscapeViewModel(QObject* parent = nullptr, LandscapeLocation* location = nullptr);
    ~PixelLandscapeViewModel() override;
    void setStorage(xuanhub::IPluginStorage* storage);
    static QString configurationError(const QVariantMap& config);
    static QString phaseAt(const QTime& time);
    bool applyConfig(const QVariantMap& config);
    void start();
    void stop();
    void refreshAt(const QDateTime& now, bool force = false);
    Q_INVOKABLE void resync();
    Q_INVOKABLE void requestLocation();
    QVariantMap solarState() const { return m_solarState; }
    QVariantMap locationState() const { return m_locationState; }
    QString phase() const { return m_phase; }
    QString previousPhase() const { return m_previousPhase; }
    bool transitionAllowed() const { return m_transitionAllowed; }
    QString weather() const { return m_weather; }
    QString scene() const { return m_scene; }
    QString frameStyle() const { return m_frameStyle; }
    bool animationsEnabled() const { return m_animationsEnabled; }
    bool rareEventsEnabled() const { return m_rareEventsEnabled; }
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
Q_SIGNALS:
    void phaseChanged();
    void configChanged();
    void pausedChanged();
    void solarStateChanged();
    void locationStateChanged();
private:
    friend class PixelLandscapeTest;
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
    void checkLocation(const QDateTime& now, bool interactive = false);
    void cancelLocation();
    void locationFinished(quint64 id, const QString& error, double latitude, double longitude,
                          const QDateTime& timestamp);
    void revokeLocation();
    QTimer m_timer;
    LandscapeLocation* m_location;
    xuanhub::IPluginStorage* m_storage = nullptr;
    QVariantMap m_solarState;
    QVariantMap m_locationState;
    QString m_locationSource = QStringLiteral("system");
    QString m_locationError;
    bool m_manualLocationEnabled = false;
    double m_latitude = 0;
    double m_longitude = 0;
    double m_systemLatitude = 0;
    double m_systemLongitude = 0;
    QDateTime m_positionAt;
    QDateTime m_lastAttempt;
    quint64 m_requestId = 0;
    bool m_pending = false;
    bool m_started = false;
    bool m_resyncQueued = false;
    bool m_livePosition = false;
    QDateTime m_lastCheck;
    QString m_timeMode = QStringLiteral("auto");
    QString m_phase = QStringLiteral("day");
    QString m_previousPhase = QStringLiteral("day");
    QString m_weather = QStringLiteral("clear");
    QString m_scene = QStringLiteral("cabin");
    QString m_frameStyle = QStringLiteral("wood");
    bool m_transitionAllowed = false;
    bool m_animationsEnabled = true;
    bool m_rareEventsEnabled = true;
    bool m_paused = false;
};
