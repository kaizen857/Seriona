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
    // 歌词行级纠错（W3，D14 菜单前三项）：经 BackendBridge 外发控制命令写 manual，
    // 约定值由桥层从当前 TrackLyricsSnapshot 取；不在前端直连 DB。返回是否被后端接受。
    // mock-only 下走本地不支持反馈，不伪造命令。
    Q_INVOKABLE bool upsertLyricSplitCorrection(const QString &rawText,
                                                const QString &original,
                                                const QString &translation);
    Q_INVOKABLE bool removeLyricSplitCorrection(const QString &rawText);
    // 行级右键「修正原文/译文」弹窗的提交门（A6）：与拖动路径共用
    // lyricOriginalIsSubmittable —— 原文为空（含全空白）时拒绝提交并返回 false，
    // QML 据此给出可见反馈（不发出命令）。返回是否真的提交了命令。
    Q_INVOKABLE bool commitLyricSplitCorrection(const QString &rawText,
                                                const QString &original,
                                                const QString &translation);
    // 提交门的判定本身（同一口径来源）：原文非空（含全空白判定）。
    // QML 用它决定是否给出「原文不能为空」的可见反馈，避免在 QML 里重写 trim 规则。
    Q_INVOKABLE bool isLyricOriginalSubmittable(const QString &original) const;
    // 整首纠错窗口保存按钮的启用条件（A4）：对**拖动结果**（boundaryIndex 换算出的两段）
    // 判定，而不是打开时那个展示对。未拖动由 touched 独立保证。QML 用它决定按钮可用性，
    // 与 commitLyricSplitBoundary 的 C++ 门同源（原文非空且构成真实变更）。
    Q_INVOKABLE bool lyricSplitBoundaryCommitAllowed(const QString &rawLine,
                                                     int boundaryIndex,
                                                     const QString &currentOriginal,
                                                     const QString &currentTranslation) const;
    // 整首纠错窗口（W3/D23）：把拖动分界换算成 (原文, 译文) 供拖动时实时预览。
    // 返回 {valid, original, translation}；纯换算，不发命令。QML 侧分界的像素定位
    // 由窗口用 TextMetrics 完成，字符索引→两段的换算复用同一 C++ 口径（可判负单测）。
    Q_INVOKABLE QVariantMap lyricSplitBoundaryParts(const QString &rawLine, int boundaryIndex) const;
    // 整首纠错窗口打开时定位初始分界：反解「能复现当前展示对 (original, translation)」的
    // 分界（返回 {valid, leftEnd, rightStart}）。纯换算，不发命令；分界字形不出现在前端。
    Q_INVOKABLE QVariantMap lyricSplitBoundaryCut(const QString &rawLine,
                                                  const QString &original,
                                                  const QString &translation) const;
    // 整首纠错窗口的提交入口：分界 → 换算 → 仅当与当前展示值不同才经
    // upsertLyricSplitCorrection 外发命令（无操作不发命令）。返回是否真的提交了命令。
    Q_INVOKABLE bool commitLyricSplitBoundary(const QString &rawLine,
                                              int boundaryIndex,
                                              const QString &currentOriginal,
                                              const QString &currentTranslation);

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
    // W3：当前曲目切分歌词快照 → LyricsModel（后端已切分，前端只透传渲染）。
    void handleTrackLyricsChanged();
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
