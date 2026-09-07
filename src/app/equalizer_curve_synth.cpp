#include "equalizer_curve_synth.h"

#include <algorithm>
#include <cmath>

namespace Seriona::App {

double eqIsoBandCenterHz(int bandMode, int bandIndex) noexcept
{
    if (bandMode == kEqBandMode31) {
        if (bandIndex >= 0 && bandIndex < static_cast<int>(kEqIso31BandCenterHz.size()))
            return kEqIso31BandCenterHz[static_cast<std::size_t>(bandIndex)];
        return 0.0;
    }
    if (bandIndex >= 0 && bandIndex < static_cast<int>(kEqIso10BandCenterHz.size()))
        return kEqIso10BandCenterHz[static_cast<std::size_t>(bandIndex)];
    return 0.0;
}

namespace {

// —— 与后端 control_state_reducer.cpp 同源的 RBJ peaking 幅频响应（逐项对齐）——
// A = 10^(gainDb/40)，x = f/f0：
//   |H(f)|² = ((1−x²)² + (x·A/Q)²) / ((1−x²)² + (x/(A·Q))²)
// Q/f0 取 ISO 表（同源声明）；band31 与 band10 两分支同构（:327-328 / :335-336，
// 分母 pow(x/(a·q),2.0) 不得截短）。band 增益防御钳 ±15 后计算（后端 config 已
// 归一化；缺失项（短 span）按 0dB——0dB 时 A=1 响应恒 0dB，等价无此 band）。
[[nodiscard]] double bandResponseDb(bool mode31,
                                    std::size_t bandIndex,
                                    double frequencyHz,
                                    std::span<const double> bandGains) noexcept
{
    const double q = mode31 ? kEqIso31BandQ : kEqIso10BandQ;
    const double centerHz = eqIsoBandCenterHz(mode31 ? kEqBandMode31 : kEqBandMode10,
                                               static_cast<int>(bandIndex));
    const double rawGain = bandIndex < bandGains.size() ? bandGains[bandIndex] : 0.0;
    const double gainDb = std::clamp(std::isfinite(rawGain) ? rawGain : 0.0,
                                     kMinEqGainDb, kMaxEqGainDb);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double x = frequencyHz / centerHz;
    const double detuned = 1.0 - x * x;
    return 10.0 * std::log10((detuned * detuned + std::pow(x * a / q, 2.0)) /
                             (detuned * detuned + std::pow(x / (a * q), 2.0)));
}

} // namespace

EqualizerCurveSynthResult synthesizeEqualizerCurve(int bandMode,
                                                   double preGainDb,
                                                   std::span<const double> bandGains,
                                                   std::uint32_t sampleRateOrZero) noexcept
{
    EqualizerCurveSynthResult result{};
    const bool mode31 = (bandMode == kEqBandMode31);
    // 活跃频段数：按 mode 取前缀（Band10=10 / Band31=31）
    const std::size_t activeBands = mode31 ? kEqIso31BandCenterHz.size() : kEqIso10BandCenterHz.size();
    const double finitePreGain = std::isfinite(preGainDb) ? std::clamp(preGainDb, kMinEqGainDb, kMaxEqGainDb)
                                                          : 0.0;
    const double nyquistCap = sampleRateOrZero > 0U ? 0.95 * (static_cast<double>(sampleRateOrZero) / 2.0) : 0.0;

    // 181 点 20–20k Hz 对数均匀（f[i] = 20 × 1000^(i/180)，与后端轴公式逐项同源）
    constexpr double kAxisMinHz = 20.0;
    constexpr double kAxisMaxHz = 20000.0;
    constexpr double kDecades = 3.0; // log10(20000/20)
    for (std::size_t i = 0; i < kEqCurvePointCount; ++i) {
        const double exponent = static_cast<double>(i) / static_cast<double>(kEqCurvePointCount - 1U);
        const double frequencyHz = kAxisMinHz * std::pow(10.0, exponent * kDecades);
        result.frequenciesHz[i] = frequencyHz;
        if (nyquistCap > 0.0 && frequencyHz >= nyquistCap) {
            // fs>0：越界硬直通守卫（f ≥ 0.95×fs/2 按 0dB）
            result.responseDb[i] = 0.0;
            continue;
        }
        double responseDb = finitePreGain; // preGain 项勿漏（后端 :356 同序）
        for (std::size_t band = 0; band < activeBands; ++band)
            responseDb += bandResponseDb(mode31, band, frequencyHz, bandGains);
        result.responseDb[i] = responseDb;
    }
    return result;
}

} // namespace Seriona::App
