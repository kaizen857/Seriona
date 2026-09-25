import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

// 歌词行右键菜单（W3/D14）：四项 = 修正原文/译文、此行无译文、恢复本行自动识别、
// 查看本行自动判定。前三项经 appFacade 外发控制命令写 manual；第四项【只读】——
// 展示被 manual 覆盖【之前】的自动判定结果（autoOriginal/autoTranslation），不发任何命令。
// 前两项需要文本输入/呈现，落在两个独立 Window（与 ConfirmDeleteDialog/SortDialog 同模式）。
Item {
    id: root

    // 由 Main.qml 注入；var（非 AppFacade 强类型）使只加载本组件的既有集成测试
    // （未注册 AppFacade QML 类型）仍可实例化；调用仍落到 AppFacade 的 Q_INVOKABLE。
    property var appFacade: null

    // 打开菜单时收集的行数据（含被覆盖前的 auto 值）
    property var lineData: ({})

    readonly property bool lineIsManual: !!(lineData && lineData.manualOverride)
    readonly property string autoOriginalText: lineIsManual ? (lineData.autoOriginal || "") : (lineData.displayLine || "")
    readonly property string autoTranslationText: lineIsManual ? (lineData.autoTranslation || "") : (lineData.translation || "")

    readonly property bool isOpen: lineMenu.visible

    function close() {
        lineMenu.close();
    }

    function openForLine(data, globalX, globalY) {
        root.lineData = data;
        lineMenu.showAtGlobal(globalX, globalY);
    }

    Item {
        id: menuAnchor
        width: 1
        height: 1
        visible: false
    }

    BubbleMenu {
        id: lineMenu
        objectName: "lyricLineContextMenu"
        menuWidth: 176
        arrowDirection: "up"
        targetItem: menuAnchor

        BubbleMenuItem {
            objectName: "lyricCorrectItem"
            text: qsTr("修正原文/译文")
            onTriggered: {
                lineMenu.close();
                correctionDialog.openFor(root.lineData);
            }
        }

        BubbleMenuItem {
            objectName: "lyricNoTranslationItem"
            text: qsTr("此行无译文")
            onTriggered: {
                lineMenu.close();
                if (root.appFacade) {
                    // 保留当前原文，译文传显式空串（契约：空串 = 此行无译文）。
                    root.appFacade.upsertLyricSplitCorrection(root.lineData.rawLine,
                                                             root.lineData.displayLine,
                                                             "");
                }
            }
        }

        BubbleMenuItem {
            objectName: "lyricRestoreAutoItem"
            text: qsTr("恢复本行自动识别")
            onTriggered: {
                lineMenu.close();
                if (root.appFacade)
                    root.appFacade.removeLyricSplitCorrection(root.lineData.rawLine);
            }
        }

        BubbleMenuItem {
            objectName: "lyricAutoJudgementItem"
            text: qsTr("查看本行自动判定")
            onTriggered: {
                lineMenu.close();
                // 只读展示：不发任何命令。
                // autoJudgeDialog 是 Window（不是 Popup）：Window 没有 open()，只有 show()
                // （A1 修复；此前 open() 在运行时抛 TypeError，弹窗永不出现）。
                autoJudgeDialog.show();
            }
        }
    }

    // 修正原文/译文：真实用户输入（两个文本框），保存经 UpsertLyricSplitCorrection。
    Window {
        id: correctionDialog
        objectName: "lyricCorrectionDialog"

        flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        modality: Qt.ApplicationModal
        color: "transparent"
        transientParent: root.Window.window
        width: 460
        height: Math.round(correctionColumn.implicitHeight)
        // 居中基准是 Window 自己的 transientParent（本组件根是 Item，没有 transientParent
        // 属性；A2 修复前写成 root.transientParent 恒为 undefined → x/y 恒 0，居中分支是死代码）。
        x: correctionDialog.transientParent ? Math.round(correctionDialog.transientParent.x + (correctionDialog.transientParent.width - width) / 2) : 0
        y: correctionDialog.transientParent ? Math.round(correctionDialog.transientParent.y + (correctionDialog.transientParent.height - height) / 2) : 0

        property string rawLine: ""
        // 空原文（含全空白）拒绝保存的原因文案；非空即表示本次输入未被接受。
        property string originalError: ""

        function openFor(data) {
            rawLine = data && data.rawLine ? data.rawLine : "";
            originalField.text = data && data.displayLine ? data.displayLine : "";
            translationField.text = data && data.translation ? data.translation : "";
            originalError = "";
            correctionDialog.show();
            originalField.forceActiveFocus();
        }

        // 保存按钮的完整行为（可被用例直接调用，无需模拟鼠标）：
        // 空原文（含全空白）不提交、不关窗、就地给出原因；否则经门函数外发命令并关窗。
        function saveFromFields() {
            if (!root.appFacade)
                return false;
            if (!root.appFacade.isLyricOriginalSubmittable(originalField.text)) {
                originalError = qsTr("原文不能为空（含全空白）");
                originalField.forceActiveFocus();
                return false;
            }
            originalError = "";
            const submitted = root.appFacade.commitLyricSplitCorrection(rawLine,
                                                                       originalField.text,
                                                                       translationField.text);
            close();
            return submitted;
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.raisedSurfaceColor
            radius: Theme.radiusLarge
            border.color: Theme.borderColor
            border.width: 1
            focus: true

            Keys.onEscapePressed: correctionDialog.close()

            ColumnLayout {
                id: correctionColumn
                anchors.fill: parent
                anchors.margins: Theme.spacing16
                spacing: Theme.spacing8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("修正原文/译文")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("原文")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontBody
                }

                TextField {
                    id: originalField
                    objectName: "lyricCorrectionOriginalField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    selectByMouse: true

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.baseColor
                        border.color: originalField.activeFocus ? Theme.borderAccent : Theme.borderColor
                        border.width: 1
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("译文")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontBody
                }

                TextField {
                    id: translationField
                    objectName: "lyricCorrectionTranslationField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    selectByMouse: true

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.baseColor
                        border.color: translationField.activeFocus ? Theme.borderAccent : Theme.borderColor
                        border.width: 1
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("译文留空表示此行无译文")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }

                // 空原文拒绝提示（A6）：与拖动路径同口径 —— 原文为空（含全空白）的写入
                // 对用户不可解释，不做「只留译文」。保存被拒时在此给出原因。
                Text {
                    objectName: "lyricCorrectionOriginalError"
                    Layout.fillWidth: true
                    visible: correctionDialog.originalError !== ""
                    text: correctionDialog.originalError
                    color: Theme.warningColor
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    spacing: Theme.spacing8

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 34
                        radius: Theme.radiusSmall
                        color: cancelArea.pressed ? Theme.pressedColor : (cancelArea.containsMouse ? Theme.hoverColor : "transparent")
                        border.color: Theme.borderSubtle
                        border.width: 1

                        MouseArea {
                            id: cancelArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: correctionDialog.close()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("取消")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 34
                        radius: Theme.radiusSmall
                        color: saveArea.pressed ? Qt.darker(Theme.accentColor, 1.2) : (saveArea.containsMouse ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor)

                        MouseArea {
                            id: saveArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: correctionDialog.saveFromFields()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("保存")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
            }
        }
    }

    // 查看本行自动判定：只读。展示被覆盖前的 auto 值；未命中 manual 的行标注
    // 「当前显示的就是自动结果」。绝不展示修正后的当前值，也不发命令。
    Window {
        id: autoJudgeDialog
        objectName: "lyricAutoJudgementDialog"

        flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        modality: Qt.ApplicationModal
        color: "transparent"
        transientParent: root.Window.window
        width: 440
        height: Math.round(judgeColumn.implicitHeight)
        // 同 correctionDialog：居中基准是 Window 自己的 transientParent（A2 修复）。
        x: autoJudgeDialog.transientParent ? Math.round(autoJudgeDialog.transientParent.x + (autoJudgeDialog.transientParent.width - width) / 2) : 0
        y: autoJudgeDialog.transientParent ? Math.round(autoJudgeDialog.transientParent.y + (autoJudgeDialog.transientParent.height - height) / 2) : 0

        Rectangle {
            anchors.fill: parent
            color: Theme.raisedSurfaceColor
            radius: Theme.radiusLarge
            border.color: Theme.borderColor
            border.width: 1
            focus: true

            Keys.onEscapePressed: autoJudgeDialog.close()

            ColumnLayout {
                id: judgeColumn
                anchors.fill: parent
                anchors.margins: Theme.spacing16
                spacing: Theme.spacing8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("本行自动判定")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("自动判定原文")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    Layout.fillWidth: true
                    text: root.lineIsManual ? (root.lineData.autoOriginal || qsTr("（空）")) : (root.lineData.displayLine || qsTr("（空）"))
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("自动判定译文")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontBody
                }

                Text {
                    Layout.fillWidth: true
                    text: root.autoTranslationText || qsTr("（空）")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: !root.lineIsManual
                    text: qsTr("当前显示的就是自动结果")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.lineIsManual
                    text: qsTr("该行已手工修正，以上为覆盖前的自动结果")
                    color: Theme.secondaryTextColor
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    spacing: Theme.spacing8

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredWidth: 88
                        Layout.preferredHeight: 34
                        radius: Theme.radiusSmall
                        color: closeArea.pressed ? Theme.pressedColor : (closeArea.containsMouse ? Theme.hoverColor : "transparent")
                        border.color: Theme.borderSubtle
                        border.width: 1

                        MouseArea {
                            id: closeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: autoJudgeDialog.close()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("关闭")
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                        }
                    }
                }
            }
        }
    }
}
