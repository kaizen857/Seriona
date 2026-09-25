import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

// 纠错管理列表（todo 34）：范围 = 当前曲目。
// 数据来源 = appFacade.lyrics.lines() 中 manualOverride === true 的行
//（不调用任何全库读命令、不新增读命令、不直连 DB/FS）。
// 单条删除与批量「恢复自动识别」都经 appFacade.removeLyricSplitCorrection 逐行外发
// RemoveLyricSplitCorrection（约定值由桥层取自当前 TrackLyricsSnapshot.convention）；
// 批量恢复【绝不】走全库清理命令 —— 那会连带清掉其它曲目的纠错。
Item {
    id: root

    property var appFacade: null

    // 当前曲目 manual 行的呈现模型；增删或切歌后由 refresh() 整份重建。
    ListModel {
        id: manualModel
        objectName: "lyricCorrectionManualModel"
    }

    implicitHeight: contentColumn.implicitHeight

    function currentLines() {
        if (!root.appFacade || !root.appFacade.lyrics || !root.appFacade.lyrics.lines)
            return [];
        return root.appFacade.lyrics.lines();
    }

    // 从当前曲目快照重建列表：只收 manualOverride === true 的行。
    // 先 clear 再逐行 append ⇒ 切歌后不会残留上一首的条目。
    function refresh() {
        manualModel.clear();
        const rows = root.currentLines();
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i];
            if (!row || row.manualOverride !== true)
                continue;
            manualModel.append({
                rawLine: (row.rawLine !== undefined && row.rawLine !== null) ? String(row.rawLine) : "",
                original: (row.displayLine !== undefined && row.displayLine !== null) ? String(row.displayLine) : "",
                translation: (row.translation !== undefined && row.translation !== null) ? String(row.translation) : "",
                autoOriginal: (row.autoOriginal !== undefined && row.autoOriginal !== null) ? String(row.autoOriginal) : "",
                autoTranslation: (row.autoTranslation !== undefined && row.autoTranslation !== null) ? String(row.autoTranslation) : ""
            });
        }
    }

    // 单条删除：键是 rawLine（= line.text = 回传键），不是 displayLine/translation。
    // 键往返错会静默写错行，故此处不提供任何按展示值取键的重载。
    // 空键与 restoreAll() 同口径：后端对空键是拒绝的（bridge 本地不发命令），
    // 故这里也不外发，直接返回 false。
    function removeRow(rawLine) {
        if (!root.appFacade)
            return false;
        const key = (rawLine !== undefined && rawLine !== null) ? String(rawLine) : "";
        if (key.length === 0)
            return false;
        const removed = root.appFacade.removeLyricSplitCorrection(key);
        root.refresh();
        return removed;
    }

    // 批量「恢复自动识别」：限于当前曲目 —— 对快照中每个 manual 行各发一次
    // RemoveLyricSplitCorrection（逐行，N 次，N = 当前曲目有效 manual 行数）。
    // 空 rawLine 无有效回传键（后端会本地拒绝），不计入、不外发。返回实际发出的条数。
    function restoreAll() {
        if (!root.appFacade)
            return 0;
        const rows = root.currentLines();
        let issued = 0;
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i];
            if (!row || row.manualOverride !== true)
                continue;
            const key = (row.rawLine !== undefined && row.rawLine !== null) ? String(row.rawLine) : "";
            if (key.length === 0)
                continue;
            root.appFacade.removeLyricSplitCorrection(key);
            ++issued;
        }
        root.refresh();
        return issued;
    }

    // 快照重发布（切歌/增删后的时机③）→ 列表整份重建；stub/非模型目标上未知信号被忽略。
    // 只订阅【结构性】变化（整份替换 / 行增删）：刷新是 clear()+逐行 append 的整份重建，
    // 若挂到每次 dataChanged（播放行推进即触发）会在用户滚动时把视口反复打回。
    // 纠错写入会改变 manualOverride/autoOriginal/autoTranslation，而 LyricsModel 的去重谓词
    // 包含这三个字段 ⇒ 内容变化走的是整份替换 → modelReset，故不订阅 dataChanged。
    Connections {
        target: root.appFacade ? root.appFacade.lyrics : null
        ignoreUnknownSignals: true
        function onModelReset() { root.refresh(); }
        function onRowsInserted() { root.refresh(); }
        function onRowsRemoved() { root.refresh(); }
    }

    Component.onCompleted: root.refresh()

    ColumnLayout {
        id: contentColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Theme.spacing8

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing8

            Text {
                Layout.fillWidth: true
                text: qsTr("已记录的纠错")
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
            }

            Text {
                objectName: "lyricCorrectionCountText"
                text: qsTr("%1 条").arg(manualModel.count)
                color: Theme.secondaryTextColor
                font.pixelSize: Theme.fontCaption
                verticalAlignment: Text.AlignVCenter
            }
        }

        // 列表视口高度恒定（滚动而非随条数改布局）：ListView 是 Flickable，在 ColumnLayout 里
        // 切换 visible / 改写 Layout.preferredHeight 时布局不会按新值收敛，故视口固定高度、
        // 空态提示叠在视口中央（列表范围本身由 manualModel 的内容决定，不设条数上限）。
        Item {
            objectName: "lyricCorrectionViewport"
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            clip: true

            Text {
                anchors.centerIn: parent
                width: parent.width - Theme.spacing16
                visible: manualModel.count === 0
                text: qsTr("当前曲目没有已记录的纠错")
                color: Theme.secondaryTextColor
                font.pixelSize: Theme.fontCaption
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            ListView {
                id: correctionList
                objectName: "lyricCorrectionList"
                anchors.fill: parent
                clip: true
                model: manualModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: StyledScrollBar {}

                delegate: Rectangle {
                    id: rowRect
                    objectName: "lyricCorrectionRowDelegate"
                    required property int index
                    required property string rawLine
                    required property string original
                    required property string translation
                    required property string autoOriginal
                    required property string autoTranslation

                    width: ListView.view.width
                    height: rowLayout.implicitHeight + Theme.spacing8 * 2
                    radius: Theme.radiusSmall
                    color: "transparent"

                    ColumnLayout {
                        id: rowLayout
                        anchors.fill: parent
                        anchors.margins: Theme.spacing8
                        spacing: Theme.spacing4

                        Text {
                            objectName: "lyricCorrectionRawText"
                            Layout.fillWidth: true
                            text: rowRect.rawLine
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.Wrap
                        }

                        Text {
                            objectName: "lyricCorrectionCurrentText"
                            Layout.fillWidth: true
                            text: qsTr("当前修正：原文 %1 ｜ 译文 %2")
                                      .arg(rowRect.original.length > 0 ? rowRect.original : qsTr("（空）"))
                                      .arg(rowRect.translation.length > 0 ? rowRect.translation : qsTr("（空）"))
                            color: Theme.secondaryTextColor
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }

                        Text {
                            objectName: "lyricCorrectionAutoText"
                            Layout.fillWidth: true
                            text: qsTr("恢复后自动判定：原文 %1 ｜ 译文 %2")
                                      .arg(rowRect.autoOriginal.length > 0 ? rowRect.autoOriginal : qsTr("（空）"))
                                      .arg(rowRect.autoTranslation.length > 0 ? rowRect.autoTranslation : qsTr("（空）"))
                            color: Theme.secondaryTextColor
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Item { Layout.fillWidth: true }

                            Rectangle {
                                Layout.preferredWidth: 88
                                Layout.preferredHeight: 26
                                radius: Theme.radiusSmall
                                color: removeArea.pressed
                                       ? Theme.dangerPressedColor
                                       : (removeArea.containsMouse ? Theme.dangerHoverColor : Theme.dangerColor)
                                activeFocusOnTab: true
                                Accessible.role: Accessible.Button
                                Accessible.name: qsTr("恢复本行自动识别")

                                MouseArea {
                                    id: removeArea
                                    objectName: "lyricCorrectionRemoveButton"
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.removeRow(rowRect.rawLine)
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("恢复本行")
                                    color: Theme.textOnAccent
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "lyricCorrectionRestoreAllButton"
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            radius: Theme.radiusSmall
            enabled: manualModel.count > 0
            color: !enabled
                   ? Theme.borderSubtle
                   : (restoreArea.pressed
                      ? Qt.darker(Theme.accentColor, 1.2)
                      : (restoreArea.containsMouse ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor))
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("恢复自动识别")

            MouseArea {
                id: restoreArea
                anchors.fill: parent
                enabled: parent.enabled
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.restoreAll()
            }

            Text {
                anchors.centerIn: parent
                text: qsTr("恢复自动识别（当前曲目）")
                color: Theme.textOnAccent
                font.pixelSize: Theme.fontBody
            }
        }
    }
}
