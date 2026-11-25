// MusicViewModel.cpp — 用共享单调时钟选取当前歌词
#include "MusicViewModel.h"

#include <cmath>

MusicViewModel::MusicViewModel() {
    m_lineTimer.setSingleShot(true);
    m_lineTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_lineTimer, &QTimer::timeout, this, &MusicViewModel::updateLine);
}

MusicViewModel::~MusicViewModel() {
    m_thread.requestInterruption();
    m_thread.quit();
    m_thread.wait();
}

void MusicViewModel::start() {
    m_worker = new music::Worker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &music::Worker::start);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &music::Worker::playbackRead, this, &MusicViewModel::acceptPlayback);
    connect(m_worker, &music::Worker::lyricsRead, this, &MusicViewModel::acceptLyrics);
    connect(m_worker, &music::Worker::commandFinished, this, [this](bool accepted) {
        m_pending = false;
        m_commandFailed = !accepted && m_playback.connected;
        Q_EMIT playbackChanged();
    });
    m_thread.start();
}

void MusicViewModel::setTwoLines(bool twoLines) {
    if (m_twoLines == twoLines) return;
    m_twoLines = twoLines;
    Q_EMIT twoLinesChanged();
}

void MusicViewModel::setUniformLineFont(bool uniform) {
    if (m_uniformLineFont == uniform) return;
    m_uniformLineFont = uniform;
    Q_EMIT uniformLineFontChanged();
}

qint64 MusicViewModel::positionMs() const {
    if (!m_playback.hasTimeline) return 0;
    qint64 position = m_playback.positionMs;
    if (playing()) {
        const auto elapsed = std::chrono::steady_clock::now() - m_playback.sampledAt;
        position += qint64(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() * m_playback.rate);
    }
    return durationMs() > 0 ? qBound(qint64(0), position, durationMs()) : qMax(qint64(0), position);
}

void MusicViewModel::acceptPlayback(const music::Playback& playback) {
    const bool trackChanged = m_playback.trackRevision != playback.trackRevision;
    const bool visibilityChanged = m_playback.connected != playback.connected;
    const bool changed = trackChanged || visibilityChanged || m_playback.playing != playback.playing
        || m_playback.track.durationMs != playback.track.durationMs
        || m_playback.canPrevious != playback.canPrevious || m_playback.canToggle != playback.canToggle
        || m_playback.canNext != playback.canNext;
    m_playback = playback;
    if (trackChanged) {
        m_lyrics.clear();
        m_line = -1;
        m_commandFailed = false;
        Q_EMIT linesChanged();
    }
    updateLine();
    if (changed) Q_EMIT playbackChanged();
    if (visibilityChanged) Q_EMIT barItemVisibleChanged();
}

void MusicViewModel::acceptLyrics(quint64 trackRevision, const music::Lyrics& lyrics) {
    if (!barItemVisible() || trackRevision != m_playback.trackRevision) return;
    m_lyrics = lyrics;
    updateLine();
    // 前奏期间当前索引可能仍是 -1，下一句也需要刷新。
    if (m_line == -1) Q_EMIT linesChanged();
}

void MusicViewModel::updateLine() {
    const qint64 position = positionMs();
    const int line = m_playback.hasTimeline ? music::currentLine(m_lyrics, position) : -1;
    if (m_line != line) {
        m_line = line;
        Q_EMIT linesChanged();
    }
    m_lineTimer.stop();
    if (playing() && m_playback.hasTimeline && line + 1 < m_lyrics.size()) {
        const auto next = m_lyrics.at(line + 1).startMs;
        if (durationMs() == 0 || next <= durationMs()) {
            const double delay = std::ceil((next - position) / m_playback.rate);
            m_lineTimer.start(int(qBound(1., delay, 2147483647.)));
        }
    }
}

QString MusicViewModel::lineAt(int index) const {
    return index >= 0 && index < m_lyrics.size() ? m_lyrics.at(index).text : QString{};
}

void MusicViewModel::previous() { if (canPrevious()) send(music::Command::Previous); }
void MusicViewModel::togglePlayback() { if (canToggle()) send(music::Command::Toggle); }
void MusicViewModel::next() { if (canNext()) send(music::Command::Next); }

void MusicViewModel::send(music::Command command) {
    if (!m_worker || !barItemVisible()) return;
    m_pending = true;
    m_commandFailed = false;
    Q_EMIT playbackChanged();
    const auto revision = m_playback.trackRevision;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, command, revision] {
        worker->execute(command, revision);
    }, Qt::QueuedConnection);
}
