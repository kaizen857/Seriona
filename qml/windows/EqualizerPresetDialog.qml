import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

// 均衡器预设管理对话框（任务 44 F2.4）——模态弹层（非独立 Window）。
//
// 交互裁定（与任务书一致并注释于此）：
//  - 列表 = settings.eqPresetList（C++ 保证内置 8 款恒在前、builtin=true 只读；
//    数据源直接绑定该 Q_PROPERTY——bandMode 切换/CRUD 后经 eqPresetListChanged
//    NOTIFY 自动刷新，本组件不做本地缓存/镜像）。分区视觉：分区头「内置预设」/
//    「我的预设」由 sectionedList 视图模型插入 header 行表达（纯派生，不改数据源）。
//  - 列表项单击 = 应用 + 关闭（与主流 EQ 预设交互一致，裁定采纳方案一）：
//    settings.applyEqPreset(id) 成功后关窗。内置/用户行都支持。
//  - 内置分区不可删不可改名：UI 不给入口（无重命名/删除按钮，仅「内置」徽标），
//    硬保证在 C++（renameEqPreset/deleteEqPreset 传内置 id 一律返回 false）。
//  - 用户行内重命名：点「重命名」→ 行内 TextField（原名预填）→ 回车提交
//    settings.renameEqPreset(id, 新名)；返回 false（空名/超长，C++ 校验）→ 保持
//    编辑态 + 红框闪烁 + 全选，不提交；Esc/失焦取消（仅取消，不关弹层）。
//  - 用户行内删除 = 二态按钮（裁定采纳行内二态）：点「删除」→ 变红态
//    「确认删除？」，再点执行 settings.deleteEqPreset(id)；此时点行内其它处或
//    点其它预设行先复位武装（不应用）；超时(4s)自动复位。
//  - 新增：底部「新建预设」TextField + 保存，settings.addEqPreset(name)（以当前
//    bandMode 的当前增益/preGain 存档）；空名/超长不提交（红框闪烁提示）。
//
// settings 引用由宿主（EqualizerWindow，任务 42）经 property 注入（仿 SettingsWindow
// `property var settings` 惯例）；本文件不引用 appFacade/任何控制器，自包含可单测。
Popup {
    id: root

    objectName: "equalizerPresetDialog"

    required property var settings

    // 视图模型（纯派生，来源 settings.eqPresetList）：
    // 每项 { kind: "header", text } / { kind: "empty", text } / { kind: "preset", ...源行字段 }
    // kind=header 表达分区头；kind=empty 在用户分区为空时占位；kind=preset 透传源行
    // （id/name/builtin/preGainDb/gains）。sectionedList 在绑定中读 settings.eqPresetList，
    // QML 依赖跟踪保证 eqPresetListChanged 时自动重算（NOTIFY 驱动，无本地镜像）。
    property var sectionedList: buildSectionedList(settings ? settings.eqPresetList : [])

    // 行内编辑状态（按预设 id 定位；仅用户行可进入）
    property string renamingId: ""
    property string confirmingDeleteId: ""

    // 删除确认超时复位
    property int deleteConfirmTimeoutMs: 4000

    modal: true
    focus: true
    parent: Overlay.overlay

    // 弹层尺寸：宽度贴近内容，高度受所属窗口约束（防 EQ 窗较小时被钳制截断）
    readonly property int dialogWidth: 520
    readonly property int dialogMargin: 24
    width: Math.min(dialogWidth, (Overlay.overlay ? Overlay.overlay.width : dialogWidth) - dialogMargin)
    height: Math.min(560, (Overlay.overlay ? Overlay.overlay.height : 560) - dialogMargin)
    x: Overlay.overlay ? Math.round((Overlay.overlay.width - width) / 2) : 0
    y: Overlay.overlay ? Math.round((Overlay.overlay.height - height) / 2) : 0

    padding: 0
    margins: 0

    // Esc / 点击遮罩关闭；行内编辑中的 Esc 由各 TextField 先行消费（仅取消编辑不关窗）
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 半透明遮罩（同 AboutOverlay 惯例）
    Overlay.modal: Rectangle {
        color: Theme.overlayScrimColor
    }

    background: Rectangle {
        color: "transparent"
    }

    // 关闭时复位所有行内编辑/武装状态（防下次打开残留：rename 文本、确认态、新建输入）
    onClosed: {
        root.renamingId = "";
        root.confirmingDeleteId = "";
        deleteConfirmTimer.stop();
        if (newNameInput) {
            newNameInput.text = "";
            newNameInput.errorFlash = false;
        }
    }

    // 删除确认超时 Timer（仅在武装后启动；触发/点其它处复位 confirmingDeleteId）
    Timer {
        id: deleteConfirmTimer
        interval: root.deleteConfirmTimeoutMs
        onTriggered: root.confirmingDeleteId = ""
    }

    function buildSectionedList(list) {
        var out = [];
        var builtinStarted = false;
        var userStarted = false;
        for (var i = 0; i < list.length; ++i) {
            var item = list[i];
            if (item.builtin) {
                if (!builtinStarted) {
                    builtinStarted = true;
                    out.push({ kind: "header", text: qsTr("内置预设") });
                }
                out.push(item);
            } else {
                if (!userStarted) {
                    userStarted = true;
                    out.push({ kind: "header", text: qsTr("我的预设") });
                }
                out.push(item);
            }
        }
        if (!userStarted) {
            out.push({ kind: "header", text: qsTr("我的预设") });
            out.push({ kind: "empty", text: qsTr("还没有用户预设，可在下方新建") });
        }
        return out;
    }

    // —— 预设行内动作（均由 delegate 行触发）——
    function startRename(presetId) {
        root.confirmingDeleteId = "";
        deleteConfirmTimer.stop();
        root.renamingId = presetId;
    }

    function cancelRename() {
        root.renamingId = "";
    }

    // 回车提交重命名：C++ 返回 false（空名/超长）→ 红框 + 全选，保持编辑态
    function commitRename(presetId, newName) {
        var trimmed = newName.trim();
        if (trimmed === "" || !root.settings.renameEqPreset(presetId, trimmed)) {
            root.renameErrorFlash(presetId);
            return;
        }
        root.renamingId = "";
    }

    function armDelete(presetId) {
        root.renamingId = "";
        if (root.confirmingDeleteId === presetId) {
            // 已武装同一行 = 第二步：执行删除
            root.confirmingDeleteId = "";
            deleteConfirmTimer.stop();
            if (root.settings) {
                root.settings.deleteEqPreset(presetId);
            }
        } else {
            // 首次点删除（或换行武装）：复位旧态 → 武装当前行
            root.confirmingDeleteId = presetId;
            deleteConfirmTimer.restart();
        }
    }

    function cancelDeleteConfirm() {
        root.confirmingDeleteId = "";
        deleteConfirmTimer.stop();
    }

    // 应用预设：成功即关窗（列表项单击 = 应用 + 关闭裁定）。
    // 若存在删除武装（点的是非武装行），本次点击视为「点其它处复位」——仅复位不应用。
    function applyPreset(presetId) {
        if (root.confirmingDeleteId !== "") {
            root.cancelDeleteConfirm();
            return;
        }
        if (root.settings && root.settings.applyEqPreset(presetId)) {
            root.close();
        }
    }

    // 红框闪烁：供各行重命名 TextField 复用（按 index 定位 delegate 实例）
    function renameErrorFlash(presetId) {
        var model = root.sectionedList;
        for (var i = 0; i < model.length; ++i) {
            if (model[i].kind === "preset" && model[i].id === presetId) {
                var item = presetListView.itemAtIndex(i);
                if (item) {
                    item.flashRenameError();
                }
                return;
            }
        }
    }

    // —— 行内文字动作按钮（重命名/删除/取消/保存，Basic Button 定制）——
    component InlineTextButton: Button {
        id: btn
        property color labelColor: Theme.textSecondary
        property color fillColor: "transparent"
        property color hoverFill: Theme.hoverColor
        property color pressFill: Theme.pressedColor

        implicitWidth: Math.max(56, contentItem.implicitWidth + Theme.spacing16)
        implicitHeight: 32
        padding: 0

        background: Rectangle {
            radius: Theme.radiusSmall
            color: !btn.enabled ? "transparent"
                 : btn.down ? btn.pressFill
                 : btn.hovered ? btn.hoverFill : btn.fillColor
            border.color: "transparent"
            border.width: 0
            Behavior on color {
                ColorAnimation { duration: Theme.animationFast }
            }
        }
        contentItem: Text {
            text: btn.text
            color: !btn.enabled ? Theme.textDisabled : btn.labelColor
            font.pixelSize: Theme.fontBody
            font.weight: btn.down ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // —— 主卡片 ——
    Rectangle {
        anchors.fill: parent
        color: Theme.raisedSurfaceColor
        radius: Theme.radiusLarge
        border.color: Theme.borderColor
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // 标题栏
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing16
                    anchors.rightMargin: Theme.spacing12
                    spacing: Theme.spacing8

                    Text {
                        Layout.fillWidth: true
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("均衡器预设")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontSubtitle
                        font.weight: Font.DemiBold
                    }

                    StyleButton {
                        objectName: "equalizerPresetDialogClose"
                        iconSource: "qrc:/qt/qml/Seriona/qml/assets/close.svg"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        iconSize: 14
                        baseColor: "transparent"
                        hoverColor: Theme.dangerHoverColor
                        pressedColor: Theme.dangerPressedColor
                        onClicked: root.close()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderColor
            }

            // 操作提示（交互裁定说明）
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.spacing16
                    anchors.rightMargin: Theme.spacing16
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("点击预设即应用；用户预设可重命名或删除，内置预设只读")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }
            }

            // 预设列表区（单 ListView，分区头/空态为视图模型行；每行 idle /
            // 重命名 / 删除确认三态互斥铺满整行）
            ListView {
                id: presetListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: Theme.spacing4
                Layout.bottomMargin: Theme.spacing4
                clip: true
                model: root.sectionedList
                spacing: 2

                ScrollBar.vertical: StyledScrollBar {}

                delegate: Item {
                    id: delegateRoot
                    required property var modelData
                    required property int index

                    width: presetListView.width
                    height: modelData.kind === "header" ? 30
                          : modelData.kind === "empty" ? 36
                          : 40

                    // 供 renameErrorFlash 定位：红框闪烁
                    function flashRenameError() {
                        if (renameField) {
                            renameField.showError();
                        }
                    }

                    // 分区头
                    Rectangle {
                        anchors.fill: parent
                        visible: modelData.kind === "header"
                        color: "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacing16
                            anchors.rightMargin: Theme.spacing16
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.text
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    // 空态占位
                    Rectangle {
                        anchors.fill: parent
                        visible: modelData.kind === "empty"
                        color: "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacing16
                            anchors.rightMargin: Theme.spacing16
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.text
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideRight
                        }
                    }

                    // ============ 预设行：idle 态 ============
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing8
                        anchors.rightMargin: Theme.spacing8
                        spacing: Theme.spacing4
                        visible: modelData.kind === "preset"
                                 && root.renamingId !== modelData.id
                                 && root.confirmingDeleteId !== modelData.id

                        // 行点击 = 应用 + 关闭（删除武装期间点击 = 复位）
                        MouseArea {
                            id: applyArea
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.applyPreset(modelData.id)

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radiusSmall
                                color: applyArea.containsMouse ? Theme.hoverColor : "transparent"
                                Behavior on color {
                                    ColorAnimation { duration: Theme.animationFast }
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacing8
                                anchors.rightMargin: Theme.spacing8
                                spacing: Theme.spacing8

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: modelData.name
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                }

                                // 内置徽标（只读提示；无任何操作按钮）
                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    visible: modelData.builtin
                                    Layout.preferredHeight: 18
                                    implicitWidth: Math.max(builtinTag.implicitWidth + Theme.spacing8, 40)
                                    radius: Theme.radiusSmall
                                    color: Theme.baseColor

                                    Text {
                                        id: builtinTag
                                        anchors.centerIn: parent
                                        text: qsTr("内置")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }
                            }
                        }

                        // 用户行操作按钮列（内置行无按钮）
                        RowLayout {
                            Layout.alignment: Qt.AlignVCenter
                            spacing: Theme.spacing4
                            visible: !modelData.builtin

                            InlineTextButton {
                                text: qsTr("重命名")
                                labelColor: Theme.accentColor
                                onClicked: root.startRename(modelData.id)
                            }

                            InlineTextButton {
                                text: qsTr("删除")
                                labelColor: Theme.dangerColor
                                hoverFill: Theme.baseColor
                                onClicked: root.armDelete(modelData.id)
                            }
                        }
                    }

                    // ============ 预设行：重命名态 ============
                    TextField {
                        id: renameField
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing4
                        anchors.rightMargin: Theme.spacing4
                        visible: modelData.kind === "preset" && !modelData.builtin
                                 && root.renamingId === modelData.id
                        text: modelData.name
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        selectByMouse: true

                        property bool errorFlash: false
                        function showError() {
                            renameField.errorFlash = true;
                            errorResetTimer.restart();
                            renameField.selectAll();
                            renameField.forceActiveFocus();
                        }

                        Timer {
                            id: errorResetTimer
                            interval: 1200
                            onTriggered: renameField.errorFlash = false
                        }

                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.baseColor
                            border.color: renameField.errorFlash ? Theme.dangerColor
                                         : (renameField.activeFocus ? Theme.borderAccent : Theme.borderColor)
                            border.width: 1
                        }

                        onVisibleChanged: {
                            if (visible) {
                                renameField.errorFlash = false;
                                renameField.forceActiveFocus();
                                renameField.selectAll();
                            }
                        }

                        // 回车提交（Return/Enter 双键）
                        Keys.onReturnPressed: root.commitRename(modelData.id, renameField.text)
                        Keys.onEnterPressed: root.commitRename(modelData.id, renameField.text)
                        Keys.onEscapePressed: {
                            event.accepted = true;
                            root.cancelRename();
                        }

                        // 失焦取消（Tab 移走 / 点击其它处）；提交成功路径先清 renamingId，
                        // 此后再失焦不会重复触发
                        onActiveFocusChanged: {
                            if (!activeFocus && root.renamingId === modelData.id) {
                                root.cancelRename();
                            }
                        }
                    }

                    // ============ 预设行：删除确认态 ============
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing8
                        anchors.rightMargin: Theme.spacing8
                        spacing: Theme.spacing4
                        visible: modelData.kind === "preset" && !modelData.builtin
                                 && root.confirmingDeleteId === modelData.id

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            Layout.leftMargin: Theme.spacing8
                            text: qsTr("删除预设“%1”？").arg(modelData.name)
                            color: Theme.dangerColor
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }

                        InlineTextButton {
                            text: qsTr("取消")
                            labelColor: Theme.textSecondary
                            onClicked: root.cancelDeleteConfirm()
                        }

                        InlineTextButton {
                            text: qsTr("确认删除")
                            labelColor: Theme.textOnAccent
                            fillColor: Theme.dangerColor
                            hoverFill: Theme.dangerHoverColor
                            pressFill: Theme.dangerPressedColor
                            onClicked: root.armDelete(modelData.id) // 已武装同一行 = 第二步：执行删除
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.borderColor
            }

            // ============ 新建预设区（底部） ============
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing16
                    anchors.rightMargin: Theme.spacing16
                    spacing: Theme.spacing8

                    TextField {
                        id: newNameInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        placeholderText: qsTr("预设名称…")
                        placeholderTextColor: Theme.textDisabled
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        selectByMouse: true
                        maximumLength: 64

                        property bool errorFlash: false
                        Timer {
                            id: newNameErrorTimer
                            interval: 1200
                            onTriggered: newNameInput.errorFlash = false
                        }

                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.baseColor
                            border.color: newNameInput.errorFlash ? Theme.dangerColor
                                         : (newNameInput.activeFocus ? Theme.borderAccent : Theme.borderColor)
                            border.width: 1
                        }

                        Keys.onReturnPressed: saveNewPreset()
                        Keys.onEnterPressed: saveNewPreset()

                        function saveNewPreset() {
                            var trimmed = newNameInput.text.trim();
                            if (trimmed === "" || trimmed.length > 64
                                    || !root.settings.addEqPreset(trimmed)) {
                                newNameInput.errorFlash = true;
                                newNameErrorTimer.restart();
                                newNameInput.forceActiveFocus();
                                newNameInput.selectAll();
                                return;
                            }
                            newNameInput.text = "";
                            newNameInput.errorFlash = false;
                        }
                    }

                    InlineTextButton {
                        text: qsTr("保存")
                        labelColor: Theme.textOnAccent
                        fillColor: Theme.accentColor
                        hoverFill: Theme.borderAccent
                        pressFill: Theme.pressedColor
                        onClicked: newNameInput.saveNewPreset()
                    }
                }
            }
        }
    }
}
