#pragma once

namespace Seriona::App {

// —— 均衡器常量与内置预设表（任务 36 F1.1，只读）——
// 本文件 header-only：全部 inline constexpr，引入零链接代价，仅被
// settings_controller.cpp（及其测试目标）消费。内置 8 款预设是 C++ 常量：
// 不可删改、不可被用户 CRUD 覆盖（用户预设走独立持久化键，见
// settings_controller.cpp 键组 "equalizer" 常量区）；切换 bandMode 时按档位
// 取 gains10 / gains31 对应数组。

// bandMode 属性取值（10 段 / 31 段 GEQ）
inline constexpr int kEqBandMode10 = 10;
inline constexpr int kEqBandMode31 = 31;

// 波段数；订阅镜像长度（后端推送更新，前端不落盘）
inline constexpr int kEqBandCount10 = 10;
inline constexpr int kEqBandCount31 = 31;
inline constexpr int kEqSpectrumBinCount = 120; // 频谱桶数
inline constexpr int kEqCurvePointCount = 181; // 频响曲线采样点数（频率轴 20..20k）

// 增益钳位域（与后端 SetEqualizerConfig reducer 校验一致：每项 ±15dB）
inline constexpr double kMinEqGainDb = -15.0;
inline constexpr double kMaxEqGainDb = 15.0;

// 内置预设条目：id = 稳定英文小写标识（QML/applyEqPreset 引用，不落盘）；
// displayName = 中文显示名（UTF-8）；preGainDb = 预增益；
// gains10/gains31 = 10/31 段各一套增益（double dB，0.1 粒度，长度与档位恒匹配）。
struct BuiltinEqPreset {
    const char *id;
    const char *displayName;
    double preGainDb;
    const double *gains10;
    const double *gains31;
};

// 10 段中心频率：31 62 125 250 500 1k 2k 4k 8k 16k Hz
// 31 段中心频率：20 25 31 40 50 63 80 100 125 160 200 250 315 400 500 630 800
//               1k 1.25k 1.6k 2k 2.5k 3.15k 4k 5k 6.3k 8k 10k 12.5k 16k 20k Hz

// —— 平坦：全 0dB ——
inline constexpr double kEqFlatGains10[kEqBandCount10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
inline constexpr double kEqFlatGains31[kEqBandCount31] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// —— 低音增强：低频 +6dB 起经 200/250/315Hz 渐降，400Hz 以上归零 ——
inline constexpr double kEqBassBoostGains10[kEqBandCount10] = {6, 6, 4, 2, 1, 0, 0, 0, 0, 0};
inline constexpr double kEqBassBoostGains31[kEqBandCount31] = {6, 6, 6, 6, 6, 6, 5, 5, 5, 3, 3, 2, 1, 0, 0, 0, 0, 0,
                                                            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// —— 高音增强：1.25k/1.6k/2k/2.5k/3.15k 渐升，4k 以上 +6dB ——
inline constexpr double kEqTrebleBoostGains10[kEqBandCount10] = {0, 0, 0, 0, 0, 1, 2, 4, 6, 6};
inline constexpr double kEqTrebleBoostGains31[kEqBandCount31] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                              1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6};

// —— 人声：低频微削（≤63Hz -3dB），500Hz-2.5k 中频抬升 +4dB，8k 以上回落 ——
inline constexpr double kEqVocalGains10[kEqBandCount10] = {-2, -1, 0, 1, 2, 4, 4, 2, 0, -1};
inline constexpr double kEqVocalGains31[kEqBandCount31] = {-3, -3, -3, -2, -2, -2, -1, -1, 0, 0, 0, 1, 1, 1, 2, 3, 3,
                                                        3, 4, 4, 4, 4, 3, 3, 3, 2, 1, 1, 0, 0, 0};

// —— 流行：响度微笑曲线（低/高抬升，中频 500Hz-1k 微削）——
inline constexpr double kEqPopGains10[kEqBandCount10] = {4, 3, 2, 1, -1, -1, 0, 1, 3, 4};
inline constexpr double kEqPopGains31[kEqBandCount31] = {4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 0, 0, -1, -1, -1, -1, -1,
                                                      0, 0, 0, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4};

// —— 摇滚：低频冲击 +5dB、中频 400-800Hz 挖 -2dB、高频 8k 以上 +3/+4dB ——
inline constexpr double kEqRockGains10[kEqBandCount10] = {5, 4, 2, 0, -1, -2, -1, 1, 3, 4};
inline constexpr double kEqRockGains31[kEqBandCount31] = {5, 5, 5, 4, 4, 4, 3, 3, 2, 1, 1, 0, -1, -2, -2, -2, -2, -1,
                                                       -1, -1, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4};

// —— 古典：温和低音垫 +2dB 与高频空气感 +1dB，中频平直 ——
inline constexpr double kEqClassicalGains10[kEqBandCount10] = {2, 1, 0, 0, 0, 0, 0, 0, 1, 2};
inline constexpr double kEqClassicalGains31[kEqBandCount31] = {2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
                                                            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1};

// —— 爵士：温暖低音 +4dB、250-500Hz 微削 -1dB、1.6k-8k 存在感 +1/+2dB ——
inline constexpr double kEqJazzGains10[kEqBandCount10] = {4, 3, 1, 0, -1, 0, 1, 2, 2, 1};
inline constexpr double kEqJazzGains31[kEqBandCount31] = {4, 4, 4, 3, 3, 3, 2, 2, 1, 0, 0, -1, -1, -1, -1, 0, 0, 0,
                                                       0, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 0};

// 内置 8 款（平坦/低音增强/高音增强/人声/流行/摇滚/古典/爵士），恒序；预增益全 0
inline constexpr BuiltinEqPreset kBuiltinEqPresets[] = {
    {"flat", "平坦", 0.0, kEqFlatGains10, kEqFlatGains31},
    {"bass-boost", "低音增强", 0.0, kEqBassBoostGains10, kEqBassBoostGains31},
    {"treble-boost", "高音增强", 0.0, kEqTrebleBoostGains10, kEqTrebleBoostGains31},
    {"vocal", "人声", 0.0, kEqVocalGains10, kEqVocalGains31},
    {"pop", "流行", 0.0, kEqPopGains10, kEqPopGains31},
    {"rock", "摇滚", 0.0, kEqRockGains10, kEqRockGains31},
    {"classical", "古典", 0.0, kEqClassicalGains10, kEqClassicalGains31},
    {"jazz", "爵士", 0.0, kEqJazzGains10, kEqJazzGains31},
};

} // namespace Seriona::App
