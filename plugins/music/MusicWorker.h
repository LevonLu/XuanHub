// MusicWorker.h — 播放器会话与歌词工作线程接口
#pragma once

#include "MusicLyrics.h"

#include <QObject>
#include <chrono>
#include <memory>

class QTimer;

namespace music {

enum class Command { Previous, Toggle, Next };

// 位置以 sampledAt 为锚点，界面使用同一单调时钟推进歌词。
struct Playback {
    Track track;
    quint64 trackRevision = 0;
    qint64 positionMs = 0;
    std::chrono::steady_clock::time_point sampledAt;
    double rate = 1.;
    bool connected = false;
    bool playing = false;
    bool hasTimeline = false;
    bool canPrevious = false;
    bool canToggle = false;
    bool canNext = false;
};

class Worker final : public QObject {
    Q_OBJECT
public:
    Worker();
    ~Worker() override;

    void start();
    void execute(Command command, quint64 expectedTrack);

Q_SIGNALS:
    void playbackRead(const Playback& playback);
    void lyricsRead(quint64 trackRevision, const Lyrics& lyrics);
    void commandFinished(bool accepted);

private:
    void poll();

    struct Session;
    std::unique_ptr<Session> m_session;
    QTimer* m_timer = nullptr;
    Playback m_playback;
    qint64 m_timelineStamp = 0;
    bool m_waitingForTimeline = false;
    std::chrono::steady_clock::time_point m_nextLyricsRead;
    bool m_hasLyrics = false;
};

} // namespace music

Q_DECLARE_METATYPE(music::Playback)
Q_DECLARE_METATYPE(music::Lyrics)
