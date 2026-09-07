#pragma once

#include "app_settings_storage.h"
#include "equalizer_presets.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QList>
#include <QPair>

#include <functional>

namespace Seriona::App {

// 每台输出设备的能力（后端 AudioDeviceFormat 的 supportedSampleFormats/supportedSampleRates
// 映射，经 BackendBridge::enumeratePlaybackDeviceCapabilities 填充）。
// sampleFormats/sampleRates 为空 = 未枚举或全支持（T4 语义：miniaudio 的
// ma_format_unknown / sampleRate==0 不产生条目），设置窗口下拉显示全部标准选项。
// isDefault = 系统默认播放设备（后端 ma_device_info.isDefault 透传；语义随后端而异：
// PulseAudio/PipeWire 下可靠，ALSA 下可能全部为 false，只用于 UI 标记/默认高亮）。
struct PlaybackDeviceCapabilities {
    QString deviceId;
    QString deviceName;
    QList<int> sampleFormats{};
    QList<int> sampleRates{};
    bool isDefault{false};
};

// 输出设置控制器：应用设置存储（默认内存，注入后经后端键值存储）持久化 +
// 后端 ConfigureOutput 透传。
// 本类为纯 QML 面，不依赖后端头文件/宏；与后端通信经 AppFacade 注入的 executor
// （BackendBridge::submitConfigureOutput / enumeratePlaybackDeviceCapabilities）。
//
// 推送策略（输出组）：
//  - 离散控件（sampleRate / sampleFormat / preferredDeviceId）变更立即推送；
//  - 连续控件（bufferDurationMs）变更先去抖（单发 QTimer，400ms）后推送。
//  - 后端拒绝 ConfigureOutput 时经 rollbackRejectedOutputConfig() 恢复上一次已提交的
//    合法值快照（lastValid*），快照在每次 apply() 提交前记录。
//
// 播放过渡组（键组 "transition"，9 键；域与后端 reducer 校验一致，任务 12 绑定
// SetTransitionConfig 命令）：
//  - 档位（autoAdvanceFadeMode / manualAdvanceFadeMode，枚举 0-2）与开关
//    （fadeOnTransport / fadeOnSeek）变更立即持久化并推送；
//  - 滑块（gaplessPreloadMs 0-5000 / crossfadeMs 0-10000 /
//    transportFadeMs / seekFadeMs / manualShortCrossfadeMs 0-3000）变更先去抖
//    （独立单发 QTimer，400ms，经 applyTransitionConfig() 推送）。
//  - 推送走独立的 m_applyTransitionConfigExecutor（9 参），与 ConfigureOutput 无关；
//    未绑定（mock-only）时 setter/applyTransitionConfig 均为 no-op。
//
// 均衡器组（键组 "equalizer"，任务 36 F1.1 + 任务 37 F1.2）：
//  - 7 个用户可设键（enabled/bandMode/preGainDb/bandGains10/bandGains31/
//    limiterEnabled/spectrumEnabled）变更即经注入的 AppSettingsStorage 持久化
//    （与 output/lyrics/logging/transition 同通道；落盘即时，推送按类别延迟）。
//  - 数值纪律：bandMode 仅 10/31；preGainDb ±15dB 越界拒绝；bandGains 写入
//    防御归一化（逐项 ±15dB 钳位、定长 10/31 补零/截断），非法输入不进存储。
//  - 提交语义（F1.2，仿 transition 推送机制同构，独立通道）：
//      · 推送走独立的 m_applyEqualizerConfigExecutor——全量组包（无部分状态）：
//        enabled/bandMode/preGainDb/当前档位增益数组（bandMode 10/31 取一档，
//        定长 10/31）/limiterEnabled/spectrumEnabled，经 applyEqualizerConfig()
//        提交（apply() 与去抖到期共用）；
//      · 连续控件（bandGains10/31 拖动、preGainDb 拖动）变更先去抖（独立单发
//        QTimer，50ms——EQ 规格，勿与 transition 的 400ms timer 混用）后提交；
//      · 离散控件（enabled/bandMode/limiterEnabled/spectrumEnabled）与预设应用
//        （applyEqPreset）/复位（resetEq）立即提交（不走去抖）；立即项在去抖
//        pending 期间先提交，pending 到期再推全量现值（重复提交同一全量载荷
//        无害——与 transition 档位 setter 撞滑块去抖同款行为）；
//      · 启动路径 reloadFromSettings 后 apply() 顺带推一次 EQ 组包（同 output
//        配置模式，executor 绑定前 no-op；F1.3 在 AppFacade 注入后启动即同步）；
//      · 未绑定（mock-only）时 setter/applyEqualizerConfig/resetEq 全链 no-op
//        （仅持久化，同 transition executor 未绑定先例）。
//  - spectrumBins(60)/curvePoints(181)/curveFrequencies(181) 为订阅镜像属性：
//    纯内存态，由后端推送更新（F1.3 mirrorSpectrumBins/mirrorEqualizerCurve 写者，
//    经 AppFacade 从 BackendBridge 订阅接线），严禁持久化、严禁进入 reloadFromSettings
//    （勿按频谱帧率写存储——更新频率由后端发布驱动，前端不节流不放大）。
//  - 预设：内置 8 款只读 C++ 常量（见 equalizer_presets.h，不可删改/覆盖）+
//    用户预设 CRUD（10/31 两档位独立存储键互不干扰）；eqPresetList() 暴露
//    当前 bandMode 档位的内置+用户合并列表（F2 消费面）。
//
// 设备能力过滤（需求 3 前端）：
//  - sampleRateOptions()/sampleFormatOptions() 按当前选中设备（preferredDeviceId）的
//    能力与标准列表求交；空能力 = 全支持 = 显示全部。
//  - 已保存值不在过滤结果中时保留并标注（「设备不支持」后缀），已保存值本身不变。
class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList playbackDevices READ playbackDevices NOTIFY playbackDevicesChanged)
    Q_PROPERTY(QStringList playbackDeviceNames READ playbackDeviceNames NOTIFY playbackDeviceNamesChanged)
    Q_PROPERTY(QString preferredDeviceId READ preferredDeviceId WRITE setPreferredDeviceId NOTIFY preferredDeviceIdChanged)
    // 当前生效（高亮）输出设备 id：显式选择优先，其次系统默认设备（isDefault），
    // 再无标记项回退列表首台。设备下拉高亮与采样率/位深过滤（selectedDeviceCaps）
    // 共用同一解析，保证"跟随系统默认"时两处一致。
    Q_PROPERTY(QString effectiveDeviceId READ effectiveDeviceId NOTIFY effectiveDeviceIdChanged)
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(int sampleFormat READ sampleFormat WRITE setSampleFormat NOTIFY sampleFormatChanged)
    Q_PROPERTY(int bufferDurationMs READ bufferDurationMs WRITE setBufferDurationMs NOTIFY bufferDurationMsChanged)
    Q_PROPERTY(QStringList lyricDelimiters READ lyricDelimiters WRITE setLyricDelimiters NOTIFY lyricDelimitersChanged)
    Q_PROPERTY(int followRestoreDelayMs READ followRestoreDelayMs WRITE setFollowRestoreDelayMs NOTIFY followRestoreDelayMsChanged)
    Q_PROPERTY(int logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(QVariantList sampleRateOptions READ sampleRateOptions NOTIFY sampleRateOptionsChanged)
    Q_PROPERTY(QVariantList sampleFormatOptions READ sampleFormatOptions NOTIFY sampleFormatOptionsChanged)
    Q_PROPERTY(QVariantList playbackDeviceCapabilities READ playbackDeviceCapabilities NOTIFY playbackDeviceCapabilitiesChanged)
    // 输出设备下拉的对象模型（与 playbackDevices/playbackDeviceNames 同序同长）：
    // 每项 { deviceId, deviceName, isDefault }；QML 经 textRole/valueRole/isDefault
    // 显示名称、按 id 取值、标记并默认高亮系统默认设备。
    Q_PROPERTY(QVariantList outputDeviceOptions READ outputDeviceOptions NOTIFY outputDeviceOptionsChanged)
    Q_PROPERTY(int autoAdvanceFadeMode READ autoAdvanceFadeMode WRITE setAutoAdvanceFadeMode NOTIFY autoAdvanceFadeModeChanged)
    Q_PROPERTY(bool fadeOnTransport READ fadeOnTransport WRITE setFadeOnTransport NOTIFY fadeOnTransportChanged)
    Q_PROPERTY(bool fadeOnSeek READ fadeOnSeek WRITE setFadeOnSeek NOTIFY fadeOnSeekChanged)
    Q_PROPERTY(int gaplessPreloadMs READ gaplessPreloadMs WRITE setGaplessPreloadMs NOTIFY gaplessPreloadMsChanged)
    Q_PROPERTY(int crossfadeMs READ crossfadeMs WRITE setCrossfadeMs NOTIFY crossfadeMsChanged)
    Q_PROPERTY(int transportFadeMs READ transportFadeMs WRITE setTransportFadeMs NOTIFY transportFadeMsChanged)
    Q_PROPERTY(int seekFadeMs READ seekFadeMs WRITE setSeekFadeMs NOTIFY seekFadeMsChanged)
    Q_PROPERTY(int manualAdvanceFadeMode READ manualAdvanceFadeMode WRITE setManualAdvanceFadeMode NOTIFY manualAdvanceFadeModeChanged)
    Q_PROPERTY(int manualShortCrossfadeMs READ manualShortCrossfadeMs WRITE setManualShortCrossfadeMs NOTIFY manualShortCrossfadeMsChanged)
    Q_PROPERTY(int transitionSliderStepMs READ transitionSliderStepMs CONSTANT)
    // 均衡器组（键组 "equalizer"）：7 个用户可设键，变更即持久化（F1.2 提交语义
    // 见类头注释：enabled/bandMode/limiter/spectrum 立即推送，bandGains/preGain 50ms 去抖）；
    // bandGains 写入防御归一化（±15dB 钳位 + 定长 10/31 补零/截断），bandMode/preGain 越界拒绝
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int bandMode READ bandMode WRITE setBandMode NOTIFY bandModeChanged)
    Q_PROPERTY(double preGainDb READ preGainDb WRITE setPreGainDb NOTIFY preGainDbChanged)
    Q_PROPERTY(QVariantList bandGains10 READ bandGains10 WRITE setBandGains10 NOTIFY bandGains10Changed)
    Q_PROPERTY(QVariantList bandGains31 READ bandGains31 WRITE setBandGains31 NOTIFY bandGains31Changed)
    Q_PROPERTY(bool limiterEnabled READ limiterEnabled WRITE setLimiterEnabled NOTIFY limiterEnabledChanged)
    Q_PROPERTY(bool spectrumEnabled READ spectrumEnabled WRITE setSpectrumEnabled NOTIFY spectrumEnabledChanged)
    // 订阅镜像（纯内存态，后端推送更新；F1.3 起由 mirror* 写者驱动）——绝不落盘、不进 reload
    Q_PROPERTY(QVariantList spectrumBins READ spectrumBins NOTIFY spectrumBinsChanged)
    Q_PROPERTY(QVariantList curvePoints READ curvePoints NOTIFY curvePointsChanged)
    Q_PROPERTY(QVariantList curveFrequencies READ curveFrequencies NOTIFY curveFrequenciesChanged)
    // 当前档位合并预设列表（内置 8 款 builtin=true 只读 + 用户预设）；bandMode/CRUD 变化时 NOTIFY
    Q_PROPERTY(QVariantList eqPresetList READ eqPresetList NOTIFY eqPresetListChanged)
    QML_ELEMENT
    QML_UNCREATABLE("SettingsController is owned by AppFacade")

public:
    using ApplyOutputConfigExecutor =
        std::function<void(int outputMode, int sampleRate, int sampleFormat, int bufferDurationMs, const QString &preferredDeviceId)>;
    using ApplyTransitionConfigExecutor = std::function<void(int autoAdvanceFadeMode,
                                                              bool fadeOnTransport,
                                                              bool fadeOnSeek,
                                                              int gaplessPreloadMs,
                                                              int crossfadeMs,
                                                              int transportFadeMs,
                                                              int seekFadeMs,
                                                              int manualAdvanceFadeMode,
                                                              int manualShortCrossfadeMs)>;
    // EQ 提交载荷 = 当前 EQ 设置的全量组包（无部分状态）：enabled/bandMode/preGainDb
    // + 当前档位增益数组（bandMode 10/31 取一档、定长 10/31）+ limiterEnabled/spectrumEnabled；
    // bandGains 由组包方按 m_bandMode 择一传入，消费方（F1.3 BackendBridge::submitEqualizerConfig）
    // 不需感知另一档。spectrumEnabled 非后端 EqualizerConfig 字段（独立接口方法，命令面
    // 无 spectrum 命令）——通道裁定与拆分落点见 backend_bridge.h submitEqualizerConfig 注释。
    using ApplyEqualizerConfigExecutor = std::function<void(bool enabled,
                                                            int bandMode,
                                                            double preGainDb,
                                                            const QVariantList &bandGains,
                                                            bool limiterEnabled,
                                                            bool spectrumEnabled)>;
    using EnumerateDevicesExecutor = std::function<QList<PlaybackDeviceCapabilities>()>;
    using LogLevelExecutor = std::function<void(int level)>;

    explicit SettingsController(QObject *parent = nullptr);

    // 日志等级：int 值与 spdlog::level::level_enum 一致（trace=0 .. critical=5），
    // 经 LogLevelExecutor 同步后端（seriona::app::setLogLevel）。纯 QML 面不依赖
    // 后端头文件，映射（字符串↔枚举）在前端完成。
    int logLevel() const;
    void setLogLevel(int level);
    static int logLevelFromString(const QString &name);
    static QString logLevelToString(int level);
    Q_INVOKABLE void applyLogLevel();

    QStringList playbackDevices() const;
    QStringList playbackDeviceNames() const;

    QString preferredDeviceId() const;
    void setPreferredDeviceId(const QString &deviceId);
    QString effectiveDeviceId() const;

    int sampleRate() const;
    void setSampleRate(int sampleRate);

    int sampleFormat() const;
    void setSampleFormat(int sampleFormat);

    int bufferDurationMs() const;
    void setBufferDurationMs(int bufferDurationMs);

    // 播放过渡设置（键组 "transition"，默认/量程见裁定表与 cpp 常量区；
    // 值域与后端 reducer 校验一致，T12 经 m_applyTransitionConfigExecutor 下发）。
    int autoAdvanceFadeMode() const;
    void setAutoAdvanceFadeMode(int mode);
    bool fadeOnTransport() const;
    void setFadeOnTransport(bool enabled);
    bool fadeOnSeek() const;
    void setFadeOnSeek(bool enabled);
    int gaplessPreloadMs() const;
    void setGaplessPreloadMs(int ms);
    int crossfadeMs() const;
    void setCrossfadeMs(int ms);
    int transportFadeMs() const;
    void setTransportFadeMs(int ms);
    int seekFadeMs() const;
    void setSeekFadeMs(int ms);
    int manualAdvanceFadeMode() const;
    void setManualAdvanceFadeMode(int mode);
    int manualShortCrossfadeMs() const;
    void setManualShortCrossfadeMs(int ms);

    // 过渡滑块步进（100ms）：QML from/to/stepSize 与此常量对齐。
    int transitionSliderStepMs() const;

    QStringList lyricDelimiters() const;
    void setLyricDelimiters(const QStringList &delimiters);

    // 歌词跟随恢复延迟（毫秒）：纯本地项，仅持久化不推送；QML 歌词容器绑定为恢复计时器 interval。
    int followRestoreDelayMs() const;
    void setFollowRestoreDelayMs(int delayMs);

    // 均衡器设置（键组 "equalizer"，默认/量程/预设表见 equalizer_presets.h 与 cpp
    // 常量区；值域：bandMode 10/31、preGainDb ±15、bandGains 逐项 ±15）。
    // 提交语义（F1.2）：setter 落盘即时；enabled/bandMode/limiterEnabled/
    // spectrumEnabled 变更立即推送，preGainDb/bandGains10/bandGains31 变更 50ms
    // 去抖后推送；executor 未绑定（mock-only）时推送为 no-op。
    bool enabled() const;
    void setEnabled(bool enabled);
    int bandMode() const;
    void setBandMode(int bandMode);
    double preGainDb() const;
    void setPreGainDb(double gainDb);
    QVariantList bandGains10() const;
    void setBandGains10(const QVariantList &gains);
    QVariantList bandGains31() const;
    void setBandGains31(const QVariantList &gains);
    bool limiterEnabled() const;
    void setLimiterEnabled(bool enabled);
    bool spectrumEnabled() const;
    void setSpectrumEnabled(bool enabled);

    // 订阅镜像只读面（内存态，由 mirror* 写者更新——仅 AppFacade 注入链调用，
    // 禁止持久化/推送/进 reload）
    QVariantList spectrumBins() const;
    QVariantList curvePoints() const;
    QVariantList curveFrequencies() const;
    // 镜像写者（C++/AppFacade 面，后端推送驱动；F1.3）：把 BackendBridge 订阅快照
    // 转成的 QVariantList（double）落到对应镜像成员并 NOTIFY（与现值相同则跳过）。
    // 定长契约：curvePoints/curveFrequencies 各 181 点、spectrumBins 60 桶；长度不符
    // 的推送丢弃（后端 std::array 定长，正常不可达，防御异常数据破坏 QML 消费）。
    // 更新频率 = 后端发布频率（EQ 命令生效帧 / 频谱驻留节流帧），前端不节流不放大。
    void mirrorEqualizerCurve(const QVariantList &curvePoints, const QVariantList &curveFrequencies);
    void mirrorSpectrumBins(const QVariantList &bins);

    // 当前 bandMode 档位的合并预设列表（每项
    // { id, name, builtin, preGainDb, gains }；内置 8 款恒在前，builtin=true 只读）
    QVariantList eqPresetList() const;

    // 用户预设 CRUD（QML 面；10/31 档位独立存储互不干扰，操作落当前 bandMode 档位；
    // 内置预设不可删改/重命名/覆盖——传内置 id 一律返回 false 不动作）。
    // addEqPreset：以当前 bandMode 的增益/preGain 存档；addEqPresetWithValues：指定值
    // （gains 按当前档位长度归一化，preGainDb 越界钳位）。
    Q_INVOKABLE bool addEqPreset(const QString &name);
    Q_INVOKABLE bool addEqPresetWithValues(const QString &name, const QVariantList &gains, double preGainDb);
    Q_INVOKABLE bool renameEqPreset(const QString &presetId, const QString &newName);
    Q_INVOKABLE bool deleteEqPreset(const QString &presetId);
    // 应用预设（内置按当前档位取对应数组/用户按档位存储值）→ 落值（内部 setter +
    // 即时持久化，仅实际变更的键落盘）；预设 = 立即项：有任一键实际变更时立即提交
    // 一次全量组包（不走去抖；值未变 = 无动作无推送）。
    Q_INVOKABLE bool applyEqPreset(const QString &presetId);
    // 复位均衡器为平坦（enabled/bandMode/limiterEnabled/spectrumEnabled 保持，两档
    // bandGains 全 0.0、preGainDb 0.0，变更键即时持久化）；复位 = 立即项：任一值
    // 实际变更时立即提交一次；已全平则 no-op 仍返回 true。F2 UI 复位按钮消费面。
    Q_INVOKABLE bool resetEq();

    QVariantList sampleRateOptions() const;
    QVariantList sampleFormatOptions() const;
    QVariantList playbackDeviceCapabilities() const;
    QVariantList outputDeviceOptions() const;

    // 后端协商结果落地：只更新属性（含 NOTIFY），不持久化、不推送。
    void setDefaults(int sampleRate, int bufferDurationMs, const QString &preferredDeviceId);

    // 设置存储后端注入（AppFacade 接入后端时注入 BackendBridge 实现；
    // 不注入时回退内存存储）。
    void setSettingsStorageBackend(AppSettingsBackend backend);

    // 应用设置读取 → 属性（不推送）。
    Q_INVOKABLE void reloadFromSettings();
    // 组装当前属性并提交后端 ConfigureOutput 命令。
    Q_INVOKABLE void apply();
    // 组装当前 9 项过渡属性并提交后端过渡命令（未绑定 executor = no-op）。
    // 档位/开关 setter 立即调用；滑块 setter 经 400ms 去抖后调用。
    void applyTransitionConfig();
    // 后端拒绝 ConfigureOutput 时恢复上一次已提交的合法值快照（emit NOTIFY，不持久化、不推送）。
    Q_INVOKABLE void rollbackRejectedOutputConfig();
    // 枚举后端输出设备 → playbackDevices。
    Q_INVOKABLE void enumerateDevices();

    void setApplyOutputConfigExecutor(ApplyOutputConfigExecutor executor);
    void setApplyTransitionConfigExecutor(ApplyTransitionConfigExecutor executor);
    void setApplyEqualizerConfigExecutor(ApplyEqualizerConfigExecutor executor);
    void setEnumerateDevicesExecutor(EnumerateDevicesExecutor executor);
    void setLogLevelExecutor(LogLevelExecutor executor);

signals:
    void playbackDevicesChanged();
    void playbackDeviceNamesChanged();
    void preferredDeviceIdChanged();
    void effectiveDeviceIdChanged();
    void sampleRateChanged();
    void sampleFormatChanged();
    void bufferDurationMsChanged();
    void lyricDelimitersChanged();
    void followRestoreDelayMsChanged();
    void logLevelChanged();
    void autoAdvanceFadeModeChanged();
    void fadeOnTransportChanged();
    void fadeOnSeekChanged();
    void gaplessPreloadMsChanged();
    void crossfadeMsChanged();
    void transportFadeMsChanged();
    void seekFadeMsChanged();
    void manualAdvanceFadeModeChanged();
    void manualShortCrossfadeMsChanged();
    void sampleRateOptionsChanged();
    void sampleFormatOptionsChanged();
    void playbackDeviceCapabilitiesChanged();
    void outputDeviceOptionsChanged();
    void enabledChanged();
    void bandModeChanged();
    void preGainDbChanged();
    void bandGains10Changed();
    void bandGains31Changed();
    void limiterEnabledChanged();
    void spectrumEnabledChanged();
    void spectrumBinsChanged();
    void curvePointsChanged();
    void curveFrequenciesChanged();
    void eqPresetListChanged();

private:
    void setSampleRateInternal(int sampleRate);
    void setSampleFormatInternal(int sampleFormat);
    void setBufferDurationMsInternal(int bufferDurationMs);
    void setPreferredDeviceIdInternal(const QString &deviceId);
    void setLyricDelimitersInternal(const QStringList &delimiters);
    void setFollowRestoreDelayMsInternal(int delayMs);
    void setLogLevelInternal(int level);
    void setAutoAdvanceFadeModeInternal(int mode);
    void setFadeOnTransportInternal(bool enabled);
    void setFadeOnSeekInternal(bool enabled);
    void setGaplessPreloadMsInternal(int ms);
    void setCrossfadeMsInternal(int ms);
    void setTransportFadeMsInternal(int ms);
    void setSeekFadeMsInternal(int ms);
    void setManualAdvanceFadeModeInternal(int mode);
    void setManualShortCrossfadeMsInternal(int ms);
    void setEnabledInternal(bool enabled);
    void setBandModeInternal(int bandMode);
    void setPreGainDbInternal(double gainDb);
    void setBandGains10Internal(const QVariantList &gains);
    void setBandGains31Internal(const QVariantList &gains);
    void setLimiterEnabledInternal(bool enabled);
    void setSpectrumEnabledInternal(bool enabled);
    void setUserPresetsInternal(const QVariantList &presets10, const QVariantList &presets31);
    void persistValue(const char *group, const char *key, const QVariant &value);
    void persistOutputValue(const char *key, const QVariant &value);
    void persistTransitionValue(const char *key, const QVariant &value);
    void persistEqValue(const char *key, const QVariant &value);
    void persistEqUserPresets(const QVariantList &presets);
    QVariantList &eqUserPresetsSlot();
    const QVariantList &eqUserPresets() const;
    void scheduleDebouncedApply();
    void scheduleDebouncedTransitionApply();
    void scheduleDebouncedEqualizerApply();
    // 组包当前全量 EQ 设置并提交（enabled/bandMode/preGainDb/当前档位增益数组/
    // limiterEnabled/spectrumEnabled）；离散 setter 与 applyEqPreset/resetEq 立即调用，
    // bandGains/preGain setter 经 50ms 去抖后调用，apply() 启动路径顺带调用。
    // 未绑定 m_applyEqualizerConfigExecutor = no-op（mock-only 安全）。
    void applyEqualizerConfig();
    void recordLastValidSnapshot();
    const PlaybackDeviceCapabilities *selectedDeviceCaps() const;
    QVariantList buildOptions(const QList<int> &standardValues,
                              const QList<int> &supportedValues,
                              int savedValue,
                              bool isSampleRate) const;
    QString sampleRateLabel(int value) const;
    QString sampleFormatLabel(int value) const;

    QStringList m_playbackDevices;
    QStringList m_playbackDeviceNames;
    QString m_preferredDeviceId;
    int m_sampleRate = 48000;
    int m_sampleFormat = 0;
    int m_bufferDurationMs = 300;
    QStringList m_lyricDelimiters = {QStringLiteral(" / ")};
    int m_followRestoreDelayMs = 5000;
    int m_logLevel = 2;
    int m_autoAdvanceFadeMode = 0;   // 无
    bool m_fadeOnTransport = false;  // 关
    bool m_fadeOnSeek = false;       // 关
    int m_gaplessPreloadMs = 0;
    int m_crossfadeMs = 3000;
    int m_transportFadeMs = 300;
    int m_seekFadeMs = 300;
    int m_manualAdvanceFadeMode = 0; // 无
    int m_manualShortCrossfadeMs = 500;
    // 均衡器组（键组 "equalizer"）：bandGains 初始全 0 在 ctor 填充（10/31 定长）
    bool m_enabled = false;
    int m_bandMode = kEqBandMode10;
    double m_preGainDb = 0.0;
    QVariantList m_bandGains10;
    QVariantList m_bandGains31;
    bool m_limiterEnabled = false;
    bool m_spectrumEnabled = false;
    // 订阅镜像（内存态；F1.3 起由 mirror* 写者更新，AppFacade 注入链驱动；严禁持久化）
    QVariantList m_spectrumBins;
    QVariantList m_curvePoints;
    QVariantList m_curveFrequencies;
    // 用户预设（10/31 独立存储；每项 { id, name, preGainDb, gains }，内置恒不进此表）
    QVariantList m_userPresets10;
    QVariantList m_userPresets31;
    int m_lastValidSampleRate = 48000;
    int m_lastValidSampleFormat = 0;
    int m_lastValidBufferDurationMs = 300;
    QString m_lastValidPreferredDeviceId;
    QList<PlaybackDeviceCapabilities> m_deviceCapabilities;
    bool m_hasCommittedSnapshot = false;
    QTimer m_debounceTimer;
    QTimer m_transitionDebounceTimer;
    // 均衡器连续参数去抖（50ms，EQ 规格；独立于 output 400ms / transition 400ms timer）
    QTimer m_eqDebounceTimer;
    AppSettingsStorage m_settingsStorage;
    ApplyOutputConfigExecutor m_applyOutputConfigExecutor;
    ApplyTransitionConfigExecutor m_applyTransitionConfigExecutor;
    // F1.2：EQ 推送 executor（全量组包 6 形参）；AppFacade 接入后端时注入（F1.3 已接线）
    // ——未绑定（默认空 std::function）时各提交点按 bool 判空直接 no-op（mock-only 安全）
    ApplyEqualizerConfigExecutor m_applyEqualizerConfigExecutor;
    EnumerateDevicesExecutor m_enumerateDevicesExecutor;
    LogLevelExecutor m_logLevelExecutor;
};

}
