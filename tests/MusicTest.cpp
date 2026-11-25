// MusicTest.cpp — 歌词缓存匹配和播放时间轴回归
#include "MusicViewModel.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

class MusicTest final : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/app/qml/PluginPopup.qml")),
                        "XuanHub", 1, 0, "PluginPopup");
    }

    void matchesVersionAndFallsBackFromBrokenQrc() {
        QTemporaryDir cache;
        QVERIFY(cache.isValid());
        auto write = [&cache](const QString& name, const QByteArray& bytes) {
            QFile file(cache.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write(QStringLiteral("歌手 - 曲名 - 231 - 专辑_qm.qrc"), QByteArray(16, '\0')));
        QVERIFY(write(QStringLiteral("歌手 - 曲名 - 231 - 专辑_qm.lrc"),
                      "[offset:100]\n[00:01.50][00:05.000]first\n[00:06.3]second"));
        QVERIFY(write(QStringLiteral("歌手 - 曲名 - 231 - 现场_qm.lrc"), "[00:01.00]live"));
        QVERIFY(write(QStringLiteral("其他歌手 - 曲名 - 231 - 专辑_qm.lrc"), "[00:01.00]wrong"));
        const music::Track track{QStringLiteral("曲名"), QStringLiteral("歌手"), QStringLiteral("专辑"), 231300};
        const auto lyrics = music::readQQMusicLyrics(cache.path(), track);
        QCOMPARE(lyrics.size(), 3);
        QCOMPARE(lyrics.at(0).startMs, 1600);
        QCOMPARE(lyrics.at(1).startMs, 5100);
        QCOMPARE(lyrics.at(2).startMs, 6400);
        QCOMPARE(lyrics.at(0).text, QStringLiteral("first"));
        QCOMPARE(music::currentLine(lyrics, 1599), -1);
        QCOMPARE(music::currentLine(lyrics, 1600), 0);
        QCOMPARE(music::currentLine(lyrics, 6200), 1);
        QCOMPARE(music::currentLine(lyrics, 6400), 2);
        auto ambiguous = track;
        ambiguous.album.clear();
        QVERIFY(music::readQQMusicLyrics(cache.path(), ambiguous).isEmpty());
    }

    void synchronizesPauseSeekAndLateLyrics() {
        MusicViewModel model;
        model.setTwoLines(true);
        QVERIFY(model.twoLines());
        QVERIFY(!model.barItemVisible());
        music::Playback sample;
        sample.connected = true;
        sample.playing = true;
        sample.hasTimeline = true;
        sample.canToggle = true;
        sample.sampledAt = std::chrono::steady_clock::now();
        sample.trackRevision = 1;
        sample.track.title = QStringLiteral("第一首");
        model.acceptPlayback(sample);
        QVERIFY(model.barItemVisible());
        QVERIFY(model.canToggle());
        QVERIFY(!model.canNext());
        QVERIFY(model.currentLine().isEmpty());
        model.acceptLyrics(1, {{0, QStringLiteral("甲")}, {150, QStringLiteral("乙")}});
        QCOMPARE(model.currentLine(), QStringLiteral("甲"));
        QCOMPARE(model.nextLine(), QStringLiteral("乙"));
        QTRY_COMPARE_WITH_TIMEOUT(model.currentLine(), QStringLiteral("乙"), 1000);
        sample.playing = false;
        sample.positionMs = 160;
        model.acceptPlayback(sample);
        QCOMPARE(model.positionMs(), 160);
        QCOMPARE(model.previousLine(), QStringLiteral("甲"));
        QTest::qWait(40);
        QCOMPARE(model.positionMs(), 160);
        sample.positionMs = 10;
        model.acceptPlayback(sample);
        QCOMPARE(model.currentLine(), QStringLiteral("甲"));

        ++sample.trackRevision;
        sample.track.title = QStringLiteral("第二首");
        model.acceptPlayback(sample);
        QVERIFY(model.currentLine().isEmpty());
        model.acceptLyrics(1, {{0, QStringLiteral("过期")}});
        QVERIFY(model.currentLine().isEmpty());
        model.acceptLyrics(2, {{0, QStringLiteral("丙")}});
        QCOMPARE(model.currentLine(), QStringLiteral("丙"));

        model.acceptPlayback({});
        QVERIFY(!model.barItemVisible());
        QVERIFY(!model.canToggle());
        QVERIFY(model.currentLine().isEmpty());
        model.acceptPlayback(sample);
        QVERIFY(model.barItemVisible());
    }

    void realQrcCache() {
        const QString path = qEnvironmentVariable("XUANHUB_MUSIC_TEST_QRC");
        if (path.isEmpty()) QSKIP("运行时可指定本机 QQ 音乐样本。");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lyrics = music::readQrc(file.readAll());
        QVERIFY2(lyrics.size() > 3, "真实 QQ 音乐 QRC 需要包含有效的歌词行");
        qInfo() << "Decoded timed lines:" << lyrics.size();
        const auto position = qEnvironmentVariableIntValue("XUANHUB_MUSIC_TEST_POSITION");
        if (position > 0) {
            const int line = music::currentLine(lyrics, position);
            QVERIFY(line >= 0);
            QVERIFY(!lyrics.at(line).text.isEmpty());
            qInfo() << "Selected timestamp ms:" << lyrics.at(line).startMs;
        }
    }

    void rendersDoubleLineAndPopup() {
        MusicViewModel model;
        music::Playback sample;
        sample.connected = true;
        sample.hasTimeline = true;
        sample.trackRevision = 1;
        sample.track = {QStringLiteral("测试曲目"), QStringLiteral("测试歌手"), {}, 3500};
        sample.positionMs = 1600;
        sample.canToggle = true;
        model.acceptPlayback(sample);
        model.acceptLyrics(1, {{0, QStringLiteral("上一句歌词")},
                               {1000, QStringLiteral("现在唱到这一句")},
                               {2000, QStringLiteral("下一句歌词")}});
        QQuickView view;
        QSignalSpy warnings(view.engine(), &QQmlEngine::warnings);
        auto* popup = openPanel(view, model);
        QVERIFY(popup);
        auto* next = view.rootObject()->findChild<QQuickItem*>("musicNextLine");
        auto* current = view.rootObject()->findChild<QQuickItem*>("musicCurrentLine");
        QVERIFY(next);
        QVERIFY(current);
        const auto fontSize = [](QQuickItem* line) {
            return line->property("font").value<QFont>().pixelSize();
        };
        QVERIFY(!next->isVisible());
        QCOMPARE(fontSize(current), 13);
        model.setTwoLines(true);
        QVERIFY(next->isVisible());
        QCOMPARE(next->property("text").toString(), QStringLiteral("下一句歌词"));
        QCOMPARE(fontSize(current), 15);
        QCOMPARE(fontSize(next), 11);
        view.rootObject()->setProperty("hostContentHeight", 32);
        QCOMPARE(current->height() + next->height(), 32.);
        model.setUniformLineFont(true);
        QVERIFY(next->isVisible());
        QCOMPARE(fontSize(current), 14);
        QCOMPARE(fontSize(next), 14);
        QCOMPARE(current->height() + next->height(), 32.);
        view.rootObject()->setProperty("hostContentHeight", 24);
        QVERIFY(!next->isVisible());
        QCOMPARE(fontSize(current), 13);
        view.rootObject()->setProperty("hostContentHeight", 40);
        view.rootObject()->setProperty("hostContentWidth", 125);
        QCOMPARE(view.rootObject()->width(), 125.);
        QCOMPARE(view.rootObject()->implicitWidth(), 240.);
        QCOMPARE(current->mapToItem(view.rootObject(), QPointF()).x(), 8.);
        QCOMPARE(next->mapToItem(view.rootObject(), QPointF()).x(), 8.);
        QCOMPARE(current->width(), 109.);
        view.rootObject()->setProperty("hostContentWidth", 12);
        QCOMPARE(current->mapToItem(view.rootObject(), QPointF()).x(), 3.);
        QCOMPARE(current->width(), 6.);
        auto* lyric = popup->findChild<QQuickItem*>("musicPopupCurrentLine");
        QVERIFY(lyric);
        QCOMPARE(lyric->property("text").toString(), QStringLiteral("现在唱到这一句"));
        QVERIFY(popup->findChild<QQuickItem*>("musicToggle")->isEnabled());
        QVERIFY(!popup->findChild<QQuickItem*>("musicNext")->isEnabled());
        const auto screenshot = qEnvironmentVariable("XUANHUB_MUSIC_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(popup->grabWindow().save(screenshot));
        QVERIFY2(warnings.isEmpty(), "音乐插件 QML 产生绑定警告");
    }

    void realPlayer() {
        if (!qEnvironmentVariableIsSet("XUANHUB_MUSIC_TEST_PLAYER"))
            QSKIP("仅在显式启用时控制当前 QQ 音乐会话。");
        MusicViewModel model;
        model.start();
        QTRY_VERIFY_WITH_TIMEOUT(model.barItemVisible(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(model.durationMs() > 0 && model.canToggle(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!model.m_lyrics.isEmpty(), 8000);
        QQuickView view;
        auto* popup = openPanel(view, model);
        QVERIFY(popup);
        const QString originalTitle = model.title();
        const bool originallyPlaying = model.playing();
        const auto restorePlayback = qScopeGuard([&] {
            if (model.canToggle() && model.playing() != originallyPlaying) {
                model.togglePlayback();
                QTest::qWait(600);
            }
        });
        QVERIFY(click(popup, "musicToggle"));
        QTRY_COMPARE_WITH_TIMEOUT(model.playing(), !originallyPlaying, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(model.canToggle(), 3000);
        QVERIFY(click(popup, "musicToggle"));
        QTRY_COMPARE_WITH_TIMEOUT(model.playing(), originallyPlaying, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(model.canNext(), 3000);
        QVERIFY(click(popup, "musicNext"));
        QTRY_VERIFY_WITH_TIMEOUT(model.title() != originalTitle, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(model.canPrevious(), 3000);
        const QString nextTitle = model.title();
        QVERIFY(click(popup, "musicPrevious"));
        QTRY_VERIFY_WITH_TIMEOUT(model.title() != nextTitle, 5000);
    }

private:
    static QQuickWindow* openPanel(QQuickView& view, MusicViewModel& model) {
        view.setInitialProperties({{QStringLiteral("plugin"), QVariant::fromValue(&model)}});
        view.setSource(QUrl::fromLocalFile(QStringLiteral(XUANHUB_SOURCE_DIR "/plugins/music/Music.qml")));
        if (view.status() != QQuickView::Ready) return nullptr;
        auto* popup = view.rootObject()->findChild<QQuickWindow*>("musicPopup");
        if (!popup) return nullptr;
        popup->setProperty("closeOnDeactivate", false);
        view.show();
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(80, 20));
        QTest::qWait(120);
        return popup->isVisible() ? popup : nullptr;
    }

    static bool click(QQuickWindow* popup, const char* name) {
        auto* button = popup->findChild<QQuickItem*>(name);
        if (!button || !button->isEnabled()) return false;
        const auto center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        return true;
    }
};

QTEST_MAIN(MusicTest)
#include "MusicTest.moc"
