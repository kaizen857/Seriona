#pragma once

#include <QObject>
#include <QPair>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "settings_controller.h"

#ifndef SERIONA_HAS_BACKEND
#define SERIONA_HAS_BACKEND 0
#endif

#if SERIONA_HAS_BACKEND
#include "seriona/control/media_controller.h"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#endif

namespace Seriona::App {

class BackendBridge : public QObject
{
    Q_OBJECT

public:
#if SERIONA_HAS_BACKEND
    using ControllerFactory = std::function<std::unique_ptr<seriona::control::MediaController>()>;

    explicit BackendBridge(ControllerFactory controllerFactory, QObject *parent = nullptr);
#endif
    explicit BackendBridge(QObject *parent = nullptr);
    ~BackendBridge() override;

    void start();
    void shutdown();
    bool started() const;
    bool shuttingDown() const;
    void drainForTests();

#if SERIONA_HAS_BACKEND
    seriona::control::MediaControllerCommandResult submitCommand(const seriona::control::MediaControlCommand &command);
    seriona::control::MediaControllerCommandResult scanLibrary(
        const QString &rootPath,
        seriona::scanner::ScanMode mode = seriona::scanner::ScanMode::Full);
    seriona::control::MediaControllerCommandResult applyFolderSortRules(
        const QString &rootPath,
        const QString &folderNodeId,
        const QVariantList &rules);
    seriona::control::MediaControllerCommandResult submitConfigureOutput(
        int outputMode,
        int sampleRate,
        int sampleFormat,
        int bufferDurationMs,
        const QString &preferredDeviceId);
    // 提交播放过渡参数（T12）：9 参按 TransitionConfig 契约顺序组包 → SetTransitionConfig
    // 命令；数值域与后端 reducer 校验一致（枚举 0-2、crossfade 0-10000、传送/seek/手动短
    // 交叉 0-3000、预加载 0-5000），越界本地拒绝并告警日志，绝不外发非法命令。
    // 参数顺序即跨端契约（audio_contracts.h TransitionConfig 字段声明顺序），勿调。
    seriona::control::MediaControllerCommandResult submitTransitionConfig(
        int autoAdvanceFadeMode,
        bool fadeOnTransport,
        bool fadeOnSeek,
        int gaplessPreloadMs,
        int crossfadeMs,
        int transportFadeMs,
        int seekFadeMs,
        int manualAdvanceFadeMode,
        int manualShortCrossfadeMs);
    // 提交均衡器参数（F1.3）：6 参 = SettingsController::ApplyEqualizerConfigExecutor
    // 载荷（enabled / bandMode 10|31 / preGainDb ±15 / 当前档位定长增益数组 10|31 /
    // limiterEnabled / spectrumEnabled）。数值域与后端 reducer handleSetEqualizerConfig
    // 校验一致（读到即镜像），越界本地 reject（warn + CommandRejected 通知）后 return，
    // 绝不外发非法命令；组包 = audio::EqualizerConfig（字段声明顺序即跨端契约）→
    // SetEqualizerConfig 命令（与 ConfigureOutput/SetTransitionConfig 语义隔离：
    // 仅更新均衡参数，绝不触发输出重载/设备生命周期）。
    // spectrumEnabled 通道裁定：后端 EqualizerConfig 载荷无 spectrum 字段、MediaController
    // 命令面 23 种命令无 spectrum 命令（setSpectrumEnabled 是 AudioPlaybackService 独立
    // 虚方法，控制层/订阅面无写者通道）→ 本桥层仅缓存期望值并如实 no-op（开启沿
    // warn 一次，不伪造命令不静默吞）；频谱功能待后端命令面接线。UI 开关持久化
    // （settings_controller）不受影响；频谱订阅面收默认空快照（generation 0），
    // UI 侧由 F2 呈现空态。
    seriona::control::MediaControllerCommandResult submitEqualizerConfig(
        bool enabled,
        int bandMode,
        double preGainDb,
        const QVariantList &bandGains,
        bool limiterEnabled,
        bool spectrumEnabled);
    // 删除目标（T16）：path 为绝对路径（单曲=音频文件，folder=true=递归删除文件夹）。
    // 字段契约与 T8 定死一致：MediaControlCommand.kind ∈ {DeleteTrack, DeleteFolder}，
    // targetPath 为后端绝对路径。失败经现有 CommandRejected 通知链路反馈原因。
    seriona::control::MediaControllerCommandResult deleteTarget(const QString &path, bool folder);
    // 添加到下一首播放（T14）：PlayNextTrack 命令负载按 T7 契约传 track.trackId，
    // 后端将其解析入队首（快照 queueEntries: [{trackId, nodeId}]，nodeId 由后端填充）。
    seriona::control::MediaControllerCommandResult playNextTrack(const QString &trackId);
    // 从临时队列移除（T14）：RemoveFromQueue 命令按 queueEntries 下标移除。
    seriona::control::MediaControllerCommandResult removeFromQueue(quint64 queueIndex);
    QList<QPair<QString, QString>> enumeratePlaybackDevices();
    QList<PlaybackDeviceCapabilities> enumeratePlaybackDeviceCapabilities();
    void setLogLevel(int level);
    // 前端应用设置读写（QVariant ↔ JSON 字符串经后端 app_settings 表持久化）；
    // 读取返回 nullopt 表示后端不可用/未存储（由 AppSettingsStorage 回退内存缓存），
    // 写入/删除失败返回 false。
    std::optional<QVariant> getAppSetting(const QString &group, const QString &key, const QVariant &defaultValue);
    bool setAppSetting(const QString &group, const QString &key, const QVariant &value);
    bool removeAppSetting(const QString &group, const QString &key);
    const seriona::control::PlayerStateSnapshot &playerSnapshot() const;
    const seriona::control::LibraryStateSnapshot &librarySnapshot() const;
    const seriona::audio::EqualizerStateSnapshot &equalizerStateSnapshot() const;
    const seriona::audio::SpectrumSnapshot &spectrumSnapshot() const;
    const std::deque<seriona::control::ControlDomainNotification> &notifications() const;
#endif

signals:
    void startedChanged();
    void shutdownCompleted();
    void playerSnapshotChanged();
    void librarySnapshotChanged();
    // F1.3：均衡器状态/频谱快照落地通知（AppFacade 据此把快照镜像到
    // SettingsController 的 curvePoints/curveFrequencies/spectrumBins 属性）。
    // 频谱订阅面后端暂无写者：spectrumChanged 只在收默认空快照时触发一次。
    void equalizerStateChanged();
    void spectrumChanged();
    void domainNotificationQueued();

private:
    Q_DISABLE_COPY_MOVE(BackendBridge)

#if SERIONA_HAS_BACKEND
    static ControllerFactory defaultControllerFactory();
    static seriona::control::MediaControllerCommandResult controllerStoppedResult();
    static void unsubscribe(seriona::control::SubscriptionHandle &handle);

    void registerSubscriptions();
    void unsubscribeAll();
    void submitShutdownStop();
    void applyPlayerSnapshot(seriona::control::PlayerStateSnapshot snapshot);
    void applyLibrarySnapshot(seriona::control::LibraryStateSnapshot snapshot);
    void applyEqualizerStateSnapshot(seriona::audio::EqualizerStateSnapshot snapshot);
    void applySpectrumSnapshot(seriona::audio::SpectrumSnapshot snapshot);
    void enqueueCommandFailureNotification(const seriona::control::MediaControllerCommandResult &result);
    void enqueueNotification(seriona::control::ControlDomainNotification notification);

    ControllerFactory m_controllerFactory;
    std::unique_ptr<seriona::control::MediaController> m_controller;
    seriona::control::SubscriptionHandle m_playerSubscription;
    seriona::control::SubscriptionHandle m_librarySubscription;
    seriona::control::SubscriptionHandle m_notificationSubscription;
    seriona::control::SubscriptionHandle m_equalizerSubscription;
    seriona::control::SubscriptionHandle m_spectrumSubscription;
    seriona::control::PlayerStateSnapshot m_playerSnapshot;
    seriona::control::LibraryStateSnapshot m_librarySnapshot;
    seriona::audio::EqualizerStateSnapshot m_equalizerSnapshot;
    seriona::audio::SpectrumSnapshot m_spectrumSnapshot;
    std::deque<seriona::control::ControlDomainNotification> m_notifications;
    // F1.3 spectrum 通道裁定（方案 a）：无后端命令通道时的期望值缓存——submitEqualizerConfig
    // 的 spectrumEnabled 只落到这里（开启沿 warn 一次），不伪造命令；后端命令面接线后可
    // 从此缓存回读生效。UI 开关持久化不受影响。
    bool m_spectrumEnabledRequested = false;
#endif
    bool m_started = false;
    bool m_shuttingDown = false;
};

}
