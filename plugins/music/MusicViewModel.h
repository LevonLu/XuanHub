// MusicViewModel.h — 播放状态、歌词和设置的 QML 接口
#pragma once

#include "MusicWorker.h"

#include <QThread>
#include <QTimer>

class MusicViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool barItemVisible READ barItemVisible NOTIFY barItemVisibleChanged)
    Q_PROPERTY(bool twoLines READ twoLines WRITE setTwoLines NOTIFY twoLinesChanged)
    Q_PROPERTY(bool uniformLineFont READ uniformLineFont NOTIFY uniformLineFontChanged)
    Q_PROPERTY(QString title READ title NOTIFY playbackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY playbackChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY playbackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY playbackChanged)
    Q_PROPERTY(bool canToggle READ canToggle NOTIFY playbackChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY playbackChanged)
    Q_PROPERTY(bool commandFailed READ commandFailed NOTIFY playbackChanged)
    Q_PROPERTY(QString previousLine READ previousLine NOTIFY linesChanged)
    Q_PROPERTY(QString currentLine READ currentLine NOTIFY linesChanged)
    Q_PROPERTY(QString nextLine READ nextLine NOTIFY linesChanged)

public:
    MusicViewModel();
    ~MusicViewModel() override;
    void start();

    bool barItemVisible() const { return m_playback.connected; }
    bool twoLines() const { return m_twoLines; }
    void setTwoLines(bool twoLines);
    bool uniformLineFont() const { return m_uniformLineFont; }
    void setUniformLineFont(bool uniform);

    QString title() const { return m_playback.track.title; }
    QString artist() const { return m_playback.track.artist; }
    qint64 durationMs() const { return m_playback.track.durationMs; }
    bool playing() const { return m_playback.playing; }
    bool canPrevious() const { return !m_pending && m_playback.canPrevious; }
    bool canToggle() const { return !m_pending && m_playback.canToggle; }
    bool canNext() const { return !m_pending && m_playback.canNext; }
    bool commandFailed() const { return m_commandFailed; }

    QString previousLine() const { return lineAt(m_line - 1); }
    QString currentLine() const { return lineAt(m_line); }
    QString nextLine() const { return lineAt(m_line + 1); }

    Q_INVOKABLE qint64 positionMs() const;
    Q_INVOKABLE void previous();
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void next();

Q_SIGNALS:
    void barItemVisibleChanged();
    void twoLinesChanged();
    void uniformLineFontChanged();
    void playbackChanged();
    void linesChanged();

private:
    friend class MusicTest;

    void acceptPlayback(const music::Playback& playback);
    void acceptLyrics(quint64 trackRevision, const music::Lyrics& lyrics);
    void updateLine();
    QString lineAt(int index) const;
    void send(music::Command command);

    QThread m_thread;
    music::Worker* m_worker = nullptr;
    QTimer m_lineTimer;
    music::Playback m_playback;
    music::Lyrics m_lyrics;
    int m_line = -1;
    bool m_twoLines = false;
    bool m_uniformLineFont = false;
    bool m_pending = false;
    bool m_commandFailed = false;
};
