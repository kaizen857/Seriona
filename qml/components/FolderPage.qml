import QtQuick
import QtQuick.Controls.Basic
import Seriona

// 文件夹页面组件（StackView 页面实例）
// 根 Item 禁用 anchors（StackView 限制），通过 parent 绑定获得尺寸
Item {
    id: root

    property string folderNodeId: ""
    property var projectionModel: null
    property bool isActive: false
    property int navDirection: 1
    property bool animationSuppressed: false

    property alias listView: folderListView

    // 宿主转发属性三件套（required 声明，由宿主注入并转发给内部 PlaylistDelegate 实例）
    required property var activateNodeHandler
    required property var closeMenusHandler
    required property var contextMenuHost

    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    onIsActiveChanged: {
        if (isActive && !animationSuppressed) {
            triggerPageSlideAnimation(folderListView, navDirection);
        }
    }

    function triggerPageSlideAnimation(listView, direction) {
        if (!listView || direction === 0 || listView.count === 0)
            return;

        listView.forceLayout();
        var firstVisible = 0;
        if (direction === -1) {
            var idx = listView.indexAt(0, listView.contentY + 1);
            firstVisible = idx >= 0 ? idx : 0;
        }

        var visibleRowCount = Math.ceil(listView.height / 72) + 3;
        var endIndex = Math.min(listView.count - 1, firstVisible + visibleRowCount);

        for (var i = firstVisible; i <= endIndex; ++i) {
            var item = listView.itemAtIndex(i);
            if (item && typeof item.startNavSlideIn === "function") {
                var step = i - firstVisible;
                item.startNavSlideIn(step, direction);
            }
        }
    }

    ListView {
        id: folderListView
        objectName: "folderListView"
        anchors.fill: parent
        model: root.projectionModel
        spacing: 0
        topMargin: Theme.paddingMedium
        bottomMargin: 80
        clip: true
        reuseItems: false

        delegate: PlaylistDelegate {
            activateNodeHandler: root.activateNodeHandler
            closeMenusHandler: root.closeMenusHandler
            contextMenuHost: root.contextMenuHost
        }

        ScrollBar.vertical: StyledScrollBar {}
    }
}
