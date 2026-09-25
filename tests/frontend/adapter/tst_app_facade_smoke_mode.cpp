#include "app_facade.h"

#include "lyric_split_boundary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QtTest/QTest>

#if SERIONA_HAS_BACKEND
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
using seriona::scanner::PlaylistNode;
using seriona::scanner::PlaylistNodeKind;
using seriona::scanner::PlaylistTreeSnapshot;
using seriona::scanner::SongMetadata;

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

PlaylistNode makeTrack(const std::string &nodeId, const std::string &trackId)
{
    SongMetadata song;
    song.trackId = trackId;
    song.title = "Song C";
    song.duration = std::chrono::milliseconds{120000};

    PlaylistNode node;
    node.nodeId = nodeId;
    node.parentNodeId = std::string{"root"};
    node.kind = PlaylistNodeKind::Track;
    node.displayName = "Song C";
    node.song = std::move(song);
    return node;
}

PlaylistTreeSnapshot makeSnapshot()
{
    PlaylistTreeSnapshot snapshot;
    snapshot.version = 140;
    snapshot.rootNodeId = std::string{"root"};
    snapshot.nodes = {
        makeFolder("root", "Library", {"track-c"}, std::nullopt, PlaylistNodeKind::Root),
        makeTrack("track-c", "track-c-id"),
    };
    return snapshot;
}

seriona::control::LibraryStateSnapshot makeLibrarySnapshot()
{
    seriona::control::LibraryStateSnapshot snapshot;
    snapshot.libraryTree = makeSnapshot();
    return snapshot;
}

seriona::control::PlayerStateSnapshot makePlayerSnapshot()
{
    seriona::control::PlayerStateSnapshot snapshot;
    snapshot.currentTrack = seriona::control::TrackIdentity{};
    snapshot.currentTrack->trackId = "track-c-id";
    return snapshot;
}

QVariant modelValue(const Seriona::App::LibraryModel &model, const QString &nodeId, int role)
{
    const int row = model.rowForNodeId(nodeId);
    if (row < 0) {
        return {};
    }
    return model.data(model.index(row, 0), role);
}
}
#endif

class AppFacadeSmokeModeTest : public QObject
{
    Q_OBJECT

private slots:
    void doesNotStartBackendBridgeWhenSmokeDisablesAutostart();
    void sourceKeepsFacadeThin();
    void libraryControllerSubmitsTrackActivationThroughBridge();
    void lateLibrarySnapshotReappliesPlayingHighlight();
    void trackSwitchIncrementsPlayCountThroughFacade();
    void unchangedBoundaryCommitDispatchesNoCommand();
    void initialBoundaryCutReproducesSpacedRow();
    void changedBoundaryCommitDispatchesCommand();
};

void AppFacadeSmokeModeTest::doesNotStartBackendBridgeWhenSmokeDisablesAutostart()
{
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    Seriona::App::AppFacade facade;

    QCOMPARE(facade.backendBridgeStartedForTests(), false);

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
}

void AppFacadeSmokeModeTest::sourceKeepsFacadeThin()
{
    const QDir sourceRoot(QString::fromUtf8(QT_TESTCASE_SOURCEDIR));
    QFile header(sourceRoot.filePath(QStringLiteral("src/app/app_facade.h")));
    QFile implementation(sourceRoot.filePath(QStringLiteral("src/app/app_facade.cpp")));

    QVERIFY2(header.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(header.fileName()));
    QVERIFY2(implementation.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(implementation.fileName()));

    const QString headerText = QString::fromUtf8(header.readAll());
    const QString implementationText = QString::fromUtf8(implementation.readAll());

    const QStringList forbiddenFacadeTokens = {
        QStringLiteral("requestWaveformForSnapshots"),
        QStringLiteral("syncLibraryPlayingTrackId"),
        QStringLiteral("m_currentWaveformCacheKey"),
        QStringLiteral("makeWaveformRequest"),
        QStringLiteral("playingTrackIdFromSnapshot"),
    };

    for (const QString &token : forbiddenFacadeTokens) {
        const QByteArray headerMessage = QStringLiteral("AppFacade header still owns business token: %1").arg(token).toUtf8();
        QVERIFY2(!headerText.contains(token), headerMessage.constData());
        const QByteArray implementationMessage = QStringLiteral("AppFacade implementation still owns business token: %1").arg(token).toUtf8();
        QVERIFY2(!implementationText.contains(token), implementationMessage.constData());
    }
}

void AppFacadeSmokeModeTest::libraryControllerSubmitsTrackActivationThroughBridge()
{
#if SERIONA_HAS_BACKEND
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    QTemporaryDir musicDir;
    QVERIFY(musicDir.isValid());
    const QString canonicalRoot = QFileInfo(musicDir.path()).absoluteFilePath();

    Seriona::App::AppFacade facade;
    std::vector<QString> scannedRoots;
    facade.library()->setScanExecutor([&scannedRoots](const QString &rootPath, seriona::scanner::ScanMode) {
        scannedRoots.push_back(rootPath);
        seriona::control::MediaControllerCommandResult result;
        result.accepted = true;
        result.code = seriona::control::MediaControllerErrorCode::None;
        return result;
    });
    QVERIFY(facade.scanLibrary(QUrl::fromLocalFile(musicDir.path())));
    QCOMPARE(scannedRoots, std::vector<QString>{canonicalRoot});
    facade.library()->setPlaylistTreeSnapshot(makeSnapshot());

    QCOMPARE(facade.backendNotificationCountForTests(), std::size_t{0});
    facade.library()->playItem(QStringLiteral("track-c"));

    QCOMPARE(facade.backendNotificationCountForTests(), std::size_t{1});

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
#else
    QSKIP("backend disabled");
#endif
}

void AppFacadeSmokeModeTest::lateLibrarySnapshotReappliesPlayingHighlight()
{
#if SERIONA_HAS_BACKEND
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    Seriona::App::AppFacade facade;
    const seriona::control::PlayerStateSnapshot player = makePlayerSnapshot();
    facade.applyPlayerSnapshotForTests(player, seriona::control::LibraryStateSnapshot{});
    facade.library()->setPlayingTrackId(QString());
    facade.applyLibrarySnapshotForTests(player, makeLibrarySnapshot());

    QCOMPARE(facade.playback()->currentTrackId(), QStringLiteral("track-c-id"));
    QCOMPARE(facade.playback()->currentTrackNodeId(), QStringLiteral("track-c"));
    QCOMPARE(facade.library()->playingTrackId(), QStringLiteral("track-c-id"));
    QCOMPARE(modelValue(*facade.library()->model(), QStringLiteral("track-c"), Seriona::App::LibraryModel::IsPlayingRole).toBool(), true);

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
#else
    QSKIP("backend disabled");
#endif
}

void AppFacadeSmokeModeTest::trackSwitchIncrementsPlayCountThroughFacade()
{
#if SERIONA_HAS_BACKEND
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    Seriona::App::AppFacade facade;
    seriona::control::PlayerStateSnapshot first;
    first.currentTrack = seriona::control::TrackIdentity{};
    first.currentTrack->trackId = "track-c-id";
    seriona::control::PlayerStateSnapshot second;
    second.currentTrack = seriona::control::TrackIdentity{};
    second.currentTrack->trackId = "track-d-id";
    seriona::control::PlayerStateSnapshot empty;

    facade.applyPlayerSnapshotForTests(first, seriona::control::LibraryStateSnapshot{});
    QCOMPARE(facade.trackStats()->playCountFor(QStringLiteral("track-c-id")), 1);

    facade.applyPlayerSnapshotForTests(first, seriona::control::LibraryStateSnapshot{});
    QCOMPARE(facade.trackStats()->playCountFor(QStringLiteral("track-c-id")), 1);

    facade.applyPlayerSnapshotForTests(second, seriona::control::LibraryStateSnapshot{});
    QCOMPARE(facade.trackStats()->playCountFor(QStringLiteral("track-d-id")), 1);
    QCOMPARE(facade.trackStats()->playCountFor(QStringLiteral("track-c-id")), 1);

    facade.applyPlayerSnapshotForTests(empty, seriona::control::LibraryStateSnapshot{});
    QCOMPARE(facade.trackStats()->playCountFor(QStringLiteral("track-d-id")), 1);

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
#else
    QSKIP("backend disabled");
#endif
}

// D33-1：窗口打开时的初始分界复现该行当前展示对，因此「选中一行、不拖动、直接保存」
// 在门函数上必须是无操作：返回 false 且不发任何命令。
// 这里用「未启动的 bridge 会把任何外发命令转成一条拒绝通知」作可观测面：
// 只要命令被外发，backendNotificationCountForTests() 就会 +1。命令零外发 ⇒ 计数保持 0。
// 该行取空格分隔的弱约定（跳过段是纯空白），初始分界与当前展示对逐字一致。
void AppFacadeSmokeModeTest::unchangedBoundaryCommitDispatchesNoCommand()
{
#if SERIONA_HAS_BACKEND
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    Seriona::App::AppFacade facade;
    QCOMPARE(facade.backendBridgeStartedForTests(), false);

    const QString rawLine = QStringLiteral("日文 中文");
    const QString shown = QStringLiteral("日文");
    const QString trans = QStringLiteral("中文");

    const QVariantMap cut = facade.lyricSplitBoundaryCut(rawLine, shown, trans);
    QCOMPARE(cut.value(QStringLiteral("valid")).toBool(), true);
    const int boundaryIndex = cut.value(QStringLiteral("leftEnd")).toInt();

    const QVariantMap parts = facade.lyricSplitBoundaryParts(rawLine, boundaryIndex);
    QCOMPARE(parts.value(QStringLiteral("original")).toString(), shown);
    QCOMPARE(parts.value(QStringLiteral("translation")).toString(), trans);

    QCOMPARE(facade.backendNotificationCountForTests(), std::size_t{0});
    QCOMPARE(facade.commitLyricSplitBoundary(rawLine, boundaryIndex, shown, trans), false);
    QCOMPARE(facade.backendNotificationCountForTests(), std::size_t{0});

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
#else
    QSKIP("backend disabled");
#endif
}

// 含分隔符字形的行：窗口初始分界（gap 形式）同样复现当前展示对；
// 单点切片在「原文结束处」会多带分隔符（故保存由 touched 结构挡住，见 QML 源契约）。
void AppFacadeSmokeModeTest::initialBoundaryCutReproducesSpacedRow()
{
#if SERIONA_HAS_BACKEND
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    Seriona::App::AppFacade facade;

    const QString rawLine = QStringLiteral("揺るぎない Spirit / 坚定不移的Spirit");
    const QString shown = QStringLiteral("揺るぎない Spirit");
    const QString trans = QStringLiteral("坚定不移的Spirit");

    const Seriona::App::LyricSplitBoundaryCut cut =
        Seriona::App::findLyricSplitBoundaryCut(rawLine, shown, trans);
    QVERIFY(cut.valid);
    const Seriona::App::LyricSplitBoundaryParts parts =
        Seriona::App::splitLyricLineAtBoundary(rawLine, cut);
    QCOMPARE(parts.original, shown);
    QCOMPARE(parts.translation, trans);

    const QVariantMap cutFromFacade = facade.lyricSplitBoundaryCut(rawLine, shown, trans);
    QCOMPARE(cutFromFacade.value(QStringLiteral("valid")).toBool(), true);
    QCOMPARE(cutFromFacade.value(QStringLiteral("leftEnd")).toInt(), cut.leftEnd);
    QCOMPARE(cutFromFacade.value(QStringLiteral("rightStart")).toInt(), cut.rightStart);

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
#else
    QSKIP("backend disabled");
#endif
}

// 真实变更仍会走到命令外发（同一门函数、同一分界，只把当前译文换成别的值）。
// bridge 未启动故返回 false，但命令确实被外发 —— 由拒绝通知计数证明。
void AppFacadeSmokeModeTest::changedBoundaryCommitDispatchesCommand()
{
#if SERIONA_HAS_BACKEND
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    Seriona::App::AppFacade facade;
    QCOMPARE(facade.backendBridgeStartedForTests(), false);

    const QString rawLine = QStringLiteral("日文 中文");
    const QString shown = QStringLiteral("日文");
    const QString trans = QStringLiteral("中文");
    const int boundaryIndex = facade.lyricSplitBoundaryCut(rawLine, shown, trans)
                                  .value(QStringLiteral("leftEnd"))
                                  .toInt();

    QCOMPARE(facade.backendNotificationCountForTests(), std::size_t{0});
    QCOMPARE(facade.commitLyricSplitBoundary(rawLine, boundaryIndex, shown, QStringLiteral("旧译")), false);
    QCOMPARE(facade.backendNotificationCountForTests(), std::size_t{1});

    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", QVariant{});
#else
    QSKIP("backend disabled");
#endif
}

QTEST_GUILESS_MAIN(AppFacadeSmokeModeTest)

#include "tst_app_facade_smoke_mode.moc"
