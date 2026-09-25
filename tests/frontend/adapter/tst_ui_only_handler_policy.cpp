#include <QFile>
#include <QString>
#include <QtTest/QTest>

#include <initializer_list>

#ifndef SERIONA_SOURCE_DIR
#error "SERIONA_SOURCE_DIR must point to the repository source root"
#endif

namespace {

QString sourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(SERIONA_SOURCE_DIR) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qFatal("Failed to open source fixture: %s", qPrintable(relativePath));
    }

    return QString::fromUtf8(file.readAll());
}

void expectContains(const QString &source, const QString &needle)
{
    QVERIFY2(source.contains(needle), qPrintable(QStringLiteral("Missing expected handler policy: %1").arg(needle)));
}

void expectAbsent(const QString &source, const QString &needle)
{
    QVERIFY2(!source.contains(needle), qPrintable(QStringLiteral("Unexpected backend/fake handler policy: %1").arg(needle)));
}

void expectContainsAll(const QString &source, const std::initializer_list<const char *> &needles)
{
    for (const char *needle : needles) {
        expectContains(source, QString::fromUtf8(needle));
    }
}

void expectInOrder(const QString &source, const std::initializer_list<const char *> &needles)
{
    qsizetype previous = -1;
    for (const char *needle : needles) {
        const qsizetype current = source.indexOf(QString::fromUtf8(needle), previous + 1);
        QVERIFY2(current >= 0, qPrintable(QStringLiteral("Missing expected layout marker in order: %1").arg(QString::fromUtf8(needle))));
        QVERIFY2(current >= previous, qPrintable(QStringLiteral("Unexpected layout marker order: %1").arg(QString::fromUtf8(needle))));
        previous = current;
    }
}

// 截取 source 中 [startNeedle, endNeedle) 的文本（用于把单个菜单项/单个属性块隔离出来，
// 断言「该项不发命令」而不会被同文件其它项的命令调用污染）。
QString sliceBetween(const QString &source, const QString &startNeedle, const QString &endNeedle)
{
    const qsizetype start = source.indexOf(startNeedle);
    if (start < 0) {
        QTest::qFail(qPrintable(QStringLiteral("Missing slice start: %1").arg(startNeedle)), __FILE__, __LINE__);
        return {};
    }

    const qsizetype end = source.indexOf(endNeedle, start + startNeedle.size());
    if (end <= start) {
        QTest::qFail(qPrintable(QStringLiteral("Missing slice end '%1' after '%2'").arg(endNeedle, startNeedle)),
                     __FILE__, __LINE__);
        return {};
    }
    return source.mid(start, end - start);
}

QString menuBlock(const QString &source, const QString &label, const QString &nextLabel)
{
    const QString startNeedle = QStringLiteral("text: qsTr(\"%1\")").arg(label);
    const QString endNeedle = QStringLiteral("text: qsTr(\"%1\")").arg(nextLabel);
    const qsizetype start = source.indexOf(startNeedle);
    if (start < 0) {
        QTest::qFail(qPrintable(QStringLiteral("Missing menu item: %1").arg(label)), __FILE__, __LINE__);
        return {};
    }

    const qsizetype end = source.indexOf(endNeedle, start + startNeedle.size());
    if (end <= start) {
        QTest::qFail(qPrintable(QStringLiteral("Missing menu item after %1: %2").arg(label, nextLabel)), __FILE__, __LINE__);
        return {};
    }
    return source.mid(start, end - start);
}

void expectUnsupportedOnlySortAction(const QString &source, const QString &label, const QString &nextLabel)
{
    const QString block = menuBlock(source, label, nextLabel);
    expectContains(block, QStringLiteral("onTriggered: root.showUnsupportedFeedback(qsTr(\"%1\"))").arg(label));
    expectAbsent(block, QStringLiteral("libraryController."));
    expectAbsent(block, QStringLiteral("appFacade.scanLibrary"));
    expectAbsent(block, QStringLiteral("submitCommand"));
    expectAbsent(block, QStringLiteral("sort"));
}

QString inlineMenuItemBlock(const QString &source, const QString &label)
{
    const QString startNeedle = QStringLiteral("BubbleMenuItem { text: qsTr(\"%1\")").arg(label);
    const qsizetype start = source.indexOf(startNeedle);
    if (start < 0) {
        QTest::qFail(qPrintable(QStringLiteral("Missing inline menu item: %1").arg(label)), __FILE__, __LINE__);
        return {};
    }

    const qsizetype end = source.indexOf(QLatin1Char('\n'), start);
    if (end <= start) {
        QTest::qFail(qPrintable(QStringLiteral("Missing inline menu item terminator: %1").arg(label)), __FILE__, __LINE__);
        return {};
    }
    return source.mid(start, end - start);
}

void expectUnsupportedOnlyMainAction(const QString &source, const QString &label)
{
    const QString block = inlineMenuItemBlock(source, label);
    expectContains(block, QStringLiteral("onTriggered: root.showUnsupportedFeedback(qsTr(\"%1\"))").arg(label));
    expectAbsent(block, QStringLiteral("playbackController."));
    expectAbsent(block, QStringLiteral("lyricsState."));
    expectAbsent(block, QStringLiteral("libraryController."));
    expectAbsent(block, QStringLiteral("appFacade."));
    expectAbsent(block, QStringLiteral("exitRequested"));
    expectAbsent(block, QStringLiteral("submitCommand"));
}

}

class UiOnlyHandlerPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void uiOnlyHandlersDoNotUseBackendCommands();
    void qmlStartupDoesNotCallQtApplicationPropertyFunction();
    void qmlLayoutSourceContractsStayStable();
    void auxiliaryWindowLayeringContractsStayStable();
    void equalizerWindowSourceContractsStayStable();
    void lyricLineCorrectionMenuSourceContractsStayStable();
    void lyricSplitEditorWindowSourceContractsStayStable();
    void lyricCorrectionManagerSourceContractsStayStable();
};

void UiOnlyHandlerPolicyTest::uiOnlyHandlersDoNotUseBackendCommands()
{
    const QString mainQml = sourceFile(QStringLiteral("qml/Main.qml"));
    const QString mainContentQml = sourceFile(QStringLiteral("qml/views/MainContent.qml"));
    const QString sidebarQml = sourceFile(QStringLiteral("qml/components/Sidebar.qml"));
    const QString windowControlsQml = sourceFile(QStringLiteral("qml/components/WindowControls.qml"));

    expectContains(mainQml, QStringLiteral("window.startSystemMove()"));
    expectContains(mainQml, QStringLiteral("edgeFlag: Qt.TopEdge"));
    expectContains(mainQml, QStringLiteral("onCloseRequested: window.requestApplicationClose()"));
    expectContains(mainQml, QStringLiteral("onExitRequested: window.requestApplicationClose()"));
    expectAbsent(mainQml, QStringLiteral("submitCommand"));
    expectAbsent(mainQml, QStringLiteral("showUnsupportedFeedback(qsTr(\"Exit\"))"));

    expectContains(windowControlsQml, QStringLiteral("targetWindow.showMinimized()"));
    expectContains(windowControlsQml, QStringLiteral("targetWindow.showMaximized()"));
    expectContains(windowControlsQml, QStringLiteral("targetWindow.showNormal()"));
    expectContains(windowControlsQml, QStringLiteral("root.closeRequested()"));
    expectAbsent(windowControlsQml, QStringLiteral("appFacade"));
    expectAbsent(windowControlsQml, QStringLiteral("playbackController"));
    expectAbsent(windowControlsQml, QStringLiteral("libraryController"));
    expectAbsent(windowControlsQml, QStringLiteral("notifications"));

    // 更多设置菜单：四项平级（设置/均衡器/关于 Seriona/退出），纯 UI 处理，无后端命令；
    // 关于 Seriona 为真实主窗 overlay（T19），不再是 unsupported 占位反馈
    expectContains(mainContentQml, QStringLiteral("signal openSettingsRequested()"));
    expectContains(mainContentQml, QStringLiteral("signal openEqualizerRequested()"));
    expectContains(mainContentQml, QStringLiteral("text: qsTr(\"设置\")"));
    expectContains(mainContentQml, QStringLiteral("root.openSettingsRequested()"));
    expectContains(mainContentQml, QStringLiteral("text: qsTr(\"均衡器\")"));
    expectContains(mainContentQml, QStringLiteral("root.openEqualizerRequested()"));
    expectContains(mainContentQml, QStringLiteral("text: qsTr(\"关于 Seriona\")"));
    expectContains(mainContentQml, QStringLiteral("aboutOverlay.open()"));
    expectContains(mainContentQml, QStringLiteral("text: qsTr(\"退出\")"));
    expectContains(mainContentQml, QStringLiteral("root.exitRequested()"));
    expectAbsent(mainContentQml, QStringLiteral("showUnsupportedFeedback(qsTr(\"关于 Seriona\"))"));
    expectAbsent(mainContentQml, QStringLiteral("showUnsupportedFeedback(qsTr(\"退出\"))"));
    expectAbsent(mainContentQml, QStringLiteral("showUnsupportedFeedback(qsTr(\"均衡器\"))"));
    expectAbsent(mainContentQml, QStringLiteral("BubbleSubMenuItem"));
    expectAbsent(mainContentQml, QStringLiteral("淡入淡出"));
    expectAbsent(mainContentQml, QStringLiteral("无缝播放"));
    expectAbsent(mainContentQml, QStringLiteral("回放增益"));
    expectAbsent(mainContentQml, QStringLiteral("submitCommand"));

    expectContains(sidebarQml, QStringLiteral("function showUnsupportedFeedback(actionName)"));
    expectContains(sidebarQml, QStringLiteral("root.appFacade.notifications.showUnsupportedAction(actionName);"));
    expectContains(sidebarQml, QStringLiteral("function sortRulesForDialog()"));
    expectContains(sidebarQml, QStringLiteral("return currentRules.length > 0 ? currentRules : [{field: \"filename\", order: \"asc\"}];"));
    expectContains(sidebarQml, QStringLiteral("sortDialog.sortRules = root.sortRulesForDialog();"));
    expectContains(sidebarQml, QStringLiteral("libraryController.applySortRules(sortRules);"));
    expectContains(sidebarQml, QStringLiteral("libraryController.forceRescan();"));
    expectContains(sidebarQml, QStringLiteral("onAccepted: root.appFacade.scanLibrary(folder)"));
    expectAbsent(sidebarQml, QStringLiteral("sortBy"));
    expectAbsent(sidebarQml, QStringLiteral("setSort"));
    expectAbsent(sidebarQml, QStringLiteral("sortOrder"));
    expectAbsent(sidebarQml, QStringLiteral("submitCommand"));

    const QString startupViewQml = sourceFile(QStringLiteral("qml/views/StartupView.qml"));
    expectContains(startupViewQml, QStringLiteral("onClicked: root.appFacade.restorePlaylistFromStartup()"));
    expectContains(startupViewQml, QStringLiteral("if (root.appFacade.scanLibrary(folder))"));
}

void UiOnlyHandlerPolicyTest::qmlStartupDoesNotCallQtApplicationPropertyFunction()
{
    const QString mainQml = sourceFile(QStringLiteral("qml/Main.qml"));

    expectAbsent(mainQml, QStringLiteral("Qt.application.property("));
    expectContains(mainQml, QStringLiteral("property string smokeScenario"));
}

void UiOnlyHandlerPolicyTest::qmlLayoutSourceContractsStayStable()
{
    const QString themeQml = sourceFile(QStringLiteral("qml/theme/Theme.qml"));
    const QString mainQml = sourceFile(QStringLiteral("qml/Main.qml"));
    const QString mainContentQml = sourceFile(QStringLiteral("qml/views/MainContent.qml"));
    const QString sidebarQml = sourceFile(QStringLiteral("qml/components/Sidebar.qml"));
    const QString playlistDelegateQml = sourceFile(QStringLiteral("qml/components/PlaylistDelegate.qml"));
    const QString sortDialogQml = sourceFile(QStringLiteral("qml/components/SortDialog.qml"));
    const QString sortRuleRowQml = sourceFile(QStringLiteral("qml/components/SortRuleRow.qml"));
    const QString startupViewQml = sourceFile(QStringLiteral("qml/views/StartupView.qml"));

    expectContains(themeQml, QStringLiteral("readonly property int sidebarWidth: 350"));

    expectContainsAll(mainQml, {
        "width: 360",
        "height: 720",
        "minimumWidth: 360",
        "minimumHeight: 720",
        "readonly property int sidebarWidth: 350",
        "readonly property int playerMinWidth: 450",
        "Layout.preferredHeight: 40",
        "WindowControls {",
        "targetWindow: window",
        "onCloseRequested: window.requestApplicationClose()",
        "onPressed: window.startSystemMove()",
        "onPressed: window.startSystemResize(edgeFlag)",
        "Sidebar {",
        "MainContent {",
        "StartupView {"
    });
    expectInOrder(mainQml, {
        "Sidebar {",
        "MainContent {",
        "StartupView {"
    });

    expectContainsAll(mainContentQml, {
        "state: \"playback\"",
        "name: \"playback\"",
        "name: \"lyrics\"",
        "id: positionHelper",
        "width: 320",
        "id: coverContainer",
        "width: 240",
        "height: 240",
        "sourceSize.width: 240",
        "sourceSize.height: 240",
        "id: metadataContainer",
        "anchors.top: coverContainer.bottom",
        "anchors.horizontalCenter: positionHelper.horizontalCenter",
        "id: lyricsContainer",
        "anchors.top: coverContainer.bottom",
        "anchors.bottom: linearProgressContainer.top",
        "id: waveformProgressContainer",
        "id: linearProgressContainer",
        "id: controlsContainer",
        "id: volumeContainer",
        "id: bottomRowContainer",
        "id: toggleTranslationBtn",
        "PropertyChanges {",
        "target: coverContainer",
        "width: 44",
        "height: 44",
        "target: coverRect",
        "radius: 12",
        "target: coverIcon",
        "scale: 20 / 72"
    });
    expectInOrder(mainContentQml, {
        "id: positionHelper",
        "id: coverContainer",
        "id: metadataContainer",
        "id: lyricsContainer",
        "id: waveformProgressContainer",
        "id: linearProgressContainer",
        "id: controlsContainer",
        "id: volumeContainer",
        "id: bottomRowContainer",
        "id: toggleTranslationBtn"
    });

    expectContainsAll(sidebarQml, {
        "width: Theme.sidebarWidth",
        "id: playlistView",
        "ScrollBar.vertical: StyledScrollBar {}",
        "readonly property ScrollBar verticalScrollBar: playlistView.ScrollBar.vertical",
        "id: sidebarFolderDialog",
        "id: fab"
    });
    expectInOrder(sidebarQml, {
        "id: playlistView",
        "ScrollBar.vertical: StyledScrollBar {}",
        "id: sidebarFolderDialog",
        "id: fab"
    });

    // 三态滚动条规格已收敛到共享组件 StyledScrollBar（唯一真源）；
    // 结构断言跟随规格转移，防止组件内规格回退/删除。竖/横两向共用：厚轴按
    // orientation 取 width（竖向默认语义）/ height（横向），hover/pressed 6→10
    // 加宽表达式与 AsNeeded 隐藏语义为两向共享规格，均须保持锁定。
    const QString styledScrollBarQml = sourceFile(QStringLiteral("qml/components/StyledScrollBar.qml"));
    expectContainsAll(styledScrollBarQml, {
        "ScrollBar {",
        "policy: ScrollBar.AsNeeded",
        "readonly property bool isHoveredOrPressed: control.hovered || control.pressed",
        "readonly property bool verticalBar: orientation === Qt.Vertical",
        "width: verticalBar ? (isHoveredOrPressed ? 10 : Theme.scrollbarWidth) : undefined",
        "height: verticalBar ? undefined : (isHoveredOrPressed ? 10 : Theme.scrollbarWidth)",
        "visible: control.size < 1.0",
        "radius: width / 2",
        "color: control.pressed ? Theme.pressedColor"
    });

    expectContainsAll(playlistDelegateQml, {
        "Accessible.role: Accessible.ListItem",
        "Accessible.name: isFolder ? name : title",
        "ItemDelegate {",
        "height: 72",
        "Layout.preferredWidth: 44",
        "Layout.preferredHeight: 44",
        "sourceSize.width: delegate.isFolder ? 24 : 44",
        "sourceSize.height: delegate.isFolder ? 24 : 44"
    });

    expectContainsAll(sortRuleRowQml, {
        "id: fieldCombo",
        "readonly property real fullListHeight: contentItem.implicitHeight + topPadding + bottomPadding",
        "readonly property real comboTopInWindow: fieldCombo.mapToItem(null, 0, 0).y",
        "readonly property real preferredY: fieldCombo.height",
        "readonly property real windowTopLimit: margins",
        "readonly property real windowBottomLimit: fieldCombo.Window.window ? fieldCombo.Window.window.height - margins : comboTopInWindow + preferredY + fullListHeight",
        "readonly property real minY: windowTopLimit - comboTopInWindow",
        "readonly property real maxY: windowBottomLimit - comboTopInWindow - fullListHeight",
        "height: fullListHeight",
        "margins: 8",
        "padding: 4",
        "y: Math.max(minY, Math.min(preferredY, maxY))"
    });

    expectContainsAll(sortDialogQml, {
        "readonly property int maxSortRules: 5",
        "readonly property int ruleRowHeight: 44",
        "readonly property int ruleSpacing: 12",
        "readonly property int dialogChromeHeight: 148",
        "readonly property int rulesAreaHeight: maxSortRules * ruleRowHeight + (maxSortRules - 1) * ruleSpacing",
        "readonly property int addRuleButtonY: (maxSortRules - 1) * (ruleRowHeight + ruleSpacing)",
        "height: rulesAreaHeight + dialogChromeHeight",
        "height: root.rulesAreaHeight",
        "y: index * (root.ruleRowHeight + root.ruleSpacing)",
        "y: root.addRuleButtonY",
        "visible: root.sortRules.length < root.maxSortRules"
    });

    expectContainsAll(startupViewQml, {
        "width: Math.min(parent.width - Theme.paddingLarge * 2, 320)",
        "Layout.preferredWidth: 200",
        "Layout.preferredHeight: 200",
        "Layout.fillWidth: true",
        "id: restoreButton",
        "id: addFolderButton"
    });
}

// F2.6 实况契约：EqualizerWindow.qml（图谱区 T10 由 Canvas 重构为 SpectrumGraph +
// QML 静态刻度覆盖层后）不再是占位窗口。
// 本函数以源文本静态断言锁住 EQ 窗口 objectName 面 + 关键行为接线 + 防回归项。
// 文件机制边界：本测试无法实例化 QML/访问 settings（GUILESS + 纯文本契约），
// 运行时行为（点击 → settings 翻转 → Repeater 重建、图谱状态机序列）由 settings-menu
// smoke 的窗内交互步骤动态覆盖（见 Main.qml smokeTimer step3-8）。
void UiOnlyHandlerPolicyTest::equalizerWindowSourceContractsStayStable()
{
    const QString eqWindowQml = sourceFile(QStringLiteral("qml/windows/EqualizerWindow.qml"));
    const QString bandSliderQml = sourceFile(QStringLiteral("qml/components/EqBandSlider.qml"));
    const QString presetDialogQml = sourceFile(QStringLiteral("qml/windows/EqualizerPresetDialog.qml"));

    // —— objectName 契约面（实测全部存在，供 smoke/无障碍/自动化按名定位）——
    // 工具行 / 图谱区 / 控制条 / GEQ 竖条区 / 弹层：eqBandSliderN 为 Repeater 随档重建
    expectContainsAll(eqWindowQml, {
        "objectName: \"equalizerWindow\"",
        "objectName: \"eqMode10Button\"",
        "objectName: \"eqMode31Button\"",
        "objectName: \"eqMasterSwitch\"",
        "objectName: \"eqResetButton\"",
        "objectName: \"eqSpectrumSwitch\"",
        "objectName: \"eqGraphCanvas\"",
        "objectName: \"eqGraphEmptyHint\"",
        "objectName: \"eqLimiterSwitch\"",
        "objectName: \"eqPresetApplyButton\"",
        "objectName: \"eqSavePresetButton\"",
        "objectName: \"eqManagePresetsButton\"",
        "objectName: \"eqPreGainSlider\"",
        "objectName: \"eqPresetPickerMenu\"",
        "objectName: \"eqSavePresetPopup\"",
        "objectName: \"eqSaveNameInput\"",
        "objectName: \"eqBandSlider\" + index"
    });

    // —— settings 解析契约（动态上下文解析 Main 的 appFacade；无则 null 只读降级）——
    expectContainsAll(eqWindowQml, {
        "readonly property var settings: _resolveSettings()",
        "function _resolveSettings()",
        "typeof appFacade",
        "return appFacade.settings;",
        "return null;"
    });

    // —— 行为接线：总开关/10/31 切档写 settings 离散项；Repeater 竖条数随档位重建 ——
    expectContainsAll(eqWindowQml, {
        "if (root.settings)",
        "root.settings.enabled = checked",
        "root.settings.bandMode = 10;",
        "root.settings.bandMode = 31;",
        "readonly property int _bandMode: (settings && settings.bandMode === 31) ? 31 : 10",
        "readonly property int _bandCount: _bandMode === 31 ? 31 : 10",
        "model: root._bandCount",
        "width: root._bandCount * 66 + (root._bandCount - 1) * spacing",
        "root.settings[key] = list;"
    });

    // —— T10 图谱区接线契约（Canvas → SpectrumGraph；objectName eqGraphCanvas 保留在
    // 新 item 上 → 既有锁定零改）：数据/颜色注入 + 频谱开关联动柱区 + 拖动写回胶水 +
    // 空态三态状态机文案；旧 Canvas 绘制链（Canvas 类型/requestPaint/drawGraph）零残留
    expectContainsAll(eqWindowQml, {
        "SpectrumGraph {",
        "spectrumBins: root.settings ? root.settings.spectrumBins : []",
        "spectrumBarsVisible: root.settings ? root.settings.spectrumEnabled : true",
        "preGainDb: root.settings ? root.settings.preGainDb : 0",
        "onDragReleased: (bandMode, bandIndex, gainDb) => {",
        "var key = bandMode === 31 ? \"bandGains31\" : \"bandGains10\";",
        "visible: spectrumGraph.dragBandIndex >= 0",
        "qsTr(\"播放音频后显示实时频响\")",
        "qsTr(\"频谱已关闭\")",
        "qsTr(\"暂无频谱数据\")"
    });
    expectAbsent(eqWindowQml, QStringLiteral("requestPaint"));
    expectAbsent(eqWindowQml, QStringLiteral("Canvas {"));
    expectAbsent(eqWindowQml, QStringLiteral("drawGraph"));

    // —— Esc 关闭路径（contentRect 焦点链）——
    expectContainsAll(eqWindowQml, {
        "Keys.onEscapePressed",
        "root.close()"
    });

    // —— 预设管理：Loader 惰性激活 + 显式 settings 注入 + 弹层对象契约 ——
    expectContainsAll(eqWindowQml, {
        "id: presetDialogLoader",
        "active: false",
        "sourceComponent: Component {",
        "EqualizerPresetDialog {",
        "settings: root.settings",
        "function openManagePresets()"
    });
    // —— R6 预设弹层修复契约：二次点击只关闭不重开（CloseOnPressOutsideParent +
    //    visible 判据）、圆角白底清除（background:null）、与设置菜单同款进出场过渡 ——
    expectContainsAll(eqWindowQml, {
        "function togglePresetPicker()",
        "if (presetMenu.visible)",
        "closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent",
        "parent: presetApplyButton",
        "background: null",
        "enter: Transition",
        "exit: Transition"
    });
    expectContainsAll(presetDialogQml, {
        "objectName: \"equalizerPresetDialog\"",
        "objectName: \"equalizerPresetDialogClose\"",
        "closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside"
    });
    // —— R6 预设行可见性修复契约：预设行必须带 kind 标记（delegate 的
    //    visible: modelData.kind === "preset" 判定依赖它；C++ 源行无该字段）——
    expectContainsAll(presetDialogQml, {
        "function makePresetRow(item)",
        "Object.assign({ kind: \"preset\" }, item)",
        "modelData.kind === \"preset\""
    });

    // —— 防回归：EQ 域无占位反馈/无后端命令直发；竖条根 value 纯外部可写（B1）——
    expectAbsent(eqWindowQml, QStringLiteral("submitCommand"));
    expectAbsent(eqWindowQml, QStringLiteral("showUnsupportedFeedback"));
    expectContainsAll(bandSliderQml, {
        "signal userEdited(real newValue)",
        "target: bandSlider",
        "Math.max(root._gainMin, Math.min(root._gainMax, root.value))",
        "root.userEdited(bandSlider.value)",
        "root.userEdited(grid)"
    });
    expectAbsent(bandSliderQml, QStringLiteral("root.value ="));
}

// 窗口层级契约（用户实测修复）：设置/均衡器/歌曲详情三个辅助窗口取消"应用程序内顶层"——
// ① 设置窗 x/y 绑定移除（旧绑定让主窗口移动时设置窗瞬移回打开时相对位置并跟随），
//    改首次打开一次性居中；
// ② 三窗口显式 transientParent: null 解除 Qt 对嵌套 Window 的自动 transient 关联
//    （Windows 下即"所有者窗口"——永远压在主窗口之上），设置窗另移除系统级
//    WindowStaysOnTopHint；主窗口可覆盖它们；
// ③ 解除 transient 后辅助窗不再参与 lastWindowClosed 判定，主窗关闭链路补 Qt.quit()。
void UiOnlyHandlerPolicyTest::auxiliaryWindowLayeringContractsStayStable()
{
    const QString mainQml = sourceFile(QStringLiteral("qml/Main.qml"));
    const QString settingsWindowQml = sourceFile(QStringLiteral("qml/windows/SettingsWindow.qml"));
    const QString trackContextMenuQml = sourceFile(QStringLiteral("qml/components/TrackContextMenu.qml"));

    // ① 设置窗：一次性居中（打开处理器）+ 旧 x/y 绑定零残留
    expectContainsAll(mainQml, {
        "settingsWindow.x = window.x + (window.width - settingsWindow.width) / 2",
        "settingsWindow.y = window.y + (window.height - settingsWindow.height) / 2",
        "if (!settingsWindow.visible) {"
    });
    expectAbsent(mainQml, QStringLiteral("x: window.x + (window.width - width) / 2"));
    expectAbsent(mainQml, QStringLiteral("y: window.y + (window.height - height) / 2"));

    // ② 三窗口解除自动 transient 关联：设置窗/均衡器窗实例各一处（按声明顺序锁定）
    expectInOrder(mainQml, {
        "id: settingsWindow",
        "transientParent: null",
        "id: equalizerWindow",
        "transientParent: null"
    });
    // 设置窗 flags：无系统级置顶（锁定旧 flags 行不回归；注释中提及标志名属文档）
    expectContains(settingsWindowQml, QStringLiteral("flags: Qt.Dialog | Qt.FramelessWindowHint"));
    expectAbsent(settingsWindowQml,
                 QStringLiteral("flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint"));
    // 详情窗：解除自动关联；删除确认对话框保持 transient 注入（对话框语义不变）
    expectContainsAll(trackContextMenuQml, {
        "transientParent: null",
        "transientParent: root.Window.window"
    });

    // ③ 主窗关闭链路：显式退出（辅助窗不再参与 lastWindowClosed 判定）
    expectContains(mainQml, QStringLiteral("Qt.quit();"));
}

// 歌词行右键菜单（W3/D14）源契约：四项齐备 + 前三项走控制命令 + 第 4 项只读接
// autoOriginal/autoTranslation（不接当前值）+ 右键不 seek 且左键 seek/手势 park 不回归。
// 文件机制边界：GUILESS 纯文本断言；运行时交互（真实右键弹菜单、命令外发、快照时机③）
// 由 C++ 侧 backend_bridge / lyrics_model 用例覆盖，交互本身由终局波 F3 人工 QA 覆盖（本 todo 选 (C)）。
void UiOnlyHandlerPolicyTest::lyricLineCorrectionMenuSourceContractsStayStable()
{
    const QString mainContentQml = sourceFile(QStringLiteral("qml/views/MainContent.qml"));
    const QString menuQml = sourceFile(QStringLiteral("qml/components/LyricLineContextMenu.qml"));
    const QString mainQml = sourceFile(QStringLiteral("qml/Main.qml"));

    // 右键接入：显式纳入右键，且右键分支在 seek 之前 return（右键不 seek）。
    expectInOrder(mainContentQml, {
        "acceptedButtons: Qt.LeftButton | Qt.RightButton",
        "if (mouse.button === Qt.RightButton) {",
        "root.openLyricLineContextMenu(delegateItem, mouse);",
        "return;",
        "root.playbackController.seek(delegateItem.timestampSec);"
    });

    // 左键 seek 与手势 park 行为保持不变（回归锁定）。
    expectContainsAll(mainContentQml, {
        "onFlickStarted: parkLyricsFollow()",
        "onDragStarted: parkLyricsFollow()",
        "onMovementStarted: parkLyricsFollow()",
        "required property bool manualOverride",
        "required property string autoOriginal",
        "required property string autoTranslation",
        "required property string rawLine",
        "LyricLineContextMenu {",
        "appFacade: root.appFacade"
    });
    expectContains(mainQml, QStringLiteral("appFacade: window.appFacade"));

    // D14 四项齐备（文本逐字）。
    expectContainsAll(menuQml, {
        "text: qsTr(\"修正原文/译文\")",
        "text: qsTr(\"此行无译文\")",
        "text: qsTr(\"恢复本行自动识别\")",
        "text: qsTr(\"查看本行自动判定\")"
    });

    // 前三项分支：修正 → 输入弹窗；此行无译文 → Upsert（译文显式空串）；
    // 恢复 → Remove；三项均经 appFacade 控制命令（不直连 DB）。
    expectContainsAll(menuQml, {
        "correctionDialog.openFor(root.lineData);",
        "root.appFacade.upsertLyricSplitCorrection(root.lineData.rawLine,",
        "root.lineData.displayLine,",
        "root.appFacade.removeLyricSplitCorrection(root.lineData.rawLine);"
    });

    // 第 4 项：只读弹窗，且该分支【不】调用 appFacade / 不 submitCommand。
    // A1 修复：autoJudgeDialog 是 Window，只有 show() 没有 open()；此前断言
    // autoJudgeDialog.open() 是把运行时 TypeError 逐字断言为正确（S19 A5）。
    const QString autoJudgeItem = sliceBetween(menuQml,
                                               QStringLiteral("objectName: \"lyricAutoJudgementItem\""),
                                               QStringLiteral("// 修正原文/译文：真实用户输入"));
    expectContains(autoJudgeItem, QStringLiteral("autoJudgeDialog.show();"));
    expectAbsent(autoJudgeItem, QStringLiteral("autoJudgeDialog.open();"));
    expectAbsent(autoJudgeItem, QStringLiteral("appFacade"));
    expectAbsent(autoJudgeItem, QStringLiteral("upsertLyricSplitCorrection"));
    expectAbsent(autoJudgeItem, QStringLiteral("removeLyricSplitCorrection"));

    // A2 修复：两个弹窗的居中基准必须是 Window 自己的 transientParent。
    // 本组件根是 Item（没有 transientParent 属性），写成 root.transientParent 会让
    // 三元条件恒假 → x/y 恒 0（居中成为死代码）。逐字锁定不得回归。
    expectAbsent(menuQml, QStringLiteral("root.transientParent ?"));
    expectContainsAll(menuQml, {
        "x: correctionDialog.transientParent ?",
        "y: correctionDialog.transientParent ?",
        "x: autoJudgeDialog.transientParent ?",
        "y: autoJudgeDialog.transientParent ?"
    });

    // A6 修复：行级「修正原文/译文」保存必须与拖动路径同一口径 —— 空原文（含全空白）
    // 不提交、不关窗、就地给出原因；提交经门函数而非直发 upsert。
    expectContainsAll(menuQml, {
        "function saveFromFields()",
        "root.appFacade.isLyricOriginalSubmittable(originalField.text)",
        "originalError = qsTr(\"原文不能为空（含全空白）\")",
        "root.appFacade.commitLyricSplitCorrection(rawLine,",
        "onClicked: correctionDialog.saveFromFields()"
    });

    // 第 4 项【不接当前值】：只读展示绑定必须取被覆盖前的 autoOriginal/autoTranslation；
    // 若改成 displayLine/translation（当前值），本断言失败。
    expectContainsAll(menuQml, {
        "lineIsManual ? (lineData.autoOriginal || \"\") : (lineData.displayLine || \"\")",
        "lineIsManual ? (lineData.autoTranslation || \"\") : (lineData.translation || \"\")",
        "qsTr(\"当前显示的就是自动结果\")"
    });
    expectAbsent(menuQml, QStringLiteral("submitCommand"));
}

// 整首纠错窗口（todo 33）的源契约：入口用户可达、打开即定格、分界走文本度量、
// 保存经 appFacade 控制命令，且不引入 karaoke 光标定位 / DB / FS。
void UiOnlyHandlerPolicyTest::lyricSplitEditorWindowSourceContractsStayStable()
{
    const QString mainContentQml = sourceFile(QStringLiteral("qml/views/MainContent.qml"));
    const QString mainQml = sourceFile(QStringLiteral("qml/Main.qml"));
    const QString editorQml = sourceFile(QStringLiteral("qml/windows/LyricSplitEditorWindow.qml"));

    expectContainsAll(mainContentQml, {
        "signal openLyricSplitEditorRequested()",
        "text: qsTr(\"整首纠错\")",
        "root.openLyricSplitEditorRequested()"
    });
    expectContainsAll(mainQml, {
        "LyricSplitEditorWindow {",
        "onOpenLyricSplitEditorRequested:",
        "lyricSplitEditorWindow.openEditor()"
    });

    // 打开时定格：openEditor() 一次性把 lyrics.lines() 快照进 ListModel（非持续绑定）。
    expectContainsAll(editorQml, {
        "objectName: \"lyricSplitEditorWindow\"",
        "function openEditor()",
        "appFacade.lyrics.lines()",
        "frozenModel.clear()",
        "frozenModel.append("
    });

    // 分界像素定位走 Qt 文本度量（TextMetrics.advanceWidth），不硬编码字宽。
    expectContainsAll(editorQml, {
        "TextMetrics {",
        "boundaryMetrics.advanceWidth"
    });

    // 初始分界由 C++ 反解（复现当前展示对），QML 里没有任何分隔符字形参与切分。
    expectContainsAll(editorQml, {
        "appFacade.lyricSplitBoundaryCut(raw, shown, trans)",
        "row.touched !== true",
        "frozenModel.setProperty(rowRect.index, \"touched\", true)"
    });
    expectAbsent(editorQml, QStringLiteral("\" / \""));
    expectAbsent(editorQml, QStringLiteral("\"/\""));

    // A4 修复：保存门必须对**拖动结果**判定（同一 Q_INVOKABLE 门），不得再用打开时那个
    // 展示对的 reproducible 标志 —— 后者对「译文来自其它行的参照行」必然为假，
    // 会让整类行即使拖动也永远无法保存。逐字锁定可保存性判定的唯一来源。
    expectContainsAll(editorQml, {
        "function commitAllowed(row)",
        "root.appFacade.lyricSplitBoundaryCommitAllowed(row.rawLine, row.boundaryIndex,",
        "root.commitAllowed(selectedRow)",
        "if (!root.commitAllowed(row))"
    });
    expectAbsent(editorQml, QStringLiteral("selectedRow.reproducible === true"));
    expectAbsent(editorQml, QStringLiteral("row.reproducible !== true"));

    // 保存经 appFacade 控制命令（内容寻址写 manual），不直连 DB/FS、不做 karaoke 光标定位。
    expectContainsAll(editorQml, {
        "commitLyricSplitBoundary(row.rawLine, row.boundaryIndex,"
    });
    expectAbsent(editorQml, QStringLiteral("submitCommand"));
    expectAbsent(editorQml, QStringLiteral("positionAt"));
    expectAbsent(editorQml, QStringLiteral("QSql"));
    expectAbsent(editorQml, QStringLiteral("QFile"));
}

// 纠错管理列表（todo 34）的源契约：范围 = 当前曲目、数据取自 lyrics.lines() 的 manual 行、
// 逐行外发 Remove、批量不碰全库清理；auto 判定槽接被覆盖前的 autoOriginal/autoTranslation。
// 运行时交互（真实点「恢复本行/恢复自动识别」）由 tst_lyric_ui_runtime 的实例化用例覆盖。
void UiOnlyHandlerPolicyTest::lyricCorrectionManagerSourceContractsStayStable()
{
    const QString settingsQml = sourceFile(QStringLiteral("qml/windows/SettingsWindow.qml"));
    const QString managerQml = sourceFile(QStringLiteral("qml/components/LyricCorrectionManager.qml"));
    const QString cmakeLists = sourceFile(QStringLiteral("CMakeLists.txt"));
    const QString gateScript = sourceFile(QStringLiteral("scripts/verify-middle-layer.sh"));
    const QString artworkTest = sourceFile(QStringLiteral("tests/frontend/adapter/tst_artwork_transition.cpp"));

    // 入口：设置面板内实例化，注入 root.appFacade，打开设置时刷新（切曲目后列表跟随当前曲目）。
    expectContainsAll(settingsQml, {
        "LyricCorrectionManager {",
        "objectName: \"lyricCorrectionManager\"",
        "appFacade: root.appFacade",
        "correctionManager.refresh();"
    });

    // 数据来源 = lyrics.lines() 的 manual 行；范围过滤在 refresh 里显式写死。
    expectContainsAll(managerQml, {
        "appFacade.lyrics.lines()",
        "row.manualOverride !== true",
        "function refresh()"
    });

    // 单条删除：键取 rawLine（回传键），经 removeLyricSplitCorrection 外发。
    // F3：与 restoreAll 同口径——空键不外发（后端会拒绝），故守卫与调用点一并锁定。
    expectContainsAll(managerQml, {
        "function removeRow(rawLine)",
        "const key = (rawLine !== undefined && rawLine !== null) ? String(rawLine) : \"\";",
        "if (key.length === 0)",
        "root.appFacade.removeLyricSplitCorrection(key);"
    });

    // 批量「恢复自动识别」：逐行发同一个命令（键取自该行 rawLine），不走全库清理；
    // 空 rawLine 无有效回传键，跳过不发（见 F6）。
    expectContainsAll(managerQml, {
        "function restoreAll()",
        "const key = (row.rawLine !== undefined && row.rawLine !== null) ? String(row.rawLine) : \"\";",
        "if (key.length === 0)",
        "root.appFacade.removeLyricSplitCorrection(key);"
    });

    // 列表不订阅高频的 dataChanged（它只订阅结构性变化）：整份重建挂上去会在播放行推进时
    // 反复打回滚动位置（见 F4）。运行期由 seriona_frontend_lyric_correction_scroll_kept 覆盖。
    expectAbsent(managerQml, QStringLiteral("onDataChanged"));

    // delegate 的按钮调用点也必须传 rawLine（回传键），不能传展示值 ——
    // 单条删除的键往返是「删除静默失效」唯一的可判负面（运行期用例走的是
    // removeRow 直调，覆盖不到这一行接线）。
    expectContains(managerQml, QStringLiteral("onClicked: root.removeRow(rowRect.rawLine)"));

    // 每条的自动判定可对照：auto 槽接被覆盖前的 autoOriginal/autoTranslation，不是当前值。
    const QString autoTextBlock = sliceBetween(managerQml,
                                               QStringLiteral("objectName: \"lyricCorrectionAutoText\""),
                                               QStringLiteral("objectName: \"lyricCorrectionRemoveButton\""));
    expectContainsAll(autoTextBlock, {
        "rowRect.autoOriginal",
        "rowRect.autoTranslation"
    });
    expectAbsent(autoTextBlock, QStringLiteral("rowRect.original"));
    expectAbsent(autoTextBlock, QStringLiteral("rowRect.translation"));

    // 边界：不调用全库读命令、不走全库清理、不直连 DB/FS、不重引入分隔符切分。
    expectAbsent(managerQml, QStringLiteral("listManual"));
    expectAbsent(managerQml, QStringLiteral("clearManual"));
    expectAbsent(managerQml, QStringLiteral("submitCommand"));
    expectAbsent(managerQml, QStringLiteral("QSql"));
    expectAbsent(managerQml, QStringLiteral("QFile"));
    expectAbsent(managerQml, QStringLiteral("\" / \""));
    expectAbsent(managerQml, QStringLiteral("\"/\""));
    // P9：不给纠错表设上限，也不在列表里自动清理（不擅自 remove 模型行）。
    expectAbsent(managerQml, QStringLiteral("splice"));
    expectAbsent(managerQml, QStringLiteral("manualModel.remove("));

    // 新组件必须在三处登记，否则门禁/测试模块会漏掉它。
    expectContains(cmakeLists, QStringLiteral("qml/components/LyricCorrectionManager.qml"));
    expectContains(gateScript, QStringLiteral("qml/components/LyricCorrectionManager.qml"));
    expectContains(artworkTest, QStringLiteral("\"LyricCorrectionManager\""));
}

QTEST_GUILESS_MAIN(UiOnlyHandlerPolicyTest)

#include "tst_ui_only_handler_policy.moc"
