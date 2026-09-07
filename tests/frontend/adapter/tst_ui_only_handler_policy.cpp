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
    void equalizerWindowSourceContractsStayStable();
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

// F2.6 实况契约：EqualizerWindow.qml（1153 行全实现）不再是占位窗口。
// 本函数以源文本静态断言锁住 EQ 窗口 objectName 面 + 关键行为接线 + 防回归项。
// 文件机制边界：本测试无法实例化 QML/访问 settings（GUILESS + 纯文本契约），
// 运行时行为（点击 → settings 翻转 → Repeater 重建）由 settings-menu smoke 的
// 窗内交互步骤动态覆盖（见 Main.qml smokeTimer step3/step4）。
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
    expectContainsAll(presetDialogQml, {
        "objectName: \"equalizerPresetDialog\"",
        "objectName: \"equalizerPresetDialogClose\"",
        "closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside"
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

QTEST_GUILESS_MAIN(UiOnlyHandlerPolicyTest)

#include "tst_ui_only_handler_policy.moc"
