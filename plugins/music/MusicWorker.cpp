// MusicWorker.cpp — QQ 音乐的 SMTC 会话与缓存适配
#include "MusicWorker.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>

#include <QTimer>
#include <QThread>
#include <cmath>
#include <type_traits>

namespace music {
namespace {

using namespace winrt::Windows::Media::Control;
using winrt::Windows::Foundation::AsyncStatus;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

QString text(const winrt::hstring& value) {
    return QString::fromWCharArray(value.c_str(), value.size());
}

template<class Operation>
auto awaitResult(const Operation& operation) {
    if (operation.wait_for(750ms) == AsyncStatus::Completed)
        return operation.GetResults();
    operation.Cancel();
    using Result = decltype(operation.GetResults());
    if constexpr (std::is_same_v<Result, bool>) return false;
    else return Result{nullptr};
}

bool isQQMusic(const GlobalSystemMediaTransportControlsSession& session) {
    const QString app = text(session.SourceAppUserModelId());
    return app.compare(QStringLiteral("QQMusic.exe"), Qt::CaseInsensitive) == 0;
}

} // namespace

// 媒体读取与命令共用 SMTC 会话。缓存读取由 QQ 音乐适配器提供。
struct Worker::Session {
    Session() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Session() {
        manager = nullptr;
        player = nullptr;
        winrt::uninit_apartment();
    }

    GlobalSystemMediaTransportControlsSession pick() {
        if (!manager)
            manager = awaitResult(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
        if (manager) {
            GlobalSystemMediaTransportControlsSession candidate{nullptr};
            for (const auto& entry : manager.GetSessions()) {
                if (!isQQMusic(entry)) continue;
                if (entry == player) return entry;
                if (!candidate) candidate = entry;
            }
            return candidate;
        }
        return nullptr;
    }

    Lyrics lyrics(const Track& track) {
        if (cacheDirectory.isEmpty()) cacheDirectory = qqMusicCacheDirectory();
        return readQQMusicLyrics(cacheDirectory, track);
    }

    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    GlobalSystemMediaTransportControlsSession player{nullptr};
    QString cacheDirectory;
};

Worker::Worker() = default;
Worker::~Worker() = default;

void Worker::start() {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Worker::poll);
    poll();
}

void Worker::poll() {
    if (QThread::currentThread()->isInterruptionRequested()) return;
    Playback snapshot;
    snapshot.trackRevision = m_playback.trackRevision;
    try {
        if (!m_session) m_session = std::make_unique<Session>();
        const auto player = m_session->pick();
        const bool sessionChanged = player != m_session->player;
        m_session->player = player;
        if (player) {
            const auto properties = awaitResult(player.TryGetMediaPropertiesAsync());
            if (!properties) {
                m_timer->start(250);
                return;
            }

            const auto info = player.GetPlaybackInfo();
            const auto timeline = player.GetTimelineProperties();
            const auto controls = info.Controls();
            const auto stamp = timeline.LastUpdatedTime();
            const auto ticks = stamp.time_since_epoch().count();
            snapshot.sampledAt = Clock::now();
            snapshot.track = {text(properties.Title()), text(properties.Artist()),
                              text(properties.AlbumTitle()), 0};
            snapshot.connected = !snapshot.track.title.isEmpty();
            snapshot.playing = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            snapshot.canPrevious = controls.IsPreviousEnabled();
            snapshot.canNext = controls.IsNextEnabled();
            snapshot.canToggle = controls.IsPlayPauseToggleEnabled()
                || (snapshot.playing ? controls.IsPauseEnabled() : controls.IsPlayEnabled());
            if (const auto rate = info.PlaybackRate(); rate && std::isfinite(rate.Value()) && rate.Value() > 0)
                snapshot.rate = rate.Value();

            if (sessionChanged || !(snapshot.track == m_playback.track)) {
                ++snapshot.trackRevision;
                m_hasLyrics = false;
                m_nextLyricsRead = {};
                // 媒体属性可能先于新曲的时间轴到达。期间保持归零，避免沿用上一首的进度。
                m_waitingForTimeline = !sessionChanged && ticks == m_timelineStamp;
            }
            if (ticks != m_timelineStamp) m_waitingForTimeline = false;
            m_timelineStamp = ticks;
            if (!m_waitingForTimeline) {
                snapshot.positionMs = qMax(qint64(0), qint64(timeline.Position().count() / 10000));
                snapshot.track.durationMs = qMax(qint64(0), qint64(timeline.EndTime().count() / 10000));
                snapshot.hasTimeline = ticks != 0;
                if (snapshot.playing && snapshot.hasTimeline) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        winrt::clock::now() - stamp).count();
                    snapshot.positionMs += qint64(qMax(qint64(0), qint64(elapsed)) * snapshot.rate);
                }
            }
        } else if (m_playback.connected) {
            ++snapshot.trackRevision;
        }
    } catch (const winrt::hresult_error&) {
        m_session.reset();
        snapshot = {};
        snapshot.trackRevision = m_playback.trackRevision + 1;
        m_hasLyrics = false;
    }

    m_playback = snapshot;
    Q_EMIT playbackRead(snapshot);
    const auto now = Clock::now();
    if (snapshot.connected && snapshot.hasTimeline && !m_hasLyrics && now >= m_nextLyricsRead) {
        m_nextLyricsRead = now + 2s;
        const auto lines = m_session->lyrics(snapshot.track);
        if (!lines.isEmpty()) {
            m_hasLyrics = true;
            Q_EMIT lyricsRead(snapshot.trackRevision, lines);
        }
    }
    m_timer->start(snapshot.connected ? (snapshot.playing ? 250 : 500) : 1000);
}

void Worker::execute(Command command, quint64 expectedTrack) {
    bool accepted = false;
    const auto player = m_session ? m_session->player : nullptr;
    if (player && m_playback.connected && m_playback.trackRevision == expectedTrack) {
        try {
            const auto info = player.GetPlaybackInfo();
            const auto controls = info.Controls();
            switch (command) {
            case Command::Previous:
                if (controls.IsPreviousEnabled()) accepted = awaitResult(player.TrySkipPreviousAsync());
                break;
            case Command::Next:
                if (controls.IsNextEnabled()) accepted = awaitResult(player.TrySkipNextAsync());
                break;
            case Command::Toggle:
                if (controls.IsPlayPauseToggleEnabled())
                    accepted = awaitResult(player.TryTogglePlayPauseAsync());
                else if (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                    if (controls.IsPauseEnabled()) accepted = awaitResult(player.TryPauseAsync());
                } else if (controls.IsPlayEnabled())
                    accepted = awaitResult(player.TryPlayAsync());
                break;
            }
        } catch (const winrt::hresult_error&) {
            accepted = false;
        }
    }
    poll();
    Q_EMIT commandFinished(accepted);
}

} // namespace music
