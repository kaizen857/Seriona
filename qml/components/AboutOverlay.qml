import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

// 关于 Seriona 界面：主窗内底部抽屉式弹层（Bottom Sheet），参照 Amberol / GNOME AdwAboutDialog
// 设计风格——半透明遮罩 + 底部滑入圆角卡片，包含应用 Logo、版本、简介、仓库链接、开发者致谢
// 与 GNU General Public License v3.0 (GPL-3.0-or-later) 许可证说明，支持主页与许可证详情切换。
//
// 关闭方式：Esc / 点击遮罩（closePolicy），与既有弹窗规范完全一致。
Popup {
    id: root

    modal: true
    focus: true
    parent: Overlay.overlay

    // 响应式底部抽屉宽度（最大约 520px，两侧保留安全边距）
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - Theme.spacing16 * 2 : 520, 520)
    x: Overlay.overlay ? Math.round((Overlay.overlay.width - width) / 2) : 0
    y: Overlay.overlay ? Math.max(0, Overlay.overlay.height - height) : 0
    margins: 0
    bottomMargin: 0
    padding: 0
    topPadding: 0
    bottomPadding: 0
    leftPadding: 0
    rightPadding: 0

    // Esc / 点击遮罩关闭
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 半透明遮罩（圆角对齐主窗口 OpacityMask：最大化时 0，否则匹配 Theme.spacing24）
    // 注意：Popup（QQuickPopup : QObject）自身不能用 Window.window attached 属性
    // （仅 Item 派生类型支持，访问会警告）；Overlay.overlay 是 QQuickOverlay（Item），
    // 且 scrim 组件在弹窗首次打开时才创建（此时弹窗已在窗口内，overlay 非 null），
    // 故经 Overlay.overlay.Window.window 取所属窗口。
    Overlay.modal: Rectangle {
        color: Theme.overlayScrimColor
        radius: (Overlay.overlay && Overlay.overlay.Window.window
                 && Overlay.overlay.Window.window.visibility === Window.Maximized)
                ? 0 : Theme.spacing24
    }

    // 页面状态：0 = 主页, 1 = 许可证与法律信息
    property int currentPage: 0

    onAboutToShow: {
        root.currentPage = 0
    }

    // 抽屉滑入滑出动效（在 contentItem 容器/透明度上进行平滑过渡，确保 Popup 自身锚定在窗口底部）
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.animationStandard
                easing.type: Theme.easingDecelerate
            }
            NumberAnimation {
                target: drawerContainer
                property: "y"
                from: root.height > 0 ? root.height : 400
                to: 0
                duration: Theme.animationStandard
                easing.type: Theme.easingDecelerate
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.animationFast
                easing.type: Theme.easingAccelerate
            }
            NumberAnimation {
                target: drawerContainer
                property: "y"
                from: 0
                to: root.height > 0 ? root.height : 400
                duration: Theme.animationFast
                easing.type: Theme.easingAccelerate
            }
        }
    }

    background: Rectangle {
        color: "transparent"
    }

    contentItem: Item {
        id: drawerContainer
        implicitWidth: drawerBackground.implicitWidth
        implicitHeight: drawerBackground.implicitHeight
        clip: true

        Rectangle {
            id: drawerBackground
            anchors.fill: parent
            implicitHeight: mainLayout.implicitHeight
            color: Theme.raisedSurfaceColor
            topLeftRadius: Theme.radiusLarge + 4
            topRightRadius: Theme.radiusLarge + 4
            bottomLeftRadius: 0
            bottomRightRadius: 0
            border.color: Theme.borderColor
            border.width: 1
        }

        ColumnLayout {
            id: mainLayout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0

        // 顶部把手指示器（Bottom Sheet Affordance Handle）
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 14

            Rectangle {
                anchors.centerIn: parent
                width: 36
                height: 4
                radius: Theme.radiusFull
                color: Theme.borderSubtle
            }
        }

        // 顶栏：标题 + 返回按钮 + 关闭按钮
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing16
                anchors.rightMargin: Theme.spacing12
                spacing: Theme.spacing8

                // 返回按钮（仅在详情子页显示）
                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: Theme.radiusSmall
                    color: backMouse.containsMouse ? Theme.hoverColor : "transparent"
                    visible: root.currentPage !== 0

                    MouseArea {
                        id: backMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentPage = 0
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "←"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontTitle
                    }
                }

                Text {
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                    text: root.currentPage === 0 ? qsTr("关于 Seriona") : qsTr("法律与许可证")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }

                // 关闭按钮
                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: Theme.radiusSmall
                    color: closeMouse.containsMouse ? Theme.hoverColor : "transparent"

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "\u2715"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTitle
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }

        // 主体内容区：StackLayout 切换主页与法律信息页
        StackLayout {
            Layout.fillWidth: true
            currentIndex: root.currentPage

            // ==========================================
            // Page 0: 主关于页面（Amberol / AdwAboutDialog 风格）
            // ==========================================
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing16
                Layout.bottomMargin: Theme.spacing20
                Layout.leftMargin: Theme.spacing24
                Layout.rightMargin: Theme.spacing24
                spacing: Theme.spacing12

                // 1. 程序化精致 Logo：带柔和渐变与音符
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 76
                    radius: 22
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.accentColor }
                        GradientStop { position: 1.0; color: Theme.gradientColor0 }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 66
                        height: 66
                        radius: 18
                        color: "transparent"
                        border.color: Theme.borderSubtle
                        border.width: 1
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "\u266A"
                        color: Theme.textOnAccent
                        font.pixelSize: 38
                    }
                }

                // 2. 应用名称与副标题
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Theme.spacing4

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Seriona"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontHeading
                        font.bold: true
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("优雅小巧的现代音乐播放器")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                    }
                }

                // 3. 版本号胶囊标签
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: versionLabel.implicitWidth + Theme.spacing16
                    Layout.preferredHeight: 24
                    radius: Theme.radiusFull
                    color: Theme.baseColor
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Text {
                        id: versionLabel
                        objectName: "aboutVersionLabel"
                        anchors.centerIn: parent
                        text: qsTr("v%1 (C++23 & Qt 6)").arg(Qt.application.version)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                    }
                }

                // 4. 链接与操作按钮组（代码仓库、报告问题）
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.spacing4
                    spacing: Theme.spacing12

                    Rectangle {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 32
                        radius: Theme.radiusSmall
                        color: repoMouse.pressed ? Theme.pressedColor : (repoMouse.containsMouse ? Theme.hoverColor : Theme.baseColor)
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: Theme.animationFast } }

                        MouseArea {
                            id: repoMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally("https://github.com/kaizen857/Seriona")
                        }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Theme.spacing4

                            Text {
                                text: "⌥"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                            }
                            Text {
                                text: qsTr("代码仓库")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 32
                        radius: Theme.radiusSmall
                        color: issueMouse.pressed ? Theme.pressedColor : (issueMouse.containsMouse ? Theme.hoverColor : Theme.baseColor)
                        border.color: Theme.borderSubtle
                        border.width: 1

                        Behavior on color { ColorAnimation { duration: Theme.animationFast } }

                        MouseArea {
                            id: issueMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally("https://github.com/kaizen857/Seriona/issues")
                        }

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Theme.spacing4

                            Text {
                                text: "!"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                                font.bold: true
                            }
                            Text {
                                text: qsTr("报告问题")
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontBody
                            }
                        }
                    }
                }

                // 5. 开发者与致谢信息卡片
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacing4
                    Layout.preferredHeight: creditColumn.implicitHeight + Theme.spacing16
                    radius: Theme.radiusMedium
                    color: Theme.surfaceColor
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        id: creditColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spacing8
                        spacing: Theme.spacing4

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("开发者:")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                            }
                            Text {
                                text: "kaizen857"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontCaption
                                font.weight: Font.DemiBold
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("致谢:")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Qt 6, spdlog, Catch2, miniaudio"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                // 6. 法律与许可证入口卡片（明确标注 GPL-3.0）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 38
                    radius: Theme.radiusMedium
                    color: licenseEntryMouse.pressed ? Theme.pressedColor : (licenseEntryMouse.containsMouse ? Theme.hoverColor : Theme.surfaceColor)
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Theme.animationFast } }

                    MouseArea {
                        id: licenseEntryMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentPage = 1
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing12
                        anchors.rightMargin: Theme.spacing12
                        spacing: Theme.spacing8

                        Text {
                            text: qsTr("开源许可证")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                        }

                        Item { Layout.fillWidth: true; Layout.preferredHeight: 1 }

                        Text {
                            text: "GNU GPL v3.0"
                            color: Theme.accentColor
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                        }

                        Text {
                            text: "→"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.spacing2
                    text: "© 2026 kaizen857 · GNU GPL v3.0 · Vibe Coding"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
            }

            // ==========================================
            // Page 1: 法律与许可证详情页 (GPL-3.0)
            // ==========================================
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing12
                Layout.bottomMargin: Theme.spacing20
                Layout.leftMargin: Theme.spacing24
                Layout.rightMargin: Theme.spacing24
                spacing: Theme.spacing12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing8

                    Rectangle {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        radius: Theme.radiusSmall
                        color: Theme.accentColor
                        opacity: 0.2

                        Text {
                            anchors.centerIn: parent
                            text: "§"
                            color: Theme.accentColor
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            text: "GNU General Public License v3.0"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                        }

                        Text {
                            text: "GPL-3.0-or-later"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    radius: Theme.radiusMedium
                    color: Theme.surfaceColor
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Flickable {
                        id: licenseFlickable
                        anchors.fill: parent
                        anchors.margins: Theme.spacing12
                        contentWidth: width
                        contentHeight: licenseDetailsText.implicitHeight
                        clip: true

                        ScrollBar.vertical: ScrollBar {
                            parent: licenseFlickable.parent
                            anchors.top: licenseFlickable.top
                            anchors.bottom: licenseFlickable.bottom
                            anchors.right: licenseFlickable.right
                            anchors.rightMargin: 2
                            policy: ScrollBar.AsNeeded
                        }

                        Text {
                            id: licenseDetailsText
                            width: parent.width - 8
                            text: qsTr("Seriona 是自由开源软件。\n\n本软件基于 GNU 通用公共许可证第三版（GNU General Public License v3.0，简称 GPL-3.0-or-later）分发。\n\n核心授权条款：\n• 自由运行：您可以出于任何目的运行本程序。\n• 自由研究与修改：您可以获取完整源代码并自由修改。\n• 自由分发：您可以向任何人分发软件副本。\n• 传染性开源（Copyleft）：若您修改或分发本软件的衍生版本，必须同样以 GPL-3.0 许可证完整开源全部源码。\n\n免责声明：\n本程序是在希望其有用的情况下分发的，但没有任何担保；甚至没有适销性或特定用途适用性的暗示担保。详细信息请参阅源码 LICENSE 文件。")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            lineHeight: 1.3
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // 外部许可链接按钮
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: Theme.radiusSmall
                    color: gplWebMouse.pressed ? Theme.pressedColor : (gplWebMouse.containsMouse ? Theme.hoverColor : Theme.baseColor)
                    border.color: Theme.borderSubtle
                    border.width: 1

                    Behavior on color { ColorAnimation { duration: Theme.animationFast } }

                    MouseArea {
                        id: gplWebMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally("https://www.gnu.org/licenses/gpl-3.0.html")
                    }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Theme.spacing4

                        Text {
                            text: qsTr("访问 GNU.org 查看完整许可证")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                        }

                        Text {
                            text: "↗"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
            }
        }
    }
}
}


