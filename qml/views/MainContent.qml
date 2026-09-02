import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQml.Models
import Qt5Compat.GraphicalEffects
import QtQuick.Effects
import Seriona

Item {
    id: root

    signal coverClicked()
    signal coverDragRequested()
    signal backClicked()
    signal playlistToggled()
    signal exitRequested()
    signal openSettingsRequested()
    signal openEqualizerRequested()
    property bool isSidebarOpen: false
    required property PlaybackController playbackController
    required property NotificationController notifications
    required property LibraryController libraryController
    readonly property bool hasOpenMenu: mainMenu.visible

    // 切换歌曲动画方向
    // 1: 切换下一首 (歌词从右滑入)，-1: 切换上一首 (歌词从左滑入)
    property int lyricsSwitchDirection: 1

    // ---- 切歌逐行划出/划入动画状态（两段式：先退场 → 换数据 → 再进入）----
    property bool lyricsAnimBusy: false        // 动画相位进行中（退场或进入）
    property bool lyricsAnimEntering: false    // 相位③ 进入（用于区分 modelReset 到达时的缓冲语义）
    property var pendingLyricsRows: []         // modelReset 到达但退场未完成时的新歌数据缓冲

    // 歌词数据源：QAbstractListModel 的 rowCount()/data() 非 Q_INVOKABLE，QML 无法直接调用，
    // 通过 LyricsModel::lines()（Q_INVOKABLE）读取全部行做快照
    // 歌词显示层快照：Repeater 只绑这个 ListModel，与 C++ LyricsModel 解耦，
    // 使"退场动画播完才换数据"的两段式时序完全可控（LyricsModel 的 modelReset 不再直接重建视图）
    ListModel {
        id: lyricsViewList
    }

    // 切歌动画相位协调器（root 作用域：QML 中 id 不是对象属性，lyricsContainer 内无法 root 访问）
    Timer {
        id: lyricsPhaseTimer
        interval: 400
        repeat: false
        onTriggered: root.finishLyricsPhase()
    }

    // 复制 LyricsModel 全部行为 JS 数组（isCurrent 不复制：行高亮实时绑定 lyricsState.currentIndex）
    function snapshotRowsFromLyricsState() {
        var rows = [];
        var arr = lyricsState.lines();
        for (var i = 0; i < arr.length; ++i) {
            rows.push({
                displayLine: arr[i].displayLine,
                translation: arr[i].translation,
                timestampSec: arr[i].timestampSec
            });
        }
        return rows;
    }

    function applyRowsToView(rows) {
        lyricsViewList.clear();
        for (var i = 0; i < rows.length; ++i)
            lyricsViewList.append(rows[i]);
    }

    // 无动画直接同步快照（非歌词态切歌 / modelReset 时空闲）
    function applyLyricsSnapshotDirectly() {
        applyRowsToView(snapshotRowsFromLyricsState());
        if (lyricsContainer && root.state === "lyrics")
            lyricsContainer.scheduleSnapToCurrentLyric();
    }

    // 相位① 退场：对可视范围内旧行逐行划出，随后协调器进入换数据
    function beginLyricsSwitch() {
        if (lyricsAnimBusy) {
            // 动画中再次切歌：打断重排（现有行全部复位，重新退场）
            lyricsContainer.resetAllLines();
        }
        lyricsAnimBusy = true;
        lyricsAnimEntering = false;
        pendingLyricsRows = [];
        var range = lyricsContainer.visibleLineRange();
        if (range[1] < range[0]) {
            // 当前无歌词：无行可退，直接进入换数据
            finishLyricsExitPhase();
            return;
        }
        var n = range[1] - range[0] + 1;
        for (var i = range[0]; i <= range[1]; ++i) {
            var it = lyricsRepeater.itemAt(i);
            if (it)
                it.startLineExit(lyricsSwitchDirection, i - range[0]);
        }
        lyricsPhaseTimer.interval = (n - 1) * 25 + 260 + 40; // 最后一行延迟+动画时长+余量
        lyricsPhaseTimer.restart();
    }

    // 相位② 换数据：应用新歌快照（modelReset 已把新数据存进 pendingLyricsRows；未到则兜底直读）
    function finishLyricsExitPhase() {
        if (pendingLyricsRows.length === 0)
            pendingLyricsRows = snapshotRowsFromLyricsState();
        // 先置进入相位标志再换数据：新 delegate 创建即"屏外透明"初始态，
        // 避免"先显示新歌词、再播进入动画"的闪现帧（与文件夹动画第一次编写时的坑相同）
        lyricsAnimEntering = true;
        applyRowsToView(pendingLyricsRows);
        pendingLyricsRows = [];
        // 等布局稳定（新 delegate 已就位、contentHeight 已更新）再进入相位③
        Qt.callLater(root.beginLyricsEnter);
    }

    // 相位③ 进入：对可视范围内新行逐行划入，完成后恢复 idle 并定位当前行
    function beginLyricsEnter() {
        lyricsAnimEntering = true;
        var range = lyricsContainer.visibleLineRange();
        if (range[1] < range[0]) {
            finishLyricsEnterPhase();
            return;
        }
        var n = range[1] - range[0] + 1;
        for (var i = range[0]; i <= range[1]; ++i) {
            var it = lyricsRepeater.itemAt(i);
            if (it)
                it.startLineEnter(lyricsSwitchDirection, i - range[0]);
        }
        lyricsPhaseTimer.interval = (n - 1) * 25 + 260 + 40;
        lyricsPhaseTimer.restart();
    }

    // 协调器统一回调：退场完成 → 换数据；进入完成 → 恢复
    function finishLyricsPhase() {
        if (lyricsAnimEntering) {
            lyricsAnimEntering = false;
            lyricsAnimBusy = false;
            lyricsContainer.scheduleSnapToCurrentLyric();
        } else {
            finishLyricsExitPhase();
        }
    }

    Connections {
        target: root.playbackController

        function onCurrentSongChanged() {
            // 切歌后恢复歌词自动跟随；歌词界面内播放逐行划出/划入动画
            // （此刻 LyricsModel 仍是旧歌，modelReset 稍后到达——两段式时序的关键提前量）
            if (lyricsContainer) {
                lyricsContainer.lyricsSyncToPlayback = true;
                if (root.state === "lyrics")
                    root.beginLyricsSwitch();
                lyricsRestoreTimer.stop();
            }
        }
    }
    readonly property bool scanRunning: root.libraryController ? (root.libraryController.scanStatus === "running") : false
    readonly property string scanToastTitle: qsTr("正在扫描曲库")
    readonly property string scanToastMessage: root.libraryController && root.libraryController.totalSongCount > 0
        ? qsTr("正在扫描：%1 / %2").arg(root.libraryController.scannedSongCount).arg(root.libraryController.totalSongCount)
        : qsTr("正在扫描曲库…")
    readonly property real scanToastProgress: root.libraryController && root.libraryController.totalSongCount > 0
        ? Math.min(root.libraryController.scannedSongCount / root.libraryController.totalSongCount, 1.0)
        : 0.0

    readonly property real playbackTimelineDuration: playbackController.totalDuration
    readonly property real playbackTimelinePosition: playbackController.currentPosition
    readonly property real boundedPlaybackTimelinePosition: playbackTimelineDuration > 0 ? Math.max(0, Math.min(playbackTimelinePosition, playbackTimelineDuration)) : 0
    readonly property real playbackTimelineProgress: playbackTimelineDuration > 0 ? boundedPlaybackTimelinePosition / playbackTimelineDuration : 0
    property bool toastVisible: false
    property string toastTitle: ""
    property string toastMessage: ""
    property string toastCode: ""
    property string toastSeverity: "info"
    required property LyricsModel lyricsState
    required property SettingsController settings

    Binding {
        target: lyricsState
        property: "playbackPosition"
        value: root.boundedPlaybackTimelinePosition
    }

    // 时间格式化辅助函数
    function formatTime(seconds) {
        if (seconds < 0) seconds = 0;
        var m = Math.floor(seconds / 60);
        var s = Math.floor(seconds % 60);
        return (m < 10 ? "0" + m : m) + ":" + (s < 10 ? "0" + s : s);
    }

    function closeMenus() {
        mainMenu.close();
    }

    function openMenuForSmoke() {
        mainMenu.showAtTarget();
    }

    function showUnsupportedFeedback(actionName) {
        root.notifications.showUnsupportedAction(actionName);
        mainMenu.close();
    }

    function showLatestNotificationToast() {
        if (root.scanRunning || !root.notifications.hasNotification)
            return;

        toastTitle = root.notifications.latestTitle;
        toastMessage = root.notifications.latestMessage;
        toastCode = root.notifications.latestCode;
        toastSeverity = root.notifications.latestSeverity;
        notificationClearTimer.stop();
        toastVisible = true;
        notificationAutoHideTimer.restart();
    }

    function hideNotificationToast() {
        if (!toastVisible || root.scanRunning)
            return;

        toastVisible = false;
        notificationClearTimer.restart();
    }

    // 初始快照：启动时把 LyricsModel 已有内容同步到显示层（无动画）
    Component.onCompleted: {
        showLatestNotificationToast();
        root.applyLyricsSnapshotDirectly();
    }

    Connections {
        target: root.notifications

        function onLatestNotificationChanged() {
            root.showLatestNotificationToast();
        }
    }

    Connections {
        target: root.libraryController

        function onScanStatusChanged() {
            if (root.scanRunning) {
                toastSeverity = "info";
                toastCode = "";
                notificationAutoHideTimer.stop();
                toastVisible = true;
            } else if (toastVisible) {
                toastVisible = false;
                notificationClearTimer.restart();
            }
        }
    }

    Timer {
        id: notificationAutoHideTimer
        interval: 3200
        onTriggered: root.hideNotificationToast()
    }

    Timer {
        id: notificationClearTimer
        interval: 260
        onTriggered: {
            if (!root.toastVisible)
                root.notifications.clear();
        }
    }

    Rectangle {
        id: notificationToast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.toastVisible ? 12 : -height
        width: Math.min(
            Math.max(notificationColumn.implicitWidth + Theme.paddingLarge * 2, 240),
            320
        )
        height: notificationColumn.implicitHeight + Theme.paddingLarge
        radius: height / 2
        color: root.toastSeverity === "error" ? Theme.toastErrorBg : root.toastSeverity === "warning" ? Theme.toastWarningBg : Theme.toastInfoBg
            border.color: root.toastSeverity === "error" ? Theme.dangerColor : root.toastSeverity === "warning" ? Theme.toastWarningBorder : Theme.hoverColor
        border.width: 1
        opacity: root.toastVisible ? 1.0 : 0.0
        visible: opacity > 0.0 || root.toastVisible
        z: 300

        Behavior on anchors.bottomMargin {
            NumberAnimation {
                duration: 240
                easing.type: root.toastVisible ? Easing.OutCubic : Easing.InCubic
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 180
                easing.type: root.toastVisible ? Easing.OutQuad : Easing.InQuad
            }
        }

        Column {
            id: notificationColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.paddingMedium
            anchors.rightMargin: Theme.paddingMedium
            spacing: 2

            Text {
                width: parent.width
                text: root.scanRunning ? root.scanToastTitle : root.toastTitle
                color: Theme.textColor
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: root.scanRunning
                    ? root.scanToastMessage
                    : (root.toastCode.length > 0 && root.toastCode !== "None" && root.toastCode !== "Unsupported"
                        ? qsTr("%1（%2）").arg(root.toastMessage).arg(root.toastCode)
                        : root.toastMessage)
                color: Theme.secondaryTextColor
                font.pixelSize: 11
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            ProgressBar {
                id: scanProgressBar
                visible: root.scanRunning
                width: parent.width
                implicitHeight: 4
                value: root.scanToastProgress
                indeterminate: root.scanRunning && root.libraryController ? (root.libraryController.totalSongCount === 0) : false
                padding: 0

                background: Rectangle {
                    implicitHeight: 4
                    radius: 2
                    color: Theme.borderColor
                }

                contentItem: Rectangle {
                    implicitHeight: 4
                    radius: 2
                    color: Theme.mainColor
                }
            }
        }
    }

    // 1. 播放布局定位辅助器 (仅在 playback 状态下用于定位)
    Item {
        id: positionHelper
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.max(8, (parent.height - 650) / 2)
        // 内容区宽度：小窗口保底 320（保持原布局），大窗口/最大化跟随窗口宽度
        width: Math.max(320, root.width - 2 * Theme.paddingLarge)
        height: 650
        visible: false
    }

    // 2. 封面组件 (共享元素)
    Item {
        id: coverContainer
        width: 240
        height: 240
        z: 10
        x: (parent.width - 240) / 2
        y: positionHelper.y + 5

        RectangularGlow {
            id: coverGlow
            anchors.fill: coverRect
            glowRadius: 30
            spread: 0.1
            color: Theme.shadowCardColor
            cornerRadius: coverRect.radius + 15
            z: -1
            opacity: 1.0
        }

        Rectangle {
            id: coverRect
            anchors.fill: parent
            radius: 24
            color: Theme.mainColor
            antialiasing: true
            layer.enabled: true
            layer.effect: OpacityMask {
                maskSource: Rectangle {
                    width: coverRect.width
                    height: coverRect.height
                    radius: coverRect.radius
                }
            }

            Text {
                id: coverIcon
                anchors.centerIn: parent
                text: root.playbackController.coverPlaceholderText
                font.pixelSize: 72
                color: Theme.textColor
                opacity: 0.6
                visible: preferredCover.status !== Image.Ready && fallbackCover.status !== Image.Ready
            }

            // 缩略图回退层：preferred 未 Ready 且与缩略图不同源时显示，曲目切换由绑定自然重置
            Image {
                id: fallbackCover
                anchors.fill: parent
                source: root.playbackController.coverThumbnailSource
                sourceSize.width: 240
                sourceSize.height: 240
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: preferredCover.status !== Image.Ready
                         && status === Image.Ready
                         && root.playbackController.coverArtworkSource !== root.playbackController.coverThumbnailSource
            }

            // 首选封面层（全图，或缩略图优先阶段的同源缩略图）
            Image {
                id: preferredCover
                anchors.fill: parent
                source: root.playbackController.coverArtworkSource
                sourceSize.width: 240
                sourceSize.height: 240
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                property real pressX: 0
                property real pressY: 0
                property bool suppressClick: false

                onPressed: function (mouse) {
                    pressX = mouse.x;
                    pressY = mouse.y;
                    suppressClick = false;
                }

                onPositionChanged: function (mouse) {
                    if (pressed && root.state === "playback" && !root.hasOpenMenu && !suppressClick) {
                        var dx = mouse.x - pressX;
                        var dy = mouse.y - pressY;
                        if (Math.sqrt(dx * dx + dy * dy) >= Qt.styleHints.startDragDistance) {
                            suppressClick = true;
                            root.coverDragRequested();
                        }
                    }
                }

                onClicked: {
                    if (suppressClick) {
                        suppressClick = false;
                        return;
                    }

                    if (root.state === "playback") {
                        root.coverClicked();
                    } else {
                        root.backClicked();
                    }
                }
            }
        }
    }

    // 3. 歌曲元数据组件 (共享元素)
    Item {
        id: metadataContainer
        width: positionHelper.width
        height: metadataLayout.implicitHeight
        z: 9
        anchors.top: coverContainer.bottom
        anchors.topMargin: 16
        anchors.horizontalCenter: positionHelper.horizontalCenter

        Item {
            id: metadataLayout
            anchors.fill: parent

            MarqueeText {
                id: titleText
                width: Math.min(implicitWidth, parent.width)
                text: root.playbackController.songTitle
                color: Theme.textColor
                font.pixelSize: 22
                font.bold: true
                font.letterSpacing: 0.5
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
            }

            MarqueeText {
                id: artistText
                width: Math.min(implicitWidth, parent.width)
                text: root.playbackController.artistName
                color: Theme.secondaryTextColor
                font.pixelSize: 15
                font.weight: Font.Medium
                // 歌词态交叉淡化到合并行后自动隐藏（visible 不可动画，opacity 归零即隐）
                visible: opacity > 0.0
                anchors.top: titleText.bottom
                anchors.topMargin: 8
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                id: dashText
                width: Math.min(implicitWidth, parent.width)
                text: " — "
                color: Theme.secondaryTextColor
                font.pixelSize: 15
                opacity: 0.0
                visible: opacity > 0.0
                anchors.verticalCenter: artistText.verticalCenter
            }

            MarqueeText {
                id: albumText
                width: Math.min(implicitWidth, parent.width)
                text: root.playbackController.albumName
                color: Theme.secondaryTextColor
                font.pixelSize: 13
                opacity: 0.8
                visible: opacity > 0.0
                anchors.top: artistText.bottom
                anchors.topMargin: 6
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // 歌词界面合并行：歌手 — 专辑 单行整体滚动（一个滚动单元）。
            // 行业惯例（网易云桌面歌词/Apple Music mini player/VLC 副信息行等）：紧凑信息行
            // 长文本整行滚动，而非各字段各自滚动。文本拼接带空字段兜底（任一为空只显另一）。
            // 锚定跟随 titleText（两态同一绑定），播放态 opacity 0 不可见，切换时随 title 轨道
            // 淡入到位；原三控件在歌词态 opacity 0 隐藏，各自轨道锚定动画保留。
            MarqueeText {
                id: metaCombinedText
                // 必须显式限宽：不设则 Item 采用隐式尺寸(=implicitWidth)，isOverflow 恒 false 永不滚动
                width: Math.min(implicitWidth, parent.width)
                text: {
                    var a = root.playbackController.artistName;
                    var al = root.playbackController.albumName;
                    if (a && al)
                        return a + " — " + al;
                    return a || al;
                }
                color: Theme.secondaryTextColor
                font.pixelSize: 13
                font.weight: Font.Medium
                opacity: 0.0
                visible: opacity > 0.0 && (root.playbackController.artistName.length + root.playbackController.albumName.length > 0)
                anchors.top: titleText.bottom
                anchors.topMargin: 8
                anchors.left: metadataLayout.left
            }
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.state === "lyrics"
            cursorShape: Qt.PointingHandCursor
            onClicked: root.backClicked()
        }
    }

    // 4. 歌词滚动区域 (仅在 lyrics 状态下显示)
    // Flickable + Column + Repeater：主动滚动（切行/点击/恢复）由显式 SmoothedAnimation
    // （lyricsScrollAnim）驱动；行高/容器尺寸变化的被动补偿在 onContentHeightChanged 内同帧瞬时写
    // contentY（保持锚点行屏幕位置，无中间帧跳变）。两路分离避免常驻 Behavior on contentY 把
    // 同帧补偿变成动画（跳走再滚回），且不依赖 ListView highlight 机制（行高变化不会瞬移）。
    Flickable {
        id: lyricsContainer
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        anchors.top: metadataContainer.bottom
        anchors.topMargin: 8
        anchors.bottom: linearProgressContainer.top
        anchors.bottomMargin: 10
        // 宽度跟随 positionHelper（= 窗口减边距，小窗口保底 320），两状态一致无切换跳变
        anchors.left: positionHelper.left
        anchors.right: positionHelper.right
        opacity: 0.0
        visible: opacity > 0.0
        z: 11

        // 播放同步滚动：true=自动跟随当前行；用户拖动浏览时置 false，避免动画与手指竞争
        property bool lyricsSyncToPlayback: true

        // 翻译区展开因子（0..1）：showTranslation 翻转时由 Behavior 平滑动画。
        // 所有行共享同一条动画曲线（单动画驱动全表同步展开/收缩），
        // 各行翻译容器的高度/透明度均绑定该因子；行间距随因子连续增长，无首帧台阶
        property real translationExpand: lyricsState.showTranslation ? 1 : 0

        Behavior on translationExpand {
            NumberAnimation {
                duration: 240
                easing.type: Easing.InOutQuad
            }
        }

        // 底部填充（容器高 3/4，显式加在 contentHeight 上）：最后一行行中心需滚到上 1/4 锚点，
        // 其下需有 3/4 容器高的内容空间；不加则内容滚到底即 clamp，最后几句歌词停在视口底部。
        // 显式加法不依赖 Column/Positioner 对动态子项的布局行为（与 Qcm 等歌词实现用
        // ListView footer 补 vh*(1-锚点比) 的做法同构），内容总高恒 = 歌词总高 + 3H/4。
        contentWidth: lyricsColumn.width
        contentHeight: lyricsColumn.height + lyricsContainer.height * 3 / 4

        // 显式滚动动画（替代常驻 Behavior on contentY）：只在主动滚动点启动，
        // 运行中改 to 平滑重定向；行高变化的被动补偿走瞬时写，不受动画拦截
        SmoothedAnimation {
            id: lyricsScrollAnim
            target: lyricsContainer
            property: "contentY"
            velocity: 400
            duration: 400
        }

        // 有未决的主动定位（scheduleSnapToCurrentLyric 已排队、动画未启动）：
        // 期间的行高变化（切行加粗）并入该定位，不做瞬时补偿，避免打断切行平滑滚动
        property bool snapPending: false

        // 当前行锚点目标：行中心锚定容器上 1/4 处。
        // 行顶保护（超长折行行高 > 容器高/2 时，行中心锚定会把行顶推出视口顶，行首被裁）：
        // 目标位置不得超过 item.y（行顶贴视口顶），普通行（行高 < 容器高/2）时取行中心锚点，行为不变。
        // 无有效当前行/行尚未实例化时返回 -1
        function snapToCurrentLyricTarget() {
            const idx = lyricsState.currentIndex;
            if (idx < 0)
                return -1;
            const item = lyricsRepeater.itemAt(idx);
            if (!item)
                return -1;
            const centerTarget = item.y + item.height / 2 - lyricsContainer.height / 4;
            const targetY = Math.min(centerTarget, item.y);
            return Math.max(0, Math.min(targetY, lyricsContainer.contentHeight - lyricsContainer.height));
        }

        // 主动平滑滚动到当前行锚点（切行/点击跳转/闲置恢复/切歌后定位）
        function snapToCurrentLyric() {
            snapPending = false;
            const targetY = snapToCurrentLyricTarget();
            if (targetY < 0)
                return;
            lyricsScrollAnim.to = targetY;
            if (!lyricsScrollAnim.running)
                lyricsScrollAnim.start();
        }

        // 被动布局变化（行高/容器尺寸变化）补偿：跟随态下保持锚点行屏幕位置不动。
        // Positioner 先重排子项、contentHeight 绑定后求值，本处理器读到的是新几何；
        // 直接写 contentY 与变化同帧完成（无 Qt.callLater 中间帧、无动画拦截 → 不跳不滚回）。
        // 有主动定位未决/进行中（切行动画）时不打断：未启动则跳过（由 snap 动画收敛），
        // 运行中则重定向动画目标（SmoothedAnimation 平滑衔接）
        function compensateLayoutChange() {
            if (root.state !== "lyrics" || !lyricsSyncToPlayback)
                return;
            if (snapPending)
                return;
            const targetY = snapToCurrentLyricTarget();
            if (targetY < 0)
                return;
            if (lyricsScrollAnim.running) {
                lyricsScrollAnim.to = targetY;
            } else {
                lyricsContainer.contentY = targetY;
            }
        }

        // 主动定位入口：延迟到布局稳定后（行高变化完成）再定位；
        // 仅歌词态跟随：切换动画期间（锚链驱动 height 连续变化）不重算 contentY，
        // 避免歌词列表在淡出窗口内整体下移又回位的闪烁
        function scheduleSnapToCurrentLyric() {
            if (root.state === "lyrics" && lyricsSyncToPlayback) {
                snapPending = true;
                Qt.callLater(lyricsContainer.snapToCurrentLyric);
            }
        }

        // 切歌动画：可视范围裁剪——只对 [firstVisible, lastVisible]（±1 行余量）内的行播动画，
        // 视口外行全程被 clip 裁剪不可见，动画是纯开销（行高可变，须按实际 y/height 遍历求交集）
        function visibleLineRange() {
            var count = lyricsViewList.count;
            if (count === 0)
                return [0, -1];
            var top = lyricsContainer.contentY;
            var bottom = top + lyricsContainer.height;
            var first = -1, last = -1;
            for (var i = 0; i < count; ++i) {
                var it = lyricsRepeater.itemAt(i);
                if (!it)
                    continue;
                var y0 = it.y, y1 = it.y + it.height;
                if (y1 < top || y0 > bottom) {
                    if (first >= 0) {
                        last = i - 1;
                        break;
                    }
                    continue;
                }
                if (first < 0)
                    first = i;
                last = i;
            }
            if (first < 0)
                return [0, -1];
            return [Math.max(0, first - 1), Math.min(count - 1, last + 1)];
        }

        // 退场相位：可视行从左/右划出（方向由 lyricsSwitchDirection 贯穿）；可见区间与时长由 root 协调
        function startLinesExit(direction) {
            var range = visibleLineRange();
            if (range[1] < range[0])
                return;
            for (var i = range[0]; i <= range[1]; ++i) {
                var it = lyricsRepeater.itemAt(i);
                if (it)
                    it.startLineExit(direction, i - range[0]);
            }
        }

        // 进入相位：新行从另一侧划入（可视行）
        function startLinesEnter(direction) {
            var range = visibleLineRange();
            if (range[1] < range[0])
                return;
            for (var i = range[0]; i <= range[1]; ++i) {
                var it = lyricsRepeater.itemAt(i);
                if (it)
                    it.startLineEnter(direction, i - range[0]);
            }
        }

        // 打断重排：全部行复位到原位（快速连续切歌时先复位再重新退场）
        function resetAllLines() {
            var count = lyricsViewList.count;
            for (var i = 0; i < count; ++i) {
                var it = lyricsRepeater.itemAt(i);
                if (it)
                    it.resetLine();
            }
        }

        // 用户开始滚动 → 停车（暂停自动跟随）并重置空闲计时
        function parkLyricsFollow() {
            lyricsSyncToPlayback = false;
            lyricsRestoreTimer.restart();
        }

        // 用户手动拖动/滚轮 → 停车；停止滚动（含惯性结束）后重新计时
        onFlickStarted: parkLyricsFollow()
        onDragStarted: parkLyricsFollow()
        onMovementStarted: parkLyricsFollow()
        onMovementEnded: lyricsRestoreTimer.restart()
        // 停车态兜底：任何滚动源（滚动条/程序化）导致的 contentY 变化都重置空闲计时
        onContentYChanged: if (!lyricsSyncToPlayback) lyricsRestoreTimer.restart()

        // 空闲 N 秒（设置可调，默认 5s）无操作后自动恢复自动跟随并平滑回拉当前行
        Timer {
            id: lyricsRestoreTimer
            interval: root.settings.followRestoreDelayMs
            repeat: false
            onTriggered: {
                if (root.state !== "lyrics")
                    return;
                lyricsContainer.lyricsSyncToPlayback = true;
                lyricsContainer.snapToCurrentLyric();
            }
        }

        // 行高变化（翻译展开/加粗折行）或容器尺寸变化 → 同帧瞬时补偿，保持锚点行屏幕位置
        onContentHeightChanged: compensateLayoutChange()
        onHeightChanged: compensateLayoutChange()

        Connections {
            target: lyricsState

            function onCurrentIndexChanged() {
                // 播放推进到新行 → 滚动到当前行（用户拖动浏览中则不打扰，保持浏览位置）
                lyricsContainer.scheduleSnapToCurrentLyric();
            }
        }

        // 行切换滚动改由 lyricsScrollAnim（显式 SmoothedAnimation）驱动：
        // 目标连续变化时从当前值+当前速度平滑重定向，动画必触发；
        // 行高/容器变化的被动补偿走 compensateLayoutChange（瞬时写），不受此处动画拦截

        Column {
            id: lyricsColumn
            width: lyricsContainer.width

            Repeater {
                id: lyricsRepeater
                model: lyricsViewList

                delegate: Item {
                    id: delegateItem
                    required property int index
                    required property string displayLine
                    required property string translation
                    required property real timestampSec
                    width: lyricsContainer.width
                    height: lyricColumn.implicitHeight + Theme.paddingLarge

                    // 行高亮实时跟随播放进度（currentIndex 来自 C++ LyricsModel，快照只存静态文本）
                    readonly property bool isActive: index === lyricsState.currentIndex

                    // ---- 切歌逐行划出/划入动画（transform 纯视觉，不触发布局/锚点）----
                    // 初始态绑定进入相位：数据加载帧（applyRowsToView 后、进入动画启动前）保持屏外透明，
                    // 避免新歌词先显示再划入的闪现；startLineEnter/startLineExit 的赋值会解除绑定接管动画
                    opacity: root.lyricsAnimEntering ? 0.0 : 1.0
                    transform: Translate {
                        id: lineSlide
                        x: root.lyricsAnimEntering ? (root.lyricsSwitchDirection >= 0 ? lyricsContainer.width : -lyricsContainer.width) : 0
                    }

                    SequentialAnimation {
                        id: lineExitAnim
                        PauseAnimation {
                            id: lineExitDelay
                            duration: 0
                        }
                        ParallelAnimation {
                            NumberAnimation {
                                id: lineExitX
                                target: lineSlide
                                property: "x"
                                duration: 260
                                easing.type: Easing.InCubic
                            }
                            NumberAnimation {
                                target: delegateItem
                                property: "opacity"
                                to: 0
                                duration: 180
                                easing.type: Easing.OutQuad
                            }
                        }
                    }

                    SequentialAnimation {
                        id: lineEnterAnim
                        PauseAnimation {
                            id: lineEnterDelay
                            duration: 0
                        }
                        ParallelAnimation {
                            NumberAnimation {
                                target: lineSlide
                                property: "x"
                                to: 0
                                duration: 260
                                easing.type: Easing.OutCubic
                            }
                            NumberAnimation {
                                target: delegateItem
                                property: "opacity"
                                to: 1
                                duration: 220
                                easing.type: Easing.OutQuad
                            }
                        }
                    }

                    // 退场：下一首向左划出（x → -width），上一首向右划出（x → +width）；step×25ms 错峰
                    function startLineExit(direction, step) {
                        lineExitAnim.stop();
                        lineSlide.x = 0;
                        lineExitX.to = -(direction >= 0 ? 1 : -1) * lyricsContainer.width;
                        lineExitDelay.duration = step * 25;
                        lineExitAnim.start();
                    }

                    // 进入：下一首从右划入（x 从 +width → 0），上一首从左划入（x 从 -width → 0）
                    function startLineEnter(direction, step) {
                        lineEnterAnim.stop();
                        lineSlide.x = (direction >= 0 ? 1 : -1) * lyricsContainer.width;
                        delegateItem.opacity = 0.0;
                        lineEnterDelay.duration = step * 25;
                        lineEnterAnim.start();
                    }

                    // 打断复位（快速连续切歌 / 应用新快照前）
                    function resetLine() {
                        lineExitAnim.stop();
                        lineEnterAnim.stop();
                        lineSlide.x = 0;
                        delegateItem.opacity = 1.0;
                    }

                    Column {
                        id: lyricColumn
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        // 跟随歌词容器宽度（小窗口与原 296 一致，最大化时拓宽）
                        width: parent.width - Theme.paddingLarge * 2
                        // 行距（主歌词↔翻译的 4px）并入翻译容器高度增量：
                        // Qt 对 height==0 的子项整项剔除，若依赖 spacing 会在展开首帧产生 4px×行数 的台阶
                        spacing: 0

                        transformOrigin: Item.Center
                        scale: delegateItem.isActive ? 1.0 : 0.75

                        Behavior on scale {
                            NumberAnimation { duration: 350; easing.type: Easing.InOutQuad }
                        }

                        Text {
                            id: lyricText
                            width: parent.width
                            text: delegateItem.displayLine
                            color: Theme.textColor
                            font.pixelSize: 32
                            font.weight: delegateItem.isActive ? Font.Bold : Font.Normal
                            opacity: delegateItem.isActive ? 1.0 : 0.4
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            Behavior on opacity {
                                NumberAnimation { duration: 350; easing.type: Easing.InOutQuad }
                            }
                        }

                        // 翻译区：布局贡献随 lyricsContainer.translationExpand 从 0 连续增长到
                        // (翻译自然高 + 4px 行距)，展开/收缩与全表同步；单共享动画驱动，无逐行动画对象。
                        // 文本自然高排版一次即缓存（显式 height 不触发重新 shaping），外层容器负责裁剪，
                        // Text.y 随因子下移形成"行距先出、文本自上而下露出"的展开观感。
                        // 透明度分层：容器承载 factor（开关渐隐渐显），内部 Text 只负责高亮行明暗
                        // （isActive），两者相乘 = 原 0.8/0.3，且高亮切换仍走独立 Behavior 动画
                        Item {
                            id: translationClip
                            width: parent.width
                            height: (delegateItem.translation !== "")
                                    ? lyricsContainer.translationExpand * (translationTextCtrl.implicitHeight + 4) : 0
                            opacity: lyricsContainer.translationExpand
                            visible: height > 0
                            clip: true

                            Text {
                                id: translationTextCtrl
                                width: parent.width
                                // 行距随展开在文本上方形成（0 → 4px）
                                y: lyricsContainer.translationExpand * 4
                                text: delegateItem.translation
                                color: Theme.secondaryTextColor
                                font.pixelSize: 22
                                font.weight: delegateItem.isActive ? Font.Bold : Font.Normal
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                // 与外层 factor 相乘后 = 展开完成态 isActive ? 0.8 : 0.3
                                opacity: delegateItem.isActive ? 1.0 : 0.375

                                Behavior on opacity {
                                    NumberAnimation { duration: 300; easing.type: Easing.InOutQuad }
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function () {
                            if (delegateItem.timestampSec > 0) {
                                // 点击跳转 = 明确的跟随意图，恢复自动跟随
                                lyricsContainer.lyricsSyncToPlayback = true;
                                lyricsContainer.scheduleSnapToCurrentLyric();
                                lyricsRestoreTimer.stop();
                                root.playbackController.seek(delegateItem.timestampSec);
                            }
                        }
                    }
                }

            }
        }
    }

    // 歌词内容重置（切歌）时的新数据入口：退场动画在播则缓冲，否则直接同步
    Connections {
        target: lyricsState

        function onModelReset() {
            // LyricsModel 此刻已是新歌（C++ 同步替换完成）：
            // - 退场相位进行中：把新歌数据存入缓冲，退场完成后由 finishLyricsExitPhase 应用（两段式）
            // - 空闲（非歌词态切歌 / 无动画场景）：直接同步快照
            if (root.lyricsAnimBusy && !root.lyricsAnimEntering)
                root.pendingLyricsRows = root.snapshotRowsFromLyricsState();
            else
                root.applyLyricsSnapshotDirectly();
        }
    }

    // 4.1 无歌词占位提示 (仅在 lyrics 状态且无歌词时显示)
    Item {
        id: noLyricsPlaceholder
        anchors.fill: lyricsContainer
        z: 11
        visible: lyricsContainer.visible && (lyricsViewList.count === 0)
        opacity: visible ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
        }

        Text {
            anchors.centerIn: parent
            text: qsTr("无歌词")
            color: Theme.secondaryTextColor
            font.pixelSize: 24
            font.weight: Font.Normal
            opacity: 0.5
        }
    }

    // 5. 波形进度条区域 (仅在 playback 状态下显示)
    Item {
        id: waveformProgressContainer
        width: 320
        height: 85
        opacity: 1.0
        visible: opacity > 0.0
        z: 8
        anchors.top: metadataContainer.bottom
        anchors.topMargin: 14
        anchors.horizontalCenter: positionHelper.horizontalCenter

        WaveformProgressBar {
            id: waveProgress
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: 320
            height: 68
            waveformHeights: root.playbackController.waveformHeights
            barWidth: root.playbackController.waveformBarWidth
            progress: root.playbackTimelineProgress
            onSeekRequested: function (pos) {
                if (root.playbackTimelineDuration > 0) {
                    root.playbackController.seek(pos * root.playbackTimelineDuration);
                }
            }
        }

        Item {
            anchors.top: waveProgress.bottom
            anchors.topMargin: 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: 320
            height: 15

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: waveProgress.isHovering ? root.formatTime(root.playbackTimelineDuration * waveProgress.hoverProgress) : root.playbackController.currentPositionText
                color: Theme.secondaryTextColor
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (waveProgress.isHovering) {
                        var remainingPreview = root.playbackTimelineDuration * (1.0 - waveProgress.hoverProgress);
                        return "-" + root.formatTime(remainingPreview);
                    } else {
                        return root.playbackController.remainingDurationText;
                    }
                }
                color: Theme.secondaryTextColor
                font.pixelSize: 11
                font.weight: Font.Medium
            }
        }
    }

    // 6. 线性进度条区域 (仅在 lyrics 状态下显示)
    Item {
        id: linearProgressContainer
        height: 30
        opacity: 0.0
        visible: opacity > 0.0
        z: 7
        anchors.bottom: controlsContainer.top
        anchors.bottomMargin: 10
        anchors.left: positionHelper.left
        anchors.right: positionHelper.right

        Slider {
            id: progressSlider
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 15
            from: 0
            to: root.playbackTimelineDuration
            value: root.boundedPlaybackTimelinePosition
            onMoved: {
                if (root.playbackTimelineDuration > 0) {
                    root.playbackController.seek(value);
                }
            }

            background: Rectangle {
                x: progressSlider.leftPadding
                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                width: progressSlider.availableWidth
                height: 4
                radius: 2
                color: Theme.baseColor

                Rectangle {
                    width: progressSlider.visualPosition * parent.width
                    height: parent.height
                    color: Theme.progressBarColor
                    radius: 2
                    opacity: 0.8
                }
            }

            handle: Rectangle {
                x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                width: 12
                height: 12
                radius: 6
                color: Theme.textColor
                visible: progressSlider.hovered || progressSlider.pressed
            }
        }

        Item {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 15

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 0
                anchors.verticalCenter: parent.verticalCenter
                text: root.playbackController.currentPositionText
                color: Theme.secondaryTextColor
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 0
                anchors.verticalCenter: parent.verticalCenter
                text: root.playbackController.remainingDurationText
                color: Theme.secondaryTextColor
                font.pixelSize: 11
                font.weight: Font.Medium
            }
        }
    }

    // 7. 播放控制按钮区域 (共享元素)
    Item {
        id: controlsContainer
        width: 320
        height: 68
        z: 6
        anchors.top: waveformProgressContainer.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: positionHelper.horizontalCenter

        StyleButton {
            id: prevButton
            width: 40
            height: 40
            anchors.verticalCenter: playButton.verticalCenter
            anchors.right: playButton.left
            anchors.rightMargin: 35
            iconSource: "qrc:/qt/qml/Seriona/qml/assets/prev.svg"
            textColor: Theme.textColor
            onClicked: {
                root.lyricsSwitchDirection = -1;
                root.playbackController.skipPrevious();
            }
            SharedToolTip {
                text: qsTr("上一首")
            }
        }

        StyleButton {
            id: playButton
            width: 68
            height: 68
            anchors.centerIn: parent
            iconSource: root.playbackController.isPlaying ? "qrc:/qt/qml/Seriona/qml/assets/pause.svg" : "qrc:/qt/qml/Seriona/qml/assets/play.svg"
            baseColor: Theme.playButtonBg
            hoverColor: Qt.darker(Theme.playButtonBg, 1.1)
            pressedColor: Qt.darker(Theme.playButtonBg, 1.2)
            textColor: Theme.playButtonText
            onClicked: root.playbackController.togglePlay()
            SharedToolTip {
                text: root.playbackController.isPlaying ? qsTr("暂停") : qsTr("播放")
            }
        }

        StyleButton {
            id: nextButton
            width: 40
            height: 40
            anchors.verticalCenter: playButton.verticalCenter
            anchors.left: playButton.right
            anchors.leftMargin: 25
            iconSource: "qrc:/qt/qml/Seriona/qml/assets/next.svg"
            textColor: Theme.textColor
            onClicked: {
                root.lyricsSwitchDirection = 1;
                root.playbackController.skipNext();
            }
            SharedToolTip {
                text: qsTr("下一首")
            }
        }
    }

    // 8. 音量控制区域 (仅在 playback 状态下显示)
    RowLayout {
        id: volumeContainer
        width: 320
        height: 25
        spacing: 12
        opacity: 1.0
        visible: opacity > 0.0
        z: 4
        anchors.top: controlsContainer.bottom
        anchors.topMargin: 12
        anchors.horizontalCenter: positionHelper.horizontalCenter

        Item {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            Image {
                id: volDownIcon
                anchors.fill: parent
                source: "qrc:/qt/qml/Seriona/qml/assets/volume_down.svg"
                sourceSize.width: 20
                sourceSize.height: 20
                visible: false
            }
            ColorOverlay {
                anchors.fill: volDownIcon
                source: volDownIcon
                color: Theme.secondaryTextColor
            }
        }

        Slider {
            id: volumeSlider
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            from: 0.0
            to: 1.0
            value: root.playbackController.volume
            onMoved: root.playbackController.volume = value
            background: Rectangle {
                x: volumeSlider.leftPadding
                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                width: volumeSlider.availableWidth
                height: 10
                radius: 5
                color: Theme.baseColor
                Rectangle {
                    width: volumeSlider.visualPosition * parent.width
                    height: parent.height
                    color: Theme.textColor
                    radius: 5
                    opacity: 0.6
                }
            }
            handle: Rectangle {
                x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: Theme.textColor
                visible: volumeSlider.hovered || volumeSlider.pressed
            }

            SharedToolTip {
                text: qsTr("音量")
            }
        }

        Item {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            Image {
                id: volUpIcon
                anchors.fill: parent
                source: "qrc:/qt/qml/Seriona/qml/assets/volume_up.svg"
                sourceSize.width: 20
                sourceSize.height: 20
                visible: false
            }
            ColorOverlay {
                anchors.fill: volUpIcon
                source: volUpIcon
                color: Theme.secondaryTextColor
            }
        }
    }

    // 9. 底部功能按钮区域 (仅在 playback 状态下显示)
    RowLayout {
        id: bottomRowContainer
        width: 320
        height: 40
        opacity: 1.0
        visible: opacity > 0.0
        z: 3
        anchors.top: volumeContainer.bottom
        anchors.topMargin: 16
        anchors.horizontalCenter: positionHelper.horizontalCenter

        Row {
            spacing: 20
            StyleButton {
                buttonWidth: 36
                buttonHeight: 36
                iconSource: "qrc:/qt/qml/Seriona/qml/assets/playlist.svg"
                textColor: Theme.textColor
                checkable: true
                checked: root.isSidebarOpen
                onClicked: root.playlistToggled()
            }
            StyleButton {
                buttonWidth: 36
                buttonHeight: 36
                iconSource: root.playbackController.isShuffle ? "qrc:/qt/qml/Seriona/qml/assets/shuffle_on.svg" : "qrc:/qt/qml/Seriona/qml/assets/shuffle_off.svg"
                textColor: Theme.textColor
                checkable: true
                checked: root.playbackController.isShuffle
                onClicked: root.playbackController.toggleShuffle()
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Row {
            spacing: 20
            StyleButton {
                iconSource: root.playbackController.repeatMode === 1 ? "qrc:/qt/qml/Seriona/qml/assets/repeat_list.svg" : root.playbackController.repeatMode === 2 ? "qrc:/qt/qml/Seriona/qml/assets/repeat_one.svg" : "qrc:/qt/qml/Seriona/qml/assets/repeat_off.svg"
                buttonWidth: 36
                buttonHeight: 36
                textColor: Theme.textColor
                onClicked: root.playbackController.cycleRepeatMode()
            }
            StyleButton {
                id: settingsBtn
                buttonWidth: 36
                buttonHeight: 36
                iconSource: "qrc:/qt/qml/Seriona/qml/assets/settings.svg"
                textColor: Theme.textColor
                onClicked: mainMenu.toggle()

                BubbleMenu {
                    id: mainMenu
                    menuWidth: 180
                    arrowDirection: "down"
                    targetItem: settingsBtn

                    BubbleMenuItem {
                        objectName: "settingsMenuItem"
                        text: qsTr("设置")
                        onTriggered: {
                            mainMenu.close();
                            root.openSettingsRequested();
                        }
                    }
                    BubbleMenuItem {
                        objectName: "equalizerMenuItem"
                        text: qsTr("均衡器")
                        onTriggered: {
                            mainMenu.close();
                            root.openEqualizerRequested();
                        }
                    }
                    BubbleMenuItem {
                        objectName: "aboutMenuItem"
                        text: qsTr("关于 Seriona")
                        onTriggered: {
                            mainMenu.close();
                            aboutOverlay.open();
                        }
                    }
                    BubbleMenuItem {
                        text: qsTr("退出")
                        onTriggered: {
                            mainMenu.close();
                            root.exitRequested();
                        }
                    }
                }
            }
        }
    }

    // 10. 打开/关闭翻译按钮 (仅在 lyrics 状态下显示，位于右下角)
    StyleButton {
        id: toggleTranslationBtn
        buttonWidth: 40
        buttonHeight: 40
        iconSource: "qrc:/qt/qml/Seriona/qml/assets/translate.svg"
        textColor: Theme.textColor
        checkable: true
        checked: lyricsState.showTranslation
        z: 100
        opacity: 0.0
        visible: opacity > 0.0
        
        anchors.right: parent.right
        anchors.rightMargin: Theme.paddingLarge
        anchors.bottom: linearProgressContainer.top
        anchors.bottomMargin: Theme.paddingMedium

        onClicked: {
            // 用户手动浏览中（浏览焦点不在高亮歌词）时，点击翻译视为明确的跟随意图：
            // 先恢复自动跟随并【瞬时】跳回高亮歌词，再切换翻译。
            // 不用平滑回拉动画：行高变化（翻译展开/收缩）会逐帧触发 compensateLayoutChange
            // 重定向与 Flickable 收缩钳制（关闭翻译时内容变矮尤甚），与回拉动画竞争写
            // contentY 会导致无法收敛到正确焦点；瞬时归位后行高变化统一走同帧瞬时补偿
            if (!lyricsContainer.lyricsSyncToPlayback) {
                lyricsContainer.lyricsSyncToPlayback = true;
                lyricsRestoreTimer.stop();
                const targetY = lyricsContainer.snapToCurrentLyricTarget();
                if (targetY >= 0)
                    lyricsContainer.contentY = targetY;
            }
            lyricsState.toggleTranslation();
        }
    }

    // 状态定义
    state: "playback"

    states: [
        State {
            name: "playback"
            PropertyChanges {
                target: coverContainer
                x: (parent.width - 240) / 2
                y: positionHelper.y + 5
                width: 240
                height: 240
            }
            PropertyChanges {
                target: metadataContainer
                height: 70
            }
            AnchorChanges {
                target: titleText
                anchors.horizontalCenter: metadataLayout.horizontalCenter
                anchors.left: undefined
            }
            AnchorChanges {
                target: artistText
                anchors.horizontalCenter: metadataLayout.horizontalCenter
                anchors.left: undefined
            }
            AnchorChanges {
                target: albumText
                anchors.horizontalCenter: metadataLayout.horizontalCenter
                anchors.top: artistText.bottom
                anchors.left: undefined
                anchors.verticalCenter: undefined
            }
            AnchorChanges {
                target: dashText
                anchors.left: artistText.right
                anchors.verticalCenter: artistText.verticalCenter
            }
            PropertyChanges {
                target: dashText
                opacity: 0.0
            }
            PropertyChanges {
                target: prevButton
                anchors.rightMargin: 35
            }
            PropertyChanges {
                target: nextButton
                anchors.leftMargin: 35
            }
            PropertyChanges {
                target: waveformProgressContainer
                height: 98
            }
            PropertyChanges {
                target: waveProgress
                flatMode: false
                height: 68
            }
            PropertyChanges {
                target: toggleTranslationBtn
                opacity: 0.0
            }
        },
        State {
            name: "lyrics"
            AnchorChanges {
                target: metadataContainer
                anchors.top: parent.top
                anchors.left: coverContainer.right
                anchors.right: parent.right
                anchors.horizontalCenter: undefined
            }
            AnchorChanges {
                target: titleText
                anchors.left: metadataLayout.left
                anchors.horizontalCenter: undefined
            }
            AnchorChanges {
                target: artistText
                anchors.left: metadataLayout.left
                anchors.horizontalCenter: undefined
            }
            AnchorChanges {
                target: dashText
                anchors.left: artistText.right
                anchors.verticalCenter: artistText.verticalCenter
            }
            AnchorChanges {
                target: albumText
                anchors.left: dashText.right
                anchors.verticalCenter: artistText.verticalCenter
                anchors.top: undefined
                anchors.horizontalCenter: undefined
            }
            AnchorChanges {
                target: linearProgressContainer
                anchors.top: undefined
                anchors.bottom: controlsContainer.top
                anchors.left: parent.left
                anchors.right: parent.right
            }
            AnchorChanges {
                target: controlsContainer
                anchors.top: undefined
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
            }
            AnchorChanges {
                target: waveformProgressContainer
                anchors.top: undefined
                anchors.bottom: controlsContainer.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.horizontalCenter: undefined
            }
            PropertyChanges {
                target: coverContainer
                x: Theme.paddingLarge
                y: Theme.paddingLarge
                width: 44
                height: 44
            }
            PropertyChanges {
                target: coverRect
                radius: 12
            }
            PropertyChanges {
                target: coverIcon
                // 字号恒 72，以 scale(20/72) 模拟 20px 视觉：消除逐帧 setFont 的字体查找/shaping 成本（不改变视觉终态）
                scale: 20 / 72
            }
            PropertyChanges {
                target: coverGlow
                opacity: 0.0
            }
            PropertyChanges {
                target: metadataContainer
                height: 44
                anchors.topMargin: Theme.paddingLarge
                anchors.leftMargin: Theme.paddingMedium
                anchors.rightMargin: Theme.paddingLarge
            }
            PropertyChanges {
                target: titleText
                font.pixelSize: 16
            }
            PropertyChanges {
                target: artistText
                font.pixelSize: 13
                // 交叉淡化：轨道锚定动画保留（左移到合并行位置），显示权交给合并行
                opacity: 0.0
            }
            PropertyChanges {
                target: dashText
                // 合并行自带 " — " 分隔符，原 dash 淡出
                opacity: 0.0
            }
            PropertyChanges {
                target: albumText
                // 不再限制宽度（歌词态不可见）；原 width/visible 处理删除，
                // 避免 PropertyChanges 覆盖 visible 绑定（visible 由 opacity>0 派生）
                opacity: 0.0
            }
            PropertyChanges {
                target: metaCombinedText
                opacity: 1.0
            }
            PropertyChanges {
                target: lyricsContainer
                opacity: 1.0
            }
            PropertyChanges {
                target: linearProgressContainer
                anchors.bottomMargin: Theme.paddingMedium
                anchors.leftMargin: Theme.paddingLarge
                anchors.rightMargin: Theme.paddingLarge
                opacity: 1.0
            }
            PropertyChanges {
                target: controlsContainer
                anchors.bottomMargin: Theme.paddingLarge
            }
            PropertyChanges {
                target: prevButton
                anchors.rightMargin: Theme.paddingLarge
            }
            PropertyChanges {
                target: nextButton
                anchors.leftMargin: Theme.paddingLarge
            }
            PropertyChanges {
                target: waveformProgressContainer
                anchors.leftMargin: Theme.paddingLarge
                anchors.rightMargin: Theme.paddingLarge
                anchors.bottomMargin: Theme.paddingMedium
                opacity: 0.0
                height: 60
            }
            PropertyChanges {
                target: waveProgress
                flatMode: true
                height: 40
            }
            PropertyChanges {
                target: volumeContainer
                opacity: 0.0
            }
            PropertyChanges {
                target: bottomRowContainer
                opacity: 0.0
            }
            PropertyChanges {
                target: toggleTranslationBtn
                opacity: 0.8
            }
        }
    ]

    transitions: [
        Transition {
            from: "playback"
            to: "lyrics"

            AnchorAnimation {
                targets: [metadataContainer, controlsContainer, waveformProgressContainer, linearProgressContainer, titleText, artistText, albumText, dashText]
                duration: 400
                easing.type: Easing.InOutCubic
            }

            // metadataContainer.height（44↔70）必须与锚链同步动画：
            // 否则切换瞬间高度瞬跳 26px，带动 lyricsContainer.top 阶跃，
            // 触发 onHeightChanged→snap→contentY 补偿，产生歌词整体下移又回位的闪烁
            NumberAnimation {
                target: metadataContainer
                property: "height"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                target: coverContainer
                properties: "x,y,width,height"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                targets: [coverRect, coverGlow, titleText, artistText, albumText, dashText, metaCombinedText, prevButton, nextButton] // coverIcon 独立动画（scale 模拟字号，见契约测试锚点）
                properties: "radius,font.pixelSize,opacity,spacing,anchors.topMargin,anchors.leftMargin,anchors.rightMargin,anchors.bottomMargin"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                target: coverIcon // coverIcon 独立动画（scale 模拟字号，见契约测试锚点）
                properties: "opacity,scale"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                targets: [waveProgress, waveformProgressContainer]
                property: "height"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                targets: [volumeContainer, bottomRowContainer]
                property: "opacity"
                duration: 180
                easing.type: Easing.OutQuad
            }

            NumberAnimation {
                target: waveformProgressContainer
                property: "opacity"
                duration: 300
                easing.type: Easing.OutQuad
            }

            SequentialAnimation {
                PauseAnimation { duration: 150 }
                NumberAnimation {
                    targets: [lyricsContainer, linearProgressContainer, toggleTranslationBtn]
                    property: "opacity"
                    duration: 250
                    easing.type: Easing.OutQuad
                }
            }
        },
        Transition {
            from: "lyrics"
            to: "playback"

            AnchorAnimation {
                targets: [metadataContainer, controlsContainer, waveformProgressContainer, linearProgressContainer, titleText, artistText, albumText, dashText]
                duration: 400
                easing.type: Easing.InOutCubic
            }

            // metadataContainer.height（44↔70）必须与锚链同步动画：
            // 否则切换瞬间高度瞬跳 26px，带动 lyricsContainer.top 阶跃，
            // 触发 onHeightChanged→snap→contentY 补偿，产生歌词整体下移又回位的闪烁
            NumberAnimation {
                target: metadataContainer
                property: "height"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                target: coverContainer
                properties: "x,y,width,height"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                targets: [coverRect, coverGlow, titleText, artistText, albumText, dashText, metaCombinedText, prevButton, nextButton] // coverIcon 独立动画（scale 模拟字号，见契约测试锚点）
                properties: "radius,font.pixelSize,opacity,spacing,anchors.topMargin,anchors.leftMargin,anchors.rightMargin,anchors.bottomMargin"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                target: coverIcon // coverIcon 独立动画（scale 模拟字号，见契约测试锚点）
                properties: "opacity,scale"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                targets: [waveProgress, waveformProgressContainer]
                property: "height"
                duration: 400
                easing.type: Easing.InOutCubic
            }

            NumberAnimation {
                targets: [lyricsContainer, linearProgressContainer, toggleTranslationBtn]
                property: "opacity"
                duration: 180
                easing.type: Easing.OutQuad
            }

            SequentialAnimation {
                PauseAnimation { duration: 150 }
                NumberAnimation {
                    targets: [waveformProgressContainer, volumeContainer, bottomRowContainer]
                    property: "opacity"
                    duration: 250
                    easing.type: Easing.OutQuad
                }
            }
        }
    ]

    // 关于 Seriona overlay（T19）：主窗内弹层，入口为设置菜单"关于 Seriona"项
    AboutOverlay {
        id: aboutOverlay
        objectName: "aboutOverlay"
    }
}
