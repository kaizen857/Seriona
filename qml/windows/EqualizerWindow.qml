import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

// 均衡器窗口（F2.1 框架层 + F2.2 内容布局，任务 41/42）。
// 缩放手柄仿 Main.qml 的 ResizeArea（原 350-471）自包含同款实现：Main 的缩放手柄文本
// 被 tests/frontend/adapter/tst_ui_only_handler_policy.cpp 契约锁定（红线守卫），
// 不能把 Main 的实现在此抽取迁移，故本窗复制同款逻辑且保持 Main.qml 零改动。
//
// 内容布局（F2.2 任务 42）自上而下四段：
//   ① 工具行：10/31 频段切换 + 总开关（enabled）+ 复位（resetEq）
//   ② 图谱区（单 Canvas）：对数频率轴 20-20k、dB 轴 ±15、0dB 中线；
//      频谱 bins（60 桶，半透明下层，可开关）→ 频响曲线（181 点，2px 上层）
//   ③ 底部控制条：限幅器开关 + 预设快捷应用 + 保存（另存为用户预设）+ 管理
//   ④ GEQ 竖条区：pre-gain 固定左 + 分隔线 + 横向 ScrollView 内 band 竖条
//      （内容不缩放，宽度不足横向滚动）
//
// —— settings 来源（42 接线裁定）——
// 44 弹层与全部控件按 SettingsWindow 惯例消费 settings（AppFacade 的 SettingsController）。
// Main.qml 的 equalizerWindow 实例（L480-484）当前未注入 appFacade（任务约束：本任务
// 不得改 Main.qml；Main 的 smoke 扩展在任务 46 才动）。实测：本窗口作为 Main 根 Window
// 的直接子对象实例化时，其根 Window 的 `appFacade` property 经 QML 创建上下文作用域可被
// 本组件解析（settings-menu smoke 下 console 探针证实 typeof appFacade === "object"）。
// 因此这里不声明同名本地 property（会遮蔽动态解析），仅经 _resolveSettings() 在需要时
// 读取；独立载入（harness/单测）无该上下文时回退 null，控件只读不崩。任务 46 若改为
// 显式注入也无需改本文件逻辑。
//
// —— 预设管理弹层（44）实例化裁定 ——
// 44 弹层经 Loader(active:false) 惰性承载（见组件体 :~906），首击「管理」才激活并 open()。
// 理由：44 的分区列表 delegate 对 header/empty 行的 modelData.name/builtin 绑定在对象实例化
// 即求值（Popup 未 open 也建树），惰性加载可避免 EQ 窗每次启动/显示产生 3 条
// "Unable to assign [undefined]..." 运行时告警（44 内部潜在细节，本任务不改 44；已记入
// .omo/plans/equalizer-b0-notes.md 任务 42 记录）。弹层根自带 objectName
// "equalizerPresetDialog"，46 打开后可按 findChild 断言。
//
// —— 数据语义（F1.3 镜像面）——
// curvePoints/curveFrequencies = 181 点（reducer 先行全轴 20-20k 对数；首帧快照未到前
// 为空/全 0 频率轴 → 容错为空态，不绘线）。spectrumBins = 60 桶 double（后端频谱推送未
// 接线 = 空快照 60 全 0 → 判为"无频谱数据"）。bandGains10/31 = QVariantList 逐项 ±15dB
// （0.1 网格；C++ setter 归一化 + 50ms 去抖推送）。bandMode 10/31、enabled、
// limiterEnabled、spectrumEnabled 变更 C++ 即持久化 + 立即推送。
//
// —— fs<40k 截断标注裁定（如实降级）——
// 频谱桶轴上限按 fs/2 截断需要快照 sampleRate；settings 镜像面未存 sampleRate（任务 38
// 裁定：settings.sampleRate 是输出目标率，回填会污染输出配置，不映射；F1.3 只镜像了
// binsDb 60 桶）。前端拿不到实际 fs → 本任务不加 C++ 镜像面（无数据源、徒增面），频谱桶
// 按 20-20k 全轴绘制；后端已把不可测桶（≥0.95×fs/2 band）置 -120 地板。此为已知限制，
// 待后端频谱推送接线并镜像 sampleRate 后可在此补充截断（单点：_freqX 映射）。
Window {
    id: root
    objectName: "equalizerWindow"

    flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"

    width: 860
    height: 620
    minimumWidth: 640
    minimumHeight: 420

    // ================================================================
    // settings 解析 + 常量表 + 图谱几何/判定助手（见文件头裁定注释）
    // ================================================================
    readonly property var settings: _resolveSettings()
    function _resolveSettings() {
        // appFacade 无本地声明，经创建上下文动态解析（见文件头）；无则 null
        if (typeof appFacade === "undefined" || !appFacade)
            return null;
        return appFacade.settings;
    }

    // 当前档位频点表（Hz double，与 src/app/equalizer_presets.h 注释一致；
    // 显示格式化由 EqBandSlider 内 formatFrequency 完成，此处只供传参）
    readonly property var bandFreqs10: [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    readonly property var bandFreqs31: [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000]

    // 生效档位（无 settings 时按 10 段默认，控件只读兜底）
    readonly property int _bandMode: (settings && settings.bandMode === 31) ? 31 : 10
    readonly property int _bandCount: _bandMode === 31 ? 31 : 10

    // 频率表（仅定义用：绘制频率标签/竖条传参不依赖 settings）
    function bandFreqs(mode) {
        return mode === 31 ? root.bandFreqs31 : root.bandFreqs10;
    }
    function freqForBand(index) {
        var t = bandFreqs(root._bandMode);
        return (index >= 0 && index < t.length) ? t[index] : 0;
    }

    // 图谱判据：曲线数据有效（频率轴 20-20k 单调且点对与增益等长）
    function hasCurveData() {
        if (!root.settings)
            return false;
        var fs = root.settings.curveFrequencies;
        var ps = root.settings.curvePoints;
        if (!fs || !ps || fs.length < 2 || ps.length !== fs.length)
            return false;
        if (fs[0] <= 0 || fs[fs.length - 1] <= fs[0])
            return false;
        for (var i = 0; i < fs.length; ++i) {
            if (!isFinite(fs[i]) || fs[i] <= 0)
                return false;
        }
        return true;
    }

    // 频谱数据判据：60 桶中存在任意非零有效值（空快照 = 60 全 0 → false）
    function hasSpectrumData() {
        if (!root.settings)
            return false;
        var bins = root.settings.spectrumBins;
        if (!bins || bins.length < 2)
            return false;
        for (var i = 0; i < bins.length; ++i) {
            var v = bins[i];
            if (isFinite(v) && v !== 0)
                return true;
        }
        return false;
    }

    // 颜色 → 带 alpha 的 canvas 可用色（色相仍取自 Theme token）
    function withAlpha(color, a) {
        return Qt.rgba(color.r, color.g, color.b, a);
    }

    // —— 图谱几何映射（画布局部坐标系） ——
    // 边距：左 44 留 dB 数字；上 12；右 10；下 24 留频率标签。
    function plotMetrics(w, h) {
        var left = 44, right = 10, top = 12, bottom = 24;
        return { x: left, y: top, w: Math.max(24, w - left - right),
                 h: Math.max(24, h - top - bottom) };
    }
    function freqToX(f, m) {
        var f0 = 20, f1 = 20000;
        var lf = Math.log(f / f0) / Math.log(f1 / f0);
        return m.x + lf * m.w;
    }
    function dbToY(db, m) {
        // +15 → 顶(m.y)，-15 → 底(m.y+m.h)
        return m.y + ((15 - db) / 30) * m.h;
    }
    function freqLabel(f) {
        if (f < 1000)
            return String(Math.round(f));
        var s = (f / 1000).toFixed(f % 1000 === 0 ? 0 : 1);
        if (s.indexOf(".") >= 0)
            s = s.replace(/0+$/, "").replace(/\.$/, "");
        return s + "k";
    }

    // 主绘制：网格 → 频谱（下层）→ 曲线（上层）
    function drawGraph(ctx, w, h) {
        var m = plotMetrics(w, h);
        // 底色区（轻微抬升，区分图谱卡片内容）
        ctx.fillStyle = root.withAlpha(Theme.textPrimary, 0.02);
        ctx.fillRect(m.x, m.y, m.w, m.h);

        // —— dB 横向网格线（每 5dB 一条，含 ±15 边线）+ 左侧刻度 ——
        ctx.font = "9px sans-serif";
        ctx.textBaseline = "middle";
        for (var db = 15; db >= -15; db -= 5) {
            var y = dbToY(db, m);
            var major = (db === 0);
            ctx.strokeStyle = major ? root.withAlpha(Theme.textPrimary, 0.55)
                                    : (db === 15 || db === -15 ? root.withAlpha(Theme.textPrimary, 0.28)
                                                               : root.withAlpha(Theme.textPrimary, 0.13));
            ctx.lineWidth = major ? 1.2 : 1;
            ctx.beginPath();
            ctx.moveTo(m.x, y);
            ctx.lineTo(m.x + m.w, y);
            ctx.stroke();
            // 刻度文本（0 与边线更亮）
            ctx.fillStyle = major ? root.withAlpha(Theme.textSecondary, 0.9)
                                  : root.withAlpha(Theme.textSecondary, 0.6);
            ctx.textAlign = "right";
            ctx.fillText((db > 0 ? "+" : "") + db, m.x - 5, y);
        }

        // —— 对数频率竖线 + 底部标签 ——
        var ticks = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
        ctx.textAlign = "center";
        for (var t = 0; t < ticks.length; ++t) {
            var fx = freqToX(ticks[t], m);
            ctx.strokeStyle = root.withAlpha(Theme.textPrimary, 0.1);
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(fx, m.y);
            ctx.lineTo(fx, m.y + m.h);
            ctx.stroke();
            ctx.fillStyle = root.withAlpha(Theme.textSecondary, 0.75);
            ctx.fillText(freqLabel(ticks[t]), fx, m.y + m.h + 11);
        }

        // —— 频谱 bins（下层半透明；关/无数据则跳过） ——
        if (root.settings && root.settings.spectrumEnabled && root.hasSpectrumData()) {
            var bins = root.settings.spectrumBins;
            var n = bins.length;
            // 独立标定：0dBFS(0) 抵到 0dB 中线（y0），-60dB 抵图底；[-120..-60] 压缩到底线
            var y0 = dbToY(0, m);
            var span = y0 - (m.y + m.h);
            ctx.fillStyle = root.withAlpha(Theme.accentColor, 0.16);
            for (var b = 0; b < n; ++b) {
                var v = bins[b];
                if (!isFinite(v) || v >= 0)
                    continue;
                var frac = Math.min(1, -v / 60);      // -60dB → 全高到底
                var bx = freqToX(20 * Math.pow(1000, b / (n - 1)), m);
                var bw = Math.max(1.5, (m.w / n) * 0.72);
                var by = y0 - frac * span;
                ctx.fillRect(bx - bw / 2, by, bw, y0 - by);
            }
        }

        // —— 频响曲线（上层 2px；无效/空数据不绘，空态由 UI 提示文本表达） ——
        if (root.hasCurveData()) {
            var ps = root.settings.curvePoints;
            var fs = root.settings.curveFrequencies;
            ctx.strokeStyle = Theme.accentColor;
            ctx.lineWidth = 2;
            ctx.lineJoin = "round";
            ctx.beginPath();
            var started = false;
            for (var i = 0; i < ps.length; ++i) {
                var g = ps[i];
                if (!isFinite(g))
                    continue;
                var cx = freqToX(fs[i], m);
                var cy = dbToY(Math.max(-15, Math.min(15, g)), m);
                if (!started) {
                    ctx.moveTo(cx, cy);
                    started = true;
                } else {
                    ctx.lineTo(cx, cy);
                }
            }
            ctx.stroke();
        }
    }

    function requestGraphPaint() {
        if (graphCanvas)
            graphCanvas.requestPaint();
    }

    // ================================================================
    // 窗口体
    // ================================================================
    Rectangle {
        id: contentRect
        anchors.fill: parent
        color: Theme.surfaceColor
        radius: Theme.radiusLarge
        border.color: Theme.borderColor
        border.width: 1
        focus: true

        Keys.onEscapePressed: {
            root.close();
        }

        // Title Bar
        Rectangle {
            id: titleBar
            width: parent.width
            height: 48
            color: "transparent"

            Text {
                text: qsTr("均衡器")
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
                onClicked: {
                    root.close();
                }
            }

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 40 // Don't overlap close button
                onPressed: {
                    root.startSystemMove();
                }
            }
        }

        Rectangle {
            id: divider
            width: parent.width
            height: 1
            anchors.top: titleBar.bottom
            color: Theme.borderColor
        }

        // ================================================================
        // Content Area（F2.2 完整布局：工具行 / 图谱 / 控制条 / GEQ 竖条）
        // ================================================================
        Item {
            anchors.top: divider.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing12
                anchors.rightMargin: Theme.spacing12
                anchors.topMargin: Theme.spacing8
                anchors.bottomMargin: Theme.spacing8
                spacing: Theme.spacing8

                // ----------------------------------------------------
                // ① 工具行：10/31 切换 + 总开关 + 复位
                // ----------------------------------------------------
                RowLayout {
                    id: toolRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    spacing: Theme.spacing8

                    Text {
                        text: qsTr("频段")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // 10 段
                    EqTextButton {
                        id: mode10Button
                        objectName: "eqMode10Button"
                        text: qsTr("10 段")
                        minWidth: 52
                        active: root._bandMode === 10
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: {
                            if (root.settings)
                                root.settings.bandMode = 10;
                        }
                    }

                    // 31 段
                    EqTextButton {
                        id: mode31Button
                        objectName: "eqMode31Button"
                        text: qsTr("31 段")
                        minWidth: 52
                        active: root._bandMode === 31
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: {
                            if (root.settings)
                                root.settings.bandMode = 31;
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // 总开关（EQ 主启用）
                    Text {
                        text: qsTr("总开关")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        Layout.alignment: Qt.AlignVCenter
                    }

                    EqSwitch {
                        id: masterSwitch
                        objectName: "eqMasterSwitch"
                        checked: root.settings ? root.settings.enabled : false
                        Layout.alignment: Qt.AlignVCenter
                        onToggled: {
                            if (root.settings)
                                root.settings.enabled = checked;
                        }
                    }

                    EqTextButton {
                        id: resetButton
                        objectName: "eqResetButton"
                        text: qsTr("复位")
                        minWidth: 56
                        labelColor: Theme.textSecondary
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: {
                            if (root.settings)
                                root.settings.resetEq();
                        }
                    }
                }

                // ----------------------------------------------------
                // ② 图谱区：卡片 + 单 Canvas（频谱下层/曲线上层/空态）
                // ----------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 72
                    color: Theme.raisedSurfaceColor
                    radius: Theme.radiusMedium
                    border.color: Theme.borderSubtle
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        // 图卡片头：标题 + 频谱显示开关
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            color: "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacing12
                                anchors.rightMargin: Theme.spacing8
                                spacing: Theme.spacing8

                                Text {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: qsTr("频谱与频响")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: qsTr("频谱")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                    Layout.alignment: Qt.AlignVCenter
                                }

                                EqSwitch {
                                    id: spectrumSwitch
                                    objectName: "eqSpectrumSwitch"
                                    checked: root.settings ? root.settings.spectrumEnabled : false
                                    compact: true
                                    Layout.alignment: Qt.AlignVCenter
                                    onToggled: {
                                        if (root.settings)
                                            root.settings.spectrumEnabled = checked;
                                    }
                                }
                            }
                        }

                        // 绘图宿主（撑满余下空间；画布 + 空态提示叠放）
                        Item {
                            id: plotHost
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.leftMargin: Theme.spacing4
                            Layout.rightMargin: Theme.spacing4
                            Layout.bottomMargin: Theme.spacing2

                            // Canvas 绘图区（单画布：网格 → 频谱下层 → 曲线上层）
                            Canvas {
                                id: graphCanvas
                                objectName: "eqGraphCanvas"
                                anchors.fill: parent
                                renderStrategy: Canvas.Cooperative

                                onPaint: {
                                    var ctx = getContext("2d");
                                    if (!ctx)
                                        return;
                                    ctx.clearRect(0, 0, width, height);
                                    root.drawGraph(ctx, width, height);
                                }

                                // 画布尺寸变化须重绘
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                            }

                            // 空态提示（覆盖于画布之上）：无频响数据 → 播放引导；
                            // 有频响但无频谱数据 → 频谱说明。依赖在 visible 绑定表达式内
                            // 直读 settings 属性 → NOTIFY 自动刷新。
                            Text {
                                id: graphEmptyHint
                                objectName: "eqGraphEmptyHint"
                                anchors.centerIn: parent
                                text: root.hasCurveData() ? qsTr("暂无频谱数据") : qsTr("播放音频后显示实时频响")
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontBody
                                visible: !root.hasCurveData() || !root.hasSpectrumData()
                            }
                        }
                    }
                }

                // ----------------------------------------------------
                // ③ 底部控制条：限幅器开关 + 预设快捷应用 + 保存 + 管理
                // ----------------------------------------------------
                RowLayout {
                    id: controlBar
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    spacing: Theme.spacing8

                    Text {
                        text: qsTr("限幅器")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        Layout.alignment: Qt.AlignVCenter
                    }

                    EqSwitch {
                        id: limiterSwitch
                        objectName: "eqLimiterSwitch"
                        checked: root.settings ? root.settings.limiterEnabled : false
                        Layout.alignment: Qt.AlignVCenter
                        onToggled: {
                            if (root.settings)
                                root.settings.limiterEnabled = checked;
                        }
                    }

                    Text {
                        text: qsTr("防止削波，限制输出峰值")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        Layout.alignment: Qt.AlignVCenter
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }

                    // 预设快捷应用（下拉）
                    EqTextButton {
                        id: presetApplyButton
                        objectName: "eqPresetApplyButton"
                        text: qsTr("预设 ▾")
                        minWidth: 72
                        labelColor: Theme.textSecondary
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: root.openPresetPicker()
                    }

                    // 保存当前设置为用户预设
                    EqTextButton {
                        id: savePresetButton
                        objectName: "eqSavePresetButton"
                        text: qsTr("保存")
                        minWidth: 60
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: {
                            saveNameInput.text = "";
                            savePresetPopup.open();
                        }
                    }

                    // 管理（打开预设管理弹层 44）
                    EqTextButton {
                        id: managePresetsButton
                        objectName: "eqManagePresetsButton"
                        text: qsTr("管理")
                        minWidth: 60
                        labelColor: Theme.accentColor
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: root.openManagePresets()
                    }
                }

                // ----------------------------------------------------
                // ④ GEQ 竖条区：pre-gain 固定左 + 分隔 + 横向滚动 band 竖条
                // ----------------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 236
                    Layout.minimumHeight: 184
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        // —— pre-gain（总增益），固定左侧 ——
                        Item {
                            Layout.preferredWidth: 76
                            Layout.fillHeight: true

                            EqBandSlider {
                                id: preGainSlider
                                objectName: "eqPreGainSlider"
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacing4
                                anchors.rightMargin: Theme.spacing4
                                bandLabel: qsTr("总增益")
                                value: root.settings ? root.settings.preGainDb : 0
                                enabled: root.settings ? root.settings.enabled : false
                                onUserEdited: (v) => {
                                    if (root.settings)
                                        root.settings.preGainDb = v;
                                }
                            }
                        }

                        // 分隔线
                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                            Layout.leftMargin: Theme.spacing2
                            Layout.rightMargin: Theme.spacing6
                            color: Theme.borderColor
                        }

                        // —— band 竖条横向滚动区（内容不缩放，宽度不足滚动） ——
                        ScrollView {
                            id: bandScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            contentWidth: bandRow.width
                            contentHeight: bandRow.height
                            ScrollBar.horizontal: ScrollBar {
                                policy: ScrollBar.AsNeeded
                                height: Theme.scrollbarWidth
                                background: Rectangle { color: "transparent" }
                                contentItem: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: parent.hovered ? Theme.scrollbarHoverColor : Theme.scrollbarColor
                                }
                            }
                            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                            Row {
                                id: bandRow
                                width: root._bandCount * 66 + (root._bandCount - 1) * spacing
                                height: bandScroll.availableHeight
                                spacing: Theme.spacing2

                                Repeater {
                                    model: root._bandCount

                                    // 每档一个 EqBandSlider；用户改动经闭包 index 整表写回
                                    // （C++ setter 归一化 + 50ms 去抖推送）
                                    EqBandSlider {
                                        required property int index

                                        width: 66
                                        height: bandRow.height
                                        bandFrequencyHz: root.freqForBand(index)
                                        enabled: root.settings ? root.settings.enabled : false
                                        objectName: "eqBandSlider" + index

                                        value: {
                                            if (!root.settings)
                                                return 0;
                                            var gains = root._bandMode === 31 ? root.settings.bandGains31
                                                                                : root.settings.bandGains10;
                                            return (gains && gains.length > index) ? gains[index] : 0;
                                        }

                                        onUserEdited: (v) => {
                                            if (!root.settings)
                                                return;
                                            var key = root._bandMode === 31 ? "bandGains31" : "bandGains10";
                                            var list = root.settings[key].slice();
                                            if (index < list.length)
                                                list[index] = v;
                                            root.settings[key] = list;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 镜像数据变化 → 图谱重绘（QObject 子项，不参与布局）
        Connections {
            target: root.settings
            function onCurvePointsChanged() { root.requestGraphPaint(); }
            function onCurveFrequenciesChanged() { root.requestGraphPaint(); }
            function onSpectrumBinsChanged() { root.requestGraphPaint(); }
            function onSpectrumEnabledChanged() { root.requestGraphPaint(); }
        }
    }

    // ================================================================
    // 预设快捷应用下拉（非模态，点按钮下方弹出）
    // ================================================================
    Popup {
        id: presetMenu
        objectName: "eqPresetPickerMenu"
        width: 240
        height: Math.min(presetMenuList.contentHeight + 40, 320)
        padding: 0
        margins: 0
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
        modal: false

        // 打开前由 openPresetPicker 定位
        contentItem: Rectangle {
            color: Theme.raisedSurfaceColor
            radius: Theme.radiusMedium
            border.color: Theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    Layout.leftMargin: Theme.spacing12
                    Layout.rightMargin: Theme.spacing12
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("应用预设")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }

                ListView {
                    id: presetMenuList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: root.settings ? root.settings.eqPresetList : []

                    delegate: ItemDelegate {
                        width: presetMenuList.width
                        height: 32
                        required property var modelData
                        required property int index

                        contentItem: Text {
                            text: modelData.name
                            color: parent.hovered ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacing12
                            elide: Text.ElideRight
                        }
                        background: Rectangle {
                            color: parent.hovered ? Theme.hoverColor : "transparent"
                        }
                        onClicked: {
                            if (root.settings && root.settings.applyEqPreset(modelData.id))
                                presetMenu.close();
                        }
                    }
                }
            }
        }
    }

    function openPresetPicker() {
        if (!root.settings)
            return;
        if (!root.settings.eqPresetList.length) {
            root.openManagePresets(); // 空列表（异常态）→ 引导到管理新建
            return;
        }
        // 弹层不手动改 parent：Qt Quick Controls 的 Popup 打开时自挂所属窗口的
        // Overlay（手设普通 Item parent 会导致 open() 不显示）。坐标空间 = Overlay
        // = 窗口 contentItem（原点一致），故直接映射到 contentItem 定位。
        // 弹层高度在打开后才按列表行数收敛，这里用行数估算高度先定位（上弹），
        // 超窗高时改向下弹；delegate 恒 32 高，估算即实际。
        var hostItem = root.contentItem ? root.contentItem : contentRect;
        var estH = Math.min(root.settings.eqPresetList.length * 32 + 40, 320);
        var pos = presetApplyButton.mapToItem(hostItem, 0, 0);
        var px = pos.x + presetApplyButton.width - presetMenu.width;
        var py = pos.y - estH - Theme.spacing4;
        if (py < 4)
            py = pos.y + presetApplyButton.height + Theme.spacing4; // 上方不够，改下方
        presetMenu.x = Math.max(4, Math.min(px, hostItem.width - presetMenu.width - 4));
        presetMenu.y = Math.max(4, py);
        presetMenu.open();
    }

    // ================================================================
    // 保存预设弹层（小输入框：另存当前增益为命名用户预设）
    // ================================================================
    Popup {
        id: savePresetPopup
        objectName: "eqSavePresetPopup"
        modal: true
        focus: true
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(360, (Overlay.overlay ? Overlay.overlay.width : 360) - 48)
        height: 172
        x: Overlay.overlay ? Math.round((Overlay.overlay.width - width) / 2) : 0
        y: Overlay.overlay ? Math.round((Overlay.overlay.height - height) / 2) : 0
        padding: 0

        Overlay.modal: Rectangle {
            color: Theme.overlayScrimColor
        }
        background: Rectangle { color: "transparent" }

        contentItem: Rectangle {
            color: Theme.raisedSurfaceColor
            radius: Theme.radiusLarge
            border.color: Theme.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing16
                spacing: Theme.spacing12

                Text {
                    text: qsTr("保存为预设")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                }

                TextField {
                    id: saveNameInput
                    objectName: "eqSaveNameInput"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    placeholderText: qsTr("预设名称")
                    placeholderTextColor: Theme.textDisabled
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    leftPadding: Theme.spacing8
                    rightPadding: Theme.spacing8
                    selectByMouse: true
                    maximumLength: 64

                    property bool errorFlash: false

                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.baseColor
                        border.width: 1
                        border.color: saveNameInput.errorFlash ? Theme.dangerColor
                                                               : (saveNameInput.activeFocus ? Theme.borderAccent : Theme.borderColor)
                    }
                    onAccepted: root.confirmSavePreset()

                    // 红框闪烁（空名/超长/重名被 C++ 拒绝时）
                    onErrorFlashChanged: {
                        if (saveNameInput.errorFlash) {
                            saveErrorTimer.restart();
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.spacing8

                    Item { Layout.fillWidth: true }

                    EqTextButton {
                        text: qsTr("取消")
                        minWidth: 60
                        labelColor: Theme.textSecondary
                        onClicked: savePresetPopup.close()
                    }

                    EqTextButton {
                        text: qsTr("保存")
                        minWidth: 60
                        labelColor: Theme.textOnAccent
                        onClicked: root.confirmSavePreset()
                    }
                }
            }
        }

        onOpened: {
            saveNameInput.forceActiveFocus();
        }
        onClosed: {
            saveNameInput.errorFlash = false;
        }
    }

    Timer {
        id: saveErrorTimer
        interval: 1400
        onTriggered: saveNameInput.errorFlash = false
    }

    function confirmSavePreset() {
        if (!root.settings)
            return;
        var name = saveNameInput.text.trim();
        if (name.length === 0) {
            saveNameInput.errorFlash = true;
            return;
        }
        if (!root.settings.addEqPreset(name)) {
            saveNameInput.errorFlash = true;
            return;
        }
        savePresetPopup.close();
    }

    // 预设管理弹层（44）：settings 传入。用 Loader 惰性实例化——弹层未打开时
    // 不建对象（44 的分区列表 delegate 对 header/empty 行的 modelData.name 绑定
    // 在实例化即求值会产生 undefined 告警；惰性加载可避免启动噪音，且符合
    // "Loader.active:false 释放未用组件"惯例）。46 可于打开后按 objectName 断言。
    Loader {
        id: presetDialogLoader
        active: false
        sourceComponent: Component {
            EqualizerPresetDialog {
                settings: root.settings
            }
        }
    }

    function openManagePresets() {
        if (!root.settings)
            return;
        if (!presetDialogLoader.item)
            presetDialogLoader.active = true;
        if (presetDialogLoader.item)
            presetDialogLoader.item.open();
    }

    // 自绘八向 resize（仿 Main.qml ResizeArea）：边缘 5px、四角 10x10 命中区，
    // 光标随边缘切换，按下请求系统缩放（Qt 6.8 Window.startSystemResize；
    // offscreen/无窗口系统支持时无副作用）。
    Item {
        anchors.fill: parent
        z: 1000

        // Top
        ResizeArea {
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: 10
                rightMargin: 10
            }
            height: 5
            edgeFlag: Qt.TopEdge
        }
        // Bottom
        ResizeArea {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                leftMargin: 10
                rightMargin: 10
            }
            height: 5
            edgeFlag: Qt.BottomEdge
        }
        // Left
        ResizeArea {
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
                topMargin: 10
                bottomMargin: 10
            }
            width: 5
            edgeFlag: Qt.LeftEdge
        }
        // Right
        ResizeArea {
            anchors {
                right: parent.right
                top: parent.top
                bottom: parent.bottom
                topMargin: 10
                bottomMargin: 10
            }
            width: 5
            edgeFlag: Qt.RightEdge
        }
        // TopLeft
        ResizeArea {
            anchors {
                left: parent.left
                top: parent.top
            }
            width: 10
            height: 10
            edgeFlag: Qt.TopEdge | Qt.LeftEdge
        }
        // TopRight
        ResizeArea {
            anchors {
                right: parent.right
                top: parent.top
            }
            width: 10
            height: 10
            edgeFlag: Qt.TopEdge | Qt.RightEdge
        }
        // BottomLeft
        ResizeArea {
            anchors {
                left: parent.left
                bottom: parent.bottom
            }
            width: 10
            height: 10
            edgeFlag: Qt.BottomEdge | Qt.LeftEdge
        }
        // BottomRight
        ResizeArea {
            anchors {
                right: parent.right
                bottom: parent.bottom
            }
            width: 10
            height: 10
            edgeFlag: Qt.BottomEdge | Qt.RightEdge
        }
    }

    component ResizeArea: MouseArea {
        property int edgeFlag
        cursorShape: {
            switch (edgeFlag) {
            case (Qt.TopEdge | Qt.LeftEdge):
                return Qt.SizeFDiagCursor;
            case (Qt.BottomEdge | Qt.RightEdge):
                return Qt.SizeFDiagCursor;
            case (Qt.TopEdge | Qt.RightEdge):
                return Qt.SizeBDiagCursor;
            case (Qt.BottomEdge | Qt.LeftEdge):
                return Qt.SizeBDiagCursor;
            case Qt.TopEdge:
                return Qt.SizeVerCursor;
            case Qt.BottomEdge:
                return Qt.SizeVerCursor;
            case Qt.LeftEdge:
                return Qt.SizeHorCursor;
            case Qt.RightEdge:
                return Qt.SizeHorCursor;
            default:
                return Qt.ArrowCursor;
            }
        }
        onPressed: root.startSystemResize(edgeFlag)
    }

    // ================================================================
    // 组件：文字按钮（模式/复位/保存/管理/取消共用）
    // ================================================================
    component EqTextButton: Button {
        id: btn

        // active = 选中态（实色底，用于 10/31 切换当前档）；labelColor 可覆写
        property bool active: false
        property color labelColor: Theme.textPrimary
        property int minWidth: 0

        implicitWidth: Math.max(minWidth, contentItem.implicitWidth + Theme.spacing12)
        implicitHeight: 28
        padding: 0
        hoverEnabled: true

        background: Rectangle {
            radius: Theme.radiusSmall
            color: !btn.enabled ? "transparent"
                 : btn.active ? (btn.down ? Theme.accentColor : Theme.accentColor)
                 : btn.down ? Theme.pressedColor
                 : btn.hovered ? Theme.hoverColor : Theme.baseColor
            border.width: btn.active ? 0 : 1
            border.color: btn.active ? "transparent"
                         : btn.hovered ? Theme.borderColor : Theme.borderSubtle

            Behavior on color {
                ColorAnimation { duration: Theme.animationFast }
            }
        }

        contentItem: Text {
            text: btn.text
            color: !btn.enabled ? Theme.textDisabled
                 : btn.active ? Theme.textOnAccent : btn.labelColor
            font.pixelSize: Theme.fontBody
            font.weight: btn.active ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ================================================================
    // 组件：开关（仿 SettingsWindow Switch 视觉；compact 缩小尺寸）
    // ================================================================
    component EqSwitch: Switch {
        id: sw
        property bool compact: false
        implicitWidth: sw.compact ? 40 : 48
        implicitHeight: sw.compact ? 24 : 32
        padding: 0

        indicator: Rectangle {
            id: swTrack
            implicitWidth: sw.compact ? 30 : 36
            implicitHeight: sw.compact ? 16 : 20
            radius: height / 2
            x: (sw.width - width) / 2
            y: (sw.height - height) / 2
            color: sw.checked ? Theme.accentColor
                 : (sw.hovered ? Theme.hoverColor : Theme.baseColor)
            border.color: sw.checked ? "transparent" : Theme.borderColor
            border.width: 1

            Behavior on color {
                ColorAnimation { duration: Theme.animationFast }
            }

            Rectangle {
                id: swKnob
                width: swTrack.height - 4
                height: swTrack.height - 4
                radius: width / 2
                y: (swTrack.height - height) / 2
                x: sw.checked ? swTrack.width - width - 2 : 2
                color: sw.checked ? Theme.textOnAccent : Theme.textSecondary

                Behavior on x {
                    NumberAnimation { duration: Theme.animationFast }
                }
                Behavior on color {
                    ColorAnimation { duration: Theme.animationFast }
                }
            }
        }
        contentItem: Item {
        }
    }
}
