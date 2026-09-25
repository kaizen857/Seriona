import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Seriona

// 整首纠错窗口（W3/D23）：列出当前曲目的全部行，逐行可视化「原文/译文分界」；
// 选中一行后拖动分界（像素↔字符换算全部经 TextMetrics，不硬编码字宽），
// 保存时经 appFacade.commitLyricSplitBoundary 外发控制命令（内容寻址写 manual，
// 不是存字符偏移），后端随后按时机③重发布快照。
//
// 打开时【定格】行集合：openEditor() 把 lyrics.lines() 一次性快照进 frozenModel，
// 之后曲库/歌词变化不再改动窗口内容（关闭重开才刷新）。
// 不做逐字级高亮、不做逐字时间轴；不在窗口内做语言判定（只呈现与提交）；不直连 DB/FS。
Window {
    id: root
    objectName: "lyricSplitEditorWindow"

    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    transientParent: null
    width: 720
    height: 560

    property var appFacade: null
    property int selectedIndex: -1

    readonly property int lineFontPixelSize: 18

    // 分界像素定位的唯一度量源。假设（记录于证据 observability-choice.md）：
    // 行文本左对齐、单行（NoWrap）渲染；分界 x 换算只用本 TextMetrics.advanceWidth，
    // 不用平均字宽近似。elide 只影响超过行宽的显示，不影响度量。
    TextMetrics {
        id: boundaryMetrics
        font.pixelSize: root.lineFontPixelSize
    }

    ListModel {
        id: frozenModel
        objectName: "lyricSplitFrozenModel"
    }

    function openEditor() {
        root.selectedIndex = -1;
        frozenModel.clear();
        var rows = (root.appFacade && root.appFacade.lyrics) ? root.appFacade.lyrics.lines() : [];
        for (var i = 0; i < rows.length; ++i) {
            var row = rows[i];
            var raw = (row.rawLine !== undefined && row.rawLine !== null) ? String(row.rawLine) : "";
            var shown = (row.displayLine !== undefined && row.displayLine !== null) ? String(row.displayLine) : "";
            var trans = (row.translation !== undefined && row.translation !== null) ? String(row.translation) : "";
            var cut = root.initialCut(raw, shown, trans);
            frozenModel.append({
                rawLine: raw,
                displayLine: shown,
                translation: trans,
                timestampSec: (row.timestampSec !== undefined && row.timestampSec !== null) ? row.timestampSec : 0,
                manualOverride: !!row.manualOverride,
                reproducible: cut.valid,
                boundaryIndex: cut.valid ? cut.leftEnd : 0,
                touched: false
            });
        }
        show();
    }

    // 初始分界的唯一来源：由 C++ 反解「能复现当前展示对」的分界（不含任何分隔符字形）。
    // valid=false 表示该行当前展示对无法由分界表达 → 该行不可保存（保存按钮置灰）。
    function initialCut(raw, shown, trans) {
        if (!root.appFacade)
            return { valid: false, leftEnd: 0, rightStart: 0 };
        return root.appFacade.lyricSplitBoundaryCut(raw, shown, trans);
    }

    function previewFor(raw, boundaryIndex) {
        if (!root.appFacade)
            return { valid: false, original: "", translation: "" };
        return root.appFacade.lyricSplitBoundaryParts(raw, boundaryIndex);
    }

    readonly property var selectedRow: (root.selectedIndex >= 0 && root.selectedIndex < frozenModel.count)
                                       ? frozenModel.get(root.selectedIndex) : null

    // 保存按钮是否可用 == 「真的拖动过」且「拖动结果可提交」。
    // 关键（A4 修复）：可提交性判定针对**拖动结果**（按 row.boundaryIndex 换算出的两段），
    // 不是打开时那个展示对——后者对「译文来自其它行的参照行」必然不可复现，
    // 用它当门会让整类行即使拖动也永远无法保存。判定与 C++ 门同源（同一 Q_INVOKABLE）。
    function commitAllowed(row) {
        if (!row || !root.appFacade)
            return false;
        return root.appFacade.lyricSplitBoundaryCommitAllowed(row.rawLine, row.boundaryIndex,
                                                             row.displayLine, row.translation);
    }

    readonly property bool canSaveSelected: !!selectedRow && selectedRow.touched === true
                                            && root.commitAllowed(selectedRow)

    function prefixWidth(raw, boundaryIndex) {
        boundaryMetrics.text = raw.substring(0, Math.max(0, Math.min(boundaryIndex, raw.length)));
        return boundaryMetrics.advanceWidth;
    }

    // 像素 x → 字符索引：最大的 i 使 prefixWidth(raw, i) <= x（二分，O(log n) 次度量）。
    function indexAtX(raw, x) {
        if (raw.length === 0)
            return 0;
        var lo = 0, hi = raw.length;
        while (lo < hi) {
            var mid = Math.floor((lo + hi + 1) / 2);
            if (prefixWidth(raw, mid) <= x)
                lo = mid;
            else
                hi = mid - 1;
        }
        return lo;
    }

    function saveSelected() {
        if (root.selectedIndex < 0 || root.selectedIndex >= frozenModel.count)
            return false;
        var row = frozenModel.get(root.selectedIndex);
        if (!root.appFacade)
            return false;
        if (row.touched !== true)
            return false;
        if (!root.commitAllowed(row))
            return false;
        return root.appFacade.commitLyricSplitBoundary(row.rawLine, row.boundaryIndex,
                                                       row.displayLine, row.translation);
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.raisedSurfaceColor
        radius: Theme.radiusLarge
        border.color: Theme.borderColor
        border.width: 1
        focus: true

        Keys.onEscapePressed: root.close()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing16
            spacing: Theme.spacing8

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("整首纠错")
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                }

                Rectangle {
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 30
                    radius: Theme.radiusSmall
                    color: closeArea.pressed ? Theme.pressedColor : (closeArea.containsMouse ? Theme.hoverColor : "transparent")
                    border.color: Theme.borderSubtle
                    border.width: 1
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("关闭")

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("关闭")
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("选中一行后拖动蓝色分界，保存后写入手工纠错（分界位置基于文本度量）")
                color: Theme.secondaryTextColor
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: frozenModel.count === 0
                text: qsTr("当前曲目没有可纠错的歌词行")
                color: Theme.secondaryTextColor
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            ListView {
                id: lineList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: frozenModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: StyledScrollBar {}

                delegate: Rectangle {
                    id: rowRect
                    required property int index
                    required property string rawLine
                    required property string displayLine
                    required property string translation
                    required property bool manualOverride
                    required property bool reproducible
                    required property int boundaryIndex
                    required property bool touched

                    readonly property bool isSelected: root.selectedIndex === index
                    // 未拖动时预览当前展示对（保存被 touched 挡住）；拖动后预览按分界换算出的两段，
                    // 所见即所提交。
                    readonly property var previewParts: touched
                        ? root.previewFor(rawLine, boundaryIndex)
                        : ({ valid: reproducible, original: displayLine, translation: translation })

                    // 分界像素宽在【绑定求值之外】算好再交给句柄的 x 读取：
                    // prefixWidth() 会写 boundaryMetrics.text，若直接写在 x 的绑定里，
                    // 就成了「求值期写被读取的属性」⇒ 自引用绑定环（实测拖动时 4 条
                    // Binding loop detected for property "x"）。这里显式同步，x 只读结果。
                    property real boundaryPrefixWidth: 0
                    function syncBoundaryPrefixWidth() {
                        boundaryPrefixWidth = root.prefixWidth(rawLine, boundaryIndex);
                    }
                    onBoundaryIndexChanged: syncBoundaryPrefixWidth()
                    onRawLineChanged: syncBoundaryPrefixWidth()
                    Component.onCompleted: syncBoundaryPrefixWidth()

                    width: lineList.width
                    height: rowColumn.implicitHeight + Theme.spacing8 * 2
                    radius: Theme.radiusSmall
                    color: isSelected ? Theme.hoverColor : "transparent"

                    // 命中整行的选择区：声明在 Column 之前 ⇒ 位于其下；未选中行的
                    // dragArea 处于 disabled，按下会落到这里完成「点选本行」。
                    MouseArea {
                        objectName: "lyricSplitRowSelectArea"
                        anchors.fill: parent
                        onClicked: root.selectedIndex = rowRect.index
                    }

                    Column {
                        id: rowColumn
                        x: Theme.spacing12
                        y: Theme.spacing8
                        width: parent.width - Theme.spacing24
                        spacing: Theme.spacing4

                        Item {
                            width: parent.width
                            height: rawTextFull.height + 4

                            Text {
                                id: rawTextFull
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.topMargin: 2
                                width: parent.width
                                text: rowRect.rawLine
                                color: Theme.textPrimary
                                font.pixelSize: root.lineFontPixelSize
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }

                            Text {
                                anchors.left: rawTextFull.left
                                anchors.top: rawTextFull.top
                                width: rawTextFull.width
                                visible: rowRect.isSelected
                                text: rowRect.isSelected ? rowRect.rawLine.substring(0, rowRect.boundaryIndex) : ""
                                color: Theme.accentColor
                                font: rawTextFull.font
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                id: boundaryHandle
                                visible: rowRect.isSelected
                                x: rowRect.isSelected ? rawTextFull.x + rowRect.boundaryPrefixWidth : 0
                                y: rawTextFull.y - 2
                                width: 2
                                height: rawTextFull.height + 4
                                color: Theme.accentColor

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.top: parent.top
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: Theme.accentColor
                                }
                            }

                            // 拖动分界（H1）：与 rawTextFull 同父（兄弟）⇒ 四边锚定合法、几何非零；
                            // 且 mouse.x 与 rawTextFull 同坐标系，prefixWidth/indexAtX 语义不变。
                            // 仅对选中行启用；按下的点须落在当前分界附近才开始拖动，否则视为普通点击。
                            MouseArea {
                                id: dragArea
                                objectName: "lyricSplitDragArea"
                                enabled: rowRect.isSelected
                                anchors.fill: rawTextFull
                                cursorShape: Qt.SplitHCursor
                                preventStealing: true

                                property bool dragging: false

                                onPressed: function (mouse) {
                                    dragging = Math.abs(mouse.x - root.prefixWidth(rowRect.rawLine, rowRect.boundaryIndex)) <= 16;
                                }
                                onPositionChanged: function (mouse) {
                                    if (!dragging)
                                        return;
                                    var nextIndex = root.indexAtX(rowRect.rawLine, mouse.x);
                                    if (nextIndex !== rowRect.boundaryIndex) {
                                        frozenModel.setProperty(rowRect.index, "boundaryIndex", nextIndex);
                                        // 真正改变过分界才置 touched：未拖动 = 结构上不可保存。
                                        frozenModel.setProperty(rowRect.index, "touched", true);
                                    }
                                }
                                onReleased: dragging = false
                                onCanceled: dragging = false
                            }
                        }

                        Text {
                            width: parent.width
                            visible: rowRect.isSelected
                            text: qsTr("原文：%1    译文：%2").arg(rowRect.previewParts.original).arg(rowRect.previewParts.translation)
                            color: rowRect.previewParts.valid ? Theme.secondaryTextColor : Theme.warningColor
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing8

                Item { Layout.fillWidth: true }

                Rectangle {
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 34
                    radius: Theme.radiusSmall
                    color: root.canSaveSelected
                        ? (saveArea.pressed ? Qt.darker(Theme.accentColor, 1.2) : (saveArea.containsMouse ? Qt.lighter(Theme.accentColor, 1.2) : Theme.accentColor))
                        : Theme.borderSubtle
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("保存选中行分界")

                    MouseArea {
                        id: saveArea
                        anchors.fill: parent
                        enabled: root.canSaveSelected
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.saveSelected()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("保存选中行")
                        color: Theme.textOnAccent
                        font.pixelSize: Theme.fontBody
                    }
                }
            }
        }
    }
}
