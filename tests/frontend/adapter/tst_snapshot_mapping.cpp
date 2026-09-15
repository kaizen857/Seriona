#include "backend_snapshot_mapper.h"

#if SERIONA_HAS_BACKEND
#include "seriona/control/control_contracts.h"
#endif

#include <QObject>
#include <QtTest/QTest>

#if SERIONA_HAS_BACKEND
#include <chrono>
#include <filesystem>
#endif

class SnapshotMappingTest : public QObject {
    Q_OBJECT

private slots:
#if SERIONA_HAS_BACKEND
    void mapsPlayerSnapshotToQtFacingProperties();
    void mapsPartialPlayerSnapshotToSafeDefaults();
    void mapsLibraryScanStatusAndProgress();
    void mapsLibraryErrorFallbacks();
    void mapsArtworkRefPathsRawIntoViewState();
    void mapsEmptyArtworkRefToEmptyArtFields();
    void mapsThumbnailThenFullUpgrade();
#else
    void mapperUnavailableWithoutBackend();
#endif
};

#if SERIONA_HAS_BACKEND
namespace {

using seriona::control::LibraryScanStatus;
using seriona::control::LibraryStateSnapshot;
using seriona::control::PlaybackStatus;
using seriona::control::PlayerStateSnapshot;
using seriona::control::RepeatMode;

LibraryStateSnapshot libraryWithTrack()
{
    seriona::scanner::SongMetadata song;
    song.trackId = "track-1";
    song.filePath = std::filesystem::path{"/music/display.flac"};
    song.sourceFilePath = std::filesystem::path{"/music/source.wav"};
    song.title = "Library Title";
    song.artist = "Library Artist";
    song.album = "Library Album";
    song.duration = std::chrono::milliseconds{185000};
    song.sampleRate = 96000;
    song.bitDepth = 24;
    song.channels = 2;
    song.artworkPath = std::filesystem::path{"/covers/library.png"};

    seriona::scanner::PlaylistNode root;
    root.nodeId = "root";
    root.kind = seriona::scanner::PlaylistNodeKind::Root;
    root.displayName = "Library";
    root.childNodeIds = {"track-node"};

    seriona::scanner::PlaylistNode track;
    track.nodeId = "track-node";
    track.parentNodeId = "root";
    track.kind = seriona::scanner::PlaylistNodeKind::Track;
    track.displayName = "Track Node";
    track.song = song;

    seriona::scanner::PlaylistTreeSnapshot tree;
    tree.version = 7;
    tree.rootNodeId = "root";
    tree.nodes = {root, track};

    LibraryStateSnapshot library;
    library.version = 7;
    library.libraryTree = tree;
    return library;
}

}

void SnapshotMappingTest::mapsPlayerSnapshotToQtFacingProperties()
{
    LibraryStateSnapshot library = libraryWithTrack();
    PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{
        .trackId = "track-1",
        .filePath = std::filesystem::path{"/fallback/fallback.mp3"},
        .sourceId = {},
        .libraryId = {},
    };
    player.display = seriona::control::DisplayMetadata{
        .title = "Seriona Echo",
        .artist = "Adapter Fixture",
        .album = "Contract Smoke",
        .albumArtist = {},
        .genre = {},
    };
    player.artwork = seriona::control::ArtworkRef{.localPath = std::filesystem::path{"/covers/player.png"}};
    player.playback.state = PlaybackStatus::Playing;
    player.repeatMode = RepeatMode::One;
    player.shuffle = true;
    player.volume = 0.5F;
    player.timeline.position = std::chrono::milliseconds{42000};
    player.timeline.duration = std::chrono::milliseconds{180000};
    player.capabilities.canPlay = true;
    player.capabilities.canPause = true;
    player.capabilities.canSeek = true;
    player.capabilities.canSetVolume = true;
    player.capabilities.canSelectTrack = true;

    const Seriona::App::PlayerSnapshotViewState mapped = Seriona::App::mapPlayerSnapshot(player, &library);

    QCOMPARE(mapped.isPlaying, true);
    QCOMPARE(mapped.currentTrack.trackId, QStringLiteral("track-1"));
    QCOMPARE(mapped.currentTrack.nodeId, QStringLiteral("track-node"));
    QCOMPARE(mapped.currentTrack.title, QStringLiteral("Seriona Echo"));
    QCOMPARE(mapped.currentTrack.artist, QStringLiteral("Adapter Fixture"));
    QCOMPARE(mapped.currentTrack.album, QStringLiteral("Contract Smoke"));
    QCOMPARE(mapped.currentTrack.artworkPath, QStringLiteral("/covers/player.png"));
    QCOMPARE(mapped.currentTrack.durationSeconds, 185.0);
    QCOMPARE(mapped.currentTrack.audioFormat, QStringLiteral("WAV"));
    QCOMPARE(mapped.currentTrack.audioSampleRate, 96000);
    QCOMPARE(mapped.currentTrack.audioBitDepth, 24);
    QCOMPARE(mapped.currentTrack.audioChannels, 2);
    QCOMPARE(mapped.volume, 0.5);
    QCOMPARE(mapped.shuffle, true);
    QCOMPARE(mapped.repeatMode, 2);
    QCOMPARE(mapped.capability, QStringLiteral("play,pause,seek,set-volume,select-track"));
    QCOMPARE(mapped.timeline.position, std::chrono::milliseconds{42000});
    QCOMPARE(mapped.timeline.durationSeconds, 180.0);
    QCOMPARE(mapped.timeline.smooth, true);
    QCOMPARE(Seriona::App::playingTrackIdFromSnapshot(player), QStringLiteral("track-1"));
}

void SnapshotMappingTest::mapsPartialPlayerSnapshotToSafeDefaults()
{
    PlayerStateSnapshot player;

    const Seriona::App::PlayerSnapshotViewState mapped = Seriona::App::mapPlayerSnapshot(player, nullptr);

    QCOMPARE(mapped.isPlaying, false);
    QCOMPARE(mapped.currentTrack.trackId, QString());
    QCOMPARE(mapped.currentTrack.nodeId, QString());
    QCOMPARE(mapped.currentTrack.title, QStringLiteral("No song selected"));
    QCOMPARE(mapped.currentTrack.artist, QStringLiteral("Unknown Artist"));
    QCOMPARE(mapped.currentTrack.album, QStringLiteral("Unknown Album"));
    QCOMPARE(mapped.currentTrack.durationSeconds, 0.0);
    QCOMPARE(mapped.currentTrack.audioFormat, QString());
    QCOMPARE(mapped.volume, 1.0);
    QCOMPARE(mapped.shuffle, false);
    QCOMPARE(mapped.repeatMode, 0);
    QCOMPARE(mapped.capability, QStringLiteral("none"));
    QCOMPARE(mapped.timeline.durationSeconds, 0.0);
    QCOMPARE(mapped.timeline.positionSeconds, 0.0);
    QCOMPARE(mapped.timeline.smooth, false);
    QCOMPARE(Seriona::App::playingTrackIdFromSnapshot(player), QString());
}

void SnapshotMappingTest::mapsLibraryScanStatusAndProgress()
{
	LibraryStateSnapshot scanning;
	scanning.scanStatus = LibraryScanStatus::Scanning;
	scanning.scanProgress = seriona::scanner::ScanProgress{.filesDiscovered = 10, .filesScanned = 3, .filesSkipped = 4};

    LibraryStateSnapshot completed;
    completed.scanStatus = LibraryScanStatus::Completed;

    LibraryStateSnapshot stopped;
    stopped.scanStatus = LibraryScanStatus::Stopped;

    const Seriona::App::LibrarySnapshotViewState scanningMapped = Seriona::App::mapLibrarySnapshot(scanning);
    const Seriona::App::LibrarySnapshotViewState completedMapped = Seriona::App::mapLibrarySnapshot(completed);
    const Seriona::App::LibrarySnapshotViewState stoppedMapped = Seriona::App::mapLibrarySnapshot(stopped);

	QCOMPARE(scanningMapped.scanStatus, QStringLiteral("running"));
	QCOMPARE(scanningMapped.scanProgress, 70);
	QCOMPARE(scanningMapped.scannedSongCount, quint64{7});
	QCOMPARE(scanningMapped.totalSongCount, quint64{10});
    QCOMPARE(completedMapped.scanStatus, QStringLiteral("completed"));
    QCOMPARE(completedMapped.scanProgress, 100);
    QCOMPARE(completedMapped.scannedSongCount, quint64{0});
    QCOMPARE(completedMapped.totalSongCount, quint64{0});
    QCOMPARE(stoppedMapped.scanStatus, QStringLiteral("pending"));
    QCOMPARE(stoppedMapped.scanProgress, 0);
}

void SnapshotMappingTest::mapsLibraryErrorFallbacks()
{
    LibraryStateSnapshot detailError;
    detailError.scanStatus = LibraryScanStatus::Error;
    detailError.lastError = seriona::scanner::ScannerError{.message = {}, .detail = "metadata parser failed"};

    LibraryStateSnapshot missingError;
    missingError.scanStatus = LibraryScanStatus::Error;

    const Seriona::App::LibrarySnapshotViewState detailMapped = Seriona::App::mapLibrarySnapshot(detailError);
    const Seriona::App::LibrarySnapshotViewState missingMapped = Seriona::App::mapLibrarySnapshot(missingError);

    QCOMPARE(detailMapped.scanStatus, QStringLiteral("error"));
    QCOMPARE(detailMapped.lastError, QStringLiteral("metadata parser failed"));
    QCOMPARE(missingMapped.scanStatus, QStringLiteral("error"));
    QCOMPARE(missingMapped.lastError, QStringLiteral("扫描失败"));
}

void SnapshotMappingTest::mapsArtworkRefPathsRawIntoViewState()
{
    LibraryStateSnapshot library = libraryWithTrack();
    PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{
        .trackId = "track-1",
        .filePath = std::filesystem::path{"/fallback/fallback.mp3"},
        .sourceId = {},
        .libraryId = {},
    };
    player.display = seriona::control::DisplayMetadata{
        .title = "Raw Paths",
        .artist = "Space Artist",
        .album = "非 ASCII 专辑",
        .albumArtist = {},
        .genre = {},
    };
    player.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{u8"/music/My Tracks/重低音 神曲.png"},
        .thumbnailPath = std::filesystem::path{u8"/thumbs/缩 略 图/track cover.png"},
    };

    const Seriona::App::PlayerSnapshotViewState mapped = Seriona::App::mapPlayerSnapshot(player, &library);

    QCOMPARE(mapped.currentTrack.preferredArtworkPath, QStringLiteral("/music/My Tracks/重低音 神曲.png"));
    QCOMPARE(mapped.currentTrack.fallbackThumbnailPath, QStringLiteral("/thumbs/缩 略 图/track cover.png"));
    QCOMPARE(mapped.currentTrack.artworkPath, QStringLiteral("/music/My Tracks/重低音 神曲.png"));
    QCOMPARE(mapped.currentTrack.title, QStringLiteral("Raw Paths"));
    QCOMPARE(mapped.currentTrack.artist, QStringLiteral("Space Artist"));
    QCOMPARE(mapped.currentTrack.album, QStringLiteral("非 ASCII 专辑"));
    QCOMPARE(mapped.currentTrack.durationSeconds, 185.0);
    QCOMPARE(mapped.currentTrack.audioFormat, QStringLiteral("WAV"));
    QCOMPARE(mapped.currentTrack.audioSampleRate, 96000);
    QCOMPARE(mapped.currentTrack.audioBitDepth, 24);
    QCOMPARE(mapped.currentTrack.audioChannels, 2);
}

void SnapshotMappingTest::mapsEmptyArtworkRefToEmptyArtFields()
{
    PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{
        .trackId = "track-2",
        .filePath = std::filesystem::path{"/music/empty.mp3"},
        .sourceId = {},
        .libraryId = {},
    };
    player.artwork = seriona::control::ArtworkRef{};

    const Seriona::App::PlayerSnapshotViewState mapped = Seriona::App::mapPlayerSnapshot(player, nullptr);

    QCOMPARE(mapped.currentTrack.preferredArtworkPath, QString());
    QCOMPARE(mapped.currentTrack.fallbackThumbnailPath, QString());
    QCOMPARE(mapped.currentTrack.artworkPath, QString());
    QCOMPARE(mapped.currentTrack.trackId, QStringLiteral("track-2"));
}

void SnapshotMappingTest::mapsThumbnailThenFullUpgrade()
{
    PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{
        .trackId = "track-3",
        .filePath = std::filesystem::path{"/music/upgrade.mp3"},
        .sourceId = {},
        .libraryId = {},
    };
    player.artwork = seriona::control::ArtworkRef{
        .localPath = std::filesystem::path{"/thumbs/track.png"},
        .thumbnailPath = std::filesystem::path{"/thumbs/track.png"},
    };

    const Seriona::App::PlayerSnapshotViewState thumbnailOnly = Seriona::App::mapPlayerSnapshot(player, nullptr);
    QCOMPARE(thumbnailOnly.currentTrack.preferredArtworkPath, QStringLiteral("/thumbs/track.png"));
    QCOMPARE(thumbnailOnly.currentTrack.fallbackThumbnailPath, QStringLiteral("/thumbs/track.png"));

    player.artwork->localPath = std::filesystem::path{"/covers/full.png"};

    const Seriona::App::PlayerSnapshotViewState upgraded = Seriona::App::mapPlayerSnapshot(player, nullptr);
    QCOMPARE(upgraded.currentTrack.preferredArtworkPath, QStringLiteral("/covers/full.png"));
    QCOMPARE(upgraded.currentTrack.fallbackThumbnailPath, QStringLiteral("/thumbs/track.png"));
    QCOMPARE(upgraded.currentTrack.artworkPath, QStringLiteral("/covers/full.png"));
}
#else
void SnapshotMappingTest::mapperUnavailableWithoutBackend()
{
    QVERIFY(true);
}
#endif

QTEST_GUILESS_MAIN(SnapshotMappingTest)

#include "tst_snapshot_mapping.moc"
