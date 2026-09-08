#include "spectrum_display_model.h"

#include <algorithm>
#include <cstddef>

namespace Seriona::App {

// —— 几何映射函数族（纯函数：显式域参数 → [0,1] 分数，无 item 几何）——
// 域底比较用 !(value > floor) 而非 value <= floor：NaN/-inf 输入落入底值分支
// （NaN 参与 <= 恒 false，会穿透成 NaN 毒化通道；此处统一按底处理）。

double SpectrumDisplayModel::freqToFraction(double freqHz)
{
    if (!(freqHz > kFreqAxisMinHz))
        return 0.0; // ≤20Hz（含 NaN/-inf）→ 轴底
    if (freqHz >= kFreqAxisMaxHz)
        return 1.0; // ≥20kHz（含 +inf）→ 轴顶
    // 对数轴归一：ln(f/20) / ln(20k/20)
    return std::log(freqHz / kFreqAxisMinHz) / std::log(kFreqAxisMaxHz / kFreqAxisMinHz);
}

double SpectrumDisplayModel::frequencyAtFraction(double fraction)
{
    if (!(fraction > 0.0))
        return kFreqAxisMinHz; // ≤0（含 NaN）→ 轴底
    if (fraction >= 1.0)
        return kFreqAxisMaxHz;
    // 逆映射：20 · 1000^fraction
    return kFreqAxisMinHz * std::exp(std::log(kFreqAxisMaxHz / kFreqAxisMinHz) * fraction);
}

double SpectrumDisplayModel::bandCenterFraction(int index)
{
    // 桶中心约定：(i + 0.5) / binCount（对数轴分数；前端 x 用桶中心非桶边）
    const int i = std::clamp(index, 0, kBinCount - 1);
    return (static_cast<double>(i) + 0.5) / static_cast<double>(kBinCount);
}

double SpectrumDisplayModel::bandCenterHz(int index)
{
    return frequencyAtFraction(bandCenterFraction(index));
}

double SpectrumDisplayModel::spectrumDbToFraction(double dbFs)
{
    // 显示域 -60..0dBFS 线性 → [0,1]：≤-60（含 -120 静音地板/NaN/-inf）→ 0，≥0 → 1
    if (!(dbFs > kSpectrumDbFloorDb))
        return 0.0;
    const double f = (dbFs - kSpectrumDbFloorDb) / (kSpectrumDbCeilDb - kSpectrumDbFloorDb);
    return f < 1.0 ? f : 1.0;
}

double SpectrumDisplayModel::gainDbToFraction(double gainDb)
{
    // EQ 增益 ±15dB 全高 → [0,1]（0dB 恒半高）
    if (!(gainDb > kGainDbFloor))
        return 0.0;
    const double f = (gainDb - kGainDbFloor) / (kGainDbCeil - kGainDbFloor);
    return f < 1.0 ? f : 1.0;
}

// —— 显示状态机 ——

SpectrumDisplayModel::SpectrumDisplayModel()
    : m_target(kBinCount, 0.0)
    , m_display(kBinCount, 0.0)
    , m_peak(kBinCount, 0.0)
{
}

void SpectrumDisplayModel::reset()
{
    std::fill(m_target.begin(), m_target.end(), 0.0);
    std::fill(m_display.begin(), m_display.end(), 0.0);
    std::fill(m_peak.begin(), m_peak.end(), 0.0);
}

void SpectrumDisplayModel::setBinsDb(std::span<const double> binsDb)
{
    const std::size_t n = std::min(binsDb.size(), static_cast<std::size_t>(kBinCount));
    for (std::size_t i = 0; i < n; ++i) {
        const double target = spectrumDbToFraction(binsDb[i]);
        m_target[i] = target;
        // attack 即时：目标高于当前显示/峰值 → 立即到位；回落只发生在 advanceBy
        if (target > m_display[i])
            m_display[i] = target;
        if (target > m_peak[i])
            m_peak[i] = target;
    }
}

void SpectrumDisplayModel::advanceBy(double dtSeconds)
{
    if (!(dtSeconds > 0.0))
        return; // dt ≤ 0 或 NaN：无时间流逝/时钟回拨，不推进
    // 单指数回落因子：大步长 → exp(−dt/τ) → 0，显示直落目标，无过冲/NaN
    const double barFactor = std::exp(-dtSeconds / kBarFallTauSeconds);
    const double peakFactor = std::exp(-dtSeconds / kPeakFallTauSeconds);
    for (int i = 0; i < kBinCount; ++i) {
        const double target = m_target[i];
        if (m_display[i] > target)
            m_display[i] = target + (m_display[i] - target) * barFactor;
        if (m_peak[i] > target) {
            m_peak[i] = target + (m_peak[i] - target) * peakFactor;
        } else if (m_peak[i] < target) {
            m_peak[i] = target; // 峰值 max 保持兜底（常规路径在 setBinsDb 已即时更新）
        }
    }
}

double SpectrumDisplayModel::displayValue(int index) const
{
    return m_display[static_cast<std::size_t>(std::clamp(index, 0, kBinCount - 1))];
}

double SpectrumDisplayModel::peakValue(int index) const
{
    return m_peak[static_cast<std::size_t>(std::clamp(index, 0, kBinCount - 1))];
}

} // namespace Seriona::App
