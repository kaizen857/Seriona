#pragma once

#include "equalizer_presets.h"

#include <cmath>
#include <span>
#include <vector>

namespace Seriona::App {

// —— 频谱显示平滑模型（任务 7，纯 C++；T8 渲染核心消费）——
// 输入 binsDb（120 桶 dBFS：0dBFS 满刻度、-120 静音地板）→ 输出每桶 [0,1] 显示值
// （柱 display + 峰值保持 peak 双通道）。本类零 Qt 依赖（无 QtQuick/QSG/QObject，
// 纯标准库 + equalizer_presets.h 常量），可无 GUI 单测；类内无实时时钟——调用方
// 按 wall-clock dt 注入 advanceBy(dtSeconds) 推进，帧计数不存在于本类。
//
// 平滑语义（调研文档 §6.2；显示帧率与 ~40Hz 数据率解耦）：
//  - attack 即时：目标升 → display/peak 立即到位（新数据 ≤1 帧可见）；
//  - decay 单指数回落：display(t+dt) = target + (display(t) − target)·exp(−dt/τ)，
//    τ 取 135ms（规格带 120-150ms 中值）；
//  - peak：max 保持（新最大值即时更新）+ 独立回落 τ 取 900ms（规格带 800ms-1s）。
//  对恒定低于显示的目标，t=τ 处恰余 e^−1（≈0.368）——测试按此实测锁定。
//
// 输入映射：binsDb → 目标分数。频谱显示域 -60..0dBFS 线性 → [0,1]
// （0dBFS → 1 顶；≤-60dB → 0 底；-120 静音地板同 0）。db ≥ 0 一律钳 1。
//
// 几何映射函数族（纯静态函数，显式域参数、不依赖 item 几何，输出 [0,1] 分数）：
// spectrum_graph.h 的 item 级 Q_INVOKABLE（freqToX/dbToY/dbToX）在 T8 接线时委托
// 本族换算（fraction × item 宽/高）；本类与桶数常量共用 equalizer_presets.h 单一源。
class SpectrumDisplayModel
{
public:
    // —— 桶数与平滑时间常数（调研 §6.2 带内取值，测试锁定在规格带内）——
    static constexpr int kBinCount = kEqSpectrumBinCount; // 120（settings 镜像门同源常量）
    // 柱回落 τ（120-150ms 带内取 135ms；e^−1 实测以本常量为准）
    static constexpr double kBarFallTauSeconds = 0.135;
    // 峰值回落 τ（800ms-1s 带内取 900ms）
    static constexpr double kPeakFallTauSeconds = 0.9;

    // —— 轴/显示域常量（T8 起 spectrum_graph 委托本类，常量以此处为单一源）——
    static constexpr double kFreqAxisMinHz = 20.0; // 对数频率轴 20Hz
    static constexpr double kFreqAxisMaxHz = 20000.0; // 对数频率轴 20kHz
    static constexpr double kSpectrumDbFloorDb = -60.0; // 频谱显示域底（≤此值显示 0；-120 静音地板同 0）
    static constexpr double kSpectrumDbCeilDb = 0.0; // 频谱显示域顶（0dBFS；≥0 钳 1）
    static constexpr double kGainDbFloor = kMinEqGainDb; // 曲线/手柄增益域底（-15dB，复用预设头常量）
    static constexpr double kGainDbCeil = kMaxEqGainDb; // 曲线/手柄增益域顶（+15dB）

    // —— 几何映射函数族（纯函数；输出 [0,1] 分数，域外钳制）——
    // 对数频率轴 20..20k → [0,1]：freqToX 委托 x = fraction·itemWidth；域外钳 0/1。
    static double freqToFraction(double freqHz);
    // 频率分数 → Hz（freqToFraction 逆；bandCenterHz 共用）。域外钳到轴两端。
    static double frequencyAtFraction(double fraction);
    // 桶 i 对数轴中心分数：(i + 0.5)/binCount —— 桶中心约定（前端 x 用桶中心非桶边）。
    static double bandCenterFraction(int index);
    // 桶 i 中心频率 Hz（20..20k 对数轴 (i+0.5)/120 处；i 域外钳 0/末桶）。
    static double bandCenterHz(int index);
    // 频谱 dBFS -60..0 → [0,1]：0dBFS→1、-60 及以下→0、≥0 钳 1。
    // 柱目标喂入与 spectrum_graph dbToX 委托共用（item y = (1−frac)·itemHeight）。
    static double spectrumDbToFraction(double dbFs);
    // EQ 增益 ±15dB → [0,1]：+15→1、-15→0、0→0.5（dbToY 委托；item y = (1−frac)·itemHeight）。
    static double gainDbToFraction(double gainDb);

    // —— 显示状态机（wall-clock dt 推进）——
    SpectrumDisplayModel();
    // 全通道清零（柱/峰值/目标；空数据/频谱关闭态入口）。
    void reset();
    // 喂入新频谱快照（dBFS，≤kBinCount 长度自适应取前缀；超长截断）。目标=映射分数；
    // 攻击即时：目标高于当前显示 → display/peak 立即到位；低于 → 保持现状待 advanceBy 回落。
    void setBinsDb(std::span<const double> binsDb);
    // 墙钟推进 dt 秒（dt ≤ 0 或非有限值无操作）：目标低于显示 → 按各自 τ 单指数回落。
    // 大步长天然安全：exp(−dt/τ)→0 直落目标，无过冲/NaN；恒定输入收敛后保持 == 目标。
    void advanceBy(double dtSeconds);

    // —— 读取面（只读；向量构造期一次性定长，无每帧分配）——
    int binCount() const { return kBinCount; }
    double displayValue(int index) const; // [0,1] 柱显示分数（index 域外钳 0/末）
    double peakValue(int index) const; // [0,1] 峰值保持分数
    const std::vector<double> &displays() const { return m_display; } // 定长 kBinCount
    const std::vector<double> &peaks() const { return m_peak; } // 定长 kBinCount

private:
    std::vector<double> m_target; // 每桶目标分数 [0,1]（最新快照映射）
    std::vector<double> m_display; // 每桶显示分数 [0,1]
    std::vector<double> m_peak; // 每桶峰值分数 [0,1]
};

} // namespace Seriona::App
