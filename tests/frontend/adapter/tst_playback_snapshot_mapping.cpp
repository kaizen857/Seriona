#include "playback_controller.h"
#include "artwork_palette_worker.h"

#include "seriona/control/control_contracts.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSemaphore>
#include <QSignalSpy>
#include <QString>
#include <QtTest/QTest>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>

namespace {

seriona::control::PlayerStateSnapshot makeFilledSnapshot(seriona::control::PlaybackStatus status)
{
    seriona::control::PlayerStateSnapshot snapshot;
    snapshot.playback.state = status;

    snapshot.currentTrack = seriona::control::TrackIdentity{};
    snapshot.currentTrack->trackId = "track-echo";

    snapshot.display = seriona::control::DisplayMetadata{};
    snapshot.display->title = "Seriona Echo";
    snapshot.display->artist = "Adapter Fixture";
    snapshot.display->album = "Contract Smoke";

    snapshot.timeline.position = std::chrono::milliseconds{42000};
    snapshot.timeline.duration = std::chrono::milliseconds{185000};
    snapshot.volume = 0.35F;
    snapshot.shuffle = true;
    snapshot.repeatMode = seriona::control::RepeatMode::All;

    snapshot.capabilities.canPlay = true;
    snapshot.capabilities.canPause = true;
    snapshot.capabilities.canStop = true;
    snapshot.capabilities.canSeek = true;
    snapshot.capabilities.canSkipNext = true;
    snapshot.capabilities.canSkipPrevious = true;
    snapshot.capabilities.canSetRepeat = true;
    snapshot.capabilities.canSetShuffle = true;
    snapshot.capabilities.canSetVolume = true;
    snapshot.capabilities.canSelectTrack = true;

    return snapshot;
}

seriona::control::PlayerStateSnapshot makeTimelineSnapshot(
    seriona::control::PlaybackStatus status,
    std::chrono::milliseconds position,
    std::optional<std::chrono::milliseconds> duration)
{
    seriona::control::PlayerStateSnapshot snapshot;
    snapshot.playback.state = status;
    snapshot.freshness.version = 1;
    snapshot.freshness.sampledAt = std::chrono::steady_clock::now();
    snapshot.timeline.position = position;
    snapshot.timeline.duration = duration;
    return snapshot;
}

void expectPinnedSnapshotPosition(
    Seriona::App::PlaybackController &controller,
    seriona::control::PlaybackStatus status,
    qreal position)
{
    seriona::control::PlayerStateSnapshot snapshot = makeTimelineSnapshot(
        status,
        std::chrono::milliseconds{qRound64(position * 1000.0)},
        std::chrono::milliseconds{60000});
    snapshot.freshness.sampledAt -= std::chrono::seconds{3};

    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.currentPosition(), position);
    QTest::qWait(180);
    QCOMPARE(controller.currentPosition(), position);
}

void applyAndCompareStoppedState(Seriona::App::PlaybackController &controller,
    seriona::control::PlayerStateSnapshot &snapshot,
    seriona::control::PlaybackStatus status)
{
    snapshot.playback.state = status;
    controller.applyPlayerStateSnapshot(snapshot);
    QCOMPARE(controller.isPlaying(), false);
}

// 构造含单曲目的库快照（trackId 可解析出标题/艺术家），供队列映射测试使用。
seriona::control::LibraryStateSnapshot makeLibrarySnapshotWithSong(
    const std::string &trackId,
    const std::string &nodeId,
    const std::string &title,
    const std::string &artist)
{
    seriona::control::LibraryStateSnapshot library;
    seriona::scanner::PlaylistTreeSnapshot tree;
    seriona::scanner::SongMetadata song;
    song.trackId = trackId;
    song.title = title;
    song.artist = artist;
    seriona::scanner::PlaylistNode node;
    node.nodeId = nodeId;
    node.kind = seriona::scanner::PlaylistNodeKind::Track;
    node.displayName = title;
    node.song = std::move(song);
    tree.rootNodeId = std::string{"root"};
    tree.nodes = {std::move(node)};
    library.libraryTree = std::move(tree);
    return library;
}

}

class PlaybackSnapshotMappingTest : public QObject
{
    Q_OBJECT

private slots:
    void snapshotMapping();
    void emptySnapshot();
    void timelineSmoothing();
    void timelineStoppedAndErrorSnapToBackend();
    void coverArtworkSourcesUrlEncodeRawPaths();
    void emptyArtExposesEmptySources();
    void thumbnailThenFullUpgradeSkipsSecondPaletteDecode();
    void slowPaletteDecoderDoesNotDelaySnapshotApplication();
    void rapidThumbnailUpdatesDeliverOnlyLatestPalette();
    void latePaletteResultDroppedAfterTrackSwitch();
    void stalePriorTrackFullArtworkDoesNotLeak();
    void shutdownWithBlockedDecoder();
    void queueEntriesMapping();
};

void PlaybackSnapshotMappingTest::snapshotMapping()
{
    Seriona::App::PlaybackController controller;
    seriona::control::PlayerStateSnapshot snapshot = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);

    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.isPlaying(), true);
    QCOMPARE(controller.songTitle(), QStringLiteral("Seriona Echo"));
    QCOMPARE(controller.artistName(), QStringLiteral("Adapter Fixture"));
    QCOMPARE(controller.albumName(), QStringLiteral("Contract Smoke"));
    QCOMPARE(controller.currentPosition(), 42.0);
    QCOMPARE(controller.totalDuration(), 185.0);
    QCOMPARE(controller.currentPositionText(), QStringLiteral("00:42"));
    QCOMPARE(controller.totalDurationText(), QStringLiteral("03:05"));
    QCOMPARE(controller.remainingDurationText(), QStringLiteral("-02:23"));
    QCOMPARE(controller.volume(), static_cast<qreal>(0.35F));
    QCOMPARE(controller.isShuffle(), true);
    QCOMPARE(controller.repeatMode(), 1);
    QCOMPARE(controller.capability(), QStringLiteral("play,pause,stop,seek,skip-next,skip-previous,set-repeat,set-shuffle,set-volume,select-track"));

    // T6（需求 4）切轨新语义：后端 selectTrack 立即发布乐观 Playing 快照，音频层的
    // Loading 被 visibleStateDuringSeek_ 抑制回 Playing，直至真实 Playing/Stopped/Error
    // 到达（Error 必放行）。前端 backend_snapshot_mapper.cpp:362 保持无状态"仅
    // Playing→isPlaying=true"：下面四行是纯映射契约断言；Loading 行保留为防御性
    // 断言（若仍到达，无状态映射仍得 false，但切轨/seek 期间后端不再发送，播放按钮
    // 不会出现瞬时 false）。
    applyAndCompareStoppedState(controller, snapshot, seriona::control::PlaybackStatus::Paused);
    applyAndCompareStoppedState(controller, snapshot, seriona::control::PlaybackStatus::Stopped);
    applyAndCompareStoppedState(controller, snapshot, seriona::control::PlaybackStatus::Loading);
    applyAndCompareStoppedState(controller, snapshot, seriona::control::PlaybackStatus::Error);

    // 切轨快照序列：乐观 Playing 直连真实 Playing（T6 抑制 Loading），
    // isPlaying 全程无中间 false；isPlayingChanged 无参信号，spy 只记录
    // 翻转次数 —— 序列期间仅乐观这一次翻转（count==1 即无中间态）。
    QSignalSpy playingSpy(&controller, &Seriona::App::PlaybackController::isPlayingChanged);
    seriona::control::PlayerStateSnapshot nextTrack =
        makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    nextTrack.currentTrack->trackId = "track-echo-2";
    controller.applyPlayerStateSnapshot(nextTrack);
    QCOMPARE(controller.isPlaying(), true);
    QCOMPARE(playingSpy.count(), 1);
    controller.applyPlayerStateSnapshot(nextTrack);
    QCOMPARE(controller.isPlaying(), true);
    QCOMPARE(playingSpy.count(), 1);

    // 真实错误快照不被抑制：Error 释放 isPlaying（按钮回到"播放"，第二次翻转），
    // 且快照其余字段照常应用（错误不被前端吞掉）。
    nextTrack.playback.state = seriona::control::PlaybackStatus::Error;
    nextTrack.playback.errorCode = std::string{"load-failed"};
    nextTrack.playback.errorMessage = std::string{"track failed to load"};
    controller.applyPlayerStateSnapshot(nextTrack);
    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(playingSpy.count(), 2);
    QCOMPARE(controller.currentTrackId(), QStringLiteral("track-echo-2"));
    QCOMPARE(controller.songTitle(), QStringLiteral("Seriona Echo"));

    snapshot.playback.state = seriona::control::PlaybackStatus::Playing;
    snapshot.muted = true;
    snapshot.volume = 0.85F;
    snapshot.shuffle = false;
    snapshot.repeatMode = seriona::control::RepeatMode::One;
    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.isPlaying(), true);
    QCOMPARE(controller.volume(), 0.0);
    QCOMPARE(controller.isShuffle(), false);
    QCOMPARE(controller.repeatMode(), 2);

    snapshot.muted = false;
    snapshot.volume = 2.0F;
    snapshot.repeatMode = seriona::control::RepeatMode::Off;
    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.volume(), 1.0);
    QCOMPARE(controller.repeatMode(), 0);
}

void PlaybackSnapshotMappingTest::emptySnapshot()
{
    Seriona::App::PlaybackController controller;
    seriona::control::PlayerStateSnapshot snapshot;
    snapshot.timeline.position = std::chrono::milliseconds{12000};

    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.isPlaying(), false);
    QCOMPARE(controller.songTitle(), QStringLiteral("No song selected"));
    QCOMPARE(controller.artistName(), QStringLiteral("Unknown Artist"));
    QCOMPARE(controller.albumName(), QStringLiteral("Unknown Album"));
    QCOMPARE(controller.currentPosition(), 12.0);
    QCOMPARE(controller.totalDuration(), 0.0);
    QCOMPARE(controller.currentPositionText(), QStringLiteral("00:12"));
    QCOMPARE(controller.totalDurationText(), QStringLiteral("00:00"));
    QCOMPARE(controller.remainingDurationText(), QStringLiteral("-00:00"));
    QCOMPARE(controller.volume(), 1.0);
    QCOMPARE(controller.isShuffle(), false);
    QCOMPARE(controller.repeatMode(), 0);
    QCOMPARE(controller.capability(), QStringLiteral("none"));
}

void PlaybackSnapshotMappingTest::timelineSmoothing()
{
    Seriona::App::PlaybackController controller;
    seriona::control::PlayerStateSnapshot playing = makeTimelineSnapshot(
        seriona::control::PlaybackStatus::Playing,
        std::chrono::milliseconds{5800},
        std::chrono::milliseconds{60000});

    controller.applyPlayerStateSnapshot(playing);

    QCOMPARE(controller.totalDuration(), 60.0);
    QCOMPARE(controller.currentPositionText(), QStringLiteral("00:05"));
    // 位置平滑 timer 100ms 步进；慢机上放宽到 3000ms 轮询
    QTRY_VERIFY_WITH_TIMEOUT(controller.currentPosition() >= 6.0, 3000);
    QCOMPARE(controller.currentPositionText(), QStringLiteral("00:06"));

    seriona::control::PlayerStateSnapshot paused = makeTimelineSnapshot(
        seriona::control::PlaybackStatus::Paused,
        std::chrono::milliseconds{12000},
        std::chrono::milliseconds{60000});
    controller.applyPlayerStateSnapshot(paused);
    QCOMPARE(controller.currentPosition(), 12.0);
    QTest::qWait(180);
    QCOMPARE(controller.currentPosition(), 12.0);

    seriona::control::PlayerStateSnapshot seeked = makeTimelineSnapshot(
        seriona::control::PlaybackStatus::Playing,
        std::chrono::milliseconds{33000},
        std::chrono::milliseconds{60000});
    controller.applyPlayerStateSnapshot(seeked);
    QVERIFY(controller.currentPosition() >= 33.0);
    QVERIFY(controller.currentPosition() < 33.1);
    QCOMPARE(controller.currentPositionText(), QStringLiteral("00:33"));

    seriona::control::PlayerStateSnapshot missingDuration = makeTimelineSnapshot(
        seriona::control::PlaybackStatus::Paused,
        std::chrono::milliseconds{15000},
        std::nullopt);
    controller.applyPlayerStateSnapshot(missingDuration);
    QCOMPARE(controller.currentPosition(), 15.0);
    QCOMPARE(controller.totalDuration(), 0.0);
    QCOMPARE(controller.currentPositionText(), QStringLiteral("00:15"));
    QCOMPARE(controller.remainingDurationText(), QStringLiteral("-00:00"));
}

void PlaybackSnapshotMappingTest::timelineStoppedAndErrorSnapToBackend()
{
    Seriona::App::PlaybackController controller;

    expectPinnedSnapshotPosition(controller, seriona::control::PlaybackStatus::Stopped, 9.0);
    expectPinnedSnapshotPosition(controller, seriona::control::PlaybackStatus::Loading, 11.0);
    expectPinnedSnapshotPosition(controller, seriona::control::PlaybackStatus::Error, 13.0);
}

void PlaybackSnapshotMappingTest::coverArtworkSourcesUrlEncodeRawPaths()
{
    Seriona::App::PlaybackController controller([](const QString &) {
        return Seriona::App::GradientPalette{
            QStringLiteral("#000000"), QStringLiteral("#111111"), QStringLiteral("#222222")};
    });

    seriona::control::PlayerStateSnapshot snapshot = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    const QString rawArtwork = QStringLiteral("/music/My Tracks/重低音 神曲.png");
    const QString rawThumbnail = QStringLiteral("/thumbs/缩 略 图/track cover.png");
    snapshot.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{rawArtwork.toStdString()},
        .thumbnailPath = std::filesystem::path{rawThumbnail.toStdString()},
    };

    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.coverArtworkPath(), rawArtwork);
    QCOMPARE(controller.coverArtworkSource(), QUrl::fromLocalFile(rawArtwork).toString());
    QCOMPARE(controller.coverThumbnailSource(), QUrl::fromLocalFile(rawThumbnail).toString());
}

void PlaybackSnapshotMappingTest::emptyArtExposesEmptySources()
{
    Seriona::App::PlaybackController controller([](const QString &) {
        return Seriona::App::GradientPalette{
            QStringLiteral("#000000"), QStringLiteral("#111111"), QStringLiteral("#222222")};
    });

    seriona::control::PlayerStateSnapshot snapshot = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.coverArtworkPath(), QString());
    QCOMPARE(controller.coverArtworkSource(), QString());
    QCOMPARE(controller.coverThumbnailSource(), QString());
    QCOMPARE(controller.songTitle(), QStringLiteral("Seriona Echo"));
}

void PlaybackSnapshotMappingTest::thumbnailThenFullUpgradeSkipsSecondPaletteDecode()
{
    int decodeCalls = 0;
    QString decodedPath;
    Seriona::App::PlaybackController controller(
        [&decodeCalls, &decodedPath](const QString &path) {
            ++decodeCalls;
            decodedPath = path;
            return Seriona::App::GradientPalette{
                QStringLiteral("#112233"), QStringLiteral("#223344"), QStringLiteral("#334455")};
        });

    seriona::control::PlayerStateSnapshot snapshot = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    const QString thumbnailPath = QStringLiteral("/thumbs/folder/track.png");
    snapshot.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{thumbnailPath.toStdString()},
        .thumbnailPath = std::filesystem::path{thumbnailPath.toStdString()},
    };

    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.coverThumbnailSource(), QUrl::fromLocalFile(thumbnailPath).toString());
    QTRY_COMPARE_WITH_TIMEOUT(controller.gradientColor0(), QStringLiteral("#112233"), 8000);
    QCOMPARE(decodeCalls, 1);
    QCOMPARE(decodedPath, thumbnailPath);

    snapshot.artwork->localPath = std::filesystem::path{"/covers/full.png"};
    controller.applyPlayerStateSnapshot(snapshot);

    QCOMPARE(controller.coverArtworkSource(), QUrl::fromLocalFile(QStringLiteral("/covers/full.png")).toString());
    QCOMPARE(controller.coverThumbnailSource(), QUrl::fromLocalFile(thumbnailPath).toString());
    QCOMPARE(controller.coverArtworkPath(), QStringLiteral("/covers/full.png"));
    QCOMPARE(decodeCalls, 1);
    QCOMPARE(controller.songTitle(), QStringLiteral("Seriona Echo"));
    QCOMPARE(controller.artistName(), QStringLiteral("Adapter Fixture"));
    QCOMPARE(controller.albumName(), QStringLiteral("Contract Smoke"));
    QCOMPARE(controller.currentPosition(), 42.0);
    QCOMPARE(controller.totalDuration(), 185.0);
}

void PlaybackSnapshotMappingTest::slowPaletteDecoderDoesNotDelaySnapshotApplication()
{
    // 解码器用信号量阻塞而非忙等：apply 的墙钟测量不与自旋竞争，只验证"不 join 解码线程"
    QSemaphore releaseDecoder;
    Seriona::App::PlaybackController controller([&releaseDecoder](const QString &) {
        releaseDecoder.acquire();
        return Seriona::App::GradientPalette{
            QStringLiteral("#aabbcc"), QStringLiteral("#bbccdd"), QStringLiteral("#ccddee")};
    });

    seriona::control::PlayerStateSnapshot snapshot = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    snapshot.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/thumbs/slow.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/slow.png"},
    };

    QSignalSpy songSpy(&controller, &Seriona::App::PlaybackController::currentSongChanged);
    QElapsedTimer timer;
    timer.start();
    controller.applyPlayerStateSnapshot(snapshot);
    const qint64 applyElapsedMs = timer.nsecsElapsed() / 1000000;

    QVERIFY2(applyElapsedMs <= 500,
        qPrintable(QStringLiteral("snapshot application blocked %1 ms behind a blocked decoder").arg(applyElapsedMs)));
    QCOMPARE(songSpy.count(), 1);
    QCOMPARE(controller.coverArtworkSource(), QUrl::fromLocalFile(QStringLiteral("/thumbs/slow.png")).toString());

    releaseDecoder.release();
    QTRY_COMPARE_WITH_TIMEOUT(controller.gradientColor0(), QStringLiteral("#aabbcc"), 8000);
}

void PlaybackSnapshotMappingTest::rapidThumbnailUpdatesDeliverOnlyLatestPalette()
{
    QSemaphore firstDecodeEntered;
    QSemaphore releaseFirstDecode;
    Seriona::App::PlaybackController controller(
        [&firstDecodeEntered, &releaseFirstDecode](const QString &path) {
            if (path == QStringLiteral("/thumbs/a.png")) {
                firstDecodeEntered.release();
                releaseFirstDecode.acquire();
                return Seriona::App::GradientPalette{
                    QStringLiteral("#aa0000"), QStringLiteral("#bb0000"), QStringLiteral("#cc0000")};
            }
            return Seriona::App::GradientPalette{
                QStringLiteral("#00bb00"), QStringLiteral("#00cc00"), QStringLiteral("#00dd00")};
        });

    QSignalSpy gradientSpy(&controller, &Seriona::App::PlaybackController::gradientColorsChanged);

    seriona::control::PlayerStateSnapshot trackA = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackA.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/thumbs/a.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/a.png"},
    };
    controller.applyPlayerStateSnapshot(trackA);
    QVERIFY(firstDecodeEntered.tryAcquire(1, 8000));

    seriona::control::PlayerStateSnapshot trackB = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackB.currentTrack->trackId = "track-echo-2";
    trackB.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/thumbs/b.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/b.png"},
    };
    controller.applyPlayerStateSnapshot(trackB);

    releaseFirstDecode.release();
    QTRY_COMPARE_WITH_TIMEOUT(controller.gradientColor0(), QStringLiteral("#00bb00"), 8000);
    QCOMPARE(gradientSpy.count(), 1);
}

void PlaybackSnapshotMappingTest::latePaletteResultDroppedAfterTrackSwitch()
{
    QSemaphore firstDecodeEntered;
    QSemaphore releaseFirstDecode;
    Seriona::App::PlaybackController controller(
        [&firstDecodeEntered, &releaseFirstDecode](const QString &path) {
            if (path == QStringLiteral("/thumbs/a.png")) {
                firstDecodeEntered.release();
                releaseFirstDecode.acquire();
                return Seriona::App::GradientPalette{
                    QStringLiteral("#aa0000"), QStringLiteral("#bb0000"), QStringLiteral("#cc0000")};
            }
            return Seriona::App::GradientPalette{
                QStringLiteral("#00bb00"), QStringLiteral("#00cc00"), QStringLiteral("#00dd00")};
        });

    QSignalSpy gradientSpy(&controller, &Seriona::App::PlaybackController::gradientColorsChanged);

    seriona::control::PlayerStateSnapshot trackA = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackA.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/thumbs/a.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/a.png"},
    };
    controller.applyPlayerStateSnapshot(trackA);
    QVERIFY(firstDecodeEntered.tryAcquire(1, 8000));

    // Switch to a track without artwork: no worker request is made, but the
    // expected generation advances so A's in-flight result becomes stale.
    seriona::control::PlayerStateSnapshot trackB = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackB.currentTrack->trackId = "track-echo-2";
    controller.applyPlayerStateSnapshot(trackB);
    QCOMPARE(controller.coverThumbnailSource(), QString());

    // A's decode completes while the worker still considers it the latest
    // generation, so paletteReady(1) is emitted; it must be dropped by the
    // controller because the expected generation already moved past it.
    releaseFirstDecode.release();
    QTRY_COMPARE_WITH_TIMEOUT(controller.gradientColor0(), QStringLiteral("#4a2c2a"), 8000);
    QCOMPARE(gradientSpy.count(), 1);

    seriona::control::PlayerStateSnapshot trackC = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackC.currentTrack->trackId = "track-echo-3";
    trackC.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/thumbs/c.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/c.png"},
    };
    controller.applyPlayerStateSnapshot(trackC);
    QTRY_COMPARE_WITH_TIMEOUT(controller.gradientColor0(), QStringLiteral("#00bb00"), 8000);
    QCOMPARE(gradientSpy.count(), 2);
}

void PlaybackSnapshotMappingTest::stalePriorTrackFullArtworkDoesNotLeak()
{
    Seriona::App::PlaybackController controller([](const QString &) {
        return Seriona::App::GradientPalette{
            QStringLiteral("#010101"), QStringLiteral("#020202"), QStringLiteral("#030303")};
    });

    seriona::control::PlayerStateSnapshot trackA = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackA.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/covers/a-full.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/a.png"},
    };
    controller.applyPlayerStateSnapshot(trackA);
    QCOMPARE(controller.coverArtworkSource(), QUrl::fromLocalFile(QStringLiteral("/covers/a-full.png")).toString());

    seriona::control::PlayerStateSnapshot trackB = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    trackB.currentTrack->trackId = "track-echo-2";
    controller.applyPlayerStateSnapshot(trackB);

    QCOMPARE(controller.currentTrackId(), QStringLiteral("track-echo-2"));
    QCOMPARE(controller.coverArtworkPath(), QString());
    QCOMPARE(controller.coverArtworkSource(), QString());
    QCOMPARE(controller.coverThumbnailSource(), QString());
}

void PlaybackSnapshotMappingTest::shutdownWithBlockedDecoder()
{
    QSemaphore decoderEntered;
    QSemaphore releaseDecode;
    Seriona::App::ArtworkPaletteWorker worker([&decoderEntered, &releaseDecode](const QString &) {
        decoderEntered.release();
        releaseDecode.acquire();
        return Seriona::App::GradientPalette{
            QStringLiteral("#000000"), QStringLiteral("#111111"), QStringLiteral("#222222")};
    });
    worker.requestPalette(QStringLiteral("/thumbs/blocked.png"));
    QVERIFY(decoderEntered.tryAcquire(1, 8000));

    std::atomic<bool> shutdownReturned{false};
    std::thread shutdownThread([&worker, &shutdownReturned] {
        worker.shutdown();
        shutdownReturned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    QCOMPARE(shutdownReturned.load(), false);

    releaseDecode.release();
    shutdownThread.join();
    QCOMPARE(shutdownReturned.load(), true);

    worker.shutdown();
}

void PlaybackSnapshotMappingTest::queueEntriesMapping()
{
    Seriona::App::PlaybackController controller;
    seriona::control::PlayerStateSnapshot snapshot = makeFilledSnapshot(seriona::control::PlaybackStatus::Playing);
    snapshot.currentTrack->trackId = "track-queued-1";

    // 跨端定死契约字段：queueEntries: [{trackId, nodeId}]
    seriona::control::QueueEntry first;
    first.trackId = "track-queued-1";
    first.nodeId = "node-queued-1";
    seriona::control::QueueEntry missing;
    missing.trackId = "track-not-in-library";
    snapshot.queueEntries = {first, missing};

    seriona::control::LibraryStateSnapshot library = makeLibrarySnapshotWithSong(
        "track-queued-1", "node-queued-1", "Queued Song", "Queue Artist");

    controller.applyPlayerStateSnapshot(snapshot, &library);

    QVariantList entries = controller.queueEntries();
    QCOMPARE(entries.size(), 2);

    const QVariantMap resolved = entries.at(0).toMap();
    QCOMPARE(resolved.value(QStringLiteral("trackId")).toString(), QStringLiteral("track-queued-1"));
    QCOMPARE(resolved.value(QStringLiteral("nodeId")).toString(), QStringLiteral("node-queued-1"));
    QCOMPARE(resolved.value(QStringLiteral("title")).toString(), QStringLiteral("Queued Song"));
    QCOMPARE(resolved.value(QStringLiteral("artist")).toString(), QStringLiteral("Queue Artist"));
    QCOMPARE(resolved.value(QStringLiteral("isPlaying")).toBool(), true);

    const QVariantMap unresolved = entries.at(1).toMap();
    QCOMPARE(unresolved.value(QStringLiteral("trackId")).toString(), QStringLiteral("track-not-in-library"));
    QCOMPARE(unresolved.value(QStringLiteral("title")).toString(), QStringLiteral("track-not-in-library"));
    QCOMPARE(unresolved.value(QStringLiteral("artist")).toString(), QString());
    QCOMPARE(unresolved.value(QStringLiteral("isPlaying")).toBool(), false);

    // 队列消费/移除后快照重应用 → 列表刷新
    snapshot.queueEntries = {missing};
    controller.applyPlayerStateSnapshot(snapshot, &library);
    QCOMPARE(controller.queueEntries().size(), 1);
    QCOMPARE(controller.queueEntries().at(0).toMap().value(QStringLiteral("trackId")).toString(),
        QStringLiteral("track-not-in-library"));

    // 队列清空 → 空列表（前端据此显示空状态说明）
    snapshot.queueEntries = {};
    controller.applyPlayerStateSnapshot(snapshot, &library);
    QVERIFY(controller.queueEntries().isEmpty());
}

QTEST_GUILESS_MAIN(PlaybackSnapshotMappingTest)

#include "tst_playback_snapshot_mapping.moc"
