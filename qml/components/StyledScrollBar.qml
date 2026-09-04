import QtQuick
import QtQuick.Controls.Basic
import Seriona

// 播放列表三态滚动条共享组件：hover/pressed 加宽 6→10、胶囊圆角、
// 三态色 + 动画、内容溢出才显示句柄（规格源 = Sidebar 播放列表滚动条，
// 原 Sidebar/FolderPage/SettingsWindow 四处逐份复制，收敛于此消除漂移）
ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded
    width: isHoveredOrPressed ? 10 : Theme.scrollbarWidth

    readonly property bool isHoveredOrPressed: control.hovered || control.pressed

    Behavior on width {
        NumberAnimation { duration: Theme.animationFast; easing.type: Easing.OutQuad }
    }

    background: Rectangle {
        color: "transparent"
    }

    contentItem: Rectangle {
        implicitWidth: control.width
        radius: width / 2
        visible: control.size < 1.0
        color: control.pressed ? Theme.pressedColor
             : control.hovered ? Theme.scrollbarHoverColor
             : Theme.scrollbarColor

        Behavior on color {
            ColorAnimation { duration: Theme.animationFast }
        }
    }
}
