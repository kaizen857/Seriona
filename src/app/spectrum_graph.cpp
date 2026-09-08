#include "spectrum_graph.h"

#include <QCursor>
#include <QDebug>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGNode>
#include <QSGVertexColorMaterial>
#include <QtMath>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

namespace Seriona::App {

namespace {

// —— 几何常量（顶点容量一次性分配；T8 零每帧堆分配）——
constexpr int kBarQuadVertices = 6; // 1 quad = 2 三角形（DrawTriangles）
constexpr int kRibbonSegmentVertices = 6; // 曲线折线段 quad（双 pass 各自独立）
constexpr int kTickIntervalMs = 16; // 推进帧率 ~60fps（仅未收敛期间运行）
constexpr double kConvergedTolerance = 1e-3; // |显示−目标| < 0.1% 判收敛（≈亚像素）
constexpr double kBinSilenceDb = -120.0; // 静音地板 dBFS（缺失/非法桶补此值，与后端同义）
constexpr int kCurveSoftAlpha = 90; // 下 pass 半透明层 alpha（255·0.35）
constexpr int kCurveFillAlpha = 22; // 曲线下淡填充 alpha（255·~0.09）
constexpr double kCurveSoftWidthPx = 3.0; // 下 pass 3px
constexpr double kCurveCoreWidthPx = 1.5; // 上 pass 1.5px 实色（伪 AA 芯线）

// —— band 手柄（T9）：三角扇圆盘 + hover 放大/提亮 + 命中容差 ——
constexpr double kHandleRadiusPx = 6.0; // 直径 12px 圆盘
constexpr double kHandleHoverRadiusPx = 8.0; // hover/拖动放大（半径 8px）
constexpr double kHandleHitTolerancePx = 8.0; // 命中容差 ±8px（距中心）
constexpr int kHandleFanSegments = 18; // 三角扇段数（24 段规格带内取 18；闭合环 → 顶点 20）
constexpr int kHandleFanVertices = kHandleFanSegments + 2; // 中心 + 环 N+1（末点=首点闭合）

// —— 顶点色 uchar 0-255（ColoredPoint2D 契约，T6 spike 坑：勿传 0..1 float）——
struct RgbaU8 {
    uchar r;
    uchar g;
    uchar b;
    uchar a;
};

// 注入 QColor → RGBA uchar（alpha 透传）
[[nodiscard]] inline RgbaU8 toRgbaU8(const QColor &color)
{
    return {static_cast<uchar>(color.red()), static_cast<uchar>(color.green()),
            static_cast<uchar>(color.blue()), static_cast<uchar>(color.alpha())};
}

// QSG 预乘混合实证（T8 探针）：QSGVertexColorMaterial 按 GL_ONE/ONE_MINUS_SRC_ALPHA
// 混合且 shader 不预乘 → 半透明层直通 RGB 会叠加增亮底层，必须预乘 pm = c·α/255。
[[nodiscard]] inline RgbaU8 withPremultipliedAlpha(const RgbaU8 &color, int alpha)
{
    const int a = qBound(0, color.a * alpha / 255, 255);
    return {static_cast<uchar>(color.r * a / 255), static_cast<uchar>(color.g * a / 255),
            static_cast<uchar>(color.b * a / 255), static_cast<uchar>(a)};
}

// 写入一根线段（P0→P1）的 ribbon quad（6 顶点两三角形）；halfWidth 法向偏移半宽。
inline void writeSegment(QSGGeometry::ColoredPoint2D *v, float x0, float y0, float x1,
                         float y1, float halfWidth, RgbaU8 color)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    float nx = 1.0f;
    float ny = 0.0f;
    if (len > 1e-6f) { // 退化（零长）段：水平法向兜底，仍产出零面积 quad，无 NaN
        nx = -dy / len;
        ny = dx / len;
    }
    const float ox = nx * halfWidth;
    const float oy = ny * halfWidth;
    const float ax = x0 - ox, ay = y0 - oy; // 段首 -n
    const float bx = x0 + ox, by = y0 + oy; // 段首 +n
    const float cx = x1 - ox, cy = y1 - oy; // 段尾 -n
    const float dx2 = x1 + ox, dy2 = y1 + oy; // 段尾 +n
    // 两三角形 (a,b,c)+(b,d,c)
    v[0].set(ax, ay, color.r, color.g, color.b, color.a);
    v[1].set(bx, by, color.r, color.g, color.b, color.a);
    v[2].set(cx, cy, color.r, color.g, color.b, color.a);
    v[3].set(bx, by, color.r, color.g, color.b, color.a);
    v[4].set(dx2, dy2, color.r, color.g, color.b, color.a);
    v[5].set(cx, cy, color.r, color.g, color.b, color.a);
}

// 写入一个闭合三角扇圆盘（DrawTriangleFan：中心 + 环 kSegments+1 点，末点=首点）。
inline void writeDiscFan(QSGGeometry::ColoredPoint2D *v, float cx, float cy, float radius,
                         RgbaU8 color)
{
    constexpr double kTwoPi = 6.28318530717958647692;
    v[0].set(cx, cy, color.r, color.g, color.b, color.a);
    for (int i = 0; i <= kHandleFanSegments; ++i) {
        const double angle = kTwoPi * static_cast<double>(i) / static_cast<double>(kHandleFanSegments);
        v[i + 1].set(cx + static_cast<float>(radius * std::cos(angle)),
                     cy + static_cast<float>(radius * std::sin(angle)),
                     color.r, color.g, color.b, color.a);
    }
}

// 以 drawMode/顶点数/材质创建（仅节点缺失或顶点容量变化时；容量固定后零分配）
QSGGeometryNode *ensureGeometryNode(QSGNode *root, int index, int vertexCount,
                                    QSGGeometry::DrawingMode drawMode)
{
    if (index < root->childCount()) {
        auto *node = static_cast<QSGGeometryNode *>(root->childAtIndex(index));
        if (node->geometry() && node->geometry()->vertexCount() == vertexCount
            && node->geometry()->drawingMode() == drawMode)
            return node;
        root->removeChildNode(node); // 容量/绘制模式变化（组合切换）→ 整体重建
        delete node;
    }
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(),
                                     vertexCount, 0);
    geometry->setDrawingMode(drawMode);
    node->setGeometry(geometry);
    node->setMaterial(new QSGVertexColorMaterial);
    root->appendChildNode(node);
    return node;
}

} // namespace

SpectrumGraph::SpectrumGraph(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setClip(true); // 曲线 ribbon 半宽/峰值线可越出 1-2px，clip 钳制在 item 内
    setAcceptedMouseButtons(Qt::LeftButton); // T9：band 手柄交互（仅鼠标左键）
    setAcceptHoverEvents(true);
    m_animTimer.setInterval(kTickIntervalMs);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, [this] { onAnimTick(); });
}

// —— 数据镜像：QVariantList 仅在变更时转换（setter 去重），转换 O(n) 一次 ——
// 长度自适应：缺失/非有限桶按 -120dB 静音地板补全，桶恒为 kBinCount 个喂入模型
// （模型对每个 index 重写目标，无上一帧残留；NaN/-inf 不可进入几何坐标）。
void SpectrumGraph::convertSpectrumBinsLocked()
{
    m_binsDb.fill(kBinSilenceDb);
    const int n = std::min(static_cast<int>(m_spectrumBins.size()),
                           static_cast<int>(m_binsDb.size()));
    for (int i = 0; i < n; ++i) {
        const QVariant &value = m_spectrumBins.at(i);
        if (!value.isValid())
            continue;
        const double db = value.toDouble();
        if (std::isfinite(db))
            m_binsDb[static_cast<std::size_t>(i)] = db;
        // NaN/±inf → 保留静音地板（模型 spectrumDbToFraction 亦 NaN 安全，双保险）
    }
    for (int i = 0; i < SpectrumDisplayModel::kBinCount; ++i)
        m_shadowTarget[static_cast<std::size_t>(i)] =
            SpectrumDisplayModel::spectrumDbToFraction(m_binsDb[static_cast<std::size_t>(i)]);
    m_displayModel.setBinsDb(m_binsDb);
}

void SpectrumGraph::setSpectrumBins(const QVariantList &bins)
{
    if (m_spectrumBins == bins)
        return;
    m_spectrumBins = bins;
    if (bins.isEmpty()) {
        // 空数据（频谱关闭/无源）→ 全通道归零态：零柱高正常渲染，无 NaN
        m_displayModel.reset();
        m_shadowTarget.fill(0.0);
    } else {
        bool allZero = true;
        const int n = bins.size();
        for (int i = 0; i < n && allZero; ++i) {
            const QVariant &value = bins.at(i);
            if (value.isValid() && value.toDouble() != 0.0)
                allZero = false;
        }
        if (n > 0 && allZero) {
            // 后端默认空快照为 120×0.0（非 -120 静音标记）：0.0dBFS 会画顶满，
            // 与 UI「暂无频谱数据」判据（hasSpectrumData 全零 → false）同语义归零。
            m_displayModel.reset();
            m_shadowTarget.fill(0.0);
        } else {
            convertSpectrumBinsLocked();
        }
    }
    emit spectrumBinsChanged();
    update();
    syncAnimTimer();
}

void SpectrumGraph::setBarTopColor(const QColor &color)
{
    if (m_barTopColor == color)
        return;
    m_barTopColor = color;
    emit barTopColorChanged();
    update();
}

void SpectrumGraph::setBarBottomColor(const QColor &color)
{
    if (m_barBottomColor == color)
        return;
    m_barBottomColor = color;
    emit barBottomColorChanged();
    update();
}

void SpectrumGraph::setPeakLineColor(const QColor &color)
{
    if (m_peakLineColor == color)
        return;
    m_peakLineColor = color;
    emit peakLineColorChanged();
    update();
}

void SpectrumGraph::setCurveColor(const QColor &color)
{
    if (m_curveColor == color)
        return;
    m_curveColor = color;
    emit curveColorChanged();
    update();
}

void SpectrumGraph::setBarWidthRatio(qreal ratio)
{
    const qreal clamped = qBound<qreal>(0.05, ratio, 1.0);
    if (qFuzzyCompare(m_barWidthRatio, clamped))
        return;
    m_barWidthRatio = clamped;
    emit barWidthRatioChanged();
    update();
}

void SpectrumGraph::setPeakLineHeight(qreal height)
{
    const qreal clamped = qMax<qreal>(0.5, height);
    if (qFuzzyCompare(m_peakLineHeight, clamped))
        return;
    m_peakLineHeight = clamped;
    emit peakLineHeightChanged();
    update();
}

// —— band 编辑面（T9）属性/镜像 ——
namespace {

// 数值卫生：非有限 → 0；钳 ±15（kMin/kMaxEqGainDb 单一源）
[[nodiscard]] double sanitizeGain(double gainDb)
{
    if (!std::isfinite(gainDb))
        return 0.0;
    return qBound(kMinEqGainDb, gainDb, kMaxEqGainDb);
}

// 0.1dB 网格吸附（钳位后取整——释放信号值 = 吸附后值）
[[nodiscard]] double snapGainToGrid(double gainDb)
{
    return std::round(sanitizeGain(gainDb) * 10.0) / 10.0;
}

} // namespace

void SpectrumGraph::convertBandGainsList(const QVariantList &list, std::span<double> dst)
{
    std::fill(dst.begin(), dst.end(), 0.0);
    const int n = std::min(static_cast<int>(list.size()), static_cast<int>(dst.size()));
    for (int i = 0; i < n; ++i) {
        const QVariant &value = list.at(i);
        if (!value.isValid())
            continue;
        const double gain = value.toDouble();
        if (std::isfinite(gain))
            dst[static_cast<std::size_t>(i)] = qBound(kMinEqGainDb, gain, kMaxEqGainDb);
    }
}

void SpectrumGraph::convertBandGains(int mode)
{
    if (mode == kEqBandMode31) {
        convertBandGainsList(m_bandGains31, m_bandGains31Db);
        m_bandGains31Count = std::min(static_cast<int>(m_bandGains31.size()),
                                      static_cast<int>(m_bandGains31Db.size()));
    } else {
        convertBandGainsList(m_bandGains10, m_bandGains10Db);
        m_bandGains10Count = std::min(static_cast<int>(m_bandGains10.size()),
                                      static_cast<int>(m_bandGains10Db.size()));
    }
}

void SpectrumGraph::setBandMode(int mode)
{
    const int clamped = (mode == kEqBandMode31) ? kEqBandMode31 : kEqBandMode10;
    if (m_bandMode == clamped)
        return;
    m_bandMode = clamped;
    // 档位切换 → 中止拖动并重建手柄集与包络（无释放信号；同整表写回纪律）
    if (m_dragBandIndex >= 0)
        cancelDrag();
    else
        rebuildEnvelopeCurve();
    setHoverIndex(-1);
    emit bandModeChanged();
    update();
}

void SpectrumGraph::setBandGains10(const QVariantList &gains)
{
    if (m_bandGains10 == gains)
        return;
    // 拖动自回环识别（R5 实时联动）：拖动态中 QML 胶水每帧把拖动值写回 settings，
// 经镜像 NOTIFY 回到本 setter——若外部表与内部镜像仅差拖动带（且该带 == 拖动
// 吸附值），视为本组件拖动的回声而非外部干预：只同步镜像与列表，不中止拖动、
// 不重建包络（否则拖动会自断）。
    if (m_bandMode == kEqBandMode10 && m_dragBandIndex >= 0
        && m_dragBandIndex < gains.size()) {
        bool dragEcho = true;
        for (int i = 0; i < gains.size(); ++i) {
            if (i == m_dragBandIndex)
                continue;
            if (i < m_bandGains10.size()) {
                const double cur = m_bandGains10.at(i).toDouble();
                const double incoming = gains.at(i).toDouble();
                if (std::abs(cur - incoming) > 1e-9) {
                    dragEcho = false;
                    break;
                }
            } else {
                dragEcho = false;
                break;
            }
        }
        if (dragEcho && std::abs(gains.at(m_dragBandIndex).toDouble() - m_dragGainDb) <= 1e-9) {
            m_bandGains10 = gains;
            convertBandGains(kEqBandMode10);
            emit bandGains10Changed();
            update();
            return;
        }
    }
    m_bandGains10 = gains;
    convertBandGains(kEqBandMode10);
    if (m_bandMode == kEqBandMode10) {
        // 活动档整表外部写回 → 中止拖动（settings 回环/预设应用视作对拖动的干预）
        if (m_dragBandIndex >= 0)
            cancelDrag();
        else
            rebuildEnvelopeCurve();
        setHoverIndex(-1);
    }
    emit bandGains10Changed();
    update();
}

void SpectrumGraph::setBandGains31(const QVariantList &gains)
{
    if (m_bandGains31 == gains)
        return;
    // 拖动自回环识别：同 setBandGains10（R5 实时联动，见上）。
    if (m_bandMode == kEqBandMode31 && m_dragBandIndex >= 0
        && m_dragBandIndex < gains.size()) {
        bool dragEcho = true;
        for (int i = 0; i < gains.size(); ++i) {
            if (i == m_dragBandIndex)
                continue;
            if (i < m_bandGains31.size()) {
                const double cur = m_bandGains31.at(i).toDouble();
                const double incoming = gains.at(i).toDouble();
                if (std::abs(cur - incoming) > 1e-9) {
                    dragEcho = false;
                    break;
                }
            } else {
                dragEcho = false;
                break;
            }
        }
        if (dragEcho && std::abs(gains.at(m_dragBandIndex).toDouble() - m_dragGainDb) <= 1e-9) {
            m_bandGains31 = gains;
            convertBandGains(kEqBandMode31);
            emit bandGains31Changed();
            update();
            return;
        }
    }
    m_bandGains31 = gains;
    convertBandGains(kEqBandMode31);
    if (m_bandMode == kEqBandMode31) {
        if (m_dragBandIndex >= 0)
            cancelDrag();
        else
            rebuildEnvelopeCurve();
        setHoverIndex(-1);
    }
    emit bandGains31Changed();
    update();
}

void SpectrumGraph::setPreGainDb(double gainDb)
{
    const double sanitized = sanitizeGain(gainDb);
    if (qFuzzyCompare(m_preGainDb, sanitized))
        return;
    m_preGainDb = sanitized;
    // preGain 不进入编辑包络（R5：曲线 = 手柄点目标包络，音频仍含前置增益）→ 无需重合成
    emit preGainDbChanged();
    update();
}

void SpectrumGraph::setHandleColor(const QColor &color)
{
    if (m_handleColor == color)
        return;
    m_handleColor = color;
    emit handleColorChanged();
    update();
}

void SpectrumGraph::setHandleHoverColor(const QColor &color)
{
    if (m_handleHoverColor == color)
        return;
    m_handleHoverColor = color;
    emit handleHoverColorChanged();
    update();
}

void SpectrumGraph::setSpectrumBarsVisible(bool visible)
{
    if (m_spectrumBarsVisible == visible)
        return;
    m_spectrumBarsVisible = visible;
    emit spectrumBarsVisibleChanged();
    update();
}

const std::array<double, kEqBandCount31> &SpectrumGraph::activeGains() const
{
    return (m_bandMode == kEqBandMode31) ? m_bandGains31Db : m_bandGains10Db;
}

int SpectrumGraph::activeGainCount() const
{
    return (m_bandMode == kEqBandMode31) ? m_bandGains31Count : m_bandGains10Count;
}

double SpectrumGraph::storedBandGain(int mode, int bandIndex) const
{
    if (bandIndex < 0)
        return 0.0;
    const std::array<double, kEqBandCount31> &table =
        (mode == kEqBandMode31) ? m_bandGains31Db : m_bandGains10Db;
    if (bandIndex >= static_cast<int>(table.size()))
        return 0.0;
    return table[static_cast<std::size_t>(bandIndex)];
}

// —— 几何映射单一源：委托模型 fraction 函数 × item 几何（常量以模型为单一源）——
qreal SpectrumGraph::freqToX(qreal freqHz) const
{
    return SpectrumDisplayModel::freqToFraction(freqHz) * width();
}

qreal SpectrumGraph::dbToY(qreal gainDb) const
{
    return (1.0 - SpectrumDisplayModel::gainDbToFraction(gainDb)) * height();
}

qreal SpectrumGraph::dbFsToY(qreal dbFs) const
{
    return (1.0 - SpectrumDisplayModel::spectrumDbToFraction(dbFs)) * height();
}

// —— band 手柄数据面（T9；映射单一源 = 绘制同函数，永不漂移）——
int SpectrumGraph::handleCount() const
{
    // 档位上限 ∩ 镜像长度（settings 归一化后恒 10/31；短列表按实际显示）
    const int bandCap = (m_bandMode == kEqBandMode31) ? kEqBandCount31 : kEqBandCount10;
    return std::min(activeGainCount(), bandCap);
}

qreal SpectrumGraph::handleCenterX(int bandIndex) const
{
    const double centerHz = eqIsoBandCenterHz(m_bandMode, bandIndex);
    if (centerHz <= 0.0)
        return -1.0;
    return freqToX(centerHz);
}

qreal SpectrumGraph::handleCenterY(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= handleCount())
        return -1.0;
    // 拖动中 = 拖动吸附值（跟随光标）；否则 = 内部镜像存储值
    const double gain = (m_dragBandIndex == bandIndex) ? m_dragGainDb
                                                       : storedBandGain(m_bandMode, bandIndex);
    return dbToY(gain);
}

int SpectrumGraph::handleHitTest(const QPointF &localPos) const
{
    const int count = handleCount();
    if (count <= 0 || width() <= 0.0 || height() <= 0.0)
        return -1;
    int best = -1;
    double bestDistSq = kHandleHitTolerancePx * kHandleHitTolerancePx;
    for (int i = 0; i < count; ++i) {
        const double dx = localPos.x() - handleCenterX(i);
        const double dy = localPos.y() - handleCenterY(i);
        const double distSq = dx * dx + dy * dy;
        if (distSq <= bestDistSq) { // 距中心 ≤8px 取最近；31 档最密 22px 间距无歧义
            bestDistSq = distSq;
            best = i;
        }
    }
    return best;
}

void SpectrumGraph::setHoverIndex(int index)
{
    if (m_hoverBandIndex == index)
        return;
    m_hoverBandIndex = index;
    updateCursorShape();
    update();
}

void SpectrumGraph::updateCursorShape()
{
    if (!window())
        return;
    if (m_dragBandIndex >= 0 || m_hoverBandIndex >= 0)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}

void SpectrumGraph::notifyDragInfo()
{
    emit dragInfoChanged();
    update();
}

// 包络合成：181 点 × 活动档增益（拖动 band 用吸附值替换）——PCHIP 单调插值纯函数
// （R5，见 equalizer_curve_synth.h synthesizeGraphicEnvelopeCurve）；频率轴取合成轴
// （20..20k 对数，绘制 x 同源）。活动档无增益（count==0）→ 无曲线（手柄同门）。
void SpectrumGraph::rebuildEnvelopeCurve()
{
    if (activeGainCount() <= 0) {
        m_curveCount = 0;
        return;
    }
    std::array<double, kEqBandCount31> gains = activeGains();
    if (m_dragBandIndex >= 0 && m_dragBandIndex < static_cast<int>(gains.size()))
        gains[static_cast<std::size_t>(m_dragBandIndex)] = m_dragGainDb;
    const EqualizerCurveSynthResult syn =
        synthesizeGraphicEnvelopeCurve(m_bandMode, std::span<const double>(gains.data(), gains.size()));
    m_curveFreqs = syn.frequenciesHz;
    m_curveDbs = syn.responseDb;
    m_curveCount = kEqCurvePointCount;
}

void SpectrumGraph::beginDrag(int bandIndex, const QPointF &localPos)
{
    m_dragBandIndex = bandIndex;
    m_dragGainDb = storedBandGain(m_bandMode, bandIndex); // 起点 = 当前值
    setHoverIndex(bandIndex);
    rebuildEnvelopeCurve();
    updateDragValue(localPos); // 首帧即跟随（含吸附/合成）
    notifyDragInfo();
}

void SpectrumGraph::updateDragValue(const QPointF &localPos)
{
    if (m_dragBandIndex < 0)
        return;
    // y → 增益：全高线性逆映射（与 dbToY 同源：fraction = 1 - y/h）
    const double h = height();
    const double fraction = h > 0.0 ? qBound(0.0, 1.0 - localPos.y() / h, 1.0) : 0.5;
    const double raw = kMinEqGainDb + fraction * (kMaxEqGainDb - kMinEqGainDb);
    const double snapped = snapGainToGrid(raw); // 钳 ±15 已含在吸附前
    if (qFuzzyCompare(m_dragGainDb, snapped))
        return; // 未跨网格：位置/包络均不需更新（0.1dB 台阶内移动无感）
    m_dragGainDb = snapped;
    rebuildEnvelopeCurve();
    notifyDragInfo();
}

void SpectrumGraph::endDrag(const QPointF &localPos, bool commit)
{
    if (m_dragBandIndex < 0)
        return;
    const int bandIndex = m_dragBandIndex;
    const int bandMode = m_bandMode;
    updateDragValue(localPos); // 释放位仍参与吸附（信号值 = 吸附后值）
    if (commit) {
        // 释放落值进内部镜像（手柄停在释放位，不依赖 settings 回环）；属性列表同步
        // 更新（保持 值面一致；设置键写回由 T10 胶水经 dragReleased 完成）
        const double value = m_dragGainDb;
        if (m_bandMode == kEqBandMode31) {
            m_bandGains31Db[static_cast<std::size_t>(bandIndex)] = value;
            m_bandGains31.replace(bandIndex, value);
        } else {
            m_bandGains10Db[static_cast<std::size_t>(bandIndex)] = value;
            m_bandGains10.replace(bandIndex, value);
        }
    }
    m_dragBandIndex = -1;
    setHoverIndex(-1); // 释放后指针仍在手柄上 → 由后续 hover 事件重设（不经由此处）
    // 落值（commit）或中止（!commit）后包络统一回落到当前内部镜像：commit 落值已入
    // 镜像 → 终态包络含释放值；中止则镜像未变 → 包络回落原值。手柄与曲线同源，无
    // 橡皮筋、无镜像回环等待（R5 本地包络恒即时）。
    rebuildEnvelopeCurve();
    notifyDragInfo();
    if (commit)
        emit dragReleased(bandMode, bandIndex, m_dragGainDb);
    update();
}

void SpectrumGraph::cancelDrag()
{
    if (m_dragBandIndex < 0) {
        rebuildEnvelopeCurve();
        return;
    }
    m_dragBandIndex = -1;
    setHoverIndex(-1);
    rebuildEnvelopeCurve();
    notifyDragInfo();
    update();
}

// —— 交互（T9）：仅鼠标左键；无键盘导航/触摸/点击柱编辑 ——
void SpectrumGraph::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int hit = handleHitTest(event->position());
        if (hit >= 0) {
            beginDrag(hit, event->position());
            event->accept(); // 隐式 grab：后续 move/release 直达本 item（注入机制实证）
            return;
        }
    }
    event->ignore(); // 柱区不可交互（禁点击柱编辑）；未命中不吞事件
}

void SpectrumGraph::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragBandIndex >= 0) {
        updateDragValue(event->position());
        event->accept();
        return;
    }
    event->ignore(); // 非拖动 move 不消费（hover 由 hover*Event 承担）
}

void SpectrumGraph::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragBandIndex >= 0) {
        endDrag(event->position(), /*commit=*/true);
        event->accept();
        return;
    }
    event->ignore();
}

void SpectrumGraph::mouseUngrabEvent()
{
    // 隐式 grab 被收回（序列外干预/出窗）→ 中止拖动：无释放信号、不落值
    if (m_dragBandIndex >= 0)
        cancelDrag();
}

void SpectrumGraph::hoverMoveEvent(QHoverEvent *event)
{
    const int hit = handleHitTest(event->position());
    if (m_dragBandIndex >= 0) {
        setHoverIndex(m_dragBandIndex); // 拖动态 hover 视觉锁定拖动手柄
        return;
    }
    setHoverIndex(hit);
}

void SpectrumGraph::hoverLeaveEvent(QHoverEvent *)
{
    if (m_dragBandIndex < 0)
        setHoverIndex(-1);
}

// 曲线显示源 = 本地包络通道（R5）：m_curveFreqs/Dbs/Count 由 rebuildEnvelopeCurve 维护，
// 数据变化即重建（无镜像仲裁/pending——包络恒本地即时）。
void SpectrumGraph::resolveCurveSource(const double *&freqs, const double *&dbs, int &count) const
{
    freqs = nullptr;
    dbs = nullptr;
    count = 0;
    if (m_curveCount >= 2) {
        freqs = m_curveFreqs.data();
        dbs = m_curveDbs.data();
        count = m_curveCount;
    }
}

// —— 推进/收敛（GUI 线程；updatePaintNode 只读状态，零 QML 引擎调用）——
// 收敛判定：display/peak 与影子目标之差 ≤ 容差即停表。模型不变量 display ≥ target、
// peak ≥ display；新目标低于当前显示时两通道同值仍待回落（峰值衰减慢 ~6.7×，
// 停表过早会冻结半空峰值），故不能只看通道间隙，必须以影子目标为界。
bool SpectrumGraph::isConverged() const
{
    const std::vector<double> &display = m_displayModel.displays();
    const std::vector<double> &peak = m_displayModel.peaks();
    for (int i = 0; i < SpectrumDisplayModel::kBinCount; ++i) {
        if (display[static_cast<std::size_t>(i)] - m_shadowTarget[static_cast<std::size_t>(i)]
                > kConvergedTolerance
            || peak[static_cast<std::size_t>(i)] - m_shadowTarget[static_cast<std::size_t>(i)]
                   > kConvergedTolerance)
            return false;
    }
    return true;
}

void SpectrumGraph::syncAnimTimer()
{
    if (m_animTimer.isActive()) {
        if (isConverged())
            m_animTimer.stop();
        return;
    }
    if (!isConverged()) {
        m_animClock.start();
        m_animTimer.start();
    }
}

void SpectrumGraph::onAnimTick()
{
    const double dt = m_animClock.nsecsElapsed() * 1e-9;
    m_animClock.restart();
    m_displayModel.advanceBy(dt); // 大步长安全：exp→0 直落目标，无过冲/NaN
    update();
    if (isConverged())
        m_animTimer.stop(); // 收敛停：无空转
}

// —— 渲染核心：柱 120（顶点色渐变）/ 峰值线 / 曲线双 pass + 淡填充 / band 手柄 ——
// 节点树（组合稳定帧零分配；组合变化 = 柱/峰 固定 + 曲线 0/3 + 手柄 0..31 尾区，
// 六种组合子节点数互异（2/5/12/15/33/36），childCount 失配即整树重建——仅组合
// 变化帧分配，随后每帧仅写顶点）：
//   [0] 柱        DrawTriangles       6×120 顶点
//   [1] 峰值线    DrawTriangles       6×120 顶点
//   [2] 曲线淡填充 DrawTriangleStrip  2×N（N = 显示曲线点数）
//   [3] 曲线下 pass DrawTriangles     6×(N−1)
//   [4] 曲线上 pass DrawTriangles     6×(N−1)
//   [5..] 手柄    DrawTriangleFan     20×k 顶点（k = 手柄数，hover/拖动放大提亮）
// 曲线显示源（本地包络通道）共用同一几何路径：仅顶点数据源固定（x 轴同公式）。
// spectrumBarsVisible=false 时柱/峰节点写零面积退化几何（节点数/组合签名不变，
// 见柱节注释；T10 频谱开关联动）。
QSGNode *SpectrumGraph::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGNode *root = oldNode ? oldNode : new QSGNode;

    const double *curveFreq = nullptr;
    const double *curveDb = nullptr;
    int curveN = 0;
    resolveCurveSource(curveFreq, curveDb, curveN);
    const bool curveVisible = curveN >= 2;
    const int handleCountNow = handleCount();
    // 组合签名核对（曲线点数/可见性/手柄数）——与上一帧不符 → 整树重建。仅签名
    // 变化帧分配节点（柱/峰/曲线/手柄各自顶点容量定长），随后每帧只写顶点。
    if (m_paintCurveN != curveN || m_paintHandleN != handleCountNow) {
        m_paintCurveN = curveN;
        m_paintHandleN = handleCountNow;
        while (root->childCount() > 0) {
            QSGNode *node = root->childAtIndex(0);
            root->removeChildNode(node);
            delete node;
        }
    }

    constexpr int kBarVertices = kBarQuadVertices * SpectrumDisplayModel::kBinCount;
    QSGGeometryNode *barNode = ensureGeometryNode(root, 0, kBarVertices, QSGGeometry::DrawTriangles);
    QSGGeometryNode *peakNode = ensureGeometryNode(root, 1, kBarVertices, QSGGeometry::DrawTriangles);

    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    const float slotW = w / static_cast<float>(SpectrumDisplayModel::kBinCount);
    const float barW = slotW * static_cast<float>(m_barWidthRatio);
    const float peakH = static_cast<float>(m_peakLineHeight);

    const RgbaU8 topColor = toRgbaU8(m_barTopColor);
    const RgbaU8 bottomColor = toRgbaU8(m_barBottomColor);
    const RgbaU8 peakColor = toRgbaU8(m_peakLineColor);

    const std::vector<double> &display = m_displayModel.displays();
    const std::vector<double> &peak = m_displayModel.peaks();

    // —— 柱：底 y=h 对齐，顶 y=(1−display)·h；顶部顶点亮色/底部暗色 GPU 插值渐变 ——
    // spectrumBarsVisible=false（T10 频谱开关 OFF）→ 全部写零面积退化几何（顶=底=h）：
    // 节点/顶点容量/组合签名不变（无整树重建与每帧分配），开关抖动零节点树扰动。
    QSGGeometry::ColoredPoint2D *barV = barNode->geometry()->vertexDataAsColoredPoint2D();
    for (int i = 0; i < SpectrumDisplayModel::kBinCount; ++i) {
        const float yTop = m_spectrumBarsVisible
            ? static_cast<float>((1.0 - display[static_cast<std::size_t>(i)]) * h)
            : h;
        const float x0 = (static_cast<float>(i) + 0.5f) * slotW - barW * 0.5f;
        const float x1 = x0 + barW;
        // 三角形 1：(左上, 左下, 右下)；三角形 2：(左上, 右下, 右上)
        barV[0].set(x0, yTop, topColor.r, topColor.g, topColor.b, topColor.a);
        barV[1].set(x0, h, bottomColor.r, bottomColor.g, bottomColor.b, bottomColor.a);
        barV[2].set(x1, h, bottomColor.r, bottomColor.g, bottomColor.b, bottomColor.a);
        barV[3].set(x0, yTop, topColor.r, topColor.g, topColor.b, topColor.a);
        barV[4].set(x1, h, bottomColor.r, bottomColor.g, bottomColor.b, bottomColor.a);
        barV[5].set(x1, yTop, topColor.r, topColor.g, topColor.b, topColor.a);
        barV += kBarQuadVertices;
    }
    // 节点级标脏（渲染线程 GPU 路径关键）：QSGGeometry::markVertexDataDirty 只标记
    // geometry 内部待上传，节点须经 markDirty(QSGNode::DirtyGeometry) 才触发渲染器
    // 重新上传顶点缓冲——缺失时屏显停留首帧内容（CPU/offscreen 软件路径无此问题，
    // GPU 后端症状为柱/曲线/手柄全部冻结，仅交互强制的窗口级重绘才刷新一次）。
    barNode->geometry()->markVertexDataDirty();
    barNode->markDirty(QSGNode::DirtyGeometry);

    // —— 峰值保持线：每柱顶部 peakLineHeight(2px) 亮色横条，独立顶点色 ——
    // 隐藏时同样退化（yTop=yBottom=h 零面积）。
    QSGGeometry::ColoredPoint2D *peakV = peakNode->geometry()->vertexDataAsColoredPoint2D();
    for (int i = 0; i < SpectrumDisplayModel::kBinCount; ++i) {
        const float yTop = m_spectrumBarsVisible
            ? static_cast<float>((1.0 - peak[static_cast<std::size_t>(i)]) * h)
            : h;
        const float yBottom = yTop + peakH; // 峰值=柱顶时充当亮色柱帽；clip 钳制越界
        const float x0 = (static_cast<float>(i) + 0.5f) * slotW - barW * 0.5f;
        const float x1 = x0 + barW;
        peakV[0].set(x0, yTop, peakColor.r, peakColor.g, peakColor.b, peakColor.a);
        peakV[1].set(x0, yBottom, peakColor.r, peakColor.g, peakColor.b, peakColor.a);
        peakV[2].set(x1, yBottom, peakColor.r, peakColor.g, peakColor.b, peakColor.a);
        peakV[3].set(x0, yTop, peakColor.r, peakColor.g, peakColor.b, peakColor.a);
        peakV[4].set(x1, yBottom, peakColor.r, peakColor.g, peakColor.b, peakColor.a);
        peakV[5].set(x1, yTop, peakColor.r, peakColor.g, peakColor.b, peakColor.a);
        peakV += kBarQuadVertices;
    }
    peakNode->geometry()->markVertexDataDirty();
    peakNode->markDirty(QSGNode::DirtyGeometry);

    if (curveVisible) {
        // —— 曲线：本地包络（R5）181 点 → 频率 fraction×w 定位 x；增益→y ——
        QSGGeometryNode *fillNode = ensureGeometryNode(root, 2, 2 * curveN, QSGGeometry::DrawTriangleStrip);
        QSGGeometryNode *softNode = ensureGeometryNode(root, 3, kRibbonSegmentVertices * (curveN - 1), QSGGeometry::DrawTriangles);
        QSGGeometryNode *coreNode = ensureGeometryNode(root, 4, kRibbonSegmentVertices * (curveN - 1), QSGGeometry::DrawTriangles);

        QSGGeometry::ColoredPoint2D *fillV = fillNode->geometry()->vertexDataAsColoredPoint2D();
        QSGGeometry::ColoredPoint2D *softV = softNode->geometry()->vertexDataAsColoredPoint2D();
        QSGGeometry::ColoredPoint2D *coreV = coreNode->geometry()->vertexDataAsColoredPoint2D();

        const RgbaU8 curveColor = toRgbaU8(m_curveColor);
        const RgbaU8 fillRgba = withPremultipliedAlpha(curveColor, kCurveFillAlpha);
        const RgbaU8 softRgba = withPremultipliedAlpha(curveColor, kCurveSoftAlpha);
        const RgbaU8 coreRgba = curveColor; // 上 pass 实色（注入 alpha 透传）

        // 淡填充：strip 交替（底点, 曲线点），三角对逐段覆盖梯形（x 单调无自交）。
        // 折线 x/y 均由模型 fraction 推导 → 有限值保证（无 NaN 几何）。
        for (int i = 0; i < curveN; ++i) {
            const std::size_t idx = static_cast<std::size_t>(i);
            const float x = static_cast<float>(SpectrumDisplayModel::freqToFraction(curveFreq[idx]) * w);
            const float y = static_cast<float>((1.0 - SpectrumDisplayModel::gainDbToFraction(curveDb[idx])) * h);
            fillV[2 * i].set(x, h, fillRgba.r, fillRgba.g, fillRgba.b, fillRgba.a);
            fillV[2 * i + 1].set(x, y, fillRgba.r, fillRgba.g, fillRgba.b, fillRgba.a);
        }
        fillNode->geometry()->markVertexDataDirty();
        fillNode->markDirty(QSGNode::DirtyGeometry);

        // 双 pass 伪 AA：下 3px 半透明（0.35α）+ 上 1.5px 实色
        for (int s = 0; s < curveN - 1; ++s) {
            const std::size_t a = static_cast<std::size_t>(s);
            const std::size_t b = static_cast<std::size_t>(s + 1);
            const float x0 = static_cast<float>(SpectrumDisplayModel::freqToFraction(curveFreq[a]) * w);
            const float y0 = static_cast<float>((1.0 - SpectrumDisplayModel::gainDbToFraction(curveDb[a])) * h);
            const float x1 = static_cast<float>(SpectrumDisplayModel::freqToFraction(curveFreq[b]) * w);
            const float y1 = static_cast<float>((1.0 - SpectrumDisplayModel::gainDbToFraction(curveDb[b])) * h);
            writeSegment(softV + kRibbonSegmentVertices * s, x0, y0, x1, y1,
                         static_cast<float>(kCurveSoftWidthPx * 0.5), softRgba);
            writeSegment(coreV + kRibbonSegmentVertices * s, x0, y0, x1, y1,
                         static_cast<float>(kCurveCoreWidthPx * 0.5), coreRgba);
        }
        softNode->geometry()->markVertexDataDirty();
        softNode->markDirty(QSGNode::DirtyGeometry);
        coreNode->geometry()->markVertexDataDirty();
        coreNode->markDirty(QSGNode::DirtyGeometry);
    }

    // —— band 手柄（T9）：每档一个圆盘，中心 (freqToX(ISO 频点), dbToY(增益)) ——
    // hover/拖动中的手柄放大（半径 8px）并提亮（hover 色）；命中/绘制同一映射。
    if (handleCountNow > 0) {
        const int handleBase = 2 + (curveVisible ? 3 : 0);
        const RgbaU8 handleRgba = toRgbaU8(m_handleColor);
        const RgbaU8 handleHoverRgba = toRgbaU8(m_handleHoverColor);
        for (int i = 0; i < handleCountNow; ++i) {
            QSGGeometryNode *node = ensureGeometryNode(root, handleBase + i,
                                                       kHandleFanVertices, QSGGeometry::DrawTriangleFan);
            QSGGeometry::ColoredPoint2D *v = node->geometry()->vertexDataAsColoredPoint2D();
            const bool emphasized = (i == m_hoverBandIndex || i == m_dragBandIndex);
            const double radius = emphasized ? kHandleHoverRadiusPx : kHandleRadiusPx;
            const double centerHz = eqIsoBandCenterHz(m_bandMode, i);
            const double gain = (m_dragBandIndex == i) ? m_dragGainDb
                                                       : storedBandGain(m_bandMode, i);
            writeDiscFan(v, static_cast<float>(freqToX(centerHz)),
                         static_cast<float>(dbToY(gain)), static_cast<float>(radius),
                         emphasized ? handleHoverRgba : handleRgba);
            node->geometry()->markVertexDataDirty();
            node->markDirty(QSGNode::DirtyGeometry);
        }
    }
    return root;
}

} // namespace Seriona::App
