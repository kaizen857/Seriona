// FolderPage 与 PlaylistDelegate 前端适配测试（Task 2）
// 断言列表：
// 1. 无 anchors 且 width/height 跟随 parent
// 2. 注入投影模型后 ListView 渲染子项
// 3. 滚动位置保持（实例存活期间 contentY 不变）+ 库刷新（投影 reset）后滚动锚点保留
// 4. 错落滑入动画幂等（连续激活两次 opacity 终值 1.0, translate x 终值 0）
// 5. Y 竞态回归锁：动画前后 delegate.y 恒定无跳变
// 6. 锚点条目消失时回退到钳制后的旧 contentY（不无理由跳顶）

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

    // 模拟外部移动/重命名文件 → watcher 触发库刷新（快照重建 → 投影 reset）：
    // 顶部可见条目与条目内偏移必须保留。
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

    auto *projection = libraryController.projectionModelForNodeId(QStringLiteral("test_folder_node"));
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

    // 全部条目换 id（模拟锚点文件被重命名/移动后不在新投影内）：
    // 内容总量不变 → 必须回退到钳制后的旧 contentY，而非跳回顶部。
    applyTreeSnapshot(25, "renamed_child_");

    QTRY_COMPARE(listView->property("count").toInt(), 25);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(listView->property("contentY").toReal() - savedContentY) < 1.0, 5000);
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

