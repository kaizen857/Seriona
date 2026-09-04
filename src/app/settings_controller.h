#pragma once

#include "app_settings_storage.h"

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
struct PlaybackDeviceCapabilities {
    QString deviceId;
    QString deviceName;
    QList<int> sampleFormats{};
    QList<int> sampleRates{};
};

// 输出设置控制器：应用设置存储（默认内存，注入后经后端键值存储）持久化 +
// 后端 ConfigureOutput 透传。
// 本类为纯 QML 面，不依赖后端头文件/宏；与后端通信经 AppFacade 注入的 executor
// （BackendBridge::submitConfigureOutput / enumeratePlaybackDeviceCapabilities）。
//
// 推送策略（输出组）：
//  - 离散控件（outputMode / sampleRate / sampleFormat / preferredDeviceId）变更立即推送；
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
//  - advanceTransitionsGreyed()：直接输出模式（outputMode==0）下过渡类行
//    {1 自动档, 4 预加载, 5 交叉长度, 8 手动档, 9 手动短交叉} 灰化；
//    {2,3,6,7}（传送类）全局可用（需求：交叉/预加载/手动档仅 Mixed 生效）。
//
// 设备能力过滤（需求 3 前端）：
//  - sampleRateOptions()/sampleFormatOptions() 按当前选中设备（preferredDeviceId）的
//    能力与标准列表求交；空能力 = 全支持 = 显示全部。
//  - 已保存值不在过滤结果中时保留并标注（「设备不支持」后缀），已保存值本身不变。
//  - sampleParamsGreyed()：直接输出模式（outputMode==0）下采样率/位深行灰化（需求 1）。
class SettingsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int outputMode READ outputMode WRITE setOutputMode NOTIFY outputModeChanged)
    Q_PROPERTY(QStringList playbackDevices READ playbackDevices NOTIFY playbackDevicesChanged)
    Q_PROPERTY(QStringList playbackDeviceNames READ playbackDeviceNames NOTIFY playbackDeviceNamesChanged)
    Q_PROPERTY(QString preferredDeviceId READ preferredDeviceId WRITE setPreferredDeviceId NOTIFY preferredDeviceIdChanged)
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(int sampleFormat READ sampleFormat WRITE setSampleFormat NOTIFY sampleFormatChanged)
    Q_PROPERTY(int bufferDurationMs READ bufferDurationMs WRITE setBufferDurationMs NOTIFY bufferDurationMsChanged)
    Q_PROPERTY(QStringList lyricDelimiters READ lyricDelimiters WRITE setLyricDelimiters NOTIFY lyricDelimitersChanged)
    Q_PROPERTY(int followRestoreDelayMs READ followRestoreDelayMs WRITE setFollowRestoreDelayMs NOTIFY followRestoreDelayMsChanged)
    Q_PROPERTY(int logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(bool sampleParamsGreyed READ sampleParamsGreyed NOTIFY outputModeChanged)
    Q_PROPERTY(QVariantList sampleRateOptions READ sampleRateOptions NOTIFY sampleRateOptionsChanged)
    Q_PROPERTY(QVariantList sampleFormatOptions READ sampleFormatOptions NOTIFY sampleFormatOptionsChanged)
    Q_PROPERTY(QVariantList playbackDeviceCapabilities READ playbackDeviceCapabilities NOTIFY playbackDeviceCapabilitiesChanged)
    Q_PROPERTY(int autoAdvanceFadeMode READ autoAdvanceFadeMode WRITE setAutoAdvanceFadeMode NOTIFY autoAdvanceFadeModeChanged)
    Q_PROPERTY(bool fadeOnTransport READ fadeOnTransport WRITE setFadeOnTransport NOTIFY fadeOnTransportChanged)
    Q_PROPERTY(bool fadeOnSeek READ fadeOnSeek WRITE setFadeOnSeek NOTIFY fadeOnSeekChanged)
    Q_PROPERTY(int gaplessPreloadMs READ gaplessPreloadMs WRITE setGaplessPreloadMs NOTIFY gaplessPreloadMsChanged)
    Q_PROPERTY(int crossfadeMs READ crossfadeMs WRITE setCrossfadeMs NOTIFY crossfadeMsChanged)
    Q_PROPERTY(int transportFadeMs READ transportFadeMs WRITE setTransportFadeMs NOTIFY transportFadeMsChanged)
    Q_PROPERTY(int seekFadeMs READ seekFadeMs WRITE setSeekFadeMs NOTIFY seekFadeMsChanged)
    Q_PROPERTY(int manualAdvanceFadeMode READ manualAdvanceFadeMode WRITE setManualAdvanceFadeMode NOTIFY manualAdvanceFadeModeChanged)
    Q_PROPERTY(int manualShortCrossfadeMs READ manualShortCrossfadeMs WRITE setManualShortCrossfadeMs NOTIFY manualShortCrossfadeMsChanged)
    Q_PROPERTY(bool advanceTransitionsGreyed READ advanceTransitionsGreyed NOTIFY outputModeChanged)
    Q_PROPERTY(int transitionSliderStepMs READ transitionSliderStepMs CONSTANT)
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

    int outputMode() const;
    void setOutputMode(int mode);

    QStringList playbackDevices() const;
    QStringList playbackDeviceNames() const;

    QString preferredDeviceId() const;
    void setPreferredDeviceId(const QString &deviceId);

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

    // 直接输出模式（outputMode==0）下过渡类行（自动档/预加载/交叉长度/手动档/
    // 手动短交叉）灰化；随 outputModeChanged 翻转（与 sampleParamsGreyed 同范式）。
    bool advanceTransitionsGreyed() const;
    // 过渡滑块步进（100ms）：QML from/to/stepSize 与此常量对齐。
    int transitionSliderStepMs() const;

    QStringList lyricDelimiters() const;
    void setLyricDelimiters(const QStringList &delimiters);

    // 歌词跟随恢复延迟（毫秒）：纯本地项，仅持久化不推送；QML 歌词容器绑定为恢复计时器 interval。
    int followRestoreDelayMs() const;
    void setFollowRestoreDelayMs(int delayMs);

    bool sampleParamsGreyed() const;
    QVariantList sampleRateOptions() const;
    QVariantList sampleFormatOptions() const;
    QVariantList playbackDeviceCapabilities() const;

    // 后端协商结果落地：只更新属性（含 NOTIFY），不持久化、不推送。
    void setDefaults(int outputMode, int sampleRate, int bufferDurationMs, const QString &preferredDeviceId);

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
    void setEnumerateDevicesExecutor(EnumerateDevicesExecutor executor);
    void setLogLevelExecutor(LogLevelExecutor executor);

signals:
    void outputModeChanged();
    void playbackDevicesChanged();
    void playbackDeviceNamesChanged();
    void preferredDeviceIdChanged();
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

private:
    void setOutputModeInternal(int mode);
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
    void persistValue(const char *group, const char *key, const QVariant &value);
    void persistOutputValue(const char *key, const QVariant &value);
    void persistTransitionValue(const char *key, const QVariant &value);
    void scheduleDebouncedApply();
    void scheduleDebouncedTransitionApply();
    void recordLastValidSnapshot();
    const PlaybackDeviceCapabilities *selectedDeviceCaps() const;
    QVariantList buildOptions(const QList<int> &standardValues,
                              const QList<int> &supportedValues,
                              int savedValue,
                              bool isSampleRate) const;
    QString sampleRateLabel(int value) const;
    QString sampleFormatLabel(int value) const;

    int m_outputMode = 0;
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
    int m_lastValidOutputMode = 0;
    int m_lastValidSampleRate = 48000;
    int m_lastValidSampleFormat = 0;
    int m_lastValidBufferDurationMs = 300;
    QString m_lastValidPreferredDeviceId;
    QList<PlaybackDeviceCapabilities> m_deviceCapabilities;
    bool m_hasCommittedSnapshot = false;
    QTimer m_debounceTimer;
    QTimer m_transitionDebounceTimer;
    AppSettingsStorage m_settingsStorage;
    ApplyOutputConfigExecutor m_applyOutputConfigExecutor;
    ApplyTransitionConfigExecutor m_applyTransitionConfigExecutor;
    EnumerateDevicesExecutor m_enumerateDevicesExecutor;
    LogLevelExecutor m_logLevelExecutor;
};

}
