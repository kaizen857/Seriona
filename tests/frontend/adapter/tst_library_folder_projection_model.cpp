#include "library_folder_projection_model.h"
#include "library_model.h"

#include <QAbstractItemModelTester>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QTest>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace {
using Seriona::App::LibraryController;
using Seriona::App::LibraryFolderProjectionModel;
using Seriona::App::LibraryModel;
using seriona::control::MediaControlCommand;
using seriona::control::MediaControllerCommandResult;
using seriona::scanner::PlaylistNode;
using seriona::scanner::PlaylistNodeKind;
using seriona::scanner::PlaylistTreeSnapshot;
using seriona::scanner::SongMetadata;

MediaControllerCommandResult acceptedResult()
{
    MediaControllerCommandResult result;
    result.accepted = true;
    return result;
}

struct CommandRecorder {
    std::vector<MediaControlCommand> commands;

    MediaControllerCommandResult record(const MediaControlCommand &command)
    {
        commands.push_back(command);
        return acceptedResult();
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
                       std::optional<std::string> parentNodeId = std::nullopt)
{
    SongMetadata song;
    song.trackId = trackId;
    song.filePath = "/music/" + displayName;
    song.sourceFilePath = song.filePath;
    song.title = title;
    song.artist = artist;
    song.album = album;
    song.sampleRate = 96000;
    song.bitDepth = 24;
    song.duration = duration;

    PlaylistNode node;
    node.nodeId = nodeId;
    node.parentNodeId = std::move(parentNodeId);
    node.kind = PlaylistNodeKind::Track;
    node.displayName = displayName;
    node.song = std::move(song);
    return node;
}

PlaylistTreeSnapshot makeProjectedTreeSnapshot()
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 7;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"album-a", "track-c"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("album-a", "Album A", {"track-a", "track-b"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeTrack("track-a", "track-a-id", "01-song-a.flac", "Song A", "Artist A", "Album A", std::chrono::milliseconds{185000}, std::string{"album-a"}),
        makeTrack("track-b", "track-b-id", "02-song-b.flac", "Song B", "Artist B", "Album A", std::chrono::milliseconds{200000}, std::string{"album-a"}),
        makeTrack("track-c", "track-c-id", "03-song-c.flac", "Song C", "Artist C", "Singles", std::chrono::milliseconds{150000}, std::string{"root"}),
    };
    return snapshot;
}

// album-a 的子女被替换为 track-x / track-y 的快照（树变化重建测试用）。
PlaylistTreeSnapshot makeProjectedTreeSnapshotV2()
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 8;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"album-a", "track-c"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("album-a", "Album A", {"track-x", "track-y"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeTrack("track-x", "track-x-id", "01-song-x.flac", "Song X", "Artist X", "Album A", std::chrono::milliseconds{185000}, std::string{"album-a"}),
        makeTrack("track-y", "track-y-id", "02-song-y.flac", "Song Y", "Artist Y", "Album A", std::chrono::milliseconds{200000}, std::string{"album-a"}),
        makeTrack("track-c", "track-c-id", "03-song-c.flac", "Song C", "Artist C", "Singles", std::chrono::milliseconds{150000}, std::string{"root"}),
    };
    return snapshot;
}

// 参数化 album-a 树：children 指定 album-a 投影的子节点顺序与键集合，
// 每个键生成节点 nodeId（trackId = nodeId + "-id"）；用于增量行操作测试。
PlaylistTreeSnapshot makeAlbumChildrenSnapshot(const std::vector<std::pair<std::string, std::string>> &children,
                                               std::uint64_t version = 51)
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = version;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes.push_back(makeFolder("root", "Library", {"album-a"}, std::nullopt, PlaylistNodeKind::Root));

    std::vector<std::string> childIds;
    childIds.reserve(children.size());
    for (const auto &[nodeId, title] : children) {
        childIds.push_back(nodeId);
        snapshot.nodes.push_back(makeTrack(nodeId, nodeId + "-id", nodeId + ".flac", title,
                                           "Artist " + nodeId, "Album A",
                                           std::chrono::milliseconds{180000}, "album-a"));
    }
    snapshot.nodes.push_back(makeFolder("album-a", "Album A", std::move(childIds), std::string{"root"}, PlaylistNodeKind::Album));
    return snapshot;
}

// 可排序快照：folder-jazz 内 track-folder-b / track-folder-a / track-folder-c。
PlaylistTreeSnapshot makeSortableSnapshot()
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 21;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"folder-jazz", "track-root"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("folder-jazz", "Jazz", {"track-folder-b", "track-folder-a", "track-folder-c"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeTrack("track-folder-b", "track-folder-b-id", "02-beta.flac", "Beta Tune", "Charlie", "Album Z", std::chrono::milliseconds{180000}, std::string{"folder-jazz"}),
        makeTrack("track-folder-a", "track-folder-a-id", "01-alpha.flac", "Alpha Tune", "Delta", "Album A", std::chrono::milliseconds{60000}, std::string{"folder-jazz"}),
        makeTrack("track-folder-c", "track-folder-c-id", "03-gamma.flac", "Gamma Tune", "Bravo", "Album M", std::chrono::milliseconds{240000}, std::string{"folder-jazz"}),
        makeTrack("track-root", "track-root-id", "00-root.flac", "Root Tune", "Root Artist", "Root Album", std::chrono::milliseconds{120000}, std::string{"root"}),
    };
    return snapshot;
}

// 深链树：root → folder-1 → … → folder-<depth>（缓存容量/深度测试用）。
PlaylistTreeSnapshot makeDeepChainSnapshot(int depth)
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 41;
    snapshot.rootNodeId = std::string{"root"};
    std::vector<PlaylistNode> nodes;
    nodes.push_back(makeFolder("root", "Library", {"folder-1"}, std::nullopt, PlaylistNodeKind::Root));
    for (int i = 1; i <= depth; ++i) {
        const std::string nodeId = "folder-" + std::to_string(i);
        const std::string parent = i == 1 ? "root" : "folder-" + std::to_string(i - 1);
        std::vector<std::string> children;
        if (i < depth) {
            children.push_back("folder-" + std::to_string(i + 1));
        } else {
            children.push_back("track-deep");
        }
        nodes.push_back(makeFolder(nodeId, "Folder " + std::to_string(i), children, parent, PlaylistNodeKind::Album));
    }
    nodes.push_back(makeTrack("track-deep", "track-deep-id", "deep.flac", "Deep Tune", "Deep Artist", "Deep Album",
                              std::chrono::milliseconds{100000}, "folder-" + std::to_string(depth)));
    snapshot.nodes = std::move(nodes);
    return snapshot;
}

// 深层树：root → folder-a → folder-b → folder-c（栈生命周期测试用）。
PlaylistTreeSnapshot makeNestedTreeSnapshot()
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 31;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"folder-a", "track-root"}, std::nullopt, PlaylistNodeKind::Root),
        makeFolder("folder-a", "Folder A", {"folder-b", "track-a1"}, std::string{"root"}, PlaylistNodeKind::Album),
        makeFolder("folder-b", "Folder B", {"folder-c", "track-b1"}, std::string{"folder-a"}, PlaylistNodeKind::Album),
        makeFolder("folder-c", "Folder C", {"track-c1"}, std::string{"folder-b"}, PlaylistNodeKind::Album),
        makeTrack("track-root", "track-root-id", "root.flac", "Root Tune", "Root Artist", "Root Album", std::chrono::milliseconds{120000}, std::string{"root"}),
        makeTrack("track-a1", "track-a1-id", "a1.flac", "A One", "A Artist", "A Album", std::chrono::milliseconds{130000}, std::string{"folder-a"}),
        makeTrack("track-b1", "track-b1-id", "b1.flac", "B One", "B Artist", "B Album", std::chrono::milliseconds{140000}, std::string{"folder-b"}),
        makeTrack("track-c1", "track-c1-id", "c1.flac", "C One", "C Artist", "C Album", std::chrono::milliseconds{150000}, std::string{"folder-c"}),
    };
    return snapshot;
}

QString nodeIdAt(const LibraryModel *model, int row)
{
    return model->data(model->index(row, 0), LibraryModel::NodeIdRole).toString();
}

QString nodeIdAt(const LibraryFolderProjectionModel *model, int row)
{
    return model->data(model->index(row, 0), LibraryModel::NodeIdRole).toString();
}

QVector<QString> projectionNodeIds(const LibraryFolderProjectionModel *model)
{
    QVector<QString> nodeIds;
    nodeIds.reserve(model->rowCount());
    for (int row = 0; row < model->rowCount(); ++row) {
        nodeIds.append(nodeIdAt(model, row));
    }
    return nodeIds;
}

void expectProjection(const LibraryFolderProjectionModel *model, const QVector<QString> &nodeIds)
{
    QCOMPARE(model->rowCount(), nodeIds.size());
    for (int row = 0; row < nodeIds.size(); ++row) {
        QCOMPARE(nodeIdAt(model, row), nodeIds.at(row));
    }
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
    return canonicalRoot;
}

void installCommandRecorder(LibraryController &controller, CommandRecorder &recorder)
{
    controller.setCommandExecutor([&recorder](const MediaControlCommand &command) {
        return recorder.record(command);
    });
}
}

class LibraryFolderProjectionModelTest : public QObject
{
    Q_OBJECT

private slots:
    void projectionContentsPerFolderLevel();
    void projectionSortsPerLevelRules();
    void revisionAdvancesOnRebuild();
    void treeChangeRebuildsProjection();
    void treeRefreshEmitsIncrementalRowsWithoutReset();
    void treeRefreshEmitsExactRowOperationArguments();
    void idleTreeRefreshEmitsNoModelSignals();
    void survivingChangedRowEmitsDataChangedOnFinalRow();
    void playingStateRetainedAcrossTreeRefresh();
    void rowForNodeIdFollowsIncrementalRefresh();
    void modelTesterValidatesMixedIncrementalScenarios();
    void playingAndFocusSyncEmitDataChanged();
    void stackDepthAndProjectionLifecycle();
    void locateNodeInFolderStackNavigatesToTargetLevel();
    void projectionCacheReusesInstances();
    void treeChangeKeepsCachedModelIdentity();
    void sortChangeKeepsModelIdentity();
    void deepChainProjectionCacheSize();
    void depthSignalsEmitOnEnterAndGoBack();
    void reconcileClearedFolderEmitsSignals();
    void ancestorChainInvokableMatchesExpected();
    void roleNamesMatchLibraryModel();
    void sortRuleChangeRebuildsIncrementallyWithoutReset();
    void repeatSetSourceSameRulesEmitsNoModelSignals();
};

void LibraryFolderProjectionModelTest::projectionContentsPerFolderLevel()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());

    // 栈初始：只有根投影（folderNodeId 为空），投影 rootProjectionNodeIds。
    QCOMPARE(controller.folderStackDepth(), 0);
    auto *rootProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    QVERIFY(rootProj != nullptr);
    QVERIFY(rootProj->folderNodeId().isEmpty());
    expectProjection(rootProj, {QStringLiteral("album-a"), QStringLiteral("track-c")});
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::NameRole).toString(), QStringLiteral("Album A"));
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::ParentNameRole).toString(), QStringLiteral("Library"));
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::SongCountRole).toInt(), 2);
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::IsFolderRole).toBool(), true);

    // 进入 album-a：获取 album-a 投影，根投影不变。
    controller.enterFolder(QStringLiteral("album-a"));
    QCOMPARE(controller.folderStackDepth(), 1);
    auto *rootProjAfterEnter = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    auto *albumAProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("album-a")));
    QVERIFY(rootProjAfterEnter == rootProj);
    QVERIFY(albumAProj != nullptr);
    QCOMPARE(albumAProj->folderNodeId(), QStringLiteral("album-a"));
    expectProjection(albumAProj, {QStringLiteral("track-a"), QStringLiteral("track-b")});
    expectProjection(rootProj, {QStringLiteral("album-a"), QStringLiteral("track-c")});
    QCOMPARE(albumAProj->data(albumAProj->index(1, 0), LibraryModel::TitleRole).toString(), QStringLiteral("Song B"));
    QCOMPARE(albumAProj->data(albumAProj->index(1, 0), LibraryModel::ArtistRole).toString(), QStringLiteral("Artist B"));
    QCOMPARE(albumAProj->data(albumAProj->index(1, 0), LibraryModel::IsFolderRole).toBool(), false);

    // 返回：根实例与内容保持，album-a 实例保留在缓存中。
    controller.goBack();
    QCOMPARE(controller.folderStackDepth(), 0);
    auto *rootProjAfterBack = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    QVERIFY(rootProjAfterBack == rootProj);
    expectProjection(rootProj, {QStringLiteral("album-a"), QStringLiteral("track-c")});
}

void LibraryFolderProjectionModelTest::projectionSortsPerLevelRules()
{
    QTemporaryDir musicDir;
    LibraryController controller;
    CommandRecorder commandRecorder;
    installCommandRecorder(controller, commandRecorder);
    scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());

    controller.enterFolder(QStringLiteral("folder-jazz"));
    auto *jazzProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-jazz")));
    QVERIFY(jazzProj != nullptr);
    expectProjection(jazzProj, {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"), QStringLiteral("track-folder-c")});

    // 排序规则应用到当前文件夹投影（与主模型投影同语义）。
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    expectProjection(jazzProj, {QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b"), QStringLiteral("track-folder-c")});

    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("desc")}}));
    expectProjection(jazzProj, {QStringLiteral("track-folder-c"), QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a")});

    // 父级投影不受子级排序影响。
    auto *rootProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    expectProjection(rootProj, {QStringLiteral("folder-jazz"), QStringLiteral("track-root")});
}

void LibraryFolderProjectionModelTest::revisionAdvancesOnRebuild()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});
    QCOMPARE(projection.projectionRevision(), 1);
    QSignalSpy revisionSpy(&projection, &LibraryFolderProjectionModel::projectionRevisionChanged);

    // 树变化 → 全量重建 → revision 递增。
    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshotV2());
    QCOMPARE(projection.projectionRevision(), 2);
    QCOMPARE(revisionSpy.count(), 1);
    expectProjection(&projection, {QStringLiteral("track-x"), QStringLiteral("track-y")});

    // 重复 setSource 也视为一次重建。
    projection.setSource(&source, QStringLiteral("album-a"), {});
    QCOMPARE(projection.projectionRevision(), 3);
    QCOMPARE(revisionSpy.count(), 2);
}

void LibraryFolderProjectionModelTest::treeChangeRebuildsProjection()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());
    controller.enterFolder(QStringLiteral("album-a"));

    auto *rootProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    auto *albumAProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("album-a")));
    QVERIFY(rootProj != nullptr);
    QVERIFY(albumAProj != nullptr);
    expectProjection(albumAProj, {QStringLiteral("track-a"), QStringLiteral("track-b")});

    // 树重建：所有已建投影全量重建（实例保持，内容更新）。
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshotV2());
    expectProjection(albumAProj, {QStringLiteral("track-x"), QStringLiteral("track-y")});
    expectProjection(rootProj, {QStringLiteral("album-a"), QStringLiteral("track-c")});
    QCOMPARE(controller.folderStackDepth(), 1);

    // 对账后当前文件夹仍为 album-a，返回行为一致。
    controller.goBack();
    QCOMPARE(controller.folderStackDepth(), 0);
    expectProjection(rootProj, {QStringLiteral("album-a"), QStringLiteral("track-c")});
}

// watcher 触发的快照刷新（treeChanged）只能走增量行操作，绝不允许 reset：
// 这是滚动位置不闪回的模型层前提（setSource 仅首建/数据源切换/文件夹切换允许 reset）。
void LibraryFolderProjectionModelTest::treeRefreshEmitsIncrementalRowsWithoutReset()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {}); // setSource 允许 reset

    QSignalSpy aboutToResetSpy(&projection, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&projection, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&projection, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(&projection, &QAbstractItemModel::rowsRemoved);

    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshotV2());

    QCOMPARE(aboutToResetSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(removedSpy.count(), 1);
    expectProjection(&projection, {QStringLiteral("track-x"), QStringLiteral("track-y")});
}

// 行操作的精确信号参数：删除段坐标、插入段坐标、Move 的 destinationChild。
void LibraryFolderProjectionModelTest::treeRefreshEmitsExactRowOperationArguments()
{
    // A) 连续删除段合并为单次 rowsRemoved(0,1)（[a,b,c] → [c]）。
    {
        LibraryModel source;
        source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot(
            {{"track-a", "Song A"}, {"track-b", "Song B"}, {"track-c", "Song C"}}));
        LibraryFolderProjectionModel projection;
        projection.setSource(&source, QStringLiteral("album-a"), {});

        QSignalSpy removedSpy(&projection, &QAbstractItemModel::rowsRemoved);
        QSignalSpy resetSpy(&projection, &QAbstractItemModel::modelReset);
        source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-c", "Song C"}}, 52));

        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(removedSpy.count(), 1);
        const QList<QVariant> removedArgs = removedSpy.takeFirst();
        QCOMPARE(removedArgs.at(1).toInt(), 0);
        QCOMPARE(removedArgs.at(2).toInt(), 1);
        expectProjection(&projection, {QStringLiteral("track-c")});
    }

    // B) 连续插入段合并为单次 rowsInserted(1,2)（[a,b] → [a,x,y,b]）。
    {
        LibraryModel source;
        source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-a", "Song A"}, {"track-b", "Song B"}}));
        LibraryFolderProjectionModel projection;
        projection.setSource(&source, QStringLiteral("album-a"), {});

        QSignalSpy insertedSpy(&projection, &QAbstractItemModel::rowsInserted);
        source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot(
            {{"track-a", "Song A"}, {"track-x", "Song X"}, {"track-y", "Song Y"}, {"track-b", "Song B"}}, 52));

        QCOMPARE(insertedSpy.count(), 1);
        const QList<QVariant> insertedArgs = insertedSpy.takeFirst();
        QCOMPARE(insertedArgs.at(1).toInt(), 1);
        QCOMPARE(insertedArgs.at(2).toInt(), 2);
        expectProjection(&projection, {QStringLiteral("track-a"), QStringLiteral("track-x"),
                                       QStringLiteral("track-y"), QStringLiteral("track-b")});
    }

    // C) 向左移动：destinationChild == 0（[a,b] → [b,a]）。
    {
        LibraryModel source;
        source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-a", "Song A"}, {"track-b", "Song B"}}));
        LibraryFolderProjectionModel projection;
        projection.setSource(&source, QStringLiteral("album-a"), {});

        QSignalSpy aboutToMoveSpy(&projection, &QAbstractItemModel::rowsAboutToBeMoved);
        QSignalSpy movedSpy(&projection, &QAbstractItemModel::rowsMoved);
        QSignalSpy resetSpy(&projection, &QAbstractItemModel::modelReset);
        source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-b", "Song B"}, {"track-a", "Song A"}}, 52));

        QCOMPARE(resetSpy.count(), 0);
        QCOMPARE(aboutToMoveSpy.count(), 1);
        QCOMPARE(movedSpy.count(), 1);
        const QList<QVariant> moveArgs = aboutToMoveSpy.takeFirst();
        QCOMPARE(moveArgs.at(1).toInt(), 1); // sourceFirst
        QCOMPARE(moveArgs.at(2).toInt(), 1); // sourceLast
        QCOMPARE(moveArgs.at(4).toInt(), 0); // destinationChild
        expectProjection(&projection, {QStringLiteral("track-b"), QStringLiteral("track-a")});
    }
}

// 空闲刷新：内容完全一致时零模型信号，仅 revision 递增（QML 侧仍可感知刷新完成）。
void LibraryFolderProjectionModelTest::idleTreeRefreshEmitsNoModelSignals()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});

    int modelSignalCount = 0;
    QObject counter;
    const auto countOne = [&modelSignalCount]() { ++modelSignalCount; };
    QObject::connect(&projection, &QAbstractItemModel::rowsInserted, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::rowsRemoved, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::rowsMoved, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::dataChanged, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::modelAboutToBeReset, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::modelReset, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::layoutAboutToBeChanged, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::layoutChanged, &counter, countOne);
    QSignalSpy revisionSpy(&projection, &LibraryFolderProjectionModel::projectionRevisionChanged);
    const int revisionBefore = projection.projectionRevision();

    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());

    QCOMPARE(modelSignalCount, 0);
    QCOMPARE(projection.projectionRevision(), revisionBefore + 1);
    QCOMPARE(revisionSpy.count(), 1);
}

// 存活行的角色字段变化：dataChanged 必须命中该行在最终投影中的行号，
// 且 roles 为空 = 全角色（由增量重建在行操作之后统一发射）。
void LibraryFolderProjectionModelTest::survivingChangedRowEmitsDataChangedOnFinalRow()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-a", "Song A"}, {"track-b", "Song B"}}));

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});

    QSignalSpy movedSpy(&projection, &QAbstractItemModel::rowsMoved);
    QSignalSpy dataSpy(&projection, &QAbstractItemModel::dataChanged);

    // 刷新：track-b 前移（Move）；track-a 标题变化（最终行 1 发 dataChanged）。
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot(
        {{"track-b", "Song B"}, {"track-a", "Song A Renamed"}}, 52));

    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 1);
    const QList<QVariant> dataArgs = dataSpy.takeFirst();
    QCOMPARE(dataArgs.at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(dataArgs.at(1).value<QModelIndex>().row(), 1);
    QVERIFY(dataArgs.at(2).value<QList<int>>().isEmpty());
    QCOMPARE(projection.data(projection.index(1, 0), LibraryModel::TitleRole).toString(),
             QStringLiteral("Song A Renamed"));
    expectProjection(&projection, {QStringLiteral("track-b"), QStringLiteral("track-a")});
}

// 播放身份跨刷新保留：track-a 存活且被前插新行后，重新应用同一 trackId
// 必须命中其最终行。刷新与重应用可能各发一次 dataChanged（重复容忍，不锁计数）。
void LibraryFolderProjectionModelTest::playingStateRetainedAcrossTreeRefresh()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-a", "Song A"}, {"track-b", "Song B"}}));

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});
    QCOMPARE(projection.data(projection.index(0, 0), LibraryModel::IsPlayingRole).toBool(), false);

    source.setPlayingTrackId(QStringLiteral("track-a-id"));
    QCOMPARE(projection.data(projection.index(0, 0), LibraryModel::IsPlayingRole).toBool(), true);

    // 树刷新：源快照按既有语义清空播放身份；播放镜像随后重新应用同一 trackId。
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot(
        {{"track-x", "Song X"}, {"track-a", "Song A"}, {"track-b", "Song B"}}, 52));
    source.setPlayingTrackId(QStringLiteral("track-a-id"));

    expectProjection(&projection, {QStringLiteral("track-x"), QStringLiteral("track-a"), QStringLiteral("track-b")});
    QCOMPARE(projection.data(projection.index(1, 0), LibraryModel::IsPlayingRole).toBool(), true);
    QCOMPARE(projection.data(projection.index(0, 0), LibraryModel::IsPlayingRole).toBool(), false);
    QCOMPARE(projection.data(projection.index(2, 0), LibraryModel::IsPlayingRole).toBool(), false);
}

// rowForNodeId 跟随增量刷新：被移除键 -1，存活/新键指向最终行。
void LibraryFolderProjectionModelTest::rowForNodeIdFollowsIncrementalRefresh()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-a", "Song A"}, {"track-b", "Song B"}}));

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});
    QCOMPARE(projection.rowForNodeId(QStringLiteral("track-a")), 0);
    QCOMPARE(projection.rowForNodeId(QStringLiteral("track-b")), 1);

    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-b", "Song B"}, {"track-x", "Song X"}}, 52));
    QCOMPARE(projection.rowForNodeId(QStringLiteral("track-a")), -1);
    QCOMPARE(projection.rowForNodeId(QStringLiteral("track-b")), 0);
    QCOMPARE(projection.rowForNodeId(QStringLiteral("track-x")), 1);
    QCOMPARE(projection.rowForNodeId(QStringLiteral("missing-node")), -1);
}

// QAbstractItemModelTester(QtTest) 全程挂在投影模型上，覆盖增删移动/更新/
// 播放焦点/全量替换/空闲刷新混合场景，校验每个 begin/end 对的信号一致性。
void LibraryFolderProjectionModelTest::modelTesterValidatesMixedIncrementalScenarios()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot(
        {{"track-a", "Song A"}, {"track-b", "Song B"}, {"track-c", "Song C"}}));

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});

    QAbstractItemModelTester modelTester(&projection, QAbstractItemModelTester::FailureReportingMode::QtTest);

    // 混合场景 1：删除段 + 前插新键。
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-x", "Song X"}, {"track-c", "Song C"}}, 52));
    // 混合场景 2：重排（Move）+ 标题更新（dataChanged）。
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-c", "Song C2"}, {"track-x", "Song X"}}, 53));
    // 混合场景 3：播放/焦点身份变化。
    source.setPlayingTrackId(QStringLiteral("track-c-id"));
    source.setFocusedNodeId(QStringLiteral("track-x"));
    // 混合场景 4：全量替换（Remove all + Insert all）。
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-n", "Song N"}}, 54));
    // 混合场景 5：空闲刷新（零信号）。
    source.setPlaylistTreeSnapshot(makeAlbumChildrenSnapshot({{"track-n", "Song N"}}, 55));

    QCOMPARE(projection.rowCount(), 1);
    QCOMPARE(nodeIdAt(&projection, 0), QStringLiteral("track-n"));
    QVERIFY(projection.projectionRevision() > 0);
}

void LibraryFolderProjectionModelTest::playingAndFocusSyncEmitDataChanged()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());
    controller.enterFolder(QStringLiteral("album-a"));

    auto *rootProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    auto *albumAProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("album-a")));
    QVERIFY(rootProj != nullptr);
    QVERIFY(albumAProj != nullptr);
    QSignalSpy albumASpy(albumAProj, &QAbstractItemModel::dataChanged);
    QSignalSpy rootSpy(rootProj, &QAbstractItemModel::dataChanged);

    // 播放身份变化：仅投影内存在的行发 dataChanged（IsPlaying role）。
    controller.setPlayingTrackId(QStringLiteral("track-a-id"));
    QCOMPARE(albumASpy.count(), 1);
    const QList<QVariant> playingArguments = albumASpy.takeFirst();
    QCOMPARE(playingArguments.at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(playingArguments.at(1).value<QModelIndex>().row(), 0);
    QCOMPARE(playingArguments.at(2).value<QList<int>>(), QList<int>{LibraryModel::IsPlayingRole});
    QCOMPARE(albumAProj->data(albumAProj->index(0, 0), LibraryModel::IsPlayingRole).toBool(), true);
    QCOMPARE(albumAProj->data(albumAProj->index(1, 0), LibraryModel::IsPlayingRole).toBool(), false);
    QCOMPARE(rootSpy.count(), 0);

    // 播放切到 level 1 外的曲目：level 1 不再发，level 0 对自身行发。
    controller.setPlayingTrackId(QStringLiteral("track-c-id"));
    QCOMPARE(albumASpy.count(), 1);
    const QList<QVariant> clearedArguments = albumASpy.takeFirst();
    QCOMPARE(clearedArguments.at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(clearedArguments.at(2).value<QList<int>>(), QList<int>{LibraryModel::IsPlayingRole});
    QCOMPARE(albumAProj->data(albumAProj->index(0, 0), LibraryModel::IsPlayingRole).toBool(), false);
    QCOMPARE(rootSpy.count(), 1);
    const QList<QVariant> rootArguments = rootSpy.takeFirst();
    QCOMPARE(rootArguments.at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(rootProj->data(rootProj->index(1, 0), LibraryModel::IsPlayingRole).toBool(), true);
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::IsPlayingRole).toBool(), false);

    // 焦点身份变化：仅投影内存在的行发 dataChanged（IsFocused role）。
    controller.setFocusedNodeId(QStringLiteral("track-b"));
    QCOMPARE(albumASpy.count(), 1);
    const QList<QVariant> focusedArguments = albumASpy.takeFirst();
    QCOMPARE(focusedArguments.at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(focusedArguments.at(2).value<QList<int>>(), QList<int>{LibraryModel::IsFocusedRole});
    QCOMPARE(albumAProj->data(albumAProj->index(1, 0), LibraryModel::IsFocusedRole).toBool(), true);
    // level 0 同步清除其投影内 album-a 的焦点标记（仅投影内存在的行）。
    QCOMPARE(rootSpy.count(), 1);
    const QList<QVariant> rootFocusCleared = rootSpy.takeFirst();
    QCOMPARE(rootFocusCleared.at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(rootFocusCleared.at(2).value<QList<int>>(), QList<int>{LibraryModel::IsFocusedRole});
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::IsFocusedRole).toBool(), false);

    // 焦点移到 level 1 外的节点：level 1 清标记，level 0 对自身行发。
    controller.setFocusedNodeId(QStringLiteral("album-a"));
    QCOMPARE(albumASpy.count(), 1);
    const QList<QVariant> focusClearedArguments = albumASpy.takeFirst();
    QCOMPARE(focusClearedArguments.at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(albumAProj->data(albumAProj->index(1, 0), LibraryModel::IsFocusedRole).toBool(), false);
    QCOMPARE(rootSpy.count(), 1);
    QCOMPARE(rootProj->data(rootProj->index(0, 0), LibraryModel::IsFocusedRole).toBool(), true);
}

void LibraryFolderProjectionModelTest::stackDepthAndProjectionLifecycle()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeNestedTreeSnapshot());

    QObject *rootProjection = controller.projectionModelForNodeId(QString());
    QVERIFY(rootProjection != nullptr);
    QCOMPARE(controller.folderStackDepth(), 0);

    controller.enterFolder(QStringLiteral("folder-a"));
    QCOMPARE(controller.folderStackDepth(), 1);
    QVERIFY(controller.projectionModelForNodeId(QString()) == rootProjection);
    auto *folderAProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-a")));
    QVERIFY(folderAProj != nullptr);
    expectProjection(folderAProj, {QStringLiteral("folder-b"), QStringLiteral("track-a1")});

    controller.enterFolder(QStringLiteral("folder-b"));
    QCOMPARE(controller.folderStackDepth(), 2);
    QVERIFY(controller.projectionModelForNodeId(QString()) == rootProjection);
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-a")) == folderAProj);
    auto *folderBProj = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-b")));
    QVERIFY(folderBProj != nullptr);
    expectProjection(folderBProj, {QStringLiteral("folder-c"), QStringLiteral("track-b1")});

    // 返回：实例保留在缓存中（下方重入断言验证复用）；前缀级实例不变。
    controller.goBack();
    QCOMPARE(controller.folderStackDepth(), 1);
    QVERIFY(controller.projectionModelForNodeId(QString()) == rootProjection);
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-a")) == folderAProj);

    // 再次进入同级目录：复用同一缓存实例（缓存化语义：goBack 不再释放模型，
    // 重入 get-or-create 命中既有实例，视图滚动位置保留的前提）。
    controller.enterFolder(QStringLiteral("folder-b"));
    QCOMPARE(controller.folderStackDepth(), 2);
    QVERIFY(controller.projectionModelForNodeId(QString()) == rootProjection);
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-a")) == folderAProj);
    auto *folderBReentered = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-b")));
    QVERIFY(folderBReentered != nullptr);
    QVERIFY(folderBReentered == folderBProj);
    expectProjection(folderBReentered, {QStringLiteral("folder-c"), QStringLiteral("track-b1")});

    controller.goBack();
    controller.goBack();
    QCOMPARE(controller.folderStackDepth(), 0);
    QVERIFY(controller.projectionModelForNodeId(QString()) == rootProjection);
    expectProjection(qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString())),
                     {QStringLiteral("folder-a"), QStringLiteral("track-root")});
}

void LibraryFolderProjectionModelTest::locateNodeInFolderStackNavigatesToTargetLevel()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeNestedTreeSnapshot());

    // 深层曲目：从根逐级进入直到其直接父级，目标进入顶层投影（中间级逐级入栈）。
    controller.locateNodeInFolderStack(QStringLiteral("track-b1"));
    QCOMPARE(controller.folderStackDepth(), 2);
    auto *midLevel = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-a")));
    QVERIFY(midLevel != nullptr);
    QCOMPARE(midLevel->folderNodeId(), QStringLiteral("folder-a"));
    expectProjection(midLevel, {QStringLiteral("folder-b"), QStringLiteral("track-a1")});
    auto *top = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-b")));
    QVERIFY(top != nullptr);
    QCOMPARE(top->folderNodeId(), QStringLiteral("folder-b"));
    expectProjection(top, {QStringLiteral("folder-c"), QStringLiteral("track-b1")});

    // 目标已在当前投影：不导航。
    const int depthBefore = controller.folderStackDepth();
    controller.locateNodeInFolderStack(QStringLiteral("folder-c"));
    QCOMPARE(controller.folderStackDepth(), depthBefore);
    QCOMPARE(controller.projectionModelForNodeId(QStringLiteral("folder-b")), top);

    // 根直属目标（父为根节点）：回到根浏览，目标在根投影。
    controller.locateNodeInFolderStack(QStringLiteral("track-root"));
    QCOMPARE(controller.folderStackDepth(), 0);
    auto *rootProjection = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QString()));
    QVERIFY(rootProjection != nullptr);
    expectProjection(rootProjection, {QStringLiteral("folder-a"), QStringLiteral("track-root")});

    // 文件夹目标：进入其直接父级（目标显示在父级投影中）。
    controller.locateNodeInFolderStack(QStringLiteral("folder-b"));
    QCOMPARE(controller.folderStackDepth(), 1);
    auto *folderTop = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-a")));
    QVERIFY(folderTop != nullptr);
    QCOMPARE(folderTop->folderNodeId(), QStringLiteral("folder-a"));
    expectProjection(folderTop, {QStringLiteral("folder-b"), QStringLiteral("track-a1")});

    // 从深层回跳到浅层目标：栈收缩到目标级。
    controller.locateNodeInFolderStack(QStringLiteral("track-c1"));
    QCOMPARE(controller.folderStackDepth(), 3);
    controller.locateNodeInFolderStack(QStringLiteral("track-a1"));
    QCOMPARE(controller.folderStackDepth(), 1);
    auto *shallowTop = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-a")));
    QVERIFY(shallowTop != nullptr);
    QCOMPARE(shallowTop->folderNodeId(), QStringLiteral("folder-a"));

    // 未知节点：不导航。
    const int depthBeforeUnknown = controller.folderStackDepth();
    controller.locateNodeInFolderStack(QStringLiteral("missing-node"));
    QCOMPARE(controller.folderStackDepth(), depthBeforeUnknown);
}

void LibraryFolderProjectionModelTest::projectionCacheReusesInstances()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeNestedTreeSnapshot());

    controller.enterFolder(QStringLiteral("folder-a"));
    auto *folderA = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-a")));
    QVERIFY(folderA != nullptr);
    controller.enterFolder(QStringLiteral("folder-b"));
    auto *folderB = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-b")));
    QVERIFY(folderB != nullptr);
    // 不同层独立实例。
    QVERIFY(folderB != folderA);
    // 同层重复取用返回同一实例。
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-a")) == folderA);

    // 返回后实例保留（缓存化：不销毁）。
    controller.goBack();
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-b")) == folderB);
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-a")) == folderA);

    // 重入复用同一实例。
    controller.enterFolder(QStringLiteral("folder-b"));
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-b")) == folderB);
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-a")) == folderA);
}

void LibraryFolderProjectionModelTest::treeChangeKeepsCachedModelIdentity()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());
    controller.enterFolder(QStringLiteral("album-a"));

    auto *level1 = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("album-a")));
    QVERIFY(level1 != nullptr);
    expectProjection(level1, {QStringLiteral("track-a"), QStringLiteral("track-b")});
    const int generationBefore = controller.projectionGeneration();

    // 树重建：缓存模型实例身份不变（setSource 连接的 treeChanged 自动原地重建），
    // 内容更新为新树，投影代次递增。
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshotV2());
    QCOMPARE(controller.projectionGeneration(), generationBefore + 1);
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("album-a")) == level1);
    expectProjection(level1, {QStringLiteral("track-x"), QStringLiteral("track-y")});
    QVERIFY(controller.projectionModelForNodeId(QString()) == controller.projectionModelForNodeId(QString()));
}

void LibraryFolderProjectionModelTest::sortChangeKeepsModelIdentity()
{
    QTemporaryDir musicDir;
    LibraryController controller;
    CommandRecorder commandRecorder;
    installCommandRecorder(controller, commandRecorder);
    scanTemporaryRoot(controller, musicDir);
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    controller.enterFolder(QStringLiteral("folder-jazz"));

    auto *level1 = qobject_cast<LibraryFolderProjectionModel *>(controller.projectionModelForNodeId(QStringLiteral("folder-jazz")));
    QVERIFY(level1 != nullptr);
    const int revisionBefore = level1->projectionRevision();

    // 排序变更：模型对象身份不变（setSource 原地重建），顺序更新。
    controller.applySortRules(sortRules({{QStringLiteral("title"), QStringLiteral("asc")}}));
    QVERIFY(controller.projectionModelForNodeId(QStringLiteral("folder-jazz")) == level1);
    expectProjection(level1, {QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b"), QStringLiteral("track-folder-c")});
    QVERIFY(level1->projectionRevision() > revisionBefore);
}

void LibraryFolderProjectionModelTest::deepChainProjectionCacheSize()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeDeepChainSnapshot(25));

    // 初始只有根键。
    QCOMPARE(controller.projectionCacheSize(), 1);

    for (int i = 1; i <= 25; ++i) {
        controller.enterFolder(QStringLiteral("folder-%1").arg(i));
    }
    QCOMPARE(controller.folderStackDepth(), 25);
    // 缓存大小 == 访问目录数 + 1（含根键）。
    QCOMPARE(controller.projectionCacheSize(), 26);

    // 逐级返回后缓存不收缩（实例保留）。
    while (controller.canGoBack()) {
        controller.goBack();
    }
    QCOMPARE(controller.folderStackDepth(), 0);
    QCOMPARE(controller.projectionCacheSize(), 26);
}

void LibraryFolderProjectionModelTest::depthSignalsEmitOnEnterAndGoBack()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeNestedTreeSnapshot());
    QSignalSpy depthSpy(&controller, &LibraryController::folderStackDepthChanged);
    QSignalSpy nodeSpy(&controller, &LibraryController::currentFolderNodeIdChanged);

    controller.enterFolder(QStringLiteral("folder-a"));
    QCOMPARE(depthSpy.count(), 1);
    QCOMPARE(nodeSpy.count(), 1);
    QCOMPARE(controller.currentFolderNodeId(), QStringLiteral("folder-a"));

    controller.enterFolder(QStringLiteral("folder-b"));
    QCOMPARE(depthSpy.count(), 2);
    QCOMPARE(nodeSpy.count(), 2);

    // 重复进入同一文件夹：深度/节点未变，不发。
    controller.enterFolder(QStringLiteral("folder-b"));
    QCOMPARE(depthSpy.count(), 2);
    QCOMPARE(nodeSpy.count(), 2);

    controller.goBack();
    QCOMPARE(depthSpy.count(), 3);
    QCOMPARE(nodeSpy.count(), 3);
    QCOMPARE(controller.currentFolderNodeId(), QStringLiteral("folder-a"));

    controller.goBack();
    QCOMPARE(depthSpy.count(), 4);
    QCOMPARE(nodeSpy.count(), 4);
    QCOMPARE(controller.currentFolderNodeId(), QString());
}

void LibraryFolderProjectionModelTest::reconcileClearedFolderEmitsSignals()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());
    controller.enterFolder(QStringLiteral("album-a"));
    QCOMPARE(controller.folderStackDepth(), 1);

    QSignalSpy depthSpy(&controller, &LibraryController::folderStackDepthChanged);
    QSignalSpy nodeSpy(&controller, &LibraryController::currentFolderNodeIdChanged);

    // 重扫树不含 album-a：对账清空当前文件夹，folderStackDepthChanged 与
    // currentFolderNodeIdChanged 同步发射（深度 1 → 0，节点 → 根键）。
    controller.setPlaylistTreeSnapshot(makeSortableSnapshot());
    QCOMPARE(controller.folderStackDepth(), 0);
    QCOMPARE(controller.currentFolderNodeId(), QString());
    QCOMPARE(depthSpy.count(), 1);
    QCOMPARE(nodeSpy.count(), 1);
}

void LibraryFolderProjectionModelTest::ancestorChainInvokableMatchesExpected()
{
    LibraryController controller;
    controller.setPlaylistTreeSnapshot(makeNestedTreeSnapshot());

    // 3 层树：链 == [顶层, 中层, 目标]（从根向目标、排除根）。
    QStringList chain;
    const bool invoked = QMetaObject::invokeMethod(
        &controller, "ancestorChainForNode", Qt::DirectConnection,
        Q_RETURN_ARG(QStringList, chain), Q_ARG(QString, QStringLiteral("folder-c")));
    QVERIFY(invoked);
    QCOMPARE(chain, QStringList({QStringLiteral("folder-a"), QStringLiteral("folder-b"), QStringLiteral("folder-c")}));

    // 根直属节点：链只含自身。
    QStringList rootChildChain;
    QVERIFY(QMetaObject::invokeMethod(&controller, "ancestorChainForNode", Qt::DirectConnection,
                                      Q_RETURN_ARG(QStringList, rootChildChain), Q_ARG(QString, QStringLiteral("folder-a"))));
    QCOMPARE(rootChildChain, QStringList({QStringLiteral("folder-a")}));

    // 未知节点：现有 C++ 实现返回 [目标, 根]，去根后等价变换为 [目标]。
    QStringList unknownChain;
    QVERIFY(QMetaObject::invokeMethod(&controller, "ancestorChainForNode", Qt::DirectConnection,
                                      Q_RETURN_ARG(QStringList, unknownChain), Q_ARG(QString, QStringLiteral("missing-node"))));
    QCOMPARE(unknownChain, QStringList({QStringLiteral("missing-node")}));
}

void LibraryFolderProjectionModelTest::roleNamesMatchLibraryModel()
{
    const LibraryModel libraryModel;
    const LibraryFolderProjectionModel projectionModel;

    const QHash<int, QByteArray> expected = libraryModel.roleNames();
    const QHash<int, QByteArray> actual = projectionModel.roleNames();
    QCOMPARE(actual, expected);
    QCOMPARE(actual.size(), expected.size());
}

// 同一源+同一文件夹下的排序规则变更：只允许增量重建（行操作），绝不允许 reset；
// 投影内容按新规则排序、sortRules 更新、revision 递增。
void LibraryFolderProjectionModelTest::sortRuleChangeRebuildsIncrementallyWithoutReset()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeSortableSnapshot());

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("folder-jazz"), {}); // 首建允许 reset
    expectProjection(&projection, {QStringLiteral("track-folder-b"), QStringLiteral("track-folder-a"),
                                   QStringLiteral("track-folder-c")});
    const int revisionBefore = projection.projectionRevision();

    QSignalSpy aboutToResetSpy(&projection, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&projection, &QAbstractItemModel::modelReset);
    QSignalSpy movedSpy(&projection, &QAbstractItemModel::rowsMoved);
    QSignalSpy insertedSpy(&projection, &QAbstractItemModel::rowsInserted);
    QSignalSpy removedSpy(&projection, &QAbstractItemModel::rowsRemoved);

    // 标题升序：原序 [b,a,c] → [a,b,c]，键集合不变，至少一次 rowsMoved。
    const QVector<LibraryModel::SortRule> titleAscending{{QStringLiteral("title"), QStringLiteral("asc")}};
    projection.setSource(&source, QStringLiteral("folder-jazz"), titleAscending);

    QCOMPARE(aboutToResetSpy.count(), 0);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 0);
    QCOMPARE(removedSpy.count(), 0);
    QVERIFY(movedSpy.count() >= 1);
    expectProjection(&projection, {QStringLiteral("track-folder-a"), QStringLiteral("track-folder-b"),
                                   QStringLiteral("track-folder-c")});
    QCOMPARE(projection.sortRules().size(), 1);
    QCOMPARE(projection.sortRules().at(0).field, QStringLiteral("title"));
    QCOMPARE(projection.sortRules().at(0).order, QStringLiteral("asc"));
    QCOMPARE(projection.projectionRevision(), revisionBefore + 1);
}

// 同一源+同一文件夹+相同规则重复 setSource：零模型信号（幂等），仅 revision 递增。
void LibraryFolderProjectionModelTest::repeatSetSourceSameRulesEmitsNoModelSignals()
{
    LibraryModel source;
    source.setPlaylistTreeSnapshot(makeProjectedTreeSnapshot());

    LibraryFolderProjectionModel projection;
    projection.setSource(&source, QStringLiteral("album-a"), {});

    int modelSignalCount = 0;
    QObject counter;
    const auto countOne = [&modelSignalCount]() { ++modelSignalCount; };
    QObject::connect(&projection, &QAbstractItemModel::rowsInserted, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::rowsRemoved, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::rowsMoved, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::dataChanged, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::modelAboutToBeReset, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::modelReset, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::layoutAboutToBeChanged, &counter, countOne);
    QObject::connect(&projection, &QAbstractItemModel::layoutChanged, &counter, countOne);
    QSignalSpy revisionSpy(&projection, &LibraryFolderProjectionModel::projectionRevisionChanged);
    const int revisionBefore = projection.projectionRevision();

    projection.setSource(&source, QStringLiteral("album-a"), {});

    QCOMPARE(modelSignalCount, 0);
    QCOMPARE(projection.projectionRevision(), revisionBefore + 1);
    QCOMPARE(revisionSpy.count(), 1);
    expectProjection(&projection, {QStringLiteral("track-a"), QStringLiteral("track-b")});
}

QTEST_GUILESS_MAIN(LibraryFolderProjectionModelTest)

#include "tst_library_folder_projection_model.moc"
