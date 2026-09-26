import QtQuick
import QtQuick.Controls.Basic
import Seriona
import "menuRegistry.js" as MenuRegistry

Window {
    id: root

    default property alias content: rootPage.children

    flags: Qt.Popup | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    property int menuWidth: 160
    property string arrowDirection: "up"
    property Item targetItem: null
    property double lastClosedAt: 0
    property int lastHeight: 0
    property int anchoredX: 0

    readonly property int arrowSize: 12
    readonly property int arrowOffset: 12
    readonly property int contentPadding: Theme.spacing4

    width: menuWidth + contentPadding * 2
    height: pageStack.implicitHeight + contentPadding * 2 + arrowOffset

    property bool isPositioning: false
    property PopupInputGuard inputGuard: PopupInputGuard { window: root }

    Behavior on height {
        NumberAnimation { duration: Theme.animationFast; easing.type: Theme.easingDecelerate }
    }

    function pushPage(title, pageComponent) {
        pageStack.push(subMenuPageComponent, {
            "pageTitle": title,
            "pageComponent": pageComponent
        });
    }

    function popPage() {
        if (pageStack.depth > 1)
            pageStack.pop();
    }

    function resetPage() {
        while (pageStack.depth > 1)
            pageStack.pop(null, StackView.Immediate);
    }

    Item {
        id: backgroundLayer
        anchors.fill: parent
        opacity: root.visible ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animationFast; easing.type: Theme.easingDecelerate }
        }

        Rectangle {
            width: root.arrowSize + 2
            height: root.arrowSize + 2
            color: Theme.borderSubtle
            rotation: 45
            x: (parent.width - width) / 2
            y: root.arrowDirection === "up" ? root.arrowOffset - height / 2 - 1 : body.y + body.height - height / 2 + 1
        }

        Rectangle {
            id: body
            anchors.fill: parent
            anchors.topMargin: root.arrowDirection === "up" ? root.arrowOffset : 0
            anchors.bottomMargin: root.arrowDirection === "down" ? root.arrowOffset : 0
            color: Theme.raisedSurfaceColor
            radius: Theme.radiusMedium
            border.color: Theme.borderSubtle
            border.width: 1
        }

        Rectangle {
            width: root.arrowSize
            height: root.arrowSize
            color: Theme.raisedSurfaceColor
            rotation: 45
            x: (parent.width - width) / 2
            y: root.arrowDirection === "up" ? root.arrowOffset - height / 2 : body.y + body.height - height / 2
        }
    }

    StackView {
        id: pageStack
        x: root.contentPadding
        y: root.contentPadding + (root.arrowDirection === "up" ? root.arrowOffset : 0)
        width: root.menuWidth
        height: implicitHeight
        clip: true
        implicitHeight: currentItem ? currentItem.implicitHeight : 0
        initialItem: rootPage

        pushEnter: Transition {
            id: pushEnterTransition
            PropertyAnimation { target: pushEnterTransition.ViewTransition.item; property: "x"; from: pageStack.width; to: 0; duration: Theme.animationFast; easing.type: Theme.easingDecelerate }
        }
        pushExit: Transition {
            id: pushExitTransition
            PropertyAnimation { target: pushExitTransition.ViewTransition.item; property: "x"; from: 0; to: -pageStack.width; duration: Theme.animationFast; easing.type: Theme.easingDecelerate }
        }
        popEnter: Transition {
            id: popEnterTransition
            PropertyAnimation { target: popEnterTransition.ViewTransition.item; property: "x"; from: -pageStack.width; to: 0; duration: Theme.animationFast; easing.type: Theme.easingDecelerate }
        }
        popExit: Transition {
            id: popExitTransition
            PropertyAnimation { target: popExitTransition.ViewTransition.item; property: "x"; from: 0; to: pageStack.width; duration: Theme.animationFast; easing.type: Theme.easingDecelerate }
        }

        Column {
            id: rootPage

            property var menu: root

            width: pageStack.width
            spacing: 0

            StackView.onActivated: x = 0
        }

        Component {
            id: subMenuPageComponent

            Column {
                id: subMenuPage

                property var menu: root
                property string pageTitle: ""
                property Component pageComponent: null

                width: pageStack.width
                spacing: 0

                StackView.onActivated: x = 0

                Item {
                    width: parent.width
                    height: 40

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing4
                        radius: Theme.radiusSmall
                        color: backMouseArea.containsMouse ? Theme.hoverColor : "transparent"

                        Behavior on color {
                            ColorAnimation { duration: Theme.animationFast }
                        }
                    }

                    Text {
                        text: qsTr("‹")
                        color: Theme.textPrimary
                        font.pixelSize: 22
                        anchors.left: parent.left
                        anchors.leftMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: subMenuPage.pageTitle
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        anchors.fill: parent
                        anchors.leftMargin: 38
                        anchors.rightMargin: 38
                    }

                    MouseArea {
                        id: backMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.popPage()
                    }
                }

                Loader {
                    width: parent.width
                    sourceComponent: subMenuPage.pageComponent
                }
            }
        }
    }

    // Wayland 下 xdg_popup 有 grab 链约束：同一时刻只允许一个 grabbing popup，
    // 新 popup 的 parent 必须是当前 grab 链上的 popup。打开本菜单前先关闭
    // 其他 BubbleMenu 实例，避免"Creating a popup with a parent which does not
    // match the current topmost grabbing popup"警告、双菜单并存与定位错乱。
    readonly property bool isBubbleMenu: true

    Component.onCompleted: MenuRegistry.register(root)
    Component.onDestruction: MenuRegistry.unregister(root)

    function closeSiblingMenus() {
        MenuRegistry.closeOthers(root);
    }

    function showAtTarget() {
        if (!targetItem || !targetItem.Window.window)
            return;

        root.closeSiblingMenus();

        if (root.visible) {
            root.visible = false;
        }

        root.isPositioning = true;
        // 显示前重置页面栈（替代 onVisibleChanged 中的重置）：
        // 窗口销毁过程中 visible 变化会触发绑定，此时 StackView 内部
        // 视图正在销毁，pop() 会访问已删除的过渡 item 导致崩溃（SEGV）。
        root.resetPage();

        root.transientParent = targetItem.Window.window;

        // 定位必须使用全局坐标（mapToGlobal）：Window.x/y 在 X11 是屏幕绝对坐标，
        // 在 Wayland 由 Qt 换算为相对父窗口的 xdg_popup 坐标。
        // 若传窗口场景坐标（mapToItem(null)），主窗口不在虚拟桌面原点时，
        // popup 会被放到"场景坐标"对应的屏幕位置，甚至被合成器钳制到屏幕边缘（T14 双屏回归）。
        var topCenter = targetItem.mapToGlobal(targetItem.width / 2, 0);
        var bottomCenter = targetItem.mapToGlobal(targetItem.width / 2, targetItem.height);
        root.anchoredX = Math.round(topCenter.x - root.width / 2);
        root.x = root.anchoredX;
        root.y = arrowDirection === "up"
                ? Math.round(bottomCenter.y + 12)
                : Math.round(topCenter.y - root.height - 12);
        root.lastHeight = root.height;
        root.show();
        root.requestActivate();
        root.x = root.anchoredX;
        root.isPositioning = false;
    }

    // 右键菜单定位（T14）：按全局坐标弹出（箭头向上，菜单出现在坐标下方 12px）。
    // 与 showAtTarget 同一套 anchoredX/transientParent 约束，只替换定位来源。
    // 调用方必须传入全局坐标（mapToGlobal 结果）；传场景坐标会在主窗口
    // 不在虚拟桌面原点时错位（Wayland 双屏回归，见 showAtTarget 注释）。
    function showAtGlobal(globalX, globalY) {
        if (!targetItem || !targetItem.Window.window)
            return;

        root.closeSiblingMenus();

        if (root.visible) {
            root.visible = false;
        }

        root.isPositioning = true;
        root.resetPage();
        root.transientParent = targetItem.Window.window;

        root.anchoredX = Math.round(globalX - root.width / 2);
        root.x = root.anchoredX;
        root.y = Math.round(globalY + 12);
        root.lastHeight = root.height;
        root.show();
        root.requestActivate();
        root.x = root.anchoredX;
        root.isPositioning = false;
    }

    function toggle() {
        if (!root.visible && Date.now() - lastClosedAt < 150)
            return;

        if (root.visible) {
            root.close();
        } else {
            root.showAtTarget();
        }
    }

    onVisibleChanged: {
        if (!visible) {
            // 不要在这里重置页面栈：窗口销毁（应用退出）时 setVisible(false)
            // 会触发本处理器，此时对 StackView 调用 pop() 会在过渡机制中
            // 访问已销毁的 QObject（QQuickPropertyAnimation::createTransitionActions
            // -> ExternalRefCountData::getAndRef），导致 SIGSEGV。
            // 页面栈重置已移至 showAtTarget()。
            lastClosedAt = Date.now();
        }
    }

    onActiveChanged: {
        if (!active && visible)
            close();
    }
}
