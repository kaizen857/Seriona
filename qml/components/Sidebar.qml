import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import Qt5Compat.GraphicalEffects
import Qt.labs.platform as Platform
import Seriona

Item {
    id: root
    width: Theme.sidebarWidth

    signal closeClicked

    // Properties to control shadow visibility
    required property AppFacade appFacade
    required property LibraryController libraryController
    property bool isDockCapable: false
    property bool isSidebarOpen: false
    property bool isSearching: false
    // 视图切换（T15）：false=文件夹视图，true=队列视图。仅切换列表内容，
    // 不持久化（每次启动默认文件夹视图）；文件夹视图状态无损保留。
    property bool queueViewActive: false

    // 页面缓存与动画抑制
    property var folderPages: ({})
    property bool suppressNavAnimation: false
    // 下一次页面激活的动画方向（1=进入/push，-1=返回/pop）：
    // 由配对导航路径（activateNode/handleBackClicked）在栈操作前置位，
    // onCurrentItemChanged 同步消费——返回时新 currentItem 的错落动画向左滑入。
    property int pendingNavDirection: 1

    // 宿主代理对象供 PlaylistDelegate / FolderPage 注入
    readonly property var contextMenuHost: ({
        isOpen: trackContextMenu.isOpen,
        close: function() {
            trackContextMenu.close();
        },
        openForEntry: function(entry, targetDelegate, mouseX, mouseY) {
            var fullEntry = Object.assign({}, entry, {
                path: root.appFacade.filePathForNodeId(entry.nodeId)
            });
            trackContextMenu.openForEntry(fullEntry, targetDelegate, mouseX, mouseY);
        }
    })

    // 当前激活的列表视图（搜索页、StackView 当前页或根目录页）
    readonly property ListView activeListView: root.isSearching
        ? searchView
        : (folderStack.currentItem ? folderStack.currentItem.listView : playlistView)

    readonly property bool hasOpenMenu: sidebarMenu.visible || trackContextMenu.isOpen
    readonly property ScrollBar verticalScrollBar: playlistView.ScrollBar.vertical
    readonly property bool scanRunning: libraryController.scanStatus === "running"
    readonly property bool scanError: libraryController.scanStatus === "error"
    readonly property string scanMessage: scanRunning
        ? (libraryController.totalSongCount > 0
            ? qsTr("正在扫描：%1 / %2").arg(libraryController.scannedSongCount).arg(libraryController.totalSongCount)
            : qsTr("正在扫描曲库…"))
        : scanError
            ? (libraryController.lastError.length > 0 ? libraryController.lastError : qsTr("扫描失败，请重新选择文件夹"))
            : ""
    readonly property string emptyStateText: scanRunning
        ? (libraryController.totalSongCount > 0
            ? qsTr("正在扫描：%1 / %2").arg(libraryController.scannedSongCount).arg(libraryController.totalSongCount)
            : qsTr("正在扫描曲库…"))
        : scanError
            ? (libraryController.lastError.length > 0 ? libraryController.lastError : qsTr("扫描失败，请重新选择文件夹"))
            : libraryController.scanStatus === "completed"
                ? qsTr("扫描完成，但没有发现音频文件")
                : root.isSearching ? qsTr("没有匹配的本地结果") : qsTr("暂无曲库内容，请添加音乐文件夹")

    onIsSidebarOpenChanged: {
        if (!isSidebarOpen)
            closeMenus();
    }

    Component.onCompleted: {
        playlistView.model = libraryController.projectionModelForNodeId("");
    }

    function closeMenus() {
        sidebarMenu.close();
        trackContextMenu.close();
    }

    // 查询节点在指定视图模型中的行号
    function rowForNodeInView(view, nodeId) {
        if (!view || !view.model || !nodeId || nodeId.length === 0)
            return -1;
        if (typeof view.model.rowForNodeId === "function") {
            return view.model.rowForNodeId(nodeId);
        }
        return libraryController.rowForNodeId(nodeId);
    }

    function showUnsupportedFeedback(actionName) {
        root.appFacade.notifications.showUnsupportedAction(actionName);
        root.closeMenus();
    }

    function sortRulesForDialog() {
        var currentRules = libraryController.currentSortRules.slice();
        return currentRules.length > 0 ? currentRules : [{field: "filename", order: "asc"}];
    }

    function getOrCreateFolderPage(nodeId) {
        if (folderPages[nodeId]) {
            return folderPages[nodeId];
        }
        var pageComponent = Qt.createComponent("FolderPage.qml");
        if (pageComponent.status !== Component.Ready) {
            console.error("Failed to create FolderPage component:", pageComponent.errorString());
            return null;
        }
        var model = libraryController.projectionModelForNodeId(nodeId);
        var page = pageComponent.createObject(folderStack, {
            folderNodeId: nodeId,
            projectionModel: model,
            activateNodeHandler: root.activateNode,
            closeMenusHandler: root.closeMenus,
            contextMenuHost: root.contextMenuHost
        });
        folderPages[nodeId] = page;
        return page;
    }

    function triggerRootViewSlideIn(direction) {
        if (!playlistView || direction === 0 || playlistView.count === 0)
            return;
        playlistView.forceLayout();
        var firstVisible = 0;
        if (direction === -1) {
            var idx = playlistView.indexAt(0, playlistView.contentY + 1);
            firstVisible = idx >= 0 ? idx : 0;
        }
        var visibleRowCount = Math.ceil(playlistView.height / 72) + 3;
        var endIndex = Math.min(playlistView.count - 1, firstVisible + visibleRowCount);
        for (var i = firstVisible; i <= endIndex; ++i) {
            var item = playlistView.itemAtIndex(i);
            if (item && typeof item.startNavSlideIn === "function") {
                var step = i - firstVisible;
                item.startNavSlideIn(step, direction);
            }
        }
    }

    function activateNode(nodeId, isFolder) {
        if (nodeId.length === 0)
            return;

        root.closeMenus();
        libraryController.selectBrowserNode(nodeId);
        if (isFolder) {
            if (folderStack.busy) {
                console.warn("Navigation ignored: folderStack is busy");
                return;
            }
            root.pendingNavDirection = 1;
            var page = getOrCreateFolderPage(nodeId);
            if (!page)
                return;
            folderStack.push(page);
            libraryController.enterFolder(nodeId);

            // 配对零信号 no-op 自愈（若 enterFolder 失败或节点已被移除未实际进入）
            if (libraryController.currentFolderNodeId !== nodeId) {
                console.warn("Self-healing enterFolder: controller node did not change to", nodeId);
                if (folderStack.depth > 1) {
                    folderStack.pop();
                } else {
                    folderStack.clear();
                }
            }
            return;
        }

        libraryController.playItem(nodeId);
    }

    function handleBackClicked() {
        if (!libraryController.canGoBack)
            return;
        if (folderStack.busy) {
            console.warn("Back navigation ignored: folderStack is busy");
            return;
        }

        root.closeMenus();
        var wasDepth = folderStack.depth;
        root.pendingNavDirection = -1;
        if (wasDepth > 1) {
            folderStack.pop();
        } else {
            folderStack.clear();
            if (!root.suppressNavAnimation) {
                triggerRootViewSlideIn(-1);
            }
        }
        libraryController.goBack();

        // 配对零信号自愈（若 goBack 未使深度减少）
        if (libraryController.folderStackDepth >= wasDepth) {
            console.warn("Self-healing goBack: controller depth did not decrease");
            reconcileStackToController();
        }
    }

    // 幂等收敛处理器
    function reconcileStackToController() {
        if (folderStack.busy) {
            return;
        }

        var ctrlDepth = libraryController.folderStackDepth;
        var stackDepth = folderStack.depth;
        var targetNodeId = libraryController.currentFolderNodeId;
        var targetChain = libraryController.ancestorChainForNode(targetNodeId);

        // 判断当前栈是否已与目标链完全一致
        var isAligned = false;
        if (ctrlDepth === stackDepth) {
            if (ctrlDepth === 0) {
                isAligned = true;
            } else if (folderStack.currentItem && folderStack.currentItem.folderNodeId === targetNodeId) {
                isAligned = true;
                for (var i = 0; i < stackDepth; ++i) {
                    var item = folderStack.get(i);
                    if (!item || item.folderNodeId !== targetChain[i]) {
                        isAligned = false;
                        break;
                    }
                }
            }
        }

        if (isAligned) {
            root.suppressNavAnimation = false;
            return;
        }

        // 不一致需收敛：统一设置抑制标志
        root.suppressNavAnimation = true;

        // 链对齐循环
        while (true) {
            ctrlDepth = libraryController.folderStackDepth;
            stackDepth = folderStack.depth;
            targetChain = libraryController.ancestorChainForNode(libraryController.currentFolderNodeId);

            if (ctrlDepth === 0) {
                if (folderStack.depth > 0) {
                    folderStack.clear(StackView.Immediate);
                }
                break;
            }

            // 1. stackDepth > ctrlDepth
            if (stackDepth > ctrlDepth) {
                if (ctrlDepth === 0) {
                    folderStack.clear(StackView.Immediate);
                } else {
                    while (folderStack.depth > ctrlDepth) {
                        if (folderStack.depth > 1) {
                            folderStack.pop(null, StackView.Immediate);
                        } else {
                            folderStack.clear(StackView.Immediate);
                            break;
                        }
                    }
                }
                stackDepth = folderStack.depth;
            }

            // 检查公共前缀分歧
            var mismatchIndex = -1;
            for (var j = 0; j < stackDepth && j < ctrlDepth; ++j) {
                var stackItem = folderStack.get(j);
                if (!stackItem || stackItem.folderNodeId !== targetChain[j]) {
                    mismatchIndex = j;
                    break;
                }
            }

            if (mismatchIndex >= 0) {
                // 有分歧：pop 到公共前缀深度 mismatchIndex
                if (mismatchIndex === 0) {
                    folderStack.clear(StackView.Immediate);
                } else {
                    while (folderStack.depth > mismatchIndex) {
                        if (folderStack.depth > 1) {
                            folderStack.pop(null, StackView.Immediate);
                        } else {
                            folderStack.clear(StackView.Immediate);
                            break;
                        }
                    }
                }
                stackDepth = folderStack.depth;
            }

            // 补齐缺失层级
            for (var k = stackDepth; k < ctrlDepth; ++k) {
                var missingNodeId = targetChain[k];
                var page = getOrCreateFolderPage(missingNodeId);
                if (page) {
                    folderStack.push(page, StackView.Immediate);
                }
            }

            break;
        }

        // 收敛完成判定
        ctrlDepth = libraryController.folderStackDepth;
        stackDepth = folderStack.depth;
        targetNodeId = libraryController.currentFolderNodeId;

        if (stackDepth === ctrlDepth) {
            if (ctrlDepth === 0) {
                if (targetNodeId === "") {
                    root.suppressNavAnimation = false;
                }
            } else if (folderStack.currentItem && folderStack.currentItem.folderNodeId === targetNodeId) {
                root.suppressNavAnimation = false;
            }
        }
    }

    Connections {
        target: libraryController

        function onFolderStackDepthChanged() {
            root.reconcileStackToController();
        }

        function onCurrentFolderNodeIdChanged() {
            root.reconcileStackToController();
        }
    }

    Connections {
        target: folderStack

        function onBusyChanged() {
            if (!folderStack.busy) {
                root.reconcileStackToController();
            }
        }

        function onCurrentItemChanged() {
            var item = folderStack.currentItem;
            // 复位全部缓存页（含 pop 离栈页）的激活标记：重入已访问目录时
            // isActive 置位有变化 → onIsActiveChanged 触发 → 错落动画重新播放
            // （视觉需求 4；只遍历栈内页面会漏掉离栈缓存页，其 isActive 保持
            // true 导致再次 push 重入时置位无变化、动画不播）。
            for (var key in root.folderPages) {
                var page = root.folderPages[key];
                if (page && page !== item) {
                    page.isActive = false;
                }
            }
            if (item) {
                item.animationSuppressed = root.suppressNavAnimation;
                item.navDirection = root.pendingNavDirection;
                item.isActive = true;
            }
        }
    }

    RectangularGlow {
        id: shadow
        anchors.fill: contentRect
        glowRadius: 10
        spread: 0.2
        color: Theme.shadowPopupColor
        visible: !root.isDockCapable && root.isSidebarOpen
        z: -1
    }

    Rectangle {
        id: contentRect
        anchors.fill: parent
        color: "transparent"

        DynamicBackground {
            anchors.fill: parent
            playbackController: root.appFacade.playback
        }

        // Right border line
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.borderSubtle
            z: 2
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Top Bar
            Rectangle {
                z: 2
                Layout.fillWidth: true
                Layout.preferredHeight: root.isSearching ? 100 : 50
                color: "transparent"

                Behavior on Layout.preferredHeight {
                    NumberAnimation {
                        duration: Theme.animationStandard
                        easing.type: Theme.easingDecelerate
                    }
                }

                Column {
                    anchors.fill: parent

                    Item {
                        width: parent.width
                        height: 50

                        MouseArea {
                            anchors.fill: parent
                            visible: root.hasOpenMenu
                            enabled: visible
                            onClicked: root.closeMenus()
                        }

                        // 表头空白处按下拖拽窗口（Qt 6.8 Window.startSystemMove；
                        // offscreen/无窗口系统支持时无副作用）。按钮/搜索框等交互元素
                        // 位于上层（RowLayout z:1、搜索行 z:10），点击事件不受影响。
                        MouseArea {
                            id: headerDragArea
                            anchors.fill: parent
                            visible: !root.hasOpenMenu
                            enabled: visible
                            acceptedButtons: Qt.LeftButton
                            onPressed: root.Window.window.startSystemMove()
                        }

                        RowLayout {
                            z: 1
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacing16
                            anchors.rightMargin: Theme.spacing16
                            spacing: Theme.spacing12

                            StyleButton {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                iconSource: "qrc:/qt/qml/Seriona/qml/assets/arrow_back.svg"
                                buttonWidth: 20
                                buttonHeight: 20
                                iconSize: 14
                                enabled: libraryController.canGoBack
                                onClicked: root.handleBackClicked()
                                SharedToolTip {
                                    text: qsTr("返回")
                                }
                            }

                            StyleButton {
                                id: searchButton
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                iconSource: "qrc:/qt/qml/Seriona/qml/assets/search.svg"
                                buttonWidth: 20
                                buttonHeight: 20
                                iconSize: 14
                                checkable: true
                                checked: root.isSearching
                                textColor: root.isSearching ? Theme.accentColor : Theme.textPrimary
                                onClicked: {
                                    root.closeMenus();
                                    root.isSearching = !root.isSearching;
                                    if (root.isSearching) {
                                        searchInput.forceActiveFocus();
                                    } else {
                                        libraryController.clearSearch();
                                    }
                                }
                                SharedToolTip {
                                    text: qsTr("搜索")
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.queueViewActive ? qsTr("播放队列") : libraryController.currentFolderName
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontTitle
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }

                            StyleButton {
                                id: sidebarMoreBtn
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                iconSource: "qrc:/qt/qml/Seriona/qml/assets/more_vert.svg"
                                buttonWidth: 20
                                buttonHeight: 20
                                iconSize: 14
                                onClicked: sidebarMenu.toggle()

                                SharedToolTip {
                                    text: qsTr("更多")
                                }

                                BubbleMenu {
                                    id: sidebarMenu
                                    targetItem: sidebarMoreBtn

                                    BubbleMenuItem {
                                        text: qsTr("排序")
                                        onTriggered: {
                                            sidebarMenu.close();
                                            sortDialog.sortRules = root.sortRulesForDialog();
                                            sortDialog.show();
                                        }
                                    }
                                    BubbleMenuItem {
                                        text: qsTr("强制重新扫描")
                                        onTriggered: {
                                            libraryController.forceRescan();
                                            sidebarMenu.close();
                                        }
                                    }
                                    BubbleMenuItem {
                                        text: qsTr("添加文件夹")
                                        onTriggered: {
                                            sidebarMenu.close();
                                            sidebarFolderDialog.open();
                                        }
                                    }
                                }
                            }

                            StyleButton {
                                Layout.preferredWidth: 20
                                Layout.preferredHeight: 20
                                iconSource: "qrc:/qt/qml/Seriona/qml/assets/close.svg"
                                buttonWidth: 20
                                buttonHeight: 20
                                iconSize: 14
                                onClicked: {
                                    root.closeMenus();
                                    root.closeClicked();
                                }
                                SharedToolTip {
                                    text: qsTr("关闭")
                                }
                            }
                        }
                    }

                    Item {
                        width: parent.width
                        height: 50
                        visible: opacity > 0.0
                        opacity: root.isSearching ? 1.0 : 0.0
                        z: 10

                        Behavior on opacity {
                            NumberAnimation {
                                duration: Theme.animationFast
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width - Theme.spacing24
                            height: 36
                            color: Theme.baseColor
                            radius: Theme.radiusLarge
                            border.color: searchInput.activeFocus ? Theme.borderAccent : Theme.borderSubtle
                            border.width: 1

                            Behavior on border.color {
                                ColorAnimation { duration: Theme.animationFast }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacing12
                                anchors.rightMargin: Theme.spacing12
                                spacing: Theme.spacing8

                                Item {
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18

                                    Image {
                                        id: searchFieldIcon
                                        anchors.fill: parent
                                        source: "qrc:/qt/qml/Seriona/qml/assets/search.svg"
                                        sourceSize.width: 18
                                        sourceSize.height: 18
                                        fillMode: Image.PreserveAspectFit
                                        visible: false
                                    }

                                    ColorOverlay {
                                        anchors.fill: searchFieldIcon
                                        source: searchFieldIcon
                                        color: searchInput.activeFocus ? Theme.accentColor : Theme.textSecondary
                                        Behavior on color {
                                            ColorAnimation { duration: Theme.animationFast }
                                        }
                                    }
                                }

                                TextField {
                                    id: searchInput
                                    Layout.fillWidth: true
                                    background: null
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    placeholderText: qsTr("搜索当前文件夹及子目录...")
                                    placeholderTextColor: Theme.textDisabled
                                    selectByMouse: true
                                    text: libraryController.searchQuery
                                    verticalAlignment: Text.AlignVCenter
                                    onTextEdited: {
                                        searchView.contentY = 0;
                                        libraryController.searchQuery = text
                                    }
                                    onAccepted: {
                                        searchView.contentY = 0;
                                        libraryController.submitSearch()
                                    }
                                }

                                Item {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    visible: libraryController.searchQuery.length > 0

                                    Image {
                                        id: clearFieldIcon
                                        anchors.fill: parent
                                        source: "qrc:/qt/qml/Seriona/qml/assets/close.svg"
                                        sourceSize.width: 16
                                        sourceSize.height: 16
                                        fillMode: Image.PreserveAspectFit
                                        visible: false
                                    }

                                    ColorOverlay {
                                        anchors.fill: clearFieldIcon
                                        source: clearFieldIcon
                                        color: clearFieldMouseArea.containsMouse ? Theme.textPrimary : Theme.textSecondary
                                    }

                                    MouseArea {
                                        id: clearFieldMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            searchView.contentY = 0;
                                            libraryController.clearSearch()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.borderSubtle
                    }
                }
            }

            // 视图切换条（T15）：文件夹视图 ↔ 队列视图。独立一行，不覆盖
            // 表头拖拽区/搜索行/条目列表的任何既有交互区域。
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: "transparent"

                Rectangle {
                    id: tabSwitchContainer
                    anchors.centerIn: parent
                    width: 176
                    height: 30
                    radius: Theme.radiusFull
                    color: Theme.borderSubtle
                    border.color: Theme.borderSubtle
                    border.width: 1

                    // 滑动指示胶囊（Sliding Pill Indicator）
                    Rectangle {
                        id: tabIndicator
                        width: 82
                        height: 26
                        radius: 13
                        y: 2
                        x: root.queueViewActive ? 90 : 4
                        color: Theme.hoverColor

                        Behavior on x {
                            NumberAnimation {
                                duration: Theme.animationStandard
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    Row {
                        anchors.centerIn: parent
                        spacing: 4

                        Item {
                            id: folderViewButton
                            objectName: "folderViewButton"
                            width: 82
                            height: 30

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.queueViewActive) {
                                        root.queueViewActive = false;
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("文件夹")
                                color: !root.queueViewActive ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontCaption + 1
                                font.weight: !root.queueViewActive ? Font.DemiBold : Font.Normal

                                Behavior on color {
                                    ColorAnimation { duration: Theme.animationFast }
                                }
                            }
                        }

                        Item {
                            id: queueViewButton
                            objectName: "queueViewButton"
                            width: 82
                            height: 30

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!root.queueViewActive) {
                                        root.queueViewActive = true;
                                        queueView.rebuild();
                                    }
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: qsTr("播放队列")
                                color: root.queueViewActive ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: Theme.fontCaption + 1
                                font.weight: root.queueViewActive ? Font.DemiBold : Font.Normal

                                Behavior on color {
                                    ColorAnimation { duration: Theme.animationFast }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: playlistPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Rectangle {
                    id: scanBanner
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacing12
                    height: 34
                    radius: Theme.radiusLarge
                    color: root.scanError ? Theme.toastErrorBg : Theme.baseColor
                    border.color: root.scanError ? Theme.dangerColor : Theme.borderColor
                    border.width: 1
                    visible: (root.scanRunning || root.scanError) && !root.queueViewActive
                    z: 2

                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing12
                        anchors.rightMargin: Theme.spacing12
                        text: root.scanMessage
                        color: root.scanError ? Theme.dangerColor : Theme.textSecondary
                        font.pixelSize: Theme.fontCaption + 1
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Item {
                    id: slidingTrack
                    anchors.fill: parent
                    transform: Translate {
                        id: trackTranslate
                        x: root.queueViewActive ? -playlistPanel.width : 0
                        Behavior on x {
                            NumberAnimation {
                                duration: 250
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    Item {
                        id: folderPage
                        width: playlistPanel.width
                        height: playlistPanel.height
                        x: 0

                        // 0级根目录列表视图（默认视图，常驻）
                        ListView {
                            id: playlistView
                            objectName: "playlistView"
                            anchors.fill: parent
                            visible: !root.queueViewActive && !root.isSearching && folderStack.depth === 0
                            spacing: 0
                            topMargin: Theme.paddingMedium + (scanBanner.visible ? scanBanner.height + 8 : 0)
                            bottomMargin: 80
                            clip: true
                            reuseItems: false
                            delegate: PlaylistDelegate {
                                activateNodeHandler: root.activateNode
                                closeMenusHandler: root.closeMenus
                                contextMenuHost: root.contextMenuHost
                            }

                            ScrollBar.vertical: StyledScrollBar {}
                        }

                        // 第 1 层及更深层文件夹列表（StackView 页面栈承载）
                        StackView {
                            id: folderStack
                            objectName: "folderStack"
                            anchors.fill: parent
                            visible: !root.queueViewActive && !root.isSearching
                            // depth 0 时空栈禁用输入：不拦截下方 playlistView 的点击
                            // （计划 :142 规格）；enabled 不影响 pop 过渡动画可见性
                            enabled: folderStack.depth > 0
                            // Qt 6 中 enabled=false 仍接收 hover（Qt5→6 行为变更）；
                            // 不加此属性会在 depth 0 拦截 hover，导致根视图条目无 hover 反馈
                            hoverEnabled: folderStack.depth > 0
                            z: folderStack.depth > 0 ? 2 : -1

                            pushEnter: Transition {
                                PropertyAnimation {
                                    property: "x"
                                    from: folderStack.width
                                    to: 0
                                    duration: 220
                                    easing.type: Easing.OutCubic
                                }
                                PropertyAnimation {
                                    property: "opacity"
                                    from: 0.0
                                    to: 1.0
                                    duration: 220
                                    easing.type: Easing.OutQuad
                                }
                            }
                            pushExit: Transition {
                                PropertyAnimation {
                                    property: "x"
                                    from: 0
                                    to: -folderStack.width * 0.3
                                    duration: 220
                                    easing.type: Easing.OutCubic
                                }
                                PropertyAnimation {
                                    property: "opacity"
                                    from: 1.0
                                    to: 0.0
                                    duration: 220
                                    easing.type: Easing.OutQuad
                                }
                            }
                            popEnter: Transition {
                                PropertyAnimation {
                                    property: "x"
                                    from: -folderStack.width * 0.3
                                    to: 0
                                    duration: 220
                                    easing.type: Easing.OutCubic
                                }
                                PropertyAnimation {
                                    property: "opacity"
                                    from: 0.0
                                    to: 1.0
                                    duration: 220
                                    easing.type: Easing.OutQuad
                                }
                            }
                            popExit: Transition {
                                PropertyAnimation {
                                    property: "x"
                                    from: 0
                                    to: folderStack.width
                                    duration: 220
                                    easing.type: Easing.OutCubic
                                }
                                PropertyAnimation {
                                    property: "opacity"
                                    from: 1.0
                                    to: 0.0
                                    duration: 220
                                    easing.type: Easing.OutQuad
                                }
                            }
                        }

                        // 搜索结果列表视图（绑定主模型）
                        ListView {
                            id: searchView
                            objectName: "searchView"
                            anchors.fill: parent
                            model: libraryController.model
                            visible: !root.queueViewActive && root.isSearching
                            spacing: 0
                            topMargin: Theme.paddingMedium + (scanBanner.visible ? scanBanner.height + 8 : 0)
                            bottomMargin: 80
                            clip: true
                            reuseItems: false
                            delegate: PlaylistDelegate {
                                activateNodeHandler: root.activateNode
                                closeMenusHandler: root.closeMenus
                                contextMenuHost: root.contextMenuHost
                            }

                            ScrollBar.vertical: StyledScrollBar {}
                        }
                    }

                    Connections {
                        target: libraryController

                        function onScrollRequestChanged() {
                            var reqNodeId = libraryController.scrollRequest;
                            if (!reqNodeId || reqNodeId.length === 0)
                                return;

                            var view = root.activeListView;
                            if (!view || !view.model)
                                return;

                            var row = root.rowForNodeInView(view, reqNodeId);
                            if (row >= 0) {
                                Qt.callLater(() => {
                                    view.positionViewAtIndex(row, ListView.Beginning);
                                    root.suppressNavAnimation = false;
                                });
                            } else {
                                // 目标不在当前激活页投影内：调用 locateNodeInFolderStack 逐级切栈后定位
                                if (typeof libraryController.locateNodeInFolderStack === "function") {
                                    libraryController.locateNodeInFolderStack(reqNodeId);
                                    Qt.callLater(() => {
                                        var newView = root.activeListView;
                                        var newRow = root.rowForNodeInView(newView, reqNodeId);
                                        if (newView && newRow >= 0) {
                                            newView.positionViewAtIndex(newRow, ListView.Beginning);
                                        }
                                        root.suppressNavAnimation = false;
                                    });
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacing24 * 2
                        text: root.emptyStateText
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: !root.queueViewActive && (root.activeListView ? root.activeListView.count === 0 : true)
                    }

                    Item {
                        id: queuePage
                        width: playlistPanel.width
                        height: playlistPanel.height
                        x: playlistPanel.width

                        // 队列视图（T15）：展示临时队列，空队列显示引导文案；
                        // 移除/右键命令经信号上抛，由下方 onRemoveRequested/
                        // onContextMenuRequested 接 AppFacade 与 TrackContextMenu。
                        QueueView {
                            id: queueView
                            objectName: "queueListView"
                            anchors.fill: parent
                            visible: root.queueViewActive
                            transitionDirection: 0
                            queueEntries: root.appFacade.playback.queueEntries

                            onRemoveRequested: (index) => root.appFacade.removeFromQueue(index)

                            onContextMenuRequested: (index, targetDelegate, mouseX, mouseY) => {
                                const entry = root.appFacade.playback.queueEntries[index];
                                if (!entry)
                                    return;
                                root.closeMenus();
                                trackContextMenu.openForEntry({
                                    nodeId: entry.nodeId,
                                    trackId: entry.trackId,
                                    isFolder: false,
                                    name: entry.title,
                                    title: entry.title,
                                    artist: entry.artist,
                                    album: "",
                                    parentName: "",
                                    songCount: 0,
                                    duration: "",
                                    format: "",
                                    sampleRate: 0,
                                    bitDepth: 0,
                                    artworkSource: "",
                                    year: 0,
                                    path: root.appFacade.filePathForNodeId(entry.nodeId),
                                    queueIndex: index
                                }, targetDelegate, mouseX, mouseY);
                            }
                        }
                    }
                }
            }
        }

        Platform.FolderDialog {
            id: sidebarFolderDialog
            title: qsTr("选择音乐文件夹")

            onAccepted: root.appFacade.scanLibrary(folder)
        }

        MouseArea {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 50
            anchors.bottom: parent.bottom
            z: 9
            visible: root.hasOpenMenu
            enabled: visible
            onClicked: root.closeMenus()
        }

        // Floating Action Button (FAB)
        StyleButton {
            id: fab
            objectName: "locateFab"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing16
            buttonWidth: 40
            buttonHeight: 40
            iconSize: 20
            iconSource: "qrc:/qt/qml/Seriona/qml/assets/my_location.svg"
            baseColor: Theme.accentColor
            textColor: Theme.textOnAccent
            z: 10
            visible: !root.queueViewActive

            onClicked: {
                // 搜索激活时先退出搜索再定位：定位的滚动请求按 activeListView
                // 处理（isSearching 时指向 searchView），且深层曲目不在搜索投影内；
                // 必须先退出搜索模式（与搜索按钮关闭分支一致），让滚动请求
                // 落在文件夹视图上。C++ 侧 showNodeInBrowserProjection 会兜底
                // 清空 searchQuery，此处显式先行保证顺序正确。
                if (root.isSearching) {
                    root.isSearching = false;
                    libraryController.clearSearch();
                }
                libraryController.locateCurrentSong();
                Qt.callLater(() => {
                    var ctrlDepth = libraryController.folderStackDepth;
                    var stackDepth = folderStack.depth;
                    var targetNodeId = libraryController.currentFolderNodeId;
                    if (stackDepth === ctrlDepth) {
                        if (ctrlDepth === 0) {
                            if (targetNodeId === "") {
                                root.suppressNavAnimation = false;
                            }
                        } else if (folderStack.currentItem && folderStack.currentItem.folderNodeId === targetNodeId) {
                            root.suppressNavAnimation = false;
                        }
                    }
                });
            }

            // Shadow for FAB
            layer.enabled: true
            layer.effect: DropShadow {
                transparentBorder: true
                horizontalOffset: Theme.shadowCardOffsetX
                verticalOffset: Theme.shadowCardOffsetY
                radius: Theme.shadowCardBlur
                samples: 17
                color: Theme.shadowPopupColor
            }
        }
    }

    SortDialog {
        id: sortDialog
        transientParent: root.Window.window

        sortRules: []
        
        onAccepted: {
            libraryController.applySortRules(sortRules);
        }
    }

    // 播放列表条目右键菜单（T14）：详情 / 添加到下一首播放 / 删除（含确认弹窗）；
    // "从队列移除"（T15）在队列视图上下文可见（queueContext 绑定视图状态）。
    TrackContextMenu {
        id: trackContextMenu
        objectName: "trackContextMenu"
        appFacade: root.appFacade
        queueContext: root.queueViewActive
    }
}
