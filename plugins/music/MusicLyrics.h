// MusicLyrics.h — 本地歌词的行时间轴与 QQ 音乐缓存读取
#pragma once

#include <QString>
#include <QVector>

namespace music {

struct Track {
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = 0;
    bool operator==(const Track& other) const {
        return title == other.title && artist == other.artist && album == other.album;
    }
};

struct LyricLine {
    qint64 startMs;
    QString text;
};

using Lyrics = QVector<LyricLine>;

Lyrics readQrc(const QByteArray& bytes);
Lyrics readLrc(const QByteArray& bytes);
Lyrics readQQMusicLyrics(const QString& directory, const Track& track);
QString qqMusicCacheDirectory();
int currentLine(const Lyrics& lyrics, qint64 positionMs);

} // namespace music
