import QtQuick
import QtQuick.Controls.Basic
import Seriona

// 播放列表三态滚动条共享组件：hover/pressed 加宽 6→10、胶囊圆角、
// 三态色 + 动画、内容溢出才显示句柄（规格源 = Sidebar 播放列表滚动条，
// 原 Sidebar/FolderPage/SettingsWindow 四处逐份复制，收敛于此消除漂移）
ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded

    readonly property bool isHoveredOrPressed: control.hovered || control.pressed
    readonly property bool verticalBar: orientation === Qt.Vertical

    // 厚轴随 hover/pressed 6→10 加宽；长轴交由 ScrollBar 附挂机制管理
    // （ScrollView/Flickable 自动贴合视口），故仅按方向设置厚轴：
    // 竖向默认语义厚轴 = width，横向（GEQ 波段区等）厚轴 = height。
    width: verticalBar ? (isHoveredOrPressed ? 10 : Theme.scrollbarWidth) : undefined
    height: verticalBar ? undefined : (isHoveredOrPressed ? 10 : Theme.scrollbarWidth)

    Behavior on width {
        enabled: control.verticalBar
        NumberAnimation { duration: Theme.animationFast; easing.type: Easing.OutQuad }
    }

    Behavior on height {
        enabled: !control.verticalBar
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
