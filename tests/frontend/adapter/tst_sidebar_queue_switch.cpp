// 侧边栏文件夹/队列视图切换（T15）+ StackView 页面栈导航（Task 3）测试：
// 加载 qml/components/Sidebar.qml，断言默认文件夹视图、切换按钮状态、队列空状态说明、
// 注入队列后条目渲染、队列右键菜单"从队列移除"项在队列上下文可见（TrackContextMenu
// queueContext），以及文件夹页面栈导航（进入/返回/滚动保留/动画抑制/缓存/locate 收敛/
// rescan 自愈/配对 no-op 自愈/深层排序）。
//
// 测试用真实 AppFacade（关闭后端桥自启，避免启动线程）+ LibraryController
// 经 context property 注入。导航驱动方式：主用 libraryController.enterFolder/
// goBack/applyLibraryStateSnapshot/setPlayingTrackId（C++ 信号驱动收敛）+
// QQuickItem 断言；返回键用 QMetaObject::invokeMethod(sidebar, "handleBackClicked")；
// locate 用 libraryController.locateCurrentSong()。
#include "app_facade.h"
#include "library_folder_projection_model.h"
#include "library_model.h"
#include "seriona/control/control_contracts.h"

#include <QColor>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QTest>
#include <QWindow>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr auto kHostQml = R"(
import QtQuick
import QtQuick.Window
import Seriona

Window {
    id: testWindow
    width: 900
    height: 700
    visible: true

    Sidebar {
        id: sidebar
        objectName: "sidebar"
        height: parent.height
        appFacade: appFacadeContext
        libraryController: libraryContext
    }
}
)";

QVariantMap makeQueueEntry(const QString &trackId, const QString &title, const QString &artist, bool isPlaying)
{
    return QVariantMap{
        {QStringLiteral("trackId"), trackId},
        {QStringLiteral("nodeId"), trackId},
        {QStringLiteral("title"), title},
        {QStringLiteral("artist"), artist},
        {QStringLiteral("isPlaying"), isPlaying},
    };
}

// 两位零填充（保证按标题字典序与数字序一致：01..12）
std::string padded(int value)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", value);
    return std::string(buf);
}

// 树构建器：nodes 按 childNodeIds 顺序保留（排序规则为空时投影保持该顺序）
struct TreeSpec {
    seriona::scanner::PlaylistTreeSnapshot tree;

    TreeSpec()
    {
        tree.rootNodeId = std::string{"root"};
    }

    void root(std::vector<std::string> children)
    {
        seriona::scanner::PlaylistNode node;
        node.nodeId = std::string{"root"};
        node.kind = seriona::scanner::PlaylistNodeKind::Root;
        node.displayName = std::string{"Library"};
        node.childNodeIds = std::move(children);
        tree.nodes.push_back(std::move(node));
    }

    void folder(const std::string &nodeId, const std::string &parent, const std::string &name, std::vector<std::string> children)
    {
        seriona::scanner::PlaylistNode node;
        node.nodeId = nodeId;
        node.parentNodeId = parent;
        node.kind = seriona::scanner::PlaylistNodeKind::Directory;
        node.displayName = name;
        node.childNodeIds = std::move(children);
        tree.nodes.push_back(std::move(node));
    }

    void track(const std::string &nodeId, const std::string &parent, const std::string &title)
    {
        seriona::scanner::SongMetadata song;
        song.trackId = nodeId;
        song.title = title;
        seriona::scanner::PlaylistNode node;
        node.nodeId = nodeId;
        node.parentNodeId = parent;
        node.kind = seriona::scanner::PlaylistNodeKind::Track;
        node.displayName = title;
        node.song = std::move(song);
        tree.nodes.push_back(std::move(node));
    }
};

void applyTree(Seriona::App::LibraryController &controller, TreeSpec spec)
{
    seriona::control::LibraryStateSnapshot library;
    library.libraryTree = std::move(spec.tree);
    controller.applyLibraryStateSnapshot(library);
}

// 监听 listView 第 index 个 delegate 的 navTranslate.x 变化（动画是否播过）。
// 返回 nullptr 表示该 index 尚无 delegate/transform（调用方先 QTRY 就绪）。
// 注：QSignalSpy 无法直接监听 QML 创建的 QQuickTranslate 的 xChanged
// （QML 元对象注册的 indexOfSignal 返回 -1），因此改为轮询采样断言。
qreal translateX(QQuickItem *listView, int index)
{
    if (!listView) {
        return -1.0;
    }
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
    if (!item) {
        return -1.0;
    }
    auto list = item->transform();
    if (list.count(&list) == 0) {
        return -1.0;
    }
    return list.at(&list, 0)->property("x").toReal();
}

// 在窗口期内轮询 delegate 的 navTranslate.x：出现非 0 采样即视为"动画播过"。
// 返回 true = 观察到动画；false = 全程无位移（delegate 缺失采样视为无位移）。
// 窗口默认 2000ms：慢机（10-100x）上点击→信号链→首帧→动画起播的延迟可远大于
// 原生 400ms（动画本体 ~400ms），短窗会把"迟到的动画"误判为没播。
bool xMoved(QQuickItem *listView, int index, int windowMs = 2000)
{
    const QDeadlineTimer deadline(windowMs);
    while (!deadline.hasExpired()) {
        const qreal x = translateX(listView, index);
        if (x != 0.0 && x != -1.0) {
            return true;
        }
        QTest::qWait(5);
    }
    return false;
}

// 在窗口期内轮询 StackView.busy：出现过 true 采样 = 有过渡运行。
bool busyFlipped(QQuickItem *stack, int windowMs = 2000)
{
    const QDeadlineTimer deadline(windowMs);
    while (!deadline.hasExpired()) {
        if (stack->property("busy").toBool()) {
            return true;
        }
        QTest::qWait(5);
    }
    return false;
}

// 负向断言前置：先收敛到静止基线——StackView 空闲、且在采样对象上确实存在的
// delegate 位移已归 0——再开负向采样窗。慢机上若上一段动画的尾巴仍在飞行就采样，
// 会把旧位移误判为本次触发（假失败）。delegate 缺失（translateX == -1，视图被覆盖
// 或正在重建）视为已静止，不做等待；已存在却长时间停在非 0 位则视为异常。
void settleZeroBaseline(QQuickItem *stack, QQuickItem *listView, int index)
{
    if (stack) {
        QTRY_VERIFY_WITH_TIMEOUT(!stack->property("busy").toBool(), 8000);
    }
    if (listView) {
        const QDeadlineTimer deadline(8000);
        while (!deadline.hasExpired()) {
            const qreal x = translateX(listView, index);
            if (x == -1.0 || qAbs(x - 0.0) < 1e-6) {
                return;
            }
            QTest::qWait(50);
        }
        QVERIFY2(false, "delegate stayed off its zero baseline for 8s");
    }
}

} // namespace

class SidebarQueueSwitchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void defaultViewIsFolder();
    void switchToQueueShowsEmptyGuide();
    void switchBackToFolderRestoresFolderView();
    void queueEntriesRenderWithPlayingHighlight();
    void queueContextMenuItemVisibleOnlyInQueueContext();
    void scrollRetentionAcrossFolderNavigation();
    void rootScrollRetentionRegression();
    void emptyFolderAndRootBackNoOp();
    void backToRootClearsStack();
    void rootSlideInAnimationOnBack();
    void followPlayingZeroAnimation();
    void repeatEnterReusesPageInstance();
    void deepChainNavigationAndCacheSize();
    void searchActivePreservesNavigationState();
    void locateWhileSearchingExitsSearchAndLocates();
    void rapidBackClicksStateConsistency();
    void controllerStackSynchronization();
    void locateColdCacheAndBranchVariants();
    void noOpLocateSuppressionReset();
    void rescanRemovesCurrentFolderFallback();
    void pairingNoOpSelfHealing();
    void deepChainSortRuleRestoration();
    // 用户报告：根视图（playlistView）条目 hover 无 UI 变化，深层 FolderPage 条目正常——回归锁
    void rootViewDelegateHover();
    // 对照组：FolderPage 条目 hover 必须保持正常（hoverEnabled: false 不得波及其子树）
    void folderPageDelegateHover();

private:
    QQmlApplicationEngine engine;
    QQuickWindow *window = nullptr;
    QQuickItem *sidebar = nullptr;
    std::unique_ptr<Seriona::App::AppFacade> facade;
    Seriona::App::LibraryController libraryController;

    QObject *findItem(const QString &objectName) const;
    QQuickItem *switchButton(const QString &objectName) const;
    void clickSwitch(const QString &objectName);
    void applyQueueSnapshot(const QVariantList &entries, const QString &currentTrackId = QString());
    void applyDeepFolderTree();
    void applyFolderChainTree(int depth);

    // ---- Task 3 导航测试工具 ----
    QQuickItem *stackView() const;
    QObject *pageAt(int index) const;
    QObject *currentPage() const;
    QObject *currentListView() const;
    QQuickItem *itemAt(QQuickItem *listView, int index) const;
    void clickDelegate(QQuickItem *delegate);
    QQuickItem *backButton() const;
    int countFolderPageInstances() const;
    // 控制器与栈逐层对齐断言（depth 0 单独断言 currentFolderNodeId 为空）
    void assertStackAligned();
    // 确定性等待：先给足时间让任一在途过渡启动（busy 在下一帧才置位），
    // 再等 StackView 完全空闲（220ms 过渡结束）
    void waitStackSettled();
    // ListView 语义的"顶部"断言：contentY 被钳到 -topMargin（首项位于 topMargin 之下）
    void assertListViewAtTop(QObject *listView) const;
    // 复位导航状态（页面缓存/栈/控制器/搜索/队列），保证测试隔离
    void resetNavigationState();
    // 树构建（节点 id 命名空间彼此独立，避免跨测试缓存污染）
    void applyRichTree(int variant = 0);
    void applyBranchTree();
    void applyBranchTreeNoF2();
    void applyBranchTreeNoF1();
    void applyEmptyFolderTree();
    void applyDeepScrollableTree(int depth, const QString &prefix = QStringLiteral("f"));
};

void SidebarQueueSwitchTest::initTestCase()
{
    QCoreApplication::instance()->setProperty("seriona.backendBridgeAutostartEnabled", false);

    facade = std::make_unique<Seriona::App::AppFacade>();
    QCOMPARE(facade->backendBridgeStartedForTests(), false);

    engine.rootContext()->setContextProperty(QStringLiteral("appFacadeContext"), facade.get());
    engine.rootContext()->setContextProperty(QStringLiteral("libraryContext"), &libraryController);
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.loadData(QByteArray(kHostQml), QUrl(QStringLiteral("qrc:/seriona_sidebar_queue_test.qml")));

    QVERIFY2(!engine.rootObjects().isEmpty(), "host Window failed to load");
    window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY2(window != nullptr, "host root is not a QQuickWindow");
    QVERIFY2(window->isVisible(), "host window is not visible");

    sidebar = qobject_cast<QQuickItem *>(window->findChild<QObject *>(QStringLiteral("sidebar")));
    QVERIFY2(sidebar != nullptr, "Sidebar instance not found");
}

QObject *SidebarQueueSwitchTest::findItem(const QString &objectName) const
{
    return window->findChild<QObject *>(objectName);
}

QQuickItem *SidebarQueueSwitchTest::switchButton(const QString &objectName) const
{
    return qobject_cast<QQuickItem *>(findItem(objectName));
}

void SidebarQueueSwitchTest::clickSwitch(const QString &objectName)
{
    QQuickItem *button = switchButton(objectName);
    QVERIFY2(button != nullptr, qPrintable(QStringLiteral("switch button %1 not found").arg(objectName)));
    const QPointF center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
}

void SidebarQueueSwitchTest::applyQueueSnapshot(const QVariantList &entries, const QString &currentTrackId)
{
    // 先注入曲库快照（同一批 trackId 可解析出标题/艺术家），队列映射依赖曲库
    seriona::control::LibraryStateSnapshot library;
    seriona::scanner::PlaylistTreeSnapshot tree;
    tree.rootNodeId = std::string{"root"};
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        seriona::scanner::SongMetadata song;
        song.trackId = map.value(QStringLiteral("trackId")).toString().toStdString();
        song.title = map.value(QStringLiteral("title")).toString().toStdString();
        song.artist = map.value(QStringLiteral("artist")).toString().toStdString();
        seriona::scanner::PlaylistNode node;
        node.nodeId = map.value(QStringLiteral("nodeId")).toString().toStdString();
        node.kind = seriona::scanner::PlaylistNodeKind::Track;
        node.displayName = song.title;
        node.song = std::move(song);
        tree.nodes.push_back(std::move(node));
    }
    library.libraryTree = std::move(tree);
    facade->library()->applyLibraryStateSnapshot(library);

    seriona::control::PlayerStateSnapshot snapshot;
    snapshot.playback.state = seriona::control::PlaybackStatus::Playing;
    if (!currentTrackId.isEmpty()) {
        snapshot.currentTrack = seriona::control::TrackIdentity{};
        snapshot.currentTrack->trackId = currentTrackId.toStdString();
    }
    for (const QVariant &entry : entries) {
        const QVariantMap map = entry.toMap();
        seriona::control::QueueEntry queueEntry;
        queueEntry.trackId = map.value(QStringLiteral("trackId")).toString().toStdString();
        queueEntry.nodeId = map.value(QStringLiteral("nodeId")).toString().toStdString();
        snapshot.queueEntries.push_back(std::move(queueEntry));
    }
    facade->playback()->applyPlayerStateSnapshot(snapshot, &library);
}

void SidebarQueueSwitchTest::defaultViewIsFolder()
{
    QCOMPARE(sidebar->property("queueViewActive").toBool(), false);

    QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY2(playlistView != nullptr, "folder playlist view not found");
    QVERIFY(playlistView->isVisible());

    QQuickItem *queueListView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("queueListView")));
    QVERIFY2(queueListView != nullptr, "queue view not found");
    QVERIFY(!queueListView->isVisible());
}

void SidebarQueueSwitchTest::switchToQueueShowsEmptyGuide()
{
    clickSwitch(QStringLiteral("queueViewButton"));
    QCOMPARE(sidebar->property("queueViewActive").toBool(), true);

    QQuickItem *queueListView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("queueListView")));
    QVERIFY(queueListView->isVisible());
    QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(!playlistView->isVisible());

    QObject *emptyState = findItem(QStringLiteral("queueEmptyState"));
    QVERIFY2(emptyState != nullptr, "queue empty state not found");
    QTRY_VERIFY(qobject_cast<QQuickItem *>(emptyState)->isVisible());
    // emptyTitle/emptyHint 定义在 QueueView 根（queueListView），空状态 Column 只是展示容器
    const QString title = queueListView->property("emptyTitle").toString();
    const QString hint = queueListView->property("emptyHint").toString();
    QVERIFY2(title.contains(QStringLiteral("队列为空")), qPrintable(title));
    QVERIFY2(hint.contains(QStringLiteral("添加到下一首播放")), qPrintable(hint));
}

void SidebarQueueSwitchTest::switchBackToFolderRestoresFolderView()
{
    clickSwitch(QStringLiteral("folderViewButton"));
    QCOMPARE(sidebar->property("queueViewActive").toBool(), false);

    QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(playlistView->isVisible());
    QQuickItem *queueListView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("queueListView")));
    QVERIFY(!queueListView->isVisible());
}

void SidebarQueueSwitchTest::queueEntriesRenderWithPlayingHighlight()
{
    QVariantList entries;
    entries.append(makeQueueEntry(QStringLiteral("t-1"), QStringLiteral("队列第一首"), QStringLiteral("歌手甲"), true));
    entries.append(makeQueueEntry(QStringLiteral("t-2"), QStringLiteral("Queue Two"), QString(), false));
    applyQueueSnapshot(entries, QStringLiteral("t-1"));

    clickSwitch(QStringLiteral("queueViewButton"));
    QObject *queueListView = findItem(QStringLiteral("queueListView"));
    QTRY_COMPARE(queueListView->property("count").toInt(), 2);

    QObject *emptyState = findItem(QStringLiteral("queueEmptyState"));
    QVERIFY(!qobject_cast<QQuickItem *>(emptyState)->isVisible());

    QObject *title0 = findItem(QStringLiteral("queueTitle0"));
    QVERIFY2(title0 != nullptr, "queue delegate title 0 missing");
    QCOMPARE(title0->property("text").toString(), QStringLiteral("队列第一首"));
    QObject *artist0 = findItem(QStringLiteral("queueArtist0"));
    QCOMPARE(artist0->property("text").toString(), QStringLiteral("歌手甲"));
    QCOMPARE(title0->property("color").value<QColor>(), QColor(QStringLiteral("#5B9DFF")));

    QObject *title1 = findItem(QStringLiteral("queueTitle1"));
    QVERIFY2(title1 != nullptr, "queue delegate title 1 missing");
    QCOMPARE(title1->property("text").toString(), QStringLiteral("Queue Two"));
    QVERIFY(title1->property("color").value<QColor>() != QColor(QStringLiteral("#5B9DFF")));
}

void SidebarQueueSwitchTest::queueContextMenuItemVisibleOnlyInQueueContext()
{
    QVariantList entries;
    entries.append(makeQueueEntry(QStringLiteral("t-1"), QStringLiteral("队列第一首"), QString(), false));
    applyQueueSnapshot(entries);

    clickSwitch(QStringLiteral("queueViewButton"));
    QObject *queueListView = findItem(QStringLiteral("queueListView"));
    QTRY_COMPARE(queueListView->property("count").toInt(), 1);

    // 队列条目右键 → 菜单打开，"从队列移除"项可见（queueContext 生效）
    QQuickItem *delegate = qobject_cast<QQuickItem *>(findItem(QStringLiteral("queueDelegate0")));
    QVERIFY2(delegate != nullptr, "queue delegate 0 not found");
    const QPointF delegateCenter = delegate->mapToScene(QPointF(delegate->width() / 2, delegate->height() / 2));
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, delegateCenter.toPoint());

    QObject *removeFromQueueItem = findItem(QStringLiteral("removeFromQueueItem"));
    QVERIFY2(removeFromQueueItem != nullptr, "remove from queue menu item not found");
    QTRY_VERIFY(qobject_cast<QQuickItem *>(removeFromQueueItem)->isVisible());

    QObject *contextMenu = findItem(QStringLiteral("trackContextMenu"));
    QVERIFY2(contextMenu != nullptr, "track context menu not found");
    QObject *contextMenuPopup = findItem(QStringLiteral("trackContextMenuPopup"));
    QVERIFY2(contextMenuPopup != nullptr, "track context menu popup not found");
    QVERIFY(QMetaObject::invokeMethod(contextMenuPopup, "close"));

    // 切回文件夹视图，文件夹条目右键 → 菜单中"从队列移除"隐藏
    clickSwitch(QStringLiteral("folderViewButton"));
    QCOMPARE(sidebar->property("queueViewActive").toBool(), false);
    QTRY_VERIFY(!qobject_cast<QQuickItem *>(removeFromQueueItem)->isVisible());
}

void SidebarQueueSwitchTest::applyDeepFolderTree()
{
    seriona::control::LibraryStateSnapshot library;
    seriona::scanner::PlaylistTreeSnapshot tree;
    tree.rootNodeId = std::string{"root"};

    seriona::scanner::PlaylistNode root;
    root.nodeId = std::string{"root"};
    root.kind = seriona::scanner::PlaylistNodeKind::Root;
    root.displayName = std::string{"Library"};
    root.childNodeIds = {"l1"};
    tree.nodes.push_back(std::move(root));

    auto makeFolder = [&tree](const std::string &nodeId, const std::string &parentNodeId,
                              const std::string &displayName, std::vector<std::string> children) {
        seriona::scanner::PlaylistNode node;
        node.nodeId = nodeId;
        node.parentNodeId = parentNodeId;
        node.kind = seriona::scanner::PlaylistNodeKind::Directory;
        node.displayName = displayName;
        node.childNodeIds = std::move(children);
        tree.nodes.push_back(std::move(node));
    };
    auto makeTrack = [&tree](const std::string &nodeId, const std::string &trackId,
                             const std::string &parentNodeId, const std::string &title) {
        seriona::scanner::SongMetadata song;
        song.trackId = trackId;
        song.title = title;
        seriona::scanner::PlaylistNode node;
        node.nodeId = nodeId;
        node.parentNodeId = parentNodeId;
        node.kind = seriona::scanner::PlaylistNodeKind::Track;
        node.displayName = title;
        node.song = std::move(song);
        tree.nodes.push_back(std::move(node));
    };

    makeFolder("l1", "root", "Level One", {"l2"});
    makeFolder("l2", "l1", "Level Two", {"l3"});
    makeFolder("l3", "l2", "Level Three", {"t-1", "t-2"});
    makeTrack("t-1", "t-1", "l3", "Deep Song One");
    makeTrack("t-2", "t-2", "l3", "Deep Song Two");

    library.libraryTree = std::move(tree);
    libraryController.applyLibraryStateSnapshot(library);
}

void SidebarQueueSwitchTest::applyFolderChainTree(int depth)
{
    seriona::control::LibraryStateSnapshot library;
    seriona::scanner::PlaylistTreeSnapshot tree;
    tree.rootNodeId = std::string{"root"};

    seriona::scanner::PlaylistNode root;
    root.nodeId = std::string{"root"};
    root.kind = seriona::scanner::PlaylistNodeKind::Root;
    root.displayName = std::string{"Library"};
    root.childNodeIds = {"f1"};
    tree.nodes.push_back(std::move(root));

    for (int i = 1; i <= depth; ++i) {
        const std::string nodeId = "f" + std::to_string(i);
        const std::string parentNodeId = i == 1 ? std::string{"root"} : "f" + std::to_string(i - 1);
        const std::string child = i == depth ? std::string{"t-1"} : "f" + std::to_string(i + 1);
        seriona::scanner::PlaylistNode node;
        node.nodeId = nodeId;
        node.parentNodeId = parentNodeId;
        node.kind = seriona::scanner::PlaylistNodeKind::Directory;
        node.displayName = "Level " + std::to_string(i);
        node.childNodeIds = {child};
        tree.nodes.push_back(std::move(node));
    }

    seriona::scanner::SongMetadata song;
    song.trackId = std::string{"t-1"};
    song.title = std::string{"Deep Song One"};
    seriona::scanner::PlaylistNode track;
    track.nodeId = std::string{"t-1"};
    track.parentNodeId = "f" + std::to_string(depth);
    track.kind = seriona::scanner::PlaylistNodeKind::Track;
    track.displayName = song.title;
    track.song = std::move(song);
    tree.nodes.push_back(std::move(track));

    library.libraryTree = std::move(tree);
    libraryController.applyLibraryStateSnapshot(library);
}

// ---------------------------------------------------------------------------
// Task 3 导航测试工具实现
// ---------------------------------------------------------------------------

QQuickItem *SidebarQueueSwitchTest::stackView() const
{
    return qobject_cast<QQuickItem *>(findItem(QStringLiteral("folderStack")));
}

QObject *SidebarQueueSwitchTest::pageAt(int index) const
{
    QQuickItem *page = nullptr;
    QMetaObject::invokeMethod(stackView(), "get", Q_RETURN_ARG(QQuickItem *, page), Q_ARG(int, index));
    return page;
}

QObject *SidebarQueueSwitchTest::currentPage() const
{
    return stackView()->property("currentItem").value<QObject *>();
}

QObject *SidebarQueueSwitchTest::currentListView() const
{
    QObject *page = currentPage();
    return page ? page->property("listView").value<QObject *>() : nullptr;
}

QQuickItem *SidebarQueueSwitchTest::itemAt(QQuickItem *listView, int index) const
{
    if (!listView) {
        return nullptr;
    }
    QQuickItem *item = nullptr;
    QMetaObject::invokeMethod(listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, index));
    return item;
}

void SidebarQueueSwitchTest::clickDelegate(QQuickItem *delegate)
{
    QVERIFY(delegate != nullptr);
    const QPointF center = delegate->mapToScene(QPointF(delegate->width() / 2, delegate->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
}

QQuickItem *SidebarQueueSwitchTest::backButton() const
{
    const auto items = sidebar->findChildren<QQuickItem *>();
    for (QQuickItem *item : items) {
        const QString source = item->property("iconSource").toString();
        if (source.contains(QStringLiteral("arrow_back"))) {
            return item;
        }
    }
    return nullptr;
}

int SidebarQueueSwitchTest::countFolderPageInstances() const
{
    int count = 0;
    const auto items = window->findChildren<QQuickItem *>();
    for (const QQuickItem *item : items) {
        if (QString::fromLatin1(item->metaObject()->className()).contains(QStringLiteral("FolderPage"))) {
            ++count;
        }
    }
    return count;
}

void SidebarQueueSwitchTest::assertStackAligned()
{
    QQuickItem *stack = stackView();
    QVERIFY(stack != nullptr);
    const int ctrlDepth = libraryController.folderStackDepth();
    QCOMPARE(stack->property("depth").toInt(), ctrlDepth);

    if (ctrlDepth > 0) {
        const QStringList chain = libraryController.ancestorChainForNode(libraryController.currentFolderNodeId());
        QCOMPARE(chain.size(), ctrlDepth);
        // 逐层断言：folderStack.get(i).folderNodeId === 目标链[i]（防幽灵页漏检）
        for (int i = 0; i < ctrlDepth; ++i) {
            QObject *page = pageAt(i);
            QVERIFY2(page != nullptr, qPrintable(QStringLiteral("page missing at stack index %1").arg(i)));
            QCOMPARE(page->property("folderNodeId").toString(), chain.at(i));
        }
        QObject *current = currentPage();
        QVERIFY2(current != nullptr, "currentItem must not be null at depth > 0");
        QCOMPARE(current->property("folderNodeId").toString(), libraryController.currentFolderNodeId());
    } else {
        QCOMPARE(libraryController.currentFolderNodeId(), QString());
        QVERIFY2(stack->property("currentItem").isNull(), "currentItem must be null at depth 0");
    }
}

void SidebarQueueSwitchTest::waitStackSettled()
{
    QTest::qWait(50);
    QTRY_VERIFY(!stackView()->property("busy").toBool());
}

void SidebarQueueSwitchTest::assertListViewAtTop(QObject *listView) const
{
    QVERIFY(listView != nullptr);
    const qreal topMargin = listView->property("topMargin").toReal();
    const qreal contentY = listView->property("contentY").toReal();
    QVERIFY2(qAbs(contentY + topMargin) < 0.5,
             qPrintable(QStringLiteral("list view must sit at its top margin (contentY=%1, topMargin=%2)")
                            .arg(contentY).arg(topMargin)));
}

void SidebarQueueSwitchTest::resetNavigationState()
{
    sidebar->setProperty("isSearching", false);
    sidebar->setProperty("queueViewActive", false);
    // 清空页面缓存（替换为新的空 JS 对象）
    sidebar->setProperty("folderPages", QVariant(QVariantMap{}));

    QQuickItem *stack = stackView();
    QVERIFY(stack != nullptr);
    waitStackSettled();
    if (stack->property("depth").toInt() > 0) {
        QMetaObject::invokeMethod(stack, "clear");
        waitStackSettled();
    }

    libraryController.setPlayingTrackId(QString());
    libraryController.setFollowCurrentlyPlaying(false);
    // 复位已保存的曲库根：使排序规则查询键失效（folderSortKey 空键返回空规则），
    // 缓存模型下次访问时按默认顺序重建，防止上一测试持久化的排序规则泄漏
    libraryController.setSavedRootPath(QString());

    // 空树使当前文件夹失效 → 控制器回根（经 reconcileBrowsingState）
    TreeSpec spec;
    spec.root({});
    applyTree(libraryController, std::move(spec));
    QTest::qWait(30);
}

void SidebarQueueSwitchTest::applyRichTree(int variant)
{
    // variant 0: 完整树（root → g1 → g2 → g3，每级 10 首曲目 + 根 10 首）
    // variant 1: g3 子树移除（g2 仅 tracks）
    // variant 2: g2/g3 子树移除（g1 仅 tracks）
    // variant 3: g3 增加 t-new
    TreeSpec spec;
    std::vector<std::string> rootChildren{"g1"};
    for (int i = 1; i <= 10; ++i) {
        const std::string id = "r-" + padded(i);
        rootChildren.push_back(id);
        spec.track(id, "root", "Root Track " + padded(i));
    }
    spec.root(rootChildren);

    std::vector<std::string> g1Children{"g2"};
    for (int i = 1; i <= 10; ++i) {
        const std::string id = "a-" + padded(i);
        g1Children.push_back(id);
        spec.track(id, "g1", "Title A" + padded(i));
    }
    if (variant == 2) {
        g1Children.erase(g1Children.begin()); // g2 移除
    }
    spec.folder("g1", "root", "Folder One", g1Children);

    if (variant != 2) {
        std::vector<std::string> g2Children{"g3"};
        for (int i = 1; i <= 10; ++i) {
            const std::string id = "b-" + padded(i);
            g2Children.push_back(id);
            spec.track(id, "g2", "Title B" + padded(i));
        }
        if (variant == 1) {
            g2Children.erase(g2Children.begin()); // g3 移除
        }
        spec.folder("g2", "g1", "Folder Two", g2Children);

        if (variant != 1) {
            std::vector<std::string> g3Children{"t-1"};
            for (int i = 1; i <= 10; ++i) {
                const std::string id = "c-" + padded(i);
                g3Children.push_back(id);
                spec.track(id, "g3", "Title C" + padded(i));
            }
            if (variant == 3) {
                g3Children.push_back("t-new");
                spec.track("t-new", "g3", "New Track");
            }
            spec.folder("g3", "g2", "Folder Three", g3Children);
            spec.track("t-1", "g3", "Deep Song One");
        }
    }
    applyTree(libraryController, std::move(spec));
}

void SidebarQueueSwitchTest::applyBranchTree()
{
    // 分支树（locate 变体专用）：
    // root → [f1, g1, r1]
    //   f1 → [f2, f2a, f5, t-f1]
    //     f2 → [f3, f3b, f3c]   f2a → [f3a]   f5 → [t-5]
    //       f3 → [t-1]          f3a → [t-3a]
    //       f3b → [t-3b]        f3c → [t-3c]
    //   g1 → [t-g1]
    TreeSpec spec;
    spec.root({"f1", "g1", "r1"});
    spec.track("r1", "root", "Root Track");
    spec.folder("f1", "root", "Branch One", {"f2", "f2a", "f5", "t-f1"});
    spec.track("t-f1", "f1", "Track In F1");
    spec.folder("f2", "f1", "Branch Two", {"f3", "f3b", "f3c"});
    spec.folder("f2a", "f1", "Branch Two A", {"f3a"});
    spec.folder("f5", "f1", "Branch Five", {"t-5"});
    spec.track("t-5", "f5", "Track In F5");
    spec.folder("f3", "f2", "Branch Three", {"t-1"});
    spec.track("t-1", "f3", "Deep Song One");
    spec.folder("f3b", "f2", "Branch Three B", {"t-3b"});
    spec.track("t-3b", "f3b", "Track In F3B");
    spec.folder("f3c", "f2", "Branch Three C", {"t-3c"});
    spec.track("t-3c", "f3c", "Track In F3C");
    spec.folder("f3a", "f2a", "Branch Three A", {"t-3a"});
    spec.track("t-3a", "f3a", "Track In F3A");
    spec.folder("g1", "root", "Top G", {"t-g1"});
    spec.track("t-g1", "g1", "Track In G1");
    applyTree(libraryController, std::move(spec));
}

void SidebarQueueSwitchTest::applyBranchTreeNoF2()
{
    // f2 子树（含 f3/f3b/f3c/t-1/t-3b/t-3c）移除；f1 保留
    TreeSpec spec;
    spec.root({"f1", "g1", "r1"});
    spec.track("r1", "root", "Root Track");
    spec.folder("f1", "root", "Branch One", {"f2a", "f5", "t-f1"});
    spec.track("t-f1", "f1", "Track In F1");
    spec.folder("f2a", "f1", "Branch Two A", {"f3a"});
    spec.folder("f5", "f1", "Branch Five", {"t-5"});
    spec.track("t-5", "f5", "Track In F5");
    spec.folder("f3a", "f2a", "Branch Three A", {"t-3a"});
    spec.track("t-3a", "f3a", "Track In F3A");
    spec.folder("g1", "root", "Top G", {"t-g1"});
    spec.track("t-g1", "g1", "Track In G1");
    applyTree(libraryController, std::move(spec));
}

void SidebarQueueSwitchTest::applyBranchTreeNoF1()
{
    // f1 整棵子树移除；root → [g1, r1]
    TreeSpec spec;
    spec.root({"g1", "r1"});
    spec.track("r1", "root", "Root Track");
    spec.folder("g1", "root", "Top G", {"t-g1"});
    spec.track("t-g1", "g1", "Track In G1");
    applyTree(libraryController, std::move(spec));
}

void SidebarQueueSwitchTest::applyEmptyFolderTree()
{
    TreeSpec spec;
    spec.root({"empty"});
    spec.folder("empty", "root", "Empty Folder", {});
    applyTree(libraryController, std::move(spec));
}

void SidebarQueueSwitchTest::applyDeepScrollableTree(int depth, const QString &prefix)
{
    // 25 层深链：每层 12 首曲目（可滚动），最深层无文件夹子项。
    // 文件夹节点 id 使用调用方给定前缀（默认为 f，与既有测试一致；
    // 需要精确缓存计数断言的测试传入独立前缀，避免与先前测试的缓存键冲突）
    TreeSpec spec;
    const std::string p = prefix.toStdString();
    spec.root({p + "1"});
    for (int i = 1; i <= depth; ++i) {
        std::vector<std::string> children;
        if (i < depth) {
            children.push_back(p + std::to_string(i + 1));
        }
        for (int j = 1; j <= 12; ++j) {
            const std::string id = "t-" + std::to_string(i) + "-" + padded(j);
            children.push_back(id);
            spec.track(id, p + std::to_string(i), "Level " + std::to_string(i) + " Track " + padded(j));
        }
        spec.folder(p + std::to_string(i), i == 1 ? "root" : p + std::to_string(i - 1),
                    "Level " + std::to_string(i), children);
    }
    applyTree(libraryController, std::move(spec));
}

// ---------------------------------------------------------------------------
// Task 3 导航测试实现
// ---------------------------------------------------------------------------

// 滚动保留：每层独立保留；首次进入从 0 开始；重入保留自身上次位置；根视图回归
void SidebarQueueSwitchTest::scrollRetentionAcrossFolderNavigation()
{
    resetNavigationState();
    applyRichTree();

    QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(rootView != nullptr);
    QTRY_VERIFY(rootView->property("count").toInt() >= 11);

    rootView->setProperty("contentY", 150.0);
    QTest::qWait(40);
    const qreal rootY = rootView->property("contentY").toReal();
    QVERIFY2(rootY > 0.0, "root view must be scrollable in this tree");

    // 首次进入：contentY 从顶部边距开始（ListView 首项位于 topMargin 之下时
    // contentY 被钳到 -topMargin，即"顶部"）
    libraryController.enterFolder(QStringLiteral("g1"));
    assertStackAligned();
    QTRY_VERIFY(currentListView()->property("count").toInt() >= 1);
    assertListViewAtTop(currentListView());
    currentListView()->setProperty("contentY", 120.0);
    QTest::qWait(40);
    const qreal l1Y = currentListView()->property("contentY").toReal();
    QVERIFY2(l1Y > 0.0, "g1 page must be scrollable");

    libraryController.enterFolder(QStringLiteral("g2"));
    assertStackAligned();
    QTRY_VERIFY(currentListView()->property("count").toInt() >= 1);
    assertListViewAtTop(currentListView());
    currentListView()->setProperty("contentY", 90.0);
    QTest::qWait(40);
    const qreal l2Y = currentListView()->property("contentY").toReal();
    QVERIFY2(l2Y > 0.0, "g2 page must be scrollable");

    libraryController.enterFolder(QStringLiteral("g3"));
    assertStackAligned();
    QTRY_VERIFY(currentListView()->property("count").toInt() >= 1);
    assertListViewAtTop(currentListView());
    currentListView()->setProperty("contentY", 60.0);
    QTest::qWait(40);
    const qreal l3Y = currentListView()->property("contentY").toReal();
    QVERIFY2(l3Y > 0.0, "g3 page must be scrollable");

    // 逐级返回：每层位置独立保留（每次 back 前等过渡结束，避免 busy 拒绝）
    waitStackSettled();
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 2);
    assertStackAligned();
    QCOMPARE(currentListView()->property("contentY").toReal(), l2Y);

    waitStackSettled();
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
    assertStackAligned();
    QCOMPARE(currentListView()->property("contentY").toReal(), l1Y);

    waitStackSettled();
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    assertStackAligned();
    QCOMPARE(rootView->property("contentY").toReal(), rootY); // 根视图回归

    // 重入已访问目录：保留自身上次 contentY（Scope 重入决策锁定）
    waitStackSettled();
    libraryController.enterFolder(QStringLiteral("g1"));
    assertStackAligned();
    QCOMPARE(currentListView()->property("contentY").toReal(), l1Y);
    libraryController.enterFolder(QStringLiteral("g2"));
    assertStackAligned();
    QCOMPARE(currentListView()->property("contentY").toReal(), l2Y);
}

// 根视图滚动回归：进入/返回后 contentY 不变
void SidebarQueueSwitchTest::rootScrollRetentionRegression()
{
    resetNavigationState();
    applyRichTree();

    QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(rootView != nullptr);
    QTRY_VERIFY(rootView->property("count").toInt() >= 11);

    rootView->setProperty("contentY", 150.0);
    QTest::qWait(40);
    const qreal rootY = rootView->property("contentY").toReal();
    QVERIFY2(rootY > 0.0, "root view must be scrollable in this tree");

    libraryController.enterFolder(QStringLiteral("g1"));
    assertStackAligned();
    QTRY_VERIFY(!stackView()->property("busy").toBool());
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    QVERIFY(stackView()->property("currentItem").isNull());
    QCOMPARE(rootView->property("contentY").toReal(), rootY);
}

// 空目录进入 + 根目录 back no-op（depth 不变）
void SidebarQueueSwitchTest::emptyFolderAndRootBackNoOp()
{
    resetNavigationState();
    applyEmptyFolderTree();

    libraryController.enterFolder(QStringLiteral("empty"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 1);
    QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("empty"));
    QCOMPARE(currentListView()->property("count").toInt(), 0);

    QTRY_VERIFY(!stackView()->property("busy").toBool());
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    QVERIFY(stackView()->property("currentItem").isNull());
    QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(playlistView->isVisible());

    // 根目录按 back：no-op（depth 不变、currentItem 仍为 null）
    QTRY_VERIFY(!stackView()->property("busy").toBool());
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QCOMPARE(stackView()->property("depth").toInt(), 0);
    QVERIFY(stackView()->property("currentItem").isNull());
    QVERIFY(playlistView->isVisible());
}

// back 到根显式断言（clear() 机制锁定）：depth 0、currentItem null、根视图可见
void SidebarQueueSwitchTest::backToRootClearsStack()
{
    resetNavigationState();
    applyDeepFolderTree();

    libraryController.enterFolder(QStringLiteral("l1"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 1);

    QTRY_VERIFY(!stackView()->property("busy").toBool());
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    QVERIFY(stackView()->property("currentItem").isNull());
    QCOMPARE(libraryController.currentFolderNodeId(), QString());
    QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(playlistView->isVisible());
}

// 根视图重入错落动画（正向）+ 反向断言（locate/rescan/队列/搜索不触发）
void SidebarQueueSwitchTest::rootSlideInAnimationOnBack()
{
    // ---- 正向：back 到根 → 根视图视口可见 delegate 触发 startNavSlideIn ----
    {
        resetNavigationState();
        applyRichTree();
        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);
        QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

        libraryController.enterFolder(QStringLiteral("g1"));
        assertStackAligned();

        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
        // 动画运行（navTranslate.x 出现非 0 采样）且终值归 0
        QVERIFY(xMoved(rootView, 0) || xMoved(rootView, 1));
        QTRY_COMPARE_WITH_TIMEOUT(translateX(rootView, 0), 0.0, 8000);
        QTRY_COMPARE_WITH_TIMEOUT(translateX(rootView, 1), 0.0, 8000);
    }

    // ---- 反向 1：locate 收敛回根（视觉需求 5）不触发根动画 ----
    {
        resetNavigationState();
        applyBranchTree();
        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);
        QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

        libraryController.enterFolder(QStringLiteral("f1"));
        libraryController.enterFolder(QStringLiteral("f2"));
        libraryController.enterFolder(QStringLiteral("f3"));
        assertStackAligned();

        libraryController.setPlayingTrackId(QStringLiteral("r1")); // 根级曲目
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 0);
        QVERIFY(stackView()->property("currentItem").isNull());
        QCOMPARE(libraryController.currentFolderNodeId(), QString());
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        settleZeroBaseline(nullptr, rootView, 0); // 先等收敛尾巴归零再开负向窗
        QVERIFY(!xMoved(rootView, 0)); // 无动画记录
        QCOMPARE(translateX(rootView, 0), 0.0);
    }

    // ---- 反向 2：rescan 回根不触发根动画 ----
    {
        resetNavigationState();
        applyRichTree();
        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);
        QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

        libraryController.enterFolder(QStringLiteral("g1"));
        libraryController.enterFolder(QStringLiteral("g2"));
        libraryController.enterFolder(QStringLiteral("g3"));
        assertStackAligned();

        applyRichTree(1); // g3 移除 → 控制器回根
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        settleZeroBaseline(nullptr, rootView, 0); // rescan 收敛尾巴归零后再判"未播"
        QVERIFY(!xMoved(rootView, 0));
        // 重建后的根 delegate 亦无动画状态
        QTRY_VERIFY(rootView->property("count").toInt() > 0);
        QCOMPARE(translateX(rootView, 0), 0.0);
    }

    // ---- 反向 3：队列视图切换（depth 0）不触发根动画 ----
    {
        resetNavigationState();
        applyRichTree();
        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);
        QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

        clickSwitch(QStringLiteral("queueViewButton"));
        QCOMPARE(sidebar->property("queueViewActive").toBool(), true);
        QTest::qWait(300);
        clickSwitch(QStringLiteral("folderViewButton"));
        QCOMPARE(sidebar->property("queueViewActive").toBool(), false);
        QTest::qWait(300);
        settleZeroBaseline(nullptr, rootView, 0); // 视图重显后 delegate 重建，等归零基线
        QVERIFY(!xMoved(rootView, 0));
    }

    // ---- 反向 4：搜索退出（depth 0）不触发根动画 ----
    {
        resetNavigationState();
        applyRichTree();
        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);
        QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

        sidebar->setProperty("isSearching", true);
        QTest::qWait(300);
        sidebar->setProperty("isSearching", false);
        QTest::qWait(300);
        settleZeroBaseline(nullptr, rootView, 0); // 视图重显后 delegate 重建，等归零基线
        QVERIFY(!xMoved(rootView, 0));
    }
}

// follow-playing 收敛（深目录/根目录）零动画：无激活动画、无 StackView 过渡、
// suppressNavAnimation 复位 false（:148 行为保真）
void SidebarQueueSwitchTest::followPlayingZeroAnimation()
{
    resetNavigationState();
    applyRichTree();

    libraryController.enterFolder(QStringLiteral("g1"));
    libraryController.enterFolder(QStringLiteral("g2"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 2);

    // 既有页 delegate 动画观察（收敛期间不应触发）
    QObject *g1Page = pageAt(0);
    QVERIFY(g1Page != nullptr);
    QQuickItem *g1Lv = qobject_cast<QQuickItem *>(g1Page->property("listView").value<QObject *>());

    // follow 开启后 setPlayingTrackId 驱动收敛到深目录（g3）
    libraryController.setFollowCurrentlyPlaying(true);
    libraryController.setPlayingTrackId(QStringLiteral("t-1"));
    QCOMPARE(stackView()->property("depth").toInt(), 3);
    assertStackAligned();
    QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("g3"));
    QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    settleZeroBaseline(stackView(), g1Lv, 0); // g1 页上次滑入的尾巴归零后再判"未播"
    QVERIFY(!xMoved(g1Lv, 0)); // 无激活动画
    QVERIFY(!busyFlipped(stackView())); // 无 StackView 过渡
    QCOMPARE(stackView()->property("busy").toBool(), false);

    // 收敛到根目录（根级曲目）仍零动画
    QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(rootView != nullptr);

    libraryController.setPlayingTrackId(QStringLiteral("r-01"));
    QCOMPARE(stackView()->property("depth").toInt(), 0);
    QVERIFY(stackView()->property("currentItem").isNull());
    QCOMPARE(libraryController.currentFolderNodeId(), QString());
    QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    settleZeroBaseline(stackView(), rootView, 0); // 收敛到根的尾巴归零后再判"未播"
    QVERIFY(!xMoved(rootView, 0));
    QVERIFY(!busyFlipped(stackView()));

    libraryController.setFollowCurrentlyPlaying(false);
}

// 重复进入同一目录：复用实例（folderPages 命中）、无新实例创建；pop/clear 不销毁
void SidebarQueueSwitchTest::repeatEnterReusesPageInstance()
{
    resetNavigationState();
    applyRichTree();

    QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(rootView != nullptr);
    QTRY_VERIFY(itemAt(rootView, 0) != nullptr);
    QCOMPARE(itemAt(rootView, 0)->property("nodeId").toString(), QStringLiteral("g1"));

    const int instancesBefore = countFolderPageInstances();

    // 点击根视图 g1 行（配对路径：push 先于 enterFolder）
    clickDelegate(itemAt(rootView, 0));
    QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
    waitStackSettled();
    assertStackAligned();
    QObject *firstPage = currentPage();
    QVERIFY(firstPage != nullptr);
    QCOMPARE(firstPage->property("folderNodeId").toString(), QStringLiteral("g1"));
    QCOMPARE(countFolderPageInstances(), instancesBefore + 1);

    // back 到根（clear 不销毁页面实例，spike 断言）
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    waitStackSettled();
    // 等根视图重入错落动画结束（navTranslate 归 0 前 delegate 场景坐标偏移，点击会落空）
    QTRY_COMPARE_WITH_TIMEOUT(translateX(rootView, 0), 0.0, 8000);
    QCOMPARE(countFolderPageInstances(), instancesBefore + 1);

    // 重入：同一实例（缓存命中、无新实例创建）
    QTRY_VERIFY(itemAt(rootView, 0) != nullptr);
    clickDelegate(itemAt(rootView, 0));
    QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
    waitStackSettled();
    assertStackAligned();
    QObject *secondPage = currentPage();
    QVERIFY(secondPage != nullptr);
    QCOMPARE(secondPage, firstPage); // 同一实例
    QCOMPARE(countFolderPageInstances(), instancesBefore + 1);

    const QVariantMap pages = sidebar->property("folderPages").toMap();
    QVERIFY(pages.contains(QStringLiteral("g1")));
    QObject *cached = pages.value(QStringLiteral("g1")).value<QObject *>();
    QCOMPARE(cached, firstPage);
}

// 25 层深链往返：每层位置保留 + 缓存大小 == 去重目录数 + 1（含根键）。
// 使用独立前缀 d 的节点 id（避免 f1..f3 已被 rootSlideInAnimationOnBack 的
// 分支树缓存，使投影缓存计数断言精确）
void SidebarQueueSwitchTest::deepChainNavigationAndCacheSize()
{
    resetNavigationState();
    applyDeepScrollableTree(25, QStringLiteral("d"));
    // 投影缓存跨测试函数保留（Qt Test 单实例共享成员），断言相对基线
    const int cacheBaseline = libraryController.projectionCacheSize();

    QVector<qreal> recorded(26, 0.0);
    for (int i = 1; i <= 25; ++i) {
        libraryController.enterFolder(QStringLiteral("d%1").arg(i));
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), i);
        QObject *lv = currentListView();
        QVERIFY(lv != nullptr);
        lv->setProperty("contentY", 100.0);
        QTest::qWait(30);
        recorded[i] = lv->property("contentY").toReal();
        QVERIFY2(recorded[i] > 0.0, qPrintable(QStringLiteral("level %1 must be scrollable").arg(i)));
    }
    // 最深层位置在返回前亦保留
    QCOMPARE(currentListView()->property("contentY").toReal(), recorded[25]);

    // 缓存大小：25 层新增（性能断言；根键与残留键已在基线中）
    QCOMPARE(libraryController.projectionCacheSize(), cacheBaseline + 25);
    QCOMPARE(sidebar->property("folderPages").toMap().size(), 25);

    // 逐级返回：每层位置独立保留
    for (int i = 24; i >= 1; --i) {
        libraryController.goBack();
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), i);
        QCOMPARE(currentListView()->property("contentY").toReal(), recorded[i]);
    }
    libraryController.goBack();
    QCOMPARE(stackView()->property("depth").toInt(), 0);
    assertStackAligned();
    QCOMPARE(libraryController.currentFolderNodeId(), QString());
}

// 搜索激活时导航不破坏搜索状态
void SidebarQueueSwitchTest::searchActivePreservesNavigationState()
{
    resetNavigationState();
    applyBranchTree();

    libraryController.enterFolder(QStringLiteral("f1"));
    libraryController.enterFolder(QStringLiteral("f2"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 2);

    sidebar->setProperty("isSearching", true);
    QTest::qWait(60);
    QVERIFY(!stackView()->isVisible());
    QQuickItem *searchView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("searchView")));
    QVERIFY(searchView->isVisible());

    // 搜索激活期间导航：搜索状态保持
    libraryController.enterFolder(QStringLiteral("f3"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 3);
    QCOMPARE(sidebar->property("isSearching").toBool(), true);

    sidebar->setProperty("isSearching", false);
    QTest::qWait(60);
    QVERIFY(stackView()->isVisible());
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 3);
    QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f3"));
}

// 搜索激活时点击"定位当前播放歌曲"（FAB）：自动退出搜索并定位到播放曲目。
// 回归锁：定位的滚动请求按 activeListView 处理，搜索模式（isSearching）会把
// 请求吞进 searchView，且深层曲目不在搜索投影内导致定位失效。
void SidebarQueueSwitchTest::locateWhileSearchingExitsSearchAndLocates()
{
    resetNavigationState();
    applyRichTree();

    libraryController.enterFolder(QStringLiteral("g1"));
    libraryController.enterFolder(QStringLiteral("g2"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 2);

    sidebar->setProperty("isSearching", true);
    libraryController.setSearchQuery(QStringLiteral("zzz-nonexistent"));
    QTest::qWait(60);
    QCOMPARE(sidebar->property("isSearching").toBool(), true);
    QQuickItem *searchView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("searchView")));
    QVERIFY2(searchView != nullptr, "search view not found");
    QVERIFY(searchView->isVisible());
    QVERIFY(!stackView()->isVisible());

    libraryController.setPlayingTrackId(QStringLiteral("t-1"));
    QQuickItem *locateFab = qobject_cast<QQuickItem *>(findItem(QStringLiteral("locateFab")));
    QVERIFY2(locateFab != nullptr, "locate FAB not found");
    const QPointF center = locateFab->mapToScene(QPointF(locateFab->width() / 2, locateFab->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());

    QCOMPARE(sidebar->property("isSearching").toBool(), false);
    QCOMPARE(libraryController.searchQuery(), QString());
    QVERIFY(!searchView->isVisible());

    QTRY_COMPARE(stackView()->property("depth").toInt(), 3);
    QCOMPARE(libraryController.currentFolderNodeId(), QStringLiteral("g3"));
    QCOMPARE(libraryController.scrollRequest(), QStringLiteral("t-1"));
    QVERIFY(stackView()->isVisible());
    QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);

    libraryController.clearSearch();
}

// 快速连点 back 两次：最终状态一致、无半程动画残留
void SidebarQueueSwitchTest::rapidBackClicksStateConsistency()
{
    resetNavigationState();
    applyBranchTree();

    libraryController.enterFolder(QStringLiteral("f1"));
    libraryController.enterFolder(QStringLiteral("f2"));
    libraryController.enterFolder(QStringLiteral("f3"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 3);

    // 第一次 back：配对 pop（过渡进行中 busy=true）+ goBack
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QVERIFY(stackView()->property("busy").toBool());
    // 第二次 back：busy 检查拒绝（不排队）
    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_VERIFY(!stackView()->property("busy").toBool());
    QTRY_COMPARE(stackView()->property("depth").toInt(), 2);
    assertStackAligned();
    QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f2"));

    // 无半程动画残留：delegate 终态 opacity 1.0 / translate x 0
    QQuickItem *f2Lv = qobject_cast<QQuickItem *>(currentListView());
    QVERIFY(f2Lv != nullptr);
    QTRY_VERIFY(itemAt(f2Lv, 0) != nullptr);
    QTRY_COMPARE(itemAt(f2Lv, 0)->property("opacity").toReal(), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(translateX(f2Lv, 0), 0.0, 8000);

    // 稳定一致（无第二次 pop 残留）
    QTest::qWait(1000);
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 2);
}

// controller↔栈同步：每次导航后深度相等 + 逐层链校验 + 返回键 enabled + 排序恢复
void SidebarQueueSwitchTest::controllerStackSynchronization()
{
    resetNavigationState();
    applyRichTree();

    QQuickItem *backBtn = backButton();
    QVERIFY2(backBtn != nullptr, "back button not found");
    QVERIFY(!backBtn->isEnabled()); // 根：禁用

    libraryController.enterFolder(QStringLiteral("g1"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 1);
    QVERIFY(backBtn->isEnabled()); // level-1：启用

    libraryController.enterFolder(QStringLiteral("g2"));
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 2);

    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
    waitStackSettled();
    assertStackAligned();

    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    waitStackSettled();
    assertStackAligned();
    QVERIFY(!backBtn->isEnabled());

    // 重进缓存目录恢复其排序规则
    libraryController.enterFolder(QStringLiteral("g1"));
    assertStackAligned();

    QVariantList rules;
    QVariantMap rule;
    rule.insert(QStringLiteral("field"), QStringLiteral("title"));
    rule.insert(QStringLiteral("order"), QStringLiteral("desc"));
    rules.append(rule);
    // 排序持久化需要已保存的曲库根（真实应用在扫描配置时设置；测试宿主未扫描过）
    libraryController.setSavedRootPath(QStringLiteral("/test-music"));
    libraryController.applySortRules(rules);
    // 持久化命令经后端桥发送，测试宿主桥未启动会被拒绝；用 applyFolderSortSetting
    // 模拟后端回执，把规则记入 m_savedFolderSortRules，重进目录时才能恢复
    seriona::control::FolderSortSetting persisted;
    persisted.rootPath = std::filesystem::path("/test-music");
    persisted.folderNodeId = "g1";
    seriona::control::FolderSortRule persistedRule;
    persistedRule.field = seriona::control::FolderSortField::Title;
    persistedRule.direction = seriona::control::FolderSortDirection::Descending;
    persisted.rules = {persistedRule};
    libraryController.applyFolderSortSetting(persisted);

    auto *g1Model = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        currentPage()->property("projectionModel").value<QObject *>());
    QVERIFY(g1Model != nullptr);
    QVERIFY(g1Model->rowCount() > 0);
    QCOMPARE(g1Model->entryAt(0)->nodeId, QStringLiteral("a-10")); // 标题降序 → a-10 在前

    QMetaObject::invokeMethod(sidebar, "handleBackClicked");
    QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
    waitStackSettled();
    assertStackAligned();

    libraryController.enterFolder(QStringLiteral("g1"));
    assertStackAligned();
    auto *reentered = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        currentPage()->property("projectionModel").value<QObject *>());
    QCOMPARE(reentered, g1Model); // 同一模型实例
    QCOMPARE(reentered->entryAt(0)->nodeId, QStringLiteral("a-10")); // 排序规则恢复
}

// locate 变体：冷缓存 / 跨分支 / 反向分歧 / 同级兄弟 / 等深跨分支 / 祖先
void SidebarQueueSwitchTest::locateColdCacheAndBranchVariants()
{
    // ① 冷缓存：全新库无先导导航，从根 locate 3 层深链曲目
    {
        resetNavigationState();
        applyBranchTree();
        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);
        QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

        libraryController.setPlayingTrackId(QStringLiteral("t-1"));
        libraryController.locateCurrentSong();

        QCOMPARE(stackView()->property("depth").toInt(), 3);
        assertStackAligned(); // 逐层链校验
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f3"));
        // 页面被创建并入栈（folderPages 命中新增）
        const QVariantMap pages = sidebar->property("folderPages").toMap();
        QCOMPARE(pages.size(), 3);
        QVERIFY(pages.contains(QStringLiteral("f1")));
        QVERIFY(pages.contains(QStringLiteral("f2")));
        QVERIFY(pages.contains(QStringLiteral("f3")));
        // 同步期间无激活动画且无 StackView 过渡
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        settleZeroBaseline(stackView(), rootView, 0); // 冷缓存收敛尾巴归零后再判"未播"
        QVERIFY(!busyFlipped(stackView()));
        QVERIFY(!xMoved(rootView, 0));

        // 定位完成后 back 逐级返回原位置
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QTRY_COMPARE(stackView()->property("depth").toInt(), 2);
        waitStackSettled();
        assertStackAligned();
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f2"));
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
        waitStackSettled();
        assertStackAligned();
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
        assertStackAligned();
    }

    // ② 跨分支：先浏览 [f1,f5]，locate f3 链 → [f1,f2,f3]（f5 不残留）
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        libraryController.enterFolder(QStringLiteral("f5"));
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 2);

        libraryController.setPlayingTrackId(QStringLiteral("t-1"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 3);
        assertStackAligned(); // 逐层 [f1,f2,f3]
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f3"));
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    }

    // ③ 反向分歧：从 [f1,f2,f3] locate 顶层 g1 → [g1]（无 f1 残留）；
    //    再回 [f1,f2,f3] locate 中间分支 f5 → [f1,f5]
    {
        libraryController.setPlayingTrackId(QStringLiteral("t-g1"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 1);
        assertStackAligned(); // 逐层 [g1]
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("g1"));
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);

        libraryController.enterFolder(QStringLiteral("f1"));
        libraryController.enterFolder(QStringLiteral("f2"));
        libraryController.enterFolder(QStringLiteral("f3"));
        assertStackAligned();

        libraryController.setPlayingTrackId(QStringLiteral("t-5"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 2);
        assertStackAligned(); // 逐层 [f1,f5]
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f5"));
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    }

    // ④ 同级兄弟：从 [f1,f2,f3] locate f3c 曲目（同父 f2）→ [f1,f2,f3c]
    {
        libraryController.enterFolder(QStringLiteral("f2"));
        libraryController.enterFolder(QStringLiteral("f3"));
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 3);

        libraryController.setPlayingTrackId(QStringLiteral("t-3c"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 3);
        assertStackAligned(); // 逐层 [f1,f2,f3c]
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f3c"));
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    }

    // ⑤ 等深跨分支：从 [f1,f2a,f3a] locate f3b 曲目 → [f1,f2,f3b]（f2a 不残留）
    {
        libraryController.enterFolder(QStringLiteral("f2a"));
        libraryController.enterFolder(QStringLiteral("f3a"));
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 3);

        libraryController.setPlayingTrackId(QStringLiteral("t-3b"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 3);
        assertStackAligned(); // 逐层 [f1,f2,f3b]（f2a/f3a 不在栈中）
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f3b"));
        QCOMPARE(pageAt(1)->property("folderNodeId").toString(), QStringLiteral("f2"));
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    }

    // ⑥ 祖先：深处 [f1,f2,f3b] locate 浅层 f1 目录内曲目 → [f1]
    {
        libraryController.setPlayingTrackId(QStringLiteral("t-f1"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 1);
        assertStackAligned(); // 逐层 [f1]
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f1"));
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    }
}

// no-op locate 抑制复位（三变体）：每次后动画已恢复、suppressNavAnimation 复位 false
void SidebarQueueSwitchTest::noOpLocateSuppressionReset()
{
    // 变体 A：已在当前文件夹内 locate 当前曲目（folderChanged=false 零深度信号）
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        libraryController.enterFolder(QStringLiteral("f2"));
        libraryController.enterFolder(QStringLiteral("f3"));
        assertStackAligned();

        libraryController.setPlayingTrackId(QStringLiteral("t-1"));
        libraryController.locateCurrentSong();
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        // callLater 复位窗口：慢机上事件可能延迟，改为轮询等待"保持复位"
        QTRY_COMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 3);

        // 动画已恢复：返回时 f2 页 delegate 错落滑入（isActive 动画触发）
        QObject *f2Page = pageAt(1);
        QVERIFY(f2Page != nullptr);
        QQuickItem *f2Lv = qobject_cast<QQuickItem *>(f2Page->property("listView").value<QObject *>());
        QTRY_VERIFY(itemAt(f2Lv, 0) != nullptr);
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QVERIFY(xMoved(f2Lv, 0, 2000)); // back 触发错落滑入
        QTRY_COMPARE_WITH_TIMEOUT(translateX(f2Lv, 0), 0.0, 8000);
        assertStackAligned();
    }

    // 变体 B：无播放曲目 locate（early return 零信号）
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        libraryController.enterFolder(QStringLiteral("f2"));
        libraryController.enterFolder(QStringLiteral("f3"));
        assertStackAligned();

        libraryController.setPlayingTrackId(QString());
        libraryController.locateCurrentSong();
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 3);

        QObject *f2Page = pageAt(1);
        QVERIFY(f2Page != nullptr);
        QQuickItem *f2Lv = qobject_cast<QQuickItem *>(f2Page->property("listView").value<QObject *>());
        QTRY_VERIFY(itemAt(f2Lv, 0) != nullptr);
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QVERIFY(xMoved(f2Lv, 0, 2000)); // 动画已恢复 → back 触发错落滑入
        QTRY_COMPARE_WITH_TIMEOUT(translateX(f2Lv, 0), 0.0, 8000);
        assertStackAligned();
    }

    // 变体 C：深处 locate 根级曲目（栈坍缩 depth 0，动画已恢复后 back 根滑入）
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        libraryController.enterFolder(QStringLiteral("f2"));
        libraryController.enterFolder(QStringLiteral("f3"));
        assertStackAligned();

        QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(rootView != nullptr);

        libraryController.setPlayingTrackId(QStringLiteral("r1"));
        libraryController.locateCurrentSong();
        QCOMPARE(stackView()->property("depth").toInt(), 0);
        QVERIFY(stackView()->property("currentItem").isNull());
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
        settleZeroBaseline(nullptr, rootView, 0); // 坍缩尾巴归零后再判"未播"
        QVERIFY(!xMoved(rootView, 0)); // 收敛回根本身不播根动画（静止基线后窗口内保持 0）

        // 动画已恢复：进入 g1 后 back 到根 → 根错落滑入
        libraryController.enterFolder(QStringLiteral("g1"));
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 1);
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QVERIFY(xMoved(rootView, 0, 2000)); // back 触发根错落滑入
        QTRY_COMPARE_WITH_TIMEOUT(translateX(rootView, 0), 0.0, 8000);
        QCOMPARE(stackView()->property("depth").toInt(), 0);
    }
}

// rescan：模型身份不变 + 新树内容；重扫移除当前文件夹 → 栈坍缩；busy 过渡中重扫收敛
void SidebarQueueSwitchTest::rescanRemovesCurrentFolderFallback()
{
    // Part A：深链中重扫 → 重进缓存目录可用（模型身份不变、内容为新树）
    {
        resetNavigationState();
        applyRichTree();
        libraryController.enterFolder(QStringLiteral("g1"));
        libraryController.enterFolder(QStringLiteral("g2"));
        libraryController.enterFolder(QStringLiteral("g3"));
        assertStackAligned();

        QObject *g3Page = currentPage();
        auto *g3Model = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
            g3Page->property("projectionModel").value<QObject *>());
        QVERIFY(g3Model != nullptr);
        const int g3RowsBefore = g3Model->rowCount();
        QObject *g2Page = pageAt(1);
        auto *g2Model = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
            g2Page->property("projectionModel").value<QObject *>());
        QVERIFY(g2Model != nullptr);

        applyRichTree(3); // g3 增加 t-new
        QCOMPARE(currentPage()->property("projectionModel").value<QObject *>(), g3Model);
        QCOMPARE(pageAt(1)->property("projectionModel").value<QObject *>(), g2Model);
        QCOMPARE(g3Model->rowCount(), g3RowsBefore + 1);
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 3);

        // 返回后 g2 页面仍可用（模型身份不变）
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QTRY_COMPARE(stackView()->property("depth").toInt(), 2);
        QCOMPARE(currentPage()->property("projectionModel").value<QObject *>(), g2Model);
        assertStackAligned();
    }

    // Part B：重扫移除当前文件夹 → 回退根：栈坍缩 depth 0、back 禁用、根视图可见
    {
        resetNavigationState();
        applyRichTree();
        libraryController.enterFolder(QStringLiteral("g1"));
        libraryController.enterFolder(QStringLiteral("g2"));
        libraryController.enterFolder(QStringLiteral("g3"));
        assertStackAligned();

        applyRichTree(1); // g3 移除
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
        QVERIFY(stackView()->property("currentItem").isNull());
        QCOMPARE(libraryController.currentFolderNodeId(), QString());
        QVERIFY(!libraryController.canGoBack());
        QQuickItem *backBtn = backButton();
        QVERIFY(backBtn != nullptr);
        QVERIFY(!backBtn->isEnabled());
        QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(playlistView->isVisible());
        QCOMPARE(sidebar->property("suppressNavAnimation").toBool(), false);
    }

    // Part C：busy 过渡中收到重扫信号 → busyChanged 后最终收敛 depth 0
    {
        resetNavigationState();
        applyRichTree();
        libraryController.enterFolder(QStringLiteral("g1"));
        assertStackAligned();

        // 配对点击 g1 页内 g2 行 → push 过渡进行中（busy）
        QObject *g1Page = currentPage();
        QVERIFY(g1Page != nullptr);
        QQuickItem *g1Lv = qobject_cast<QQuickItem *>(g1Page->property("listView").value<QObject *>());
        QVERIFY(g1Lv != nullptr);
        QTRY_VERIFY(itemAt(g1Lv, 0) != nullptr);
        QCOMPARE(itemAt(g1Lv, 0)->property("nodeId").toString(), QStringLiteral("g2"));
        // 等条目静止在最终位置（错落滑入结束）再点击，避免点击落在半程位移上；点击后
        // busy 在下一帧才置位 → 轮询等待其进入过渡
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(translateX(g1Lv, 0)) < 1e-6 && qAbs(itemAt(g1Lv, 0)->property("opacity").toReal() - 1.0) < 1e-6,
            8000);
        clickDelegate(itemAt(g1Lv, 0));
        QTRY_VERIFY(stackView()->property("busy").toBool());

        // 过渡中收到重扫（g2 移除）→ 控制器回根；back 点击（canGoBack 已 false）被忽略
        applyRichTree(2);
        QMetaObject::invokeMethod(sidebar, "handleBackClicked");
        QTRY_VERIFY(!stackView()->property("busy").toBool());
        // busyChanged 后收敛：栈坍缩 depth 0，绝不重推页面
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
        QVERIFY(stackView()->property("currentItem").isNull());
        QCOMPARE(libraryController.currentFolderNodeId(), QString());
        QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(playlistView->isVisible());
        QTest::qWait(1000);
        QCOMPARE(stackView()->property("depth").toInt(), 0);
    }
}

// 配对 no-op 自愈：陈旧行 enterFolder 零信号 → pop 回；根陈旧行 → clear；
// back 与重扫竞态 → 栈坍缩绝不重推
void SidebarQueueSwitchTest::pairingNoOpSelfHealing()
{
    // 变体 1：rescan 移除 f2 后点击其陈旧行 → push 的页被 pop 回（栈深不变）
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        assertStackAligned();
        QCOMPARE(stackView()->property("depth").toInt(), 1);

        applyBranchTreeNoF2(); // f2 移除（f1 保留）
        QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
        assertStackAligned();

        QMetaObject::invokeMethod(sidebar, "activateNode", Q_ARG(QVariant, QVariant(QStringLiteral("f2"))), Q_ARG(QVariant, QVariant(true)));
        QCOMPARE(stackView()->property("depth").toInt(), 1); // 刚 push 的页被 pop 回
        assertStackAligned();
        QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f1"));
        QCOMPARE(libraryController.currentFolderNodeId(), QStringLiteral("f1"));
        // 页面保留在缓存，后续重进可复用
        QVERIFY(sidebar->property("folderPages").toMap().contains(QStringLiteral("f2")));
    }

    // 变体 2：从根点击陈旧行 → push 后 depth 1 → 自愈 clear() → depth 0
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        assertStackAligned();

        applyBranchTreeNoF1(); // f1 整棵移除 → 控制器回根、栈坍缩
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0);
        QVERIFY(stackView()->property("currentItem").isNull());

        QMetaObject::invokeMethod(sidebar, "activateNode", Q_ARG(QVariant, QVariant(QStringLiteral("f1"))), Q_ARG(QVariant, QVariant(true)));
        // 自愈：栈 [] → push 后 depth 1 → clear() → depth 0（锁 pop 在 depth 1 的 no-op 陷阱）
        QCOMPARE(stackView()->property("depth").toInt(), 0);
        QVERIFY(stackView()->property("currentItem").isNull());
        QCOMPARE(libraryController.currentFolderNodeId(), QString());
        QVERIFY(!libraryController.canGoBack());
        QVERIFY(sidebar->property("folderPages").toMap().contains(QStringLiteral("f1")));
    }

    // 变体 3：back 点击与重扫竞态（goBack 零信号——控制器已回根）→ 栈坍缩绝不重推
    {
        resetNavigationState();
        applyBranchTree();
        libraryController.enterFolder(QStringLiteral("f1"));
        assertStackAligned();

        QObject *f1Page = currentPage();
        QVERIFY(f1Page != nullptr);
        QQuickItem *f1Lv = qobject_cast<QQuickItem *>(f1Page->property("listView").value<QObject *>());
        QVERIFY(f1Lv != nullptr);
        QTRY_VERIFY(itemAt(f1Lv, 0) != nullptr);
        QCOMPARE(itemAt(f1Lv, 0)->property("nodeId").toString(), QStringLiteral("f2"));
        // 条目静止后点击；busy 下一帧才置位 → 轮询等待进入过渡
        QTRY_VERIFY_WITH_TIMEOUT(
            qAbs(translateX(f1Lv, 0)) < 1e-6 && qAbs(itemAt(f1Lv, 0)->property("opacity").toReal() - 1.0) < 1e-6,
            8000);
        clickDelegate(itemAt(f1Lv, 0)); // 配对 push → 过渡进行中（busy）
        QTRY_VERIFY(stackView()->property("busy").toBool());

        applyBranchTreeNoF2(); // 重扫移除 f2 → 控制器已回根（goBack 零信号场景）
        QMetaObject::invokeMethod(sidebar, "handleBackClicked"); // 被忽略
        QTRY_VERIFY(!stackView()->property("busy").toBool());
        QTRY_COMPARE(stackView()->property("depth").toInt(), 0); // 残余栈被清空
        QVERIFY(stackView()->property("currentItem").isNull());
        QCOMPARE(libraryController.currentFolderNodeId(), QString());
        QQuickItem *playlistView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
        QVERIFY(playlistView->isVisible());
        QTest::qWait(1000);
        QCOMPARE(stackView()->property("depth").toInt(), 0); // 绝不重推页面
    }
}

// 深层排序：25 层中 applySortRules → 当前层原地重建且顺序更新、其他缓存层不受影响、
// 栈无变化
void SidebarQueueSwitchTest::deepChainSortRuleRestoration()
{
    resetNavigationState();
    applyDeepScrollableTree(25);

    for (int i = 1; i <= 25; ++i) {
        libraryController.enterFolder(QStringLiteral("f%1").arg(i));
        assertStackAligned();
    }
    QCOMPARE(stackView()->property("depth").toInt(), 25);

    auto *currentModel = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        currentPage()->property("projectionModel").value<QObject *>());
    QVERIFY(currentModel != nullptr);
    const int currentRowsBefore = currentModel->rowCount();
    QVERIFY(currentRowsBefore > 0);
    const QString currentFirstBefore = currentModel->entryAt(0)->nodeId;

    auto *otherModel = qobject_cast<Seriona::App::LibraryFolderProjectionModel *>(
        libraryController.projectionModelForNodeId(QStringLiteral("f10")));
    QVERIFY(otherModel != nullptr);
    const int otherRowsBefore = otherModel->rowCount();
    const QString otherFirstBefore = otherModel->rowCount() > 0 ? otherModel->entryAt(0)->nodeId : QString();

    // 排序：仅当前层（f25）原地重建
    QVariantList rules;
    QVariantMap rule;
    rule.insert(QStringLiteral("field"), QStringLiteral("title"));
    rule.insert(QStringLiteral("order"), QStringLiteral("desc"));
    rules.append(rule);
    libraryController.applySortRules(rules);

    // 当前层：模型身份不变、顺序更新（标题降序 → t-25-12 在前）
    QCOMPARE(currentPage()->property("projectionModel").value<QObject *>(), currentModel);
    QCOMPARE(currentModel->rowCount(), currentRowsBefore);
    QCOMPARE(currentModel->entryAt(0)->nodeId, QStringLiteral("t-25-12"));
    QVERIFY(currentModel->entryAt(0)->nodeId != currentFirstBefore);

    // 其他缓存层：身份与顺序均不变
    QCOMPARE(libraryController.projectionModelForNodeId(QStringLiteral("f10")), otherModel);
    QCOMPARE(otherModel->rowCount(), otherRowsBefore);
    QCOMPARE(otherModel->entryAt(0)->nodeId, otherFirstBefore);

    // 栈无变化
    assertStackAligned();
    QCOMPARE(stackView()->property("depth").toInt(), 25);
    QCOMPARE(currentPage()->property("folderNodeId").toString(), QStringLiteral("f25"));
}

// 用户报告：根视图（playlistView）条目 hover 无 UI 变化，深层 FolderPage 条目正常
// （folderStack 在 depth 0 时可见但 disabled，疑似拦截 hover）。回归锁：
// 根视图条目 hover 必须置位（与 FolderPage 条目行为一致）。
void SidebarQueueSwitchTest::rootViewDelegateHover()
{
    resetNavigationState();
    applyRichTree();

    QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(rootView != nullptr);
    QTRY_VERIFY(itemAt(rootView, 0) != nullptr);
    QQuickItem *rootDelegate = itemAt(rootView, 0);

    // 先移出条目区域，确保初始为未悬停态（慢机上离开事件可能延迟 → 轮询等待清空）
    QTest::mouseMove(window, QPoint(-50, -50));
    QTRY_VERIFY_WITH_TIMEOUT(!rootDelegate->property("hovered").toBool(), 5000);

    // 悬停到根视图第一个条目：hovered 必须置位（当前 bug：始终 false）
    const QPointF center = rootDelegate->mapToScene(QPointF(rootDelegate->width() / 2, rootDelegate->height() / 2));
    QTest::mouseMove(window, center.toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(rootDelegate->property("hovered").toBool(), 5000);
}

// 对照组：FolderPage 条目 hover 保持正常（hoverEnabled: false 只禁 StackView 自身命中）
void SidebarQueueSwitchTest::folderPageDelegateHover()
{
    resetNavigationState();
    applyRichTree();

    QQuickItem *rootView = qobject_cast<QQuickItem *>(findItem(QStringLiteral("playlistView")));
    QVERIFY(rootView != nullptr);
    QTRY_VERIFY(itemAt(rootView, 0) != nullptr);

    QQuickItem *folderDelegate = itemAt(rootView, 0);
    clickDelegate(folderDelegate);
    QTRY_COMPARE(stackView()->property("depth").toInt(), 1);
    waitStackSettled();
    QObject *page = currentPage();
    QVERIFY(page != nullptr);
    QQuickItem *pageList = qobject_cast<QQuickItem *>(page->property("listView").value<QObject *>());
    QVERIFY(pageList != nullptr);
    QTRY_VERIFY(itemAt(pageList, 0) != nullptr);
    QQuickItem *pageDelegate = itemAt(pageList, 0);
    // 点击进入时鼠标停留在 g1 行场景位置，可能恰好落在页面首行上：先移出清空 hover
    QTest::mouseMove(window, QPoint(-50, -50));
    QTRY_VERIFY_WITH_TIMEOUT(!pageDelegate->property("hovered").toBool(), 5000);
    const QPointF pageCenter =
        pageDelegate->mapToScene(QPointF(pageDelegate->width() / 2, pageDelegate->height() / 2));
    QTest::mouseMove(window, pageCenter.toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(pageDelegate->property("hovered").toBool(), 5000);
}

QTEST_MAIN(SidebarQueueSwitchTest)

#include "tst_sidebar_queue_switch.moc"
