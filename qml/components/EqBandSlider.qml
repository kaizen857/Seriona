import QtQuick
import QtQuick.Controls.Basic
import Seriona

// ============================================================
// EqBandSlider —— GEQ 频段 / pre-gain 可复用的竖条增益组件
// 用途：均衡器单频段或总增益（pre-gain）的竖直滑块条。
//   - 竖置 QtQuick.Controls Slider（标准件路径，无 Canvas/Shapes 图形依赖），
//     范围 ±15 dB、交互步进 0.5 dB（拖动松手吸附，键盘每次 0.5）。
//   - 0 dB 中线：滑块行程正中绘有中性色刻度线；正值增益（手柄在线上方）以
//     「增强色」填充中线上段，负值增益（手柄在线下方）以「削减色」填充中线
//     下段，拖动跨 0 即时切换颜色与方向。
//   - 顶部频率标签：band 身份（外部传 bandLabel 自定义文本，pre-gain 传"总增益"；
//     否则按 bandFrequencyHz 自动格式化：31/62/125/250/500/1k/2k/4k/8k/16k 或
//     1.25k/6.3k 等，内部 helper 完成，不依赖外部频率表）。R4 按用户实测由底部对调至此。
//   - 底部数值文本（如 +2.5 / −3.0 / 0.0，一位小数）；单击进入 TextField 编辑：
//     0.1 dB 精度、输入超界提交时钳位到 ±15、回车提交、Esc 取消恢复原值。
// 属性：
//   value        real   当前增益，数值域 [-15, 15]（内部展示会钳位；允许 0.1 网格）。
//   bandFrequencyHz real 频段中心频率（Hz）。bandLabel 为空时用它生成顶部标签。
//   bandLabel    string 顶部标签文本；非空则覆盖自动频率文本（如 pre-gain "总增益"）。
//   positiveColor color 正值增益色（默认 Theme.accentColor —— 品牌强调/增强）。
//   negativeColor color 负值增益色（默认 Theme.dangerColor —— 削减/负面语义）。
//   neutralColor color 0 dB 中线 / 零值手柄与文本色（默认 Theme.textSecondary）。
// 信号：
//   userEdited(real newValue)  用户改动后的最终值（拖动手柄提交 或 数值编辑提交）。
//                              程序化赋值 value 不会触发本信号（消费端据此接 50ms 去抖）。
// 写回机制（F2.3 修复，勿回归）：
//   - root.value 只作「外部可写存储」：外部（delegate 的 value: {...} 绑定，预设应用/
//     复位/镜像 NOTIFY 经它赋值）是它唯一写者。组件内任何地方禁止命令式写 root.value——
//     对绑定属性做命令式赋值会永久销毁该外部绑定（Qt 语义铁律，与赋值位置无关），
//     此后 resetEq/applyEqPreset 等整表写回不再刷新本滑条（42 review B1 实证）。
//   - 内部单向传导：Binding{ 内部 slider.value ← root.value } 把外部程序化值推入滑块；
//     用户拖动/键盘直接改内部 slider.value，经 onValueChanged 同步到 _displayValue 镜像
//     ——读数/填充/手柄颜色统一读该镜像，保证拖动实时跟手、不依赖外部去抖回写刷新。
//   - 用户路径（拖动 onMoved / 数值编辑提交）只发 userEdited(real)，绝不回写 root.value；
//     消费端收信号写 settings → NOTIFY → delegate value 绑定重算 → 写回 root.value →
//     Binding 推回内部 slider，闭环保持外部存储与显示一致。
// 数值域：±15 dB；拖动与键盘步进 0.5；数值编辑按 0.1 网格取整并钳位 ±15。
// 无障碍：底层即真实 Slider —— 方向键/Home/End 可操作，Accessible 名称取自可见标签。
// ============================================================
Item {
    id: root

    // ---- 对外属性（默认值用仓库 Theme token，可按需覆盖） ----
    property real value: 0
    property real bandFrequencyHz: 0
    property string bandLabel: ""
    property color positiveColor: Theme.accentColor
    property color negativeColor: Theme.dangerColor
    property color neutralColor: Theme.textSecondary

    // ---- 内部常量 / 只读派生 ----
    readonly property real _gainMin: -15
    readonly property real _gainMax: 15
    readonly property real _handleW: 18
    readonly property real _handleH: 18
    readonly property real _railW: 4

    // 0 dB 处手柄在行程中的位置（0=行程顶，1=行程底）。from=-15/to=15 时恰为 0.5。
    readonly property real _zeroVpos: (_gainMax - 0) / (_gainMax - _gainMin)

    property bool _editing: false          // 数值编辑器是否展开

    signal userEdited(real newValue)

    // 实时展示源（内部镜像 bandSlider.value）：程序化推送（root.value → Binding）与
    // 用户交互（拖动/键盘）都会更新它。读数/填充/手柄颜色统一读本镜像——拖动实时
    // 跟手、不依赖外部去抖回写才刷新。组件内绝不命令式写 root.value（见文件头机制）。
    property real _displayValue: 0

    // ---- 频率 / 数值格式化 helper（放组件内，自包含） ----
    // 频率 → 底标签：<1000 显示整数 Hz；≥1000 显示 "k" 单位并去除多余尾零
    // （1000→1k、1250→1.25k、6300→6.3k、20000→20k）。
    function formatFrequency(hz) {
        if (!isFinite(hz) || hz <= 0)
            return "";
        if (hz < 1000)
            return Math.round(hz).toString();
        var khz = hz / 1000;
        var s = khz.toFixed(3).replace(/0+$/, "").replace(/\.$/, "");
        return s + "k";
    }

    // 当前展示的顶标签：优先自定义 bandLabel（pre-gain "总增益"），否则按频率格式化。
    readonly property string displayBandLabel:
        root.bandLabel.length > 0 ? root.bandLabel : formatFrequency(root.bandFrequencyHz)

    // 增益 → 一位小数文本：正数带 "+"、负数带 "-"、|x|<0.05 归零显示 "0.0"。
    function formatGain(g) {
        var a = Math.round(Math.abs(g) * 10) / 10;
        if (a < 0.05)
            return "0.0";
        return (g > 0 ? "+" : "-") + a.toFixed(1);
    }

    // 颜色随符号：正=增强色、负=削减色、|x|<0.05 按 0 显示中性色（与读数归零口径一致）。
    // 读 _displayValue 实时镜像：拖动跨 0 即时切换、外部写回同源刷新。
    readonly property color _gainColor: Math.abs(root._displayValue) < 0.05 ? root.neutralColor
                                        : root._displayValue > 0 ? root.positiveColor
                                                                 : root.negativeColor

    implicitWidth: 64
    implicitHeight: 236

    // ============================================================
    // 上部：频率 / 自定义标签（band 身份；R4 与下部数值读数对调）
    // ============================================================
    Item {
        id: labelArea
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 20

        Text {
            anchors.fill: parent
            text: root.displayBandLabel
            color: root.enabled ? Theme.textSecondary : Theme.textDisabled
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    // ============================================================
    // 中部：竖置 Slider（标准 QtQuick.Controls 组件 + 自定义外观）
    // ============================================================
    Item {
        id: sliderHost
        anchors.top: labelArea.bottom
        anchors.bottom: readoutArea.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacing2
        anchors.bottomMargin: Theme.spacing2

        Slider {
            id: bandSlider
            anchors.fill: parent
            orientation: Qt.Vertical
            from: root._gainMin
            to: root._gainMax
            stepSize: 0.5
            snapMode: Slider.SnapOnRelease
            enabled: root.enabled
            focusPolicy: Qt.StrongFocus

            // 单向传导：root.value（外部唯一写者）经 Binding 推入内部 slider。Binding 对象
            // 在用户拖动/脚本对 slider.value 写后仍存活（source 变化时重新推入），故外部
            // 整表写回（复位/预设/镜像）能持续刷新滑块。显示不读 root.value 而读 _displayValue
            // 镜像：拖动实时跟手，且 root.value 的命令式赋值（会杀外部 delegate 绑定）被根除。
            Binding {
                target: bandSlider
                property: "value"
                value: Math.max(root._gainMin, Math.min(root._gainMax, root.value))
            }
            onValueChanged: root._displayValue = bandSlider.value
            // 用户路径只发信号、不回写 root.value；0.5 步进已在拖动松手时吸附完成。
            onMoved: root.userEdited(bandSlider.value)

            Accessible.name: root.displayBandLabel.length > 0 ? root.displayBandLabel
                                                              : qsTr("均衡器频段")
            Accessible.description: qsTr("上下拖动或使用方向键调节增益，单击下方数值可直接输入")

            // ---- 自定义竖条外观 ----
            background: Item {
                id: sliderBg
                // 显式铺到 padding 盒（与 Basic 风格同约定），子项用其局部坐标计算。
                x: bandSlider.leftPadding
                y: bandSlider.topPadding
                width: bandSlider.availableWidth
                height: bandSlider.availableHeight

                readonly property real handleHalf: root._handleH / 2
                readonly property real railTop: handleHalf
                readonly property real railHeight: bandSlider.availableHeight - root._handleH

                // 手柄中心纵坐标（局部）：vpos 从行程顶(0)排到行程底(1)。
                function centerY(vpos) {
                    return railTop + vpos * (railHeight);
                }
                // 当前手柄中心 & 0dB 中线手柄中心
                readonly property real curCenterY: centerY(bandSlider.visualPosition)
                readonly property real zeroCenterY: centerY(root._zeroVpos)

                // 底部轨道（中性底色）
                Rectangle {
                    x: (sliderBg.width - root._railW) / 2
                    y: sliderBg.railTop
                    width: root._railW
                    height: sliderBg.railHeight
                    radius: root._railW / 2
                    color: Theme.progressBarTrackColor
                }

                // 0 dB 中线标记（高于手柄直径的横线，值非 0 时仍能定位中线）
                Rectangle {
                    x: (sliderBg.width - (root._handleW + 8)) / 2
                    y: sliderBg.zeroCenterY - 1
                    width: root._handleW + 8
                    height: 1
                    color: root.enabled ? root.neutralColor : Theme.textDisabled
                    opacity: 0.9
                }

                // 正/负填充段：从 0dB 中线延伸到当前手柄中心，颜色随符号即时切换。
                Rectangle {
                    id: gainFill
                    x: (sliderBg.width - root._railW) / 2
                    width: root._railW
                    radius: root._railW / 2
                    color: root._gainColor
                    // 正值：手柄在中线上方（curCenterY < zeroCenterY），填充 y=cur..zero
                    visible: bandSlider.value !== 0 && Math.abs(bandSlider.value) >= 0.05
                    y: bandSlider.value > 0 ? sliderBg.curCenterY : sliderBg.zeroCenterY
                    height: bandSlider.value > 0
                            ? sliderBg.zeroCenterY - sliderBg.curCenterY
                            : sliderBg.curCenterY - sliderBg.zeroCenterY
                    opacity: root.enabled ? 0.95 : 0.4
                }
            }

            handle: Rectangle {
                id: gainHandle
                width: root._handleW
                height: root._handleH
                radius: width / 2
                // 竖置 Slider 几何：x 居中；y = topPadding + visualPosition*(availableHeight-h)
                x: bandSlider.leftPadding + (bandSlider.availableWidth - width) / 2
                y: bandSlider.topPadding + bandSlider.visualPosition * (bandSlider.availableHeight - height)
                color: root.enabled ? root._gainColor : Theme.textDisabled
                border.width: bandSlider.visualFocus ? 2 : 1
                border.color: bandSlider.visualFocus ? Theme.borderAccent : Theme.borderSubtle
                scale: bandSlider.pressed ? 1.1 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: Theme.animationFast }
                }
            }
        }
    }

    // ============================================================
    // 下部：数值读数区（显示 or 编辑；R4 与上部频率标签对调）
    // ============================================================
    Item {
        id: readoutArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 28

        // 常态读数文本
        Text {
            id: gainText
            anchors.fill: parent
            visible: !root._editing
            text: formatGain(root._displayValue)
            color: root.enabled ? root._gainColor : Theme.textDisabled
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        // 单击读数 → 进入编辑
        MouseArea {
            anchors.fill: parent
            visible: !root._editing
            enabled: root.enabled
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.beginEditing()
        }

        // 编辑态输入框（回车提交 / Esc 取消 / 失焦提交）
        TextField {
            id: gainEditor
            anchors.fill: parent
            visible: root._editing
            enabled: root.enabled
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            leftPadding: 2
            rightPadding: 2
            topPadding: 3
            bottomPadding: 3
            selectByMouse: true
            validator: RegularExpressionValidator {
                // 允许中间态：可选符号、至多 2 位整数、至多 1 位小数（0.1 网格）
                regularExpression: /^[+-]?\d{0,2}(\.\d?)?$/
            }
            background: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.baseColor
                border.width: 1
                border.color: gainEditor.activeFocus ? Theme.borderAccent : Theme.borderColor
            }
            onAccepted: root.commitEditing()
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape && root._editing) {
                    event.accepted = true;   // 消费 Esc：仅取消本编辑器，不外传给窗口关闭逻辑
                    root.cancelEditing();
                }
            }
            onEditingFinished: {
                // 失焦（点别处）也按「提交」处理；Esc 已先置 _editing=false，不会重复提交
                if (root._editing)
                    root.commitEditing();
            }
        }
    }

    // ============================================================
    // 数值编辑行为
    // ============================================================
    function beginEditing() {
        if (!root.enabled)
            return;
        root._editing = true;
        gainEditor.text = formatGain(root._displayValue);
        // 展开后再聚焦并全选，保证回车/输入直接覆盖旧值
        gainEditor.forceActiveFocus();
        gainEditor.selectAll();
    }

    // 回车 / 失焦提交：解析 → 越界钳位 ±15 → 0.1 网格取整 → 写回并广播
    function commitEditing() {
        if (!root._editing)
            return;
        var raw = gainEditor.text.trim();
        var num;
        if (raw === "")
            num = NaN;                       // 空文本视作无效输入
        else
            num = Number(raw);
        if (isNaN(num)) {                    // 非法字符：忽略本次编辑，恢复显示
            root.cancelEditing();
            return;
        }
        var clamped = Math.max(root._gainMin, Math.min(root._gainMax, num));
        var grid = Math.round(clamped * 10) / 10;
        // 提交只落到内部 slider（读数/手柄即刻跟随，镜像经 onValueChanged 同步），并广播
        // userEdited；不写 root.value（那是外部 delegate 的绑定面，命令式赋值即杀绑定）。
        // 消费端 settings 写回 → delegate value 绑定重算 → root.value → Binding 推回内部
        // slider，闭环保持外部存储与显示一致。
        bandSlider.value = grid;
        root.userEdited(grid);
        root._editing = false;
        gainEditor.focus = false;
    }

    // Esc / 非法输入取消：放弃本次编辑，恢复进入编辑前的值（值本身未被改动）
    function cancelEditing() {
        if (!root._editing)
            return;
        root._editing = false;
        gainEditor.focus = false;
    }
}
