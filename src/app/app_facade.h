#pragma once

#include "library_model.h"
#include "lyrics_model.h"
#include "navigation_controller.h"
#include "notification_controller.h"
#include "playback_controller.h"
#include "settings_controller.h"
#include "track_stats_controller.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>

#include <cstddef>
#include <memory>

#ifndef SERIONA_HAS_BACKEND
#define SERIONA_HAS_BACKEND 0
#endif

#if SERIONA_HAS_BACKEND
#include "seriona/control/control_contracts.h"
#endif

namespace Seriona::App {

class BackendBridge;
#if SERIONA_HAS_BACKEND
class WaveformProvider;
#endif

class AppFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString layerName READ layerName CONSTANT)
    Q_PROPERTY(bool foundationReady READ foundationReady CONSTANT)
    Q_PROPERTY(PlaybackController *playback READ playback CONSTANT)
    Q_PROPERTY(LibraryController *library READ library CONSTANT)
    Q_PROPERTY(LyricsModel *lyrics READ lyrics CONSTANT)
    Q_PROPERTY(NotificationController *notifications READ notifications CONSTANT)
    Q_PROPERTY(NavigationController *navigation READ navigation CONSTANT)
    Q_PROPERTY(SettingsController *settings READ settings CONSTANT)
    Q_PROPERTY(TrackStatsController *trackStats READ trackStats CONSTANT)
    QML_ELEMENT

public:
    explicit AppFacade(QObject *parent = nullptr);
    ~AppFacade() override;

    QString layerName() const;
    bool foundationReady() const;
    PlaybackController *playback();
    LibraryController *library();
    LyricsModel *lyrics();
    NotificationController *notifications();
    NavigationController *navigation();
    SettingsController *settings();
    TrackStatsController *trackStats();
    bool backendBridgeStartedForTests() const;
    std::size_t backendNotificationCountForTests() const;
#if SERIONA_HAS_BACKEND
    void applyPlayerSnapshotForTests(
        const seriona::control::PlayerStateSnapshot &player,
        const seriona::control::LibraryStateSnapshot &library);
    void applyLibrarySnapshotForTests(
        const seriona::control::PlayerStateSnapshot &player,
        const seriona::control::LibraryStateSnapshot &library);
#endif

    Q_INVOKABLE void shutdown();
    Q_INVOKABLE bool scanLibrary(const QUrl &rootUrl);
    Q_INVOKABLE bool restorePlaylistFromStartup();
    // 删除确认后的命令入口（T16）：path 为绝对路径，folder=true 时递归删除文件夹。
    // 仅应在确认弹窗确认后调用；成功后经 NotificationController 反馈 toast，
    // 失败原因经既有 CommandRejected 通知链路展示。
    Q_INVOKABLE bool deleteTarget(const QString &path, bool folder);
    // 添加到下一首播放（T14 右键菜单）：经后端 PlayNextTrack 命令（T7）把该曲目入队首。
    // 成功 toast 提示；mock-only 下走本地不支持反馈，不伪造命令。
    Q_INVOKABLE bool playNextTrack(const QString &trackId);
    // 从临时队列移除（T14 菜单命令层）：RemoveFromQueue 按 queueEntries 下标移除。
    Q_INVOKABLE bool removeFromQueue(quint64 queueIndex);
    // 详情窗口路径（T14）：按 nodeId 返回条目绝对路径——歌曲=音频文件，文件夹=完整目录；
    // 未知/无法可靠重建（cue 容器等）节点返回空。删除链（deleteTarget）与详情展示共用。
    Q_INVOKABLE QString filePathForNodeId(const QString &nodeId);

private:
#if SERIONA_HAS_BACKEND
    void handlePlayerSnapshotChanged(
        const seriona::control::PlayerStateSnapshot &player,
        const seriona::control::LibraryStateSnapshot &library);
    void handleLibrarySnapshotChanged(
        const seriona::control::PlayerStateSnapshot &player,
        const seriona::control::LibraryStateSnapshot &library);
    // F1.3：均衡器状态/频谱快照 → SettingsController 镜像属性（curvePoints/
    // curveFrequencies/spectrumBins）；快照经 bridge 访问器取回，主线程落地。
    void handleEqualizerStateChanged();
    void handleSpectrumChanged();
#endif

    PlaybackController m_playback;
    LibraryController m_library;
    LyricsModel m_lyrics;
    NotificationController m_notifications;
    NavigationController m_navigation;
    SettingsController m_settings;
    TrackStatsController m_trackStats;
    std::unique_ptr<BackendBridge> m_backendBridge;
    bool m_shuttingDown = false;
#if SERIONA_HAS_BACKEND
    std::unique_ptr<WaveformProvider> m_waveformProvider;
#endif
};

}
