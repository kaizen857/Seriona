#pragma once

#include "equalizer_curve_synth.h"
#include "spectrum_display_model.h"

#include <QColor>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QTimer>
#include <QVariantList>

#include <array>

class QHoverEvent;
class QMouseEvent;
class QPointF;
class QSGNode;

namespace Seriona::App {

// 频谱/频响自绘图谱区（任务 6 骨架 → 任务 8 渲染核心完成）。
// 注册为 QML_ELEMENT（类型名 SpectrumGraph，文件名 spectrum_graph.* 与类名对应）；
// 窗口接线（EqualizerWindow 图谱区替换 Canvas）属任务 10，本类不依赖窗口层。
//
// 数据语义（与 settings 镜像面同源，T10 绑定 settings.spectrumBins/bandGains*/…）：
//  - spectrumBins：120 桶 dBFS（0dBFS 满刻度，-120 静音地板；长度由 SettingsController
//    镜像门 kEqSpectrumBinCount 保证，本 item 按长度自适应绘制，短/空列表按静音补全）；
//  - 显示曲线（R5）：本地 PCHIP 包络（手柄点平滑插值，恒过手柄），由 bandMode +
//    活动档 bandGains 合成——见 synthesizeGraphicEnvelopeCurve 与 rebuildEnvelopeCurve。
//    R5 前曲线 = 后端物理响应镜像（curvePoints/curveFrequencies 181 点，RBJ 逐点叠加）；
//    R5 调研结论：消费级 GEQ/播放器 UI 曲线 = 手柄点目标包络，物理叠加会「峰高于手柄」
//    （相邻高增益带裙边叠加），与编辑语义冲突——故显示与 DSP 解耦：曲线随增益即时本地
//    更新，音频仍为后端真实 RBJ。后端镜像曲线仍经 settings.curvePoints 订阅到达
//    （后端契约面保留），本类不再消费其绘制。
//
// 渲染与推进架构（T8 决策，单线程无锁依据：Qt Quick 官方约定——GUI 线程写
// item 状态（属性 setter/16ms 推进定时器），渲染线程在帧同步期于
// updatePaintNode() 读取状态构建几何，两线程以渲染循环帧边界同步；与官方
// scenegraph-customgeometry 示例同一模式）：
//  - 数据到达（setSpectrumBins，GUI 线程）→ 转 double 定长数组 → 喂入
//    SpectrumDisplayModel（attack 即时到位）；
//  - 墙钟推进：QTimer（16ms，仅未收敛期间运行）内量取 QElapsedTimer 帧间隔
//    dt → model.advanceBy(dt)（柱 τ=135ms / 峰值 τ=900ms 单指数回落）→ update()；
//    全部通道 |显示−目标| < 1e-3 时停表（收敛停；空数据/全 0 立即收敛，无空转）；
//  - updatePaintNode() 纯几何：零 QML 引擎调用、零每帧堆分配——QSG 节点/几何/
//    材质按固定顶点容量一次性创建（曲线存在性切换时增删），每帧只写顶点再
//    markVertexDataDirty；顶点色为 ColoredPoint2D uchar 0-255（T6 spike 坑：勿传 0..1）。
//
// 几何单一映射源（Q_INVOKABLE，QML 静态刻度/手柄定位与 C++ 绘制共用；y 域两套
// 映射同处一个全高线性空间，见调研文档 §6.1）：
//  - freqToX(freqHz)：对数频率轴 20..20000 → item 本地 x；
//  - dbToY(gainDb)：EQ 增益 ±15dB 全高（+15 → 顶 y=0，-15 → 底 y=height，0dB 恒图半高）——
//    曲线与 band 编辑点层（T9 手柄 y == dbToY(gain) 断言锚定）；
//  - dbFsToY(dbFs)：频谱 dBFS -60..0 全高（0dBFS → 顶 y=0，-60 → 底 y=height）——
//    频谱刻度标签 0/-20/-40/-60 与每 20dB 网格线（T10 覆盖层）及柱高共用。
//    （T8 命名收敛裁决：任务 6 字面三件套中 dbToX 无几何 x 语义，T8 起更名
//    dbFsToY 与模型 spectrumDbToFraction 命名同族；仓库内无既有调用方，T10 覆盖层
//    按本名接线——已记 learnings 供 T10。）
//  - spectrumBarsVisible（T10 最小属性扩展，QML 接线 settings.spectrumEnabled）：
//    频谱柱区（柱 + 峰值线）显隐门——只影响柱/峰几何，曲线与 band 手柄恒显。
//    隐藏时 updatePaintNode 仍写零面积退化几何（顶点数/节点组合签名不变，无整树
//    重建、无每帧分配；开关抖动不触发节点树变化）。隐藏不等于停数据：镜像 bins
//    继续喂模型，重新可见时从当前平滑态续画（无跳变）。
// 轴域/增益域常量单一源：spectrum_display_model.h（其再派生 equalizer_presets.h）；
// 本类不重定义任何轴常量，Q_INVOKABLE 全部委托模型 fraction 函数 × item 宽/高。
// ISO band 频点/Q（手柄 x 定位）单一源：equalizer_curve_synth.h（同源声明文件）。

// ============================================================================
// Band 编辑手柄面（任务 9，本类第二功能块；无 QML 文本——浮层数值文本由 T10
// QML 覆盖层呈现，本类只提供拖动期间只读属性/信号数据面）：
//  - 手柄 = 当前 bandMode 档的 band 中心频点 × 当前增益（y == dbToY(gain) 全高）；
//    直径 12px 三角扇圆盘，hover 放大(半径 8px)/提亮；命中容差 ±8px（最近手柄）。
//  - 交互（仅鼠标左键，无键盘/触摸/点击柱编辑）：
//      按下命中 → 拖动态（y 实时跟随光标）→ 钳 ±15dB、0.1dB 网格吸附 → 释放发
//      dragReleased(bandMode, bandIndex, gainDb)（值 = 吸附后值）并把值落入内部
//      镜像（手柄即刻停在释放位，不依赖 settings 回环）。bandMode 切换 / 整表
//      外部写回（setBandGains10/31）中止拖动并重建手柄集（无释放信号）。
//  - 拖动数据面（T10 浮层绑定）：只读 dragBandIndex（-1 = 无拖动）与 dragGainDb
//    （吸附后值，拖动中实时更新；NOTIFY dragInfoChanged）。几何读取面
//    handleCount/handleCenterX/handleCenterY 供断言与静态定位（映射委托本类
//    freqToX/dbToY 单一源——y==dbToY(gain) 永不漂移，QA 探针 ±1px 断言锚）。
//  - ★ 写回键同源声明（review 核对项）：拖动释放信号携带 (bandMode, bandIndex,
//    gainDb)，消费端（T10 QML 胶水）应写回设置键 bandGains10/bandGains31——
//    键名与 GEQ 竖条区（EqBandSlider 的 root.settings[key] = list 写回，见
//    qml/windows/EqualizerWindow.qml:721-734 与锁测试 tst_ui_only_handler_policy）
//    同键同语义（bandMode=10 → "bandGains10"，31 → "bandGains31"）；本类不写
//    settings（app 级写回胶水归 T10/T11，本任务不承诺）。
//  - 显示曲线 = 本地 PCHIP 包络（R5，见类头数据语义节）：任何增益/档位/拖动值
//    变化即重建 181 点包络（rebuildEnvelopeCurve；拖动态以吸附值替换拖动 band）——
//    曲线恒过手柄、无过冲、即时更新（无镜像仲裁/pending；释放落值后直接重建）。
//  - 手柄节点追加在曲线节点之后（尾区）；节点树组合（柱/峰 固定 + 曲线 0/3 +
//    手柄 0/n）变化时整树重建（仅组合变化帧分配），每帧仅写顶点。
// ============================================================================
//
// 颜色/尺寸注入属性：默认值取 Theme.accentColor（#5B9DFF）同族；QML 层经 Theme 值
// 注入覆盖（T10 接线），C++ 不自行读取 Theme。
class SpectrumGraph : public QQuickItem
{
    Q_OBJECT
    // 数据属性（订阅镜像只读面；变更触发重绘）
    Q_PROPERTY(QVariantList spectrumBins READ spectrumBins WRITE setSpectrumBins NOTIFY spectrumBinsChanged)
    // 频谱桶数（固定 120，镜像门同源常量）
    Q_PROPERTY(int binCount READ binCount CONSTANT)
    // 颜色注入（QML 层 Theme 值；柱顶亮/底暗渐变 + 峰值线 + 曲线）
    Q_PROPERTY(QColor barTopColor READ barTopColor WRITE setBarTopColor NOTIFY barTopColorChanged)
    Q_PROPERTY(QColor barBottomColor READ barBottomColor WRITE setBarBottomColor NOTIFY barBottomColorChanged)
    Q_PROPERTY(QColor peakLineColor READ peakLineColor WRITE setPeakLineColor NOTIFY peakLineColorChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY curveColorChanged)
    // band 编辑面（T9；值面 = settings 镜像只读绑定输入，T10 接线；本类不写 settings）
    Q_PROPERTY(int bandMode READ bandMode WRITE setBandMode NOTIFY bandModeChanged)
    Q_PROPERTY(QVariantList bandGains10 READ bandGains10 WRITE setBandGains10 NOTIFY bandGains10Changed)
    Q_PROPERTY(QVariantList bandGains31 READ bandGains31 WRITE setBandGains31 NOTIFY bandGains31Changed)
    Q_PROPERTY(double preGainDb READ preGainDb WRITE setPreGainDb NOTIFY preGainDbChanged)
    // 手柄颜色注入（默认 accent 同族亮色；hover 提亮色）
    Q_PROPERTY(QColor handleColor READ handleColor WRITE setHandleColor NOTIFY handleColorChanged)
    Q_PROPERTY(QColor handleHoverColor READ handleHoverColor WRITE setHandleHoverColor NOTIFY handleHoverColorChanged)
    // 频谱柱区显隐门（T10 最小属性扩展，见类头注释；默认 true = 全量显示）
    Q_PROPERTY(bool spectrumBarsVisible READ spectrumBarsVisible WRITE setSpectrumBarsVisible NOTIFY spectrumBarsVisibleChanged)
    // 只读拖动信息面（T10 浮层文本绑定；拖动态以外恒 -1/0）
    Q_PROPERTY(int dragBandIndex READ dragBandIndex NOTIFY dragInfoChanged)
    Q_PROPERTY(double dragGainDb READ dragGainDb NOTIFY dragInfoChanged)
    // 尺寸注入（QML 层可调；柱宽比例沿用旧 Canvas 0.72，峰值线高 T8 规格 2px）
    Q_PROPERTY(qreal barWidthRatio READ barWidthRatio WRITE setBarWidthRatio NOTIFY barWidthRatioChanged)
    Q_PROPERTY(qreal peakLineHeight READ peakLineHeight WRITE setPeakLineHeight NOTIFY peakLineHeightChanged)
    QML_ELEMENT
public:
    explicit SpectrumGraph(QQuickItem *parent = nullptr);

    QVariantList spectrumBins() const { return m_spectrumBins; }
    void setSpectrumBins(const QVariantList &bins);
    // binCount 与模型同源（= equalizer_presets.h kEqSpectrumBinCount）
    int binCount() const { return SpectrumDisplayModel::kBinCount; }

    QColor barTopColor() const { return m_barTopColor; }
    void setBarTopColor(const QColor &color);
    QColor barBottomColor() const { return m_barBottomColor; }
    void setBarBottomColor(const QColor &color);
    QColor peakLineColor() const { return m_peakLineColor; }
    void setPeakLineColor(const QColor &color);
    QColor curveColor() const { return m_curveColor; }
    void setCurveColor(const QColor &color);

    // —— band 编辑面（T9）——
    int bandMode() const { return m_bandMode; }
    void setBandMode(int mode);
    QVariantList bandGains10() const { return m_bandGains10; }
    void setBandGains10(const QVariantList &gains);
    QVariantList bandGains31() const { return m_bandGains31; }
    void setBandGains31(const QVariantList &gains);
    double preGainDb() const { return m_preGainDb; }
    void setPreGainDb(double gainDb);
    QColor handleColor() const { return m_handleColor; }
    void setHandleColor(const QColor &color);
    QColor handleHoverColor() const { return m_handleHoverColor; }
    void setHandleHoverColor(const QColor &color);
    bool spectrumBarsVisible() const { return m_spectrumBarsVisible; }
    void setSpectrumBarsVisible(bool visible);
    int dragBandIndex() const { return m_dragBandIndex; }
    double dragGainDb() const { return m_dragGainDb; }

    qreal barWidthRatio() const { return m_barWidthRatio; }
    void setBarWidthRatio(qreal ratio);
    qreal peakLineHeight() const { return m_peakLineHeight; }
    void setPeakLineHeight(qreal height);

    // —— 几何映射单一源（QML 刻度/手柄定位与 C++ 绘制永不漂移；item 全宽/全高坐标系）——
    // 全部委托 SpectrumDisplayModel 纯函数 fraction × item 几何，域外钳制（含 NaN）。
    // 对数频率 → x：freqToX(20)=0、freqToX(20000)=width()。
    Q_INVOKABLE qreal freqToX(qreal freqHz) const;
    // EQ 增益 ±15dB → y（+15 → 顶 0，-15 → 底 height()，0 → 半高）。
    Q_INVOKABLE qreal dbToY(qreal gainDb) const;
    // 频谱 dBFS -60..0 → y（0dBFS → 顶 0，-60 及以下 → 底 height()）——柱高/刻度共用。
    Q_INVOKABLE qreal dbFsToY(qreal dbFs) const;
    // —— band 手柄数据面（T9；映射单一源 = 绘制同函数）——
    // 当前 bandMode 档可见手柄数（档增益列表非空才显示；10/31）。
    Q_INVOKABLE int handleCount() const;
    // band 中心频点 x（freqToX(eqIsoBandCenterHz)；index 域外返回 -1）。
    Q_INVOKABLE qreal handleCenterX(int bandIndex) const;
    // 手柄当前显示 y：拖动中 = dbToY(拖动吸附值)，否则 = dbToY(内部镜像增益)；
    // index 域外返回 -1。断言锚：手柄 y == dbToY(gain) ±1px。
    Q_INVOKABLE qreal handleCenterY(int bandIndex) const;

signals:
    void spectrumBinsChanged();
    void barTopColorChanged();
    void barBottomColorChanged();
    void peakLineColorChanged();
    void curveColorChanged();
    void barWidthRatioChanged();
    void peakLineHeightChanged();
    // —— band 编辑面（T9）——
    void bandModeChanged();
    void bandGains10Changed();
    void bandGains31Changed();
    void preGainDbChanged();
    void handleColorChanged();
    void handleHoverColorChanged();
    void spectrumBarsVisibleChanged();
    // 拖动信息（dragBandIndex/dragGainDb）任一变化；释放后 index 回 -1。
    void dragInfoChanged();
    // 拖动释放（bandMode/bandIndex 均指释放时的档位与序号；gainDb = 吸附后值）。
    // 消费端（T10 QML 胶水）以此写回 settings bandGains10/31 同键（见类头同源声明）。
    void dragReleased(int bandMode, int bandIndex, double gainDb);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    // 交互（T9）：仅鼠标左键；无键盘/触摸/点击柱编辑
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    // 数据镜像：QVariantList（仅变更时转换）+ 定长 double 数组（绘制消费）
    void convertSpectrumBinsLocked();
    // band 增益镜像（QVariantList → 定长数组，钳 ±15、非有限 → 0）
    void convertBandGains(int mode);
    void convertBandGainsList(const QVariantList &list, std::span<double> dst);

    // 推进/收敛（GUI 线程）：墙钟 dt 推进模型；未收敛续帧，收敛停
    void syncAnimTimer();
    bool isConverged() const;
    void onAnimTick();

    // —— band 编辑面（T9）——
    // 活动档增益数组（内部镜像：设置键写回由 T10 胶水负责，本类只读镜像 +
    // 释放落值）；活动档为空（列表长度 0）→ 手柄不显示。
    const std::array<double, kEqBandCount31> &activeGains() const;
    // 活动档有效手柄数（镜像列表长度 ∩ 档位上限；0 = 无手柄）
    int activeGainCount() const;
    // 命中测试：局部坐标 → 手柄序号（距中心 ≤ 8px 取最近；无命中 -1）
    int handleHitTest(const QPointF &localPos) const;
    void setHoverIndex(int index); // hover 变化才重绘
    void beginDrag(int bandIndex, const QPointF &localPos); // 命中起始拖动态 + 包络起点
    void updateDragValue(const QPointF &localPos); // 拖动中：钳 ±15 + 0.1 吸附 + 包络重建
    void endDrag(const QPointF &localPos, bool commit); // 释放提交 or 中止（ungrab/外部干预）
    void cancelDrag(); // 中止：清拖动态并回落包络（无释放信号）
    void rebuildEnvelopeCurve(); // 按活动档增益（拖动态含吸附值）合成 181 点包络
    void notifyDragInfo();
    void updateCursorShape();
    // 活动档内部镜像存储值（index 域外按 0）
    double storedBandGain(int mode, int bandIndex) const;
    // 曲线显示源 = 本地包络通道（m_curveFreqs/Dbs/Count；<2 点视为无曲线）
    void resolveCurveSource(const double *&freqs, const double *&dbs, int &count) const;
    // 上一帧绘制布局签名（曲线点数/可见性/手柄数；与当前不符 → 整树重建）。
    // updatePaintNode 于帧同步期读写（GUI 线程阻塞中），无跨线程竞争。
    int m_paintCurveN = -1;
    int m_paintHandleN = -1;

    QVariantList m_spectrumBins;

    // 定长镜像（构造期一次性定长，无每帧分配）
    std::array<double, SpectrumDisplayModel::kBinCount> m_binsDb{};
    // 收敛判据影子目标：与喂入模型同一转换步用同一纯函数（spectrumDbToFraction）
    // 计算，仅作推进循环停表判据（模型 target 内部量不可直读；同源计算无漂移）
    std::array<double, SpectrumDisplayModel::kBinCount> m_shadowTarget{};
    // 显示曲线通道（R5 包络；频率轴 = 合成轴 20..20k 对数，同公式）
    std::array<double, kEqCurvePointCount> m_curveFreqs{};
    std::array<double, kEqCurvePointCount> m_curveDbs{};
    int m_curveCount = 0; // 包络有效点数（0/181；<2 视为无曲线，跳过曲线几何）

    // band 编辑面状态（T9）
    QVariantList m_bandGains10;
    QVariantList m_bandGains31;
    // 双档均按 31 容量（10 档用前缀 10 项；活动档返回统一类型引用）
    std::array<double, kEqBandCount31> m_bandGains10Db{};
    std::array<double, kEqBandCount31> m_bandGains31Db{};
    int m_bandGains10Count = 0; // >0 = 该档有镜像数据（手柄显示门）
    int m_bandGains31Count = 0;
    int m_bandMode = kEqBandMode10;
    double m_preGainDb = 0.0;
    QColor m_handleColor{0x8F, 0xBF, 0xFF}; // 手柄（accent 亮化）
    QColor m_handleHoverColor{0xE2, 0xEE, 0xFF}; // hover 提亮（近白）
    bool m_spectrumBarsVisible = true; // 频谱柱区显隐门（T10；曲线/手柄不受影响）
    int m_hoverBandIndex = -1; // hover 手柄（无 = -1）
    int m_dragBandIndex = -1; // 拖动手柄（无 = -1）
    double m_dragGainDb = 0.0; // 拖动吸附值（释放信号值源）

    // 显示平滑模型（成员组合；T8 消费 T7 交付，常量/映射以模型为单一源）
    SpectrumDisplayModel m_displayModel;
    QTimer m_animTimer;
    QElapsedTimer m_animClock;

    QColor m_barTopColor{0x7F, 0xB2, 0xFF}; // 柱渐变顶（亮）：Theme.accentColor 同族亮化，QML 层注入覆盖
    QColor m_barBottomColor{0x1F, 0x3B, 0x73}; // 柱渐变底（暗）
    QColor m_peakLineColor{0xC9, 0xDE, 0xFF}; // 峰值保持线（更亮）
    QColor m_curveColor{0x5B, 0x9D, 0xFF}; // 181 点曲线 = Theme.accentColor
    qreal m_barWidthRatio = 0.72; // 柱宽占格宽比例（旧 Canvas (plotW/n)*0.72 自适应语义）
    qreal m_peakLineHeight = 2.0; // 峰值线厚度 px（T8 规格）
};

} // namespace Seriona::App
