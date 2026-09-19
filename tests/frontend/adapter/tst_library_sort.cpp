#include "library_model.h"

#include "seriona/control/control_contracts.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QTest>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
using Seriona::App::LibraryController;
using Seriona::App::LibraryModel;
using seriona::control::FolderSortDirection;
using seriona::control::FolderSortField;
using seriona::control::FolderSortMissingValuePolicy;
using seriona::control::FolderSortRule;
using seriona::control::FolderSortSetting;
using seriona::control::MediaControlCommand;
using seriona::control::MediaControlCommandKind;
using seriona::control::MediaControllerCommandResult;
using seriona::control::MediaControllerErrorCode;
using seriona::control::PlayerStateSnapshot;
using seriona::control::TrackIdentity;
using seriona::scanner::PlaylistNode;
using seriona::scanner::PlaylistNodeKind;
using seriona::scanner::PlaylistTreeSnapshot;
using seriona::scanner::SongMetadata;

MediaControllerCommandResult acceptedResult()
{
    MediaControllerCommandResult result;
    result.accepted = true;
    result.code = MediaControllerErrorCode::None;
    return result;
}

struct CommandRecorder {
    std::vector<MediaControlCommand> commands;

    MediaControllerCommandResult record(const MediaControlCommand &command)
    {
        commands.push_back(command);
        return acceptedResult();
    }

    void clear()
    {
        commands.clear();
    }
};

struct ScanRecorder {
    std::vector<QString> roots;

    MediaControllerCommandResult record(const QString &rootPath, seriona::scanner::ScanMode)
    {
        roots.push_back(rootPath);
        return acceptedResult();
    }
};

PlaylistNode makeFolder(const std::string &nodeId,
                        const std::string &displayName,
                        std::vector<std::string> childNodeIds = {},
                        std::optional<std::string> parentNodeId = std::nullopt,
                        PlaylistNodeKind kind = PlaylistNodeKind::Directory)
{
    PlaylistNode node;
    node.nodeId = nodeId;
    node.displayName = displayName;
    node.kind = kind;
    node.parentNodeId = std::move(parentNodeId);
    node.childNodeIds = std::move(childNodeIds);
    return node;
}

PlaylistNode makeTrack(const std::string &nodeId,
                       const std::string &trackId,
                       const std::string &displayName,
                       const std::string &title,
                       const std::string &artist,
                       const std::string &album,
                       std::chrono::milliseconds duration,
                       std::optional<std::string> parentNodeId = std::nullopt,
                       std::optional<std::uint32_t> year = std::nullopt,
                       std::optional<std::uint32_t> discNumber = std::nullopt,
                       std::optional<std::uint32_t> trackNumber = std::nullopt,
                       std::optional<std::filesystem::file_time_type> fileMtime = std::nullopt)
{
    SongMetadata song;
    song.trackId = trackId;
    song.filePath = "/music/" + displayName;
    song.sourceFilePath = song.filePath;
    song.title = title;
    song.artist = artist;
    song.album = album;
    song.sampleRate = 48000;
    song.bitDepth = 24;
    song.duration = duration;
    song.year = year;
    song.discNumber = discNumber;
    song.trackNumber = trackNumber;
    song.fileMtime = fileMtime;

    PlaylistNode node;
    node.nodeId = nodeId;
    node.parentNodeId = std::move(parentNodeId);
    node.kind = PlaylistNodeKind::Track;
    node.displayName = displayName;
    node.song = std::move(song);
    return node;
}

PlaylistTreeSnapshot makeSortableSnapshot(std::uint64_t version = 21)
{
    const std::filesystem::file_time_type baseTime{};
    PlaylistTreeSnapshot snapshot;
    snapshot.version = version;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"folder-jazz", "track-root-z", "track-root-a"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("folder-jazz", "Jazz", {"track-folder-b", "track-folder-a", "track-folder-c"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeTrack("track-folder-b", "track-folder-b-id", "02-beta.flac", "Beta Tune", "Charlie", "Album Z", std::chrono::milliseconds{180000}, std::string{"folder-jazz"}, std::uint32_t{2021}, std::uint32_t{2}, std::uint32_t{7}, baseTime + std::chrono::seconds{20}),
        makeTrack("track-folder-a", "track-folder-a-id", "01-alpha.flac", "Alpha Tune", "Delta", "Album A", std::chrono::milliseconds{60000}, std::string{"folder-jazz"}, std::uint32_t{2019}, std::uint32_t{1}, std::uint32_t{3}, baseTime + std::chrono::seconds{10}),
        makeTrack("track-folder-c", "track-folder-c-id", "03-gamma.flac", "Gamma Tune", "Bravo", "Album M", std::chrono::milliseconds{240000}, std::string{"folder-jazz"}, std::uint32_t{2020}, std::uint32_t{1}, std::uint32_t{9}, baseTime + std::chrono::seconds{30}),
        makeTrack("track-root-z", "track-root-z-id", "z-root.flac", "Zulu Root", "Root Artist B", "Root Album", std::chrono::milliseconds{300000}, std::string{"root"}, std::uint32_t{2022}, std::uint32_t{1}, std::uint32_t{2}, baseTime + std::chrono::seconds{50}),
        makeTrack("track-root-a", "track-root-a-id", "a-root.flac", "Alpha Root", "Root Artist A", "Root Album", std::chrono::milliseconds{90000}, std::string{"root"}, std::uint32_t{2018}, std::uint32_t{1}, std::uint32_t{1}, baseTime + std::chrono::seconds{40}),
    };
    return snapshot;
}

PlaylistTreeSnapshot makeUpdatedSortableSnapshot()
{
    const std::filesystem::file_time_type baseTime{};
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 22;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"folder-jazz", "track-root-a", "track-root-z"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("folder-jazz", "Jazz", {"track-folder-c", "track-folder-a", "track-folder-b"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeTrack("track-folder-c", "track-folder-c-id", "03-gamma.flac", "Gamma Tune", "Bravo", "Album M", std::chrono::milliseconds{240000}, std::string{"folder-jazz"}, std::uint32_t{2020}, std::uint32_t{1}, std::uint32_t{9}, baseTime + std::chrono::seconds{30}),
        makeTrack("track-folder-a", "track-folder-a-id", "01-alpha.flac", "Alpha Tune", "Delta", "Album A", std::chrono::milliseconds{60000}, std::string{"folder-jazz"}, std::uint32_t{2019}, std::uint32_t{1}, std::uint32_t{3}, baseTime + std::chrono::seconds{10}),
        makeTrack("track-folder-b", "track-folder-b-id", "02-beta.flac", "Beta Tune", "Charlie", "Album Z", std::chrono::milliseconds{180000}, std::string{"folder-jazz"}, std::uint32_t{2021}, std::uint32_t{2}, std::uint32_t{7}, baseTime + std::chrono::seconds{20}),
        makeTrack("track-root-a", "track-root-a-id", "a-root.flac", "Alpha Root", "Root Artist A", "Root Album", std::chrono::milliseconds{90000}, std::string{"root"}, std::uint32_t{2018}, std::uint32_t{1}, std::uint32_t{1}, baseTime + std::chrono::seconds{40}),
        makeTrack("track-root-z", "track-root-z-id", "z-root.flac", "Zulu Root", "Root Artist B", "Root Album", std::chrono::milliseconds{300000}, std::string{"root"}, std::uint32_t{2022}, std::uint32_t{1}, std::uint32_t{2}, baseTime + std::chrono::seconds{50}),
    };
    return snapshot;
}

PlaylistTreeSnapshot makeFolderRemovedSnapshot()
{
    const std::filesystem::file_time_type baseTime{};
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 23;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"track-root-a", "track-root-z"}, std::nullopt, PlaylistNodeKind::Root),
        makeTrack("track-root-a", "track-root-a-id", "a-root.flac", "Alpha Root", "Root Artist A", "Root Album", std::chrono::milliseconds{90000}, std::string{"root"}, std::uint32_t{2018}, std::uint32_t{1}, std::uint32_t{1}, baseTime + std::chrono::seconds{40}),
        makeTrack("track-root-z", "track-root-z-id", "z-root.flac", "Zulu Root", "Root Artist B", "Root Album", std::chrono::milliseconds{300000}, std::string{"root"}, std::uint32_t{2022}, std::uint32_t{1}, std::uint32_t{2}, baseTime + std::chrono::seconds{50}),
    };
    return snapshot;
}

// 模拟「后端按文件夹排序规则重排 folder-jazz 后推送的快照」：前端应直接渲染该顺序。
PlaylistTreeSnapshot makeBackendSortedSnapshot(std::vector<std::string> folderChildren, std::uint64_t version = 24)
{
    PlaylistTreeSnapshot snapshot = makeSortableSnapshot(version);
    for (PlaylistNode &node : snapshot.nodes) {
        if (node.nodeId == "folder-jazz") {
            node.childNodeIds = std::move(folderChildren);
        }
    }
    return snapshot;
}

// 搜索专用快照：根下两个文件夹（folder-alpha、folder-nest）+ 一首根歌曲。
// 查询 "Alpha" 的加权得分：track-t1 标题完全匹配 100 > track-nested 标题前缀 50 >
// track-t2 歌手前缀 25 > track-t3 专辑前缀 15；文件夹名含 "Alpha" 也不得出现。
PlaylistTreeSnapshot makeSearchSnapshot()
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 41;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"folder-alpha", "folder-nest", "track-root-solo"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("folder-alpha", "Alpha Tracks", {"track-t1", "track-t2", "track-t3"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeFolder("folder-nest", "Nested Box", {"track-nested"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeTrack("track-t1", "track-t1-id", "01-t1.flac", "Alpha", "Beta Singer", "Gamma Album", std::chrono::milliseconds{120000}, std::string{"folder-alpha"}),
        makeTrack("track-t2", "track-t2-id", "02-t2.flac", "Omega", "Alpha Singer", "Delta Album", std::chrono::milliseconds{130000}, std::string{"folder-alpha"}),
        makeTrack("track-t3", "track-t3-id", "03-t3.flac", "Omega Two", "Epsilon", "Alpha Album", std::chrono::milliseconds{140000}, std::string{"folder-alpha"}),
        makeTrack("track-nested", "track-nested-id", "04-nest.flac", "Alpha Deep", "Nest Artist", "Nest Album", std::chrono::milliseconds{150000}, std::string{"folder-nest"}),
        makeTrack("track-root-solo", "track-root-solo-id", "05-solo.flac", "Solo", "Nested", "Box", std::chrono::milliseconds{160000}, std::string{"root"}),
    };
    return snapshot;
}

QString nodeIdAt(const LibraryModel *model, int row)
{
    return model->data(model->index(row, 0), LibraryModel::NodeIdRole).toString();
}

void expectProjection(const LibraryModel *model, const QVector<QString> &nodeIds)
{
    QCOMPARE(model->rowCount(), nodeIds.size());
    for (int row = 0; row < nodeIds.size(); ++row) {
        QCOMPARE(nodeIdAt(model, row), nodeIds.at(row));
        QCOMPARE(model->rowForNodeId(nodeIds.at(row)), row);
    }
}

bool nodeIsPlaying(const LibraryModel *model, const QString &nodeId)
{
    const int row = model->rowForNodeId(nodeId);
    if (row < 0) {
        return false;
    }
    return model->data(model->index(row, 0), LibraryModel::IsPlayingRole).toBool();
}

PlayerStateSnapshot playerSnapshotForTrack(const std::string &trackId)
{
    PlayerStateSnapshot snapshot;
    snapshot.currentTrack = TrackIdentity{};
    snapshot.currentTrack->trackId = trackId;
    return snapshot;
}

QVariantList sortRules(std::initializer_list<std::pair<QString, QString>> rules)
{
    QVariantList result;
    for (const auto &[field, order] : rules) {
        QVariantMap rule;
        rule.insert(QStringLiteral("field"), field);
        rule.insert(QStringLiteral("order"), order);
        result.append(rule);
    }
    return result;
}

QString scanTemporaryRoot(LibraryController &controller, QTemporaryDir &musicDir)
{
    Q_ASSERT(musicDir.isValid());
    ScanRecorder recorder;
    controller.setScanExecutor([&recorder](const QString &rootPath, seriona::scanner::ScanMode mode) {
        return recorder.record(rootPath, mode);
    });

    const QString canonicalRoot = QFileInfo(musicDir.path()).absoluteFilePath();
    if (!controller.scanLibrary(QUrl::fromLocalFile(musicDir.path()))) {
        qFatal("scanTemporaryRoot expected scanLibrary to accept a temporary root");
    }
    if (recorder.roots != std::vector<QString>{canonicalRoot}) {
        qFatal("scanTemporaryRoot expected scan executor to receive the canonical root");
    }
    return canonicalRoot;
}

void installCommandRecorder(LibraryController &controller, CommandRecorder &recorder)
{
    controller.setCommandExecutor([&recorder](const MediaControlCommand &command) {
        return recorder.record(command);
    });
}

void expectFolderSortCommand(const CommandRecorder &recorder,
                             const QString &rootPath,
                             const QString &folderNodeId,
                             FolderSortField field,
                             FolderSortDirection direction)
{
    QCOMPARE(recorder.commands.size(), std::size_t{1});
    const MediaControlCommand &command = recorder.commands.front();
    QCOMPARE(static_cast<int>(command.kind), static_cast<int>(MediaControlCommandKind::ApplyFolderSortRules));
    QVERIFY(command.folderSortSetting.has_value());
    QCOMPARE(QString::fromStdString(command.folderSortSetting->rootPath.generic_string()), rootPath);
    QCOMPARE(QString::fromStdString(command.folderSortSetting->folderNodeId), folderNodeId);
    QCOMPARE(command.folderSortSetting->rules.size(), std::size_t{1});
    const FolderSortRule &rule = command.folderSortSetting->rules.front();
    QCOMPARE(static_cast<int>(rule.field), static_cast<int>(field));
    QCOMPARE(static_cast<int>(rule.direction), static_cast<int>(direction));
    QCOMPARE(static_cast<int>(rule.missingValuePolicy), static_cast<int>(FolderSortMissingValuePolicy::Last));
}
}

class LibrarySortTest : public QObject
{
    Q_OBJECT

private slots:
    void noRulesPreserveScannerOrderForRootFolderAndSearch();
    void emptyRulesRestoreScannerOrderAfterSorting();
    void titleAscendingAndDescendingSortCurrentFolderProjection();
    void supportedSortDialogFieldsSortCurrentProjection();
    void currentRulesReapplyAcrossEnterFolderSearchSubmitAndClear();
    void snapshotUpdateReappliesRulesWithoutMutatingScannerOrder();
    void snapshotReconcileKeepsRootSearchSortAndPlaybackMarkers();
    void snapshotReconcileFallsBackToVisibleRowsWhenFolderDisappears();
    void invalidSortPayloadLeavesProjectionUnchanged();
    void folderSortSendsApplyCommandWithRootAndFolderKey();
    void sameFolderNodeIdUnderDifferentRootsDoesNotReuseSavedRules();
    void reenterFolderAndBackendStateReloadRestoreSavedRules();
    void equivalentBackendRootPathVariantsReloadSavedRules();
    void backendFolderSortNotificationUpdatesActiveCurrentRules();
    void currentSortRulesExposeFolderRulesForQmlAndFallbacks();
    void searchProjectionSortDoesNotPersistOrOverwriteSavedFolderRules();
    void missingContextAndMalformedSortPayloadDoNotPersist();
    void searchScopedToCurrentFolderSubtree();
    void searchExcludesFolders();
    void searchRanksByWeightedScore();
    void clearSearchRestoresUserSortRules();
};

void LibrarySortTest::noRulesPreserveScannerOrderForRootFolderAndSearch()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());

    expectProjection(controller.model(), {QStringLiteral("folder-jazz"), QStringLiteral("track-root-z"), QStringLiteral("track-root-a")});
    QCOMPARE(controller.model()->childNodeIds(QStringLiteral("root")), QVector<QString>({QStringLiteral("folder-jazz"), QStringLiteral("track-root-z"), QStringLiteral("track-root-a")}));

    controller.enterFolder(QStringLiteral("folder-jazz"));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(controller.model()->childNodeIds(QStringLiteral("folder-jazz")), QVector<QString>({QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")}));

    controller.setSearchQuery(QStringLiteral("Tune"));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.clearSearch();
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::emptyRulesRestoreScannerOrderAfterSorting()
{
    // 排序权威已移交后端（见 docs/playback-sort-order-skip-defect-design-decision-2026-09-19.md
    // §8 决策⑦）：前端应用/清空规则都不再本地重排，始终渲染后端快照的树序。
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.applySortRules({});

    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::titleAscendingAndDescendingSortCurrentFolderProjection()
{
    // 前端只上报规则并渲染后端返回的顺序（决策⑦）：应用规则本身不改动投影，
    // 后端按该规则重排后推送的新快照才改变它——这里用 makeBackendSortedSnapshot 模拟。
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    const QString rootPath = scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);

    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    expectFolderSortCommand(recorder, rootPath, QStringLiteral("folder-jazz"), FolderSortField::Title, FolderSortDirection::Ascending);
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.setPlaylistTreeSnapshot(makeBackendSortedSnapshot({"track-folder-a", "track-folder-b", "track-folder-c"}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b"), QStringLiteral("track-folder-c")});

    recorder.clear();
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("desc")}}));
    expectFolderSortCommand(recorder, rootPath, QStringLiteral("folder-jazz"), FolderSortField::Title, FolderSortDirection::Descending);
    expectProjection(controller.model(), {QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b"), QStringLiteral("track-folder-c")});

    controller.setPlaylistTreeSnapshot(makeBackendSortedSnapshot({"track-folder-c", "track-folder-b", "track-folder-a"}, 25));
    expectProjection(controller.model(), {QStringLiteral("track-folder-c"), QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a")});
}

void LibrarySortTest::supportedSortDialogFieldsSortCurrentProjection()
{
    // 每个可排序字段都应以正确字段/方向上报后端；前端不本地重排（决策⑦）。
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    const QString rootPath = scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    struct FieldCase {
        QString field;
        QString order;
        FolderSortField expectedField;
        FolderSortDirection expectedDirection;
    };
    const std::vector<FieldCase> cases{
        {QStringLiteral("artist"), QStringLiteral("asc"), FolderSortField::Artist, FolderSortDirection::Ascending},
        {QStringLiteral("album"), QStringLiteral("desc"), FolderSortField::Album, FolderSortDirection::Descending},
        {QStringLiteral("filename"), QStringLiteral("desc"), FolderSortField::Filename, FolderSortDirection::Descending},
        {QStringLiteral("year"), QStringLiteral("asc"), FolderSortField::Year, FolderSortDirection::Ascending},
        {QStringLiteral("duration"), QStringLiteral("asc"), FolderSortField::Duration, FolderSortDirection::Ascending},
        {QStringLiteral("createdDate"), QStringLiteral("desc"), FolderSortField::CreatedDate, FolderSortDirection::Descending},
        {QStringLiteral("discNumber"), QStringLiteral("desc"), FolderSortField::DiscNumber, FolderSortDirection::Descending},
        {QStringLiteral("trackNumber"), QStringLiteral("asc"), FolderSortField::TrackNumber, FolderSortDirection::Ascending},
    };

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);

    for (const FieldCase &testCase : cases) {
        recorder.clear();
        controller.applySortRules(sortRules({{testCase.field, testCase.order}}));
        expectFolderSortCommand(recorder, rootPath, QStringLiteral("folder-jazz"), testCase.expectedField, testCase.expectedDirection);
    }

    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::currentRulesReapplyAcrossEnterFolderSearchSubmitAndClear()
{
    // 前端不再本地重排浏览投影（决策⑦）：规则被记住并跨导航保持，投影始终渲染
    // 后端树序；搜索仍按相关性排序（前端唯一保留的排序语义）。
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());

    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    expectProjection(controller.model(), {QStringLiteral("folder-jazz"), QStringLiteral("track-root-z"), QStringLiteral("track-root-a")});

    controller.enterFolder(QStringLiteral("folder-jazz"));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(controller.currentSortRules().size(), 1);
    QCOMPARE(controller.currentSortRules().first().toMap().value(QStringLiteral("field")).toString(), QStringLiteral("title"));
    QCOMPARE(controller.currentSortRules().first().toMap().value(QStringLiteral("order")).toString(), QStringLiteral("asc"));

    controller.setSearchQuery(QStringLiteral("Tune"));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.submitSearch();
    QCOMPARE(controller.selectedBrowserNodeId(), QStringLiteral("track-folder-b"));
    QCOMPARE(controller.scrollRequest(), QStringLiteral("track-folder-b"));

    controller.clearSearch();
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::snapshotUpdateReappliesRulesWithoutMutatingScannerOrder()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));
    controller.applySortRules(sortRules({{QStringLiteral("duration"), QStringLiteral("desc")}}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(controller.model()->childNodeIds(QStringLiteral("folder-jazz")), QVector<QString>({QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")}));

    controller.setPlaylistTreeSnapshot(makeUpdatedSortableSnapshot());

    QCOMPARE(controller.model()->version(), 22ULL);
    expectProjection(controller.model(), {QStringLiteral("track-folder-c"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b")});
    QCOMPARE(controller.model()->childNodeIds(QStringLiteral("folder-jazz")), QVector<QString>({QStringLiteral("track-folder-c"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b")}));
}

void LibrarySortTest::snapshotReconcileKeepsRootSearchSortAndPlaybackMarkers()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("desc")}}));
    controller.setSearchQuery(QStringLiteral("Root"));
    controller.setPlayingTrackId(QStringLiteral("track-root-z-id"));

    expectProjection(controller.model(), {QStringLiteral("track-root-z"), QStringLiteral("track-root-a")});
    QVERIFY(nodeIsPlaying(controller.model(), QStringLiteral("track-root-z")));

    controller.setPlaylistTreeSnapshot(makeUpdatedSortableSnapshot());

    QCOMPARE(controller.model()->version(), 22ULL);
    expectProjection(controller.model(), {QStringLiteral("track-root-a"), QStringLiteral("track-root-z")});
    QVERIFY(nodeIsPlaying(controller.model(), QStringLiteral("track-root-z")));

    controller.clearSearch();
    expectProjection(controller.model(), {QStringLiteral("folder-jazz"), QStringLiteral("track-root-a"), QStringLiteral("track-root-z")});

    controller.applyPlayerStateSnapshot(playerSnapshotForTrack("track-root-a-id"), true);
    QCOMPARE(controller.playingTrackId(), QStringLiteral("track-root-a-id"));
    QVERIFY(!nodeIsPlaying(controller.model(), QStringLiteral("track-root-z")));
    QVERIFY(nodeIsPlaying(controller.model(), QStringLiteral("track-root-a")));

    controller.applyPlayerStateSnapshot(PlayerStateSnapshot{}, true);
    QCOMPARE(controller.playingTrackId(), QString());
    QVERIFY(!nodeIsPlaying(controller.model(), QStringLiteral("track-root-a")));
}

void LibrarySortTest::snapshotReconcileFallsBackToVisibleRowsWhenFolderDisappears()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));
    controller.setSelectedBrowserNodeId(QStringLiteral("track-folder-b"));
    controller.setPlayingTrackId(QStringLiteral("track-folder-b-id"));

    controller.setPlaylistTreeSnapshot(makeFolderRemovedSnapshot());

    QCOMPARE(controller.model()->version(), 23ULL);
    QCOMPARE(controller.currentFolderName(), QStringLiteral("My Music"));
    QCOMPARE(controller.canGoBack(), false);
    expectProjection(controller.model(), {QStringLiteral("track-root-a"), QStringLiteral("track-root-z")});
    QCOMPARE(controller.focusedNodeId(), QStringLiteral("track-root-a"));
    QCOMPARE(controller.selectedBrowserNodeId(), QStringLiteral("track-root-a"));
    QCOMPARE(controller.rowForNodeId(controller.focusedNodeId()), 0);
    QCOMPARE(controller.rowForNodeId(controller.selectedBrowserNodeId()), 0);
    QCOMPARE(controller.rowForNodeId(QStringLiteral("folder-jazz")), -1);
    QCOMPARE(controller.rowForNodeId(QStringLiteral("track-folder-b")), -1);
    QVERIFY(!nodeIsPlaying(controller.model(), QStringLiteral("track-root-a")));

    controller.applyPlayerStateSnapshot(PlayerStateSnapshot{}, true);

    QCOMPARE(controller.playingTrackId(), QString());
}

void LibrarySortTest::invalidSortPayloadLeavesProjectionUnchanged()
{
    // 无效载荷不得改变当前规则，也不得本地重排投影（决策⑦）。
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.applySortRules(sortRules({{QStringLiteral("unknownField"), QStringLiteral("desc")}}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.applySortRules(sortRules({{QStringLiteral("artist"), QStringLiteral("sideways")}}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    const QVariantList currentRules = controller.currentSortRules();
    QCOMPARE(currentRules.size(), 1);
    QCOMPARE(currentRules.first().toMap().value(QStringLiteral("field")).toString(), QStringLiteral("title"));
    QCOMPARE(currentRules.first().toMap().value(QStringLiteral("order")).toString(), QStringLiteral("asc"));
}

void LibrarySortTest::folderSortSendsApplyCommandWithRootAndFolderKey()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    const QString rootPath = scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);

    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("desc")}}));

    expectFolderSortCommand(recorder,
                            rootPath,
                            QStringLiteral("folder-jazz"),
                            FolderSortField::Title,
                            FolderSortDirection::Descending);
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(controller.lastError(), QString());
}

void LibrarySortTest::sameFolderNodeIdUnderDifferentRootsDoesNotReuseSavedRules()
{
    QTemporaryDir firstRoot;
    QTemporaryDir secondRoot;
    QVERIFY(firstRoot.isValid());
    QVERIFY(secondRoot.isValid());

    LibraryController controller;
    scanTemporaryRoot(controller, firstRoot);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    QCOMPARE(recorder.commands.size(), std::size_t{1});
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    scanTemporaryRoot(controller, secondRoot);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot(31));
    controller.enterFolder(QStringLiteral("folder-jazz"));

    QCOMPARE(recorder.commands.size(), std::size_t{1});
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::reenterFolderAndBackendStateReloadRestoreSavedRules()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    const QString rootPath = scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.goBack();
    controller.enterFolder(QStringLiteral("folder-jazz"));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(controller.currentSortRules().size(), 1);

    LibraryController reloaded;
    scanTemporaryRoot(reloaded, musicDir);
    reloaded.setPlaylistTreeSnapshot(makeSortableSnapshot());

    FolderSortSetting saved;
    saved.rootPath = std::filesystem::path(rootPath.toStdString());
    saved.folderNodeId = "folder-jazz";
    saved.rules = {FolderSortRule{FolderSortField::Duration, FolderSortDirection::Descending, FolderSortMissingValuePolicy::Last}};
    reloaded.applyFolderSortSetting(saved);
    reloaded.enterFolder(QStringLiteral("folder-jazz"));

    expectProjection(reloaded.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(reloaded.currentSortRules().size(), 1);
    QCOMPARE(reloaded.currentSortRules().first().toMap().value(QStringLiteral("field")).toString(), QStringLiteral("duration"));
}

void LibrarySortTest::equivalentBackendRootPathVariantsReloadSavedRules()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController reloaded;
    const QString rootPath = scanTemporaryRoot(reloaded, musicDir);
    reloaded.setPlaylistTreeSnapshot(makeSortableSnapshot());

    const QString variantRootPath = rootPath + QStringLiteral("/./");
    QVERIFY(variantRootPath != rootPath);

    FolderSortSetting saved;
    saved.rootPath = std::filesystem::path(variantRootPath.toStdString());
    saved.folderNodeId = "folder-jazz";
    saved.rules = {FolderSortRule{FolderSortField::Duration, FolderSortDirection::Descending, FolderSortMissingValuePolicy::Last}};
    reloaded.applyFolderSortSetting(saved);
    reloaded.enterFolder(QStringLiteral("folder-jazz"));

    expectProjection(reloaded.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(reloaded.currentSortRules().size(), 1);
}

void LibrarySortTest::backendFolderSortNotificationUpdatesActiveCurrentRules()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    const QString rootPath = scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));
    QCOMPARE(controller.currentSortRules().size(), 0);

    QSignalSpy sortRulesChanged(&controller, &LibraryController::currentSortRulesChanged);
    FolderSortSetting saved;
    saved.rootPath = std::filesystem::path(rootPath.toStdString());
    saved.folderNodeId = "folder-jazz";
    saved.rules = {FolderSortRule{FolderSortField::Duration, FolderSortDirection::Descending, FolderSortMissingValuePolicy::Last}};

    controller.applyFolderSortSetting(saved);

    QCOMPARE(sortRulesChanged.count(), 1);
    const QVariantList currentRules = controller.currentSortRules();
    QCOMPARE(currentRules.size(), 1);
    QCOMPARE(currentRules.first().toMap().value(QStringLiteral("field")).toString(), QStringLiteral("duration"));
    QCOMPARE(currentRules.first().toMap().value(QStringLiteral("order")).toString(), QStringLiteral("desc"));
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::currentSortRulesExposeFolderRulesForQmlAndFallbacks()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);
    controller.applySortRules(sortRules({{QStringLiteral("duration"), QStringLiteral("desc")}}));

    QVariantList currentRules = controller.currentSortRules();
    QCOMPARE(currentRules.size(), 1);
    const QVariantMap currentRule = currentRules.first().toMap();
    QCOMPARE(currentRule.value(QStringLiteral("field")).toString(), QStringLiteral("duration"));
    QCOMPARE(currentRule.value(QStringLiteral("order")).toString(), QStringLiteral("desc"));

    controller.applySortRules(sortRules({{QStringLiteral("notAField"), QStringLiteral("asc")}}));
    currentRules = controller.currentSortRules();
    QCOMPARE(currentRules.size(), 1);
    QCOMPARE(currentRules.first().toMap().value(QStringLiteral("field")).toString(), QStringLiteral("duration"));
    QCOMPARE(currentRules.first().toMap().value(QStringLiteral("order")).toString(), QStringLiteral("desc"));

    controller.setPlaylistTreeSnapshot(makeFolderRemovedSnapshot());

    QCOMPARE(controller.currentSortRules().size(), 0);
    expectProjection(controller.model(), {QStringLiteral("track-root-a"), QStringLiteral("track-root-z")});
}

void LibrarySortTest::searchProjectionSortDoesNotPersistOrOverwriteSavedFolderRules()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    LibraryController controller;
    scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    CommandRecorder recorder;
    installCommandRecorder(controller, recorder);
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    QCOMPARE(recorder.commands.size(), std::size_t{1});

    controller.setSearchQuery(QStringLiteral("Tune"));
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("desc")}}));

    QCOMPARE(recorder.commands.size(), std::size_t{1});
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    controller.clearSearch();
    controller.goBack();
    controller.enterFolder(QStringLiteral("folder-jazz"));

    QCOMPARE(recorder.commands.size(), std::size_t{1});
    expectProjection(controller.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
    QCOMPARE(controller.currentSortRules().size(), 1);
}

void LibrarySortTest::missingContextAndMalformedSortPayloadDoNotPersist()
{
    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());

    LibraryController rootOnly;
    scanTemporaryRoot(rootOnly, musicDir);
    rootOnly.setPlaylistTreeSnapshot(makeSortableSnapshot());
    CommandRecorder rootRecorder;
    installCommandRecorder(rootOnly, rootRecorder);

    rootOnly.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));

    QCOMPARE(rootRecorder.commands.size(), std::size_t{0});
    QVERIFY(rootOnly.lastError().contains(QStringLiteral("文件夹")));

    LibraryController folderWithoutRoot;
    folderWithoutRoot.setPlaylistTreeSnapshot(makeSortableSnapshot());
    folderWithoutRoot.enterFolder(QStringLiteral("folder-jazz"));
    CommandRecorder noRootRecorder;
    installCommandRecorder(folderWithoutRoot, noRootRecorder);

    folderWithoutRoot.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));

    QCOMPARE(noRootRecorder.commands.size(), std::size_t{0});
    QVERIFY(folderWithoutRoot.lastError().contains(QStringLiteral("曲库")));

    LibraryController malformed;
    scanTemporaryRoot(malformed, musicDir);
    malformed.setPlaylistTreeSnapshot(makeSortableSnapshot());
    malformed.enterFolder(QStringLiteral("folder-jazz"));
    CommandRecorder malformedRecorder;
    installCommandRecorder(malformed, malformedRecorder);
    expectProjection(malformed.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    malformed.applySortRules(sortRules({{QStringLiteral("unknownField"), QStringLiteral("desc")}}));
    malformed.applySortRules(sortRules({{QStringLiteral("artist"), QStringLiteral("sideways")}}));

    QCOMPARE(malformedRecorder.commands.size(), std::size_t{0});
    expectProjection(malformed.model(), {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});
}

void LibrarySortTest::searchScopedToCurrentFolderSubtree()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSearchSnapshot());

    controller.setSearchQuery(QStringLiteral("Alpha"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-nested"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});

    controller.enterFolder(QStringLiteral("folder-alpha"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});

    controller.setSearchQuery(QStringLiteral("Solo"));
    expectProjection(controller.model(), {});

    controller.setSearchQuery(QStringLiteral("Alpha"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});
}

void LibrarySortTest::searchExcludesFolders()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSearchSnapshot());

    controller.setSearchQuery(QStringLiteral("Nested Box"));
    expectProjection(controller.model(), {});

    controller.setSearchQuery(QStringLiteral("Alpha Tracks"));
    expectProjection(controller.model(), {});

    controller.setSearchQuery(QStringLiteral("Nested"));
    expectProjection(controller.model(), {QStringLiteral("track-root-solo")});
}

void LibrarySortTest::searchRanksByWeightedScore()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSearchSnapshot());

    controller.setSearchQuery(QStringLiteral("Alpha"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-nested"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});

    controller.enterFolder(QStringLiteral("folder-alpha"));
    controller.setSearchQuery(QStringLiteral("Alpha"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});
}

void LibrarySortTest::clearSearchRestoresUserSortRules()
{
    // 「恢复」指规则被重新记为当前规则（并上报后端）；浏览投影仍渲染后端树序，
    // 搜索时才按相关性排（决策⑦）。
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeSearchSnapshot());
    controller.enterFolder(QStringLiteral("folder-alpha"));

    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("desc")}}));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});
    QCOMPARE(controller.currentSortRules().size(), 1);
    QCOMPARE(controller.currentSortRules().first().toMap().value(QStringLiteral("order")).toString(), QStringLiteral("desc"));

    controller.setSearchQuery(QStringLiteral("Alpha"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});

    controller.clearSearch();
    QCOMPARE(controller.currentSortRules().size(), 1);
    QCOMPARE(controller.currentSortRules().first().toMap().value(QStringLiteral("field")).toString(), QStringLiteral("title"));
    QCOMPARE(controller.currentSortRules().first().toMap().value(QStringLiteral("order")).toString(), QStringLiteral("desc"));
    expectProjection(controller.model(), {QStringLiteral("track-t1"), QStringLiteral("track-t2"), QStringLiteral("track-t3")});
}

QTEST_GUILESS_MAIN(LibrarySortTest)

#include "tst_library_sort.moc"
