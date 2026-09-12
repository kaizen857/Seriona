// FolderPage 与 PlaylistDelegate 前端适配测试（Task 2）
// 断言列表：
// 1. 无 anchors 且 width/height 跟随 parent
// 2. 注入投影模型后 ListView 渲染子项
// 3. 滚动位置保持（实例存活期间 contentY 不变）+ 库刷新（投影增量行操作）不扰动视口
// 4. 错落滑入动画幂等（连续激活两次 opacity 终值 1.0, translate x 终值 0）
// 5. Y 竞态回归锁：动画前后 delegate.y 恒定无跳变
// 6. 全量替换（Remove all + Insert all）不 reset、保留旧 contentY（不无理由跳顶）
// 7. 视口上方插入/删除后 contentY 保持、锚点行随内容位移
// 8. 视口外刷新（重命名/前插/删除/重排）后可见行 indexAt↔delegate↔模型映射一致

#include "app_facade.h"
#include "library_folder_projection_model.h"
#include "library_model.h"

#include <QGuiApplication>
#include <QMetaProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QSignalSpy>
#include <QTest>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using seriona::scanner::PlaylistNode;
using seriona::scanner::PlaylistNodeKind;
using seriona::scanner::PlaylistTreeSnapshot;
using seriona::scanner::SongMetadata;

namespace {

constexpr auto kHostQml = R"(
import QtQuick
import QtQuick.Window
import Seriona

Window {
    id: testWindow
    width: 600
    height: 700
    visible: true

    Item {
        id: container
        objectName: "container"
        width: 300
        height: 600

        FolderPage {
            id: folderPage
            objectName: "folderPage"
            folderNodeId: "test_folder_node"

            activateNodeHandler: (nodeId, isFolder) => {
                testHost.onActivateNode(nodeId, isFolder);
            }
            closeMenusHandler: () => {
                testHost.onCloseMenus();
            }
            contextMenuHost: QtObject {
                property bool isOpen: false
                function openForEntry(entry, targetDelegate, mouseX, mouseY) {
                    testHost.onContextMenuOpen(entry ? entry.nodeId : "");
                }
                function close() {
                    isOpen = false;
                }
            }
        }
    }
}
)";

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

} // namespace

class TestHostBridge : public QObject
{
    Q_OBJECT

public:
    explicit TestHostBridge(QObject *parent = nullptr) : QObject(parent) {}

    QString lastActivatedNodeId;
    bool lastActivatedIsFolder = false;
    bool menusClosed = false;
    QString lastContextMenuNodeId;

public slots:
    void onActivateNode(const QString &nodeId, bool isFolder)
    {
        lastActivatedNodeId = nodeId;
        lastActivatedIsFolder = isFolder;
    }

    void onCloseMenus()
    {
        menusClosed = true;
    }

    void onContextMenuOpen(const QString &nodeId)
    {
        lastContextMenuNodeId = nodeId;
    }
};

class FolderPageTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void componentLoadingAndDimensions();
    void modelRendering();
    void scrollPositionRetention();
    void scrollAnchorFallbackWhenAnchorMissing();
    void aboveViewportInsertKeepsContentY();
    void refreshSweepKeepsVisibleRowsMapped();
    void animationIdempotency();
    void yRaceRegressionLock();

private:
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QQuickItem *container = nullptr;
    QQuickItem *folderPage = nullptr;
    TestHostBridge hostBridge;
    std::unique_ptr<Seriona::App::AppFacade> facade;
    Seriona::App::LibraryController libraryController;

    void applyTreeSnapshot(int childCount = 20, const std::string &nodeIdPrefix = "child_");
    void applyTreeSnapshotChildren(const std::vector<std::string> &childIds);
    void sweepVisibleRows(QQuickItem *listView, Seriona::App::LibraryFolderProjectionModel *projection);
};

void FolderPageTest::applyTreeSnapshot(int childCount, const std::string &nodeIdPrefix)
{
    std::vector<std::string> childIds;
    std::vector<PlaylistNode> nodes;

    for (int i = 0; i < childCount; ++i) {
        std::string cId = nodeIdPrefix + std::to_string(i);
        childIds.push_back(cId);
        nodes.push_back(makeTrack(cId, "track_id_" + std::to_string(i),
                                  "Track " + std::to_string(i), "Title " + std::to_string(i),
                                  "Artist " + std::to_string(i), "Album " + std::to_string(i),
                                  std::chrono::milliseconds{180000}, "test_folder_node"));
    }

    nodes.push_back(makeFolder("root", "Library", {"test_folder_node"}, std::nullopt, PlaylistNodeKind::Root));
    nodes.push_back(makeFolder("test_folder_node", "Test Folder", childIds, std::string{"root"}, PlaylistNodeKind::Directory));

    PlaylistTreeSnapshot snapshot;
    snapshot.version = 1;
    snapshot.rootNodeId = "root";
    snapshot.nodes = std::move(nodes);

    libraryController.setPlaylistTreeSnapshot(snapshot);
}

void FolderPageTest::applyTreeSnapshotChildren(const std::vector<std::string> &childIds)
{
    std::vector<PlaylistNode> nodes;
    nodes.reserve(childIds.size() + 2);

    for (const std::string &childId : childIds) {
        nodes.push_back(makeTrack(childId, childId + "-track", "Track " + childId, "Title " + childId,
                                  "Artist " + childId, "Album " + childId,
                                  std::chrono::milliseconds{180000}, "test_folder_node"));
    }
    nodes.push_back(makeFolder("root", "Library", {"test_folder_node"}, std::nullopt, PlaylistNodeKind::Root));
    nodes.push_back(makeFolder("test_folder_node", "Test Folder", childIds, std::string{"root"}, PlaylistNodeKind::Directory));

    PlaylistTreeSnapshot snapshot;
    snapshot.version = 1;
    snapshot.rootNodeId = "root";
    snapshot.nodes = std::move(nodes);

    libraryController.setPlaylistTreeSnapshot(snapshot);
}

// 从当前视口顶部起逐行遍历，断言每个可见行 indexAt 命中且 delegate.nodeId
// 与模型最终行映射一致（QTBUG-120941 视图刷新后 itemAtIndex 失效回归锁）。
void FolderPageTest::sweepVisibleRows(QQuickItem *listView, Seriona::App::LibraryFolderProjectionModel *projection)
{
    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(30);

    const qreal viewportTop = listView->property("contentY").toReal();
    const qreal viewportBottom = viewportTop + listView->height();
    int firstVisible = -1;
    QMetaObject::invokeMethod(listView, "indexAt", Q_RETURN_ARG(int, firstVisible),
                              Q_ARG(qreal, 0.0), Q_ARG(qreal, viewportTop + 1.0));
    QVERIFY2(firstVisible >= 0, "no visible row at current contentY");

    int sweptRows = 0;
    for (int row = firstVisible; row < projection->rowCount(); ++row) {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row));
        if (item == nullptr) {
            break;
        }
        const qreal itemTop = item->y();
        if (itemTop >= viewportBottom) {
            break;
        }
        if (itemTop + item->height() <= viewportTop) {
            continue;
        }

        int hit = -1;
        QMetaObject::invokeMethod(listView, "indexAt", Q_RETURN_ARG(int, hit),
                                  Q_ARG(qreal, 0.0), Q_ARG(qreal, qMax(itemTop, viewportTop) + 1.0));
        QCOMPARE(hit, row);
        const QString nodeId = item->property("nodeId").toString();
        QVERIFY2(!nodeId.isEmpty(), "delegate rendered without nodeId");
        QCOMPARE(nodeId, projection->data(projection->index(row, 0), Seriona::App::LibraryModel::NodeIdRole).toString());
        QCOMPARE(projection->rowForNodeId(nodeId), row);
        ++sweptRows;
    }
    QVERIFY2(sweptRows > 0, "sweep visited no visible rows");
}

void FolderPageTest::initTestCase()
{
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    facade = std::make_unique<Seriona::App::AppFacade>();
    QCOMPARE(facade->backendBridgeStartedForTests(), false);

    engine.rootContext()->setContextProperty(QStringLiteral("testHost"), &hostBridge);
    engine.rootContext()->setContextProperty(QStringLiteral("appFacadeContext"), facade.get());
    engine.rootContext()->setContextProperty(QStringLiteral("libraryContext"), &libraryController);
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.loadData(QByteArray(kHostQml), QUrl(QStringLiteral("qrc:/seriona_folder_page_test.qml")));

    QVERIFY2(!engine.rootObjects().isEmpty(), "host Window failed to load");
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY2(window != nullptr, "host root is not a QQuickWindow");
    QVERIFY2(window->isVisible(), "host window is not visible");

    container = qobject_cast<QQuickItem *>(window->findChild<QObject *>(QStringLiteral("container")));
    QVERIFY2(container != nullptr, "container not found");

    folderPage = qobject_cast<QQuickItem *>(window->findChild<QObject *>(QStringLiteral("folderPage")));
    QVERIFY2(folderPage != nullptr, "FolderPage instance not found");
}

void FolderPageTest::componentLoadingAndDimensions()
{
    // 1. 无 anchors 约束验证：FolderPage 根节点使用 parent 尺寸绑定，而非 anchors
    // 验证跟随 container 尺寸
    QCOMPARE(folderPage->width(), container->width());
    QCOMPARE(folderPage->height(), container->height());

    // 动态调整 container 尺寸，断言跟随
    container->setWidth(350);
    container->setHeight(650);
    QCOMPARE(folderPage->width(), 350.0);
    QCOMPARE(folderPage->height(), 650.0);

    // 恢复尺寸
    container->setWidth(300);
    container->setHeight(600);
    QCOMPARE(folderPage->width(), 300.0);
    QCOMPARE(folderPage->height(), 600.0);
}

void FolderPageTest::modelRendering()
{
    applyTreeSnapshot(25);

    auto *projection = libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node"));
    QVERIFY(projection != nullptr);

    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);

    QTRY_COMPARE(listView->property("count").toInt(), 25);

    // 验证 delegate 渲染
    QTRY_VERIFY(listView->findChild<QQuickItem *>() != nullptr);
}

void FolderPageTest::scrollPositionRetention()
{
    applyTreeSnapshot(25);

    auto *projection = libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node"));
    QVERIFY(projection != nullptr);
    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);
    QTRY_COMPARE(listView->property("count").toInt(), 25);

    // 强制布局
    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(50);

    // 滚动到某位置
    listView->setProperty("contentY", 150.0);
    QTest::qWait(50);
    QCOMPARE(listView->property("contentY").toReal(), 150.0);

    // 组件未销毁，反复读取 contentY 保持
    QTest::qWait(100);
    QCOMPARE(listView->property("contentY").toReal(), 150.0);

    // 模拟 watcher 触发的库刷新（同一内容快照 → 增量刷新零行操作）：
    // 刷新不得扰动视口：顶部可见条目与条目内偏移必须保留。
    int anchorIndex = -1;
    QMetaObject::invokeMethod(listView, "indexAt", Q_RETURN_ARG(int, anchorIndex),
                              Q_ARG(qreal, 0.0), Q_ARG(qreal, 151.0));
    QVERIFY2(anchorIndex >= 0, "no visible anchor row at contentY=150");
    QQuickItem *anchorItem = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, anchorItem),
                              Q_ARG(int, anchorIndex));
    QVERIFY(anchorItem != nullptr);
    const QString anchorNodeId = anchorItem->property("nodeId").toString();
    QVERIFY(!anchorNodeId.isEmpty());
    const qreal anchorOffset = listView->property("contentY").toReal() - anchorItem->y();

    applyTreeSnapshot(25);

    QTRY_COMPARE(listView->property("count").toInt(), 25);
    QTRY_VERIFY_WITH_TIMEOUT(listView->property("contentY").toReal() > 100.0, 5000);
    int restoredIndex = -1;
    QMetaObject::invokeMethod(listView, "indexAt", Q_RETURN_ARG(int, restoredIndex),
                              Q_ARG(qreal, 0.0),
                              Q_ARG(qreal, listView->property("contentY").toReal() + 1.0));
    QVERIFY2(restoredIndex >= 0, "no visible row after refresh restore");
    QQuickItem *restoredItem = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, restoredItem),
                              Q_ARG(int, restoredIndex));
    QVERIFY(restoredItem != nullptr);
    QCOMPARE(restoredItem->property("nodeId").toString(), anchorNodeId);
    QVERIFY2(qAbs((listView->property("contentY").toReal() - restoredItem->y()) - anchorOffset) < 1.0,
             "intra-item scroll offset not retained");
}

void FolderPageTest::scrollAnchorFallbackWhenAnchorMissing()
{
    applyTreeSnapshot(25);

    auto *projection = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node")));
    QVERIFY(projection != nullptr);
    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);
    QTRY_COMPARE(listView->property("count").toInt(), 25);

    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(50);
    listView->setProperty("contentY", 150.0);
    QTest::qWait(50);
    const qreal savedContentY = listView->property("contentY").toReal();
    QCOMPARE(savedContentY, 150.0);

    // 全部条目换 id（模拟整个文件夹内容被重命名/移动）：
    // 增量全量替换（Remove all + Insert all）不 reset → 视图保留旧 contentY，而非跳回顶部。
    applyTreeSnapshot(25, "renamed_child_");

    QTRY_COMPARE(listView->property("count").toInt(), 25);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(listView->property("contentY").toReal() - savedContentY) < 1.0, 5000);

    // 全量替换后可见行必须完整实例化（仅 contentY 数值保持不算通过）。
    sweepVisibleRows(listView, projection);
}

// 视口上方插入/删除：增量行操作不得给 contentY 赋值（不跳顶），
// 原顶部条目随内容整体位移后仍在视口内渲染。
void FolderPageTest::aboveViewportInsertKeepsContentY()
{
    applyTreeSnapshot(25);

    auto *projection = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node")));
    QVERIFY(projection != nullptr);
    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);
    QTRY_COMPARE(listView->property("count").toInt(), 25);

    // 前置归位：前序用例的全量替换可能在本机 Qt 6.11.2 上留下不完整布局（视图保留
    // contentY 数值但未完成行实例化，属 Qt 视图缺陷），先回顶部等待布局收敛。
    listView->setProperty("contentY", 0.0);
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        QMetaObject::invokeMethod(listView, "forceLayout");
        QQuickItem *topItem = nullptr;
        QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, topItem), Q_ARG(int, 0));
        return topItem != nullptr && qAbs(topItem->y()) < 1.0;
    }(), 5000);

    listView->setProperty("contentY", 150.0);
    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(50);
    QCOMPARE(listView->property("contentY").toReal(), 150.0);

    int anchorIndex = -1;
    QMetaObject::invokeMethod(listView, "indexAt", Q_RETURN_ARG(int, anchorIndex),
                              Q_ARG(qreal, 0.0), Q_ARG(qreal, 151.0));
    QVERIFY2(anchorIndex >= 0, "no visible anchor row at contentY=150");
    QQuickItem *anchorItem = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, anchorItem),
                              Q_ARG(int, anchorIndex));
    QVERIFY(anchorItem != nullptr);
    const QString anchorNodeId = anchorItem->property("nodeId").toString();
    QVERIFY(!anchorNodeId.isEmpty());
    // 锚点行必须与模型行映射一致（防前序残留布局导致 indexAt 指向错误行）。
    QCOMPARE(anchorNodeId, projection->data(projection->index(anchorIndex, 0),
                                            Seriona::App::LibraryModel::NodeIdRole).toString());

    // 顶部前插 2 行（视口上方 Insert，行高 72 → 内容整体下移 144px）。
    std::vector<std::string> prepended{"prepend_0", "prepend_1"};
    for (int i = 0; i < 25; ++i) {
        prepended.push_back("child_" + std::to_string(i));
    }
    applyTreeSnapshotChildren(prepended);

    QTRY_COMPARE(listView->property("count").toInt(), 27);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(listView->property("contentY").toReal() - 150.0) < 1.0, 5000);
    QQuickItem *shiftedAnchor = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        QMetaObject::invokeMethod(listView, "forceLayout");
        QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, shiftedAnchor),
                                  Q_ARG(int, anchorIndex + 2));
        return shiftedAnchor != nullptr;
    }(), 5000);
    QCOMPARE(shiftedAnchor->property("nodeId").toString(), anchorNodeId);

    // 再删除这 2 行（视口上方 Remove）：contentY 仍稳定，锚点回到原行。
    applyTreeSnapshot(25);

    QTRY_COMPARE(listView->property("count").toInt(), 25);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(listView->property("contentY").toReal() - 150.0) < 1.0, 5000);
    QQuickItem *restoredAnchor = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        QMetaObject::invokeMethod(listView, "forceLayout");
        QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, restoredAnchor),
                                  Q_ARG(int, anchorIndex));
        return restoredAnchor != nullptr;
    }(), 5000);
    QCOMPARE(restoredAnchor->property("nodeId").toString(), anchorNodeId);
}

// QTBUG-120941 回归扫描（视口外修改）：滚到底部后，反复触发只改动视口外（顶部）
// 节点的外部刷新（重命名/前插/删除），每次从顶到底遍历可见行：indexAt 必须命中、
// delegate.nodeId 与模型最终行及 rowForNodeId 映射必须一致（刷新后行索引不失配）。
void FolderPageTest::refreshSweepKeepsVisibleRowsMapped()
{
    applyTreeSnapshot(25);

    auto *projection = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node")));
    QVERIFY(projection != nullptr);
    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);
    QTRY_COMPARE(listView->property("count").toInt(), 25);
    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(50);

    // 滚到底部：视口只覆盖模型尾部，后续刷新只改动视口外（顶部）节点。
    const qreal bottom = listView->property("contentHeight").toReal()
                       + listView->property("bottomMargin").toReal()
                       - listView->height();
    listView->setProperty("contentY", bottom);
    QTest::qWait(50);

    // 刷新 1：视口外重命名（顶部 child_0 → moved_0：Remove 0 + Insert 0）。
    std::vector<std::string> renamed{"moved_0"};
    for (int i = 1; i < 25; ++i) {
        renamed.push_back("child_" + std::to_string(i));
    }
    applyTreeSnapshotChildren(renamed);
    QTRY_COMPARE(listView->property("count").toInt(), 25);
    sweepVisibleRows(listView, projection);

    // 刷新 2：视口外前插 2 行（顶部 Insert）。
    std::vector<std::string> prepended{"extra_0", "extra_1"};
    prepended.insert(prepended.end(), renamed.begin(), renamed.end());
    applyTreeSnapshotChildren(prepended);
    QTRY_COMPARE(listView->property("count").toInt(), 27);
    sweepVisibleRows(listView, projection);

    // 刷新 3：删除视口外前插的 2 行（顶部 Remove）。
    applyTreeSnapshotChildren(renamed);
    QTRY_COMPARE(listView->property("count").toInt(), 25);
    sweepVisibleRows(listView, projection);

    // 刷新 4：视口外重排（顶部两行互换 → 单次 Move），校验移动后行映射。
    QSignalSpy movedSpy(projection, &QAbstractItemModel::rowsMoved);
    std::vector<std::string> swapped = renamed;
    const std::string firstId = swapped.at(0);
    swapped.at(0) = swapped.at(1);
    swapped.at(1) = firstId;
    applyTreeSnapshotChildren(swapped);
    QTRY_COMPARE(listView->property("count").toInt(), 25);
    QCOMPARE(movedSpy.count(), 1);
    sweepVisibleRows(listView, projection);
}

void FolderPageTest::animationIdempotency()
{
    applyTreeSnapshot(25);

    auto *projection = libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node"));
    QVERIFY(projection != nullptr);
    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);

    listView->setProperty("contentY", 0.0);
    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(50);

    // 连续激活两次
    folderPage->setProperty("isActive", false);
    folderPage->setProperty("navDirection", 1);
    folderPage->setProperty("isActive", true);
    QTest::qWait(20);

    folderPage->setProperty("isActive", false);
    folderPage->setProperty("isActive", true);

    // 检查视口内第一个 delegate 的 opacity 和 translate.x 终值
    // （动画 220ms + stagger 最大 180ms；慢机上轮询等待终值收敛，不用固定睡窗）
    QQuickItem *firstDelegate = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, firstDelegate), Q_ARG(int, 0));
    QVERIFY2(firstDelegate != nullptr, "firstDelegate not found at index 0");
    QTRY_COMPARE_WITH_TIMEOUT(firstDelegate->property("opacity").toReal(), 1.0, 8000);

    auto transformList = firstDelegate->transform();
    QVERIFY2(transformList.count(&transformList) > 0, "firstDelegate transform list is empty");
    auto *tr = transformList.at(&transformList, 0);
    QVERIFY2(tr != nullptr, "transform element is null");
    QTRY_COMPARE_WITH_TIMEOUT(tr->property("x").toReal(), 0.0, 8000);
}

void FolderPageTest::yRaceRegressionLock()
{
    // Y 竞态回归锁：
    // 使用 QObject::connect 连接到 delegate 根 Item 的 yChanged 信号，
    // 在激活动画期间持续监控 delegate 根 y 属性变化。
    // 断言：激活动画期间 delegate 根 y 恒等于布局值，绝不发生非布局跳变（如误动画到 0）。
    applyTreeSnapshot(25);

    auto *projection = libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node"));
    QVERIFY(projection != nullptr);
    folderPage->setProperty("projectionModel", QVariant::fromValue(projection));

    auto *listView = folderPage->findChild<QQuickItem *>(QStringLiteral("folderListView"));
    QVERIFY(listView != nullptr);

    // 恢复顶部
    listView->setProperty("contentY", 0.0);
    QMetaObject::invokeMethod(listView, "forceLayout");
    QTest::qWait(50);

    QQuickItem *secondDelegate = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, secondDelegate), Q_ARG(int, 1));
    QVERIFY(secondDelegate != nullptr);

    const qreal initialY = secondDelegate->y();
    QVERIFY(initialY > 0); // 第二项 y 应在 72 左右

    std::vector<qreal> yHistory;
    yHistory.push_back(initialY);

    // 连接 yChanged 监控信号
    auto conn = QObject::connect(secondDelegate, &QQuickItem::yChanged, [&]() {
        yHistory.push_back(secondDelegate->y());
    });

    // 激活动画
    folderPage->setProperty("isActive", false);
    folderPage->setProperty("navDirection", 1);
    folderPage->setProperty("isActive", true);

    // 采样等待动画结束
    for (int frame = 0; frame < 20; ++frame) {
        QTest::qWait(25);
        yHistory.push_back(secondDelegate->y());
    }

    QObject::disconnect(conn);

    // 断言在整个激活动画期间，secondDelegate 的 y 始终等于 initialY
    for (qreal yVal : yHistory) {
        QCOMPARE(yVal, initialY);
    }
}

QTEST_MAIN(FolderPageTest)
#include "tst_folder_page.moc"

