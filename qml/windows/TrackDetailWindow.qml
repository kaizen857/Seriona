import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Seriona

// 曲目/文件夹详情窗口（T14）：数据来自 Sidebar delegate 的 model role 字段（entryData），
// 播放次数/星级经 appFacade.trackStats（T16 数据层，按 trackId 持久化）。
// 星级只读显示 + 可点击编辑（setRating）；无记录显示 0 次 / 未评级。
Window {
    id: root
    objectName: "trackDetailWindow"

    // 不设 Qt.WindowStaysOnTopHint：该标志是系统级置顶（Windows WS_EX_TOPMOST），
    // 会让详情窗悬浮于所有应用窗口之上；Wayland 也不支持应用自行置顶（xdg-shell 限制）。
    // "保持在主窗口之上"由 transientParent 关系保证（调用方 TrackContextMenu 注入）。
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"

    width: 420
    height: 640

    required property AppFacade appFacade
    // Sidebar delegate 收集的条目数据（与 LibraryModel role 同源，不新增后端调用）
    property var entryData: ({})
    readonly property bool isFolder: !!root.entryData && !!root.entryData.isFolder
    readonly property string trackId: !!root.entryData && root.entryData.trackId ? root.entryData.trackId : ""
    readonly property int playCount: root.trackId.length > 0 ? appFacade.trackStats.playCountFor(root.trackId) : 0
    readonly property int rating: root.trackId.length > 0 ? appFacade.trackStats.ratingFor(root.trackId) : 0
    readonly property string entryTitle: !!root.entryData && root.entryData.title ? root.entryData.title : ""
    readonly property string entryName: !!root.entryData && root.entryData.name ? root.entryData.name : ""
    readonly property string entryArtist: !!root.entryData && root.entryData.artist ? root.entryData.artist : ""
    readonly property string entryAlbum: !!root.entryData && root.entryData.album ? root.entryData.album : ""
    readonly property string entryArtwork: !!root.entryData && root.entryData.artworkSource ? root.entryData.artworkSource : ""
    readonly property bool hasArtwork: root.entryArtwork.length > 0
    readonly property string formatText: {
        var parts = [];
        if (!!root.entryData && root.entryData.format && root.entryData.format.length > 0)
            parts.push(root.entryData.format);
        if (!!root.entryData && root.entryData.sampleRate > 0)
            parts.push((root.entryData.sampleRate / 1000) + "kHz");
        if (!!root.entryData && root.entryData.bitDepth > 0)
            parts.push(root.entryData.bitDepth + "bit");
        return parts.join(" · ");
    }

    Rectangle {
        id: contentRect
        anchors.fill: parent
        color: Theme.surfaceColor
        radius: Theme.radiusLarge
        border.color: Theme.borderColor
        border.width: 1
        focus: true

        Keys.onEscapePressed: root.close()

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Title Bar
            Rectangle {
                id: titleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                color: "transparent"

                Text {
                    text: root.isFolder ? qsTr("文件夹详情") : qsTr("歌曲详情")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    anchors.centerIn: parent
                }

                StyleButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing12
                    anchors.verticalCenter: parent.verticalCenter
                    iconSource: "qrc:/qt/qml/Seriona/qml/assets/close.svg"
                    buttonWidth: 28
                    buttonHeight: 28
                    iconSize: 12
                    textColor: Theme.textSecondary
                    onClicked: root.close()
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.rightMargin: 48
                    onPressed: root.startSystemMove()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderColor
            }

            // Scrollable Content Area
            ScrollView {
                id: scrollView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: Theme.scrollbarWidth
                    background: Rectangle {
                        color: "transparent"
                    }
                    contentItem: Rectangle {
                        radius: Theme.radiusSmall
                        color: parent.hovered ? Theme.scrollbarHoverColor : Theme.scrollbarColor
                    }
                }

                ColumnLayout {
                    id: mainColumn
                    width: scrollView.availableWidth
                    spacing: Theme.spacing16

                    // Top Hero / Header Section
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacing24
                        Layout.rightMargin: Theme.spacing24
                        Layout.topMargin: Theme.spacing16
                        spacing: Theme.spacing12

                        // Cover / Thumbnail Container
                        Rectangle {
                            id: coverContainer
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 140
                            Layout.preferredHeight: 140
                            radius: Theme.radiusMedium
                            color: Theme.raisedSurfaceColor
                            border.color: Theme.borderSubtle
                            border.width: 1
                            antialiasing: true

                            // 1. Artwork image (works for both audio tracks and folders with resolved artwork)
                            Image {
                                id: artworkImage
                                anchors.fill: parent
                                source: root.entryArtwork
                                sourceSize: Qt.size(280, 280)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                visible: root.hasArtwork && status === Image.Ready

                                layer.enabled: true
                                layer.effect: OpacityMask {
                                    maskSource: Rectangle {
                                        width: 140
                                        height: 140
                                        radius: Theme.radiusMedium
                                    }
                                }
                            }

                            // 2. Folder SVG fallback (when isFolder is true and NO artwork is available)
                            Image {
                                id: folderIcon
                                anchors.fill: parent
                                anchors.margins: 32
                                source: "qrc:/qt/qml/Seriona/qml/assets/folder.svg"
                                sourceSize: Qt.size(80, 80)
                                fillMode: Image.PreserveAspectFit
                                visible: false
                            }

                            ColorOverlay {
                                anchors.fill: folderIcon
                                source: folderIcon
                                color: Theme.accentColor
                                visible: root.isFolder && (!root.hasArtwork || artworkImage.status !== Image.Ready)
                            }

                            // 3. Audio note fallback (when !isFolder and NO artwork is available)
                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusMedium
                                color: Theme.baseColor
                                visible: !root.isFolder && (!root.hasArtwork || artworkImage.status !== Image.Ready)

                                Text {
                                    anchors.centerIn: parent
                                    text: "♫"
                                    color: Theme.textOnAccent
                                    font.pixelSize: 44
                                }
                            }
                        }

                        // Main Title (selectable/copyable or clean text)
                        TextEdit {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            text: root.isFolder ? root.entryName : root.entryTitle
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontSubtitle
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            readOnly: true
                            selectByMouse: true
                            selectedTextColor: Theme.textOnAccent
                            selectionColor: Theme.accentColor
                            wrapMode: TextEdit.Wrap
                        }

                        // Subtitle: Artist - Album (for tracks)
                        TextEdit {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            visible: !root.isFolder && (root.entryArtist.length > 0 || root.entryAlbum.length > 0)
                            text: root.entryArtist.length > 0 && root.entryAlbum.length > 0
                                ? root.entryArtist + " · " + root.entryAlbum
                                : root.entryArtist + root.entryAlbum
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            horizontalAlignment: Text.AlignHCenter
                            readOnly: true
                            selectByMouse: true
                            selectedTextColor: Theme.textOnAccent
                            selectionColor: Theme.accentColor
                            wrapMode: TextEdit.Wrap
                        }

                        // Rating Card Section (for tracks)
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: ratingLayout.implicitHeight + Theme.spacing16
                            color: Theme.raisedSurfaceColor
                            radius: Theme.radiusMedium
                            border.color: Theme.borderSubtle
                            border.width: 1
                            visible: !root.isFolder

                            ColumnLayout {
                                id: ratingLayout
                                anchors.fill: parent
                                anchors.margins: Theme.spacing8
                                spacing: Theme.spacing4

                                Row {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: Theme.spacing8

                                    Repeater {
                                        model: 5

                                        Text {
                                            id: starIcon
                                            text: "★"
                                            color: (index + 1) <= root.rating ? Theme.ratingColor : Theme.ratingUnselectedColor
                                            font.pixelSize: Theme.fontHeading
                                            scale: starArea.pressed ? 1.25 : (starArea.containsMouse ? 1.15 : 1.0)

                                            Behavior on scale {
                                                NumberAnimation { duration: Theme.animationFast }
                                            }
                                            Behavior on color {
                                                ColorAnimation { duration: Theme.animationFast }
                                            }

                                            MouseArea {
                                                id: starArea
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: appFacade.trackStats.setRating(root.trackId, index + 1)
                                            }
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: root.rating > 0 ? qsTr("%1 星").arg(root.rating) : qsTr("未评级 (点击星星评分)")
                                    color: root.rating > 0 ? Theme.ratingColor : Theme.textDisabled
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }

                    // Metadata Properties Section (Card List: Label on top, Value below with selectability)
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacing24
                        Layout.rightMargin: Theme.spacing24
                        Layout.bottomMargin: Theme.spacing24
                        spacing: Theme.spacing8

                        Repeater {
                            model: root.isFolder
                                ? [
                                    {label: qsTr("上级目录"), value: root.entryData && root.entryData.parentName ? root.entryData.parentName : "—"},
                                    {label: qsTr("包含歌曲"), value: root.entryData && root.entryData.songCount !== undefined ? qsTr("%1 首").arg(root.entryData.songCount) : "—"},
                                    {label: qsTr("总时长"), value: root.entryData && root.entryData.duration ? root.entryData.duration : "—"},
                                    {label: qsTr("文件夹路径"), value: root.entryData && root.entryData.path ? root.entryData.path : "—"}
                                  ]
                                : [
                                    {label: qsTr("标题"), value: root.entryTitle.length > 0 ? root.entryTitle : "—"},
                                    {label: qsTr("艺术家"), value: root.entryArtist.length > 0 ? root.entryArtist : "—"},
                                    {label: qsTr("专辑"), value: root.entryAlbum.length > 0 ? root.entryAlbum : "—"},
                                    {label: qsTr("年份"), value: root.entryData && root.entryData.year ? String(root.entryData.year) : "—"},
                                    {label: qsTr("时长"), value: root.entryData && root.entryData.duration ? root.entryData.duration : "—"},
                                    {label: qsTr("音频格式"), value: root.entryData && root.entryData.format ? root.entryData.format : "—"},
                                    {label: qsTr("采样率"), value: root.entryData && root.entryData.sampleRate > 0 ? (root.entryData.sampleRate / 1000) + " kHz" : "—"},
                                    {label: qsTr("位深度"), value: root.entryData && root.entryData.bitDepth > 0 ? root.entryData.bitDepth + " bit" : "—"},
                                    {label: qsTr("规格组合"), value: root.formatText.length > 0 ? root.formatText : "—"},
                                    {label: qsTr("播放次数"), value: qsTr("%1 次").arg(root.playCount)},
                                    {label: qsTr("文件路径"), value: root.entryData && root.entryData.path ? root.entryData.path : "—"}
                                  ]

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: itemColumn.implicitHeight + Theme.spacing12 * 2
                                color: Theme.raisedSurfaceColor
                                radius: Theme.radiusMedium
                                border.color: Theme.borderSubtle
                                border.width: 1

                                ColumnLayout {
                                    id: itemColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.spacing12
                                    spacing: Theme.spacing4

                                    Text {
                                        text: modelData.label
                                        color: Theme.detailLabelColor
                                        font.pixelSize: Theme.fontCaption
                                        font.weight: Font.Medium
                                    }

                                    TextEdit {
                                        Layout.fillWidth: true
                                        text: modelData.value
                                        color: Theme.detailValueColor
                                        font.pixelSize: Theme.fontBody
                                        readOnly: true
                                        selectByMouse: true
                                        selectedTextColor: Theme.textOnAccent
                                        selectionColor: Theme.accentColor
                                        wrapMode: TextEdit.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
