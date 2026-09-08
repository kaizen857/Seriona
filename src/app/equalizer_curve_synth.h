#pragma once

// —— EQ 频响曲线纯合成（纯 C++23，零 Qt 依赖）——
// 两个同轴纯函数（均输出 181 点 20..20k 对数均匀轴）：
//   · synthesizeEqualizerCurve：物理叠加响应（RBJ peaking 逐点叠加 + preGain），
//     与后端 reducer 快照逐点同源——conformance 测试锁 ≤0.05dB；R5 前作拖动显示
//     源，R5 起显示曲线改用包络（本函数保留：后端物理响应同源锁 + 未来真实响应
//     显示模式的本地合成面）；
//   · synthesizeGraphicEnvelopeCurve：R5 显示曲线（手柄点 PCHIP 包络，恒过手柄、
//     不过冲，与 DSP 物理响应解耦）——见下方声明注释。
//
// ★ 同源声明（review 核对项，勿漂移）：
//   synthesizeEqualizerCurve 的曲线语义与后端 makeEqualizerStateSnapshot 逐条同源 ——
//   Seriona_Backend/src/control/control_state_reducer.cpp:306-363：
//     · 轴生成  :306-313  （181 点 20..20k 对数均匀，f[i]=20·1000^(i/180)）；
//     · RBJ 响应 :319-336 （:335-336 Band10 分支 return 的分母含 pow(x/(a·q),2.0)，
//       与 Band31 分支 :327-328 同构——复制时两分支均不得截短）；
//     · 快照函数 :342-363（responseDb = preGainDb + Σ(band)；fs>0 时
//       f ≥ 0.95×(fs/2) 的点按 0dB；fs=0 全轴计算）。
//   ISO 频点/Q 常量复制自 Seriona_Backend/inc/seriona/audio/equalizer_tables.h
//   （同源声明；前端与后端各持一份——仓库跨仓无共享头，漂移锁 = conformance 测试
//   tst_equalizer_curve_conformance.cpp：≤0.05dB × 全部 181 点 × 测试组）。
//   前端曲线点数常量 kEqCurvePointCount / 增益域常量 kMinEqGainDb/kMaxEqGainDb
//   仍以 equalizer_presets.h 为单一源，本文件不重定义。

#include "equalizer_presets.h"

#include <array>
#include <cstdint>
#include <span>

namespace Seriona::App {

// —— ISO 标称中心频点与每档固定 Q（复制自 equalizer_tables.h，同源声明见文件头）——
// 10 段图示均衡（1-octave，ISO 标称中心频率）：20 Hz–16 kHz 档位。
inline constexpr std::array<double, 10> kEqIso10BandCenterHz{
    31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};

// 31 段图示均衡（1/3-octave，ISO 标称中心频率）：20 Hz–20 kHz 档位。
inline constexpr std::array<double, 31> kEqIso31BandCenterHz{
    20.0,   25.0,   31.5,   40.0,   50.0,   63.0,   80.0,   100.0,  125.0,  160.0,
    200.0,  250.0,  315.0,  400.0,  500.0,  630.0,  800.0,  1000.0, 1250.0, 1600.0,
    2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0,
    20000.0};

// 每档固定 Q（RBJ peaking 设计值，与 equalizer_tables.h 同值）
inline constexpr double kEqIso10BandQ = 1.41;
inline constexpr double kEqIso31BandQ = 4.32;

// 手柄中心频点查询：bandMode 10/31 → ISO 频点表；index 域外返回 0。
// （SpectrumGraph 手柄 x = freqToX(本函数)；QML/T10 如需定位同一频点亦用本表。）
[[nodiscard]] double eqIsoBandCenterHz(int bandMode, int bandIndex) noexcept;

// —— 合成结果（定长数组；构造期定长，无运行期分配）——
struct EqualizerCurveSynthResult {
    // 181 点对数频率轴（20..20k；与 EqualizerStateSnapshot::curveFrequenciesHz
    // 同公式，独立生成以锁轴公式漂移）
    std::array<double, kEqCurvePointCount> frequenciesHz{};
    // 181 点响应 dB（responseDb = preGainDb + Σ(band)；fs 钳制见下）
    std::array<double, kEqCurvePointCount> responseDb{};
};

// 纯函数：按当前 EQ 档位/前置增益/增益数组合成 181 点频响曲线。
//   bandMode       10 或 31（其余按 10 处理）；
//   preGainDb      前置增益（勿漏——曲线每点含此项）；
//   bandGains      当前档位增益（bandMode=31 取前 31 项、10 取前 10 项；短于档位的
//                  缺失项按 0dB 处理；逐项应已由设置控制器归一化 ±15，本函数防御钳位）；
//   sampleRateOrZero 与后端同语义：>0 时 f ≥ 0.95×(fs/2) 的点按 0dB 直通（越界硬
//                  直通守卫同语义）；=0（未定）全轴计算。
// 可独立单测的纯函数（conformance 测试的直接被测面）。
[[nodiscard]] EqualizerCurveSynthResult synthesizeEqualizerCurve(int bandMode,
                                                                 double preGainDb,
                                                                 std::span<const double> bandGains,
                                                                 std::uint32_t sampleRateOrZero = 0) noexcept;

// —— 图形包络合成（R5：GEQ 显示曲线 = 过手柄点的目标包络，与 DSP 解耦）——
// 调研结论（消费级 GEQ/播放器惯例，Spotify/foobar2000/moOde 同）：GEQ UI 曲线画
// 「手柄点平滑包络」而非逐点物理叠加——物理响应在相邻高增益带叠加后中心会高出
// 手柄 4-5dB（peaking 裙边贡献），与「手柄即所见增益」的编辑语义冲突。
// 本函数以 (ISO 中心频点, 档增益) 为型值点，对 181 点对数轴做 PCHIP 单调三次
// 插值（Fritsch–Carlson 斜率约束）：曲线恒过每个手柄、段间单调、绝不过冲
// （Catmull-Rom 等过冲样条会产生「峰高于手柄」的同类问题，故不用）。
// 不含 preGainDb（总增益不进编辑包络；与主流播放器推子图形语义一致——音频仍含）。
[[nodiscard]] EqualizerCurveSynthResult synthesizeGraphicEnvelopeCurve(
    int bandMode,
    std::span<const double> bandGains) noexcept;

} // namespace Seriona::App
