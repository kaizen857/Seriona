import QtQuick
import QtQuick.Controls.Basic
import Seriona

// 播放列表右键菜单（T14）：Sidebar delegate 右键弹出，含 详情 / 添加到下一首播放 /
// 从队列移除（命令层，队列视图上下文 T15 接入）/ 删除（确认弹窗 → appFacade.deleteTarget）。
// 菜单定位走 BubbleMenu.showAtGlobal（鼠标全局坐标，箭头向上）。
Item {
    id: root

    required property AppFacade appFacade

    // 队列视图右键上下文（T15）：true 时"从队列移除"菜单项可见；
    // 文件夹视图右键时保持 false（由 Sidebar 绑定 queueViewActive）。
    property bool queueContext: false

    readonly property bool isOpen: contextMenu.visible

    // Sidebar delegate 右键时收集的条目数据（LibraryModel role 同源 + 文件路径）
    property var entryData: ({})

    function close() {
        contextMenu.close();
    }

    // 打开右键菜单：data 为条目数据对象；
    // 支持 openForEntry(data, targetDelegate, clickX, clickY) 或向后兼容的 openForEntry(data, globalX, globalY)
    function openForEntry(data, targetDelegateOrGlobalX, clickXOrGlobalY, clickY) {
        root.entryData = data;
        if (targetDelegateOrGlobalX && typeof targetDelegateOrGlobalX === "object" && targetDelegateOrGlobalX.mapToItem) {
            var targetDelegate = targetDelegateOrGlobalX;
            var clickX = clickXOrGlobalY;
            var safeX = (clickX !== undefined && clickX >= 0 && clickX <= targetDelegate.width) ? clickX : targetDelegate.width / 2;
            var safeY = (clickY !== undefined && clickY >= 0 && clickY <= targetDelegate.height) ? clickY : targetDelegate.height / 2;
            // 必须转全局坐标：BubbleMenu 按全局坐标定位（Wayland 双屏下
            // 场景坐标会导致菜单错位并被合成器钳制到屏幕边缘）。
            var globalPos = targetDelegate.mapToGlobal(safeX, safeY);
            contextMenu.targetItem = targetDelegate;
            contextMenu.showAtGlobal(globalPos.x, globalPos.y);
        } else {
            var globalX = targetDelegateOrGlobalX;
            var globalY = clickXOrGlobalY;
            contextMenu.targetItem = menuAnchor;
            contextMenu.showAtGlobal(globalX, globalY);
        }
    }

    // 定位锚点（BubbleMenu 需要 targetItem 提供 transientParent 与 geometry）
    Item {
        id: menuAnchor
        width: 1
        height: 1
        visible: false
    }

    BubbleMenu {
        id: contextMenu
        objectName: "trackContextMenuPopup"
        menuWidth: 176
        arrowDirection: "up"
        targetItem: menuAnchor

        BubbleMenuItem {
            id: detailItem
            text: qsTr("详情")
            onTriggered: {
                contextMenu.close();
                detailWindow.entryData = root.entryData;
                detailWindow.show();
            }
        }

        BubbleMenuItem {
            text: qsTr("添加到下一首播放")
            visible: !root.entryData.isFolder
            onTriggered: {
                contextMenu.close();
                root.appFacade.playNextTrack(root.entryData.trackId);
            }
        }

        BubbleMenuItem {
            // 队列视图右键上下文（T15）：queueContext（= Sidebar.queueViewActive）
            // 为 true 时可见；queueIndex 由队列条目右键传入（队列列表下标）。
            id: removeFromQueueItem
            objectName: "removeFromQueueItem"
            text: qsTr("从队列移除")
            visible: root.queueContext
            onTriggered: {
                contextMenu.close();
                root.appFacade.removeFromQueue(root.entryData.queueIndex);
            }
        }

        BubbleMenuItem {
            text: root.entryData.isFolder ? qsTr("删除文件夹") : qsTr("删除歌曲")
            onTriggered: {
                contextMenu.close();
                deleteDialog.targetName = root.entryData.isFolder ? root.entryData.name : root.entryData.title;
                deleteDialog.isFolder = root.entryData.isFolder;
                deleteDialog.trackCount = root.entryData.isFolder ? root.entryData.songCount : 0;
                deleteDialog.show();
            }
        }
    }

    ConfirmDeleteDialog {
        id: deleteDialog
        // 注入主窗口作为 transientParent：ConfirmDeleteDialog 是独立 Window，
        // 自身无法用 Window.window（仅 Item 可用），由本组件（Item）提供。
        // 主窗口移动时 x/y 绑定跟随，保证居中。
        transientParent: root.Window.window

        onConfirmed: {
            root.appFacade.deleteTarget(root.entryData.path, root.entryData.isFolder);
        }
    }

    TrackDetailWindow {
        id: detailWindow
        appFacade: root.appFacade
        // 注入主窗口作为 transientParent：详情窗是独立 Window，自身无法用 Window.window
        // （仅 Item 可用）。固定"只在主窗口之上、不压过其它应用"的对话框语义
        // （与同文件的 ConfirmDeleteDialog 一致）。
        transientParent: root.Window.window
    }
}

