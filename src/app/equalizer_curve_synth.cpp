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

namespace {

// —— PCHIP（Fritsch–Carlson 保形三次插值）——
// 节点为 (对数频位置 x∈[0,1], 增益 dB)；x 域即 181 点轴的指数域（freqToX），
// 采样点在 x 域等距 → 插值在同一线性域进行。
// 节点斜率规则（保证段内单调、曲线不过冲）：
//   内部：左右差分同号时取加权调和均值（FC 原式：1/m = (w1/dL + w2/dR)/(w1+w2)，
//         w1=2hR+hL 配 dL、w2=hR+2hL 配 dR——加权算术不满足 m ≤ 3·min(dL,dR) 的
//         单调充分条件，邻带斜率悬殊时可能过冲，故必须调和）；异号或含 0 → 0；
//   端点：三点公式（左端 ( (2hL+hR)dL − hL·dR )/(hL+hR)，右端镜像取末两段），
//         与内侧差分异号 → 0，与外侧差分异号且 |m|>3|d内| → 钳 3·d内。
[[nodiscard]] double freqToX(double frequencyHz) noexcept
{
    return std::log10(frequencyHz / 20.0) / 3.0; // 轴 20..20k = 3 decades
}

[[nodiscard]] double hermiteAt(double t, double y0, double m0, double h, double y1, double m1) noexcept
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    return h00 * y0 + h * (h10 * m0 + h11 * m1) + h01 * y1;
}

// 采样点 x 所在段定位（x ∈ (xs[0], xs[n-1])，span 为有序前缀；upper_bound 二分）
[[nodiscard]] std::size_t locateSegment(std::span<const double> xs, double x) noexcept
{
    return static_cast<std::size_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin() - 1);
}

} // namespace

EqualizerCurveSynthResult synthesizeGraphicEnvelopeCurve(int bandMode,
                                                         std::span<const double> bandGains) noexcept
{
    EqualizerCurveSynthResult result{};
    const bool mode31 = (bandMode == kEqBandMode31);
    const std::span<const double> centers = mode31 ? std::span<const double>{kEqIso31BandCenterHz}
                                                   : std::span<const double>{kEqIso10BandCenterHz};
    const std::size_t n = centers.size();
    if (n == 0)
        return result;

    std::array<double, kEqIso31BandCenterHz.size()> xs{};
    std::array<double, kEqIso31BandCenterHz.size()> ys{};
    std::array<double, kEqIso31BandCenterHz.size()> ms{};
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = freqToX(centers[i]);
        const double raw = i < bandGains.size() ? bandGains[i] : 0.0;
        ys[i] = std::clamp(std::isfinite(raw) ? raw : 0.0, kMinEqGainDb, kMaxEqGainDb);
    }
    const auto xSpan = std::span<const double>(xs.data(), n);
    const auto ySpan = std::span<const double>(ys.data(), n);

    if (n == 1) {
        ms[0] = 0.0;
    } else {
        std::array<double, kEqIso31BandCenterHz.size()> ds{};
        for (std::size_t i = 0; i + 1 < n; ++i)
            ds[i] = (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]);
        const auto sameSignNonZero = [](double a, double b) { return (a > 0.0) == (b > 0.0) && a != 0.0 && b != 0.0; };
        if (n == 2) {
            ms[0] = ds[0];
            ms[1] = ds[0];
        } else {
            for (std::size_t i = 1; i + 1 < n; ++i) {
                if (!sameSignNonZero(ds[i - 1], ds[i])) {
                    ms[i] = 0.0;
                    continue;
                }
                const double hL = xs[i] - xs[i - 1];
                const double hR = xs[i + 1] - xs[i];
                const double w1 = 2.0 * hR + hL; // 配左差分（FC 原式权重）
                const double w2 = hR + 2.0 * hL; // 配右差分
                ms[i] = (w1 + w2) / (w1 / ds[i - 1] + w2 / ds[i]);
            }
            // 左端（节点 0）：三点公式用段 0/1；右端（节点 n-1）：镜像用末两段
            const auto clampEdge = [&](double m, double dIn, double dOut) {
                if (!sameSignNonZero(m, dIn))
                    return 0.0;
                if (!sameSignNonZero(dIn, dOut) && std::abs(m) > 3.0 * std::abs(dIn))
                    return 3.0 * dIn;
                return m;
            };
            const double h0 = xs[1] - xs[0];
            const double h1 = xs[2] - xs[1];
            ms[0] = clampEdge(((2.0 * h0 + h1) * ds[0] - h0 * ds[1]) / (h0 + h1), ds[0], ds[1]);
            const double hLn = xs[n - 1] - xs[n - 2]; // 末段
            const double hRn = xs[n - 2] - xs[n - 3]; // 倒数第二段
            ms[n - 1] = clampEdge(((2.0 * hLn + hRn) * ds[n - 2] - hLn * ds[n - 3]) / (hLn + hRn),
                                  ds[n - 2], ds[n - 3]);
        }
    }

    constexpr double kAxisMinHz = 20.0;
    constexpr double kDecades = 3.0;
    for (std::size_t i = 0; i < kEqCurvePointCount; ++i) {
        const double exponent = static_cast<double>(i) / static_cast<double>(kEqCurvePointCount - 1U);
        const double frequencyHz = kAxisMinHz * std::pow(10.0, exponent * kDecades);
        result.frequenciesHz[i] = frequencyHz;
        // x 域等距采样恰为 exponent；首尾手柄之外平坦延展（10 档 20-31.5Hz / 16k-20k）
        if (exponent <= xs[0]) {
            result.responseDb[i] = ys[0];
            continue;
        }
        if (exponent >= xs[n - 1]) {
            result.responseDb[i] = ys[n - 1];
            continue;
        }
        const std::size_t k = locateSegment(xSpan, exponent);
        const double xk = xs[k];
        const double xk1 = xs[k + 1];
        result.responseDb[i] = hermiteAt((exponent - xk) / (xk1 - xk), ySpan[k], ms[k], xk1 - xk,
                                         ySpan[k + 1], ms[k + 1]);
    }
    return result;
}

} // namespace Seriona::App
