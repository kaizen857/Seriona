import QtQuick

// 列表模型完整重建（beginResetModel/endResetModel，例如曲库快照刷新触发的投影重建）
// 会让 ListView 视口归顶。本组件在重建前记录顶部可见条目的 nodeId 与条目内偏移，
// 重建后按 nodeId 定位行并恢复 contentY；锚点条目已不存在时回退到钳制后的旧位置。
QtObject {
    id: root

    property ListView view: null

    property string anchorNodeId: ""
    property real anchorOffset: 0
    property real savedContentY: 0
    property bool hasAnchor: false

    function capture() {
        hasAnchor = false;
        if (!view || view.count === 0 || !view.model)
            return;
        const index = view.indexAt(0, view.contentY + 1);
        if (index < 0)
            return;
        const item = view.itemAtIndex(index);
        if (!item || typeof item.nodeId !== "string" || item.nodeId.length === 0)
            return;
        anchorNodeId = item.nodeId;
        anchorOffset = view.contentY - item.y;
        savedContentY = view.contentY;
        hasAnchor = true;
    }

    function applyContentY(value) {
        const topY = -view.topMargin;
        const bottomY = Math.max(topY, view.contentHeight + view.bottomMargin - view.height);
        view.contentY = Math.min(Math.max(value, topY), bottomY);
    }

    function restore() {
        if (!hasAnchor || !view || !view.model)
            return;
        const nodeId = anchorNodeId;
        const offset = anchorOffset;
        const fallbackY = savedContentY;
        hasAnchor = false;

        const model = view.model;
        const row = typeof model.rowForNodeId === "function" ? model.rowForNodeId(nodeId) : -1;
        if (row < 0) {
            applyContentY(fallbackY);
            return;
        }
        view.positionViewAtIndex(row, ListView.Beginning);
        view.forceLayout();
        const item = view.itemAtIndex(row);
        applyContentY(item ? item.y + offset : view.contentY + offset);
    }

    property Connections modelConnections: Connections {
        target: root.view ? root.view.model : null
        // view/model 在组件初始化阶段尚未就绪，此时无信号可匹配；忽略该阶段的
        // 未知处理器告警，模型就绪后 target 变化会重新连接。
        ignoreUnknownSignals: true

        function onModelAboutToBeReset() {
            root.capture();
        }

        function onModelReset() {
            Qt.callLater(root.restore);
        }
    }
}
