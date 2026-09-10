import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Qt5Compat.GraphicalEffects
import QtCore
import Seriona

Window {
    id: window
    width: 360
    height: 720
    minimumWidth: 360
    minimumHeight: 720
    visible: true
    title: qsTr("Seriona Music Player")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    readonly property int sidebarWidth: 350
    readonly property int playerMinWidth: 450
    readonly property bool isDockCapable: width >= (sidebarWidth + playerMinWidth)
    property string smokeScenario: ""
    property bool smokeLoggingEnabled: false
    property bool layoutAnimEnabled: true

    readonly property AppFacade appFacade: AppFacade {}
    readonly property var navigationController: appFacade.navigation
    property bool shutdownRequested: false

    // smoke 调试日志统一出口：仅在非 Release 构建且启用 smoke 模式时输出，
    // 由 C++ 侧 main.cpp 注入 smokeLoggingEnabled 控制（NDEBUG 下恒为 false）。
    function smokeLog(message) {
        if (window.smokeLoggingEnabled)
            console.log(message);
    }

    function requestApplicationClose() {
        if (shutdownRequested)
            return;

        shutdownRequested = true;
        close();
    }

    function applySmokeScenario(scenario) {
        smokeLog("[smoke] applySmokeScenario called with: " + scenario);
        if (scenario === "" || scenario === "startup")
            return;

        navigationController.restorePlaylistFromStartup();

        if (scenario === "lyrics") {
            navigationController.showLyricsView();
            return;
        }

        if (scenario === "sidebar-tree") {
            if (!navigationController.sidebarOpen)
                navigationController.toggleSidebar();
            appFacade.library.focusedNodeId = "favorites";
            appFacade.library.playingTrackId = "track-aurora";
            sidebarSmokeTimer.start();
            return;
        }

        if (scenario === "empty-library") {
            appFacade.library.searchQuery = "__seriona_empty_smoke__";
            return;
        }

        if (scenario === "settings-menu") {
            navigationController.showPlaybackView();
            mainContent.openMenuForSmoke();
            smokeLog("[smoke] mainMenu opened");
            smokeTimer.step = 0;
            smokeTimer.start();
            return;
        }

        navigationController.showPlaybackView();
    }

    function smokeTextItems(item) {
        var result = [];
        if (!item || !item.visible)
            return result;

        if (item.text !== undefined && item.text !== "") {
            result.push({
                "text": String(item.text),
                "x": Math.round(item.mapToItem(window.contentItem, 0, 0).x),
                "y": Math.round(item.mapToItem(window.contentItem, 0, 0).y),
                "width": Math.round(item.width || 0),
                "height": Math.round(item.height || 0),
                "paintedWidth": Math.round(item.paintedWidth || 0),
                "paintedHeight": Math.round(item.paintedHeight || 0),
                "truncatedRisk": item.paintedWidth !== undefined && item.width !== undefined && item.paintedWidth > item.width + 1
            });
        }

        var children = item.children || [];
        for (var i = 0; i < children.length; ++i)
            result = result.concat(smokeTextItems(children[i]));
        return result;
    }

    // smoke 窗内交互辅助：按 objectName 定位 / 计数（settings-menu EQ 断言用；零等待，仅遍历对象树）
    function smokeFindByObjectName(root, targetName) {
        var seen = [];
        var stack = [root];
        while (stack.length > 0) {
            var cur = stack.pop();
            if (cur === undefined || cur === null)
                continue;
            if (seen.indexOf(cur) !== -1)
                continue;
            seen.push(cur);
            if (cur.objectName !== undefined && String(cur.objectName) === targetName)
                return cur;
            // Popup 经 Overlay 重挂后不在 item 树内，但实例仍可从宿主 Loader 的 item 引用取到
            if (cur.item !== undefined && cur.item !== null
                    && String(cur.item.objectName) === targetName)
                return cur.item;
            var toVisit = [];
            if (cur.contentItem !== undefined && cur.contentItem !== null)
                toVisit.push(cur.contentItem);
            var data = cur.data;
            if (data !== undefined) {
                for (var i = 0; i < data.length; ++i)
                    toVisit.push(data[i]);
            }
            for (var j = toVisit.length - 1; j >= 0; --j)
                stack.push(toVisit[j]);
        }
        return null;
    }

    function smokeCountByObjectNamePrefix(root, namePrefix) {
        var count = 0;
        var seen = [];
        var stack = [root];
        while (stack.length > 0) {
            var cur = stack.pop();
            if (cur === undefined || cur === null)
                continue;
            if (seen.indexOf(cur) !== -1)
                continue;
            seen.push(cur);
            var name = cur.objectName;
            if (name !== undefined && String(name).indexOf(namePrefix) === 0)
                ++count;
            var toVisit = [];
            if (cur.contentItem !== undefined && cur.contentItem !== null)
                toVisit.push(cur.contentItem);
            var data = cur.data;
            if (data !== undefined) {
                for (var i = 0; i < data.length; ++i)
                    toVisit.push(data[i]);
            }
            for (var j = toVisit.length - 1; j >= 0; --j)
                stack.push(toVisit[j]);
        }
        return count;
    }

    function smokeVisualStateJson() {
        var state = {
            "scenario": window.smokeScenario,
            "window": {
                "width": window.width,
                "height": window.height,
                "startupScreenVisible": navigationController.startupScreenVisible,
                "currentView": navigationController.currentView,
                "sidebarOpen": navigationController.sidebarOpen
            },
            "library": {
                "focusedNodeId": appFacade.library.focusedNodeId,
                "playingTrackId": appFacade.library.playingTrackId,
                "searchQuery": appFacade.library.searchQuery,
                "visibleNodeCount": appFacade.library.visibleNodeCount,
                "scanStatus": appFacade.library.scanStatus,
                "lastError": appFacade.library.lastError
            },
            "playback": {
                "songTitle": appFacade.playback.songTitle,
                "artistName": appFacade.playback.artistName,
                "albumName": appFacade.playback.albumName,
                "isPlaying": appFacade.playback.isPlaying
            },
            "texts": smokeTextItems(window.contentItem)
        };
        return JSON.stringify(state, null, 2);
    }

    Component.onCompleted: applySmokeScenario(window.smokeScenario)

    onClosing: function (closeEvent) {
        shutdownRequested = true;
        appFacade.shutdown();
    }

    Timer {
        id: animResetTimer
        interval: 50
        onTriggered: window.layoutAnimEnabled = true
    }

    Timer {
        id: manualAnimResetTimer
        interval: 310
        onTriggered: window.navigationController.clearManualSidebarToggle()
    }

    onIsDockCapableChanged: {
        layoutAnimEnabled = false;
        navigationController.syncSidebarForDockCapability(isDockCapable);
        animResetTimer.restart();
    }

    // 遮罩形状
    Rectangle {
        id: rectMask
        anchors.fill: parent
        radius: window.visibility === Window.Maximized ? 0 : Theme.spacing24
        visible: false
    }

    // 主内容容器
    Item {
        id: windowContent
        anchors.fill: parent
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: rectMask
        }

        // Global Drag Area
        MouseArea {
            anchors.fill: parent
            z: -1
            acceptedButtons: Qt.LeftButton
            onPressed: window.startSystemMove()
        }

        DynamicBackground {
            anchors.fill: parent
            playbackController: window.appFacade.playback
        }

        // Sidebar Container
        Sidebar {
            id: sidebarContainer
            height: parent.height
            z: 500
            visible: !window.navigationController.startupScreenVisible
            isDockCapable: window.isDockCapable
            isSidebarOpen: window.navigationController.sidebarOpen
            libraryController: window.appFacade.library
            appFacade: window.appFacade
            x: isSidebarOpen ? 0 : -width

            onCloseClicked: {
                sidebarContainer.closeMenus();
                window.navigationController.closeSidebar();
                manualAnimResetTimer.restart();
            }

            Behavior on x {
                enabled: window.layoutAnimEnabled
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutCubic
                }
            }
        }

        // Click Mask
        MouseArea {
            id: clickMask
            anchors.fill: parent
            z: 499
            visible: (!window.isDockCapable && window.navigationController.sidebarOpen) || sidebarContainer.hasOpenMenu || mainContent.hasOpenMenu
            onClicked: {
                sidebarContainer.closeMenus();
                mainContent.closeMenus();
            }
        }

        // Player Container
        Item {
            id: playerContainer
            height: parent.height
            property bool isDocked: !window.navigationController.startupScreenVisible && window.isDockCapable && window.navigationController.sidebarOpen
            x: isDocked ? window.sidebarWidth : 0
            width: isDocked ? (window.width - window.sidebarWidth) : window.width

            Behavior on x {
                enabled: window.layoutAnimEnabled
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutCubic
                }
            }

            Behavior on width {
                enabled: window.layoutAnimEnabled && window.navigationController.manualSidebarToggle
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutCubic
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Custom Title Bar
                Rectangle {
                    id: titleBar
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: "transparent"

                    MouseArea {
                        anchors.fill: parent
                        onPressed: window.startSystemMove()
                    }

                    Text {
                        text: qsTr("Seriona")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        font.letterSpacing: 1.0
                        anchors.centerIn: parent
                    }

                    WindowControls {
                        targetWindow: window
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacing12
                        anchors.verticalCenter: parent.verticalCenter
                        onCloseRequested: window.requestApplicationClose()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // Main Content (Playback & Lyrics with shared element transition)
                    MainContent {
                        id: mainContent
                        anchors.fill: parent
                        visible: !window.navigationController.startupScreenVisible
                        enabled: !window.navigationController.startupScreenVisible
                        state: window.navigationController.currentView
                        isSidebarOpen: window.navigationController.sidebarOpen
                        playbackController: window.appFacade.playback
                        notifications: window.appFacade.notifications
                        libraryController: window.appFacade.library
                        lyricsState: window.appFacade.lyrics
                        settings: window.appFacade.settings

                        onCoverClicked: {
                            window.navigationController.showLyricsView();
                        }

                        onCoverDragRequested: {
                            window.startSystemMove();
                        }

                        onBackClicked: {
                            window.navigationController.showPlaybackView();
                        }

                        onPlaylistToggled: {
                            if (!window.navigationController.sidebarOpen)
                                mainContent.closeMenus();
                            if (window.navigationController.sidebarOpen)
                                sidebarContainer.closeMenus();
                            window.navigationController.toggleSidebar();
                            manualAnimResetTimer.restart();
                        }

                        onExitRequested: window.requestApplicationClose()

                        onOpenSettingsRequested: {
                            settingsWindow.show();
                        }

                        onOpenEqualizerRequested: {
                            // 首次打开时居中(一次性定位,不跟随主窗口)
                            if (!equalizerWindow.visible) {
                                equalizerWindow.x = window.x + (window.width - equalizerWindow.width) / 2;
                                equalizerWindow.y = window.y + (window.height - equalizerWindow.height) / 2;
                            }
                            equalizerWindow.show();
                        }
                    }

                    StartupView {
                        anchors.fill: parent
                        visible: window.navigationController.startupScreenVisible
                        enabled: window.navigationController.startupScreenVisible
                        navigationController: window.navigationController
                        appFacade: window.appFacade
                        libraryController: window.appFacade.library
                    }
                }
            }
        }
    }

    // 8-directional resizing using custom ResizeArea logic
    Item {
        anchors.fill: parent
        visible: window.visibility !== Window.Maximized
        z: 1000

        // Top
        ResizeArea {
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: 10
                rightMargin: 10
            }
            height: 5
            edgeFlag: Qt.TopEdge
        }
        // Bottom
        ResizeArea {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: 10
                rightMargin: 10
            }
            height: 5
            edgeFlag: Qt.BottomEdge
        }
        // Left
        ResizeArea {
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
                topMargin: 10
                bottomMargin: 10
            }
            width: 5
            edgeFlag: Qt.LeftEdge
        }
        // Right
        ResizeArea {
            anchors {
                right: parent.right
                top: parent.top
                bottom: parent.bottom
                topMargin: 10
                bottomMargin: 10
            }
            width: 5
            edgeFlag: Qt.RightEdge
        }
        // TopLeft
        ResizeArea {
            anchors {
                left: parent.left
                top: parent.top
            }
            width: 10
            height: 10
            edgeFlag: Qt.TopEdge | Qt.LeftEdge
        }
        // TopRight
        ResizeArea {
            anchors {
                right: parent.right
                top: parent.top
            }
            width: 10
            height: 10
            edgeFlag: Qt.TopEdge | Qt.RightEdge
        }
        // BottomLeft
        ResizeArea {
            anchors {
                left: parent.left
                bottom: parent.bottom
            }
            width: 10
            height: 10
            edgeFlag: Qt.BottomEdge | Qt.LeftEdge
        }
        // BottomRight
        ResizeArea {
            anchors {
                right: parent.right
                bottom: parent.bottom
            }
            width: 10
            height: 10
            edgeFlag: Qt.BottomEdge | Qt.RightEdge
        }
    }

    component ResizeArea: MouseArea {
        property int edgeFlag
        cursorShape: {
            switch (edgeFlag) {
            case (Qt.TopEdge | Qt.LeftEdge):
                return Qt.SizeFDiagCursor;
            case (Qt.BottomEdge | Qt.RightEdge):
                return Qt.SizeFDiagCursor;
            case (Qt.TopEdge | Qt.RightEdge):
                return Qt.SizeBDiagCursor;
            case (Qt.BottomEdge | Qt.LeftEdge):
                return Qt.SizeBDiagCursor;
            case Qt.TopEdge:
                return Qt.SizeVerCursor;
            case Qt.BottomEdge:
                return Qt.SizeVerCursor;
            case Qt.LeftEdge:
                return Qt.SizeHorCursor;
            case Qt.RightEdge:
                return Qt.SizeHorCursor;
            default:
                return Qt.ArrowCursor;
            }
        }
        onPressed: window.startSystemResize(edgeFlag)
    }

    SettingsWindow {
        id: settingsWindow
        appFacade: window.appFacade
        x: window.x + (window.width - width) / 2
        y: window.y + (window.height - height) / 2
    }

    EqualizerWindow {
        id: equalizerWindow
        // x/y 坐标绑定已移除:避免窗口跟随主窗口瞬移(Wayland transient 语义问题)
        // 首次打开居中逻辑见 openEqualizer 处理器
    }

    Timer {
        id: sidebarSmokeTimer
        interval: 300
        onTriggered: {
            var sb = sidebarContainer.verticalScrollBar;
            if (sb) {
                smokeLog("[smoke] sidebar verticalScrollBar attached=true size=" + sb.size.toFixed(2) + " handleVisible=" + sb.contentItem.visible);
            } else {
                smokeLog("[smoke] sidebar verticalScrollBar attached=false");
            }
        }
    }

    Timer {
        id: smokeTimer
        interval: 100
        repeat: false
        property int step: 0
        // T10 图谱状态机断言轮询状态（settings-menu EQ 段专用）
        property int graphPollTicks: 0
        property bool graphCurveArrived: false
        // T11 10/31 快速切换风暴断言状态（step9-11；stormLeg 0..5 = 3 往返）
        property int stormLeg: 0
        property bool stormLegOk: true
        onTriggered: {
            if (step === 0) {
                mainContent.openSettingsRequested();
                step = 1;
                smokeTimer.interval = 100;
                smokeTimer.start();
            } else if (step === 1) {
                if (settingsWindow.visible) {
                    console.log("[smoke] settingsWindow opened");
                    settingsWindow.close();
                }
                mainContent.openEqualizerRequested();
                step = 2;
                smokeTimer.interval = 100;
                smokeTimer.start();
            } else if (step === 2) {
                if (equalizerWindow.visible) {
                    console.log("[smoke] equalizerWindow opened");
                    // 既有开窗断言保持；窗口暂不关闭，进入窗内交互步骤（F2.6）
                    step = 3;
                    smokeTimer.interval = 100;
                    smokeTimer.start();
                }
            } else if (step === 3) {
                if (equalizerWindow.visible) {
                    // 总开关点击 → settings.enabled 翻转（Qt6.8+ click() 模拟真实按下/释放，走 toggled 链）
                    var sw = smokeFindByObjectName(equalizerWindow, "eqMasterSwitch");
                    if (sw) {
                        var enabledBefore = appFacade.settings.enabled;
                        sw.click();
                        if (appFacade.settings.enabled !== enabledBefore)
                            console.log("[smoke] eq masterSwitch toggled settings.enabled -> " + appFacade.settings.enabled);
                    }
                    // 10/31 切档点击 → settings.bandMode 变化 + 竖条数随档重建（Repeater 即时）
                    var mode31 = smokeFindByObjectName(equalizerWindow, "eqMode31Button");
                    var mode10 = smokeFindByObjectName(equalizerWindow, "eqMode10Button");
                    if (mode31 && mode10) {
                        // GEQ 横向滚动条（自绘轨道 objectName=eqHStyledScrollBar，bandHScrollTrack）：
                        // 31 段内容(2106px)溢出视口 → 轨道 visible 句柄可拖；切 10 段(678px)不溢出
                        // → 整条隐藏（visible=false）。可见性绑定 = contentWidth > width + 1。
                        var hBar = smokeFindByObjectName(equalizerWindow, "eqHStyledScrollBar");
                        console.log("[smoke] eq hStyledScrollBar hit objectName=eqHStyledScrollBar"
                                + " type=" + (hBar ? String(hBar) : "(null)")
                                + " isStyled=" + (hBar !== null && hBar._handleW !== undefined));
                        mode31.click();
                        if (appFacade.settings.bandMode === 31
                                && smokeCountByObjectNamePrefix(equalizerWindow, "eqBandSlider") === 31)
                            console.log("[smoke] eq mode31 band sliders=31");
                        if (hBar)
                            console.log("[smoke] eq hStyledScrollBar mode31 visible=" + hBar.visible
                                    + " handleW=" + hBar._handleW.toFixed(1));
                        mode10.click();
                        if (appFacade.settings.bandMode === 10
                                && smokeCountByObjectNamePrefix(equalizerWindow, "eqBandSlider") === 10)
                            console.log("[smoke] eq mode10 band sliders=10");
                        if (hBar)
                            console.log("[smoke] eq hStyledScrollBar mode10 visible=" + hBar.visible
                                    + " handleW=" + hBar._handleW.toFixed(1));
                    }
                    step = 4;
                    smokeTimer.interval = 100;
                    smokeTimer.start();
                }
            } else if (step === 4) {
                if (equalizerWindow.visible) {
                    // 管理按钮点击 → Loader 惰性激活 + 预设弹层 visible（Popup 无开合动画，同步可见）
                    var manage = smokeFindByObjectName(equalizerWindow, "eqManagePresetsButton");
                    if (manage) {
                        manage.click();
                        var dialog = smokeFindByObjectName(equalizerWindow, "equalizerPresetDialog");
                        if (dialog && dialog.visible)
                            console.log("[smoke] eq presetDialog visible");
                        if (dialog) {
                            // R6 断言：预设行视图模型必须带 kind 标记（delegate 的
                            // visible: modelData.kind === "preset" 判定依赖它；
                            // 修复前为 undefined → 行实例存在但不可见）
                            var presetRows = dialog.sectionedList;
                            var presetKinds = "";
                            for (var ri = 0; ri < Math.min(presetRows.length, 4); ++ri)
                                presetKinds += (ri > 0 ? "," : "") + presetRows[ri].kind;
                            console.log("[smoke] eq presetDialog rows=" + presetRows.length
                                    + " kinds=" + presetKinds
                                    + " firstPresetKind=" + (presetRows.length > 1 ? presetRows[1].kind : "(none)"));
                            dialog.close();
                        }
                    }
                    // —— T10 图谱区断言起点：频谱刻度 4 档文本存在 + 显式置 OFF 初态 ——
                    // 刻度标签为静态 QML 覆盖层（eqScaleLabel*；0 顶 → -60 底，dbFsToY 定位）。
                    // 显式置 spectrumEnabled=false（settings_controller.h 默认即 false）防
                    // QSettings 持久化残留 true 造成环境相关通过。
                    // 注：smoke 进程按 main.cpp 契约不启动媒体控制器（backendBridge
                    // AutostartEnabled=false）→ 频谱 bins 恒缺、curvePoints 镜像恒缺；
                    // R5 起显示曲线 = 本地包络（SettingsController 构造器恒置 bandGains10/31
                    // 零增益 → hasCurveData() 恒 true）→ graphHintText 确定性进入
                    // 「频谱已关闭」（OFF）/「暂无频谱数据」（ON，bins 空）两态——两态在
                    // smoke 下均可达，状态机可全量断言（R5 前依赖镜像曲线不可达）。
                    var sl0 = smokeFindByObjectName(equalizerWindow, "eqScaleLabel0");
                    var sl20 = smokeFindByObjectName(equalizerWindow, "eqScaleLabel-20");
                    var sl40 = smokeFindByObjectName(equalizerWindow, "eqScaleLabel-40");
                    var sl60 = smokeFindByObjectName(equalizerWindow, "eqScaleLabel-60");
                    console.log("[smoke] eq scaleLabels4 found=" + (sl0 !== null) + "/" + (sl20 !== null)
                            + "/" + (sl40 !== null) + "/" + (sl60 !== null)
                            + " texts=" + (sl0 ? sl0.text : "-") + "/" + (sl20 ? sl20.text : "-")
                            + "/" + (sl40 ? sl40.text : "-") + "/" + (sl60 ? sl60.text : "-"));
                    appFacade.settings.spectrumEnabled = false;
                    smokeTimer.graphPollTicks = 0;
                    smokeTimer.graphCurveArrived = false;
                    step = 5;
                    smokeTimer.interval = 200;
                    smokeTimer.start();
                }
            } else if (step === 5) {
                // R5：显示曲线 = 本地包络（bandGains 恒置 → 首 tick 即就绪），无后端等待面；
                // 轮询仅作 settings 未就绪的兜底（上限 ~3s = 15 tick），记录后继续柱区联动断言。
                smokeTimer.graphPollTicks = smokeTimer.graphPollTicks + 1;
                if (equalizerWindow.visible) {
                    var gainLen = appFacade.settings.bandGains10
                            ? appFacade.settings.bandGains10.length : 0;
                    if (gainLen > 1) {
                        smokeTimer.graphCurveArrived = true;
                        console.log("[smoke] eq graph envelope ready tick=" + smokeTimer.graphPollTicks
                                + " bands=" + gainLen);
                        step = 6;
                        smokeTimer.interval = 100;
                        smokeTimer.start();
                    } else if (smokeTimer.graphPollTicks >= 15) {
                        console.log("[smoke] eq graph envelope absent after 3s (settings not seeded)");
                        step = 6; // 柱区联动断言不依赖曲线，仍继续
                        smokeTimer.interval = 100;
                        smokeTimer.start();
                    } else {
                        smokeTimer.start();
                    }
                } else {
                    smokeTimer.stop();
                }
            } else if (step === 6) {
                if (equalizerWindow.visible) {
                    // 相位 A（真实状态机起点 = 默认 OFF）：包络曲线恒在（R5）+ 开关 OFF →
                    // hint=频谱已关闭（确定性）+ 柱区隐藏。
                    var gA = smokeFindByObjectName(equalizerWindow, "eqGraphCanvas");
                    var hA = smokeFindByObjectName(equalizerWindow, "eqGraphEmptyHint");
                    if (gA && hA)
                        console.log("[smoke] eq graph phaseA barsVisible=" + gA.spectrumBarsVisible
                                + " hint='" + hA.text + "' hintVisible=" + hA.visible
                                + " curveArrived=" + smokeTimer.graphCurveArrived
                                + " bins=" + (appFacade.settings.spectrumBins
                                        ? appFacade.settings.spectrumBins.length : 0));
                    var spSwA = smokeFindByObjectName(equalizerWindow, "eqSpectrumSwitch");
                    if (spSwA) {
                        spSwA.click(); // OFF → ON（真命令 SetSpectrumEnabled 外发面）
                        console.log("[smoke] eq spectrumSwitch -> settings.spectrumEnabled="
                                + appFacade.settings.spectrumEnabled);
                    }
                    step = 7;
                    smokeTimer.interval = 150;
                    smokeTimer.start();
                }
            } else if (step === 7) {
                if (equalizerWindow.visible) {
                    // 相位 B：开关 ON → 柱区显示（barsVisible=true）；hint=暂无频谱数据
                    //（R5 确定性：bins 空 + 包络恒在）。
                    var gB = smokeFindByObjectName(equalizerWindow, "eqGraphCanvas");
                    var hB = smokeFindByObjectName(equalizerWindow, "eqGraphEmptyHint");
                    if (gB && hB)
                        console.log("[smoke] eq graph phaseB barsVisible=" + gB.spectrumBarsVisible
                                + " hint='" + hB.text + "' hintVisible=" + hB.visible
                                + " spectrumEnabled=" + appFacade.settings.spectrumEnabled
                                + " bins=" + (appFacade.settings.spectrumBins
                                        ? appFacade.settings.spectrumBins.length : 0));
                    var spSwB = smokeFindByObjectName(equalizerWindow, "eqSpectrumSwitch");
                    if (spSwB) {
                        spSwB.click(); // ON → OFF（回初始态）
                        console.log("[smoke] eq spectrumSwitch -> settings.spectrumEnabled="
                                + appFacade.settings.spectrumEnabled);
                    }
                    step = 8;
                    smokeTimer.interval = 150;
                    smokeTimer.start();
                }
            } else if (step === 8) {
                if (equalizerWindow.visible) {
                    // 相位 C：再置 OFF → 柱区隐藏 + hint 回「频谱已关闭」（状态机闭环；开关
                    // 联动三段全断言）。两态可达性见 step4 注释（R5 起 smoke 下确定性可达）。
                    var gC = smokeFindByObjectName(equalizerWindow, "eqGraphCanvas");
                    var hC = smokeFindByObjectName(equalizerWindow, "eqGraphEmptyHint");
                    if (gC && hC)
                        console.log("[smoke] eq graph phaseC barsVisible=" + gC.spectrumBarsVisible
                                + " hint='" + hC.text + "' hintVisible=" + hC.visible);
                    console.log("[smoke] eq graph state machine summary curveArrived="
                            + smokeTimer.graphCurveArrived
                            + " barsGatingOk=" + (gC ? gC.spectrumBarsVisible === false : false)
                            + " spectrumOffHintDeterministic=" + (hC && hC.text === "频谱已关闭"));
                    // T11：关窗后移至风暴完成（step 11 尾部）；此处链续走 T11 定稿断言
                    //（binCount==120 / 手柄计数 10/31 / 快速切换风暴）。
                    step = 9;
                    smokeTimer.interval = 100;
                    smokeTimer.start();
                }
            } else if (step === 9) {
                if (equalizerWindow.visible) {
                    // —— T11 定稿断言 A：binCount==120 + 手柄计数可达性 ——
                    // 手柄计数在 smoke（无后端）下可达：bandGains10/31 列表由
                    // SettingsController 构造器恒置 10/31 零增益（settings_controller.cpp
                    // :347-348，与后端无关），EqualizerWindow 绑定 settings 镜像 →
                    // 手柄镜像恒在 → SpectrumGraph.handleCount() == 档位数（非恒真：切换
                    // bandMode 后计数随之变化，10 与 31 两档实测互异）。进入本步时
                    // bandMode=10（step3 末切回 10 后未再变）。
                    var g9 = smokeFindByObjectName(equalizerWindow, "eqGraphCanvas");
                    smokeTimer.stormLeg = 1; // 风暴腿 1..5 由 step10 执行；本步 = 腿 0
                    smokeTimer.stormLegOk = true;
                    if (g9)
                        console.log("[smoke] eq graph binCount=" + g9.binCount
                                + " binCountOk=" + (g9.binCount === 120));
                    var hc10 = g9 ? g9.handleCount() : -1;
                    var sliders10 = smokeCountByObjectNamePrefix(equalizerWindow, "eqBandSlider");
                    var leg0Ok = g9 !== null && appFacade.settings.bandMode === 10
                            && hc10 === 10 && sliders10 === 10;
                    smokeTimer.stormLegOk = leg0Ok;
                    console.log("[smoke] eq storm leg0 expect=10 bandMode="
                            + appFacade.settings.bandMode + " handleCount=" + hc10
                            + " sliders=" + sliders10 + " legOk=" + leg0Ok);
                    // 风暴腿 0 点击：10 → 31（此后交替快速往返）
                    var btn9 = smokeFindByObjectName(equalizerWindow, "eqMode31Button");
                    if (btn9)
                        btn9.click();
                    step = 10;
                    smokeTimer.interval = 100;
                    smokeTimer.start();
                }
            } else if (step === 10) {
                if (equalizerWindow.visible && smokeTimer.stormLeg < 6) {
                    // —— T11 风暴腿 1..5（每腿先断言上一腿点击落点、再切下一档）——
                    // 期望 = 上一腿点击后应处的档：腿 1/3/5 期望 31（点 mode10 回切），
                    // 腿 2/4 期望 10（点 mode31）；腿 0 已在 step9 断言并点击 10→31，
                    // 合计 6 腿点击 = 10↔31 三整往返。每腿核对 settings.bandMode /
                    // SpectrumGraph.handleCount() / eqBandSlider 计数三者一致（切换在
                    // 100ms 间隔下持续走绑定→镜像→竖条重建数据流；崩溃/断言失配即
                    // legOk=false 或链中断）。末腿 5 点 mode10 → 终态 mode10。
                    var expect = (smokeTimer.stormLeg % 2 === 1) ? 31 : 10;
                    var gs = smokeFindByObjectName(equalizerWindow, "eqGraphCanvas");
                    var hcs = gs ? gs.handleCount() : -1;
                    var sliders = smokeCountByObjectNamePrefix(equalizerWindow, "eqBandSlider");
                    var legOk = gs !== null && appFacade.settings.bandMode === expect
                            && hcs === expect && sliders === expect;
                    smokeTimer.stormLegOk = smokeTimer.stormLegOk && legOk;
                    console.log("[smoke] eq storm leg" + smokeTimer.stormLeg + " expect=" + expect
                            + " bandMode=" + appFacade.settings.bandMode + " handleCount=" + hcs
                            + " sliders=" + sliders + " legOk=" + legOk);
                    var stormBtn = smokeFindByObjectName(equalizerWindow,
                            expect === 31 ? "eqMode10Button" : "eqMode31Button");
                    if (stormBtn)
                        stormBtn.click();
                    smokeTimer.stormLeg = smokeTimer.stormLeg + 1;
                    if (smokeTimer.stormLeg < 6) {
                        smokeTimer.interval = 100;
                        smokeTimer.start();
                    } else {
                        step = 11;
                        smokeTimer.interval = 100;
                        smokeTimer.start();
                    }
                }
            } else if (step === 11) {
                if (equalizerWindow.visible) {
                    // —— T11 风暴总结（终态 bandMode=10：腿 5 末点 mode10）——
                    var gF = smokeFindByObjectName(equalizerWindow, "eqGraphCanvas");
                    var hcF = gF ? gF.handleCount() : -1;
                    console.log("[smoke] eq storm summary legs=6 roundTrips=3 stormOk="
                            + smokeTimer.stormLegOk
                            + " finalBandMode=" + appFacade.settings.bandMode
                            + " finalHandleCount=" + hcF
                            + " binCountOk=" + (gF !== null && gF.binCount === 120)
                            + " barsStillHidden=" + (gF ? gF.spectrumBarsVisible === false : false));
                    // —— R6 预设弹层回归：打开（step12 断言）→ 二次点击关闭且不重开（step13）——
                    var applyBtn = smokeFindByObjectName(equalizerWindow, "eqPresetApplyButton");
                    if (applyBtn) {
                        applyBtn.click(); // 打开（enter 过渡）
                        console.log("[smoke] eq presetPicker click#1 (open)");
                    } else {
                        console.log("[smoke] eq presetPicker applyButton missing");
                    }
                    step = 12;
                    smokeTimer.interval = 200;
                    smokeTimer.start();
                }
            } else if (step === 12) {
                if (equalizerWindow.visible) {
                    // R6 断言 A：打开态 visible + 背景已清除（background:null 或全透明）
                    // + enter/exit 过渡存在（与设置菜单同款动画接线）。
                    var picker = smokeFindByObjectName(equalizerWindow, "eqPresetPickerMenu");
                    var pickerBg = picker ? picker.background : undefined;
                    var bgOk = picker !== null
                            && (pickerBg === null || pickerBg === undefined
                                || (pickerBg.color !== undefined && pickerBg.color.a === 0));
                    console.log("[smoke] eq presetPicker afterOpen visible=" + (picker ? picker.visible : "(null)")
                            + " bgOk=" + bgOk
                            + " hasEnter=" + (picker ? picker.enter !== null && picker.enter !== undefined : false)
                            + " hasExit=" + (picker ? picker.exit !== null && picker.exit !== undefined : false));
                    var applyBtn2 = smokeFindByObjectName(equalizerWindow, "eqPresetApplyButton");
                    if (applyBtn2) {
                        applyBtn2.click(); // 已打开时再点：应关闭且不重开
                        console.log("[smoke] eq presetPicker click#2 (toggle close)");
                    }
                    step = 13;
                    smokeTimer.interval = 350;
                    smokeTimer.start();
                }
            } else if (step === 13) {
                if (equalizerWindow.visible) {
                    // R6 断言 B：退场过渡结束（150ms）后弹层隐藏且未重开（noReopen）。
                    var picker2 = smokeFindByObjectName(equalizerWindow, "eqPresetPickerMenu");
                    console.log("[smoke] eq presetPicker afterSecondClick visible="
                            + (picker2 ? picker2.visible : "(null)")
                            + " noReopen=" + (picker2 ? picker2.visible === false : false));
                    equalizerWindow.close();
                    console.log("[smoke] equalizerWindow closed after eq graph state machine + T11 storm + preset picker toggle");
                }
            }
        }
    }
}
