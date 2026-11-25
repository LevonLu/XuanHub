// MusicLyrics.cpp — 解析本地 QRC/LRC 并按曲目选择 QQ 音乐缓存
#include "MusicLyrics.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStringConverter>
#include <QXmlStreamReader>

#include <algorithm>
#include <zlib.h>

#include "third_party/qqmusicdes/des.h"

namespace music {
namespace {

constexpr qsizetype maxFileBytes = 1024 * 1024;
constexpr qsizetype maxTextBytes = 2 * 1024 * 1024;

// QQMusicMonitor (MIT) 记录的 QQ 音乐本地 QRC 格式。密钥只用于歌词缓存。
constexpr unsigned char mask[] = {
    0xc3,0x4a,0xd6,0xca,0x90,0x67,0xf7,0x52,0xd8,0xa1,0x66,0x62,0x9f,0x5b,0x09,0x00,
    0xc3,0x5e,0x95,0x23,0x9f,0x13,0x11,0x7e,0xd8,0x92,0x3f,0xbc,0x90,0xbb,0x74,0x0e,
    0xc3,0x47,0x74,0x3d,0x90,0xaa,0x3f,0x51,0xd8,0xf4,0x11,0x84,0x9f,0xde,0x95,0x1d,
    0xc3,0xc6,0x09,0xd5,0x9f,0xfa,0x66,0xf9,0xd8,0xf0,0xf7,0xa0,0x90,0xa1,0xd6,0xf3,
    0xc3,0xf3,0xd6,0xa1,0x90,0xa0,0xf7,0xf0,0xd8,0xf9,0x66,0xfa,0x9f,0xd5,0x09,0xc6,
    0xc3,0x1d,0x95,0xde,0x9f,0x84,0x11,0xf4,0xd8,0x51,0x3f,0xaa,0x90,0x3d,0x74,0x47,
    0xc3,0x0e,0x74,0xbb,0x90,0xbc,0x3f,0x92,0xd8,0x7e,0x11,0x13,0x9f,0x23,0x95,0x5e,
    0xc3,0x00,0x09,0x5b,0x9f,0x62,0x66,0xa1,0xd8,0x52,0xf7,0x67,0x90,0xca,0xd6,0x4a,
};

void crypt(QByteArray& bytes, const unsigned char* key, DES_MODE mode) {
    BYTE schedule[16][6];
    des_key_setup(key, schedule, mode);
    auto* data = reinterpret_cast<unsigned char*>(bytes.data());
    for (qsizetype offset = 0; offset < bytes.size(); offset += 8)
        des_crypt(data + offset, data + offset, schedule);
}

QByteArray qrcText(QByteArray bytes) {
    static const QByteArray magic = QByteArray::fromHex("9825b0ace3028368e8fc6c");
    const bool wrapped = bytes.startsWith(magic);
    const qsizetype payloadSize = bytes.size() - (wrapped ? magic.size() : 0);
    if (payloadSize <= 0 || payloadSize % 8 != 0 || bytes.size() > maxFileBytes) return {};
    if (wrapped) {
        for (qsizetype i = magic.size(); i < bytes.size(); ++i) {
            const auto index = ((i <= 0x7fff ? i : i % 0x7fff) & 0x7f);
            bytes[i] = char(static_cast<unsigned char>(bytes.at(i)) ^ mask[index]);
        }
        bytes.remove(0, magic.size());
        crypt(bytes, reinterpret_cast<const unsigned char*>("!@#)(NHL"), DES_DECRYPT);
        crypt(bytes, reinterpret_cast<const unsigned char*>("123ZXC!@"), DES_ENCRYPT);
        crypt(bytes, reinterpret_cast<const unsigned char*>("!@#)(*$%"), DES_DECRYPT);
    } else {
        crypt(bytes, reinterpret_cast<const unsigned char*>("!@#)(*$^"), DES_DECRYPT);
    }

    QByteArray decoded(maxTextBytes, Qt::Uninitialized);
    uLongf size = static_cast<uLongf>(decoded.size());
    if (uncompress(reinterpret_cast<Bytef*>(decoded.data()), &size,
                   reinterpret_cast<const Bytef*>(bytes.constData()),
                   static_cast<uLong>(bytes.size())) != Z_OK)
        return {};
    decoded.resize(size);
    return decoded;
}

Lyrics sorted(Lyrics lines) {
    std::stable_sort(lines.begin(), lines.end(), [](const LyricLine& a, const LyricLine& b) {
        return a.startMs < b.startMs;
    });
    return lines;
}

QString normalized(const QString& text) {
    static const QRegularExpression punctuation(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded()
        .replace(punctuation, QStringLiteral(" ")).trimmed();
}

QByteArray readFile(const QFileInfo& info) {
    if (info.size() > maxFileBytes) return {};
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(maxFileBytes + 1);
    return bytes.size() <= maxFileBytes ? bytes : QByteArray{};
}

} // namespace

Lyrics readQrc(const QByteArray& bytes) {
    const QByteArray decoded = qrcText(bytes);
    if (decoded.isEmpty()) return {};
    // XML 读取属性会折叠换行。QRC 的段首时间标签本身就能界定每一行。
    QXmlStreamReader xml(decoded);
    static const QRegularExpression time(QStringLiteral("\\[(\\d{1,12}),\\d{1,12}\\]"));
    static const QRegularExpression wordTime(QStringLiteral("\\(\\d{1,12},\\d{1,12}\\)"));
    Lyrics lines;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        const auto attributes = xml.attributes();
        if (xml.name() == u"LyricLine") {
            bool ok = false;
            const qint64 start = attributes.value(u"StartTime").toLongLong(&ok);
            const QString text = attributes.value(u"LyricContent").toString().trimmed();
            if (ok && start >= 0 && !text.isEmpty()) lines.append({start, text});
            continue;
        }
        const QString content = attributes.value(u"LyricContent").toString();
        if (content.isEmpty()) continue;
        auto times = time.globalMatch(content);
        while (times.hasNext()) {
            const auto match = times.next();
            const qsizetype end = times.hasNext() ? times.peekNext().capturedStart() : content.size();
            QString line = content.mid(match.capturedEnd(), end - match.capturedEnd());
            line.remove(wordTime);
            if (!line.trimmed().isEmpty()) lines.append({match.captured(1).toLongLong(), line.trimmed()});
        }
    }
    return xml.hasError() ? Lyrics{} : sorted(std::move(lines));
}

Lyrics readLrc(const QByteArray& bytes) {
    const auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    QStringDecoder decoder(encoding);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError()) return {};

    static const QRegularExpression stamp(QStringLiteral("\\[(\\d{1,5}):(\\d{2})(?:[.:](\\d{1,3}))?\\]"));
    static const QRegularExpression offsetTag(QStringLiteral("\\[offset:([+-]?\\d{1,9})\\]"),
                                              QRegularExpression::CaseInsensitiveOption);
    const qint64 offset = offsetTag.match(text).captured(1).toLongLong();
    Lyrics lines;
    for (const QString& row : text.split(u'\n')) {
        QVector<qint64> starts;
        qsizetype end = 0;
        auto matches = stamp.globalMatch(row);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (match.capturedStart() != end) break;
            end = match.capturedEnd();
            starts.append(match.captured(1).toLongLong() * 60000
                          + match.captured(2).toLongLong() * 1000
                          + match.captured(3).leftJustified(3, u'0').toLongLong());
        }
        const QString lyric = row.mid(end).trimmed();
        // 空行时间戳可清除前一句，保留间奏。
        for (qint64 start : starts) lines.append({qMax(qint64(0), start + offset), lyric});
    }
    if (std::none_of(lines.cbegin(), lines.cend(), [](const LyricLine& line) { return !line.text.isEmpty(); }))
        return {};
    return sorted(std::move(lines));
}

QString qqMusicCacheDirectory() {
    const QString appData = qEnvironmentVariable("APPDATA");
    if (appData.isEmpty()) return {};
    const QDir settings(QDir(appData).filePath(QStringLiteral("Tencent/QQMusic")));
    // QQ 音乐用单个反斜杠保存 Windows 路径，不能按 QSettings 的转义规则读取。
    const QByteArray ini = readFile(QFileInfo(settings.filePath(QStringLiteral("WebkitCachePath.ini"))));
    QStringDecoder decoder(QStringConverter::encodingForData(ini).value_or(QStringConverter::Utf8));
    QString config = decoder.decode(ini);
    if (decoder.hasError()) config = QString::fromLocal8Bit(ini);
    for (const QString& line : config.split(u'\n')) {
        const qsizetype split = line.indexOf(u'=');
        if (split < 0 || line.left(split).trimmed().compare(QStringLiteral("Path"), Qt::CaseInsensitive) != 0)
            continue;
        const QString webkitPath = QDir::fromNativeSeparators(line.mid(split + 1).trimmed());
        if (QDir::isAbsolutePath(webkitPath)) {
            const QDir root(QFileInfo(webkitPath).absolutePath());
            if (root.exists(QStringLiteral("QQMusicLyricNew")))
                return root.filePath(QStringLiteral("QQMusicLyricNew"));
        }
    }
    const QString standard = settings.filePath(QStringLiteral("QQMusicCache/QQMusicLyricNew"));
    if (QDir(standard).exists()) return standard;
    for (const QFileInfo& drive : QDir::drives()) {
        const QString path = QDir(drive.absoluteFilePath()).filePath(QStringLiteral("QQMusicCache/QQMusicLyricNew"));
        if (QDir(path).exists()) return path;
    }
    return {};
}

Lyrics readQQMusicLyrics(const QString& directory, const Track& track) {
    const QString title = normalized(track.title);
    const QString artist = normalized(track.artist);
    if (directory.isEmpty() || title.isEmpty() || artist.isEmpty()) return {};
    const QString album = normalized(track.album);
    const auto entries = QDir(directory).entryInfoList(
        {QStringLiteral("*_qm.qrc"), QStringLiteral("*_qm.lrc")}, QDir::Files | QDir::Readable);
    struct Candidate { QFileInfo file; int score; QString version; };
    QVector<Candidate> candidates;
    for (const QFileInfo& entry : entries) {
        const QString base = entry.completeBaseName().chopped(3);
        const QStringList fields = base.split(QStringLiteral(" - "));
        if (fields.size() < 4) continue;
        // 曲名、歌手和专辑都可能带分隔符。定位时长，再尝试歌手与曲名的分界。
        for (qsizetype durationIndex = 2; durationIndex < fields.size() - 1; ++durationIndex) {
            bool ok = false;
            const qint64 seconds = fields.at(durationIndex).toInt(&ok);
            if (!ok || seconds <= 0) continue;
            for (qsizetype boundary = 1; boundary < durationIndex; ++boundary) {
                if (normalized(fields.mid(0, boundary).join(u' ')) != artist
                    || normalized(fields.mid(boundary, durationIndex - boundary).join(u' ')) != title)
                    continue;
                const auto difference = qAbs(track.durationMs - seconds * 1000);
                if (track.durationMs > 0 && difference > 3000) continue;
                const QString candidateAlbum = normalized(fields.mid(durationIndex + 1).join(u' '));
                const int score = (!album.isEmpty() && album == candidateAlbum ? 4000 : 0)
                    + (track.durationMs > 0 ? 3000 - int(difference) : 0);
                candidates.append({entry, score, base});
            }
        }
    }
    if (candidates.isEmpty()) return {};
    const auto best = std::max_element(candidates.cbegin(), candidates.cend(),
        [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
    for (const auto& candidate : candidates) {
        if (candidate.score == best->score && candidate.version != best->version)
            return {};
    }
    for (const QString& format : {QStringLiteral("qrc"), QStringLiteral("lrc")}) {
        for (const auto& candidate : candidates) {
            if (candidate.version != best->version || candidate.file.suffix().compare(format, Qt::CaseInsensitive) != 0)
                continue;
            const auto bytes = readFile(candidate.file);
            auto lyrics = format == QStringLiteral("qrc") ? readQrc(bytes) : readLrc(bytes);
            if (!lyrics.isEmpty()) return lyrics;
        }
    }
    return {};
}

int currentLine(const Lyrics& lyrics, qint64 positionMs) {
    const auto next = std::upper_bound(lyrics.cbegin(), lyrics.cend(), positionMs,
        [](qint64 pos, const LyricLine& line) { return pos < line.startMs; });
    return int(next - lyrics.cbegin()) - 1;
}

} // namespace music

