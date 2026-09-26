// 歌词纠错 UI 的运行时用例（S19 A1/A2/A4/A6 + todo 34）：实例化真实 QML 组件并触发其
// 交互入口，而不是只做源文本断言（源文本断言会把坏调用逐字断言为正确，见
// tst_ui_only_handler_policy）。
//
// 覆盖：
//  - A1：第 4 项「查看本行自动判定」触发后弹窗必须真的可见（Window.show()；open() 会抛
//        TypeError 且弹窗永不出现）。
//  - A2：修正弹窗的居中基准必须取 Window 自己的 transientParent（root 是 Item，没有该
//        属性；写成 root.transientParent 时条件恒假、x/y 恒 0）。
//  - A4：整首纠错拖动的保存门对「拖动结果」判定 —— 初始展示对不可复现的参照行，
//        拖动后仍必须可保存。
//  - A6：行级「修正原文/译文」空原文（含全空白）不提交、不关窗、给出原因。
//  - todo 34：纠错管理列表范围 = 当前曲目（A 2 条 / B 1 条互不串场）、逐行删除次数、
//        每条 auto 判定可对照（模型角色 + 渲染槽）、批量后回到与 manual 不同的 auto 值、
//        单条删除键 = rawLine（含 delegate 按钮的真实点击）、空键不发命令、
//        dataChanged 不重建列表（滚动位置保持）。
//
// appFacade 用委托到生产头文件谓词的桩（不链接后端）：门语义与 AppFacade 同源，
// 因此用例判负的是 QML 的接线，而不是桩自身的近似实现。多曲目 LyricsStub 模拟后端
// 「Remove 只撤 manual 覆盖、行保留、展示值回落到被覆盖前的 auto」的契约。
//
// 列表 delegate 的子项经 QQuickItem 视觉树获取（QML 视觉子项不在 QObject 父子链上，
// findChildren 对它们返回 0；delegate 由视图动态创建，更是只有视觉树才见得到）。
//
// 桩是同步的；生产经 QueuedConnection 异步收敛，本文件的用例只证明命令发出与最终态语义。
//
// 依赖构建目录的 Seriona QML 模块产物，故 CMake 侧 add_dependencies(seriona)。
#include "lyric_split_boundary.h"
#include "lyrics_model.h"
#include "popup_input_guard.h"

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QHash>
#include <QMetaObject>
#include <QModelIndex>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

namespace LyricUiRuntime {

// 多曲目歌词行桩：lines() 返回【当前曲目】的行，用于构造「A 2 条 manual、B 1 条 manual」
// 的双曲目场景（列表范围正确性的判负前提）。
class LyricsStub : public QObject
{
    Q_OBJECT

public:
    explicit LyricsStub(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    Q_INVOKABLE QVariantList lines() const { return m_tracks.value(m_currentTrack); }

    void setTrack(const QString &trackId, const QVariantList &rows) { m_tracks.insert(trackId, rows); }
    void setCurrentTrack(const QString &trackId) { m_currentTrack = trackId; }
    void appendRow(const QString &trackId, const QVariantMap &row) { m_tracks[trackId].append(row); }

    // 驱动被测组件的 Connections：信号名与生产 LyricsModel（QAbstractItemModel）一致，
    // 使 onModelReset / onDataChanged 处理器真的被触发（roles 载荷对 QML 处理器无影响）。
    void emitModelReset() { emit modelReset(); }
    void emitDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles)
    {
        emit dataChanged(topLeft, bottomRight, roles);
    }

    // 模拟后端 RemoveLyricSplitCorrection：只撤【当前曲目】该 rawLine 的 manual 覆盖 ——
    // 行保留，展示值回落到被覆盖前的自动判定（这正是「立即回到自动结果」能成立的原因）。
    bool removeManualOnCurrentTrack(const QString &rawLine)
    {
        QVariantList rows = m_tracks.value(m_currentTrack);
        for (int i = 0; i < rows.size(); ++i) {
            QVariantMap row = rows[i].toMap();
            if (row.value(QStringLiteral("rawLine")).toString() != rawLine)
                continue;
            const bool wasManual = row.value(QStringLiteral("manualOverride")).toBool();
            row.insert(QStringLiteral("manualOverride"), false);
            row.insert(QStringLiteral("displayLine"), row.value(QStringLiteral("autoOriginal")));
            row.insert(QStringLiteral("translation"), row.value(QStringLiteral("autoTranslation")));
            rows[i] = row;
            m_tracks.insert(m_currentTrack, rows);
            return wasManual;
        }
        return false;
    }

    // 模拟被禁用的全库 clearManual：清掉【所有曲目】的 manual 覆盖（含 B 曲目）。
    // 仅供「clearManual 未被调用」断言做变异探测 —— 生产 AppFacade 不提供该方法。
    void clearManualOnAllTracks()
    {
        for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
            QVariantList rows = it.value();
            for (int i = 0; i < rows.size(); ++i) {
                QVariantMap row = rows[i].toMap();
                if (!row.value(QStringLiteral("manualOverride")).toBool())
                    continue;
                row.insert(QStringLiteral("manualOverride"), false);
                row.insert(QStringLiteral("displayLine"), row.value(QStringLiteral("autoOriginal")));
                row.insert(QStringLiteral("translation"), row.value(QStringLiteral("autoTranslation")));
                rows[i] = row;
            }
            it.value() = rows;
        }
    }

signals:
    void modelReset();
    void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);

private:
    QHash<QString, QVariantList> m_tracks;
    QString m_currentTrack;
};

// appFacade 桩：门语义全部委托生产谓词。commit 计数分「尝试」与「成功」两个，
// 以便区分「QML 门拦住了」与「QML 放行但桩拒绝」。
class FacadeStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *lyrics READ lyrics CONSTANT)

public:
    explicit FacadeStub(QObject *parent = nullptr)
        : QObject(parent)
        , m_lyrics(new LyricsStub(this))
    {
    }

    QObject *lyrics() const { return m_lyrics; }
    LyricsStub *lyricsStub() const { return m_lyrics; }

    Q_INVOKABLE QVariantMap lyricSplitBoundaryCut(const QString &rawLine,
                                                  const QString &original,
                                                  const QString &translation) const
    {
        const Seriona::App::LyricSplitBoundaryCut cut =
            Seriona::App::findLyricSplitBoundaryCut(rawLine, original, translation);
        QVariantMap result;
        result.insert(QStringLiteral("valid"), cut.valid);
        result.insert(QStringLiteral("leftEnd"), cut.leftEnd);
        result.insert(QStringLiteral("rightStart"), cut.rightStart);
        return result;
    }

    Q_INVOKABLE QVariantMap lyricSplitBoundaryParts(const QString &rawLine, int boundaryIndex) const
    {
        const Seriona::App::LyricSplitBoundaryParts parts =
            Seriona::App::splitLyricLineAtBoundary(rawLine, boundaryIndex);
        QVariantMap result;
        result.insert(QStringLiteral("valid"), parts.valid);
        result.insert(QStringLiteral("original"), parts.original);
        result.insert(QStringLiteral("translation"), parts.translation);
        return result;
    }

    Q_INVOKABLE bool isLyricOriginalSubmittable(const QString &original) const
    {
        return Seriona::App::lyricOriginalIsSubmittable(original);
    }

    Q_INVOKABLE bool commitLyricSplitCorrection(const QString &rawText,
                                                const QString &original,
                                                const QString &translation)
    {
        ++correctionCommitAttempts;
        if (!Seriona::App::lyricOriginalIsSubmittable(original)) {
            return false;
        }
        ++correctionCommits;
        lastCorrectionRaw = rawText;
        lastCorrectionOriginal = original;
        lastCorrectionTranslation = translation;
        return true;
    }

    Q_INVOKABLE bool lyricSplitBoundaryCommitAllowed(const QString &rawLine,
                                                     int boundaryIndex,
                                                     const QString &currentOriginal,
                                                     const QString &currentTranslation) const
    {
        const Seriona::App::LyricSplitBoundaryParts parts =
            Seriona::App::splitLyricLineAtBoundary(rawLine, boundaryIndex);
        return Seriona::App::lyricSplitBoundaryShouldCommit(parts, currentOriginal, currentTranslation);
    }

    Q_INVOKABLE bool commitLyricSplitBoundary(const QString &rawLine,
                                              int boundaryIndex,
                                              const QString &currentOriginal,
                                              const QString &currentTranslation)
    {
        const Seriona::App::LyricSplitBoundaryParts parts =
            Seriona::App::splitLyricLineAtBoundary(rawLine, boundaryIndex);
        if (!Seriona::App::lyricSplitBoundaryShouldCommit(parts, currentOriginal, currentTranslation)) {
            return false;
        }
        ++boundaryCommits;
        return true;
    }

    // 恢复本行自动识别：记录发出的【键】（rawText），并让歌词桩按后端契约撤掉该行的 manual 覆盖。
    // 约定不在此断言：QML 只把 rawText 一个参数交给门面，约定由生产桥层从当前快照取
    // （正身覆盖见 tst_backend_bridge.cpp 的 lyricSplitCorrectionCommandsCarrySnapshotConvention）；
    // 本用例的 appFacade 是纯桩、桥层不参与，在 QML 层断言约定会恒真、对生产零信息。
    Q_INVOKABLE bool removeLyricSplitCorrection(const QString &rawText)
    {
        ++manualRemoveCalls;
        manualRemoveRawKeys.append(rawText);
        return m_lyrics->removeManualOnCurrentTrack(rawText);
    }

    // 【本 todo 已禁用】的全库接口：生产 AppFacade 不提供；桩保留它只是为了让
    // 「clearManual 未被调用」成为可判负断言（否则是对不存在方法的恒真断言）。
    Q_INVOKABLE void clearManual()
    {
        ++clearManualCalls;
        m_lyrics->clearManualOnAllTracks();
    }

    int correctionCommitAttempts = 0;
    int correctionCommits = 0;
    int boundaryCommits = 0;
    int manualRemoveCalls = 0;
    int clearManualCalls = 0;
    QString lastCorrectionRaw;
    QString lastCorrectionOriginal;
    QString lastCorrectionTranslation;
    QStringList manualRemoveRawKeys;

private:
    LyricsStub *m_lyrics = nullptr;
};

} // namespace LyricUiRuntime

namespace {

constexpr auto kHostQml = R"(
import QtQuick
import Seriona

Window {
    id: host
    objectName: "lyricUiRuntimeHost"
    width: 800
    height: 600
    visible: true
    property int menuClicks: 0

    LyricLineContextMenu {
        id: menu
        objectName: "lyricLineContextMenuUnderTest"
        appFacade: testFacade
    }

    MouseArea {
        id: clickTarget
        x: 200; y: 80; width: 400; height: 200
        acceptedButtons: Qt.RightButton
        onClicked: mouse => {
            const point = mapToGlobal(mouse.x, mouse.y);
            menu.openForLine({rawLine: "Test Line", displayLine: "Test Line"}, point.x, point.y);
        }
    }

    BubbleMenu {
        id: settingsMenu
        objectName: "settingsMenuUnderTest"
        targetItem: clickTarget
        arrowDirection: "down"
        BubbleMenuItem {
            objectName: "settingsActionUnderTest"
            text: qsTr("设置")
            onTriggered: {
                host.menuClicks++;
                settingsMenu.close();
            }
        }
    }

    LyricSplitEditorWindow {
        id: editor
        objectName: "lyricSplitEditorWindowUnderTest"
        appFacade: testFacade
    }

    LyricCorrectionManager {
        id: correctionManager
        objectName: "lyricCorrectionManagerUnderTest"
        width: 420
        height: 560
        appFacade: testFacade
    }
}
)";

QVariantMap manualRow(const QString &rawLine,
                      const QString &original,
                      const QString &translation,
                      const QString &autoOriginal,
                      const QString &autoTranslation)
{
    QVariantMap row;
    row.insert(QStringLiteral("rawLine"), rawLine);
    row.insert(QStringLiteral("displayLine"), original);
    row.insert(QStringLiteral("translation"), translation);
    row.insert(QStringLiteral("manualOverride"), true);
    row.insert(QStringLiteral("autoOriginal"), autoOriginal);
    row.insert(QStringLiteral("autoTranslation"), autoTranslation);
    return row;
}

QVariantMap autoOnlyRow(const QString &rawLine, const QString &original, const QString &translation)
{
    QVariantMap row;
    row.insert(QStringLiteral("rawLine"), rawLine);
    row.insert(QStringLiteral("displayLine"), original);
    row.insert(QStringLiteral("translation"), translation);
    row.insert(QStringLiteral("manualOverride"), false);
    return row;
}

} // namespace

class LyricUiRuntimeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void autoJudgementItemShowsVisibleWindow();
    void lyricLineContextMenuRepositionOnRepeatedRightClick();
    void settingsMenuStillAcceptsClicks();
    void correctionDialogCentersAndRejectsBlankOriginal();
    void splitEditorSavesNonReproducibleRowAfterDrag();

    // todo 34：纠错管理列表
    void correctionManagerScopeFollowsCurrentTrackOnly();
    void correctionManagerBulkRestoreIssuesOneRemovePerManualRow();
    void correctionManagerShowsAutoJudgementForComparison();
    void correctionManagerBulkRestoreReturnsToAutoImmediately();
    void correctionManagerSingleRemoveUsesRawKey();
    void correctionManagerDelegateButtonRemovesRowByRawLine();
    void correctionManagerSkipsEmptyRawLine();
    void correctionManagerRemoveRowSkipsEmptyKey();
    void correctionManagerScrollPositionSurvivesDataChanged();

private:
    QObject *child(const QString &objectName) const;
    bool activate(QObject *item);

    // 直接按角色名读管理列表模型的一行（不依赖 delegate 是否已建）。
    QVariantMap listRow(QObject *model, int row) const;

    // 读一个 ListModel 的一行（ListModel.get(i)），返回 {role: value}。
    QVariantMap modelRow(QObject *model, int row) const;

    // QML 视觉子项不挂在 QObject 父子链上（delegate 由视图动态创建，更是只有视觉树才见得到）
    // ⇒ 只能走 QQuickItem::childItems() 递归视觉树；findChildren 对这些项返回 0。
    QList<QQuickItem *> visualItems(const QString &objectName) const;
    QList<QQuickItem *> visualItemsIn(QQuickItem *base, const QString &objectName) const;
    QQuickItem *visualDelegate(const QString &rawLine) const;
    QQuickItem *visualDescendant(QQuickItem *base, const QString &objectName) const;

    QQuickWindow *host = nullptr;
    LyricUiRuntime::FacadeStub facade;
    QQmlApplicationEngine engine;
};

void LyricUiRuntimeTest::initTestCase()
{
    qmlRegisterType<Seriona::App::PopupInputGuard>("Seriona", 1, 0, "PopupInputGuard");
    engine.rootContext()->setContextProperty(QStringLiteral("testFacade"), &facade);
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.loadData(QByteArray(kHostQml), QUrl(QStringLiteral("qrc:/seriona_lyric_ui_runtime_test.qml")));

    QVERIFY2(!engine.rootObjects().isEmpty(), "host Window failed to load");
    host = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY2(host != nullptr, "host root is not a QQuickWindow");
    QVERIFY2(host->isVisible(), "host window is not visible");
}

QObject *LyricUiRuntimeTest::child(const QString &objectName) const
{
    return host->findChild<QObject *>(objectName);
}

bool LyricUiRuntimeTest::activate(QObject *item)
{
    // BubbleMenuItem.activate() 是 QML 函数（触发 triggered → onTriggered）。
    return QMetaObject::invokeMethod(item, "activate");
}


QVariantMap LyricUiRuntimeTest::listRow(QObject *model, int row) const
{
    QVariantMap values;
    auto *itemModel = qobject_cast<QAbstractItemModel *>(model);
    if (itemModel == nullptr) {
        return values;
    }
    const QModelIndex index = itemModel->index(row, 0);
    const QHash<int, QByteArray> roles = itemModel->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        values.insert(QString::fromUtf8(it.value()), itemModel->data(index, it.key()));
    }
    return values;
}

namespace {
void collectVisualItems(QQuickItem *item, const QString &objectName, QList<QQuickItem *> &out)
{
    if (item == nullptr) {
        return;
    }
    if (item->objectName() == objectName) {
        out.append(item);
    }
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        collectVisualItems(child, objectName, out);
    }
}
} // namespace

QList<QQuickItem *> LyricUiRuntimeTest::visualItems(const QString &objectName) const
{
    QList<QQuickItem *> out;
    collectVisualItems(host->contentItem(), objectName, out);
    return out;
}

QList<QQuickItem *> LyricUiRuntimeTest::visualItemsIn(QQuickItem *base, const QString &objectName) const
{
    QList<QQuickItem *> out;
    collectVisualItems(base, objectName, out);
    return out;
}

QVariantMap LyricUiRuntimeTest::modelRow(QObject *model, int row) const
{
    // QQmlListModel::get(int) 返回 QJSValue（QMetaObject 转不到 QVariant），只可按 role 读 data()。
    auto *abstract = qobject_cast<QAbstractItemModel *>(model);
    if (abstract == nullptr) {
        return {};
    }
    const QModelIndex index = abstract->index(row, 0);
    QVariantMap values;
    const QHash<int, QByteArray> roles = abstract->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        values.insert(QString::fromUtf8(it.value()), abstract->data(index, it.key()));
    }
    return values;
}

QQuickItem *LyricUiRuntimeTest::visualDelegate(const QString &rawLine) const
{
    const QList<QQuickItem *> delegates = visualItems(QStringLiteral("lyricCorrectionRowDelegate"));
    for (QQuickItem *delegate : delegates) {
        if (delegate->property("rawLine").toString() == rawLine) {
            return delegate;
        }
    }
    return nullptr;
}

QQuickItem *LyricUiRuntimeTest::visualDescendant(QQuickItem *base, const QString &objectName) const
{
    if (base == nullptr) {
        return nullptr;
    }
    const QList<QQuickItem *> children = base->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == objectName) {
            return child;
        }
        if (QQuickItem *found = visualDescendant(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

// A1：触发第 4 项后，只读判定弹窗必须真的可见（Window 只有 show()，open() 会抛
// TypeError 并让弹窗永不出现）。
void LyricUiRuntimeTest::autoJudgementItemShowsVisibleWindow()
{
    QObject *autoJudgeDialog = child(QStringLiteral("lyricAutoJudgementDialog"));
    QVERIFY2(autoJudgeDialog != nullptr, "autoJudgeDialog not found");
    QCOMPARE(autoJudgeDialog->property("visible").toBool(), false);

    // Window 没有 open()：若第 4 项改回 open()，QML 会抛 TypeError 且 visible 保持 false。
    QVERIFY2(autoJudgeDialog->metaObject()->indexOfMethod("open()") < 0,
             "Window unexpectedly exposes open(); this probe expects it to be absent");
    QVERIFY2(autoJudgeDialog->metaObject()->indexOfMethod("show()") >= 0,
             "Window must expose show()");

    QObject *autoJudgeItem = child(QStringLiteral("lyricAutoJudgementItem"));
    QVERIFY2(autoJudgeItem != nullptr, "lyricAutoJudgementItem not found");
    QVERIFY(activate(autoJudgeItem));
    QTRY_VERIFY(autoJudgeDialog->property("visible").toBool());

    // 收尾：关掉这个顶层 Window。它是 host 的 transient 子窗，留着可见会让后续用例经窗口投递的
    // 合成鼠标事件被它截获（host 的 item 收不到），是测试卫生问题、与产品行为无关。
    autoJudgeDialog->setProperty("visible", false);
    QTRY_VERIFY(!autoJudgeDialog->property("visible").toBool());
}

void LyricUiRuntimeTest::lyricLineContextMenuRepositionOnRepeatedRightClick()
{
    QObject *menuHost = child(QStringLiteral("lyricLineContextMenuUnderTest"));
    QVERIFY2(menuHost != nullptr, "LyricLineContextMenu instance not found");
    QObject *lineMenu = child(QStringLiteral("lyricLineContextMenu"));
    QVERIFY2(lineMenu != nullptr, "lineMenu BubbleMenu not found");

    QVERIFY(QTest::qWaitForWindowExposed(host));
    auto *popup = qobject_cast<QQuickWindow *>(lineMenu);
    QVERIFY(popup);
    for (const QPoint &point : {QPoint(300, 150), QPoint(300, 150), QPoint(300, 200), QPoint(400, 200)}) {
        const QPoint global = host->mapToGlobal(point);
        const QPoint expected(global.x() - popup->width() / 2, global.y() + 12);
        QTest::mouseClick(host, Qt::RightButton, Qt::NoModifier, point);
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        QTest::qWait(150);
        qInfo() << "REPOSITION platform=" << QGuiApplication::platformName()
                << "global=" << global << "expected=" << expected << "actual=" << popup->position();
        QVERIFY((popup->position() - expected).manhattanLength() <= 2);
    }
    QVERIFY(popup->close());
    const QPoint point(350, 160);
    const QPoint global = host->mapToGlobal(point);
    QTest::mouseClick(host, Qt::RightButton, Qt::NoModifier, point);
    QVERIFY(QTest::qWaitForWindowExposed(popup));
    QTRY_VERIFY((popup->position() - QPoint(global.x() - popup->width() / 2, global.y() + 12)).manhattanLength() <= 2);
    QVERIFY(popup->close());
}

void LyricUiRuntimeTest::settingsMenuStillAcceptsClicks()
{
    auto *popup = qobject_cast<QQuickWindow *>(child(QStringLiteral("settingsMenuUnderTest")));
    auto *action = qobject_cast<QQuickItem *>(child(QStringLiteral("settingsActionUnderTest")));
    QVERIFY(popup && action);
    auto *target = popup->property("targetItem").value<QQuickItem *>();
    QVERIFY(target);
    target->setY(350);
    QVERIFY(QTest::qWaitForWindowExposed(host));
    host->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(host));
    QTest::qWait(200);
    for (int i = 0; i < 2; ++i) {
        QVERIFY(QMetaObject::invokeMethod(popup, "showAtTarget"));
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        const QPoint global = target->mapToGlobal(QPointF(target->width() / 2, 0)).toPoint();
        const QPoint expected(global.x() - popup->width() / 2, global.y() - popup->height() - 12);
        QTRY_VERIFY((popup->position() - expected).manhattanLength() <= 2);
        const QPoint center = action->mapToScene(QPointF(action->width() / 2, action->height() / 2)).toPoint();
        QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE(host->property("menuClicks").toInt(), i + 1);
        QTRY_VERIFY(!popup->isVisible());
    }
    target->setY(80);
}

// A2 + A6：弹窗居中基准取自身 transientParent（不是恒 undefined 的 root.transientParent）；
// 空原文（含全空白）不提交、不关窗、给出原因。
void LyricUiRuntimeTest::correctionDialogCentersAndRejectsBlankOriginal()
{
    QObject *menu = child(QStringLiteral("lyricLineContextMenuUnderTest"));
    QVERIFY2(menu != nullptr, "LyricLineContextMenu instance not found");

    QVariantMap lineData;
    lineData.insert(QStringLiteral("rawLine"), QStringLiteral("原文 / 译文"));
    lineData.insert(QStringLiteral("displayLine"), QStringLiteral("原文"));
    lineData.insert(QStringLiteral("translation"), QStringLiteral("译文"));
    menu->setProperty("lineData", lineData);

    // 走真实菜单项接线（onTriggered → correctionDialog.openFor(root.lineData)）。
    QObject *correctItem = child(QStringLiteral("lyricCorrectItem"));
    QVERIFY2(correctItem != nullptr, "lyricCorrectItem not found");
    QVERIFY(activate(correctItem));

    QObject *correctionDialog = child(QStringLiteral("lyricCorrectionDialog"));
    QObject *originalField = child(QStringLiteral("lyricCorrectionOriginalField"));
    QObject *originalError = child(QStringLiteral("lyricCorrectionOriginalError"));
    QVERIFY2(correctionDialog != nullptr, "correctionDialog not found");
    QVERIFY2(originalField != nullptr, "original field not found");
    QVERIFY2(originalError != nullptr, "original error label not found");
    QTRY_VERIFY(correctionDialog->property("visible").toBool());

    // A2：居中基准必须是弹窗自己的 transientParent（root 是 Item，没有该属性）。
    // 平台可能对子窗口做小幅重排，故断言「居中」（含容差）而非逐像素相等；
    // A2 回归时该绑定恒为 0，x==0 与中心偏移两者都判负。
    QObject *transientParent = correctionDialog->property("transientParent").value<QObject *>();
    QVERIFY2(transientParent != nullptr, "correctionDialog transientParent not resolved");
    const int dialogX = correctionDialog->property("x").toInt();
    const int dialogWidth = correctionDialog->property("width").toInt();
    const int parentX = transientParent->property("x").toInt();
    const int parentWidth = transientParent->property("width").toInt();
    QVERIFY2(dialogX != 0, "centering is dead (x==0): root.transientParent regression");
    QVERIFY2(parentWidth > dialogWidth, "transientParent too narrow for the centering assertion to be meaningful");
    const int centerDelta = qAbs((dialogX + dialogWidth / 2) - (parentX + parentWidth / 2));
    QVERIFY2(centerDelta <= 8, qPrintable(QStringLiteral("not centered: delta=%1").arg(centerDelta)));

    // A6：空原文不提交、不关窗、给出原因。
    originalField->setProperty("text", QString());
    QVERIFY(QMetaObject::invokeMethod(correctionDialog, "saveFromFields"));
    QCOMPARE(facade.correctionCommitAttempts, 0);
    QCOMPARE(facade.correctionCommits, 0);
    QVERIFY2(!correctionDialog->property("originalError").toString().isEmpty(), "originalError not set");
    QVERIFY2(!originalError->property("text").toString().isEmpty(), "blank-original reason not shown");
    QVERIFY2(correctionDialog->property("visible").toBool(), "dialog must stay open on rejection");

    // 全空白同样是空原文。
    originalField->setProperty("text", QStringLiteral("   "));
    QVERIFY(QMetaObject::invokeMethod(correctionDialog, "saveFromFields"));
    QCOMPARE(facade.correctionCommitAttempts, 0);
    QCOMPARE(facade.correctionCommits, 0);

    // 合法原文提交，原因清空并关窗。
    originalField->setProperty("text", QStringLiteral("新原文"));
    QVERIFY(QMetaObject::invokeMethod(correctionDialog, "saveFromFields"));
    QCOMPARE(facade.correctionCommitAttempts, 1);
    QCOMPARE(facade.correctionCommits, 1);
    QCOMPARE(facade.lastCorrectionOriginal, QStringLiteral("新原文"));
    QVERIFY2(correctionDialog->property("originalError").toString().isEmpty(), "reason not cleared");
    QTRY_VERIFY(!correctionDialog->property("visible").toBool());
}

// A4 + H1 + M1：初始展示对无法被分界复现的参照行（译文来自别行），经【真实鼠标拖拽】
// （向 dragArea 投递 mousePress / mouseMove / mouseRelease，走生产接线）后必须可保存。
// 同时是 H1 的判负靶：dragArea 若锚定非法（几何 0×0）或收不到事件，下面的
// touched / boundaryIndex / canSaveSelected 三条断言都会失败。
// touched 只能由 dragArea.onPositionChanged 产生 —— 本用例【不】直接改 frozenModel 的
// touched/boundaryIndex（那正是 M1 指出的伪造手法）。
void LyricUiRuntimeTest::splitEditorSavesNonReproducibleRowAfterDrag()
{
    QObject *editor = child(QStringLiteral("lyricSplitEditorWindowUnderTest"));
    QVERIFY2(editor != nullptr, "LyricSplitEditorWindow instance not found");
    QObject *frozenModel = child(QStringLiteral("lyricSplitFrozenModel"));
    QVERIFY2(frozenModel != nullptr, "frozenModel not found");
    auto *editorWindow = qobject_cast<QQuickWindow *>(editor);
    QVERIFY2(editorWindow != nullptr, "editor is not a QQuickWindow");

    const QString rawLine = QStringLiteral("原文 其他词");
    const QString shown = QStringLiteral("原文");
    const QString otherLineTranslation = QStringLiteral("来自别行的译文");

    // 打开时展示对不可复现（raw 中不含该译文）—— 旧门会因此把整类行永久封锁。
    QCOMPARE(Seriona::App::findLyricSplitBoundaryCut(rawLine, shown, otherLineTranslation).valid, false);

    QVariantMap row;
    row.insert(QStringLiteral("rawLine"), rawLine);
    row.insert(QStringLiteral("displayLine"), shown);
    row.insert(QStringLiteral("translation"), otherLineTranslation);
    row.insert(QStringLiteral("manualOverride"), true);
    QVariantList rows;
    rows.append(row);
    facade.lyricsStub()->setTrack(QStringLiteral("editor"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("editor"));

    QVERIFY(QMetaObject::invokeMethod(editor, "openEditor"));
    QCOMPARE(frozenModel->property("count").toInt(), 1);
    QTRY_VERIFY(editorWindow->isVisible());
    QVERIFY(QTest::qWaitForWindowExposed(editorWindow));
    QTest::qWait(50);

    QCOMPARE(editor->property("selectedIndex").toInt(), -1);
    QCOMPARE(editor->property("canSaveSelected").toBool(), false);
    QCOMPARE(modelRow(frozenModel, 0).value(QStringLiteral("touched")).toBool(), false);

    // H1 判负靶①：dragArea 必须真的存在、可见、几何非零。
    // 此前它锚定到祖孙项 rawTextFull（非父非兄弟），Qt 丢弃锚 ⇒ 0×0 ⇒ 收不到任何事件。
    QList<QQuickItem *> dragAreas;
    QTRY_VERIFY_WITH_TIMEOUT(!(dragAreas = visualItemsIn(editorWindow->contentItem(),
                                                         QStringLiteral("lyricSplitDragArea"))).isEmpty(), 5000);
    QQuickItem *dragArea = dragAreas.first();
    QTRY_VERIFY_WITH_TIMEOUT(dragArea->width() > 0.0 && dragArea->height() > 0.0, 5000);
    QVERIFY2(dragArea->isVisible(), "dragArea is not visible");
    qInfo().nospace() << "SPLIT-EDITOR dragArea=" << dragArea->width() << "x" << dragArea->height()
                      << " boundaryIndex=" << modelRow(frozenModel, 0).value(QStringLiteral("boundaryIndex")).toInt();

    // 真实点选本行：未选中行的 dragArea 处于 disabled，按下落到选择区（生产接线 onClicked）。
    const QPointF selectPt = dragArea->mapToScene(QPointF(dragArea->width() * 0.7,
                                                          dragArea->height() / 2.0));
    QTest::mouseClick(editorWindow, Qt::LeftButton, Qt::NoModifier, selectPt.toPoint());
    QTRY_COMPARE(editor->property("selectedIndex").toInt(), 0);
    QCOMPARE(editor->property("canSaveSelected").toBool(), false);

    // 真实拖拽：初始 boundaryIndex=0 ⇒ 分界句柄在 x≈0；按在其 ±16px 内，向右拖过若干字符。
    const QPointF grabPt = dragArea->mapToScene(QPointF(1.0, dragArea->height() / 2.0));
    const QPointF dropPt = dragArea->mapToScene(QPointF(40.0, dragArea->height() / 2.0));
    QTest::mousePress(editorWindow, Qt::LeftButton, Qt::NoModifier, grabPt.toPoint());
    QTest::mouseMove(editorWindow, dropPt.toPoint());
    QTest::qWait(30);
    QTest::mouseRelease(editorWindow, Qt::LeftButton, Qt::NoModifier, dropPt.toPoint());

    // H1 判负靶② / M1：touched 只能由真实拖动置真；分界必须真的变了。
    QTRY_COMPARE(modelRow(frozenModel, 0).value(QStringLiteral("touched")).toBool(), true);
    QVERIFY2(modelRow(frozenModel, 0).value(QStringLiteral("boundaryIndex")).toInt() != 0,
             "boundaryIndex did not change after a real drag");

    // A4：对拖动结果判定即可保存。旧实现用打开时的 reproducible=false 当门，这里失败。
    QVERIFY2(editor->property("canSaveSelected").toBool(), "save stayed disabled after a real drag");
    QVERIFY(QMetaObject::invokeMethod(editor, "saveSelected"));
    QCOMPARE(facade.boundaryCommits, 1);

    // L1：收尾关闭这个顶层 Window。它是独立顶层窗，留着可见会让后续用例经窗口投递的
    // 合成鼠标事件被它截获（与 A1 用例同一理由，见 autoJudgementItemShowsVisibleWindow）。
    editorWindow->setProperty("visible", false);
    QTRY_VERIFY(!editorWindow->property("visible").toBool());
}

// todo 34 / §4.4.1：列表范围 = 当前曲目。构造「A 曲目 2 行 manual、B 曲目 1 行 manual」，
// 切到 A 时恰好 2 条；批量恢复后 A 清空而 B 的 1 条仍在（误用全库 clearManual 必败）。
void LyricUiRuntimeTest::correctionManagerScopeFollowsCurrentTrackOnly()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr, "LyricCorrectionManager instance not found");
    QVERIFY2(manualModel != nullptr, "manualModel not found");

    QVariantList aRows;
    aRows.append(manualRow(QStringLiteral("A-raw-1"), QStringLiteral("A-cur-1"), QStringLiteral("A-trans-1"),
                           QStringLiteral("A-auto-1"), QStringLiteral("A-atrans-1")));
    aRows.append(manualRow(QStringLiteral("A-raw-2"), QStringLiteral("A-cur-2"), QStringLiteral("A-trans-2"),
                           QStringLiteral("A-auto-2"), QStringLiteral("A-atrans-2")));
    aRows.append(autoOnlyRow(QStringLiteral("A-raw-3"), QStringLiteral("A-auto-3"), QStringLiteral("A-atrans-3")));

    QVariantList bRows;
    bRows.append(manualRow(QStringLiteral("B-raw-1"), QStringLiteral("B-cur-1"), QStringLiteral("B-trans-1"),
                           QStringLiteral("B-auto-1"), QStringLiteral("B-atrans-1")));

    facade.lyricsStub()->setTrack(QStringLiteral("A"), aRows);
    facade.lyricsStub()->setTrack(QStringLiteral("B"), bRows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("A"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;
    facade.manualRemoveRawKeys.clear();
    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 2);

    // 列表恰好是 A 的两行（含 rawLine 键），不含 B 曲目的条目（不能残留/串场）。
    QCOMPARE(listRow(manualModel, 0).value(QStringLiteral("rawLine")).toString(), QStringLiteral("A-raw-1"));
    QCOMPARE(listRow(manualModel, 1).value(QStringLiteral("rawLine")).toString(), QStringLiteral("A-raw-2"));

    // 批量恢复只作用于当前曲目 A（2 条逐行命令），且绝不调用全库 clearManual。
    QVERIFY(QMetaObject::invokeMethod(manager, "restoreAll"));
    QCOMPARE(facade.manualRemoveCalls, 2);
    QCOMPARE(facade.clearManualCalls, 0);
    QCOMPARE(manualModel->property("count").toInt(), 0);

    // 切到 B：B 的 1 条 manual 必须仍在（若误用全库 clearManual 会被连坐清掉）。
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("B"));
    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 1);
    QCOMPARE(listRow(manualModel, 0).value(QStringLiteral("rawLine")).toString(), QStringLiteral("B-raw-1"));
    QCOMPARE(facade.clearManualCalls, 0);
}

// todo 34 / §4.4.2：批量恢复对 N 个 manual 行逐行发出 RemoveLyricSplitCorrection
// （次数 = N，不发给 auto 行），且 clearManual 次数为 0。
void LyricUiRuntimeTest::correctionManagerBulkRestoreIssuesOneRemovePerManualRow()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QStringLiteral("N-raw-1"), QStringLiteral("N-cur-1"), QStringLiteral("N-trans-1"),
                          QStringLiteral("N-auto-1"), QStringLiteral("N-atrans-1")));
    rows.append(manualRow(QStringLiteral("N-raw-2"), QStringLiteral("N-cur-2"), QStringLiteral("N-trans-2"),
                          QStringLiteral("N-auto-2"), QStringLiteral("N-atrans-2")));
    rows.append(autoOnlyRow(QStringLiteral("N-raw-auto"), QStringLiteral("N-auto-3"), QStringLiteral("N-atrans-3")));

    facade.lyricsStub()->setTrack(QStringLiteral("N"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("N"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;
    facade.manualRemoveRawKeys.clear();

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 2);

    QVERIFY(QMetaObject::invokeMethod(manager, "restoreAll"));

    // 逐行：恰好 2 次（不是 1 次「全库」、也不是 3 次把 auto 行也发一遍）。
    QCOMPARE(facade.manualRemoveCalls, 2);
    QCOMPARE(facade.manualRemoveRawKeys.size(), 2);
    QCOMPARE(facade.manualRemoveRawKeys.at(0), QStringLiteral("N-raw-1"));
    QCOMPARE(facade.manualRemoveRawKeys.at(1), QStringLiteral("N-raw-2"));
    QVERIFY(!facade.manualRemoveRawKeys.contains(QStringLiteral("N-raw-auto")));
    QCOMPARE(facade.clearManualCalls, 0);
}

// todo 34 / §4.4.3：列表项显示被覆盖【之前】的 autoOriginal/autoTranslation 供对照。
// 两条断言都落在**执行**上：①模型角色（重建后的数据面）；②渲染槽（delegate 的
// lyricCorrectionAutoText.text —— 经视觉树取得，见 F1 说明）。两槽若被接成同一个来源，两者都失败。
void LyricUiRuntimeTest::correctionManagerShowsAutoJudgementForComparison()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QStringLiteral("C-raw-1"), QStringLiteral("CUR-ORIG"), QStringLiteral("CUR-TRANS"),
                          QStringLiteral("AUTO-ORIG"), QStringLiteral("AUTO-TRANS")));
    facade.lyricsStub()->setTrack(QStringLiteral("C"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("C"));

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 1);

    // ① 模型角色：被覆盖【之前】的 auto 值，且与当前修正值不同（两槽同源即失败）。
    const QVariantMap row = listRow(manualModel, 0);
    QCOMPARE(row.value(QStringLiteral("original")).toString(), QStringLiteral("CUR-ORIG"));
    QCOMPARE(row.value(QStringLiteral("translation")).toString(), QStringLiteral("CUR-TRANS"));
    QCOMPARE(row.value(QStringLiteral("autoOriginal")).toString(), QStringLiteral("AUTO-ORIG"));
    QCOMPARE(row.value(QStringLiteral("autoTranslation")).toString(), QStringLiteral("AUTO-TRANS"));
    QVERIFY(row.value(QStringLiteral("autoOriginal")).toString()
            != row.value(QStringLiteral("original")).toString());
    QVERIFY(row.value(QStringLiteral("autoTranslation")).toString()
            != row.value(QStringLiteral("translation")).toString());

    // ② 渲染槽：delegate 的 auto 文本显示 AUTO-*（不是 CUR-*）。delegate 子项经视觉树取得。
    QQuickItem *delegate = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((delegate = visualDelegate(QStringLiteral("C-raw-1"))) != nullptr, 5000);
    QQuickItem *autoText = visualDescendant(delegate, QStringLiteral("lyricCorrectionAutoText"));
    QVERIFY2(autoText != nullptr, "lyricCorrectionAutoText not reachable in the delegate visual tree");
    const QString rendered = autoText->property("text").toString();
    QVERIFY2(rendered.contains(QStringLiteral("AUTO-ORIG")), qPrintable(rendered));
    QVERIFY2(rendered.contains(QStringLiteral("AUTO-TRANS")), qPrintable(rendered));
    QVERIFY2(!rendered.contains(QStringLiteral("CUR-ORIG")), qPrintable(rendered));
    QVERIFY2(!rendered.contains(QStringLiteral("CUR-TRANS")), qPrintable(rendered));
}

// todo 34 / §4.4.4：批量恢复后列表清空，且展示值从【被覆盖的 manual 值】变成【与它不同的 auto 值】。
// 断言的是这次转换本身（恢复前 displayLine == R-cur-*，恢复后为 R-auto-* 且 != R-cur-*）——
// 不是复述桩里写死的 displayLine = autoOriginal 表达式。
// 桩是同步的：生产经 QueuedConnection 异步收敛（backend_bridge.cpp 的提交/订阅链），
// 本用例只证明命令发出与最终态语义，不证明生产时序，也不断言「立即」。
void LyricUiRuntimeTest::correctionManagerBulkRestoreReturnsToAutoImmediately()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QStringLiteral("R-raw-1"), QStringLiteral("R-cur-1"), QStringLiteral("R-trans-1"),
                          QStringLiteral("R-auto-1"), QStringLiteral("R-atrans-1")));
    rows.append(manualRow(QStringLiteral("R-raw-2"), QStringLiteral("R-cur-2"), QStringLiteral("R-trans-2"),
                          QStringLiteral("R-auto-2"), QStringLiteral("R-atrans-2")));
    facade.lyricsStub()->setTrack(QStringLiteral("R"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("R"));

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 2);

    // 恢复前：记录每一行当前展示的 manual 值。
    QHash<QString, QString> manualDisplay;
    const QVariantList before = facade.lyricsStub()->lines();
    for (const QVariant &entry : before) {
        const QVariantMap row = entry.toMap();
        manualDisplay.insert(row.value(QStringLiteral("rawLine")).toString(),
                             row.value(QStringLiteral("displayLine")).toString());
    }
    QCOMPARE(manualDisplay.value(QStringLiteral("R-raw-1")), QStringLiteral("R-cur-1"));
    QCOMPARE(manualDisplay.value(QStringLiteral("R-raw-2")), QStringLiteral("R-cur-2"));

    QVariant issued;
    QVERIFY(QMetaObject::invokeMethod(manager, "restoreAll", Q_RETURN_ARG(QVariant, issued)));
    QCOMPARE(issued.toInt(), 2);
    QCOMPARE(manualModel->property("count").toInt(), 0);

    // 恢复后：行仍在（未被删），覆盖已撤，且展示值变成了与 manual 不同的 auto 值。
    const QVariantList after = facade.lyricsStub()->lines();
    QCOMPARE(after.size(), 2);
    for (const QVariant &entry : after) {
        const QVariantMap row = entry.toMap();
        const QString rawLine = row.value(QStringLiteral("rawLine")).toString();
        const QString manualValue = manualDisplay.value(rawLine);
        const QString shown = row.value(QStringLiteral("displayLine")).toString();
        QCOMPARE(row.value(QStringLiteral("manualOverride")).toBool(), false);
        QVERIFY2(manualValue != QStringLiteral("R-auto-1") && manualValue != QStringLiteral("R-auto-2"),
                 qPrintable(QStringLiteral("manual value unexpectedly already looked automatic: %1").arg(manualValue)));
        QVERIFY2(shown == QStringLiteral("R-auto-1") || shown == QStringLiteral("R-auto-2"),
                 qPrintable(QStringLiteral("shown value is not the auto judgement: %1").arg(shown)));
        QVERIFY2(shown != manualValue,
                 qPrintable(QStringLiteral("shown value did not change from the manual value: %1").arg(shown)));
    }
}

// todo 34 / §4.4⑤：单条删除 —— 键是 rawLine（= line.text = 回传键），删 1 条后该行从列表消失、
// 其余行仍在。约定不在此断言：QML 只传 rawText，约定由生产桥层从当前快照取，
// 正身覆盖在 tst_backend_bridge.cpp 的 lyricSplitCorrectionCommandsCarrySnapshotConvention。
void LyricUiRuntimeTest::correctionManagerSingleRemoveUsesRawKey()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QStringLiteral("K-raw-1"), QStringLiteral("K-cur-1"), QStringLiteral("K-trans-1"),
                          QStringLiteral("K-auto-1"), QStringLiteral("K-atrans-1")));
    rows.append(manualRow(QStringLiteral("K-raw-2"), QStringLiteral("K-cur-2"), QStringLiteral("K-trans-2"),
                          QStringLiteral("K-auto-2"), QStringLiteral("K-atrans-2")));
    facade.lyricsStub()->setTrack(QStringLiteral("K"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("K"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;
    facade.manualRemoveRawKeys.clear();

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 2);

    // 键必须取 rawLine（回传键），不是 displayLine（K-cur-1）。
    QVERIFY(QMetaObject::invokeMethod(manager, "removeRow", Q_ARG(QVariant, QStringLiteral("K-raw-1"))));

    QCOMPARE(manualModel->property("count").toInt(), 1);
    QCOMPARE(listRow(manualModel, 0).value(QStringLiteral("rawLine")).toString(), QStringLiteral("K-raw-2"));
    QCOMPARE(facade.manualRemoveCalls, 1);
    QCOMPARE(facade.manualRemoveRawKeys.last(), QStringLiteral("K-raw-1"));
    QCOMPARE(facade.clearManualCalls, 0);
}

// todo 34 / F1：delegate 的「恢复本行」按钮**真实点击** → 该行的 rawLine 被外发、该行从列表消失。
// 按钮经 QQuickItem 视觉树取得（delegate 由 ListView 动态创建，findChildren 拿不到），
// 点击经 QTest::mouseClick 投递到窗口 —— 走的是生产接线 onClicked: root.removeRow(rowRect.rawLine)。
// 若该接线改传展示值（rowRect.original），本用例必败。
void LyricUiRuntimeTest::correctionManagerDelegateButtonRemovesRowByRawLine()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QStringLiteral("V-raw-1"), QStringLiteral("V-cur-1"), QStringLiteral("V-trans-1"),
                          QStringLiteral("V-auto-1"), QStringLiteral("V-atrans-1")));
    rows.append(manualRow(QStringLiteral("V-raw-2"), QStringLiteral("V-cur-2"), QStringLiteral("V-trans-2"),
                          QStringLiteral("V-auto-2"), QStringLiteral("V-atrans-2")));
    facade.lyricsStub()->setTrack(QStringLiteral("V"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("V"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;
    facade.manualRemoveRawKeys.clear();

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 2);

    // 两条都在视觉树里（否则下面的「找不到」无法归因）。
    QQuickItem *delegate1 = nullptr;
    QQuickItem *delegate2 = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((delegate1 = visualDelegate(QStringLiteral("V-raw-1"))) != nullptr, 5000);
    QTRY_VERIFY_WITH_TIMEOUT((delegate2 = visualDelegate(QStringLiteral("V-raw-2"))) != nullptr, 5000);
    QQuickItem *button = visualDescendant(delegate1, QStringLiteral("lyricCorrectionRemoveButton"));
    QVERIFY2(button != nullptr, "remove button not reachable in the delegate visual tree");

    // 视觉树可达性证据（历史上的错误结论是「offscreen 下 delegate 子项拿不到」）：
    // 经 QQuickItem::childItems() 递归可见 delegate 行 / 删除按钮 / auto 文本。
    qInfo().nospace() << "VISUAL-TREE rowDelegates=" << visualItems(QStringLiteral("lyricCorrectionRowDelegate")).size()
                      << " removeButtons=" << visualItems(QStringLiteral("lyricCorrectionRemoveButton")).size()
                      << " autoTexts=" << visualItems(QStringLiteral("lyricCorrectionAutoText")).size()
                      << " findChildrenRemoveButtons="
                      << host->findChildren<QObject *>(QStringLiteral("lyricCorrectionRemoveButton")).size();

    // offscreen 不出渲染帧：delegate 的几何要等布局收敛后才可命中，故先等它稳定，
    // 再独立校验映射出的场景点确实落在按钮内，最后投递真实鼠标点击。
    QTest::qWait(50);
    QTRY_VERIFY_WITH_TIMEOUT(button->isVisible() && button->width() > 0.0 && button->height() > 0.0, 5000);
    const QPointF center = button->mapToScene(QPointF(button->width() / 2.0, button->height() / 2.0));
    QVERIFY2(button->contains(button->mapFromScene(center)),
             qPrintable(QStringLiteral("mapped scene point is not inside the button: %1,%2")
                            .arg(center.x()).arg(center.y())));
    QTest::mouseClick(host, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    QTRY_COMPARE(facade.manualRemoveCalls, 1);

    // 键是 rawLine（V-raw-1），不是展示值 V-cur-1；且只有该行被删。
    QCOMPARE(facade.manualRemoveRawKeys.last(), QStringLiteral("V-raw-1"));
    QCOMPARE(facade.clearManualCalls, 0);
    QTRY_COMPARE(manualModel->property("count").toInt(), 1);
    QCOMPARE(listRow(manualModel, 0).value(QStringLiteral("rawLine")).toString(), QStringLiteral("V-raw-2"));
}

// todo 34 / F6：rawLine 为空的行不发命令（后端会本地拒绝空键），也不计入返回值。
void LyricUiRuntimeTest::correctionManagerSkipsEmptyRawLine()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QString(), QStringLiteral("EMPTY-cur"), QStringLiteral("EMPTY-trans"),
                          QStringLiteral("EMPTY-auto"), QStringLiteral("EMPTY-atrans")));
    rows.append(manualRow(QStringLiteral("E-raw-1"), QStringLiteral("E-cur-1"), QStringLiteral("E-trans-1"),
                          QStringLiteral("E-auto-1"), QStringLiteral("E-atrans-1")));
    facade.lyricsStub()->setTrack(QStringLiteral("E"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("E"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;
    facade.manualRemoveRawKeys.clear();

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 2);

    QVariant issued;
    QVERIFY(QMetaObject::invokeMethod(manager, "restoreAll", Q_RETURN_ARG(QVariant, issued)));

    // 发出条数 = 1（空键行被跳过），而不是 2。
    QCOMPARE(issued.toInt(), 1);
    QCOMPARE(facade.manualRemoveCalls, 1);
    QCOMPARE(facade.manualRemoveRawKeys.size(), 1);
    QVERIFY2(!facade.manualRemoveRawKeys.contains(QString()), "an empty rawLine was dispatched");
    QCOMPARE(facade.manualRemoveRawKeys.last(), QStringLiteral("E-raw-1"));
    QCOMPARE(facade.clearManualCalls, 0);
}

// todo 36 / F3：removeRow 的空键守卫必须与 restoreAll 同口径 —— 空键不外发命令
// （后端对空键是拒绝的），返回 false。判负靶：去掉守卫后 manualRemoveCalls 会变成 1。
void LyricUiRuntimeTest::correctionManagerRemoveRowSkipsEmptyKey()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");

    QVariantList rows;
    rows.append(manualRow(QStringLiteral("Z-raw-1"), QStringLiteral("Z-cur-1"), QStringLiteral("Z-trans-1"),
                          QStringLiteral("Z-auto-1"), QStringLiteral("Z-atrans-1")));
    facade.lyricsStub()->setTrack(QStringLiteral("Z"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("Z"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;
    facade.manualRemoveRawKeys.clear();

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 1);

    QVariant removed;
    QVERIFY(QMetaObject::invokeMethod(manager, "removeRow",
                                      Q_RETURN_ARG(QVariant, removed),
                                      Q_ARG(QVariant, QString())));
    QCOMPARE(removed.toBool(), false);
    QCOMPARE(facade.manualRemoveCalls, 0);
    QVERIFY(facade.manualRemoveRawKeys.isEmpty());
    QCOMPARE(facade.clearManualCalls, 0);
    // 空键不删任何东西，也不把状态重建错。
    QCOMPARE(manualModel->property("count").toInt(), 1);

    // 对照：非空键仍正常外发 —— 证明上面的 0 次不是接线整体失效。
    QVariant removedValid;
    QVERIFY(QMetaObject::invokeMethod(manager, "removeRow",
                                      Q_RETURN_ARG(QVariant, removedValid),
                                      Q_ARG(QVariant, QStringLiteral("Z-raw-1"))));
    QCOMPARE(removedValid.toBool(), true);
    QCOMPARE(facade.manualRemoveCalls, 1);
    QCOMPARE(facade.manualRemoveRawKeys.last(), QStringLiteral("Z-raw-1"));
    QCOMPARE(facade.clearManualCalls, 0);
}

// todo 34 / F4：列表不订阅 dataChanged —— 它只订阅结构性变化（modelReset / rowsInserted / rowsRemoved）。
// 播放行推进会对当前行发 dataChanged；若把整份重建挂到它上面，滚动中的视口会被反复打回。
// 判据有两层：①模型条数不因 dataChanged 变化（确定性）；②滚动位置不被重置（UX 面）。
// 末段用 modelReset 作对照，证明「没变」不是因为 Connections 根本没连上。
void LyricUiRuntimeTest::correctionManagerScrollPositionSurvivesDataChanged()
{
    QObject *manager = child(QStringLiteral("lyricCorrectionManagerUnderTest"));
    QObject *manualModel = child(QStringLiteral("lyricCorrectionManualModel"));
    QObject *list = child(QStringLiteral("lyricCorrectionList"));
    QVERIFY2(manager != nullptr && manualModel != nullptr, "correction manager/model not found");
    QVERIFY2(list != nullptr, "lyricCorrectionList not found");

    QVariantList rows;
    for (int i = 0; i < 20; ++i) {
        const QString index = QString::number(i);
        rows.append(manualRow(QStringLiteral("S-raw-") + index,
                              QStringLiteral("S-cur-") + index,
                              QStringLiteral("S-trans-") + index,
                              QStringLiteral("S-auto-") + index,
                              QStringLiteral("S-atrans-") + index));
    }
    facade.lyricsStub()->setTrack(QStringLiteral("S"), rows);
    facade.lyricsStub()->setCurrentTrack(QStringLiteral("S"));
    facade.manualRemoveCalls = 0;
    facade.clearManualCalls = 0;

    QVERIFY(QMetaObject::invokeMethod(manager, "refresh"));
    QCOMPARE(manualModel->property("count").toInt(), 20);

    // 滚动到某个正值（20 条 > 视口 240，可滚）。
    list->setProperty("contentY", 300.0);
    QTRY_VERIFY_WITH_TIMEOUT(list->property("contentY").toReal() > 0.0, 5000);
    const qreal scrolledY = list->property("contentY").toReal();

    // 快照多出一条 manual 行，但先不发结构性信号。
    facade.lyricsStub()->appendRow(QStringLiteral("S"),
                                   manualRow(QStringLiteral("S-raw-extra"), QStringLiteral("S-cur-extra"),
                                             QStringLiteral("S-trans-extra"), QStringLiteral("S-auto-extra"),
                                             QStringLiteral("S-atrans-extra")));

    // 仅当前行角色的 dataChanged（播放行推进）。
    facade.lyricsStub()->emitDataChanged(QModelIndex(), QModelIndex(), QList<int>{Seriona::App::LyricsModel::CurrentRole});
    QTest::qWait(400);

    // ① 列表未被重建（否则第 21 条会出现）。
    QCOMPARE(manualModel->property("count").toInt(), 20);
    // ② 视口没被打回。
    const qreal afterY = list->property("contentY").toReal();
    QVERIFY2(qAbs(afterY - scrolledY) <= 1.0,
             qPrintable(QStringLiteral("viewport was reset by dataChanged: %1 -> %2").arg(scrolledY).arg(afterY)));

    // 对照：结构性信号（modelReset）真的会重建 —— 证明上面的「没变」不是连接没生效。
    facade.lyricsStub()->emitModelReset();
    QTRY_COMPARE(manualModel->property("count").toInt(), 21);
}

QTEST_MAIN(LyricUiRuntimeTest)

#include "tst_lyric_ui_runtime.moc"
