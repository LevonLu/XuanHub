// PixelLandscapeViewModel.h — 离线时段、配置与实例级暂停状态
#pragma once

#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <QVariantMap>

class PixelLandscapeViewModel final : public QObject {
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

public:
    explicit PixelLandscapeViewModel(QObject* parent = nullptr);
    static QString configurationError(const QVariantMap& config);
    static QString phaseAt(const QTime& time);
    bool applyConfig(const QVariantMap& config);
    void start();
    void stop();
    void refreshAt(const QDateTime& now, bool force = false);
    Q_INVOKABLE void resync();
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
private:
    QTimer m_timer;
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
