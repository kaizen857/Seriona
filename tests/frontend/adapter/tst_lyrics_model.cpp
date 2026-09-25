#include "lyrics_model.h"

#include "seriona/control/control_contracts.h"

#include <QtTest/QTest>

#include <chrono>

namespace {

seriona::scanner::PlaylistNode makeRootNode()
{
    seriona::scanner::PlaylistNode node;
    node.nodeId = "root";
    node.kind = seriona::scanner::PlaylistNodeKind::Root;
    node.displayName = "Library";
    node.childNodeIds = {"node-decoy", "node-selected"};
    return node;
}

seriona::scanner::LyricLine makeLyricLine(std::chrono::milliseconds timestamp, const std::string &text)
{
    seriona::scanner::LyricLine line;
    line.timestamp = timestamp;
    line.text = text;
    return line;
}

seriona::scanner::PlaylistNode makeTrackNode(
    const std::string &nodeId,
    const std::string &trackId,
    const std::vector<seriona::scanner::LyricLine> &lyrics)
{
    seriona::scanner::SongMetadata song;
    song.trackId = trackId;
    song.filePath = "/music/shared.flac";
    song.sourceFilePath = "/music/shared.flac";
    song.title = trackId;
    song.effectiveLyrics = lyrics;

    seriona::scanner::PlaylistNode node;
    node.nodeId = nodeId;
    node.parentNodeId = "root";
    node.kind = seriona::scanner::PlaylistNodeKind::Track;
    node.displayName = trackId;
    node.song = song;
    return node;
}

seriona::control::LibraryStateSnapshot makeLibrary(const std::vector<seriona::scanner::LyricLine> &selectedLyrics)
{
    seriona::control::LibraryStateSnapshot library;
    library.libraryTree = seriona::scanner::PlaylistTreeSnapshot{};
    library.libraryTree->version = 17;
    library.libraryTree->rootNodeId = "root";
    library.libraryTree->nodes = {
        makeRootNode(),
        makeTrackNode("node-decoy", "path-decoy", {makeLyricLine(std::chrono::milliseconds{0}, "Wrong | 错误")}),
        makeTrackNode("node-selected", "selected-track", selectedLyrics)};
    return library;
}

seriona::control::PlayerStateSnapshot makePlayer(const std::string &trackId)
{
    seriona::control::PlayerStateSnapshot player;
    player.currentTrack = seriona::control::TrackIdentity{};
    player.currentTrack->trackId = trackId;
    player.currentTrack->filePath = "/music/shared.flac";
    player.timeline.position = std::chrono::milliseconds{0};
    return player;
}

QVariant modelValue(const Seriona::App::LyricsModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

}

class LyricsModelTest : public QObject
{
    Q_OBJECT

private slots:
    void trackidLookup();
    void missingEmpty();
};

void LyricsModelTest::trackidLookup()
{
    const seriona::control::PlayerStateSnapshot player = makePlayer("selected-track");
    seriona::control::LibraryStateSnapshot library = makeLibrary({
        makeLyricLine(std::chrono::milliseconds{0}, "Intro / 开场"),
        makeLyricLine(std::chrono::milliseconds{5000}, "Verse / 主歌"),
        makeLyricLine(std::chrono::milliseconds{10000}, "Chorus / 副歌")});
    Seriona::App::LyricsModel model;
    model.setShowTranslation(false);

    model.applyPlayerStateSnapshot(player, &library);

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.showTranslation(), false);
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::DisplayLineRole).toString(), QStringLiteral("Intro"));
    QCOMPARE(modelValue(model, 0, Seriona::App::LyricsModel::TranslationRole).toString(), QStringLiteral("开场"));

    model.setPlaybackPosition(6.0);
    QCOMPARE(model.currentIndex(), 1);
    QCOMPARE(modelValue(model, 1, Seriona::App::LyricsModel::CurrentRole).toBool(), true);
    QCOMPARE(modelValue(model, 1, Seriona::App::LyricsModel::DisplayLineRole).toString(), QStringLiteral("Verse"));

    model.setPlaybackPosition(11.0);
    QCOMPARE(model.currentIndex(), 2);
    model.selectLyric(0);
    QCOMPARE(model.currentIndex(), 0);
    model.setPlaybackPosition(6.5);
    QCOMPARE(model.currentIndex(), 1);

    library = makeLibrary({
        makeLyricLine(std::chrono::milliseconds{0}, "Plain one | 第一行"),
        makeLyricLine(std::chrono::milliseconds{0}, "Plain two | 第二行")});
    model.applyPlayerStateSnapshot(player, &library);
    model.setPlaybackPosition(90.0);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.currentIndex(), 0);
}

void LyricsModelTest::missingEmpty()
{
    Seriona::App::LyricsModel model;
    model.setShowTranslation(false);

    seriona::control::PlayerStateSnapshot emptyPlayer;
    model.applyPlayerStateSnapshot(emptyPlayer, nullptr);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.currentIndex(), 0);

    const seriona::control::LibraryStateSnapshot library = makeLibrary({
        makeLyricLine(std::chrono::milliseconds{0}, "Available | 可用")});
    seriona::control::PlayerStateSnapshot missingPlayer = makePlayer("missing-track");
    model.applyPlayerStateSnapshot(missingPlayer, &library);
    QCOMPARE(model.rowCount(), 0);

    const seriona::control::LibraryStateSnapshot noLyricsLibrary = makeLibrary({});
    seriona::control::PlayerStateSnapshot selectedPlayer = makePlayer("selected-track");
    model.applyPlayerStateSnapshot(selectedPlayer, &noLyricsLibrary);
    QCOMPARE(model.rowCount(), 0);

    model.selectLyric(12);
    model.setPlaybackPosition(20.0);
    model.toggleTranslation();
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.showTranslation(), true);
}

QTEST_GUILESS_MAIN(LyricsModelTest)

#include "tst_lyrics_model.moc"
